/**
 * @file test_TcpServerListenAddr.cpp
 * @brief TcpServerPortFactory must listen on the configured address, not on every interface.
 */

#include <gtest/gtest.h>

#if defined(__linux__)     // relies on the whole 127.0.0.0/8 block being routed to loopback

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "Rtcm3CorrectionServer.h"

namespace {

bool canConnect(const char* ip, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    const bool ok = (connect(fd, (sockaddr*)&addr, sizeof(addr)) == 0);
    close(fd);
    return ok;
}

} // namespace

TEST(TcpServerListenAddr, BindsOnlyTheConfiguredAddress)
{
    const uint16_t port = 14517;    // arbitrary, distinct from other tests in this binary
    Rtcm3CorrectionServer server(port, "127.0.0.2");
    ASSERT_EQ(server.getLastListenError().first, TCP_LISTEN_CTX__NONE);

    EXPECT_TRUE(canConnect("127.0.0.2", port));
    EXPECT_FALSE(canConnect("127.0.0.1", port)) << "listener is reachable on an address it was not configured for";
}

TEST(TcpServerListenAddr, ResolvesHostNames)
{
    const uint16_t port = 14518;
    Rtcm3CorrectionServer server(port, "localhost");
    ASSERT_EQ(server.getLastListenError().first, TCP_LISTEN_CTX__NONE);
    EXPECT_TRUE(canConnect("127.0.0.1", port));
}

TEST(TcpServerListenAddr, UnusableAddressFailsInsteadOfListeningEverywhere)
{
    const uint16_t port = 14519;
    Rtcm3CorrectionServer server(port, "::1");    // not IPv4: the listener is IPv4-only
    EXPECT_EQ(server.getLastListenError().first, TCP_LISTEN_CTX__BIND);
    EXPECT_FALSE(canConnect("127.0.0.1", port));
}

#endif
