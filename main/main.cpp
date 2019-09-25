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

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "sip_client/lwip_udp_client.h"
#include "sip_client/mbedtls_md5.h"
#include "sip_client/sip_client.h"

#include "button_handler.h"

#include <string.h>

static constexpr auto BELL_GPIO_PIN = static_cast<gpio_num_t>(CONFIG_BELL_INPUT_GPIO);
// GPIO_NUM_23 is connected to the opto coupler
static constexpr auto RING_DURATION_TIMEOUT_MSEC = CONFIG_RING_DURATION;

#if CONFIG_POWER_SAVE_MODEM_MAX
#define DEFAULT_PS_MODE WIFI_PS_MAX_MODEM
#elif CONFIG_POWER_SAVE_MODEM_MIN
#define DEFAULT_PS_MODE WIFI_PS_MIN_MODEM
#elif CONFIG_POWER_SAVE_NONE
#define DEFAULT_PS_MODE WIFI_PS_NONE
#else
#define DEFAULT_PS_MODE WIFI_PS_NONE
#endif /*CONFIG_POWER_SAVE_MODEM*/

/* FreeRTOS event group to signal when we are connected properly */
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about one event
 * - are we connected to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

static const char* TAG = "main";

using SipClientT = SipClient<AsioUdpClient, MbedtlsMd5>;

static std::string ip_to_string(const ip4_addr_t* ip)
{
    static constexpr size_t BUFFER_SIZE = 16;
    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE, IPSTR, IP2STR(ip));
    return std::string(buffer);
}

static std::string get_gw_ip_address(const system_event_sta_got_ip_t* got_ip)
{
    const ip4_addr_t* gateway = &got_ip->ip_info.gw;
    return ip_to_string(gateway);
}

static std::string get_local_ip_address(const system_event_sta_got_ip_t* got_ip)
{
    const ip4_addr_t* local_addr = &got_ip->ip_info.ip;
    return ip_to_string(local_addr);
}

static esp_err_t event_handler(void* ctx, system_event_t* event)
{
    SipClientT *client = static_cast<SipClientT *>(ctx);
    switch (event->event_id)
    {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
    {
        system_event_sta_got_ip_t* got_ip = &event->event_info.got_ip;
        //client.set_server_ip(get_gw_ip_address(got_ip));
        client->set_my_ip(get_local_ip_address(got_ip));
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
    }
    break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        client->deinit();
        /* This is a workaround as ESP32 WiFi libs don't currently auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void initialize_wifi(void* ctx)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, ctx));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, CONFIG_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, CONFIG_WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.bssid_set = false;

    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "esp_wifi_set_ps().");
    esp_wifi_set_ps(DEFAULT_PS_MODE);
}

struct handlers_t {
	SipClientT &client;
	ButtonInputHandler<SipClientT, BELL_GPIO_PIN, RING_DURATION_TIMEOUT_MSEC>& button_input_handler;
	asio::io_context &io_context;
};


static void sip_task(void* pvParameters)
{
    handlers_t* handlers = static_cast<handlers_t *>(pvParameters);
    SipClientT& client = handlers->client;
    ButtonInputHandler<SipClientT, BELL_GPIO_PIN, RING_DURATION_TIMEOUT_MSEC>& button_input_handler = handlers->button_input_handler;

    for (;;)
    {
        // Wait for wifi connection
        xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);

        if (!client.is_initialized())
        {
            bool result = client.init();
            ESP_LOGI(TAG, "SIP client initialized %ssuccessfully", result ? "" : "un");
            if (!result)
            {
                ESP_LOGI(TAG, "Waiting to try again...");
                vTaskDelay(2000 / portTICK_RATE_MS);
                continue;
            }
            client.set_event_handler([&button_input_handler](const SipClientEvent& event) {
                switch (event.event)
                {
                case SipClientEvent::Event::CALL_START:
                    ESP_LOGI(TAG, "Call start");
                    break;
                case SipClientEvent::Event::CALL_CANCELLED:
                    ESP_LOGI(TAG, "Call cancelled, reason %d", (int)event.cancel_reason);
                    button_input_handler.call_end();
                    break;
                case SipClientEvent::Event::CALL_END:
                    ESP_LOGI(TAG, "Call end");
                    button_input_handler.call_end();
                    break;
                case SipClientEvent::Event::BUTTON_PRESS:
                    ESP_LOGI(TAG, "Got button press: %c for %d milliseconds", event.button_signal, event.button_duration);
                    break;
                }
            });
        }

        //client.run();
	handlers->io_context.run();

    }
}

extern "C" void app_main(void)
{
    // seed for std::rand() used in the sip client
    std::srand(esp_random());
    // Execute io_context.run() only from one thread
    asio::io_context io_context{1};


    SipClientT client { io_context, CONFIG_SIP_USER, CONFIG_SIP_PASSWORD, CONFIG_SIP_SERVER_IP, CONFIG_SIP_SERVER_PORT, CONFIG_LOCAL_IP };
    ButtonInputHandler<SipClientT, BELL_GPIO_PIN, RING_DURATION_TIMEOUT_MSEC>  button_input_handler(client);

    handlers_t handlers{client, button_input_handler, io_context};
    nvs_flash_init();
    initialize_wifi(&client);

    // reseed after initializing wifi
    std::srand(esp_random());
    // without pinning this to core 0, funny stuff (crashes) happen,
    // because some c++ objects are not fully initialized
    xTaskCreatePinnedToCore(&sip_task, "sip_task", 4096, &handlers, 5, NULL, 0);

    //blocks forever
    button_input_handler.run();
}
