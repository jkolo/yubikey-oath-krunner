/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include "daemon/oath/management_protocol.h"
#include "shared/utils/version.h"

using namespace YubiKeyOath::Daemon;
using namespace YubiKeyOath::Shared;

/**
 * @brief Unit tests for ManagementProtocol
 *
 * Tests APDU command creation, TLV parsing, and device info extraction.
 */
class TestManagementProtocol : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Command creation tests
    void testCreateSelectCommand();
    void testCreateGetDeviceInfoCommand();

    // TLV parsing tests
    void testParseTlv_SingleEntry();
    void testParseTlv_MultipleEntries();
    void testParseTlv_EmptyData();
    void testParseTlv_IncompleteTlv();
    void testParseTlv_StopsAtStatusWord();

    // Status word tests
    void testGetStatusWord_ValidResponse();
    void testGetStatusWord_TooShort();
    void testIsSuccess_SuccessCode();
    void testIsSuccess_ErrorCodes();

    // Device info parsing tests
    void testParseDeviceInfo_CompleteResponse();
    void testParseDeviceInfo_MinimalResponse();
    void testParseDeviceInfo_InvalidStatusWord();
    void testParseDeviceInfo_TooShort();
    void testParseDeviceInfo_SerialNumberParsing();
    void testParseDeviceInfo_FirmwareVersionParsing();
    void testParseDeviceInfo_FormFactorParsing();
    void testParseDeviceInfo_NfcCapabilities_TwoBytes();
    void testParseDeviceInfo_NfcCapabilities_OneByte();

    // Form factor string tests
    void testFormFactorToString_AllKnownFactors();
    void testFormFactorToString_UnknownFactor();
};

// ========== Command Creation Tests ==========

void TestManagementProtocol::testCreateSelectCommand()
{
    QByteArray command = ManagementProtocol::createSelectCommand();

    // Expected: 00 A4 04 00 [length] [AID]
    QVERIFY(command.length() >= 5);  // CLA + INS + P1 + P2 + Lc minimum
    QCOMPARE(static_cast<quint8>(command[0]), ManagementProtocol::CLA);  // CLA = 0x00
    QCOMPARE(static_cast<quint8>(command[1]), (quint8)0xA4);              // INS = SELECT
    QCOMPARE(static_cast<quint8>(command[2]), (quint8)0x04);              // P1 = Select by name
    QCOMPARE(static_cast<quint8>(command[3]), (quint8)0x00);              // P2 = 0x00
    QCOMPARE(static_cast<quint8>(command[4]), (quint8)ManagementProtocol::MANAGEMENT_AID.length()); // Lc

    // Verify AID is appended
    QVERIFY(command.endsWith(ManagementProtocol::MANAGEMENT_AID));
}

void TestManagementProtocol::testCreateGetDeviceInfoCommand()
{
    QByteArray command = ManagementProtocol::createGetDeviceInfoCommand();

    // Expected: 00 01 13 00
    QCOMPARE(command.length(), 4);
    QCOMPARE(static_cast<quint8>(command[0]), ManagementProtocol::CLA);                    // CLA = 0x00
    QCOMPARE(static_cast<quint8>(command[1]), ManagementProtocol::INS_GET_DEVICE_INFO);  // INS = 0x01
    QCOMPARE(static_cast<quint8>(command[2]), ManagementProtocol::P1_GET_DEVICE_INFO);   // P1 = 0x13
    QCOMPARE(static_cast<quint8>(command[3]), (quint8)0x00);                             // P2 = 0x00
}

// ========== TLV Parsing Tests ==========

void TestManagementProtocol::testParseTlv_SingleEntry()
{
    // TLV: tag=0x02, length=4, value=[0x01, 0x02, 0x03, 0x04]
    QByteArray tlvData = QByteArray::fromHex("02040102 0304");

    auto result = ManagementProtocol::parseTlv(tlvData);

    QCOMPARE(result.size(), 1);
    QVERIFY(result.contains(0x02));
    QCOMPARE(result[0x02], QByteArray::fromHex("01020304"));
}

