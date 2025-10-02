# Translation Guide (i18n)

This document describes how to work with translations in the KRunner YubiKey OATH Plugin.

## Overview

The plugin uses KDE's i18n (internationalization) system based on gettext. All user-facing strings are wrapped in `i18n()` functions and can be translated into multiple languages.

## Current Translation Status

- **English (en)** - Source language (built-in)
- **Polish (pl)** - Complete translation ✅

## File Structure

```
krunner-yoath.cpp/
├── scripts/
│   └── messages.sh          # String extraction script
├── src/
│   └── shared/
│       └── po/              # Translation directory
│           ├── CMakeLists.txt      # Translation build configuration
│           ├── krunner_yubikey.pot # Template file (all translatable strings)
│           └── pl.po               # Polish translation
└── src/                     # Source files with i18n() calls
```

## For Developers

### Adding New Translatable Strings

When adding user-visible text to the code, wrap it in `i18n()`:

**C++ Code:**
```cpp
#include <KLocalizedString>

// Simple string
QString message = i18n("Code copied to clipboard");

// String with variable
QString text = i18n("Code for %1", accountName);

// String with context for translators
QString keyword = i18nc("Note this is a KRunner keyword", "yubikey");

// Plural forms
QString time = i18np("Expires in %1 second", "Expires in %1 seconds", count);
```

**QML Code:**
```qml
import org.kde.i18n

Text {
    text: i18n("Device ID: %1", deviceId)
}

Label {
    text: i18n("Connected")
}
```

### Updating Translation Template

After adding new i18n() strings, regenerate the .pot file:

```bash
cd /path/to/krunner-yoath.cpp
./scripts/messages.sh
```

This creates/updates `src/shared/po/krunner_yubikey.pot` with all translatable strings.

### Testing Translations

**Test Polish translation:**
```bash
LANGUAGE=pl_PL LANG=pl_PL.UTF-8 krunner --replace
```

**Test with English (default):**
```bash
LANGUAGE=en_US LANG=en_US.UTF-8 krunner --replace
```

## For Translators

### Creating a New Translation

1. **Initialize translation file:**
   ```bash
   cd src/shared/po/
   msginit -l <LANG> -i krunner_yubikey.pot -o <LANG>.po
   # Example for German: msginit -l de -i krunner_yubikey.pot -o de.po
   ```

2. **Edit the .po file:**

   Use a PO editor:
   - **Lokalize** (KDE): `lokalize de.po`
   - **Poedit**: GUI-based editor
   - **Text editor**: Edit msgstr lines manually

   Example entry:
   ```po
   #: src/runner/notification_orchestrator.cpp:72
   #, kde-format
   msgid "YubiKey OATH Code"
   msgstr "YubiKey OATH-Code"  # German translation
   ```

3. **Handle plural forms:**
   ```po
   msgid "Expires in %1 second"
   msgid_plural "Expires in %1 seconds"
   msgstr[0] "Läuft in %1 Sekunde ab"     # Singular
   msgstr[1] "Läuft in %1 Sekunden ab"   # Plural
   ```

4. **Update header information:**
   ```po
   "Last-Translator: Your Name <your@email.com>\n"
   "Language-Team: German <de@li.org>\n"
   "Language: de\n"
   ```

### Updating Existing Translation

When new strings are added to the code:

```bash
cd src/shared/po/
msgmerge -U de.po krunner_yubikey.pot
# Edit de.po and translate new strings
```

### Compiling Translation

Translations are compiled automatically during build, but to test manually:

```bash
msgfmt de.po -o krunner_yubikey.mo
sudo cp krunner_yubikey.mo /usr/share/locale/de/LC_MESSAGES/
```

## Build System Integration

### CMake Configuration

The `po/CMakeLists.txt` file handles translation installation:

```cmake
ki18n_install(po)
```

The main `CMakeLists.txt` includes the po subdirectory:

```cmake
add_subdirectory(po)
```

### Automatic Compilation

When you build the project, translations are automatically:
1. Compiled from .po to .mo files
2. Installed to `/usr/share/locale/<LANG>/LC_MESSAGES/krunner_yubikey.mo`

```bash
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr
cmake --build .
sudo cmake --install .
```

## Translation Coverage

### Current Translated Strings

The plugin translates:
- Notification messages (code display, touch prompts, timeouts)
- Error messages (authentication failures, clipboard errors)
- UI labels (device status, buttons, dialogs)
- Action names (copy, type)
- Configuration dialog text

### String Categories

**Notifications (high priority):**
- "Code copied to clipboard"
- "Touch your YubiKey"
- "Expires in X seconds"

**UI Elements (medium priority):**
- Device status: "Connected", "Not connected"
- Password dialog: "Authorize YubiKey"
- Settings: "Device ID:", "Password required"

