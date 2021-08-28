/*
   Copyright 2017-2019 Christian Taedcke <hacking@taedcke.com>

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