void TestManagementProtocol::testParseTlv_MultipleEntries()
{
    // Two TLVs: tag=0x02, length=2, value=[0xAA, 0xBB] AND tag=0x05, length=3, value=[0x05, 0x04, 0x03]
    QByteArray tlvData = QByteArray::fromHex("0202AABB 05030504 03");

    auto result = ManagementProtocol::parseTlv(tlvData);

    QCOMPARE(result.size(), 2);
    QVERIFY(result.contains(0x02));
    QVERIFY(result.contains(0x05));
    QCOMPARE(result[0x02], QByteArray::fromHex("AABB"));
    QCOMPARE(result[0x05], QByteArray::fromHex("050403"));
}

void TestManagementProtocol::testParseTlv_EmptyData()
{
    QByteArray emptyData;

    auto result = ManagementProtocol::parseTlv(emptyData);

    QVERIFY(result.isEmpty());
}

void TestManagementProtocol::testParseTlv_IncompleteTlv()
{
    // TLV with tag and length but value extends beyond data: tag=0x02, length=10 (claims 10 bytes), value=[only 2 bytes]
    QByteArray incompleteTlv = QByteArray::fromHex("020AABCD");  // length says 10, but only 2 bytes of data

    auto result = ManagementProtocol::parseTlv(incompleteTlv);

    // Should stop parsing and return what was successfully parsed (nothing in this case)
    QVERIFY(result.isEmpty());
}

void TestManagementProtocol::testParseTlv_StopsAtStatusWord()
{
    // TLV: tag=0x02, length=2, value=[0xAA, 0xBB] THEN status word 90 00 THEN more data (should be ignored)
    QByteArray tlvDataWithStatus = QByteArray::fromHex("0202AABB 9000 05020102");

    auto result = ManagementProtocol::parseTlv(tlvDataWithStatus);

    // Should only parse first TLV, stop at 90 00
    QCOMPARE(result.size(), 1);
    QVERIFY(result.contains(0x02));
    QCOMPARE(result[0x02], QByteArray::fromHex("AABB"));
    QVERIFY(!result.contains(0x05));  // Should NOT parse data after status word
}

// ========== Status Word Tests ==========

void TestManagementProtocol::testGetStatusWord_ValidResponse()
{
    // Response with status word 90 00 (success)
    QByteArray response = QByteArray::fromHex("0102030405069000");

    quint16 sw = ManagementProtocol::getStatusWord(response);

    QCOMPARE(sw, (quint16)0x9000);
}

void TestManagementProtocol::testGetStatusWord_TooShort()
{
    // Response with only 1 byte (need 2 for status word)
    QByteArray shortResponse = QByteArray::fromHex("01");

    quint16 sw = ManagementProtocol::getStatusWord(shortResponse);

    QCOMPARE(sw, (quint16)0);
}

void TestManagementProtocol::testIsSuccess_SuccessCode()
{
    QVERIFY(ManagementProtocol::isSuccess(0x9000));
}

void TestManagementProtocol::testIsSuccess_ErrorCodes()
{
    QVERIFY(!ManagementProtocol::isSuccess(0x6D00));  // INS not supported
    QVERIFY(!ManagementProtocol::isSuccess(0x6A80));  // Incorrect data
    QVERIFY(!ManagementProtocol::isSuccess(0x0000));  // Invalid
}

// ========== Device Info Parsing Tests ==========

