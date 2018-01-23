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

#include "esp_log.h"
#include <cstring>

class SipPacket
{
public:

    enum class Status {
        TRYING_100,
        SESSION_PROGRESS_183,
        OK_200,
        UNAUTHORIZED_401,
        PROXY_AUTH_REQ_407,
        REQUEST_CANCELLED_487,
        SERVER_ERROR_500,
        DECLINE_603,
        UNKNOWN,
    };

    enum class Method {
        NOTIFY,
        BYE,
        INFO,
        UNKNOWN
    };

    enum class ContentType {
	    APPLICATION_DTMF_RELAY,
	    UNKNOWN
    };


    SipPacket(const char* input_buffer, size_t input_buffer_length)
    : m_buffer(input_buffer)
    , m_buffer_length(input_buffer_length)
    , m_status(Status::UNKNOWN)
    , m_method(Method::UNKNOWN)
    , m_content_type(ContentType::UNKNOWN)
    , m_content_length(0)
    , m_realm()
    , m_nonce()
    {
    }

    bool parse()
    {
        bool result = parse_header();
        if (!result)
        {
            return false;
        }
        parse_body();
        return true;
    }

    Status get_status() const
    {
        return m_status;
    }

    Method get_method() const
    {
        return m_method;
    }

    ContentType get_content_type() const
    {
        return m_content_type;
    }

    uint32_t get_content_length() const
    {
	return m_content_length;
    }

    std::string get_nonce() const
    {
        return m_nonce;
    }

    std::string get_realm() const
    {
        return m_realm;
    }

    std::string get_contact() const
    {
        return m_contact;
    }

    std::string get_to_tag() const
    {
        return m_to_tag;
    }

    std::string get_cseq() const
    {
        return m_cseq;
    }

    std::string get_call_id() const
    {
        return m_call_id;
    }

    std::string get_to() const
    {
        return m_to;
    }

    std::string get_from() const
    {
        return m_from;
    }

    std::string get_via() const
    {
        return m_via;
    }

    char get_dtmf_signal() const
    {
        return m_dtmf_signal;
    }

    uint16_t get_dtmf_duration() const
    {
        return m_dtmf_duration;
    }

private:

