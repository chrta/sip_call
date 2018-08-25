/*
   Copyright 2017 Christian Taedcke <hacking@taedcke.com>

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

#include "sip_packet.h"

#define USE_SML

#ifdef USE_SML
#include "boost/sml.hpp"
#endif

#include <functional>
#include <cstdlib>
#include <string>


static void rtp_task(void *pvParameters)
{
    LwipUdpClient *socket = (LwipUdpClient *) pvParameters;
    for(;;)
    {
        if (!socket->is_initialized())
        {
            vTaskDelay(2000 / portTICK_RATE_MS);
            continue;
        }

        std::string data = socket->receive(5000);
        ESP_LOGV("RTP", "Received %d byte", data.size());
    }
}


struct SipClientEvent {
        enum class Event {
            CALL_START,
            CALL_CANCELLED,
            CALL_END,
            BUTTON_PRESS,
        };

        Event event;
        char button_signal;
        uint16_t button_duration;
};

template <class SocketT, class Md5T>
class SipClientInt
{
public:
    SipClientInt(const std::string& user, const std::string& pwd, const std::string& server_ip, const std::string& server_port, const std::string& my_ip)
    : m_socket(server_ip, server_port, LOCAL_PORT)
    , m_rtp_socket(server_ip, "7078", LOCAL_RTP_PORT)
    , m_server_ip(server_ip)
    , m_user(user)
    , m_pwd(pwd)
    , m_my_ip(my_ip)
    , m_uri("sip:" + server_ip)
    , m_to_uri("sip:" + user + "@" + server_ip)
    , m_sip_sequence_number(std::rand() % 2147483647)
    , m_call_id(std::rand() % 2147483647)
    , m_response("")
    , m_realm("")
    , m_nonce("")
    , m_tag(std::rand() % 2147483647)
    , m_branch(std::rand() % 2147483647)
    , m_caller_display(m_user)
    , m_sdp_session_id(0)
    , m_command_event_group(xEventGroupCreate())
    {
        xTaskCreate(&rtp_task, "rtp_task", 4096, &m_rtp_socket, 4, NULL);
    }

    ~SipClientInt()
    {
    }

    bool init()
    {
        bool result_rtp = m_rtp_socket.init();
        bool result_sip = m_socket.init();
        return result_rtp && result_sip;
    }

    bool is_initialized() const
    {
        return m_socket.is_initialized();
    }

    void set_server_ip(const std::string& server_ip)
    {
        m_server_ip = server_ip;
        m_socket.set_server_ip(server_ip);
        m_rtp_socket.set_server_ip(server_ip);
        m_uri = "sip:" + server_ip;
        m_to_uri = "sip:" + m_user + "@" + server_ip;
    }

    void set_my_ip(const std::string& my_ip)
    {
        m_my_ip = my_ip;
    }

    void set_credentials(const std::string& user, const std::string& password)
    {
        m_user = user;
        m_pwd = password;
        m_to_uri = "sip:" + m_user + "@" + m_server_ip;
    }

    void set_event_handler(std::function<void(const SipClientEvent&)> handler)
    {
        m_event_handler = handler;
    }

    /**
     * Initiate a call async
     *
     * \param[in] local_number A number that is registered locally on the server, e.g. "**610"
     * \param[in] caller_display This string is displayed on the caller's phone
     */
    void request_ring(const std::string& local_number, const std::string& caller_display)
    {
        if (m_state == SipState::REGISTERED)
        {
            ESP_LOGI(TAG, "Request to call %s...", local_number.c_str());
            m_call_id = std::rand() % 2147483647;
            m_uri = "sip:" + local_number + "@" + m_server_ip;
            m_to_uri = "sip:" + local_number + "@" +  m_server_ip;
            m_caller_display = caller_display;
            xEventGroupSetBits(m_command_event_group, COMMAND_DIAL_BIT);
        }
    }

    void request_cancel()
    {
        if ((m_state == SipState::RINGING) || (m_state == SipState::CALL_IN_PROGRESS))
        {
            ESP_LOGI(TAG, "Request to CANCEL call");
            xEventGroupSetBits(m_command_event_group, COMMAND_CANCEL_BIT);
        }
    }

    void run()
    {
        tx();
        rx();
    }

    //empty test function for sml transition
    void test() const {}

