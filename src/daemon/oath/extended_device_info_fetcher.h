/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QString>
#include <functional>
#include "common/result.h"
#include "shared/utils/version.h"
#include "management_protocol.h"
#include "oath_protocol.h"

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

/**
 * @brief Extended device information from Management/PIV APIs
 *
 * Matches the struct defined in yk_oath_session.h for backward compatibility.
 */
struct ExtendedDeviceInfo;  // Forward declaration

/**
 * @brief Fetches extended device information using multiple strategies
 *
 * This class encapsulates the complex logic of retrieving device information
 * from YubiKey/Nitrokey devices using multiple fallback strategies:
 *
 * - Strategy 0: OATH SELECT TAG_SERIAL_NUMBER (0x8F) - Nitrokey 3, fastest
 * - Strategy 1: Management GET DEVICE INFO - YubiKey 4.1+, most comprehensive
 * - Strategy 2: OTP GET_SERIAL - YubiKey NEO 3.x.x
 * - Strategy 3: PIV GET SERIAL - YubiKey NEO, 4, 5
 * - Strategy 4: OATH SELECT only - Final fallback (no serial)
 *
 * The class uses dependency injection for APDU transmission, allowing it
 * to be used with any OATH session implementation.
 */
class ExtendedDeviceInfoFetcher
{
public:
    /**
     * @brief Function type for sending APDU commands
     * @param command APDU command bytes
     * @return Response data including status word, or empty on error
     */
    using ApduSender = std::function<QByteArray(const QByteArray &command)>;

    /**
     * @brief Function type for parsing SELECT response
     * @param response SELECT response data
     * @param[out] deviceId Device ID from response
     * @param[out] challenge Challenge for authentication
     * @param[out] firmware Firmware version
     * @param[out] requiresPassword Whether password is required
     * @param[out] serialNumber Serial number (if present)
     * @return true if parsing succeeded
     */
    using SelectResponseParser = std::function<bool(
        const QByteArray &response,
        QString &deviceId,
        QByteArray &challenge,
        Version &firmware,
        bool &requiresPassword,
        quint32 &serialNumber
    )>;

    /**
     * @brief Constructs the fetcher with required dependencies
     * @param sendApdu Function to send APDU commands
     * @param parseSelectResponse Function to parse OATH SELECT response
     * @param deviceId Device ID for logging
     * @param selectSerialNumber Serial from initial SELECT (Strategy 0)
     * @param firmwareVersion Firmware from initial SELECT
     */
    ExtendedDeviceInfoFetcher(ApduSender sendApdu,
                              SelectResponseParser parseSelectResponse,
                              QString deviceId,
                              quint32 selectSerialNumber,
                              const Version &firmwareVersion);

    /**
     * @brief Fetches extended device information
     * @param readerName PC/SC reader name (used for NEO detection)
     * @return ExtendedDeviceInfo on success, error message on failure
     */
    [[nodiscard]] Result<ExtendedDeviceInfo> fetch(const QString &readerName = QString());

private:
    /**
     * @brief Strategy 1: Try Management GET DEVICE INFO (YubiKey 4.1+)
     * @param[out] info Extended device info to populate
     * @return true if strategy succeeded
     */
    [[nodiscard]] bool tryManagementApi(ExtendedDeviceInfo &info);

    /**
     * @brief Strategy 2: Fallback to OTP GET_SERIAL (YubiKey NEO 3.x.x)
     * @param readerName Reader name for NEO detection
     * @param[out] info Extended device info to populate
     * @return true if strategy succeeded
     */
    [[nodiscard]] bool tryOtpApi(const QString &readerName, ExtendedDeviceInfo &info);

    /**
     * @brief Strategy 3: Fallback to PIV GET SERIAL (YubiKey NEO, 4, 5)
     * @param[out] info Extended device info to populate
     * @return true if strategy succeeded
     */
    [[nodiscard]] bool tryPivApi(ExtendedDeviceInfo &info);

    /**
     * @brief Strategy 4: Final fallback - use OATH SELECT data only
     * @param[out] info Extended device info to populate
     * @return true if strategy succeeded
     */
    [[nodiscard]] bool tryOathSelectOnly(ExtendedDeviceInfo &info);

    /**
     * @brief Re-selects OATH application after using other applets
     * @return true if OATH re-selection succeeded
     */
    [[nodiscard]] bool reselectOath();

    /**
     * @brief Updates info.firmwareVersion from OATH SELECT response
     * @param[out] info Extended device info to update
     * @return true if parsing succeeded
     */
    [[nodiscard]] bool updateFirmwareFromOathSelect(ExtendedDeviceInfo &info);

    ApduSender m_sendApdu;
    SelectResponseParser m_parseSelectResponse;
    QString m_deviceId;
    quint32 m_selectSerialNumber;
    Version m_firmwareVersion;
};

} // namespace Daemon
} // namespace YubiKeyOath
