# Nitrokey 3 OATH Protocol Documentation

## Overview

This document describes the Nitrokey 3 OATH implementation based on `trussed-secrets-app` v0.14.0. Nitrokey 3 implements a YubiKey-compatible OATH protocol with several key differences.

**Source:** https://github.com/Nitrokey/trussed-secrets-app

## Key Differences from YubiKey

### 1. Serial Number Location

**Nitrokey 3:**
- ✅ Serial number available in OATH SELECT response
- Tag: `TAG_SERIAL_NUMBER (0x8F)`
- Length: 4 bytes (big-endian uint32)
- Example: `8F 04 21 8A 71 5F` → serial 562721119

**YubiKey:**
- ❌ Serial NOT in OATH SELECT
- Requires Management API (YubiKey 4.1+) or PIV/OTP fallback
- Strategy: Management GET DEVICE INFO → OTP GET_SERIAL → PIV GET_DATA

**Detection Strategy:**
```cpp
// Nitrokey detection (Strategy #0)
if (selectResponse.contains(TAG_SERIAL_NUMBER)) {
    brand = DeviceBrand::Nitrokey;
    serialNumber = parseSerialFromTag(selectResponse);
}
```

### 2. CALCULATE_ALL Availability

**Nitrokey 3:**
- ⚠️ Feature-gated with `calculate-all` Cargo feature
- May NOT be available in production firmware
- Test: Send CALCULATE_ALL (0xA4), check for `0x6D00` (INS_NOT_SUPPORTED)

**Implementation from `command.rs:920-922`:**
```rust
#[cfg(feature = "calculate-all")]
(0x00, oath::Instruction::CalculateAll, 0x00, 0x01) => {
    Self::CalculateAll(CalculateAll::try_from(data)?)
}
```

**Fallback Required:**
If CALCULATE_ALL returns `0x6D00`, use:
1. LIST (0xA1) to get credential names
2. Multiple CALCULATE (0xA2) for each credential

### 3. Touch Requirement Handling

**Nitrokey 3:**
- Touch checked **BEFORE** CALCULATE command execution
- Returns `0x6982` (SecurityStatusNotSatisfied) on touch timeout
- Implementation: `require_touch_if_needed()` calls `user_present()`

**From `authenticator.rs:887-895`:**
```rust
fn require_touch_if_needed(&mut self, credential: &CredentialFlat) -> Result<()> {
    if credential.touch_required
        && credential.encryption_key_type.unwrap() != EncryptionKeyType::PinBased
    {
        self.user_present()?;  // ← Blocks until touch or timeout
    }
    Ok(())
}
```

**YubiKey:**
- Touch checked **DURING** APDU processing
- Returns `0x6985` (ConditionsNotSatisfied) on touch requirement
- Client must re-send CALCULATE after user touch

**Comparison Table:**

| Behavior | YubiKey | Nitrokey 3 |
|----------|---------|------------|
| Touch timing | During CALCULATE | Before CALCULATE |
| Status word | `0x6985` | `0x6982` |
| Retry strategy | Re-send same CALCULATE | Re-send CALCULATE |

**Client Implementation:**
```cpp
if (sw == 0x6985 || sw == 0x6982) {  // Both YubiKey and Nitrokey
    emit touchRequired();
    return Result::error("Touch required");
}
```

### 4. LIST Command Reliability

**Nitrokey 3:**
- ✅ LIST works correctly without false TOUCH REQUIRED errors
- Can be used as primary credential enumeration method

**YubiKey:**
- ❌ LIST sometimes returns spurious TOUCH REQUIRED (0x6985)
- CALCULATE_ALL preferred to avoid this issue

**Source from YubiKey testing:**
- YubiKey NEO/4/5 LIST may trigger touch requirement unexpectedly
- CALCULATE_ALL (0xA4) introduced to work around this limitation

### 5. Firmware Version Format

**Nitrokey 3:**
- Reports version `4.x.x` for YubiKey compatibility
- Actual version from `trussed-secrets-app` Cargo.toml
- Example: `4.14.0` (v0.14.0 of trussed-secrets-app)

**From `authenticator.rs:121-154`:**
```rust
const VERSION: OathVersion = {
    OathVersion {
        major: 4,  // ← Always 4 for compatibility
        minor: crate_minor,
        patch: crate_patch,
    }
};
```

