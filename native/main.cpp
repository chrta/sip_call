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

#include "asio.hpp"

#include "sip_client/asio_udp_client.h"
#include "sip_client/mbedtls_md5.h"
#include "sip_client/sip_client.h"
#include "sip_client/sip_client_event_handler.h"

#include "keyboard_input.h"

#include <cstring>

constexpr char const* CONFIG_SIP_USER = "620";
constexpr char const* CONFIG_SIP_PASSWORD = "secret";
constexpr char const* CONFIG_SIP_SERVER_IP = "192.168.179.1";
constexpr char const* CONFIG_SIP_SERVER_PORT = "5060";
constexpr char const* CONFIG_LOCAL_IP = "192.168.170.30";

constexpr char const* CONFIG_CALL_TARGET_USER = "9170";
constexpr char const* CONFIG_CALLER_DISPLAY_MESSAGE = "CMDLine";

static constexpr char const* TAG = "main";

using SipClientT = SipClient<AsioUdpClient, MbedtlsMd5>;

struct handlers_t
{
    SipClientT& client;
    KeyboardInput& input;
    asio::io_context& io_context;
};

static void sip_task(void* pvParameters);

void sip_task(void* pvParameters)
{
    auto* ctx = static_cast<handlers_t*>(pvParameters);
    SipClientT& client = ctx->client;
    bool quit = false;

    static std::tuple handlers {
        SipEventHandlerLog {}
    };

    for (; !quit;)
    {
        if (!client.is_initialized())
        {
            const bool result = client.init();
            ESP_LOGI(TAG, "SIP client initialized %ssuccessfully", result ? "" : "un");
            if (!result)
            {
                ESP_LOGI(TAG, "Waiting to try again...");
                sleep(2); // sleep two seconds
                continue;
            }
            client.set_event_handler([](SipClientT& client, const SipClientEvent& event) {
                std::apply([event, &client](auto&... h) { (h.handle(client, event), ...); }, handlers);
            });
        }

        ctx->input.do_read([&client, &quit, ctx](char c) {
            ESP_LOGI(TAG, "keyinput=%c", c);
            if (c == 'c')
            {
                client.request_ring(CONFIG_CALL_TARGET_USER, CONFIG_CALLER_DISPLAY_MESSAGE);
            }
            else if (c == 'd')
            {
                client.request_cancel();
            }
            else if (c == 'q')
            {
                quit = true;
                client.deinit();
                ctx->io_context.stop();
            }
        });
        ctx->io_context.run();
    }
}

int main(int /*unused*/, char** /*unused*/)
{
    // seed for std::rand() used in the sip client
    std::srand(static_cast<unsigned int>(time(nullptr)));

    // Execute io_context.run() only from one thread
    asio::io_context io_context { 1 };

    SipClientT client { io_context, CONFIG_SIP_USER, CONFIG_SIP_PASSWORD, CONFIG_SIP_SERVER_IP, CONFIG_SIP_SERVER_PORT, CONFIG_LOCAL_IP };

    KeyboardInput input { io_context };

    handlers_t handlers { client, input, io_context };

    sip_task(&handlers);
}
