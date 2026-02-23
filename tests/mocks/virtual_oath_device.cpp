/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "virtual_oath_device.h"
#include <QDateTime>
#include <QMessageAuthenticationCode>

void VirtualOathDevice::setPassword(const QString& password)
{
    if (password.isEmpty()) {
        m_passwordKey.clear();
        m_authenticated = true; // No password = always authenticated
        return;
    }

    // Generate random salt (8 bytes)
    QByteArray salt = QByteArray::fromHex("0102030405060708"); // Mock salt for testing

    // Derive key using PBKDF2
    m_passwordKey = derivePasswordKey(password, salt);
    m_authenticated = false;
}

void VirtualOathDevice::addCredential(const OathCredential& cred)
{
    QString fullName = cred.originalName;
    m_credentials[fullName] = cred;
}

void VirtualOathDevice::removeCredential(const QString& name)
{
    m_credentials.remove(name);
}

QList<OathCredential> VirtualOathDevice::credentials() const
{
    return m_credentials.values();
}

bool VirtualOathDevice::hasCredential(const QString& name) const
{
    return m_credentials.contains(name);
}

QByteArray VirtualOathDevice::handlePut(const QByteArray& apdu)
{
    // Check authentication
    if (!m_passwordKey.isEmpty() && !m_authenticated) {
        return createErrorResponse(OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED);
    }

    // Parse APDU data (skip CLA, INS, P1, P2, Lc)
    if (apdu.size() < 6) {
        return createErrorResponse(OathProtocol::SW_WRONG_DATA);
    }

    QByteArray data = apdu.mid(5);

    // Parse TAG_NAME
    QByteArray nameBytes = OathProtocol::findTlvTag(data, OathProtocol::TAG_NAME);
    if (nameBytes.isEmpty()) {
        return createErrorResponse(OathProtocol::SW_WRONG_DATA);
    }
    QString name = QString::fromUtf8(nameBytes);

    // Parse TAG_KEY (secret)
    QByteArray keyTag = OathProtocol::findTlvTag(data, OathProtocol::TAG_KEY);
    if (keyTag.size() < 2) {
        return createErrorResponse(OathProtocol::SW_WRONG_DATA);
    }

    quint8 typeAndAlgo = static_cast<quint8>(keyTag[0]);
    quint8 digits = static_cast<quint8>(keyTag[1]);
    QByteArray secret = keyTag.mid(2);

    // Determine type
    OathType type = (typeAndAlgo & 0x20) ? OathType::TOTP : OathType::HOTP;
    OathAlgorithm algorithm = static_cast<OathAlgorithm>(typeAndAlgo & 0x0F);

    // Parse TAG_PROPERTY (optional - touch required)
    QByteArray propertyTag = OathProtocol::findTlvTag(data, OathProtocol::TAG_PROPERTY);
    bool touch = !propertyTag.isEmpty() && (static_cast<quint8>(propertyTag[0]) & 0x02);

    // Create credential
    OathCredential cred;
    cred.originalName = name;
    cred.type = type;
    cred.algorithm = algorithm;
    cred.digits = digits;
    cred.requiresTouch = touch;
    cred.period = 30; // Default TOTP period

    // Store credential
    addCredential(cred);

    return createSuccessResponse();
}

QByteArray VirtualOathDevice::handleDelete(const QByteArray& apdu)
{
    // Check authentication
    if (!m_passwordKey.isEmpty() && !m_authenticated) {
        return createErrorResponse(OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED);
    }

    // Parse credential name
    if (apdu.size() < 6) {
        return createErrorResponse(OathProtocol::SW_WRONG_DATA);
    }

    QByteArray data = apdu.mid(5);
    QByteArray nameBytes = OathProtocol::findTlvTag(data, OathProtocol::TAG_NAME);
    if (nameBytes.isEmpty()) {
        return createErrorResponse(OathProtocol::SW_WRONG_DATA);
    }

    QString name = QString::fromUtf8(nameBytes);

    // Check if credential exists
    if (!hasCredential(name)) {
        return createErrorResponse(OathProtocol::SW_NO_SUCH_OBJECT);
    }

    // Remove credential
    removeCredential(name);

    return createSuccessResponse();
}

QByteArray VirtualOathDevice::handleValidate(const QByteArray& apdu)
{
    // Parse challenge and response from client
    if (apdu.size() < 6) {
        return createErrorResponse(OathProtocol::SW_WRONG_DATA);
    }

    QByteArray data = apdu.mid(5);
    QByteArray challenge = OathProtocol::findTlvTag(data, OathProtocol::TAG_CHALLENGE);
    QByteArray clientResponse = OathProtocol::findTlvTag(data, OathProtocol::TAG_RESPONSE);

    if (challenge.isEmpty() || clientResponse.isEmpty()) {
        return createErrorResponse(OathProtocol::SW_WRONG_DATA);
    }

    // Calculate expected response: HMAC-SHA1(password_key, challenge)
    QByteArray expectedResponse = QMessageAuthenticationCode::hash(
        challenge,
        m_passwordKey,
        QCryptographicHash::Sha1
    );

    // Verify client response
    if (clientResponse != expectedResponse) {
        m_authenticated = false;
        return createErrorResponse(OathProtocol::SW_WRONG_DATA);
    }

    // Authentication successful
    m_authenticated = true;

    // Generate server challenge and response
    QByteArray serverChallenge = QByteArray::fromHex("AABBCCDDEEFF0011");
    QByteArray serverResponse = QMessageAuthenticationCode::hash(
        serverChallenge,
        m_passwordKey,
        QCryptographicHash::Sha1
    );

    // Build response
    QByteArray response;
    response.append(static_cast<char>(OathProtocol::TAG_RESPONSE));
    response.append(static_cast<char>(serverResponse.size()));
    response.append(serverResponse);

    return createSuccessResponse(response);
}

