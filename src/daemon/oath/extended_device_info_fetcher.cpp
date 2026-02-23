/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "extended_device_info_fetcher.h"
#include "yk_oath_session.h"  // For ExtendedDeviceInfo definition
#include "../logging_categories.h"
#include "../utils/secure_logging.h"
#include "yk_oath_protocol.h"

namespace YubiKeyOath {
namespace Daemon {

ExtendedDeviceInfoFetcher::ExtendedDeviceInfoFetcher(
    ApduSender sendApdu,
    SelectResponseParser parseSelectResponse,
    QString deviceId,  // NOLINT(modernize-pass-by-value) - QString has implicit sharing
    quint32 selectSerialNumber,
    const Version &firmwareVersion)
    : m_sendApdu(std::move(sendApdu))
    , m_parseSelectResponse(std::move(parseSelectResponse))
    , m_deviceId(std::move(deviceId))
    , m_selectSerialNumber(selectSerialNumber)
    , m_firmwareVersion(firmwareVersion)
{
}

Result<ExtendedDeviceInfo> ExtendedDeviceInfoFetcher::fetch(const QString &readerName)
{
    qCDebug(YubiKeyOathDeviceLog) << "ExtendedDeviceInfoFetcher::fetch() for device" << m_deviceId;

    ExtendedDeviceInfo info;

    // Strategy 0: OATH SELECT TAG_SERIAL_NUMBER (0x8F) - Nitrokey 3, fastest (no extra APDU)
    // If serial is already available from SELECT, use it (Nitrokey 3 supports this)
    // YubiKeys don't send this tag, so they'll use fallback strategies below
    if (m_selectSerialNumber != 0) {
        info.serialNumber = m_selectSerialNumber;
        qCInfo(YubiKeyOathDeviceLog) << "Serial from OATH SELECT TAG_SERIAL_NUMBER (0x8F):"
                                     << SecureLogging::maskSerial(m_selectSerialNumber);
    }

    // Strategy 1: Try Management GET DEVICE INFO (YubiKey 4.1+)
    if (tryManagementApi(info)) {
        return Result<ExtendedDeviceInfo>::success(info);
    }

    // Early return for Nitrokey: If we have serial from Strategy #0, skip OTP/PIV
    // (those strategies ONLY provide serial number, which we already have)
    if (m_selectSerialNumber != 0) {
        if (!reselectOath()) {
            return Result<ExtendedDeviceInfo>::error(
                QStringLiteral("Failed to re-select OATH application"));
        }

        info.firmwareVersion = m_firmwareVersion;  // From SELECT
        info.deviceModel = detectYubiKeyModel(info.firmwareVersion, QString());
        info.formFactor = 0;  // Unavailable (Nitrokey doesn't support Management API)

        qCInfo(YubiKeyOathDeviceLog) << "Using serial from TAG_SERIAL_NUMBER (Strategy #0):"
                                     << "serial=" << SecureLogging::maskSerial(info.serialNumber)
                                     << "firmware=" << info.firmwareVersion.toString()
                                     << "(skipping OTP/PIV fallbacks - not needed)";

        return Result<ExtendedDeviceInfo>::success(info);
    }

    // Strategy 2: Fallback to OTP GET_SERIAL (YubiKey NEO 3.x.x)
    if (tryOtpApi(readerName, info)) {
        return Result<ExtendedDeviceInfo>::success(info);
    }

    // Strategy 3: Fallback to PIV GET SERIAL (YubiKey NEO, 4, 5)
    if (tryPivApi(info)) {
        return Result<ExtendedDeviceInfo>::success(info);
    }

    // Strategy 4: Final fallback - use OATH SELECT data only
    if (tryOathSelectOnly(info)) {
        return Result<ExtendedDeviceInfo>::success(info);
    }

    return Result<ExtendedDeviceInfo>::error(
        QStringLiteral("Failed to get extended device info"));
}

bool ExtendedDeviceInfoFetcher::tryManagementApi(ExtendedDeviceInfo &info)
{
    qCDebug(YubiKeyOathDeviceLog) << "Attempting Management GET DEVICE INFO";

    // Select Management application
    const QByteArray selectMgmtCmd = ManagementProtocol::createSelectCommand();
    const QByteArray selectMgmtResp = m_sendApdu(selectMgmtCmd);

    if (selectMgmtResp.isEmpty() ||
        !ManagementProtocol::isSuccess(ManagementProtocol::getStatusWord(selectMgmtResp))) {
        qCDebug(YubiKeyOathDeviceLog) << "Management application not available";
        return false;
    }

    // Get device info
    const QByteArray getInfoCmd = ManagementProtocol::createGetDeviceInfoCommand();
    const QByteArray getInfoResp = m_sendApdu(getInfoCmd);

    if (getInfoResp.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "Empty response from Management GET DEVICE INFO";
        return false;
    }

    ManagementDeviceInfo mgmtInfo;
    if (!ManagementProtocol::parseDeviceInfoResponse(getInfoResp, mgmtInfo)) {
        qCDebug(YubiKeyOathDeviceLog) << "Failed to parse Management GET DEVICE INFO response";
        return false;
    }

    // Success! Got comprehensive device info
    // Only override serial if Management API returned non-zero value
    // (preserves serial from Strategy 0: TAG_SERIAL_NUMBER for Nitrokey)
    if (mgmtInfo.serialNumber != 0) {
        info.serialNumber = mgmtInfo.serialNumber;
    }
    info.firmwareVersion = mgmtInfo.firmwareVersion;
    info.formFactor = mgmtInfo.formFactor;

    // Derive device model from firmware, form factor, and NFC support
    info.deviceModel = detectYubiKeyModel(
        info.firmwareVersion, QString(), info.formFactor, mgmtInfo.nfcSupported);

    qCInfo(YubiKeyOathDeviceLog) << "Management GET DEVICE INFO succeeded:"
                                 << "serial=" << SecureLogging::maskSerial(info.serialNumber)
                                 << "firmware=" << info.firmwareVersion.toString()
                                 << "formFactor=" << info.formFactor
                                 << "nfcSupported=" << mgmtInfo.nfcSupported
                                 << "detectedModel=" << QString::number(info.deviceModel, 16);

    // CRITICAL: Re-select OATH application to restore session
    if (!reselectOath()) {
        qCWarning(YubiKeyOathDeviceLog) << "Failed to re-select OATH after Management";
        return false;
    }

    return true;
}

bool ExtendedDeviceInfoFetcher::tryOtpApi(const QString &readerName, ExtendedDeviceInfo &info)
{
    qCDebug(YubiKeyOathDeviceLog) << "Attempting OTP GET_SERIAL";

    // Select OTP application
    const QByteArray selectOtpCmd = OathProtocol::createSelectOtpCommand();
    const QByteArray selectOtpResp = m_sendApdu(selectOtpCmd);

    if (selectOtpResp.isEmpty() ||
        !OathProtocol::isSuccess(OathProtocol::getStatusWord(selectOtpResp))) {
        qCDebug(YubiKeyOathDeviceLog) << "OTP application not available";
        return false;
    }

    // Get serial number
    const QByteArray getSerialCmd = OathProtocol::createOtpGetSerialCommand();
    const QByteArray getSerialResp = m_sendApdu(getSerialCmd);

    if (getSerialResp.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "Empty response from OTP GET_SERIAL";
        // Re-select OATH before returning (best-effort, ignore failures)
        (void)reselectOath();
        return false;
    }

    quint32 serial = 0;
    if (!OathProtocol::parseOtpSerialResponse(getSerialResp, serial)) {
        qCDebug(YubiKeyOathDeviceLog) << "Failed to parse OTP GET_SERIAL response";
        (void)reselectOath();
        return false;
    }

    // Success! Got serial number
    // Only override if OTP returned non-zero value
    if (serial != 0) {
        info.serialNumber = serial;
    }

    // Parse reader name for NEO detection (Yubico method)
    auto readerInfo = OathProtocol::parseReaderNameInfo(readerName);
    if (readerInfo.valid && readerInfo.isNEO) {
        // YubiKey NEO detected via reader name
        info.formFactor = readerInfo.formFactor;  // USB_A_KEYCHAIN (0x01)

        // Use firmware Version(3, 4, 0) as default for NEO
        // (OATH SELECT returns OATH app version 0.2.1, not device firmware)
        info.firmwareVersion = Version(3, 4, 0);

        // Detect model with NEO series (firmware 3.x.x → YubiKeyNEO)
        info.deviceModel = detectYubiKeyModel(info.firmwareVersion, QString(), info.formFactor);

        qCInfo(YubiKeyOathDeviceLog) << "OTP GET_SERIAL + reader name parsing succeeded:"
                                     << "serial=" << SecureLogging::maskSerial(info.serialNumber)
                                     << "model=NEO formFactor=" << info.formFactor
                                     << "firmware=" << info.firmwareVersion.toString();

        return true;
    }

    // Non-NEO device: get firmware from OATH SELECT
    if (!reselectOath()) {
        return false;
    }

    if (!updateFirmwareFromOathSelect(info)) {
        return false;
    }

    // Derive device model from firmware only (no form factor available)
    info.formFactor = 0;  // Unavailable via OTP
    info.deviceModel = detectYubiKeyModel(info.firmwareVersion, QString(), info.formFactor);

    qCInfo(YubiKeyOathDeviceLog) << "OTP GET_SERIAL succeeded:"
                                 << "serial=" << SecureLogging::maskSerial(info.serialNumber)
                                 << "firmware=" << info.firmwareVersion.toString();

    return true;
}

bool ExtendedDeviceInfoFetcher::tryPivApi(ExtendedDeviceInfo &info)
{
    qCDebug(YubiKeyOathDeviceLog) << "Attempting PIV GET SERIAL";

    // Select PIV application
    const QByteArray selectPivCmd = OathProtocol::createSelectPivCommand();
    const QByteArray selectPivResp = m_sendApdu(selectPivCmd);

    if (selectPivResp.isEmpty() ||
        !OathProtocol::isSuccess(OathProtocol::getStatusWord(selectPivResp))) {
        qCDebug(YubiKeyOathDeviceLog) << "PIV application not available";
        return false;
    }

    // Get serial number
    const QByteArray getSerialCmd = OathProtocol::createGetSerialCommand();
    const QByteArray getSerialResp = m_sendApdu(getSerialCmd);

    if (getSerialResp.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "Empty response from PIV GET SERIAL";
        (void)reselectOath();
        return false;
    }

    quint32 serial = 0;
    if (!OathProtocol::parseSerialResponse(getSerialResp, serial)) {
        qCDebug(YubiKeyOathDeviceLog) << "Failed to parse PIV GET SERIAL response";
        (void)reselectOath();
        return false;
    }

    // Success! Got serial number
    // Only override if PIV returned non-zero value
    if (serial != 0) {
        info.serialNumber = serial;
    }

    // Get firmware from OATH SELECT
    if (!reselectOath()) {
        return false;
    }

    if (!updateFirmwareFromOathSelect(info)) {
        return false;
    }

    // Derive device model from firmware only (no form factor available)
    info.formFactor = 0;  // Unavailable via PIV
    info.deviceModel = detectYubiKeyModel(info.firmwareVersion, QString());

    qCInfo(YubiKeyOathDeviceLog) << "PIV GET SERIAL succeeded:"
                                 << "serial=" << SecureLogging::maskSerial(info.serialNumber)
                                 << "firmware=" << info.firmwareVersion.toString();

    return true;
}

bool ExtendedDeviceInfoFetcher::tryOathSelectOnly(ExtendedDeviceInfo &info)
{
    qCDebug(YubiKeyOathDeviceLog) << "Using OATH SELECT data as final fallback";

    // Get firmware from OATH SELECT
    const QByteArray selectOathCmd = OathProtocol::createSelectCommand();
    const QByteArray selectOathResp = m_sendApdu(selectOathCmd);

    if (selectOathResp.isEmpty() ||
        !OathProtocol::isSuccess(OathProtocol::getStatusWord(selectOathResp))) {
        qCWarning(YubiKeyOathDeviceLog) << "Failed to execute OATH SELECT";
        return false;
    }

    // Parse firmware from response
    QString deviceId;
    QByteArray challenge;
    Version firmware;
    bool requiresPassword = false;
    quint32 serialNumber = 0;

    const QByteArray selectData = selectOathResp.left(selectOathResp.length() - 2);
    if (!m_parseSelectResponse(selectData, deviceId, challenge, firmware,
                               requiresPassword, serialNumber)) {
        qCWarning(YubiKeyOathDeviceLog) << "Failed to parse OATH SELECT response";
        return false;
    }

    info.serialNumber = 0;  // Unavailable (final fallback - no serial available)
    info.firmwareVersion = firmware;
    info.deviceModel = detectYubiKeyModel(firmware, QString());
    info.formFactor = 0;  // Unavailable

    qCInfo(YubiKeyOathDeviceLog) << "Final fallback succeeded (no serial available):"
                                 << "firmware=" << info.firmwareVersion.toString();

    return true;
}

bool ExtendedDeviceInfoFetcher::reselectOath()
{
    const QByteArray selectOathCmd = OathProtocol::createSelectCommand();
    const QByteArray selectOathResp = m_sendApdu(selectOathCmd);

    if (selectOathResp.isEmpty() ||
        !OathProtocol::isSuccess(OathProtocol::getStatusWord(selectOathResp))) {
        qCWarning(YubiKeyOathDeviceLog) << "Failed to re-select OATH application";
        return false;
    }

    return true;
}

bool ExtendedDeviceInfoFetcher::updateFirmwareFromOathSelect(ExtendedDeviceInfo &info)
{
    const QByteArray selectOathCmd = OathProtocol::createSelectCommand();
    const QByteArray selectOathResp = m_sendApdu(selectOathCmd);

    if (selectOathResp.isEmpty() ||
        !OathProtocol::isSuccess(OathProtocol::getStatusWord(selectOathResp))) {
        qCWarning(YubiKeyOathDeviceLog) << "Failed to get OATH SELECT for firmware";
        return false;
    }

    // Parse firmware from OATH SELECT response
    QString deviceId;
    QByteArray challenge;
    Version firmware;
    bool requiresPassword = false;
    quint32 serialFromSelect = 0;

    // Don't strip status word - parseSelectResponse() handles it internally
    if (!m_parseSelectResponse(selectOathResp, deviceId, challenge, firmware,
                               requiresPassword, serialFromSelect)) {
        qCWarning(YubiKeyOathDeviceLog) << "Failed to parse OATH SELECT for firmware";
        return false;
    }

    info.firmwareVersion = firmware;
    return true;
}

} // namespace Daemon
} // namespace YubiKeyOath
