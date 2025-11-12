/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include <memory>
#include "daemon/oath/oath_protocol.h"
#include "daemon/oath/yk_oath_protocol.h"

using namespace YubiKeyOath::Shared;
using namespace YubiKeyOath::Daemon;

/**
 * @brief Unit tests for OathProtocol
 *
 * Tests OATH protocol utility functions including:
 * - APDU command creation
 * - TLV response parsing
 * - Helper functions (status word, TLV, TOTP counter)
 * - Code formatting
 */
class TestOathProtocol : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        m_protocol = std::make_unique<YKOathProtocol>();
    }

    // Helper function tests
    void testGetStatusWord();
    void testIsSuccess();
    void testHasMoreData();
    void testFormatCode();
    void testFindTlvTag();
    void testCalculateTotpCounter();
    void testCreateTotpChallenge();

    // Command creation tests
    void testCreateSelectCommand();
    void testCreateListCommand();
    void testCreateCalculateCommand();
    void testCreateCalculateAllCommand();
    void testCreateValidateCommand();
    void testCreateSendRemainingCommand();

    // Response parsing tests
    void testParseSelectResponse();
    void testParseCredentialList();
    void testParseCode();
    void testParseCalculateAllResponse();

    // Edge cases
    void testParseSelectResponse_InvalidData();
    void testParseCredentialList_EmptyResponse();
    void testParseCode_TouchRequired();
    void testFormatCode_InvalidData();

private:
    std::unique_ptr<YKOathProtocol> m_protocol;
};

// ========== Helper Function Tests ==========

void TestOathProtocol::testGetStatusWord()
{
    // Test success status word (0x9000)
    QByteArray response;
    response.append((char)0x90);
    response.append((char)0x00);
    QCOMPARE(OathProtocol::getStatusWord(response), (quint16)0x9000);

    // Test more data available (0x6100)
    response.clear();
    response.append((char)0x61);
    response.append((char)0x00);
    QCOMPARE(OathProtocol::getStatusWord(response), (quint16)0x6100);

    // Test with data before status word
    response.clear();
    response.append("somedata");
    response.append((char)0x90);
    response.append((char)0x00);
    QCOMPARE(OathProtocol::getStatusWord(response), (quint16)0x9000);

    // Test empty response
    response.clear();
    QCOMPARE(OathProtocol::getStatusWord(response), (quint16)0);

    // Test single byte response
    response.clear();
    response.append((char)0x90);
    QCOMPARE(OathProtocol::getStatusWord(response), (quint16)0);
}

void TestOathProtocol::testIsSuccess()
{
    QVERIFY(OathProtocol::isSuccess(0x9000));   // SW_SUCCESS
    QVERIFY(!OathProtocol::isSuccess(0x6100)); // More data
    QVERIFY(!OathProtocol::isSuccess(0x6982)); // Security not satisfied
    QVERIFY(!OathProtocol::isSuccess(0x6A80)); // Wrong data
    QVERIFY(!OathProtocol::isSuccess(0x0000)); // Invalid
}

void TestOathProtocol::testHasMoreData()
{
    QVERIFY(OathProtocol::hasMoreData(0x6100));  // SW_MORE_DATA
    QVERIFY(OathProtocol::hasMoreData(0x61FF));  // Any 0x61XX
    QVERIFY(OathProtocol::hasMoreData(0x6110));  // 0x61XX variant
    QVERIFY(!OathProtocol::hasMoreData(0x9000)); // Success
    QVERIFY(!OathProtocol::hasMoreData(0x6200)); // Different pattern
}

void TestOathProtocol::testFormatCode()
{
    // Test 6-digit code
    QByteArray rawCode;
    rawCode.append((char)0x06); // 6 digits
    rawCode.append((char)0x00);
    rawCode.append((char)0x00);
    rawCode.append((char)0x0F);
    rawCode.append((char)0x42); // Value: 3906 (decimal)

    QString code = OathProtocol::formatCode(rawCode, 6);
    QCOMPARE(code, QString("003906"));

    // Test 8-digit code
    rawCode.clear();
    rawCode.append((char)0x08); // 8 digits
    rawCode.append((char)0x00);
    rawCode.append((char)0x98);
    rawCode.append((char)0x96);
    rawCode.append((char)0x80); // Value: 10000000 (decimal)

    code = OathProtocol::formatCode(rawCode, 8);
    QCOMPARE(code, QString("10000000"));

    // Test with leading zeros
    rawCode.clear();
    rawCode.append((char)0x06); // 6 digits
    rawCode.append((char)0x00);
    rawCode.append((char)0x00);
    rawCode.append((char)0x00);
    rawCode.append((char)0x7B); // Value: 123 (decimal)

    code = OathProtocol::formatCode(rawCode, 6);
    QCOMPARE(code, QString("000123"));
}