QByteArray VirtualOathDevice::handleSetCode(const QByteArray& apdu)
{
    // Parse new password
    if (apdu.size() < 6) {
        return createErrorResponse(OathProtocol::SW_WRONG_DATA);
    }

    QByteArray data = apdu.mid(5);
    QByteArray newKeyTag = OathProtocol::findTlvTag(data, OathProtocol::TAG_KEY);

    if (newKeyTag.size() < 16) { // PBKDF2 derived key should be >= 16 bytes
        // Empty = remove password
        if (newKeyTag.isEmpty()) {
            m_passwordKey.clear();
            m_authenticated = true;
            return createSuccessResponse();
        }
        return createErrorResponse(OathProtocol::SW_WRONG_DATA);
    }

    // Set new password key
    m_passwordKey = newKeyTag;
    m_authenticated = true; // Auto-authenticate after password change

    return createSuccessResponse();
}

QString VirtualOathDevice::calculateTotpCode(const OathCredential& cred, quint64 timestamp) const
{
    // TOTP counter = timestamp / period
    quint64 counter = timestamp / cred.period;

    // Convert counter to big-endian bytes
    QByteArray counterBytes(8, 0);
    qToBigEndian(counter, counterBytes.data());

    // Mock secret (in real tests, this would come from PUT command)
    // For now, use credential name hash as seed
    QByteArray secret = QCryptographicHash::hash(cred.originalName.toUtf8(), QCryptographicHash::Sha1);
    QByteArray hmac;

    switch (cred.algorithm) {
        case OathAlgorithm::SHA1:
            hmac = QMessageAuthenticationCode::hash(counterBytes, secret, QCryptographicHash::Sha1);
            break;
        case OathAlgorithm::SHA256:
            hmac = QMessageAuthenticationCode::hash(counterBytes, secret, QCryptographicHash::Sha256);
            break;
        case OathAlgorithm::SHA512:
            hmac = QMessageAuthenticationCode::hash(counterBytes, secret, QCryptographicHash::Sha512);
            break;
        default:
            hmac = QMessageAuthenticationCode::hash(counterBytes, secret, QCryptographicHash::Sha1);
    }

    // Dynamic truncation (RFC 4226)
    int offset = hmac[hmac.size() - 1] & 0x0F;
    quint32 binary = ((hmac[offset] & 0x7F) << 24)
                   | ((hmac[offset + 1] & 0xFF) << 16)
                   | ((hmac[offset + 2] & 0xFF) << 8)
                   | (hmac[offset + 3] & 0xFF);

    // Generate code
    quint32 divisor = static_cast<quint32>(std::pow(10, cred.digits));
    quint32 code = binary % divisor;

    return QString::number(code).rightJustified(cred.digits, QLatin1Char('0'));
}

QString VirtualOathDevice::calculateHotpCode(const OathCredential& cred, quint64 counter) const
{
    // HOTP uses the same algorithm as TOTP but with explicit counter
    QByteArray counterBytes(8, 0);
    qToBigEndian(counter, counterBytes.data());

    // Mock secret
    QByteArray secret = QCryptographicHash::hash(cred.originalName.toUtf8(), QCryptographicHash::Sha1);
    QByteArray hmac = QMessageAuthenticationCode::hash(counterBytes, secret, QCryptographicHash::Sha1);

    int offset = hmac[hmac.size() - 1] & 0x0F;
    quint32 binary = ((hmac[offset] & 0x7F) << 24)
                   | ((hmac[offset + 1] & 0xFF) << 16)
                   | ((hmac[offset + 2] & 0xFF) << 8)
                   | (hmac[offset + 3] & 0xFF);

    quint32 divisor = static_cast<quint32>(std::pow(10, cred.digits));
    quint32 code = binary % divisor;

    return QString::number(code).rightJustified(cred.digits, QLatin1Char('0'));
}

QByteArray VirtualOathDevice::derivePasswordKey(const QString& password, const QByteArray& salt) const
{
    // PBKDF2-HMAC-SHA1 with 1000 iterations (as per OATH spec)
    // Qt doesn't have built-in PBKDF2, so we'll use simplified version for testing
    // In production tests, this would use proper PBKDF2 implementation

    QByteArray passwordBytes = password.toUtf8();
    QByteArray key = passwordBytes + salt;

    // Simplified key derivation (1000 iterations of HMAC-SHA1)
    for (int i = 0; i < 1000; ++i) {
        key = QCryptographicHash::hash(key, QCryptographicHash::Sha1);
    }

    return key.left(16); // Return 16 bytes
}
