/**
 * @file test_CorrectionService_1029.cpp
 * @brief RTCM3 Msg 1029 (text) extraction must be bounds-checked against the frame size.
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CorrectionService.h"
#include "ISComm.h"
#include "test_serial_utils.h"

namespace {

// Appends `value` as `bits` big-endian bits to an RTCM3 payload under construction.
struct BitWriter {
    std::vector<uint8_t> bytes;
    uint32_t pos = 0;
    void put(uint32_t value, uint32_t bits) {
        for (int i = (int)bits - 1; i >= 0; i--, pos++) {
            if ((pos >> 3) >= bytes.size()) bytes.push_back(0);
            if ((value >> i) & 1) bytes[pos >> 3] |= (uint8_t)(0x80 >> (pos & 7));
        }
    }
};

std::vector<uint8_t> rtcm3Frame(const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> f = { 0xD3, (uint8_t)((payload.size() >> 8) & 0x03), (uint8_t)(payload.size() & 0xFF) };
    f.insert(f.end(), payload.begin(), payload.end());
    unsigned int crc = calculate24BitCRCQ(f.data(), (unsigned int)f.size());
    f.push_back((uint8_t)(crc >> 16));
    f.push_back((uint8_t)(crc >> 8));
    f.push_back((uint8_t)crc);
    return f;
}

std::vector<uint8_t> msg1029Frame(const std::string& text)
{
    BitWriter w;
    w.put(1029, 12);                    // message number
    w.put(42, 12);                      // reference station ID
    w.put(60000, 16);                   // MJD
    w.put(3600, 17);                    // UTC seconds of day
    w.put((uint32_t)text.size(), 7);    // number of characters
    w.put((uint32_t)text.size(), 8);    // number of UTF-8 code units
    std::vector<uint8_t> payload = w.bytes;
    payload.insert(payload.end(), text.begin(), text.end());
    return rtcm3Frame(payload);
}

class CorrectionService1029 : public ::testing::Test {
protected:
    port_handle_t port = nullptr;
    void SetUp() override {
        initTestPorts();
        port = (port_handle_t)TEST0_PORT;       // loopback: what we write is what the service reads
        is_comm_port_init(COMM_PORT(port), NULL);
    }
    void feed(CorrectionService& cs, const std::vector<uint8_t>& data) {
        portWrite(port, data.data(), (int)data.size());
        cs.step();
    }
};

} // namespace

TEST_F(CorrectionService1029, TextIsDeliveredWithoutCrcBytes)
{
    CorrectionService cs(port);
    std::vector<std::string> received;
    cs.addRTCM3Msg1029Listeners([&](std::string s) { received.push_back(s); });

    feed(cs, msg1029Frame("hello caster"));

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0], "hello caster");
}

// A valid (CRC-correct) 1029 frame that is shorter than the fixed 1029 header used to make
// `size - 12` wrap around, i.e. a ~4 GB std::string::assign.
TEST_F(CorrectionService1029, ShortFrameDoesNotUnderflow)
{
    CorrectionService cs(port);
    int calls = 0;
    cs.addRTCM3Msg1029Listeners([&](std::string) { calls++; });

    BitWriter w;
    w.put(1029, 12);
    w.put(0, 4);                        // 2-byte payload: only the message number
    EXPECT_NO_THROW(feed(cs, rtcm3Frame(w.bytes)));
    EXPECT_EQ(calls, 0);

    // The service keeps working afterwards.
    feed(cs, msg1029Frame("ok"));
    EXPECT_EQ(calls, 1);
}

// The declared text length must fit inside the frame; otherwise the frame is ignored.
TEST_F(CorrectionService1029, DeclaredLengthBeyondFrameIsRejected)
{
    CorrectionService cs(port);
    int calls = 0;
    cs.addRTCM3Msg1029Listeners([&](std::string) { calls++; });

    BitWriter w;
    w.put(1029, 12);
    w.put(42, 12);
    w.put(60000, 16);
    w.put(3600, 17);
    w.put(100, 7);
    w.put(200, 8);                      // claims 200 code units...
    std::vector<uint8_t> payload = w.bytes;
    payload.push_back('x');             // ...but carries only one
    feed(cs, rtcm3Frame(payload));
    EXPECT_EQ(calls, 0);
}