void TestOathProtocol::testFindTlvTag()
{
    // Create TLV data: TAG1(0x71) + LEN(4) + DATA("test") + TAG2(0x74) + LEN(2) + DATA("AB")
    QByteArray data;
    data.append((char)0x71);
    data.append((char)0x04);
    data.append("test");
    data.append((char)0x74);
    data.append((char)0x02);
    data.append("AB");

    // Find first tag
    QByteArray value = OathProtocol::findTlvTag(data, 0x71);
    QCOMPARE(value, QByteArray("test"));

    // Find second tag
    value = OathProtocol::findTlvTag(data, 0x74);
    QCOMPARE(value, QByteArray("AB"));

    // Find non-existent tag
    value = OathProtocol::findTlvTag(data, 0x99);
    QVERIFY(value.isEmpty());

    // Empty data
    value = OathProtocol::findTlvTag(QByteArray(), 0x71);
    QVERIFY(value.isEmpty());
}

void TestOathProtocol::testCalculateTotpCounter()
{
    // Test TOTP counter calculation (30-second period)
    QByteArray counter = OathProtocol::calculateTotpCounter(30);

    // Counter should be 8 bytes
    QCOMPARE(counter.length(), 8);

    // Counter should be big-endian
    // The value should be current_time / 30
    qint64 currentTime = QDateTime::currentSecsSinceEpoch();
    qint64 expectedCounter = currentTime / 30;

    // Extract counter from result (big-endian)
    qint64 actualCounter = 0;
    for (int i = 0; i < 8; ++i) {
        actualCounter = (actualCounter << 8) | static_cast<quint8>(counter[i]);
    }

    QCOMPARE(actualCounter, expectedCounter);
}

void TestOathProtocol::testCreateTotpChallenge()
{
    // createTotpChallenge should be same as calculateTotpCounter
    QByteArray challenge = OathProtocol::createTotpChallenge(30);
    QByteArray counter = OathProtocol::calculateTotpCounter(30);

    QCOMPARE(challenge, counter);
    QCOMPARE(challenge.length(), 8);
}

// ========== Command Creation Tests ==========

void TestOathProtocol::testCreateSelectCommand()
{
    QByteArray cmd = OathProtocol::createSelectCommand();

    // Verify APDU structure
    QVERIFY(cmd.length() >= 5);
    QCOMPARE((quint8)cmd[0], OathProtocol::CLA);
    QCOMPARE((quint8)cmd[1], OathProtocol::INS_SELECT);
    QCOMPARE((quint8)cmd[2], (quint8)0x04); // P1 = Select by name
    QCOMPARE((quint8)cmd[3], (quint8)0x00); // P2

    // Lc should be length of OATH_AID
    QCOMPARE((quint8)cmd[4], (quint8)OathProtocol::OATH_AID.length());

    // Data should be OATH_AID + Le (for Nitrokey compatibility)
    // Format: CLA INS P1 P2 Lc [AID data] Le
    QByteArray aid = cmd.mid(5, OathProtocol::OATH_AID.length());
    QCOMPARE(aid, OathProtocol::OATH_AID);

    // Verify Le=0x00 is present at the end
    QCOMPARE((quint8)cmd[cmd.length() - 1], (quint8)0x00);
}

void TestOathProtocol::testCreateListCommand()
{
    QByteArray cmd = OathProtocol::createListCommand();

    // LIST command: CLA INS P1 P2 (no Lc/Le)
    QCOMPARE(cmd.length(), 4);
    QCOMPARE((quint8)cmd[0], OathProtocol::CLA);
    QCOMPARE((quint8)cmd[1], OathProtocol::INS_LIST);
    QCOMPARE((quint8)cmd[2], (quint8)0x00); // P1
    QCOMPARE((quint8)cmd[3], (quint8)0x00); // P2
}

void TestOathProtocol::testCreateCalculateCommand()
{
    QString name = "Google:user@example.com";
    QByteArray challenge;
    for (int i = 0; i < 8; i++) {
        challenge.append((char)i);
    }

    QByteArray cmd = OathProtocol::createCalculateCommand(name, challenge);

    // Verify header
    QCOMPARE((quint8)cmd[0], OathProtocol::CLA);
    QCOMPARE((quint8)cmd[1], OathProtocol::INS_CALCULATE);
    QCOMPARE((quint8)cmd[2], (quint8)0x00); // P1
    QCOMPARE((quint8)cmd[3], (quint8)0x01); // P2 = Request response

    // Verify data contains NAME and CHALLENGE tags
    QByteArray data = cmd.mid(5);

    // NAME tag
    QCOMPARE((quint8)data[0], OathProtocol::TAG_NAME);
    quint8 nameLen = data[1];
    QCOMPARE(nameLen, (quint8)name.toUtf8().length());

    // CHALLENGE tag should follow
    int challengePos = 2 + nameLen;
    QCOMPARE((quint8)data[challengePos], OathProtocol::TAG_CHALLENGE);
    QCOMPARE((quint8)data[challengePos + 1], (quint8)8);
}