private:
    enum class SipState {
        IDLE,
        REGISTER_UNAUTH,
        REGISTER_AUTH,
        REGISTERED,
        INVITE_UNAUTH,
        INVITE_UNAUTH_SENT,
        INVITE_AUTH,
        RINGING,
        CALL_START,
        CALL_IN_PROGRESS,
        CANCELLED,
        ERROR,
    };


    void tx()
    {
        switch (m_state)
        {
        case SipState::IDLE:
            //fall-through
        case SipState::REGISTER_UNAUTH:
            //sending REGISTER without auth
            m_tag = std::rand() % 2147483647;
            m_branch = std::rand() % 2147483647;
            send_sip_register();
            m_tag = std::rand() % 2147483647;
            m_branch = std::rand() % 2147483647;
            break;
        case SipState::REGISTER_AUTH:
            //sending REGISTER with auth
            compute_auth_response("REGISTER", "sip:" + m_server_ip);
            send_sip_register();
            break;
        case SipState::REGISTERED:
            //wait for request
            break;
        case SipState::INVITE_UNAUTH:
            //sending INVITE without auth
            //m_tag = std::rand() % 2147483647;
            m_sdp_session_id = std::rand();
            send_sip_invite();
            break;
        case SipState::INVITE_UNAUTH_SENT:
            break;
        case SipState::INVITE_AUTH:
            //sending INVITE with auth
            m_branch = std::rand() % 2147483647;
            compute_auth_response("INVITE", m_uri);
            send_sip_invite();
            break;
        case SipState::RINGING:
            if (xEventGroupWaitBits(m_command_event_group, COMMAND_CANCEL_BIT, true, true, 0))
            {
                ESP_LOGD(TAG, "Sending cancel request");
                send_sip_cancel();
            }
            break;
        case SipState::CALL_START:
            send_sip_ack();
            break;
        case SipState::CALL_IN_PROGRESS:
            if (xEventGroupWaitBits(m_command_event_group, COMMAND_CANCEL_BIT, true, true, 0))
            {
                ESP_LOGD(TAG, "Sending bye request");
                //send_sip_bye();
            }
            break;
        case SipState::CANCELLED:
            send_sip_ack();
            m_tag = std::rand() % 2147483647;
            m_branch = std::rand() % 2147483647;
            break;
        case SipState::ERROR:
            break;
        }
    }

    void rx()
    {
        if (m_state == SipState::REGISTERED)
        {
            if (xEventGroupWaitBits(m_command_event_group, COMMAND_DIAL_BIT, true, true, 0))
            {
                m_state = SipState::INVITE_UNAUTH;
                log_state_transition(SipState::REGISTERED, m_state);
            }
            //return;
        }
        else if (m_state == SipState::ERROR)
        {
            vTaskDelay(2000 / portTICK_RATE_MS);
            m_sip_sequence_number++;
            m_state = SipState::IDLE;
            log_state_transition(SipState::ERROR, m_state);
            return;
        }
        else if (m_state == SipState::INVITE_UNAUTH)
        {
            m_state = SipState::INVITE_UNAUTH_SENT;
            log_state_transition(SipState::INVITE_UNAUTH, m_state);
            return;
        }
        else if (m_state == SipState::CALL_START)
        {
            m_state = SipState::CALL_IN_PROGRESS;
            log_state_transition(SipState::CALL_START, m_state);
            return;
        }

        //The timeout is a workaround to be able to end a CANCEL request during ring
        std::string recv_string = m_socket.receive(SOCKET_RX_TIMEOUT_MSEC);

        if (recv_string.empty())
        {
            return;
        }

        SipPacket packet(recv_string.c_str(), recv_string.size());
        if (!packet.parse())
        {
            ESP_LOGI(TAG, "Parsing the packet failed");
            return;
        }

        SipPacket::Status reply = packet.get_status();
        ESP_LOGI(TAG, "Parsing the packet ok, reply code=%d", (int) packet.get_status());

        if (reply == SipPacket::Status::SERVER_ERROR_500)
        {
            log_state_transition(m_state, SipState::ERROR);
            m_state = SipState::ERROR;
            return;
        }
        else if ((reply == SipPacket::Status::UNAUTHORIZED_401) || (reply == SipPacket::Status::PROXY_AUTH_REQ_407))
        {
            m_realm = packet.get_realm();
            m_nonce = packet.get_nonce();
        }
        else if ((reply == SipPacket::Status::UNKNOWN) &&
                 ((packet.get_method() == SipPacket::Method::NOTIFY) ||
                  (packet.get_method() == SipPacket::Method::BYE) ||
                  (packet.get_method() == SipPacket::Method::INFO) ||
                  (packet.get_method() == SipPacket::Method::INVITE)))
        {
            send_sip_ok(packet);
        }

        if (!packet.get_contact().empty())
        {
            m_to_contact = packet.get_contact();
        }

        if (!packet.get_to_tag().empty())
        {
            m_to_tag = packet.get_to_tag();
        }

        SipState old_state = m_state;
        switch (m_state)
        {
        case SipState::IDLE:
            //fall-trough
        case SipState::REGISTER_UNAUTH:
            m_state = SipState::REGISTER_AUTH;
            m_sip_sequence_number++;
            break;
        case SipState::REGISTER_AUTH:
            if (reply == SipPacket::Status::OK_200)
            {
                m_sip_sequence_number++;
                m_nonce = "";
                m_realm = "";
                m_response = "";
                ESP_LOGI(TAG, "OK :)");
                m_uri = "sip:**613@" + m_server_ip;
                m_to_uri = "sip:**613@" +  m_server_ip;
                m_state = SipState::REGISTERED;
            }
            else
            {
                m_state = SipState::ERROR;
            }
            break;
        case SipState::REGISTERED:
            if (packet.get_method() == SipPacket::Method::INVITE)
            {
                    //received an invite, answered it already with ok, so new call is established, because someone called us
                    m_state = SipState::CALL_START;
                    if (m_event_handler)
                    {
                            m_event_handler(SipClientEvent{SipClientEvent::Event::CALL_START, ' ', 0});
                    }
            }
            break;
        case SipState::INVITE_UNAUTH_SENT:
        case SipState::INVITE_UNAUTH:
            if ((reply == SipPacket::Status::UNAUTHORIZED_401) || (reply == SipPacket::Status::PROXY_AUTH_REQ_407))
            {
                m_state = SipState::INVITE_AUTH;
                send_sip_ack();
                m_sip_sequence_number++;
            }
            else if((reply == SipPacket::Status::OK_200) || (reply == SipPacket::Status::SESSION_PROGRESS_183))
            {
                m_state = SipState::RINGING;
                m_nonce = "";
                m_realm = "";
                m_response = "";
                ESP_LOGV(TAG, "Start RINGing...");
            }
            else if (reply != SipPacket::Status::TRYING_100)
            {
                m_state = SipState::ERROR;
            }
            break;
        case SipState::INVITE_AUTH:
            if ((reply == SipPacket::Status::UNAUTHORIZED_401) || (reply == SipPacket::Status::PROXY_AUTH_REQ_407))
            {
                m_state = SipState::ERROR;
            }
            else if((reply == SipPacket::Status::OK_200) || (reply == SipPacket::Status::SESSION_PROGRESS_183) || (reply == SipPacket::Status::TRYING_100))
            {
                //trying is not yet ringing, but change state to not send invite again
                m_state = SipState::RINGING;
                m_nonce = "";
                m_realm = "";
                m_response = "";
                ESP_LOGV(TAG, "Start RINGing...");
            }
            else
            {
                m_state = SipState::ERROR;
            }
            break;
        case SipState::RINGING:
            if (reply == SipPacket::Status::SESSION_PROGRESS_183)
            {
                //TODO parse session progress reply and send appropriate answer
                //m_state = SipState::ERROR;
            }
            else if (reply == SipPacket::Status::OK_200)
            {
                //other side picked up, send an ack
                m_state = SipState::CALL_START;
                if (m_event_handler)
                {
                    m_event_handler(SipClientEvent{SipClientEvent::Event::CALL_START, ' ', 0});
                }
            }
            else if (reply == SipPacket::Status::REQUEST_CANCELLED_487)
            {
                m_state = SipState::CANCELLED;
                if (m_event_handler)
                {
                    m_event_handler(SipClientEvent{SipClientEvent::Event::CALL_CANCELLED, ' ', 0});
                }
            }
            else if (reply == SipPacket::Status::PROXY_AUTH_REQ_407)
            {
                send_sip_ack();
                m_sip_sequence_number++;
                m_state = SipState::INVITE_AUTH;
                ESP_LOGV(TAG, "Go back to send invite with auth...");
            }
            else if ((reply == SipPacket::Status::DECLINE_603) || (reply == SipPacket::Status::BUSY_HERE_486))
            {
                send_sip_ack();
                m_sip_sequence_number++;
                m_branch = std::rand() % 2147483647;
                m_state = SipState::REGISTERED;
                if (m_event_handler)
                {
                    m_event_handler(SipClientEvent{SipClientEvent::Event::CALL_CANCELLED, ' ', 0});
                }
            }
            break;
        case SipState::CALL_START:
            //should not reach this point
            break;
        case SipState::CALL_IN_PROGRESS:
            if (packet.get_method() == SipPacket::Method::BYE)
            {
                m_sip_sequence_number++;
                m_state = SipState::REGISTERED;
                if (m_event_handler)
                {
                    m_event_handler(SipClientEvent{SipClientEvent::Event::CALL_END, ' ', 0});
                }
            }
            else if ((packet.get_method() == SipPacket::Method::INFO)
                     && (packet.get_content_type() == SipPacket::ContentType::APPLICATION_DTMF_RELAY))
            {
                if (m_event_handler)
                {
                    m_event_handler(SipClientEvent{SipClientEvent::Event::BUTTON_PRESS, packet.get_dtmf_signal(), packet.get_dtmf_duration()});
                }
            }
            break;
        case SipState::CANCELLED:
            if (reply == SipPacket::Status::OK_200)
            {
                m_sip_sequence_number++;
                m_state = SipState::REGISTERED;
            }
            break;
        case SipState::ERROR:
            m_sip_sequence_number++;
            m_state = SipState::IDLE;
            break;
        }

        if (old_state != m_state)
        {
            log_state_transition(old_state, m_state);
        }
    }

    void send_sip_register()
    {
        TxBufferT& tx_buffer = m_socket.get_new_tx_buf();
        std::string uri = "sip:" + m_server_ip;

        send_sip_header("REGISTER", uri, "sip:" + m_user + "@" + m_server_ip, tx_buffer);

        tx_buffer << "Contact: \"" << m_user << "\" <sip:" << m_user << "@" << m_my_ip << ":" << LOCAL_PORT << ";transport=" << TRANSPORT_LOWER << ">\r\n";

        if (!m_response.empty())
        {
            tx_buffer << "Authorization: Digest username=\"" << m_user << "\", realm=\"" << m_realm << "\", nonce=\"" << m_nonce << "\", uri=\"" << uri << "\", algorithm=MD5, response=\"" << m_response << "\"\r\n";
        }
        tx_buffer << "Allow: INVITE, ACK, CANCEL, OPTIONS, BYE, REFER, NOTIFY, MESSAGE, SUBSCRIBE, INFO\r\n";
        tx_buffer << "Expires: 3600\r\n";
        tx_buffer << "Content-Length: 0\r\n";
        tx_buffer << "\r\n";

        m_socket.send_buffered_data();
    }

    void send_sip_invite()
    {
        TxBufferT& tx_buffer = m_socket.get_new_tx_buf();

        send_sip_header("INVITE", m_uri, m_to_uri, tx_buffer);

        tx_buffer << "Contact: \"" << m_user << "\" <sip:" << m_user << "@" << m_my_ip << ":" << LOCAL_PORT << ";transport=" << TRANSPORT_LOWER << ">\r\n";

        if (!m_response.empty())
        {
            //tx_buffer << "Proxy-Authorization: Digest username=\"" << m_user << "\", realm=\"" << m_realm << "\", nonce=\"" << m_nonce << "\", uri=\"" << m_uri << "\", response=\"" << m_response << "\"\r\n";
            tx_buffer << "Authorization: Digest username=\"" << m_user << "\", realm=\"" << m_realm << "\", nonce=\"" << m_nonce << "\", uri=\"" << m_uri << "\", response=\"" << m_response << "\"\r\n";
        }
        tx_buffer << "Content-Type: application/sdp\r\n";
        tx_buffer << "Allow: INVITE, ACK, CANCEL, OPTIONS, BYE, REFER, NOTIFY, MESSAGE, SUBSCRIBE, INFO\r\n";
        m_tx_sdp_buffer.clear();
        m_tx_sdp_buffer << "v=0\r\n"
                << "o=" << m_user << " " << m_sdp_session_id << " " << m_sdp_session_id << " IN IP4 " << m_my_ip << "\r\n"
                << "s=sip-client/0.0.1\r\n"
                << "c=IN IP4 " << m_my_ip << "\r\n"
                << "t=0 0\r\n"
                << "m=audio "<< LOCAL_RTP_PORT << " RTP/AVP 0 8 101\r\n"
                //<< "a=sendrecv\r\n"
                << "a=recvonly\r\n"
                << "a=rtpmap:101 telephone-event/8000\r\n"
                << "a=fmtp:101 0-15\r\n"
                << "a=ptime:20\r\n";

        tx_buffer << "Content-Length: " << m_tx_sdp_buffer.size() << "\r\n";
        tx_buffer << "\r\n";
        tx_buffer << m_tx_sdp_buffer.data();

        m_socket.send_buffered_data();
    }

    /**
     * CANCEL a pending INVITE
     *
     * To match the INVITE, the following parameter must not be changed:
     * * CSeq
     * * From tag value
     */
    void send_sip_cancel()
    {
        TxBufferT& tx_buffer = m_socket.get_new_tx_buf();

        send_sip_header("CANCEL", m_uri, m_to_uri, tx_buffer);

        if (!m_response.empty())
        {
            tx_buffer << "Contact: \"" << m_user << "\" <sip:" << m_user << "@" << m_my_ip << ":" << LOCAL_PORT << ";transport=" << TRANSPORT_LOWER << ">\r\n";
            tx_buffer << "Content-Type: application/sdp\r\n";
            tx_buffer << "Authorization: Digest username=\"" << m_user << "\", realm=\"" << m_realm << "\", nonce=\"" << m_nonce << "\", uri=\"" << m_uri << "\", response=\"" << m_response << "\"\r\n";
        }
        tx_buffer << "Content-Length: 0\r\n";
        tx_buffer << "\r\n";

        m_socket.send_buffered_data();
    }

    void send_sip_ack()
    {
        TxBufferT& tx_buffer = m_socket.get_new_tx_buf();
        if (m_state == SipState::CALL_START)
        {
            send_sip_header("ACK", m_to_contact, m_to_uri, tx_buffer);
            //std::string m_sdp_session_o;
            //std::string m_sdp_session_s;
            //std::string m_sdp_session_c;
            //m_tx_sdp_buffer.clear();
            //TODO: populate sdp body
            //m_tx_sdp_buffer << "v=0\r\n"
            //	              << m_sdp_session_o << "\r\n"
            //	      << m_sdp_session_s << "\r\n"
            //	      << m_sdp_session_c << "\r\n"
            //	      << "t=0 0\r\n";
            //TODO: copy each m line and select appropriate a line
            //tx_buffer << "Content-Type: application/sdp\r\n";
            //tx_buffer << "Content-Length: " << m_tx_sdp_buffer.size() << "\r\n";
            //tx_buffer << "Allow-Events: telephone-event\r\n";
            tx_buffer << "Content-Length: 0\r\n";
            tx_buffer << "\r\n";
            //tx_buffer << m_tx_sdp_buffer.data();
        }
        else
        {
            send_sip_header("ACK", m_uri, m_to_uri, tx_buffer);
            tx_buffer << "Content-Length: 0\r\n";
            tx_buffer << "\r\n";
        }
        m_socket.send_buffered_data();
    }

    void send_sip_ok(const SipPacket& packet)
    {
        TxBufferT& tx_buffer = m_socket.get_new_tx_buf();

        send_sip_reply_header("200 OK", packet, tx_buffer);
        tx_buffer << "Content-Length: 0\r\n";
        tx_buffer << "\r\n";

        m_socket.send_buffered_data();
    }

    void send_sip_header(const std::string& command, const std::string& uri, const std::string& to_uri, TxBufferT& stream)
    {
        stream << command << " " << uri << " SIP/2.0\r\n";

        stream << "CSeq: " << m_sip_sequence_number << " " << command << "\r\n";
        stream << "Call-ID: " << m_call_id << "@" << m_my_ip << "\r\n";
        stream << "Max-Forwards: 70\r\n";
        stream << "User-Agent: sip-client/0.0.1\r\n";
        if (command == "REGISTER")
        {
            stream << "From: <sip:" << m_user << "@" << m_server_ip << ">;tag=" << m_tag << "\r\n";
        }
        else if (command == "INVITE")
        {
            stream << "From: \"" << m_caller_display << "\" <sip:" << m_user << "@" << m_server_ip << ">;tag=" << m_tag << "\r\n";
        }
        else
        {
            stream << "From: \"" << m_user << "\" <sip:" << m_user << "@" << m_server_ip << ">;tag=" << m_tag << "\r\n";
        }
        stream << "Via: SIP/2.0/" << TRANSPORT_UPPER << " " << m_my_ip << ":" << LOCAL_PORT << ";branch=z9hG4bK-" << m_branch << ";rport\r\n";

        if ((command == "ACK") && !m_to_tag.empty())
        {
            stream << "To: <" << to_uri << ">;tag=" << m_to_tag << "\r\n";
        }
        else
        {
            stream << "To: <" << to_uri << ">\r\n";
        }
    }

    void send_sip_reply_header(const std::string& code, const SipPacket& packet, TxBufferT& stream)
    {
        stream << "SIP/2.0 " << code << "\r\n";

        stream << "To: " << packet.get_to() << "\r\n";
        stream << "From: " << packet.get_from() << "\r\n";
        stream << "Via: " << packet.get_via() << "\r\n";
        stream << "CSeq: " << packet.get_cseq() << "\r\n";
        stream << "Call-ID: " << packet.get_call_id()  << "\r\n";
        stream << "Max-Forwards: 70\r\n";
    }

    bool read_param(const std::string& line, const std::string& param_name, std::string& output)
    {
        std::string param(param_name + "=\"");
        size_t pos = line.find(param);
        if (pos == std::string::npos)
        {
            return false;
        }
        pos += param.size();
        size_t pos_end = line.find("\"", pos);
        if (pos_end == std::string::npos)
        {
            return false;
        }
        output = line.substr(pos, pos_end - pos);
        return true;
    }

    void compute_auth_response(const std::string& method, const std::string& uri)
    {
        std::string ha1_text;
        std::string ha2_text;
        unsigned char hash[16];

        m_response= "";
        std::string data = m_user + ":" + m_realm + ":" + m_pwd;

        m_md5.start();
        m_md5.update(data);
        m_md5.finish(hash);
        to_hex(ha1_text, hash, 16);
        ESP_LOGV(TAG, "Calculating md5 for : %s", data.c_str());
        ESP_LOGV(TAG, "Hex ha1 is %s", ha1_text.c_str());

        data = method + ":" + uri;

        m_md5.start();
        m_md5.update(data);
        m_md5.finish(hash);
        to_hex(ha2_text, hash, 16);
        ESP_LOGV(TAG, "Calculating md5 for : %s", data.c_str());
        ESP_LOGV(TAG, "Hex ha2 is %s", ha2_text.c_str());

        data = ha1_text + ":" + m_nonce + ":" + ha2_text;

        m_md5.start();
        m_md5.update(data);
        m_md5.finish(hash);
        to_hex(m_response, hash, 16);
        ESP_LOGV(TAG, "Calculating md5 for : %s", data.c_str());
        ESP_LOGV(TAG, "Hex response is %s", m_response.c_str());
    }

    void to_hex(std::string& dest, const unsigned char *data, int len)
    {
        static const char hexits[17] = "0123456789abcdef";

        dest = "";
        dest.reserve(len * 2 + 1);
        for (int i = 0; i < len; i++)
        {
            dest.push_back(hexits[data[i] >> 4]);
            dest.push_back(hexits[data[i] &  0x0F]);
        }
    }

    void log_state_transition(SipState old_state, SipState new_state)
    {
        ESP_LOGI(TAG, "New state %d -> %d", (int) old_state, (int) new_state);
    }

    SipState m_state = SipState::IDLE;

    SocketT m_socket;
    SocketT m_rtp_socket;
    Md5T    m_md5;
    std::string m_server_ip;

    std::string m_user;
    std::string m_pwd;
    std::string m_my_ip;

    std::string m_uri;
    std::string m_to_uri;
    std::string m_to_contact;
    std::string m_to_tag;

    uint32_t m_sip_sequence_number;
    uint32_t m_call_id;

    //auth stuff
    std::string m_response;
    std::string m_realm;
    std::string m_nonce;

    uint32_t m_tag;
    uint32_t m_branch;

    //misc stuff
    std::string m_caller_display;

    uint32_t m_sdp_session_id;
    Buffer<1024> m_tx_sdp_buffer;

    std::function<void(const SipClientEvent &)> m_event_handler;

    /* FreeRTOS event group to signal commands from other tasks */
    EventGroupHandle_t m_command_event_group;
    static constexpr uint8_t COMMAND_DIAL_BIT = BIT0;
    static constexpr uint8_t COMMAND_CANCEL_BIT = BIT1;

    static constexpr const uint16_t LOCAL_PORT = 5060;
    static constexpr const char* TRANSPORT_LOWER= "udp";
    static constexpr const char* TRANSPORT_UPPER= "UDP";

    static constexpr uint32_t SOCKET_RX_TIMEOUT_MSEC = 200;
    static constexpr uint16_t LOCAL_RTP_PORT = 7078;
    static constexpr const char* TAG = "SipClient";
};




