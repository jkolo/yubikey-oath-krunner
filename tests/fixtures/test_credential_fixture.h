/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "../../src/shared/types/oath_credential.h"
#include "../../src/shared/types/oath_credential_data.h"
#include <QString>

using namespace YubiKeyOath::Shared;

/**
 * @brief Factory for creating test OathCredential objects
 *
 * Provides pre-configured credential instances for consistent testing.
 * All methods are static for easy usage in tests.
 *
 * Usage:
 * @code
 * auto cred = TestCredentialFixture::createTotpCredential();
 * auto touchCred = TestCredentialFixture::createTouchCredential("Production:root");
 * @endcode
 */
class TestCredentialFixture {
public:
    /**
     * @brief Creates TOTP credential with default values
     * @param name Credential name (default: "GitHub:user")
     * @param secret Base32-encoded secret (default: JBSWY3DPEHPK3PXP = "Hello!")
     * @param digits Number of digits (default: 6)
     * @param period Validity period in seconds (default: 30)
     * @param algorithm Hash algorithm (default: SHA1)
     * @return Configured TOTP credential
     */
    static OathCredential createTotpCredential(
        const QString &name = QStringLiteral("GitHub:user"),
        const QString &secret = QStringLiteral("JBSWY3DPEHPK3PXP"), // "Hello!" in base32
        int digits = 6,
        int period = 30,
        OathAlgorithm algorithm = OathAlgorithm::SHA1
    ) {
        OathCredential cred;
        cred.originalName = name;
        cred.type = static_cast<int>(OathType::TOTP);
        cred.algorithm = static_cast<int>(algorithm);
        cred.digits = digits;
        cred.period = period;
        cred.requiresTouch = false;
        cred.deviceId = QStringLiteral("test-device");

        // Parse issuer:account from name
        if (name.contains(QLatin1Char(':'))) {
            int pos = name.indexOf(QLatin1Char(':'));
            cred.issuer = name.left(pos);
            cred.account = name.mid(pos + 1);
        } else {
            cred.issuer = QString();
            cred.account = name;
        }

        return cred;
    }

    /**
     * @brief Creates HOTP credential
     * @param name Credential name (default: "AWS:admin")
     * @param secret Base32-encoded secret
     * @param digits Number of digits (default: 6)
     * @param counter Initial counter value (default: 0)
     * @return Configured HOTP credential
     */
    static OathCredential createHotpCredential(
        const QString &name = QStringLiteral("AWS:admin"),
        const QString &secret = QStringLiteral("GEZDGNBVGY3TQOJQ"), // Different secret
        int digits = 6,
        quint64 counter = 0
    ) {
        OathCredential cred;
        cred.originalName = name;
        cred.type = static_cast<int>(OathType::HOTP);
        cred.algorithm = static_cast<int>(OathAlgorithm::SHA1);
        cred.digits = digits;
        cred.period = 0; // Not used for HOTP
        cred.requiresTouch = false;
        cred.deviceId = QStringLiteral("test-device");

        // Parse issuer:account
        if (name.contains(QLatin1Char(':'))) {
            int pos = name.indexOf(QLatin1Char(':'));
            cred.issuer = name.left(pos);
            cred.account = name.mid(pos + 1);
        } else {
            cred.issuer = QString();
            cred.account = name;
        }

        // Note: Counter would be stored separately in real implementation
        // For testing purposes, we just set the type to HOTP

        return cred;
    }

    /**
     * @brief Creates touch-required TOTP credential
     * @param name Credential name (default: "Production:root")
     * @return TOTP credential that requires physical touch
     */
    static OathCredential createTouchCredential(
        const QString &name = QStringLiteral("Production:root")
    ) {
        auto cred = createTotpCredential(name);
        cred.requiresTouch = true;
        return cred;
    }

    /**
     * @brief Creates TOTP credential with SHA256 algorithm
     * @param name Credential name
     * @return TOTP credential using SHA256
     */
    static OathCredential createSha256Credential(
        const QString &name = QStringLiteral("Microsoft:user@company.com")
    ) {
        return createTotpCredential(name, QStringLiteral("JBSWY3DPEHPK3PXP"),
                                   6, 30, OathAlgorithm::SHA256);
    }

    /**
     * @brief Creates TOTP credential with SHA512 algorithm
     * @param name Credential name
     * @return TOTP credential using SHA512
     */
    static OathCredential createSha512Credential(
        const QString &name = QStringLiteral("Enterprise:admin")
    ) {
        return createTotpCredential(name, QStringLiteral("JBSWY3DPEHPK3PXP"),
                                   6, 30, OathAlgorithm::SHA512);
    }

    /**
     * @brief Creates 8-digit TOTP credential
     * @param name Credential name
     * @return TOTP credential with 8 digits
     */
    static OathCredential create8DigitCredential(
        const QString &name = QStringLiteral("Banking:account")
    ) {
        return createTotpCredential(name, QStringLiteral("JBSWY3DPEHPK3PXP"), 8);
    }

    /**
     * @brief Creates list of diverse credentials for testing
     * @return List containing TOTP, HOTP, touch, various algorithms
     */
    static QList<OathCredential> createDiverseCredentialSet() {
        return {
            createTotpCredential(QStringLiteral("GitHub:user")),
            createTotpCredential(QStringLiteral("GitLab:admin")),
            createHotpCredential(QStringLiteral("AWS:console")),
            createTouchCredential(QStringLiteral("Production:root")),
            createSha256Credential(QStringLiteral("Microsoft:user")),
            create8DigitCredential(QStringLiteral("Bank:account"))
        };
    }

    /**
     * @brief Creates credential with specific device ID
     * @param deviceId Device identifier
     * @param name Credential name
     * @return Credential associated with specified device
     */
    static OathCredential createCredentialForDevice(
        const QString &deviceId,
        const QString &name = QStringLiteral("Test:credential")
    ) {
        auto cred = createTotpCredential(name);
        cred.deviceId = deviceId;
        return cred;
    }
};