void TestOathProtocol::testCreateCalculateAllCommand()
{
    QByteArray challenge;
    for (int i = 0; i < 8; i++) {
        challenge.append((char)i);
    }

    QByteArray cmd = OathProtocol::createCalculateAllCommand(challenge);

    // Verify header
    QCOMPARE((quint8)cmd[0], OathProtocol::CLA);
    QCOMPARE((quint8)cmd[1], OathProtocol::INS_CALCULATE_ALL);
    QCOMPARE((quint8)cmd[2], (quint8)0x00); // P1
    QCOMPARE((quint8)cmd[3], (quint8)0x01); // P2 = Truncate response

    // Lc = 1 + 1 + 8 = 10
    QCOMPARE((quint8)cmd[4], (quint8)10);

    // Data: TAG_CHALLENGE + length + challenge
    QCOMPARE((quint8)cmd[5], OathProtocol::TAG_CHALLENGE);
    QCOMPARE((quint8)cmd[6], (quint8)8);
}

void TestOathProtocol::testCreateValidateCommand()
{
    QByteArray response = QByteArray::fromHex("1122334455667788");
    QByteArray challenge = QByteArray::fromHex("aabbccdd");

    QByteArray cmd = OathProtocol::createValidateCommand(response, challenge);

    // Verify header
    QCOMPARE((quint8)cmd[0], OathProtocol::CLA);
    QCOMPARE((quint8)cmd[1], OathProtocol::INS_VALIDATE);

    // Verify contains RESPONSE and CHALLENGE tags
    QByteArray data = cmd.mid(5);
    QCOMPARE((quint8)data[0], OathProtocol::TAG_RESPONSE);
    QCOMPARE((quint8)data[1], (quint8)response.length());
}

void TestOathProtocol::testCreateSendRemainingCommand()
{
    QByteArray cmd = OathProtocol::createSendRemainingCommand();

    // SEND REMAINING: CLA INS P1 P2 Le
    QCOMPARE(cmd.length(), 5);
    QCOMPARE((quint8)cmd[0], OathProtocol::CLA);
    QCOMPARE((quint8)cmd[1], OathProtocol::INS_SEND_REMAINING);
    QCOMPARE((quint8)cmd[2], (quint8)0x00); // P1
    QCOMPARE((quint8)cmd[3], (quint8)0x00); // P2
    QCOMPARE((quint8)cmd[4], (quint8)0x00); // Le = 0 (get up to 256 bytes)
}

// ========== Response Parsing Tests ==========

void TestOathProtocol::testParseSelectResponse()
{
    // Create valid SELECT response with device ID and challenge
    QByteArray response;

    // TAG_NAME_SALT (0x71) + length + device ID
    response.append((char)0x71);
    response.append((char)0x04);
    response.append("ABCD"); // Device ID bytes

    // TAG_CHALLENGE (0x74) + length + challenge
    response.append((char)0x74);
    response.append((char)0x08);
    for (int i = 0; i < 8; i++) {
        response.append((char)i);
    }

    // Status word (0x9000 = success)
    response.append((char)0x90);
    response.append((char)0x00);

    QString deviceId;
    QByteArray challenge;
    Version firmwareVersion;
    bool requiresPassword = false;
    quint32 serialNumber = 0;
    bool result = m_protocol->parseSelectResponse(response, deviceId, challenge, firmwareVersion,
                                                     requiresPassword, serialNumber);

    QVERIFY(result);
    QCOMPARE(deviceId, QString("41424344")); // "ABCD" in hex
    QCOMPARE(challenge.length(), 8);
}

