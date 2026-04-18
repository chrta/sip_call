// SPDX-License-Identifier: AGPL-3.0-or-later
/*
   Copyright 2017-2020 Christian Taedcke <hacking@taedcke.com>
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include <freertos/queue.h>

#include <boost/sml.hpp>

#include "driver/gpio.h"

namespace sml = boost::sml;

struct e_btn
{
};
struct e_call_start
{
};
struct e_call_end
{
};
struct e_timeout
{
};

template <class SipClientT>
struct dependencies
{
    auto operator()() const noexcept
    {
        using namespace sml;

        const auto action_call = [](SipClientT& d, const auto& event) {
            d.request_ring(CONFIG_CALL_TARGET_USER, CONFIG_CALLER_DISPLAY_MESSAGE);
        };

        const auto action_cancel = [](SipClientT& d, const auto& event) {
            d.request_cancel();
        };

        // Ring-duration timeout only applies while we are still waiting for
        // the callee to pick up. Once the call is established, the user is
        // expected to end it explicitly; we stay in sInCall until the peer
        // hangs up or the call is otherwise terminated.
        return make_transition_table(
            *"idle"_s + event<e_btn> / action_call = "sRinging"_s,
            "sRinging"_s + event<e_timeout> / action_cancel = "idle"_s,
            "sRinging"_s + event<e_call_start> = "sInCall"_s,
            "sRinging"_s + event<e_call_end> = "idle"_s,
            "sInCall"_s + event<e_call_end> = "idle"_s);
    }
};

enum class Event
{
    BUTTON_PRESS,
    CALL_START,
    CALL_END
};

template <class SipClientT, gpio_num_t GPIO_PIN, int RING_DURATION_TIMEOUT_MSEC>
class ButtonInputHandler
{
public:
    explicit ButtonInputHandler(SipClientT& client)
        : m_client { client }
        , m_sm { client }
    {
        m_queue = xQueueCreate(10, sizeof(Event));

        gpio_config_t gpioConfig;
        gpioConfig.pin_bit_mask = (1ULL << GPIO_PIN);
        gpioConfig.mode = GPIO_MODE_INPUT;
        gpioConfig.pull_up_en = GPIO_PULLUP_ENABLE;
        gpioConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
        gpioConfig.intr_type = GPIO_INTR_POSEDGE;
        gpio_config(&gpioConfig);
    }

    void run()
    {
        using namespace sml;

        // This must not be in the contructor, since the constructor may be executed
        // before initializing the internal gpio isr initialization function.
        // In this case the following call would fail and crash afterwards.
        ESP_ERROR_CHECK(gpio_install_isr_service(0));
        ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_PIN, &ButtonInputHandler::int_handler, (void*)this));

        for (;;)
        {
            Event event;
            // The ring-duration timeout only arms while we are still ringing.
            // In idle and during an established call we wait indefinitely.
            TickType_t timeout = m_sm.is("sRinging"_s) ? RING_DURATION_TICKS : portMAX_DELAY;

            if (xQueueReceive(m_queue, &event, timeout))
            {
                if (event == Event::BUTTON_PRESS)
                {
                    m_sm.process_event(e_btn {});
                }
                else if (event == Event::CALL_START)
                {
                    m_sm.process_event(e_call_start {});
                }
                else if (event == Event::CALL_END)
                {
                    m_sm.process_event(e_call_end {});
                }
            }
            else
            {
                m_sm.process_event(e_timeout {});
            }
        }
    }

    void call_start()
    {
        Event event = Event::CALL_START;
        // don't wait if the queue is full
        xQueueSend(m_queue, &event, (TickType_t)0);
    }

    void call_end()
    {
        Event event = Event::CALL_END;
        // don't wait if the queue is full
        xQueueSend(m_queue, &event, (TickType_t)0);
    }

private:
    SipClientT& m_client;
    QueueHandle_t m_queue;

    using ButtonInputHandlerT = ButtonInputHandler<SipClientT, GPIO_PIN, RING_DURATION_TIMEOUT_MSEC>;

    static void int_handler(void* args)
    {
        ButtonInputHandlerT* obj = static_cast<ButtonInputHandlerT*>(args);
        Event event = Event::BUTTON_PRESS;
        xQueueSendToBackFromISR(obj->m_queue, &event, NULL);
    }

    sml::sm<dependencies<SipClientT>> m_sm;

    static constexpr uint32_t RING_DURATION_TICKS = RING_DURATION_TIMEOUT_MSEC / portTICK_PERIOD_MS;
};
