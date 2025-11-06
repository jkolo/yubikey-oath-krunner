/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include <QMetaType>
#include <QDBusArgument>

namespace YubiKeyOath {
namespace Shared {

/**
 * @brief Represents a semantic version (major.minor.patch)
 *
 * Used primarily for YubiKey firmware version comparisons.
 */
class Version
{
public:
    /**
     * @brief Constructs a Version object
     * @param major Major version number
     * @param minor Minor version number
     * @param patch Patch version number
     */
    explicit Version(int major = 0, int minor = 0, int patch = 0) noexcept;

    /**
     * @brief Gets the major version number
     */
    [[nodiscard]] int major() const noexcept { return m_major; }

    /**
     * @brief Gets the minor version number
     */
    [[nodiscard]] int minor() const noexcept { return m_minor; }

    /**
     * @brief Gets the patch version number
     */
    [[nodiscard]] int patch() const noexcept { return m_patch; }

    /**
     * @brief Converts version to string format "major.minor.patch"
     */
    [[nodiscard]] QString toString() const;

    /**
     * @brief Parses version from string format "major.minor.patch"
     * @param versionString String in format "X.Y.Z"
     * @return Parsed Version object, or Version() if parsing fails
     */
    [[nodiscard]] static Version fromString(const QString &versionString);

    /**
     * @brief Checks if this version is valid (not 0.0.0)
     */
    [[nodiscard]] bool isValid() const noexcept;

    // Comparison operators
    [[nodiscard]] bool operator==(const Version& other) const noexcept;
    [[nodiscard]] bool operator!=(const Version& other) const noexcept;
    [[nodiscard]] bool operator<(const Version& other) const noexcept;
    [[nodiscard]] bool operator<=(const Version& other) const noexcept;
    [[nodiscard]] bool operator>(const Version& other) const noexcept;
    [[nodiscard]] bool operator>=(const Version& other) const noexcept;

private:
    int m_major;
    int m_minor;
    int m_patch;
};

} // namespace Shared
} // namespace YubiKeyOath

Q_DECLARE_METATYPE(YubiKeyOath::Shared::Version)

// D-Bus marshaling operators
QDBusArgument &operator<<(QDBusArgument &arg, const YubiKeyOath::Shared::Version &version);
const QDBusArgument &operator>>(const QDBusArgument &arg, YubiKeyOath::Shared::Version &version);
