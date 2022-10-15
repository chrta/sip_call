/*
   Copyright Christian Taedcke <hacking@taedcke.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
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
