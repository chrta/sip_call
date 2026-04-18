// SPDX-License-Identifier: AGPL-3.0-or-later
/*
   Copyright 2017-2026 Christian Taedcke <hacking@taedcke.com>
 */

#pragma once

#include <cstdint>

struct SipClientEvent
{
    enum class Event : uint8_t
    {
        CALL_START,
        CALL_CANCELLED,
        CALL_END,
        BUTTON_PRESS,
    };

    enum class CancelReason : uint8_t
    {
        UNKNOWN,
        CALL_DECLINED,
        TARGET_BUSY,
    };

    Event event;
    char button_signal = ' ';
    uint16_t button_duration = 0;
    CancelReason cancel_reason = CancelReason::UNKNOWN;
};
