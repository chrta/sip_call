// SPDX-License-Identifier: AGPL-3.0-or-later
/*
   Copyright 2017-2026 Christian Taedcke <hacking@taedcke.com>
 */

#pragma once

#include "esp_log.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <system_error>

class SipPacket
{
public:
    enum class Status : uint8_t
    {
        TRYING_100,
        SESSION_PROGRESS_183,
        OK_200,
        UNAUTHORIZED_401,
        PROXY_AUTH_REQ_407,
        BUSY_HERE_486,
        REQUEST_CANCELLED_487,
        SERVER_ERROR_500,
        DECLINE_603,
        UNKNOWN,
    };

    enum class Method : uint8_t
    {
        NOTIFY,
        BYE,
        INFO,
        INVITE,
        UNKNOWN
    };

    enum class ContentType : uint8_t
    {
        APPLICATION_DTMF_RELAY,
        UNKNOWN
    };

    using ViaT = std::array<std::string, 5>;
    using RecordRouteT = std::array<std::string, 5>;

    SipPacket(char* input_buffer, size_t input_buffer_length)
        : m_buffer(input_buffer)
        , m_buffer_length(input_buffer_length)
    {
    }

    bool parse()
    {
        const bool result = parse_header();
        if (!result)
        {
            return false;
        }
        parse_body();
        return true;
    }

    [[nodiscard]] Status get_status() const
    {
        return m_status;
    }

    [[nodiscard]] Method get_method() const
    {
        return m_method;
    }

    [[nodiscard]] ContentType get_content_type() const
    {
        return m_content_type;
    }

    [[nodiscard]] uint32_t get_content_length() const
    {
        return m_content_length;
    }

    [[nodiscard]] std::string get_nonce() const
    {
        return m_nonce;
    }

    [[nodiscard]] std::string get_realm() const
    {
        return m_realm;
    }

    [[nodiscard]] std::string get_contact() const
    {
        return m_contact;
    }

    [[nodiscard]] uint32_t get_contact_expires() const
    {
        return m_contact_expires;
    }

    [[nodiscard]] std::string get_to_tag() const
    {
        return m_to_tag;
    }

    [[nodiscard]] std::string get_cseq() const
    {
        return m_cseq;
    }

    [[nodiscard]] std::string get_call_id() const
    {
        return m_call_id;
    }

    [[nodiscard]] std::string get_to() const
    {
        return m_to;
    }

    [[nodiscard]] std::string get_from() const
    {
        return m_from;
    }

    [[nodiscard]] const ViaT& get_via() const
    {
        return m_via;
    }

    [[nodiscard]] const RecordRouteT& get_record_route() const
    {
        return m_record_route;
    }

    [[nodiscard]] std::string get_p_called_party_id() const
    {
        return m_p_called_party_id;
    }

    [[nodiscard]] char get_dtmf_signal() const
    {
        return m_dtmf_signal;
    }

    [[nodiscard]] uint16_t get_dtmf_duration() const
    {
        return m_dtmf_duration;
    }
    [[nodiscard]] std::string get_media() const
    {
        return m_media;
    }
    [[nodiscard]] std::string get_cip() const
    {
        return m_cip;
    }

private:
    bool parse_header()
    {
        char* start_position = m_buffer;
        char* end_position = strstr(start_position, LINE_ENDING);

        m_method = Method::UNKNOWN;
        m_status = Status::UNKNOWN;
        m_contact_expires = 0;
        m_content_type = ContentType::UNKNOWN;
        m_content_length = 0;
        m_cseq = "";
        m_call_id = "";
        m_to = "";
        m_from = "";
        m_via.fill("");
        m_record_route.fill("");
        m_p_called_party_id = "";
        m_dtmf_signal = ' ';
        m_dtmf_duration = 0;
        m_cip = "";
        m_media = "";
        m_body = nullptr;

        if (end_position == nullptr)
        {
            ESP_LOGW(TAG, "No line ending found in %s", m_buffer);
            return false;
        }

        uint32_t line_number = 0;
        do
        {
            auto length = static_cast<size_t>(end_position - start_position);
            if (length == 0) // line only contains the line ending
            {
                ESP_LOGV(TAG, "Valid end of header detected");
                if (end_position + LINE_ENDING_LEN >= m_buffer + m_buffer_length)
                {
                    // no remaining data in buffer, so no body
                    m_body = nullptr;
                }
                else
                {
                    m_body = end_position + LINE_ENDING_LEN;
                }
                return true;
            }
            char* next_start_position = end_position + LINE_ENDING_LEN;
            line_number++;

            // Null-terminate this line in place by replacing the leading
            // byte of the CRLF. That is enough for the C string functions
            // used below; the second byte is never read.
            *end_position = '\0';
            ESP_LOGV(TAG, "Parsing line: %s", start_position);

            if (!parse_header_line(start_position, end_position, line_number))
            {
                return false;
            }

            // go to next line
            start_position = next_start_position;
            end_position = strstr(start_position, LINE_ENDING);
        } while (end_position != nullptr);

        // no line only containing the line ending found :(
        return false;
    }