void TestManagementProtocol::testParseDeviceInfo_CompleteResponse()
{
    // Complete device info response with all fields
    // Format: [LENGTH][TAG VALUE...][SW1 SW2]
    // Tags: 0x02=Serial (4 bytes), 0x05=Firmware (3 bytes), 0x04=Form factor (1 byte)
    QByteArray response = QByteArray::fromHex(
        "0D"                  // Length = 13 bytes of TLV data
        "02040012 3456"       // TAG_SERIAL: 0x00123456
        "05030504 03"         // TAG_FIRMWARE_VERSION: 5.4.3
        "04010 3"             // TAG_FORM_FACTOR: 0x03 (USB-C Keychain)
        "9000"                // Status word: success
    );

    ManagementDeviceInfo info;
    bool result = ManagementProtocol::parseDeviceInfoResponse(response, info);

    QVERIFY(result);
    QCOMPARE(info.serialNumber, (quint32)0x00123456);
    QCOMPARE(info.firmwareVersion.major(), 5);
    QCOMPARE(info.firmwareVersion.minor(), 4);
    QCOMPARE(info.firmwareVersion.patch(), 3);
    QCOMPARE((int)info.formFactor, 0x03);
}

void TestManagementProtocol::testParseDeviceInfo_MinimalResponse()
{
    // Minimal response with only serial number
    QByteArray response = QByteArray::fromHex(
        "06"                  // Length = 6 bytes
        "0204FFFF FFFF"       // TAG_SERIAL: 0xFFFFFFFF
        "9000"                // Status word
    );

    ManagementDeviceInfo info;
    bool result = ManagementProtocol::parseDeviceInfoResponse(response, info);

    QVERIFY(result);
    QCOMPARE(info.serialNumber, (quint32)0xFFFFFFFF);
    // Other fields should be defaults
    QCOMPARE((int)info.formFactor, 0);
}

void TestManagementProtocol::testParseDeviceInfo_InvalidStatusWord()
{
    // Response with error status word (6D 00 = INS not supported)
    QByteArray response = QByteArray::fromHex(
        "00"      // Empty TLV data
        "6D00"    // Status word: error
    );

    ManagementDeviceInfo info;
    bool result = ManagementProtocol::parseDeviceInfoResponse(response, info);

    QVERIFY(!result);  // Should fail due to bad status word
}

void TestManagementProtocol::testParseDeviceInfo_TooShort()
{
    // Response too short (only 1 byte, need at least 2 for status word)
    QByteArray shortResponse = QByteArray::fromHex("01");

    ManagementDeviceInfo info;
    bool result = ManagementProtocol::parseDeviceInfoResponse(shortResponse, info);

    QVERIFY(!result);
}

void TestManagementProtocol::testParseDeviceInfo_SerialNumberParsing()
{
    // Test big-endian parsing of 4-byte serial number
    QByteArray response = QByteArray::fromHex(
        "06"              // Length
        "02041234 5678"   // TAG_SERIAL: 0x12345678 (big-endian)
        "9000"
    );

    ManagementDeviceInfo info;
    bool result = ManagementProtocol::parseDeviceInfoResponse(response, info);

    QVERIFY(result);
    QCOMPARE(info.serialNumber, (quint32)0x12345678);
}

void TestManagementProtocol::testParseDeviceInfo_FirmwareVersionParsing()
{
    // Test firmware version with 4 bytes (major.minor.patch.build, where build is ignored)
    QByteArray response = QByteArray::fromHex(
        "07"               // Length
        "05040102 0304"    // TAG_FIRMWARE_VERSION: 1.2.3.4 (4th byte ignored)
        "9000"
    );

    ManagementDeviceInfo info;
    bool result = ManagementProtocol::parseDeviceInfoResponse(response, info);

    QVERIFY(result);
    QCOMPARE(info.firmwareVersion.major(), 1);
    QCOMPARE(info.firmwareVersion.minor(), 2);
    QCOMPARE(info.firmwareVersion.patch(), 3);
    // 4th byte (build) is ignored
}

