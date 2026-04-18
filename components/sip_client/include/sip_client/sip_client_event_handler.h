// SPDX-License-Identifier: AGPL-3.0-or-later
/*
   Copyright 2020 Christian Taedcke <hacking@taedcke.com>
 */

#pragma once

#include "esp_log.h"
#include "sip_client_event.h"

/**
 * Logs the occurred events
 */
struct SipEventHandlerLog
{
    template <class T>
    void handle(T& /* client */, const SipClientEvent& event)
    {
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
    };

private:
    static constexpr const char* TAG = "event_logger";
};
