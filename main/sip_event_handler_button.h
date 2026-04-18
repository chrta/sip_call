// SPDX-License-Identifier: AGPL-3.0-or-later
/*
   Copyright 2020 Christian Taedcke <hacking@taedcke.com>
 */

#pragma once

#include "button_handler.h"
#include "sip_client/sip_client_event.h"

/**
 * Notifies the button input handler about call state changes
 */
template <class ButtonHandlerT>
struct SipEventHandlerButton
{

    SipEventHandlerButton(ButtonHandlerT& handler)
        : m_button_input_handler(handler)
    {
    }

    template <class T>
    void handle(T& /* client */, const SipClientEvent& event)
    {
        switch (event.event)
        {
        case SipClientEvent::Event::CALL_CANCELLED:
            m_button_input_handler.call_end();
            break;
        case SipClientEvent::Event::CALL_END:
            m_button_input_handler.call_end();
            break;
        case SipClientEvent::Event::CALL_START:
            m_button_input_handler.call_start();
            break;
        case SipClientEvent::Event::BUTTON_PRESS:
            break;
        }
    };

private:
    ButtonHandlerT& m_button_input_handler;
};
