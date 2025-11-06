/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "version.h"

namespace YubiKeyOath {
namespace Shared {

Version::Version(int major, int minor, int patch) noexcept
    : m_major(major)
    , m_minor(minor)
    , m_patch(patch)
{
}

QString Version::toString() const
{
    return QStringLiteral("%1.%2.%3").arg(m_major).arg(m_minor).arg(m_patch);
}

Version Version::fromString(const QString &versionString)
{
    QStringList const parts = versionString.split(QLatin1Char('.'));

    if (parts.size() != 3) {
        return Version();  // Return invalid version
    }

    bool ok1 = false;
    bool ok2 = false;
    bool ok3 = false;
    int const major = parts[0].toInt(&ok1);
    int const minor = parts[1].toInt(&ok2);
    int const patch = parts[2].toInt(&ok3);

    if (!ok1 || !ok2 || !ok3) {
        return Version();  // Return invalid version if parsing failed
    }

    return Version(major, minor, patch);
}

bool Version::isValid() const noexcept
{
    return m_major != 0 || m_minor != 0 || m_patch != 0;
}

bool Version::operator==(const Version& other) const noexcept
{
    return m_major == other.m_major &&
           m_minor == other.m_minor &&
           m_patch == other.m_patch;
}

bool Version::operator!=(const Version& other) const noexcept
{
    return !(*this == other);
}

bool Version::operator<(const Version& other) const noexcept
{
    if (m_major != other.m_major) {
        return m_major < other.m_major;
    }
    if (m_minor != other.m_minor) {
        return m_minor < other.m_minor;
    }
    return m_patch < other.m_patch;
}

bool Version::operator<=(const Version& other) const noexcept
{
    return *this < other || *this == other;
}

bool Version::operator>(const Version& other) const noexcept
{
    return !(*this <= other);
}

bool Version::operator>=(const Version& other) const noexcept
{
    return !(*this < other);
}

} // namespace Shared
} // namespace YubiKeyOath

// D-Bus marshaling operators
QDBusArgument &operator<<(QDBusArgument &arg, const YubiKeyOath::Shared::Version &version)
{
    arg.beginStructure();
    arg << version.major() << version.minor() << version.patch();
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, YubiKeyOath::Shared::Version &version)
{
    int major = 0;
    int minor = 0;
    int patch = 0;
    arg.beginStructure();
    arg >> major >> minor >> patch;
    arg.endStructure();
    version = YubiKeyOath::Shared::Version(major, minor, patch);
    return arg;
}