    bool parse_header()
    {
        const char* start_position = m_buffer;
        char* end_position = strstr(start_position, LINE_ENDING);

        m_method = Method::UNKNOWN;
        m_status = Status::UNKNOWN;
        m_content_type = ContentType::UNKNOWN;
        m_content_length = 0;
        m_cseq = "";
        m_call_id = "";
        m_to = "";
        m_from = "";
        m_via = "";
        m_dtmf_signal = ' ';
        m_dtmf_duration = 0;
        m_body = nullptr;

        if (end_position == nullptr)
        {
            ESP_LOGW(TAG, "No line ending found in %s", m_buffer);
            return false;
        }

        uint32_t line_number = 0;
        do
        {
            size_t length = end_position - start_position;
            if (length == 0) //line only contains the line ending
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
            const char* next_start_position = end_position + LINE_ENDING_LEN;
            line_number++;

            //create a proper null terminated c string
            //from here on string functions may be used!
            memset(end_position, 0, LINE_ENDING_LEN);
            ESP_LOGV(TAG, "Parsing line: %s", start_position);

            if (strstr(start_position, SIP_2_0_SPACE) == start_position)
            {
                long code = strtol(start_position + strlen(SIP_2_0_SPACE), nullptr, 10);
                ESP_LOGV(TAG, "Detect status %ld", code);
                m_status = convert_status(code);
            }
            else if ((strncmp(WWW_AUTHENTICATE, start_position, strlen(WWW_AUTHENTICATE)) == 0)
                    || (strncmp(PROXY_AUTHENTICATE, start_position, strlen(PROXY_AUTHENTICATE)) == 0))
            {
                ESP_LOGV(TAG, "Detect authenticate line");
                //read realm and nonce from authentication line
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
            else if (strncmp(CONTACT, start_position, strlen(CONTACT)) == 0)
            {
                ESP_LOGV(TAG, "Detect contact line");
                const char* last_pos = strstr(start_position, ">");
                if (last_pos == nullptr)
                {
                    ESP_LOGW(TAG, "Failed to read content of contact line");
                }
                else
                {
                    m_contact = std::string(start_position + strlen(CONTACT), last_pos);
                }
            }
            else if (strncmp(TO, start_position, strlen(TO)) == 0)
            {
                ESP_LOGV(TAG, "Detect to line");
                const char* tag_pos = strstr(start_position, ">;tag=");
                if (tag_pos != nullptr)
                {
                    m_to_tag = std::string(tag_pos + strlen(">;tag="));
                }
                m_to = std::string(start_position + strlen(TO));
            }
            else if (strstr(start_position, FROM) == start_position)
            {
                m_from = std::string(start_position + strlen(FROM));
            }
            else if (strstr(start_position, VIA) == start_position)
            {
                m_via = std::string(start_position + strlen(VIA));
            }
            else if (strstr(start_position, C_SEQ) == start_position)
            {
                m_cseq = std::string(start_position + strlen(C_SEQ));
            }
            else if (strstr(start_position, CALL_ID) == start_position)
            {
                m_call_id = std::string(start_position + strlen(CALL_ID));
            }
            else if (strstr(start_position, CONTENT_TYPE) == start_position)
            {
                m_content_type = convert_content_type(start_position + strlen(CONTENT_TYPE));
            }
            else if (strstr(start_position, CONTENT_LENGTH) == start_position)
            {
                long length = strtol(start_position + strlen(CONTENT_LENGTH), nullptr, 10);
                if (length < 0)
                {
                    ESP_LOGW(TAG, "Invalid content length %ld", length);
                }
                else
                {
                    m_content_length = length;
                }
            }

            else if (line_number == 1)
            {
                //first line, but no response
                m_method = convert_method(start_position);
            }

            //go to next line
            start_position = next_start_position;
            end_position = strstr(start_position, LINE_ENDING);
        } while(end_position);

        //no line only containing the line ending found :(
        return false;
    }

    bool parse_body()
    {
        if (m_body == nullptr)
        {
            return true;
        }

        const char* start_position = m_body;
        char* end_position = strstr(start_position, LINE_ENDING);

        if (end_position == nullptr)
        {
            ESP_LOGW(TAG, "No line ending found in %s", m_body);
            return false;
        }

        do
        {
            size_t length = end_position - start_position;
            if (length == 0) //line only contains the line ending
            {
                return true;
            }

            const char* next_start_position = end_position + LINE_ENDING_LEN;

            //create a proper null terminated c string
            //from here on string functions may be used!
            memset(end_position, 0, LINE_ENDING_LEN);
            ESP_LOGV(TAG, "Parsing line: %s", start_position);

            if (strstr(start_position, SIGNAL) == start_position)
            {
                m_dtmf_signal = *(start_position + strlen(SIGNAL));
            }
            else if (strstr(start_position, DURATION) == start_position)
            {
                long duration = strtol(start_position + strlen(DURATION), nullptr, 10);
                if (duration < 0)
                {
                    ESP_LOGW(TAG, "Invalid duration %ld", duration);
                }
                else
                {
                    m_dtmf_duration = duration;
                }
            }

            //go to next line
            start_position = next_start_position;
            end_position = strstr(start_position, LINE_ENDING);
        } while(end_position);

        return true;
    }

    bool read_param(const char* line, const char* param_name, std::string& output)
    {
        const char* pos = strstr(line, param_name);
        if (pos == nullptr)
        {
            return false;
        }
        if (*(pos + strlen(param_name)) != '=')
        {
            return false;
        }

        if (*(pos + strlen(param_name) + 1) != '"')
        {
            return false;
        }

        pos += strlen(param_name) + 2;
        const char* pos_end = strchr(pos, '"');
        if (pos_end == nullptr)
        {
            return false;
        }
        output = std::string(pos, pos_end);
        return true;
    }

    Status convert_status(uint32_t code) const
    {
        switch (code)
        {
        case 200: return Status::OK_200;
        case 401: return Status::UNAUTHORIZED_401;
        case 100: return Status::TRYING_100;
        case 183: return Status::SESSION_PROGRESS_183;
        case 500: return Status::SERVER_ERROR_500;
        case 487: return Status::REQUEST_CANCELLED_487;
        case 407: return Status::PROXY_AUTH_REQ_407;
        case 603: return Status::DECLINE_603;
        }
        return Status::UNKNOWN;
    }

    Method convert_method(const char* input) const
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
        return Method::UNKNOWN;
    }

    ContentType convert_content_type(const char* input) const
    {
	if (strstr(input, APPLICATION_DTMF_RELAY) == input)
	{
	    return ContentType::APPLICATION_DTMF_RELAY;
	}
        return ContentType::UNKNOWN;
    }

    const char* m_buffer;
    const size_t m_buffer_length;

    Status m_status;
    Method m_method;
    ContentType m_content_type;
    uint32_t m_content_length;

    std::string m_realm;
    std::string m_nonce;
    std::string m_contact;
    std::string m_to_tag;
    std::string m_cseq;
    std::string m_call_id;
    std::string m_to;
    std::string m_from;
    std::string m_via;
    char m_dtmf_signal;
    uint16_t m_dtmf_duration;
    const char* m_body;

    static constexpr const char* LINE_ENDING = "\r\n";
    static constexpr size_t LINE_ENDING_LEN = 2;


    static constexpr const char* TAG = "SipPacket";
    static constexpr const char* SIP_2_0_SPACE = "SIP/2.0 ";
    static constexpr const char* WWW_AUTHENTICATE = "WWW-Authenticate";
    static constexpr const char* PROXY_AUTHENTICATE = "Proxy-Authenticate";
    static constexpr const char* CONTACT = "Contact: <";
    static constexpr const char* TO = "To: ";
    static constexpr const char* FROM = "From: ";
    static constexpr const char* VIA = "Via: ";
    static constexpr const char* C_SEQ = "CSeq: ";
    static constexpr const char* CALL_ID = "Call-ID: ";
    static constexpr const char* CONTENT_TYPE = "Content-Type: ";
    static constexpr const char* CONTENT_LENGTH = "Content-Length: ";
    static constexpr const char* REALM = "realm";
    static constexpr const char* NONCE = "nonce";
    static constexpr const char* NOTIFY = "NOTIFY ";
    static constexpr const char* BYE = "BYE ";
    static constexpr const char* INFO = "INFO ";
    static constexpr const char* APPLICATION_DTMF_RELAY = "application/dtmf-relay";
    static constexpr const char* SIGNAL = "Signal=";
    static constexpr const char* DURATION = "Duration=";
};
