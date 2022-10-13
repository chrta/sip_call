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

extern "C" {
#include "esp_netif.h"
#include "esp_wifi_default.h"
}

#include "esp_event.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "sip_client/asio_udp_client.h"
#include "sip_client/mbedtls_md5.h"
#include "sip_client/sip_client.h"
#include "sip_client/sip_client_event_handler.h"

#include "button_handler.h"
#include "sip_event_handler_actuator.h"
#include "sip_event_handler_button.h"

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

static std::string ip_to_string(const esp_ip4_addr_t* ip)
{
    static constexpr size_t BUFFER_SIZE = 16;
    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE, IPSTR, IP2STR(ip));
    return std::string(buffer);
}

#ifdef CONFIG_SIP_SERVER_IS_DHCP_SERVER
static std::string get_gw_ip_address(const system_event_sta_got_ip_t* got_ip)
{
    const ip4_addr_t* gateway = &got_ip->ip_info.gw;
    return ip_to_string(gateway);
}
#endif //CONFIG_SIP_SERVER_IS_DHCP_SERVER

static std::string get_local_ip_address(const esp_ip4_addr_t* got_ip)
{
    return ip_to_string(got_ip);
}

using ButtonInputHandlerT = ButtonInputHandler<SipClientT, BELL_GPIO_PIN, RING_DURATION_TIMEOUT_MSEC>;

struct handlers_t
{
    SipClientT* client;
    ButtonInputHandlerT* button_input_handler;
    asio::io_context* io_context;
} handlers;
/* This global handlers instance is only necessary because using event_data in event_handler is not possible */

static void event_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    /* Handlers from event_data does not work. The members are invalid somehow
     * handlers_t* ctx = static_cast<handlers_t*>(event_data); would be better,
     * because in that case no global handlers would be necessary.
     */
    handlers_t* ctx = &handlers;

    ESP_LOGI(TAG, "Event: %s %d", event_base, event_id);

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (ctx == nullptr || ctx->client == nullptr || ctx->io_context == nullptr)
        {
            return;
        }

        ESP_LOGI(TAG, "Wifi STA disconnected, deinit client");
        ctx->client->deinit();
        ctx->io_context->stop();
        /* This is a workaround as ESP32 WiFi libs don't currently auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        if (ctx == nullptr || ctx->client == nullptr)
        {
            return;
        }
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        esp_ip4_addr_t* got_ip = &event->ip_info.ip;
#ifdef CONFIG_SIP_SERVER_IS_DHCP_SERVER
        ctx->client->set_server_ip(get_gw_ip_address(got_ip));
#endif //CONFIG_SIP_SERVER_IS_DHCP_SERVER
        ctx->client->set_my_ip(get_local_ip_address(got_ip));
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
    }
}

static void initialize_wifi()
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, CONFIG_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, CONFIG_WIFI_PASSWORD, sizeof(wifi_config.sta.password));

    /* Setting a password implies station will connect to all security modes including WEP/WPA.
      * However these modes are deprecated and not advisable to be used. Incase your Access point
      * doesn't support WPA2, these mode can be enabled by commenting below line */
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    /* start wifi later */
}

static void start_wifi()
{
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "esp_wifi_set_ps().");
    esp_wifi_set_ps(DEFAULT_PS_MODE);
}

static void sip_task(void* pvParameters)
{
    handlers_t* ctx = static_cast<handlers_t*>(pvParameters);
    SipClientT& client = *ctx->client;

    static std::tuple handlers {
        SipEventHandlerLog {},
        SipEventHandlerButton { *ctx->button_input_handler },
#ifdef CONFIG_ACTUATOR_ENABLED
        SipEventHandlerActuator<static_cast<gpio_num_t>(CONFIG_ACTUATOR_OUTPUT_GPIO),
            CONFIG_ACTUATOR_ACTIVE_LEVEL,
            CONFIG_ACTUATOR_SWITCHING_DURATION,
            CONFIG_ACTUATOR_PHONE_BUTTON[0]> {},
#endif /* ACTUATOR_ENABLED */
    };

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

            client.set_event_handler([](SipClientT& client, const SipClientEvent& event) {
                std::apply([event, &client](auto&... h) { (h.handle(client, event), ...); }, handlers);
            });
        }

        ctx->io_context->run();
        // The io_context was stopped, to be able to call run() again later, restart must be called.
        ctx->io_context->restart();
    }
}

extern "C" void app_main(void)
{
    // seed for std::rand() used in the sip client
    std::srand(esp_random());
    ESP_ERROR_CHECK(nvs_flash_init());
    initialize_wifi();

    asio::io_context io_context { 1 };

    SipClientT client { io_context, CONFIG_SIP_USER, CONFIG_SIP_PASSWORD, CONFIG_SIP_SERVER_IP, CONFIG_SIP_SERVER_PORT, CONFIG_LOCAL_IP };
    ButtonInputHandler<SipClientT, BELL_GPIO_PIN, RING_DURATION_TIMEOUT_MSEC> button_input_handler(client);

    handlers.client = &client;
    handlers.button_input_handler = &button_input_handler;
    handlers.io_context = &io_context;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, &handlers, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, &handlers, nullptr));

    start_wifi();

    // reseed after initializing wifi
    std::srand(esp_random());

    // without pinning this to core 0, funny stuff (crashes) happen,
    // because some c++ objects are not fully initialized
    xTaskCreatePinnedToCore(&sip_task, "sip_task", 8192, &handlers, 5, NULL, 0);

    //blocks forever
    button_input_handler.run();
}