    bool parse_header_line(const char* start_position, const char* end_position, uint32_t line_number)
    {
        if (strstr(start_position, SIP_2_0_SPACE) == start_position)
        {
            return parse_status_line(start_position, end_position);
        }
        if (match_header_value(start_position, WWW_AUTHENTICATE) != nullptr
            || match_header_value(start_position, PROXY_AUTHENTICATE) != nullptr)
        {
            parse_auth_line(start_position);
            return true;
        }
        if (const char* value = match_header_value(start_position, CONTACT, 'm'); value != nullptr)
        {
            parse_contact_value(value);
            return true;
        }
        if (const char* value = match_header_value(start_position, TO, 't'); value != nullptr)
        {
            parse_to_value(value);
            return true;
        }
        if (const char* value = match_header_value(start_position, FROM, 'f'); value != nullptr)
        {
            m_from = value;
            return true;
        }
        if (const char* value = match_header_value(start_position, VIA, 'v'); value != nullptr)
        {
            append_via(value);
            return true;
        }
        if (const char* value = match_header_value(start_position, RECORD_ROUTE); value != nullptr)
        {
            append_record_route(value);
            return true;
        }
        if (const char* value = match_header_value(start_position, C_SEQ); value != nullptr)
        {
            m_cseq = value;
            return true;
        }
        if (const char* value = match_header_value(start_position, CALL_ID, 'i'); value != nullptr)
        {
            m_call_id = value;
            return true;
        }
        if (const char* value = match_header_value(start_position, P_CALLED_PARTY_ID); value != nullptr)
        {
            m_p_called_party_id = value;
            return true;
        }
        if (const char* value = match_header_value(start_position, CONTENT_TYPE, 'c'); value != nullptr)
        {
            m_content_type = convert_content_type(value);
            return true;
        }
        if (const char* value = match_header_value(start_position, CONTENT_LENGTH, 'l'); value != nullptr)
        {
            parse_content_length_value(value, end_position, start_position);
            return true;
        }
        if (line_number == 1)
        {
            m_method = convert_method(start_position);
        }
        return true;
    }

    bool parse_status_line(const char* start_position, const char* end_position)
    {
        const char* code_start = start_position + strlen(SIP_2_0_SPACE);
        long code = 0;
        const auto parse_result = std::from_chars(code_start, end_position, code);
        if (parse_result.ec != std::errc() || parse_result.ptr == code_start)
        {
            ESP_LOGW(TAG, "Failed to parse status code in line '%s'", start_position);
            return false;
        }
        ESP_LOGV(TAG, "Detect status %ld", code);
        m_status = convert_status(code);
        return true;
    }

    void parse_auth_line(const char* start_position)
    {
        ESP_LOGV(TAG, "Detect authenticate line");
        if (!read_param(start_position, REALM, m_realm))
        {
            ESP_LOGW(TAG, "Failed to read realm in authenticate line");
        }
        if (!read_param(start_position, NONCE, m_nonce))
        {
            ESP_LOGW(TAG, "Failed to read nonce in authenticate line");
        }
        ESP_LOGI(TAG, "Realm is %s and nonce is %s", m_realm.c_str(), m_nonce.c_str());
    }

    void parse_contact_value(const char* value)
    {
        ESP_LOGV(TAG, "Detect contact line");

        const char* open_pos = strchr(value, '<');
        const char* close_pos = strchr(value, '>');

        if ((open_pos == nullptr) || (close_pos == nullptr))
        {
            ESP_LOGW(TAG, "Failed to read content of contact line");
            return;
        }
        m_contact = std::string(open_pos + 1, close_pos);

        /* in the SIP 200 OK finishing a successful REGISTER, the contact line might also
         * contain the expire information from the server, e.g.
         * Contact: <...>;expires=300
         */
        const char* expires_pos = strstr(close_pos + 1, ";expires=");
        if (expires_pos == nullptr)
        {
            return;
        }
        const long contact_expires = strtol(expires_pos + strlen(";expires="), nullptr, 10);
        if (contact_expires < 0)
        {
            ESP_LOGW(TAG, "Invalid contact expires %ld", contact_expires);
            return;
        }
        m_contact_expires = static_cast<uint32_t>(contact_expires);
    }

