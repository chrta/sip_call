// SPDX-License-Identifier: AGPL-3.0-or-later
/*
   Copyright 2017-2026 Christian Taedcke <hacking@taedcke.com>
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

#include "sip_client_event.h"
#include "sip_dialog.h"
#include "sip_identifier.h"
#include "sip_packet.h"
#include "sip_registration.h"
#include "sip_sml_events.h"
#include "sip_sml_logger.h"

#include "boost/sml.hpp"

namespace sml = boost::sml;

template <class SocketT, class Md5T, template <typename> typename SmT, class SipClientT>
class SipClientInt
{
    using SmlSmT = sml::sm<SmT<SipClientInt<SocketT, Md5T, SmT, SipClientT>>, sml::logger<Logger>>;

    enum class AckKind : std::uint8_t
    {
        To2xx,
        Non2xx,
    };

public:
    SipClientInt(asio::io_context& io_context, const std::string& user, std::string pwd, const std::string& server_ip, const std::string& server_port, std::string my_ip, SmlSmT& sm, SipClientT& sip_client)
        : m_socket(io_context, server_ip, server_port, LOCAL_PORT, [this](std::string data) {
            rx(std::move(data));
        })
        , m_rtp_socket(io_context, server_ip, "7078", LOCAL_RTP_PORT, [](const std::string& /*unused*/) {
        })
        , m_server_ip(server_ip)
        , m_user(user)
        , m_pwd(std::move(pwd))
        , m_my_ip(std::move(my_ip))
        , m_call_id(SipIdentifier::generate())
        , m_tag(SipIdentifier::generate())
        , m_branch(SipIdentifier::generate_branch())
        , m_sm(sm)
        , m_io_context(io_context)
        , m_timer(io_context)
        , m_command_timeout_timer(io_context)
        , m_reregister_timer(io_context)
        , m_sip_client(sip_client)
    {
    }

    bool init()
    {
        const bool result_rtp = m_rtp_socket.init();
        const bool result_sip = m_socket.init();

        if (result_rtp && result_sip)
        {
            m_sm.process_event(ev_start {});
        }

        return result_rtp && result_sip;
    }

    [[nodiscard]] bool is_initialized() const
    {
        return m_socket.is_initialized();
    }

    void set_server_ip(const std::string& server_ip)
    {
        m_server_ip = server_ip;
        m_socket.set_server_ip(server_ip);
        m_rtp_socket.set_server_ip(server_ip);
    }

    void set_my_ip(const std::string& my_ip)
    {
        m_my_ip = my_ip;
    }

    void set_credentials(const std::string& user, const std::string& password)
    {
        m_user = user;
        m_pwd = password;
    }

    void set_event_handler(std::function<void(SipClientT&, const SipClientEvent&)>&& handler)
    {
        m_event_handler = std::move(handler);
    }

    /**
     * Initiate a call async
     *
     * \param[in] local_number A number that is registered locally on the server, e.g. "**610"
     * \param[in] caller_display This string is displayed on the caller's phone
     */
    void request_ring(const std::string& local_number, const std::string& caller_display)
    {
        asio::dispatch(m_io_context, [this, local_number, caller_display]() {
            ESP_LOGI(TAG, "Request to call %s...", local_number.c_str());
            this->m_sm.process_event(ev_request_call { .local_number = local_number, .caller_display = caller_display });
        });
    }

    void request_cancel()
    {
        asio::dispatch(m_io_context, [this]() {
            ESP_LOGI(TAG, "Request to CANCEL call");
            this->m_sm.process_event(ev_cancel_call {});
        });
    }

    void deinit()
    {
        ESP_LOGI(TAG, "Deinit");
        m_socket.deinit();
        m_rtp_socket.deinit();
    }

    // send initial register request
    void register_unauth()
    {
        // sending REGISTER without auth
        m_tag = SipIdentifier::generate();
        m_branch = SipIdentifier::generate_branch();
        send_sip_register();
        m_tag = SipIdentifier::generate();
        m_branch = SipIdentifier::generate_branch();
    }

    // send  register request
    void register_auth()
    {
        m_sip_sequence_number++;
        // sending REGISTER with auth
        compute_auth_response("REGISTER", "sip:" + m_server_ip);
        send_sip_register();
    }

    void schedule_reregister(uint32_t register_expires)
    {
        if (register_expires == 0)
        {
            register_expires = DEFAULT_REGISTER_EXPIRES_SEC;
        }
        const uint32_t reregister_after = std::max(register_expires / 2, MIN_REREGISTER_INTERVAL_SEC);
        m_reregister_timer.expires_after(asio::chrono::seconds(reregister_after));

        m_reregister_timer.async_wait([this](const asio::error_code& ec) {
            if (!ec)
            {
                this->m_sm.process_event(ev_reregister {});
            }
        });
    }

    void retry_register_on_timeout()
    {
        const uint32_t shift = std::min<uint32_t>(m_register_failure_count, 4U);
        const uint32_t delay_sec = std::min<uint32_t>(1U << (shift + 1), MAX_REGISTER_BACKOFF_SEC);
        ++m_register_failure_count;
        ESP_LOGI(TAG, "REGISTER retry #%u in %us", static_cast<unsigned>(m_register_failure_count), static_cast<unsigned>(delay_sec));
        m_reregister_timer.expires_after(asio::chrono::seconds(delay_sec));
        m_reregister_timer.async_wait([this](const asio::error_code& ec) {
            if (!ec)
            {
                register_unauth();
            }
        });
    }

    void is_registered()
    {
        m_sip_sequence_number++;
        m_registration = {};
        m_register_failure_count = 0;
        ESP_LOGI(TAG, "OK :)");
    }

    void send_invite(const ev_401_unauthorized& /*unused*/)
    {
        // ACK the 401/407 — non-2xx, so reuse INVITE's branch and Request-URI.
        send_sip_ack(AckKind::Non2xx);

        if (!m_dialog)
        {
            return;
        }

        m_sdp_session_id = SipIdentifier::random_u32();

        // or sending INVITE with auth
        m_branch = SipIdentifier::generate_branch();
        m_sip_sequence_number++;
        compute_auth_response("INVITE", m_dialog->remote_uri);
        send_sip_invite();
    }

    void send_invite(const ev_initiate_call& /*unused*/)
    {
        m_sip_sequence_number++;
        m_sdp_session_id = SipIdentifier::random_u32();
        m_branch = SipIdentifier::generate_branch();
        send_sip_invite();
    }

    void request_call(const ev_request_call& event)
    {
        ESP_LOGI(TAG, "Request to call %s...", event.local_number.c_str());
        m_call_id = SipIdentifier::generate();
        const std::string remote_uri = "sip:" + event.local_number + "@" + m_server_ip;
        m_dialog = Dialog { .remote_uri = remote_uri,
            .caller_display = event.caller_display,
            .remote_target = {},
            .remote_tag = {},
            .route_set = {} };
        m_sm.process_event(ev_initiate_call {});
    }

    void cancel_call(const ev_cancel_call& /*unused*/)
    {
        ESP_LOGD(TAG, "Sending CANCEL for pending INVITE");
        send_sip_cancel();
    }

    void end_call(const ev_cancel_call& /*unused*/)
    {
        ESP_LOGD(TAG, "Sending BYE for established call");
        send_sip_bye();
    }

    void bye_completed()
    {
        if (m_event_handler)
        {
            m_event_handler(m_sip_client, SipClientEvent { .event = SipClientEvent::Event::CALL_END });
        }
        m_tag = SipIdentifier::generate();
        m_branch = SipIdentifier::generate_branch();
        m_sip_sequence_number++;
        m_dialog.reset();
    }

    void handle_invite(const ev_rx_invite& /*unused*/)
    {
        // received an invite, answered it already with ok, so new call is established, because someone called us
        if (m_event_handler)
        {
            m_event_handler(m_sip_client, SipClientEvent { .event = SipClientEvent::Event::CALL_START });
        }
    }

    void call_established()
    {
        // ACK-to-2xx (RFC 3261 §13.2.2.4, cf. §17.1.1.3): handled by the UAC
        // core as a new end-to-end transaction, so mint a fresh Via branch.
        // Request-URI is the remote target from Contact; CSeq matches INVITE.
        send_sip_ack(AckKind::To2xx);
        // Subsequent in-dialog requests (e.g. BYE) need their own CSeq.
        m_sip_sequence_number++;
        if (m_event_handler)
        {
            m_event_handler(m_sip_client, SipClientEvent { .event = SipClientEvent::Event::CALL_START });
        }
    }

    void call_cancelled()
    {
        if (m_event_handler)
        {
            m_event_handler(m_sip_client, SipClientEvent { .event = SipClientEvent::Event::CALL_CANCELLED });
        }
        // ACK for 487 is non-2xx: same branch as INVITE, Request-URI = INVITE URI.
        send_sip_ack(AckKind::Non2xx);
        m_tag = SipIdentifier::generate();
        m_branch = SipIdentifier::generate_branch();
        m_sip_sequence_number++;
        m_dialog.reset();
    }

    void call_declined(const ev_486_busy_here& /*unused*/)
    {
        if (m_event_handler)
        {
            m_event_handler(m_sip_client, SipClientEvent { .event = SipClientEvent::Event::CALL_CANCELLED, .cancel_reason = SipClientEvent::CancelReason::TARGET_BUSY });
        }
        m_dialog.reset();
    }

    void call_declined(const ev_603_decline& /*unused*/)
    {
        if (m_event_handler)
        {
            m_event_handler(m_sip_client, SipClientEvent { .event = SipClientEvent::Event::CALL_CANCELLED, .cancel_reason = SipClientEvent::CancelReason::CALL_DECLINED });
        }
        m_dialog.reset();
    }

    void handle_bye()
    {
        m_sip_sequence_number++;
        if (m_event_handler)
        {
            m_event_handler(m_sip_client, SipClientEvent { .event = SipClientEvent::Event::CALL_END });
        }
        m_dialog.reset();
    }

    void handle_internal_server_error()
    {
        m_tag = SipIdentifier::generate();
        m_branch = SipIdentifier::generate_branch();
        m_sip_sequence_number++;

        // wait for timeout and restart again
        m_timer.expires_after(asio::chrono::seconds(5));

        m_timer.async_wait([this](const asio::error_code& ec) {
            if (!ec)
            {
                this->m_sm.process_event(ev_start {});
            }
        });
    }

    void handle_reply_timeout()
    {
        ESP_LOGW(TAG, "SIP transaction timeout, aborting");
        if (m_event_handler)
        {
            m_event_handler(m_sip_client, SipClientEvent { .event = SipClientEvent::Event::CALL_CANCELLED });
        }
        m_tag = SipIdentifier::generate();
        m_branch = SipIdentifier::generate_branch();
        m_sip_sequence_number++;
        m_dialog.reset();
    }