void TestManagementProtocol::testParseDeviceInfo_FormFactorParsing()
{
    // Test all known form factors
    struct TestCase {
        quint8 formFactorByte;
        quint32 expectedSerialByte; // Just to make valid response
    };

    QVector<TestCase> testCases = {
        {0x01, 0x00000001},  // USB-A Keychain
        {0x02, 0x00000002},  // USB-A Nano
        {0x03, 0x00000003},  // USB-C Keychain
        {0x04, 0x00000004},  // USB-C Nano
        {0x05, 0x00000005},  // USB-C Lightning
        {0x06, 0x00000006},  // USB-A Bio Keychain
        {0x07, 0x00000007},  // USB-C Bio Keychain
    };

    for (const auto &testCase : testCases) {
        QByteArray response;
        response.append((char)0x09);  // Length = 9 bytes
        response.append((char)0x02);  // TAG_SERIAL
        response.append((char)0x04);  // Length = 4
        response.append((char)((testCase.expectedSerialByte >> 24) & 0xFF));
        response.append((char)((testCase.expectedSerialByte >> 16) & 0xFF));
        response.append((char)((testCase.expectedSerialByte >> 8) & 0xFF));
        response.append((char)(testCase.expectedSerialByte & 0xFF));
        response.append((char)0x04);  // TAG_FORM_FACTOR
        response.append((char)0x01);  // Length = 1
        response.append((char)testCase.formFactorByte);
        response.append((char)0x90);  // Status word
        response.append((char)0x00);

        ManagementDeviceInfo info;
        bool result = ManagementProtocol::parseDeviceInfoResponse(response, info);

        QVERIFY2(result, qPrintable(QString("Form factor 0x%1 parsing failed")
                                    .arg(testCase.formFactorByte, 2, 16, QLatin1Char('0'))));
        QCOMPARE((int)info.formFactor, (int)testCase.formFactorByte);
    }
}

void TestManagementProtocol::testParseDeviceInfo_NfcCapabilities_TwoBytes()
{
    // Test 2-byte NFC capabilities (YubiKey 5 series format)
    QByteArray response = QByteArray::fromHex(
        "0B"               // Length
        "0204DEAD BEEF"    // TAG_SERIAL
        "0D021234"         // TAG_NFC_SUPPORTED: 0x1234 (2 bytes big-endian)
        "9000"
    );

    ManagementDeviceInfo info;
    bool result = ManagementProtocol::parseDeviceInfoResponse(response, info);

    QVERIFY(result);
    QCOMPARE(info.nfcSupported, (quint16)0x1234);
}

void TestManagementProtocol::testParseDeviceInfo_NfcCapabilities_OneByte()
{
    // Test 1-byte NFC capabilities (legacy YubiKey format)
    QByteArray response = QByteArray::fromHex(
        "0A"               // Length
        "0204DEAD BEEF"    // TAG_SERIAL
        "0D01AB"           // TAG_NFC_SUPPORTED: 0xAB (1 byte)
        "9000"
    );

    ManagementDeviceInfo info;
    bool result = ManagementProtocol::parseDeviceInfoResponse(response, info);

    QVERIFY(result);
    QCOMPARE(info.nfcSupported, (quint16)0x00AB);  // Should be promoted to 2 bytes
}

// ========== Form Factor String Tests ==========

void TestManagementProtocol::testFormFactorToString_AllKnownFactors()
{
    QCOMPARE(ManagementProtocol::formFactorToString(0x01), QString("USB-A Keychain"));
    QCOMPARE(ManagementProtocol::formFactorToString(0x02), QString("USB-A Nano"));
    QCOMPARE(ManagementProtocol::formFactorToString(0x03), QString("USB-C Keychain"));
    QCOMPARE(ManagementProtocol::formFactorToString(0x04), QString("USB-C Nano"));
    QCOMPARE(ManagementProtocol::formFactorToString(0x05), QString("USB-C Lightning"));
    QCOMPARE(ManagementProtocol::formFactorToString(0x06), QString("USB-A Bio Keychain"));
    QCOMPARE(ManagementProtocol::formFactorToString(0x07), QString("USB-C Bio Keychain"));
}

void TestManagementProtocol::testFormFactorToString_UnknownFactor()
{
    QString result = ManagementProtocol::formFactorToString(0xFF);

    QVERIFY(result.startsWith("Unknown"));
    QVERIFY(result.contains("ff"));  // Should show hex value in lowercase
}

QTEST_MAIN(TestManagementProtocol)
#include "test_management_protocol.moc"