void TestOathProtocol::testParseCredentialList()
{
    // Create LIST response with two credentials
    QByteArray response;

    // Credential 1: TAG_NAME_LIST (0x72) + length + algo byte + name
    response.append((char)0x72);
    response.append((char)0x11); // Length: 1 (algo) + 16 (name)
    response.append((char)0x22); // Algo: TOTP (0x02 in lower nibble)
    response.append("Google:user@test", 16); // 16 bytes (not null-terminated)

    // Credential 2
    response.append((char)0x72);
    response.append((char)0x07); // Length: 1 + 6
    response.append((char)0x12); // Algo: HOTP (0x01 in lower nibble = HOTP, 0x02 = TOTP)
    response.append("GitHub", 6); // 6 bytes (not including colon to avoid parsing issue)

    // Status word
    response.append((char)0x90);
    response.append((char)0x00);

    QList<OathCredential> credentials = OathProtocol::parseCredentialList(response);

    QCOMPARE(credentials.size(), 2);

    // Check first credential
    QCOMPARE(credentials[0].originalName, QString("Google:user@test"));
    QCOMPARE(credentials[0].issuer, QString("Google"));
    QCOMPARE(credentials[0].account, QString("user@test"));
    QVERIFY(credentials[0].isTotp); // Algo 0x22 has TOTP (0x02) in lower nibble

    // Check second credential (no account - entire name becomes account, issuer empty)
    QCOMPARE(credentials[1].originalName, QString("GitHub"));
    QCOMPARE(credentials[1].issuer, QString("")); // No colon, so no issuer
    QCOMPARE(credentials[1].account, QString("GitHub")); // Entire name is account
}

void TestOathProtocol::testParseCode()
{
    // Create CALCULATE response with code
    QByteArray response;

    // TAG_TOTP_RESPONSE (0x76) + length + digits + code value
    response.append((char)0x76);
    response.append((char)0x05); // Length
    response.append((char)0x06); // 6 digits
    response.append((char)0x00);
    response.append((char)0x00);
    response.append((char)0x0F);
    response.append((char)0x42); // Code value: 3906

    // Status word
    response.append((char)0x90);
    response.append((char)0x00);

    QString code = m_protocol->parseCode(response);
    QCOMPARE(code, QString("003906"));
}

void TestOathProtocol::testParseCalculateAllResponse()
{
    // Create CALCULATE ALL response with one credential + code
    QByteArray response;

    // NAME tag (0x71) + name
    response.append((char)0x71);
    response.append((char)0x10); // Length: 16 bytes
    response.append("Google:user@test", 16); // 16 bytes (not null-terminated)

    // TOTP_RESPONSE tag (0x76) + code
    response.append((char)0x76);
    response.append((char)0x05); // Length
    response.append((char)0x06); // 6 digits
    response.append((char)0x00);
    response.append((char)0x00);
    response.append((char)0x0F);
    response.append((char)0x42); // Code: 3906

    // Status word
    response.append((char)0x90);
    response.append((char)0x00);

    QList<OathCredential> credentials = m_protocol->parseCalculateAllResponse(response);

    QCOMPARE(credentials.size(), 1);
    QCOMPARE(credentials[0].originalName, QString("Google:user@test"));
    QCOMPARE(credentials[0].code, QString("003906"));
}

// ========== Edge Cases ==========

void TestOathProtocol::testParseSelectResponse_InvalidData()
{
    QString deviceId;
    QByteArray challenge;
    Version firmwareVersion;
    bool requiresPassword = false;
    quint32 serialNumber = 0;

    // Empty response
    QVERIFY(!m_protocol->parseSelectResponse(QByteArray(), deviceId, challenge, firmwareVersion,
                                                requiresPassword, serialNumber));

    // Single byte
    QByteArray response;
    response.append((char)0x90);
    QVERIFY(!m_protocol->parseSelectResponse(response, deviceId, challenge, firmwareVersion,
                                                requiresPassword, serialNumber));

    // Error status word (0x6982)
    response.clear();
    response.append((char)0x69);
    response.append((char)0x82);
    QVERIFY(!m_protocol->parseSelectResponse(response, deviceId, challenge, firmwareVersion,
                                                requiresPassword, serialNumber));
}

void TestOathProtocol::testParseCredentialList_EmptyResponse()
{
    // Empty response
    QList<OathCredential> creds = OathProtocol::parseCredentialList(QByteArray());
    QVERIFY(creds.isEmpty());

    // Just status word
    QByteArray response;
    response.append((char)0x90);
    response.append((char)0x00);
    creds = OathProtocol::parseCredentialList(response);
    QVERIFY(creds.isEmpty());
}

void TestOathProtocol::testParseCode_TouchRequired()
{
    // Touch required status (0x6985)
    QByteArray response;
    response.append((char)0x69);
    response.append((char)0x85);

    QString code = m_protocol->parseCode(response);
    QVERIFY(code.isEmpty());
}

void TestOathProtocol::testFormatCode_InvalidData()
{
    // Too short data
    QByteArray rawCode;
    rawCode.append((char)0x06);
    rawCode.append((char)0x00);

    QString code = OathProtocol::formatCode(rawCode, 6);
    QVERIFY(code.isEmpty());

    // Empty data
    code = OathProtocol::formatCode(QByteArray(), 6);
    QVERIFY(code.isEmpty());
}

QTEST_MAIN(TestOathProtocol)
#include "test_oath_protocol.moc"
