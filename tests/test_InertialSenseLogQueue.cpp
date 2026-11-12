/**
 * @file test_InertialSenseLogQueue.cpp
 * @brief LoggerThread() queue handling: each queued packet is written exactly once.
 */

#include <map>

#include <gtest/gtest.h>

#include "InertialSense.h"

namespace {

using queue_t = InertialSense::log_packet_queue_t;

// Port handles are only used as map keys here, never dereferenced.
port_handle_t const PORT_A = reinterpret_cast<port_handle_t>(0x10);
port_handle_t const PORT_B = reinterpret_cast<port_handle_t>(0x20);

p_data_buf_t packet(uint32_t id)
{
    p_data_buf_t pkt = {};
    pkt.hdr.id = id;
    return pkt;
}

// One LoggerThread() pass: take the shared queue, then write what was taken.
void loggerPass(queue_t& shared, queue_t& pending, std::map<port_handle_t, std::vector<uint32_t>>& written)
{
    InertialSense::TakeLogPackets(shared, pending);
    InertialSense::WriteLogPackets(pending, [&](port_handle_t port, p_data_buf_t& pkt) {
        written[port].push_back(pkt.hdr.id);
        return true;
    });
}

} // namespace

TEST(InertialSenseLogQueue, PacketsAreWrittenExactlyOnce)
{
    queue_t shared, pending;
    std::map<port_handle_t, std::vector<uint32_t>> written;

    shared[PORT_A] = { packet(1), packet(2) };
    loggerPass(shared, pending, written);
    EXPECT_TRUE(shared.empty());

    // Next pass: only port B produced data. Port A's old batch must not be written again.
    shared[PORT_B] = { packet(3) };
    loggerPass(shared, pending, written);

    // Idle passes write nothing.
    loggerPass(shared, pending, written);
    loggerPass(shared, pending, written);

    EXPECT_EQ(written[PORT_A], (std::vector<uint32_t>{ 1, 2 }));
    EXPECT_EQ(written[PORT_B], (std::vector<uint32_t>{ 3 }));
    EXPECT_TRUE(pending.empty());
}

TEST(InertialSenseLogQueue, TakeAppendsToPendingPackets)
{
    queue_t shared, pending;
    pending[PORT_A] = { packet(1) };
    shared[PORT_A] = { packet(2), packet(3) };

    InertialSense::TakeLogPackets(shared, pending);

    ASSERT_EQ(pending[PORT_A].size(), 3u);
    EXPECT_EQ(pending[PORT_A][0].hdr.id, 1u);
    EXPECT_EQ(pending[PORT_A][2].hdr.id, 3u);
    EXPECT_TRUE(shared.empty());
}

TEST(InertialSenseLogQueue, FailedWritesAreCountedAndDropped)
{
    queue_t pending;
    pending[PORT_A] = { packet(1), packet(2), packet(3) };

    int calls = 0;
    size_t failed = InertialSense::WriteLogPackets(pending, [&](port_handle_t, p_data_buf_t& pkt) {
        calls++;
        return pkt.hdr.id != 2;
    });

    EXPECT_EQ(calls, 3);
    EXPECT_EQ(failed, 1u);
    EXPECT_TRUE(pending.empty());

    // Not retried on the next pass.
    calls = 0;
    InertialSense::WriteLogPackets(pending, [&](port_handle_t, p_data_buf_t&) { calls++; return true; });
    EXPECT_EQ(calls, 0);
}
