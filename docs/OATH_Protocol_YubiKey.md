# YubiKey OATH Protocol Documentation

This document describes the OATH (OATH-HOTP/OATH-TOTP) protocol implementation for YubiKey devices, based on the Yubico .NET SDK and practical implementation findings.

## Table of Contents

1. [Overview](#overview)
2. [Application Selection](#application-selection)
3. [Authentication](#authentication)
4. [Commands](#commands)
5. [Multi-Part Responses](#multi-part-responses)
6. [Data Structures](#data-structures)
7. [Error Codes](#error-codes)
8. [Implementation Notes](#implementation-notes)

## Overview

The YubiKey OATH application allows storage and generation of TOTP (Time-based One-Time Password) and HOTP (HMAC-based One-Time Password) credentials. The application can be password-protected and supports mutual authentication between the host and the YubiKey.

### Application ID (AID)
- **AID**: `a0000005272101`
- **Length**: 7 bytes

## Application Selection

### SELECT Command

Selects the OATH application on the YubiKey.

**Command Structure:**
```
CLA: 0x00
INS: 0xa4  (SELECT)
P1:  0x04  (Select by name)
P2:  0x00
Lc:  0x07  (Length of AID)
Data: a0000005272101  (OATH AID)
```

**Response on Success (0x9000):**
```
Tag 0x79: Version (e.g., 050200 for v5.2.0)
Tag 0x71: Name/Device ID (used as salt for authentication)
Tag 0x74: Challenge (8 bytes, used for mutual authentication)
```

**Response on Authentication Required (0x6982):**
The application is password-protected and requires authentication.

## Authentication

YubiKey OATH uses mutual authentication based on PBKDF2 key derivation and HMAC-SHA1.

### Key Derivation

**PBKDF2 Parameters:**
- **Algorithm**: HMAC-SHA1
- **Password**: User-provided password (UTF-8 encoded)
- **Salt**: Device ID from SELECT response (tag 0x71)
- **Iterations**: 1000
- **Key Length**: 16 bytes

```cpp
QByteArray key = QPasswordDigestor::deriveKeyPbkdf2(
    QCryptographicHash::Sha1,
    password.toUtf8(),    // Password as input
    deviceId,             // Device ID as salt
    1000,                 // Iterations
    16                    // Key length
);
```

### VALIDATE Command

Performs mutual authentication with the YubiKey.

**Command Structure:**
```
CLA: 0x00
INS: 0xa3  (VALIDATE)
P1:  0x00
P2:  0x00
Lc:  Variable
Data:
  Tag 0x75: Response (16 bytes) - HMAC-SHA1 of YubiKey's challenge
  Tag 0x74: Challenge (8 bytes) - Host's challenge to YubiKey
```

**Response Calculation:**
1. Extract challenge from SELECT response (tag 0x74)
2. Calculate HMAC-SHA1(derived_key, yubikey_challenge)
3. Send response along with host's challenge

**Response on Success (0x9000):**
```
Tag 0x75: YubiKey's response to host's challenge (verification)
```

## Commands

### LIST Command

Lists all stored OATH credentials.

**Command Structure:**
```
CLA: 0x00
INS: 0xa1  (LIST)
P1:  0x00
P2:  0x00
Le:  0x00  (Expect maximum data)
```

**Response Format:**
```
Tag 0x72: Name tag, followed by credential data
  - First byte: Type and properties
    - Bit 4: TOTP (1) or HOTP (0)
    - Bit 1: Requires touch (1) or not (0)
  - Remaining bytes: Credential name (UTF-8)
    - Format: [issuer:]account
```

### CALCULATE Command

Generates an OTP code for a specific credential.

**Command Structure:**
```
CLA: 0x00
INS: 0xa2  (CALCULATE)
P1:  0x00
P2:  0x01  (Request response)
Lc:  Variable
Data:
  Tag 0x71: Name tag (credential name)
  Tag 0x74: Challenge (8 bytes timestamp for TOTP)
```

**Timestamp Calculation (TOTP):**
```cpp
qint64 timestamp = QDateTime::currentSecsSinceEpoch() / 30;
// Convert to 8-byte big-endian
```

**Response Format:**
```
Tag 0x76: Full response (6-8 digit code)
  - First byte: Number of digits
  - Remaining bytes: Code value (big-endian)
```

### SEND REMAINING Command

Retrieves additional data when response indicates more data available (0x61XX).

**Command Structure:**
```
CLA: 0x00
INS: 0xa5  (SEND REMAINING)
P1:  0x00
P2:  0x00
Le:  Expected length (from status word)
```

**Note**: This is YubiKey OATH specific. Standard ISO 7816-4 GET RESPONSE (0xC0) is **not supported**.

## Multi-Part Responses

When the response data exceeds the maximum APDU size, YubiKey returns status word 0x61XX where XX indicates remaining bytes.

### Handling Algorithm

1. **Initial Command**: Send LIST or other command
2. **Check Status**: If status = 0x61XX, more data available
3. **Extract Length**: XX = remaining bytes (0x00 means 256 bytes)
4. **Send REMAINING**: Use SEND REMAINING command with Le = XX
5. **Concatenate**: Append response data
6. **Repeat**: Until status = 0x9000

### Implementation Example

```cpp
QByteArray data = response.left(response.length() - 2);
while ((sw & 0xFF00) == 0x6100) {
    quint8 remainingBytesRaw = sw & 0x00FF;
    quint8 remainingBytes = (remainingBytesRaw == 0) ? 256 : remainingBytesRaw;

    QByteArray moreResponse = sendRemaining(remainingBytes);
    sw = extractStatusWord(moreResponse);
    data.append(moreResponse.left(moreResponse.length() - 2));
}
```

## Data Structures

### TLV (Tag-Length-Value) Format

All OATH data uses TLV encoding:
- **Tag**: 1 byte identifier
- **Length**: 1 byte (0x00-0xFF)
- **Value**: Variable length data

### Common Tags

| Tag  | Description |
|------|-------------|
| 0x71 | Name/Device ID |
| 0x72 | Name list item |
| 0x74 | Challenge |
| 0x75 | Response |
| 0x76 | Full response (OTP code) |
| 0x77 | Truncated response |
| 0x78 | HOTP counter |
| 0x79 | Version |

### Credential Name Format

Credentials can be stored in two formats:
1. **Simple**: `account_name`
2. **Issuer:Account**: `issuer:account_name`

Example: `Google:user@example.com`

## Error Codes

| Status Word | Description |
|-------------|-------------|
| 0x9000 | Success |
| 0x6100-0x61FF | More data available (XX bytes) |
| 0x6982 | Security status not satisfied (authentication required) |
| 0x6a80 | Incorrect parameters/wrong data |
| 0x6a81 | Function not supported |
| 0x6a82 | File not found |
| 0x6d00 | Instruction not supported |
| 0x6985 | Conditions not satisfied |

## Implementation Notes

### Password Storage

- Passwords should be stored securely (e.g., KWallet)
- Use UTF-8 encoding for password input
- Device ID must be preserved between sessions for consistent authentication

### Performance Considerations

- Cache credentials after successful LIST command
- Implement timeout for credential cache (recommended: 30 seconds)
- Use CALCULATE for individual codes rather than CALCULATE_ALL for better performance

### Security Notes

- Always verify YubiKey's response during mutual authentication
- Implement proper error handling for authentication failures
- Consider touch requirement for sensitive credentials

### Common Pitfalls

1. **Wrong Continuation Command**: Use SEND REMAINING (0xa5), not GET RESPONSE (0xc0)
2. **Length Interpretation**: 0x00 means 256 bytes, not 0 bytes
3. **Authentication**: Device ID is salt, password is input (not the reverse)
4. **Challenge Order**: Response tag (0x75) comes before Challenge tag (0x74) in VALIDATE

### Reference Implementation

This documentation is based on:
- [Yubico .NET SDK](https://github.com/Yubico/Yubico.NET.SDK)
- YubiKey OATH Application specification
- Practical implementation in krunner-yoath.cpp

For detailed implementation examples, see the source code in this project.