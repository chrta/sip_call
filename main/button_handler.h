/*
   Copyright 2017 Christian Taedcke <hacking@taedcke.com>

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

#pragma once

#include "freertos/FreeRTOS.h"
#include <freertos/queue.h>

#include <boost/sml.hpp>

#include "driver/gpio.h"

namespace sml = boost::sml;

struct e_btn {};
struct e_timeout {};

template<class SipClientT>
struct dependencies {
    auto operator()() const noexcept {
        using namespace sml;

        const auto action_call = [](SipClientT& d, const auto& event) {
            d.request_ring(CONFIG_CALL_TARGET_USER, CONFIG_CALLER_DISPLAY_MESSAGE);
        };

        const auto action_cancel = [](SipClientT& d, const auto& event) {
            d.request_cancel();
        };

        return make_transition_table(
                *"idle"_s        + event<e_btn> / action_call = "sRinging"_s
                , "sRinging"_s   + event<e_timeout> / action_cancel = "idle"_s
        );
    }
};

template<class SipClientT, gpio_num_t GPIO_PIN, int RING_DURATION_TIMEOUT_MSEC>
class ButtonInputHandler {
public:

    explicit ButtonInputHandler(SipClientT& client)
    : m_client{client}
    , m_sm{client}
    {
        m_queue = xQueueCreate(10, sizeof(uint32_t));

        gpio_config_t gpioConfig;
        gpioConfig.pin_bit_mask = (1ULL << GPIO_PIN);
        gpioConfig.mode         = GPIO_MODE_INPUT;
        gpioConfig.pull_up_en   = GPIO_PULLUP_DISABLE;
        gpioConfig.pull_down_en = GPIO_PULLDOWN_ENABLE;
        gpioConfig.intr_type    = GPIO_INTR_POSEDGE;
        gpio_config(&gpioConfig);

        gpio_install_isr_service(0);
        gpio_isr_handler_add(GPIO_PIN, ButtonInputHandler::int_handler, (void*) this);
    }

    void run() {
        using namespace sml;
        for (;;)
        {
            uint32_t io_num;
            TickType_t timeout = m_sm.is("idle"_s) ? portMAX_DELAY : RING_DURATION_TICKS;

            if(xQueueReceive(m_queue, &io_num, timeout)) {
                m_sm.process_event(e_btn{});
            } else {
                m_sm.process_event(e_timeout{});
            }
        }
    }

private:
    SipClientT& m_client;
    QueueHandle_t m_queue;

    using ButtonInputHandlerT = ButtonInputHandler<SipClientT, GPIO_PIN, RING_DURATION_TIMEOUT_MSEC>;

    static void int_handler(void *args) {
        ButtonInputHandlerT* obj  = static_cast<ButtonInputHandlerT*>(args);
        uint32_t gpio = 0;
        xQueueSendToBackFromISR(obj->m_queue, &gpio, NULL);
    }

    sml::sm<dependencies<SipClientT>> m_sm;

    static constexpr uint32_t RING_DURATION_TICKS = RING_DURATION_TIMEOUT_MSEC * portTICK_PERIOD_MS;
};
