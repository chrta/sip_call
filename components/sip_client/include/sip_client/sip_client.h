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

#include "sip_client_internal.h"
#include "sip_sml_events.h"
#include "sip_sml_logger.h"
#include "sip_states.h"

#include <cstdlib>
#include <functional>
#include <string>

template <class SocketT, class Md5T>
class SipClient
{
private:
    using SipClientT = SipClient<SocketT, Md5T>;
    using SipClientInternal = SipClientInt<SocketT, Md5T, sip_states, SipClientT>;
    using SmlSmT = sml::sm<sip_states<SipClientInternal>, sml::logger<Logger>>;

public:
    SipClient(asio::io_context& io_context, const std::string& user, const std::string& pwd, const std::string& server_ip, const std::string& server_port, const std::string& my_ip)
        : m_sip {
            io_context, user, pwd, server_ip, server_port, my_ip, m_sm, *this
        }
        , m_sm { m_sip, m_logger }
    {
    }

    SipClient(const SipClient&) = delete;
    SipClient(SipClient&&) = delete;

    SipClient& operator=(const SipClient&) = delete;
    SipClient& operator=(SipClient&&) = delete;

    bool init()
    {
        return m_sip.init();
    }

    [[nodiscard]] bool is_initialized() const
    {
        return m_sip.is_initialized();
    }

    void set_server_ip(const std::string& server_ip)
    {
        m_sip.set_server_ip(server_ip);
    }

    void set_my_ip(const std::string& my_ip)
    {
        m_sip.set_my_ip(my_ip);
    }

    void set_credentials(const std::string& user, const std::string& password)
    {
        m_sip.set_credentials(user, password);
    }

    void set_event_handler(std::function<void(SipClientT&, const SipClientEvent&)> handler)
    {
        m_sip.set_event_handler(handler);
    }

    /**
     * Initiate a call async
     *
     * \param[in] local_number A number that is registered locally on the server, e.g. "**610"
     * \param[in] caller_display This string is displayed on the caller's phone
     */
    void request_ring(const std::string& local_number, const std::string& caller_display)
    {
        m_sip.request_ring(local_number, caller_display);
    }

    void request_cancel()
    {
        m_sip.request_cancel();
    }

    void deinit()
    {
        m_sip.deinit();
    }

private:
    SipClientInternal m_sip;
    Logger m_logger {};
    SmlSmT m_sm;
};
