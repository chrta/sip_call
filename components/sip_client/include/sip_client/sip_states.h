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

namespace sml = boost::sml;

template <class SipClientT>
struct sip_states
{
    auto operator()() const noexcept
    {
        using namespace sml;

        const auto idle = state<class idle>;

        const auto action_register_unauth = [](SipClientT& sip, const auto& event) {
            (void)event;
            sip.register_unauth();
        };

        const auto action_register_auth = [](SipClientT& sip, const auto& event) {
            (void)event;
            sip.register_auth();
        };

        const auto action_is_registered = [](SipClientT& sip, const auto& event) {
            sip.schedule_reregister(event.contact_expires);
            sip.is_registered();
        };

        const auto action_retry_reregistered = [](SipClientT& sip, const auto& event) {
            (void)event;
            sip.schedule_reregister(20);
        };

        const auto action_send_invite = [](SipClientT& sip, const auto& event) {
            sip.send_invite(event);
        };

        const auto action_request_call = [](SipClientT& sip, const auto& event) {
            sip.request_call(event);
        };

        const auto action_cancel_call = [](SipClientT& sip, const auto& event) {
            sip.cancel_call(event);
        };

        const auto action_rx_invite = [](SipClientT& sip, const auto& event) {
            sip.handle_invite(event);
        };

        const auto action_call_established = [](SipClientT& sip, const auto& event) {
            (void)event;
            sip.call_established();
        };

        const auto action_call_cancelled = [](SipClientT& sip, const auto& event) {
            (void)event;
            sip.call_cancelled();
        };

        const auto action_call_declined = [](SipClientT& sip, const auto& event) {
            (void)event;
            sip.call_declined(event);
        };

        const auto action_rx_bye = [](SipClientT& sip, const auto& event) {
            (void)event;
            sip.handle_bye();
        };

        const auto action_rx_internal_server_error = [](SipClientT& sip, const auto& event) {
            (void)event;
            sip.handle_internal_server_error();
        };

        return make_transition_table(
            *idle + event<ev_start> / action_register_unauth = "waiting_for_auth_reply"_s,
            "waiting_for_auth_reply"_s + event<ev_401_unauthorized> / action_register_auth = "waiting_for_auth_reply"_s,
            "waiting_for_auth_reply"_s + event<ev_reply_timeout> / action_register_unauth = "waiting_for_auth_reply"_s,
            "waiting_for_auth_reply"_s + event<ev_200_ok> / action_is_registered = "registered"_s,
            "waiting_for_auth_reply"_s + event<ev_500_internal_server_error> / action_rx_internal_server_error = "idle"_s,
            "registered"_s + event<ev_reregister> / action_register_unauth = "waiting_for_auth_reply"_s,
            "registered"_s + event<ev_request_call> / action_request_call = "registered"_s,
            "registered"_s + event<ev_initiate_call> / action_send_invite = "calling"_s,
            "registered"_s + event<ev_rx_invite> / action_rx_invite = "call_established"_s,
            "registered"_s + event<ev_start> / action_register_unauth = "waiting_for_auth_reply"_s,
            "calling"_s + event<ev_401_unauthorized> / action_send_invite = "calling"_s,
            "calling"_s + event<ev_cancel_call> / action_cancel_call = "cancelling"_s,
            "calling"_s + event<ev_183_session_progress> = "calling"_s,
            "calling"_s + event<ev_100_trying> = "calling"_s,
            "calling"_s + event<ev_200_ok> / action_call_established = "call_established"_s,
            "calling"_s + event<ev_487_request_cancelled> / action_call_cancelled = "registered"_s,
            "calling"_s + event<ev_486_busy_here> / action_call_declined = "registered"_s,
            "calling"_s + event<ev_603_decline> / action_call_declined = "registered"_s,
            "calling"_s + event<ev_reregister> / action_retry_reregistered = "calling"_s,
            "calling"_s + event<ev_start> / action_register_unauth = "waiting_for_auth_reply"_s,
            "call_established"_s + event<ev_rx_bye> / action_rx_bye = "registered"_s,
            "call_established"_s + event<ev_200_ok> = X,
            "call_established"_s + event<ev_reregister> / action_retry_reregistered = "call_established"_s,
            "call_established"_s + event<ev_start> / action_register_unauth = "waiting_for_auth_reply"_s,
            "cancelling"_s + event<ev_200_ok> = "cancelling"_s,
            "cancelling"_s + event<ev_487_request_cancelled> / action_call_cancelled = "registered"_s,
            "calling"_s + event<ev_200_ok> = X);
    }
};
