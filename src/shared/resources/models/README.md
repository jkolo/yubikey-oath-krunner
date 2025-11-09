# YubiKey Model Images

This directory contains model-specific images for different YubiKey models.

**YubiKey 5 Series:**
- YubiKey 5 NFC (USB-A + NFC)
- YubiKey 5C (USB-C only)
- YubiKey 5C NFC (USB-C + NFC)
- YubiKey 5 Nano (USB-A, compact)
- YubiKey 5C Nano (USB-C, compact)
- YubiKey 5Ci (USB-C + Lightning)

**Legacy Models:**
- YubiKey NEO (USB-A + NFC, first NFC model)
- YubiKey NEO-n (USB-A)
- YubiKey 4 (USB-A)


## Fallback Icons

If a specific model icon is not found, the resolver will:
1. Try series-level icon (e.g., `yubikey-5.svg` for any YubiKey 5)
2. Fall back to generic `yubikey.svg` icon
