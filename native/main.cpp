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

#include <cstring>

#define CONFIG_RING_DURATION (7000)
#define CONFIG_SIP_USER "620"
#define CONFIG_SIP_PASSWORD "secret"
#define CONFIG_SIP_SERVER_IP "192.168.179.1"
#define CONFIG_SIP_SERVER_PORT "5060"
#define CONFIG_LOCAL_IP "192.168.170.30"

static const char* TAG = "main";

using SipClientT = SipClient<AsioUdpClient, MbedtlsMd5>;

struct handlers_t
{
    SipClientT& client;
    asio::io_context& io_context;
};

static void sip_task(void* pvParameters) __attribute__((noreturn));

void sip_task(void* pvParameters)
{
    auto* handlers = static_cast<handlers_t*>(pvParameters);
    SipClientT& client = handlers->client;

    for (;;)
    {
        if (!client.is_initialized())
        {
            bool result = client.init();
            ESP_LOGI(TAG, "SIP client initialized %ssuccessfully", result ? "" : "un");
            if (!result)
            {
                ESP_LOGI(TAG, "Waiting to try again...");
                sleep(2); //sleep two seconds
                continue;
            }
            client.set_event_handler([](const SipClientEvent& event) {
                switch (event.event)
                {
                case SipClientEvent::Event::CALL_START:
                    ESP_LOGI(TAG, "Call start");
                    break;
                case SipClientEvent::Event::CALL_CANCELLED:
                    ESP_LOGI(TAG, "Call cancelled, reason %d", (int)event.cancel_reason);
                    break;
                case SipClientEvent::Event::CALL_END:
                    ESP_LOGI(TAG, "Call end");
                    break;
                case SipClientEvent::Event::BUTTON_PRESS:
                    ESP_LOGI(TAG, "Got button press: %c for %d milliseconds", event.button_signal, event.button_duration);
                    break;
                }
            });
        }

        handlers->io_context.run();
    }
}

int main(int, char**)
{
    // seed for std::rand() used in the sip client
    std::srand(time(nullptr));

    // Execute io_context.run() only from one thread
    asio::io_context io_context { 1 };

    SipClientT client { io_context, CONFIG_SIP_USER, CONFIG_SIP_PASSWORD, CONFIG_SIP_SERVER_IP, CONFIG_SIP_SERVER_PORT, CONFIG_LOCAL_IP };

    handlers_t handlers { client, io_context };

    sip_task(&handlers);
}
