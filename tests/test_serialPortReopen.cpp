/**
 * @file test_serialPortReopen.cpp
 * @brief Opening an already-open serial port with a different baud rate must apply the new rate.
 *
 * Uses a pseudo-terminal as the "serial device": on Linux, tcgetattr() on the pty master reports the
 * termios of its slave, so the test can observe the rate the SDK configured on the slave side.
 */

#include <gtest/gtest.h>

#if defined(__linux__)

#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include "serialPort.h"
#include "serialPortPlatform.h"

namespace {

struct Pty {
    int master = -1;
    std::string slaveName;

    Pty() {
        master = posix_openpt(O_RDWR | O_NOCTTY);
        if (master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0) {
            const char* n = ptsname(master);
            if (n) slaveName = n;
        }
    }
    ~Pty() { if (master >= 0) close(master); }

    speed_t speed() const {
        struct termios tty = {};
        if (tcgetattr(master, &tty) != 0) return (speed_t)-1;
        return cfgetospeed(&tty);
    }
};

class SerialPortReopen : public ::testing::Test {
protected:
    Pty pty;
    serial_port_t sp = {};
    port_handle_t port = (port_handle_t)&sp;

    void SetUp() override {
        if (pty.master < 0 || pty.slaveName.empty())
            GTEST_SKIP() << "no pseudo-terminal available";
        serialPortInit(port, 0, PORT_TYPE__UART | PORT_TYPE__COMM, 0);
    }
    void TearDown() override { serialPortClose(port); }
};

} // namespace

TEST_F(SerialPortReopen, ReopenWithDifferentBaudAppliesIt)
{
    ASSERT_EQ(serialPortOpen(port, pty.slaveName.c_str(), 115200, 0), PORT_ERROR__NONE);
    ASSERT_EQ(pty.speed(), (speed_t)B115200);

    ASSERT_EQ(serialPortOpen(port, pty.slaveName.c_str(), 57600, 0), PORT_ERROR__NONE);
    EXPECT_TRUE(serialPortIsOpen(port));
    EXPECT_EQ(pty.speed(), (speed_t)B57600);

    // serialPortOpenRetry() must not short-circuit on an already-open port either.
    ASSERT_EQ(serialPortOpenRetry(port, pty.slaveName.c_str(), 230400, 0), PORT_ERROR__NONE);
    EXPECT_EQ(pty.speed(), (speed_t)B230400);
}

TEST_F(SerialPortReopen, ReopenWithSameSettingsKeepsPortOpen)
{
    ASSERT_EQ(serialPortOpen(port, pty.slaveName.c_str(), 115200, 0), PORT_ERROR__NONE);
    void* handle = sp.handle;

    // Same settings (also via the port's own stored name, which aliases serialPort->portName): no-op.
    EXPECT_EQ(serialPortOpen(port, sp.portName, 115200, 0), PORT_ERROR__NONE);
    EXPECT_EQ(sp.handle, handle);
    EXPECT_TRUE(serialPortIsOpen(port));
    EXPECT_EQ(pty.speed(), (speed_t)B115200);
}

#endif