**Error Messages (high priority):**
- "YubiKey OATH authentication failed"
- "Invalid password"
- "Touch operation timed out"

## Quality Guidelines

### For Translators

1. **Consistency**: Use the same terminology throughout
   - "YubiKey" - never translate (product name)
   - "OATH" - never translate (standard name)
   - "TOTP/HOTP" - never translate (technical terms)

2. **Context**: Read the source location comments
   ```po
   #: src/runner/notification_orchestrator.cpp:105
   #, kde-format
   msgid "Please touch your YubiKey..."
   ```

3. **Placeholders**: Preserve %1, %2, etc.
   ```po
   msgid "Code for %1"
   msgstr "Kod dla %1"  # Keep %1 in place
   ```

4. **Line breaks**: Preserve \n characters
   ```po
   msgid "Line 1\nLine 2"
   msgstr "Linia 1\nLinia 2"
   ```

5. **HTML tags**: Preserve <b>, </b> tags
   ```po
   msgid "<b>Bold text</b>"
   msgstr "<b>Pogrubiony tekst</b>"
   ```

### For Developers

1. **Extract context**: Use i18nc() for ambiguous strings
   ```cpp
   // Bad - "OK" could mean many things
   i18n("OK")

   // Good - translator knows this is a button
   i18nc("@action:button", "OK")
   ```

2. **Avoid concatenation**: Use placeholders
   ```cpp
   // Bad - word order varies by language
   QString text = i18n("Code") + ": " + code;

   // Good - translator can reorder
   QString text = i18n("Code: %1", code);
   ```

3. **Use plurals correctly**:
   ```cpp
   // Bad - incorrect for many languages
   QString text = QString::number(n) + i18n(" seconds");

   // Good - handles all plural forms
   QString text = i18np("%1 second", "%1 seconds", n);
   ```

## KDE Translation Infrastructure

### Submitting to KDE

If you want translations to be handled by KDE translation teams:

1. **Join KDE Incubator**: https://invent.kde.org/
2. **Configure on l10n.kde.org**: https://l10n.kde.org/
3. **Translation teams download .pot**
4. **Teams submit .po files**
5. **Incorporate translations into releases**

### Standalone Translations

For translations outside KDE infrastructure:
1. Translators edit .po files directly
2. Submit via GitHub pull requests
3. Maintainer reviews and merges
4. Included in next release

## Troubleshooting

### Translations Not Showing

**Check locale:**
```bash
locale  # Should show pl_PL.UTF-8 for Polish
```

**Check .mo file installed:**
```bash
ls -la /usr/share/locale/pl/LC_MESSAGES/krunner_yubikey.mo
```

**Force locale for testing:**
```bash
LANGUAGE=pl_PL LANG=pl_PL.UTF-8 krunner --replace
```

### Missing Strings

**Regenerate template:**
```bash
./scripts/messages.sh
msgmerge -U src/shared/po/pl.po src/shared/po/krunner_yubikey.pot
# Translate new entries in pl.po
```

**Rebuild and reinstall:**
```bash
cd build
cmake --build .
sudo cmake --install .
```

### Encoding Issues

All .po files must be UTF-8:
```po
"Content-Type: text/plain; charset=UTF-8\n"
```

Check with:
```bash
file po/pl.po  # Should say "UTF-8 Unicode text"
```

## Resources

- **KDE i18n Guide**: https://develop.kde.org/docs/plasma/widget/translations-i18n/
- **Gettext Manual**: https://www.gnu.org/software/gettext/manual/
- **Lokalize Documentation**: https://docs.kde.org/stable5/en/lokalize/lokalize/
- **KDE Localization**: https://l10n.kde.org/

## Example Translation Workflow

### Adding a New Feature

1. **Developer adds feature with i18n:**
   ```cpp
   notification->setText(i18n("Feature enabled"));
   ```

2. **Update translation template:**
   ```bash
   ./scripts/messages.sh
   ```

3. **Translator updates Polish:**
   ```bash
   cd src/shared/po/
   msgmerge -U pl.po krunner_yubikey.pot
   lokalize pl.po  # Translate new string
   ```

4. **Build and test:**
   ```bash
   cd ../build
   cmake --build .
   sudo cmake --install .
   LANGUAGE=pl_PL krunner --replace
   ```

## Contributing Translations

We welcome translations to all languages! To contribute:

1. **Fork the repository**
2. **Create/update your language .po file**
3. **Test the translation**
4. **Submit a pull request**

Include in your PR:
- Updated .po file
- Screenshot showing translated UI (optional but helpful)
- Your name for credits

---

**Questions?** Open an issue on GitHub or contact the maintainers.