    void parse_to_value(const char* value)
    {
        ESP_LOGV(TAG, "Detect to line");
        const char* tag_pos = strstr(value, ";tag=");
        if (tag_pos != nullptr)
        {
            m_to_tag = std::string(tag_pos + strlen(";tag="));
            m_to = std::string(value, tag_pos);
        }
        else
        {
            m_to = std::string(value);
        }
    }

    void parse_content_length_value(const char* value, const char* end_position, const char* start_position)
    {
        uint32_t content_length = 0;
        const auto parse_result = std::from_chars(value, end_position, content_length);
        if (parse_result.ec != std::errc() || parse_result.ptr == value)
        {
            ESP_LOGW(TAG, "Invalid content length in line '%s'", start_position);
            return;
        }
        m_content_length = content_length;
    }

    bool parse_body()
    {
        if (m_body == nullptr)
        {
            return true;
        }

        char* start_position = m_body;
        char* end_position = strstr(start_position, LINE_ENDING);

        if (end_position == nullptr)
        {
            ESP_LOGW(TAG, "No line ending found in %s", m_body);
            return false;
        }

        do
        {
            auto length = static_cast<size_t>(end_position - start_position);
            if (length == 0) // line only contains the line ending
            {
                return true;
            }

            char* next_start_position = end_position + LINE_ENDING_LEN;

            // Null-terminate this line in place by replacing the leading
            // byte of the CRLF. That is enough for the C string functions
            // used below; the second byte is never read.
            *end_position = '\0';
            ESP_LOGV(TAG, "Parsing line: %s", start_position);

            if (strstr(start_position, SIGNAL) == start_position)
            {
                m_dtmf_signal = *(start_position + strlen(SIGNAL));
            }
            else if (strstr(start_position, DURATION) == start_position)
            {
                const long duration = strtol(start_position + strlen(DURATION), nullptr, 10);
                if (duration < 0)
                {
                    ESP_LOGW(TAG, "Invalid duration %ld", duration);
                }
                else
                {
                    m_dtmf_duration = static_cast<uint16_t>(duration);
                }
            }
            else if (strstr(start_position, MEDIA) == start_position)
            {
                m_media = std::string(start_position + strlen(MEDIA));
            }
            else if (strstr(start_position, CIP) == start_position)
            {
                m_cip = std::string(start_position + strlen(CIP));
            }

            // go to next line
            start_position = next_start_position;
            end_position = strstr(start_position, LINE_ENDING);
        } while (end_position != nullptr);

        return true;
    }

    // Case-insensitive byte comparison of length `n`. ASCII-only — SIP
    // headers are ASCII per RFC 3261 §7.3.1.
    static bool iequals_n(const char* a, const char* b, size_t n)
    {
        return std::equal(a, a + n, b, [](char ca, char cb) {
            return std::tolower(static_cast<unsigned char>(ca))
                == std::tolower(static_cast<unsigned char>(cb));
        });
    }

    // If `line` begins with a SIP header name (RFC 3261 §7.3.1 HCOLON
    // syntax — case-insensitive long name OR single-letter compact form,
    // followed by optional LWS, a ':' and optional LWS), return a pointer
    // to the start of the value; otherwise nullptr.
    static const char* match_header_value(const char* line, const char* long_name, char compact = '\0')
    {
        const char* p = nullptr;
        const size_t long_len = strlen(long_name);
        if (iequals_n(line, long_name, long_len))
        {
            p = line + long_len;
        }
        else if (compact != '\0'
            && std::tolower(static_cast<unsigned char>(line[0]))
                == std::tolower(static_cast<unsigned char>(compact)))
        {
            p = line + 1;
        }
        else
        {
            return nullptr;
        }
        while (*p == ' ' || *p == '\t')
        {
            ++p;
        }
        if (*p != ':')
        {
            return nullptr;
        }
        ++p;
        while (*p == ' ' || *p == '\t')
        {
            ++p;
        }
        return p;
    }

    static bool read_param(const char* line, const char* param_name, std::string& output)
    {
        const char* pos = strstr(line, param_name);
        if (pos == nullptr)
        {
            return false;
        }
        pos += strlen(param_name);
        if (*pos != '=')
        {
            return false;
        }
        ++pos;
        if (*pos == '"')
        {
            // Quoted form: value runs up to the next unescaped double quote.
            ++pos;
            const char* pos_end = strchr(pos, '"');
            if (pos_end == nullptr)
            {
                return false;
            }
            output = std::string(pos, pos_end);
            return true;
        }
        // Token form (RFC 3261 §25.1): ends at ',' ';' or LWS.
        const char* pos_end = pos;
        while (*pos_end != '\0' && *pos_end != ',' && *pos_end != ';'
            && *pos_end != ' ' && *pos_end != '\t')
        {
            ++pos_end;
        }
        if (pos_end == pos)
        {
            return false;
        }
        output = std::string(pos, pos_end);
        return true;
    }

