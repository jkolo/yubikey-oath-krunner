# YubiKey OATH KRunner Plugin - Root Source CMakeLists
#
# Build order matters:
# 1. shared - Shared libraries (dbus, common, types) used by all components
# 2. daemon - D-Bus service daemon
# 3. krunner - KRunner plugin (uses shared components)
# 4. config - KCM configuration module (uses shared components)
# 5. po - Project translations

# Shared components (common, dbus, types, config, resources)
add_subdirectory(shared)

# D-Bus service daemon
add_subdirectory(daemon)

# KRunner plugin
add_subdirectory(krunner)

# Configuration module
add_subdirectory(config)

# Translations
add_subdirectory(shared/po)
