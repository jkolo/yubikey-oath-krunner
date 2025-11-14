/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "virtual_oath_device.h"

/**
 * @brief Virtual Nitrokey 3 OATH device emulator
 *
 * Emulates Nitrokey-specific OATH protocol behavior:
 * - LIST v1 format with properties byte
 * - Individual CALCULATE (0xA2) only (no CALCULATE_ALL support)
 * - Touch required via 0x6982 status word (not 0x6985)
 * - TAG_SERIAL_NUMBER (0x8F) included in SELECT response
 * - TAG_PROPERTY (0x78) uses Tag-Value format, NOT TLV: "78 02" (correct), not "78 01 02"
 *
 * Usage:
 * @code
 * VirtualNitrokey nitrokey("87654321", Version(1,6,0), "Nitrokey 3C");
 * nitrokey.addCredential(OathCredential("GitLab:admin", "ZYXWVUTSRQPONMLK"));
 * nitrokey.setTouchRequired(true);
 *
 * QByteArray selectResponse = nitrokey.handleSelect(selectApdu);
 * QByteArray listResponse = nitrokey.handleList(listApdu);
 * QByteArray calcResponse = nitrokey.handleCalculate(calcApdu);
 * @endcode
 */
class VirtualNitrokey : public VirtualOathDevice {
public:
    /**
     * @brief Construct virtual Nitrokey 3
     * @param serial Serial number (8 hex digits)
     * @param firmware Firmware version (e.g., 1.6.0 for NK3C)
     * @param modelName Model name (e.g., "Nitrokey 3C")
     */
    VirtualNitrokey(QString serial, Version firmware, QString modelName)
        : VirtualOathDevice(serial, firmware, parseSerialNumber(serial))
        , m_modelName(std::move(modelName))
    {}

    // Nitrokey-specific APDU handlers
    QByteArray handleSelect(const QByteArray& apdu) override;
    QByteArray handleList(const QByteArray& apdu) override;
    QByteArray handleCalculate(const QByteArray& apdu) override;
    QByteArray handleCalculateAll(const QByteArray& apdu) override;

    // Touch simulation
    void setTouchRequired(bool enabled) { m_touchRequired = enabled; }
    bool touchRequired() const { return m_touchRequired; }
    void simulateTouch() { m_touchPending = false; }
    void setPendingTouch() { m_touchPending = true; }

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
};