    [[nodiscard]] static Status convert_status(long code)
    {
        switch (code)
        {
        case 200:
            return Status::OK_200;
        case 401:
            return Status::UNAUTHORIZED_401;
        case 100:
            return Status::TRYING_100;
        case 183:
            return Status::SESSION_PROGRESS_183;
        case 500:
            return Status::SERVER_ERROR_500;
        case 486:
            return Status::BUSY_HERE_486;
        case 487:
            return Status::REQUEST_CANCELLED_487;
        case 407:
            return Status::PROXY_AUTH_REQ_407;
        case 603:
            return Status::DECLINE_603;
        default:
            return Status::UNKNOWN;
        }
    }

    static Method convert_method(const char* input)
    {
        if (strstr(input, NOTIFY) == input)
        {
            return Method::NOTIFY;
        }
        if (strstr(input, BYE) == input)
        {
            return Method::BYE;
        }
        if (strstr(input, INFO) == input)
        {
            return Method::INFO;
        }
        if (strstr(input, INVITE) == input)
        {
            return Method::INVITE;
        }
        return Method::UNKNOWN;
    }

    static ContentType convert_content_type(const char* input)
    {
        if (strstr(input, APPLICATION_DTMF_RELAY) == input)
        {
            return ContentType::APPLICATION_DTMF_RELAY;
        }
        return ContentType::UNKNOWN;
    }

    void append_via(const std::string& via)
    {
        for (auto& v : m_via)
        {
            if (v.empty())
            {
                v = via;
                return;
            }
        }
    }

    void append_record_route(const std::string& record_route)
    {
        for (auto& rr : m_record_route)
        {
            if (rr.empty())
            {
                rr = record_route;
                return;
            }
        }
    }

    char* m_buffer;
    const size_t m_buffer_length;

    Status m_status { Status::UNKNOWN };
    Method m_method { Method::UNKNOWN };
    ContentType m_content_type { ContentType::UNKNOWN };
    uint32_t m_content_length { 0 };

    std::string m_realm;
    std::string m_nonce;
    std::string m_contact;
    uint32_t m_contact_expires {};
    std::string m_to_tag;
    std::string m_cseq;
    std::string m_call_id;
    std::string m_to;
    std::string m_from;
    ViaT m_via;
    RecordRouteT m_record_route;
    std::string m_p_called_party_id;
    std::string m_media;
    std::string m_cip;

    char* m_body {};

    uint16_t m_dtmf_duration {};
    char m_dtmf_signal {};

    static constexpr const char* LINE_ENDING = "\r\n";
    static constexpr size_t LINE_ENDING_LEN = 2;

    static constexpr const char* TAG = "SipPacket";
    static constexpr const char* SIP_2_0_SPACE = "SIP/2.0 ";
    static constexpr const char* WWW_AUTHENTICATE = "WWW-Authenticate";
    static constexpr const char* PROXY_AUTHENTICATE = "Proxy-Authenticate";
    static constexpr const char* CONTACT = "Contact";
    static constexpr const char* TO = "To";
    static constexpr const char* FROM = "From";
    static constexpr const char* VIA = "Via";
    static constexpr const char* RECORD_ROUTE = "Record-Route";
    static constexpr const char* P_CALLED_PARTY_ID = "P-Called-Party-ID";
    static constexpr const char* C_SEQ = "CSeq";
    static constexpr const char* CALL_ID = "Call-ID";
    static constexpr const char* CONTENT_TYPE = "Content-Type";
    static constexpr const char* CONTENT_LENGTH = "Content-Length";
    static constexpr const char* REALM = "realm";
    static constexpr const char* NONCE = "nonce";
    static constexpr const char* NOTIFY = "NOTIFY ";
    static constexpr const char* BYE = "BYE ";
    static constexpr const char* INFO = "INFO ";
    static constexpr const char* INVITE = "INVITE ";
    static constexpr const char* APPLICATION_DTMF_RELAY = "application/dtmf-relay";
    static constexpr const char* SIGNAL = "Signal=";
    static constexpr const char* DURATION = "Duration=";
    static constexpr const char* MEDIA = "m=";
    static constexpr const char* CIP = "c=IN IP4 ";
};
