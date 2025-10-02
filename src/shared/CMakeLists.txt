# YubiKey OATH - Shared Components
#
# This directory contains code shared between daemon, krunner, and config:
# - common/ - Core utilities (logging, result types)
# - dbus/ - D-Bus client library and types
# - types/ - Shared data structures (oath_credential)

# D-Bus client library (used by krunner and config)
add_subdirectory(dbus)