#ifdef USE_SML
namespace sml = boost::sml;

struct ev_start {};
struct ev_2 {};
struct ev_3 {};

template <class SipClientT>
struct sip_states {
    auto operator()() const noexcept {
        using namespace sml;

        const auto idle = state<class idle>;

        const auto action = [](SipClientT& sip, const auto& event) {  /// event is deduced, order is not important
            (void) event;
            sip.test();
        };

        return make_transition_table(
                *idle + event<ev_start> = "s1"_s
                , "s1"_s + sml::on_entry<_> / [] { ESP_LOGV("SIP SM", "s1 on entry"); }
        , "s1"_s + sml::on_exit<_> / [] { ESP_LOGV("SIP SM", "s1 on exit"); }
        , "s1"_s + event<ev_2> / action = state<class s2>
        , state<class s2> + event<ev_3> = X
        );
    }
};
#endif //USE_SML


template <class SocketT, class Md5T>
class SipClient
{
public:
    SipClient(const std::string& user, const std::string& pwd, const std::string& server_ip, const std::string& server_port, const std::string& my_ip)
    : m_sip{user, pwd, server_ip, server_port, my_ip}
#ifdef USE_SML
    , m_sm{m_sip}
#endif
    {}

    bool init()
    {
        return m_sip.init();
    }

    bool is_initialized() const
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

    void set_event_handler(std::function<void(const SipClientEvent &)> handler)
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

    void run()
    {
#ifdef USE_SML
        m_sm.process_event(ev_start{});
        //assert(sm.is(sml::X));
#endif

        m_sip.run();
    }

private:
    SipClientInt<SocketT, Md5T> m_sip;

#ifdef USE_SML
    sml::sm<sip_states<SipClientInt<SocketT, Md5T>>> m_sm;
#endif

};
