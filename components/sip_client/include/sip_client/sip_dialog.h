// SPDX-License-Identifier: AGPL-3.0-or-later
/*
   Copyright 2026 Christian Taedcke <hacking@taedcke.com>
 */

#pragma once

#include "sip_packet.h"
#include <string>

struct Dialog
{
    std::string remote_uri;
    std::string caller_display;
    std::string remote_target;
    std::string remote_tag;
    SipPacket::RecordRouteT route_set;
};
