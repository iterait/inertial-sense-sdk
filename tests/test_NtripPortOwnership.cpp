/**
 * @file test_NtripPortOwnership.cpp
 * @brief The TCP port NtripCorrectionService::connect() creates is owned (closed/released) by the service.
 */

#include <gtest/gtest.h>

#if !defined(_WIN32)

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "NtripCorrectionService.h"

namespace {

// Minimal NTRIP caster: accepts connections, answers each mount-point request with "ICY 200 OK"
// and keeps the connection open so the test can observe when the client closes it.
class FakeCaster {
public:
    FakeCaster() {
        listener = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(listener, (sockaddr*)&addr, sizeof(addr));
        listen(listener, 4);
        socklen_t len = sizeof(addr);
        getsockname(listener, (sockaddr*)&addr, &len);
        port = ntohs(addr.sin_port);
        thread = std::thread([this] { run(); });
    }
    ~FakeCaster() {
        stop = true;
        thread.join();
        for (int fd : clients) close(fd);
        close(listener);
    }

    std::string url() const { return "ntrip://127.0.0.1:" + std::to_string(port) + "/MOUNT"; }

    int client(size_t i) {
        std::lock_guard<std::mutex> lock(mtx);
        return i < clients.size() ? clients[i] : -1;
    }

    // true if the client side of connection i has been closed (EOF seen within timeoutMs)
    bool clientClosed(size_t i, int timeoutMs = 1000) {
        int fd = client(i);
        if (fd < 0) return false;
        pollfd pfd = { fd, POLLIN, 0 };
        if (poll(&pfd, 1, timeoutMs) != 1) return false;
        char c;
        return recv(fd, &c, 1, MSG_DONTWAIT) == 0;
    }

private:
    void run() {
        while (!stop) {
            pollfd pfd = { listener, POLLIN, 0 };
            if (poll(&pfd, 1, 20) != 1) continue;
            int fd = accept(listener, nullptr, nullptr);
            if (fd < 0) continue;
            std::string req;
            char buf[256];
            while (req.find("\r\n\r\n") == std::string::npos) {
                ssize_t n = recv(fd, buf, sizeof(buf), 0);
                if (n <= 0) break;
                req.append(buf, (size_t)n);
            }
            const char* resp = "ICY 200 OK\r\n\r\n";
            send(fd, resp, strlen(resp), MSG_NOSIGNAL);
            std::lock_guard<std::mutex> lock(mtx);
            clients.push_back(fd);
        }
    }

    int listener = -1;
    uint16_t port = 0;
    std::atomic<bool> stop{false};
    std::thread thread;
    std::mutex mtx;
    std::vector<int> clients;
};

} // namespace

TEST(NtripPortOwnership, DestructorClosesTheConnection)
{
    FakeCaster caster;
    auto ntrip = std::make_unique<NtripCorrectionService>();
    ASSERT_TRUE(ntrip->connect(caster.url()));
    ASSERT_TRUE(ntrip->isConnected());
    EXPECT_FALSE(caster.clientClosed(0, 50));

    ntrip.reset();
    EXPECT_TRUE(caster.clientClosed(0)) << "the caster connection outlived the NtripCorrectionService";
}

TEST(NtripPortOwnership, ReconnectReleasesThePreviousConnection)
{
    FakeCaster caster;
    NtripCorrectionService ntrip;
    ASSERT_TRUE(ntrip.connect(caster.url()));
    ASSERT_TRUE(ntrip.connect(caster.url()));
    ASSERT_TRUE(ntrip.isConnected());

    EXPECT_TRUE(caster.clientClosed(0)) << "the first connection was leaked by the second connect()";
    EXPECT_FALSE(caster.clientClosed(1, 50));
}

#endif