**YubiKey:**
- Real firmware version: 5.x.x (YubiKey 5), 4.x.x (YubiKey 4), 3.x.x (NEO)

### 6. Management Interface

**Nitrokey 3:**
- ❌ YubiKey Management API NOT supported
- Returns `0x6A82` (FileNotFound) on Management SELECT
- No GET DEVICE INFO, no form factor detection

**YubiKey:**
- ✅ Management API available (YubiKey 4.1+)
- Provides: serial, firmware, form factor, USB/NFC capabilities

## Supported Features (100% Compatible)

All core OATH commands work identically:

### Commands
- `SELECT (0xA4)` - Select OATH application
- `LIST (0xA1)` - List all credentials
- `CALCULATE (0xA2)` - Calculate single TOTP/HOTP code
- `PUT (0x01)` - Add/update credential
- `DELETE (0x02)` - Remove credential
- `SET_CODE (0x03)` - Set/change password
- `RESET (0x04)` - Factory reset (WARNING: Deletes all credentials!)
- `VALIDATE (0xA3)` - Authenticate with password
- `SEND_REMAINING (0xA5)` - Get chained response data

### Tags
- `TAG_NAME (0x71)` - Credential name
- `TAG_CHALLENGE (0x74)` - TOTP/HOTP challenge
- `TAG_RESPONSE (0x75)` - Full HMAC response
- `TAG_TRUNCATED_RESPONSE (0x76)` - Truncated TOTP/HOTP code
- `TAG_PROPERTY (0x78)` - Properties byte (touch, algorithm, etc.)
- `TAG_VERSION (0x79)` - Firmware version
- `TAG_SERIAL_NUMBER (0x8F)` - Device serial (Nitrokey-specific)

### Properties Byte (0x78)
- Bit 0: `RequireTouch` (1 = touch required)
- Bit 1-3: Unused
- Bit 4-7: Algorithm (SHA1/SHA256/SHA512)

### Algorithms
- `SHA1 (0x01)` - HMAC-SHA1 (default, most compatible)
- `SHA256 (0x02)` - HMAC-SHA256
- `SHA512 (0x03)` - NOT supported (returns FunctionNotSupported)

### Credential Types
- `HOTP (0x10)` - HMAC-based OTP (counter-based)
- `TOTP (0x20)` - Time-based OTP (30s default period)

## Protocol Constants

```cpp
// Application Identifier (AID)
const QByteArray OATH_AID = {0xa0, 0x00, 0x00, 0x05, 0x27, 0x21, 0x01};

// Instructions
const quint8 INS_PUT = 0x01;
const quint8 INS_DELETE = 0x02;
const quint8 INS_SET_CODE = 0x03;
const quint8 INS_RESET = 0x04;
const quint8 INS_LIST = 0xa1;
const quint8 INS_CALCULATE = 0xa2;
const quint8 INS_VALIDATE = 0xa3;
const quint8 INS_CALCULATE_ALL = 0xa4;  // May not be available!
const quint8 INS_SEND_REMAINING = 0xa5;

// Status Words
const quint16 SW_SUCCESS = 0x9000;
const quint16 SW_MORE_DATA = 0x61XX;  // XX = bytes available
const quint16 SW_SECURITY_NOT_SATISFIED = 0x6982;  // Touch timeout or auth required
const quint16 SW_CONDITIONS_NOT_SATISFIED = 0x6985;  // NOT used by Nitrokey for touch
const quint16 SW_WRONG_DATA = 0x6a80;
const quint16 SW_FILE_NOT_FOUND = 0x6a82;
const quint16 SW_NO_SPACE = 0x6a84;
const quint16 SW_INS_NOT_SUPPORTED = 0x6d00;
```

## Detection Algorithm

### Reader Name Detection
```cpp
QString readerName = "Nitrokey Nitrokey 3 [CCID/ICCD Interface]";

if (readerName.contains("Nitrokey", Qt::CaseInsensitive)) {
    return DeviceBrand::Nitrokey;
} else if (readerName.contains("Yubico", Qt::CaseInsensitive) ||
           readerName.contains("YubiKey", Qt::CaseInsensitive)) {
    return DeviceBrand::YubiKey;
}
```

