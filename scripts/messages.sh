#!/bin/sh
# SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# Extract translatable strings from source code
$EXTRACTRC `find . -name \*.rc -o -name \*.ui -o -name \*.kcfg` >> rc.cpp || true
$XGETTEXT `find . -name \*.cpp -o -name \*.h -o -name \*.qml` -o $podir/krunner_yubikey.pot
rm -f rc.cpp
