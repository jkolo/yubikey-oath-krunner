/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "virtual_oath_device.h"

/**
 * @brief Virtual YubiKey OATH device emulator
 *
 * Emulates YubiKey-specific OATH protocol behavior:
 * - CALCULATE_ALL (0xA4) for bulk code generation
 * - Touch required via 0x6985 status word
 * - LIST command may spuriously return 0x6985 (known YubiKey bug)
 * - Serial number retrieved via Management API (not in SELECT)
 * - LIST v0 format (no properties byte)
 *
 * Usage:
 * @code
 * VirtualYubiKey yubikey("12345678", Version(5,4,2), "YubiKey 5C NFC");
 * yubikey.addCredential(OathCredential("GitHub:user", "JBSWY3DPEHPK3PXP"));
 * yubikey.setTouchRequired(true);
 *
 * QByteArray selectResponse = yubikey.handleSelect(selectApdu);
 * QByteArray calcAllResponse = yubikey.handleCalculateAll(calcAllApdu);
 * @endcode
 */
class VirtualYubiKey : public VirtualOathDevice {
public:
    /**
     * @brief Construct virtual YubiKey
     * @param serial Serial number (8 hex digits)
     * @param firmware Firmware version
     * @param modelName Model name (e.g., "YubiKey 5C NFC")
     */
    VirtualYubiKey(QString serial, Version firmware, QString modelName)
        : VirtualOathDevice(serial, firmware, parseSerialNumber(serial))
        , m_modelName(std::move(modelName))
    {}

    // YubiKey-specific APDU handlers
    QByteArray handleSelect(const QByteArray& apdu) override;
    QByteArray handleList(const QByteArray& apdu) override;
    QByteArray handleCalculate(const QByteArray& apdu) override;
    QByteArray handleCalculateAll(const QByteArray& apdu) override;

    // Touch simulation
    void setTouchRequired(bool enabled) { m_touchRequired = enabled; }
    bool touchRequired() const { return m_touchRequired; }
    void simulateTouch() { m_touchPending = false; }
    void setPendingTouch() { m_touchPending = true; }

    // Bug emulation control
    void setEmulateListBug(bool enabled) { m_emulateListBug = enabled; }
    bool emulateListBug() const { return m_emulateListBug; }

    // Model info
    QString modelName() const { return m_modelName; }

private:
    static quint32 parseSerialNumber(const QString& serial) {
        bool ok;
        quint32 sn = serial.toUInt(&ok, 16);
        return ok ? sn : 0;
    }

    QString m_modelName;
    bool m_touchRequired = false;
    bool m_touchPending = false;
    bool m_emulateListBug = true; // Emulate YubiKey LIST bug by default
};