### Firmware + Serial Detection
```cpp
// Nitrokey 3: Firmware 4.14.0+ with TAG_SERIAL_NUMBER in SELECT
if (selectSerialNumber != 0 && firmwareVersion >= Version(4, 14, 0)) {
    return DeviceBrand::Nitrokey;
}

// YubiKey 5: Firmware 5.x.x without TAG_SERIAL_NUMBER
if (firmwareVersion.major() == 5 && selectSerialNumber == 0) {
    return DeviceBrand::YubiKey;
}
```

## Credential Fetching Strategy

### For Nitrokey 3 (LIST-based)
```cpp
Result<QList<OathCredential>> fetchCredentialsNitrokey() {
    // 1. Send LIST command
    QByteArray listCmd = OathProtocol::createListCommand();
    QByteArray listResp = sendApdu(listCmd);

    // 2. Parse credential names
    QStringList names = OathProtocol::parseListResponse(listResp);

    // 3. Calculate code for each credential
    QList<OathCredential> results;
    for (const QString& name : names) {
        auto codeResult = calculateCode(name);
        if (codeResult.isSuccess()) {
            OathCredential cred;
            cred.name = name;
            cred.code = codeResult.value();
            results.append(cred);
        }
    }

    return Result::success(results);
}
```

### For YubiKey (CALCULATE_ALL-based)
```cpp
Result<QList<OathCredential>> fetchCredentialsYubiKey() {
    // Single CALCULATE_ALL command
    QByteArray challenge = OathProtocol::createTotpChallenge();
    QByteArray cmd = OathProtocol::createCalculateAllCommand(challenge);
    QByteArray resp = sendApdu(cmd);

    // Parse all credentials with codes
    return OathProtocol::parseCalculateAllResponse(resp);
}
```

## Authentication (PBKDF2)

**100% Compatible** - Both Nitrokey 3 and YubiKey use identical authentication:

```cpp
// PBKDF2 parameters
const int iterations = 1000;
const int keyLength = 16;  // 128-bit key
const QByteArray salt = QByteArray::fromHex(deviceId.toLatin1());

// Derive key
QByteArray key = deriveKeyPbkdf2(password.toUtf8(), salt, iterations, keyLength);

// Calculate HMAC-SHA1 response
QByteArray response = HMAC_SHA1(challenge, key);

// Send VALIDATE command
sendApdu(createValidateCommand(response, ourChallenge));
```

## Form Factor Detection

**Nitrokey 3:**
- No Management API, cannot detect form factor programmatically
- Use reader name heuristics:
  - All Nitrokey 3 models have NFC built-in
  - Detect USB type from model name if available

**Known Models:**
- Nitrokey 3A NFC (USB-A + NFC, standard size)
- Nitrokey 3A Mini (USB-A + NFC, compact)
- Nitrokey 3C NFC (USB-C + NFC)

## Testing Checklist

### Nitrokey 3 Specific
- [ ] Serial number detected from TAG_SERIAL_NUMBER (0x8F)
- [ ] Brand detected as Nitrokey
- [ ] CALCULATE_ALL returns 0x6D00 (expected)
- [ ] LIST fallback retrieves all credentials
- [ ] Touch-required credentials work (0x6982 handled)
- [ ] Correct icon displayed (nitrokey-3c.png, etc.)

### Compatibility
- [ ] YubiKey 5/4/NEO still work correctly
- [ ] YubiKey continues to use CALCULATE_ALL
- [ ] Mixed YubiKey + Nitrokey environment works

## References

- **Nitrokey Source:** https://github.com/Nitrokey/trussed-secrets-app
- **YubiKey OATH Spec:** https://developers.yubico.com/OATH/YKOATH_Protocol.html
- **RFC 4226 (HOTP):** https://tools.ietf.org/html/rfc4226
- **RFC 6238 (TOTP):** https://tools.ietf.org/html/rfc6238
- **PBKDF2 Spec:** https://tools.ietf.org/html/rfc2898

## Version History

- **v1.0** (2025-01-10): Initial Nitrokey 3 protocol documentation
  - Documented key differences from YubiKey
  - LIST-based credential fetching strategy
  - Touch requirement status code mapping (0x6982)