private:
    void rx(std::string recv_string)
    {
        if (recv_string.empty())
        {
            return;
        }

        SipPacket packet(const_cast<char*>(recv_string.data()), recv_string.size());
        if (!packet.parse())
        {
            ESP_LOGI(TAG, "Parsing the packet failed");
            return;
        }

        m_command_timeout_timer.cancel();

        const SipPacket::Status reply = packet.get_status();
        ESP_LOGI(TAG, "Parsing the packet ok, reply code=%d", static_cast<int>(packet.get_status()));

        if (reply == SipPacket::Status::SERVER_ERROR_500)
        {
            m_sm.process_event(ev_500_internal_server_error {});
            return;
        }
        if ((reply == SipPacket::Status::UNAUTHORIZED_401) || (reply == SipPacket::Status::PROXY_AUTH_REQ_407))
        {
            m_registration.realm = packet.get_realm();
            m_registration.nonce = packet.get_nonce();
            m_registration.proxy_auth = (reply == SipPacket::Status::PROXY_AUTH_REQ_407);
        }
        else if ((reply == SipPacket::Status::UNKNOWN) && ((packet.get_method() == SipPacket::Method::NOTIFY) || (packet.get_method() == SipPacket::Method::BYE) || (packet.get_method() == SipPacket::Method::INFO)))
        {
            send_sip_ok(packet);
        }

        if (m_dialog)
        {
            if (!packet.get_contact().empty())
            {
                m_dialog->remote_target = packet.get_contact();
            }
            if (!packet.get_to_tag().empty())
            {
                m_dialog->remote_tag = packet.get_to_tag();
            }
            const auto& packet_record_route = packet.get_record_route();
            if (!packet_record_route.front().empty())
            {
                m_dialog->route_set = packet_record_route;
            }
        }

        if ((reply == SipPacket::Status::UNAUTHORIZED_401) || (reply == SipPacket::Status::PROXY_AUTH_REQ_407))
        {
            m_sm.process_event(ev_401_unauthorized {});
        }
        else if (reply == SipPacket::Status::OK_200)
        {
            m_sm.process_event(ev_200_ok { packet.get_contact_expires() });
        }
        else if (reply == SipPacket::Status::TRYING_100)
        {
            m_sm.process_event(ev_100_trying {});
        }
        else if (reply == SipPacket::Status::SESSION_PROGRESS_183)
        {
            m_sm.process_event(ev_183_session_progress {});
        }
        else if (reply == SipPacket::Status::REQUEST_CANCELLED_487)
        {
            m_sm.process_event(ev_487_request_cancelled {});
        }
        else if (reply == SipPacket::Status::DECLINE_603)
        {
            send_sip_ack(AckKind::Non2xx);
            m_sip_sequence_number++;
            m_branch = SipIdentifier::generate_branch();

            m_sm.process_event(ev_603_decline {});
        }
        else if (reply == SipPacket::Status::BUSY_HERE_486)
        {
            send_sip_ack(AckKind::Non2xx);
            m_sip_sequence_number++;
            m_branch = SipIdentifier::generate_branch();

            m_sm.process_event(ev_486_busy_here {});
        }

        if (packet.get_method() == SipPacket::Method::BYE)
        {
            m_sm.process_event(ev_rx_bye {});
        }
        else if ((packet.get_method() == SipPacket::Method::INFO)
            && (packet.get_content_type() == SipPacket::ContentType::APPLICATION_DTMF_RELAY))
        {
            if (m_event_handler)
            {
                m_event_handler(m_sip_client, SipClientEvent { .event = SipClientEvent::Event::BUTTON_PRESS, .button_signal = packet.get_dtmf_signal(), .button_duration = packet.get_dtmf_duration() });
            }
        }

        // Do not accept calls to e.g. **9 on fritzbox from self.
        // But immediately pick up all other calls, also to **9 from other participants.
        if (packet.get_method() == SipPacket::Method::INVITE)
        {
            const std::string from_user = extract_uri_user(packet.get_from());
            const bool is_self = !from_user.empty() && from_user == m_user;
            if (!is_self)
            {
                ESP_LOGV(TAG, "Accept invite from : '%s'", packet.get_from().c_str());
                send_sip_ok(packet);
                m_sm.process_event(ev_rx_invite {});
            }
            else
            {
                ESP_LOGV(TAG, "Drop invite from : %s", packet.get_from().c_str());
                send_sip_decline(packet);
            }
        }
    }

    // Extract the user part of the first sip: URI embedded in `header`.
    // Handles display-name + angle-bracket and bare-URI forms.
    static std::string extract_uri_user(const std::string& header)
    {
        static constexpr const char* SIP_PREFIX = "sip:";
        const size_t sip_pos = header.find(SIP_PREFIX);
        if (sip_pos == std::string::npos)
        {
            return {};
        }
        const size_t user_start = sip_pos + strlen(SIP_PREFIX);
        const size_t at_pos = header.find('@', user_start);
        if (at_pos == std::string::npos)
        {
            return {};
        }
        return header.substr(user_start, at_pos - user_start);
    }

    void send_sip_register()
    {
        TxBufferT& tx_buffer = m_socket.get_new_tx_buf();
        const std::string uri = "sip:" + m_server_ip;

        send_sip_header("REGISTER", uri, "sip:" + m_user + "@" + m_server_ip, tx_buffer);

        tx_buffer << "Contact: \"" << m_user << "\" <sip:" << m_user << "@" << m_my_ip << ":" << LOCAL_PORT << ";transport=" << TRANSPORT_LOWER << ">\r\n";

        if (!m_registration.response.empty())
        {
            tx_buffer << "Authorization: Digest username=\"" << m_user << "\", realm=\"" << m_registration.realm << "\", nonce=\"" << m_registration.nonce << "\", uri=\"" << uri << "\", algorithm=MD5, response=\"" << m_registration.response << "\"\r\n";
        }
        tx_buffer << "Allow: INVITE, ACK, CANCEL, OPTIONS, BYE, REFER, NOTIFY, MESSAGE, SUBSCRIBE, INFO\r\n";
        tx_buffer << "Expires: 3600\r\n";
        tx_buffer << "Content-Length: 0\r\n";
        tx_buffer << "\r\n";

        m_socket.send_buffered_data();
        arm_command_timeout();
    }

    void arm_command_timeout()
    {
        m_command_timeout_timer.expires_after(asio::chrono::seconds(COMMAND_TIMEOUT_SEC));
        m_command_timeout_timer.async_wait([this](const asio::error_code& ec) {
            if (!ec)
            {
                this->m_sm.process_event(ev_reply_timeout {});
            }
        });
    }

    void send_sip_invite()
    {
        if (!m_dialog)
        {
            return;
        }
        const std::string& remote_uri = m_dialog->remote_uri;
        TxBufferT& tx_buffer = m_socket.get_new_tx_buf();

        send_sip_header("INVITE", remote_uri, remote_uri, tx_buffer);

        tx_buffer << "Contact: \"" << m_user << "\" <sip:" << m_user << "@" << m_my_ip << ":" << LOCAL_PORT << ";transport=" << TRANSPORT_LOWER << ">\r\n";

        if (!m_registration.response.empty())
        {
            if (m_registration.proxy_auth)
            {
                tx_buffer << "Proxy-";
            }
            tx_buffer << "Authorization: Digest username=\"" << m_user << "\", realm=\"" << m_registration.realm << "\", nonce=\"" << m_registration.nonce << "\", uri=\"" << remote_uri << "\", response=\"" << m_registration.response << "\"\r\n";
        }
        tx_buffer << "Content-Type: application/sdp\r\n";
        tx_buffer << "Allow: INVITE, ACK, CANCEL, OPTIONS, BYE, REFER, NOTIFY, MESSAGE, SUBSCRIBE, INFO\r\n";
        m_tx_sdp_buffer.clear();
        m_tx_sdp_buffer << "v=0\r\n"
                        << "o=" << m_user << " " << m_sdp_session_id << " " << m_sdp_session_id << " IN IP4 " << m_my_ip << "\r\n"
                        << "s=sip-client/0.0.1\r\n"
                        << "c=IN IP4 " << m_my_ip << "\r\n"
                        << "t=0 0\r\n"
                        << "m=audio " << LOCAL_RTP_PORT << " RTP/AVP 0 8\r\n"
                        // << "a=sendrecv\r\n"
                        << "a=recvonly\r\n"
                        << "a=ptime:20\r\n";

        tx_buffer << "Content-Length: " << m_tx_sdp_buffer.size() << "\r\n";
        tx_buffer << "\r\n";
        tx_buffer << m_tx_sdp_buffer.data();

        m_socket.send_buffered_data();
        arm_command_timeout();
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
        if (!m_dialog)
        {
            return;
        }
        const std::string& remote_uri = m_dialog->remote_uri;
        TxBufferT& tx_buffer = m_socket.get_new_tx_buf();

        send_sip_header("CANCEL", remote_uri, remote_uri, tx_buffer);

        if (!m_registration.response.empty())
        {
            tx_buffer << "Contact: \"" << m_user << "\" <sip:" << m_user << "@" << m_my_ip << ":" << LOCAL_PORT << ";transport=" << TRANSPORT_LOWER << ">\r\n";
            tx_buffer << "Content-Type: application/sdp\r\n";
            tx_buffer << "Authorization: Digest username=\"" << m_user << "\", realm=\"" << m_registration.realm << "\", nonce=\"" << m_registration.nonce << "\", uri=\"" << remote_uri << "\", response=\"" << m_registration.response << "\"\r\n";
        }
        tx_buffer << "Content-Length: 0\r\n";
        tx_buffer << "\r\n";

        m_socket.send_buffered_data();
        arm_command_timeout();
    }

    /**
     * BYE an established dialog.
     *
     * RFC 3261 §15.1.1: BYE runs in its own transaction. It targets the
     * remote target learned from the dialog and carries a fresh branch
     * and CSeq.
     */
    void send_sip_bye()
    {
        if (!m_dialog)
        {
            return;
        }
        m_branch = SipIdentifier::generate_branch();

        TxBufferT& tx_buffer = m_socket.get_new_tx_buf();
        const std::string& request_uri = !m_dialog->remote_target.empty()
            ? m_dialog->remote_target
            : m_dialog->remote_uri;
        send_sip_header("BYE", request_uri, m_dialog->remote_uri, tx_buffer);
        tx_buffer << "Content-Length: 0\r\n";
        tx_buffer << "\r\n";

        m_socket.send_buffered_data();
        arm_command_timeout();
    }

    void send_sip_ack(AckKind kind)
    {
        if (!m_dialog)
        {
            return;
        }
        if (kind == AckKind::To2xx)
        {
            // RFC 3261 §13.2.2.4 (cf. §17.1.1.3): ACK-to-2xx is a new transaction → fresh branch.
            m_branch = SipIdentifier::generate_branch();
        }
        TxBufferT& tx_buffer = m_socket.get_new_tx_buf();
        // ACK-to-non-2xx MUST reuse the INVITE's Request-URI; ACK-to-2xx MAY
        // route to the remote target learned from the 200 OK's Contact.
        const std::string& request_uri = (kind == AckKind::To2xx) && !m_dialog->remote_target.empty()
            ? m_dialog->remote_target
            : m_dialog->remote_uri;
        send_sip_header("ACK", request_uri, m_dialog->remote_uri, tx_buffer);
        // std::string m_sdp_session_o;
        // std::string m_sdp_session_s;
        // std::string m_sdp_session_c;
        // m_tx_sdp_buffer.clear();
        // TODO: populate sdp body
        // m_tx_sdp_buffer << "v=0\r\n"
        //	              << m_sdp_session_o << "\r\n"
        //	      << m_sdp_session_s << "\r\n"
        //	      << m_sdp_session_c << "\r\n"
        //	      << "t=0 0\r\n";
        // TODO: copy each m line and select appropriate a line
        // tx_buffer << "Content-Type: application/sdp\r\n";
        // tx_buffer << "Content-Length: " << m_tx_sdp_buffer.size() << "\r\n";
        // tx_buffer << "Allow-Events: telephone-event\r\n";
        tx_buffer << "Content-Length: 0\r\n";
        tx_buffer << "\r\n";
        // tx_buffer << m_tx_sdp_buffer.data();

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

    void send_sip_decline(const SipPacket& packet)
    {
        TxBufferT& tx_buffer = m_socket.get_new_tx_buf();

        send_sip_reply_header("603 Decline", packet, tx_buffer);
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
        else if (command == "INVITE" && m_dialog)
        {
            stream << "From: \"" << m_dialog->caller_display << "\" <sip:" << m_user << "@" << m_server_ip << ">;tag=" << m_tag << "\r\n";
        }
        else
        {
            stream << "From: \"" << m_user << "\" <sip:" << m_user << "@" << m_server_ip << ">;tag=" << m_tag << "\r\n";
        }
        stream << "Via: SIP/2.0/" << TRANSPORT_UPPER << " " << m_my_ip << ":" << LOCAL_PORT << ";branch=" << m_branch << ";rport\r\n";

        const bool is_in_dialog_request = (command == "ACK") || (command == "BYE");
        if (is_in_dialog_request && m_dialog && !m_dialog->remote_tag.empty())
        {
            stream << "To: <" << to_uri << ">;tag=" << m_dialog->remote_tag << "\r\n";
        }
        else
        {
            stream << "To: <" << to_uri << ">\r\n";
        }
        if (is_in_dialog_request && m_dialog)
        {
            for (auto it = std::crbegin(m_dialog->route_set); it != std::crend(m_dialog->route_set); ++it) // NOLINT(modernize-loop-convert)
            {
                if (it->empty())
                {
                    continue;
                }
                stream << "Route: " << *it << "\r\n";
            }
        }
    }

    void send_sip_reply_header(const std::string& code, const SipPacket& packet, TxBufferT& stream)
    {
        stream << "SIP/2.0 " << code << "\r\n";

        // RFC 3261 §12.1.1: the UAS MUST add a tag to the To header of
        // dialog-creating responses (and any response to a request that did
        // not already carry one). packet.get_to() is stripped of any tag
        // parameter by the parser, so reattach the peer's tag if present,
        // otherwise add our own to form the dialog.
        const std::string& req_to_tag = packet.get_to_tag();
        if (!req_to_tag.empty())
        {
            stream << "To: " << packet.get_to() << ";tag=" << req_to_tag << "\r\n";
        }
        else
        {
            stream << "To: " << packet.get_to() << ";tag=" << m_tag << "\r\n";
        }
        stream << "From: " << packet.get_from() << "\r\n";

        for (const auto& rr : packet.get_record_route())
        {
            if (rr.empty())
            {
                break;
            }
            stream << "Record-Route: " << rr << "\r\n";
        }

        for (const auto& v : packet.get_via())
        {
            if (v.empty())
            {
                break;
            }
            stream << "Via: " << v << "\r\n";
        }

        stream << "CSeq: " << packet.get_cseq() << "\r\n";
        stream << "Call-ID: " << packet.get_call_id() << "\r\n";
        stream << "Max-Forwards: 70\r\n";
    }

    void compute_auth_response(const std::string& method, const std::string& uri)
    {
        std::string ha1_text;
        std::string ha2_text;
        std::array<unsigned char, 16> hash {};

        m_registration.response.clear();
        std::string data = m_user + ":" + m_registration.realm + ":" + m_pwd;

        m_md5.start();
        m_md5.update(data);
        m_md5.finish(hash);
        to_hex(ha1_text, hash);
        ESP_LOGV(TAG, "Calculating md5 for : %s", data.c_str());
        ESP_LOGV(TAG, "Hex ha1 is %s", ha1_text.c_str());

        data = method + ":" + uri;

        m_md5.start();
        m_md5.update(data);
        m_md5.finish(hash);
        to_hex(ha2_text, hash);
        ESP_LOGV(TAG, "Calculating md5 for : %s", data.c_str());
        ESP_LOGV(TAG, "Hex ha2 is %s", ha2_text.c_str());

        data = ha1_text + ":" + m_registration.nonce + ":" + ha2_text;

        m_md5.start();
        m_md5.update(data);
        m_md5.finish(hash);
        to_hex(m_registration.response, hash);
        ESP_LOGV(TAG, "Calculating md5 for : %s", data.c_str());
        ESP_LOGV(TAG, "Hex response is %s", m_registration.response.c_str());
    }

    void to_hex(std::string& dest, const std::array<unsigned char, 16>& data)
    {
        static const std::array<char, 16> hexits { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };

        dest = "";
        dest.reserve((data.size() * 2) + 1);
        for (auto byte : data)
        {
            dest.push_back(hexits[byte >> 4]);
            dest.push_back(hexits[byte & 0x0F]);
        }
    }

    SocketT m_socket;
    SocketT m_rtp_socket;
    Md5T m_md5;
    std::string m_server_ip;

    std::string m_user;
    std::string m_pwd;
    std::string m_my_ip;

    uint32_t m_sip_sequence_number { SipIdentifier::random_u32() & 0x7FFFFFFFU };
    std::string m_call_id;

    Registration m_registration;
    std::optional<Dialog> m_dialog;

    std::string m_tag;
    std::string m_branch;

    uint32_t m_register_failure_count { 0 };

    uint32_t m_sdp_session_id { 0 };
    Buffer<1024> m_tx_sdp_buffer;

    std::function<void(SipClientT&, const SipClientEvent&)> m_event_handler;

    SmlSmT& m_sm;

    asio::io_context& m_io_context;
    asio::steady_timer m_timer;
    asio::steady_timer m_command_timeout_timer;
    asio::steady_timer m_reregister_timer;

    SipClientT& m_sip_client;

    static constexpr const uint16_t LOCAL_PORT = 5060;
    static constexpr const char* TRANSPORT_LOWER = "udp";
    static constexpr const char* TRANSPORT_UPPER = "UDP";

    static constexpr uint32_t SOCKET_RX_TIMEOUT_MSEC = 200;
    static constexpr uint16_t LOCAL_RTP_PORT = 7078;
    static constexpr uint32_t DEFAULT_REGISTER_EXPIRES_SEC = 3600;
    static constexpr uint32_t MIN_REREGISTER_INTERVAL_SEC = 30;
    static constexpr uint32_t MAX_REGISTER_BACKOFF_SEC = 32;
    static constexpr uint32_t COMMAND_TIMEOUT_SEC = 5;
    static constexpr const char* TAG = "SipClient";
};
