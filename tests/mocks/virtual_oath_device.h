/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "../../src/daemon/oath/oath_protocol.h"
#include "../../src/shared/types/oath_credential.h"
#include "../../src/shared/common/result.h"
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QCryptographicHash>
#include <QtEndian>
#include <cmath>

using namespace YubiKeyOath::Daemon;
using namespace YubiKeyOath::Shared;

/**
 * @brief Base class for virtual OATH device emulators
 *
 * Provides common functionality for emulating OATH devices (YubiKey, Nitrokey).
 * Derived classes implement brand-specific APDU handling and protocol quirks.
 *
 * Usage:
 * @code
 * auto device = std::make_unique<VirtualYubiKey>("12345678", Version(5,4,2));
 * device->addCredential(OathCredential("GitHub:user", "JBSWY3DPEHPK3PXP"));
 * device->setPassword("mypassword");
 *
 * QByteArray response = device->processApdu(selectApdu);
 * @endcode
 */
class VirtualOathDevice {
public:
    virtual ~VirtualOathDevice() = default;

    /**
     * @brief Main APDU dispatcher - routes commands to appropriate handlers
     * @param apdu Command APDU bytes
     * @return Response APDU (data + status word)
     */
    QByteArray processApdu(const QByteArray& apdu) {
        if (apdu.size() < 4) {
            return createErrorResponse(OathProtocol::SW_WRONG_DATA);
        }

        quint8 ins = static_cast<quint8>(apdu[1]);
        quint8 p1 = static_cast<quint8>(apdu[2]);

        // INS 0xA4 is used for both SELECT (P1=0x04) and CALCULATE_ALL (P1=0x00/0x01)
        if (ins == 0xA4) {
            if (p1 == 0x04) {
                return handleSelect(apdu);
            } else {
                return handleCalculateAll(apdu);
            }
        }

        switch (ins) {
            case OathProtocol::INS_LIST:
                return handleList(apdu);
            case OathProtocol::INS_CALCULATE:
                return handleCalculate(apdu);
            case OathProtocol::INS_PUT:
                return handlePut(apdu);
            case OathProtocol::INS_DELETE:
                return handleDelete(apdu);
            case OathProtocol::INS_VALIDATE:
                return handleValidate(apdu);
            case OathProtocol::INS_SET_CODE:
                return handleSetCode(apdu);
            default:
                return createErrorResponse(OathProtocol::SW_INS_NOT_SUPPORTED);
        }
    }

    // Pure virtual APDU handlers (brand-specific implementations)
    virtual QByteArray handleSelect(const QByteArray& apdu) = 0;
    virtual QByteArray handleList(const QByteArray& apdu) = 0;
    virtual QByteArray handleCalculate(const QByteArray& apdu) = 0;
    virtual QByteArray handleCalculateAll(const QByteArray& apdu) = 0;

    // Common APDU handlers (shared implementation)
    virtual QByteArray handlePut(const QByteArray& apdu);
    virtual QByteArray handleDelete(const QByteArray& apdu);
    virtual QByteArray handleValidate(const QByteArray& apdu);
    virtual QByteArray handleSetCode(const QByteArray& apdu);

    // State management
    void setPassword(const QString& password);
    void addCredential(const OathCredential& cred);
    void removeCredential(const QString& name);
    QList<OathCredential> credentials() const;
    bool hasCredential(const QString& name) const;

    // Configuration
    QString deviceId() const { return m_deviceId; }
    Version firmwareVersion() const { return m_firmwareVersion; }
    quint32 serialNumber() const { return m_serialNumber; }
    bool isPasswordProtected() const { return !m_passwordKey.isEmpty(); }
    bool isAuthenticated() const { return m_authenticated; }
    bool isSessionActive() const { return m_sessionActive; }

protected:
    /**
     * @brief Protected constructor (only for derived classes)
     */
    VirtualOathDevice(QString deviceId, Version firmware, quint32 serial)
        : m_deviceId(std::move(deviceId))
        , m_firmwareVersion(firmware)
        , m_serialNumber(serial)
    {}

    /**
     * @brief Create error response with status word
     */
    QByteArray createErrorResponse(quint16 statusWord) const {
        QByteArray response;
        response.append(static_cast<char>((statusWord >> 8) & 0xFF));
        response.append(static_cast<char>(statusWord & 0xFF));
        return response;
    }

    /**
     * @brief Create success response (data + 0x9000)
     */
    QByteArray createSuccessResponse(const QByteArray& data = QByteArray()) const {
        QByteArray response = data;
        response.append(static_cast<char>(0x90));
        response.append(static_cast<char>(0x00));
        return response;
    }

    /**
     * @brief Calculate TOTP code for credential
     */
    QString calculateTotpCode(const OathCredential& cred, quint64 timestamp) const;

    /**
     * @brief Calculate HOTP code for credential
     */
    QString calculateHotpCode(const OathCredential& cred, quint64 counter) const;

    /**
     * @brief Derive password key using PBKDF2
     */
    QByteArray derivePasswordKey(const QString& password, const QByteArray& salt) const;

    // State
    QString m_deviceId;
    Version m_firmwareVersion;
    quint32 m_serialNumber;
    QByteArray m_passwordKey;           // PBKDF2 derived key
    QByteArray m_lastChallenge;         // Challenge from SELECT
    QMap<QString, OathCredential> m_credentials;
    bool m_authenticated = false;
    bool m_sessionActive = false;
};
