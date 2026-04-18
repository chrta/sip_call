// SPDX-License-Identifier: AGPL-3.0-or-later
/*
   Copyright 2017-2021 Christian Taedcke <hacking@taedcke.com>
 */

#pragma once

// event for sip sml state machine
struct ev_start
{
};
struct ev_401_unauthorized
{
};
struct ev_initiate_call
{
};
struct ev_200_ok
{
    const uint32_t contact_expires;
};
struct ev_183_session_progress
{
};
struct ev_100_trying
{
};
struct ev_request_call
{
    const std::string local_number;
    const std::string caller_display;
};
struct ev_cancel_call
{
};

struct ev_rx_invite
{
};

struct ev_rx_bye
{
};

struct ev_487_request_cancelled
{
};

struct ev_486_busy_here
{
};

struct ev_603_decline
{
};

struct ev_500_internal_server_error
{
};

struct ev_reply_timeout
{
};

struct ev_reregister
{
};
