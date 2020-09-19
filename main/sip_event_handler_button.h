/*
   Copyright 2020 Christian Taedcke <hacking@taedcke.com>

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
        case SipClientEvent::Event::BUTTON_PRESS:
            break;
        }
    };

private:
    ButtonHandlerT& m_button_input_handler;
};
