// SPDX-License-Identifier: AGPL-3.0-or-later
/*
   Copyright 2021-2026 Christian Taedcke <hacking@taedcke.com>
 */

#include <termios.h>

class KeyboardInput
{
public:
    explicit KeyboardInput(asio::io_context& io_context)
        : stream { io_context, ::dup(STDIN_FILENO) }
    {
        termios current {};
        tcgetattr(0, &old_termios);
        current = old_termios;
        current.c_lflag &= ~ICANON; /* disable buffered i/o */
        tcsetattr(0, TCSANOW, &current);
    }

    ~KeyboardInput()
    {
        tcsetattr(0, TCSANOW, &old_termios);
    }

    KeyboardInput(const KeyboardInput&) = delete;
    KeyboardInput& operator=(const KeyboardInput&) = delete;
    KeyboardInput(KeyboardInput&&) = delete;
    KeyboardInput& operator=(KeyboardInput&&) = delete;

    void do_read(const std::function<void(char c)>& on_press)
    {
        asio::async_read(
            stream, asio::buffer(buf),
            [this, on_press](std::error_code ec, std::size_t len) {
                if (ec)
                {
                    ESP_LOGE(TAG, "exit with %s", ec.message().c_str());
                }
                else
                {
                    if ((len == 1) && on_press)
                    {
                        on_press(buf[0]);
                    }
                    do_read(on_press);
                }
            });
    }

private:
    asio::posix::stream_descriptor stream;
    std::array<char, 1> buf {};
    termios old_termios {};

    static constexpr char const* TAG = "key";
};
