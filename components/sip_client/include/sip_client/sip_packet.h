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

  
  SipPacket(const char* input_buffer, size_t input_buffer_length)
    : m_buffer(input_buffer)
    , m_buffer_length(input_buffer_length)
    , m_status(Status::UNKNOWN)
    , m_method(Method::UNKNOWN)
    , m_realm()
    , m_nonce()
  {
  }

  bool parse_header()
  {
    const char* start_position = m_buffer;
    char* end_position = strstr(start_position, LINE_ENDING);

    m_method = Method::UNKNOWN;
    m_status = Status::UNKNOWN;
    m_cseq = "";
    m_call_id = "";
    m_to = "";
    m_from = "";
    m_via = "";
      
    if (end_position == nullptr)
    {
      ESP_LOGW(TAG, "No line endong found in %s", m_buffer);
      return false;
    }

    uint32_t line_number = 0;
    do
    {
      size_t length = end_position - start_position;
      if (length == 0) //line only contains the line ending
      {
	//valid end of header
	ESP_LOGV(TAG, "Valid end of header detected");
	return true;
      }
      const char* next_start_position = end_position + LINE_ENDING_LEN;
      line_number++;
      
      //create a proper null terminated c string
      //frome here on string functions may be used!
      memset(end_position, 0, LINE_ENDING_LEN);
      ESP_LOGV(TAG, "Parsing line: %s", start_position);

      if (strstr(start_position, "SIP/2.0 ") == start_position)
      {
	long code = strtol(start_position + strlen ("SIP/2.0 "), nullptr, 10);
	ESP_LOGV(TAG, "Detect status %ld", code);
	switch (code)
	{
	case 200: m_status = Status::OK_200; break;
	case 401: m_status = Status::UNAUTHORIZED_401; break;
	case 100: m_status = Status::TRYING_100; break;
	case 183: m_status = Status::SESSION_PROGRESS_183; break;
	case 500: m_status = Status::SERVER_ERROR_500; break;
	case 487: m_status = Status::REQUEST_CANCELLED_487; break;
	case 407: m_status = Status::PROXY_AUTH_REQ_407; break;
	case 603: m_status = Status::DECLINE_603; break;
	default:  m_status = Status::UNKNOWN;
	}
      }
      else if ((strncmp("WWW-Authenticate", start_position, strlen("WWW-Authenticate")) == 0) || (strncmp("Proxy-Authenticate", start_position, strlen("Proxy-Authenticate")) == 0))
      {
	ESP_LOGV(TAG, "Detect authenticate line");
	//read realm and nonce from authentication line
	if (!read_param(start_position, "realm", m_realm))
	{
	  ESP_LOGW(TAG, "Failed to read realm in authenticate line");
	  //continue;
	}
	if (!read_param(start_position, "nonce", m_nonce))
	{
	  ESP_LOGW(TAG, "Failed to read nonce in authenticate line");
	  //continue;
	}
	ESP_LOGI(TAG, "Realm is %s and nonce is %s", m_realm.c_str(), m_nonce.c_str());
      }
      else if (strncmp("Contact: <", start_position, strlen("Contact: <")) == 0)
      {
	ESP_LOGV(TAG, "Detect contact line");
	const char* last_pos = strstr(start_position, ">");
	if (last_pos == nullptr)
	{
	  ESP_LOGW(TAG, "Failed to read content of contact line");
	}
	else
	{
	  m_contact = std::string(start_position + strlen("Contact: <"), last_pos);
	}
      }
      else if (strncmp("To: ", start_position, strlen("To: ")) == 0)
      {
	ESP_LOGV(TAG, "Detect to line");
	const char* tag_pos = strstr(start_position, ">;tag=");
	if (tag_pos != nullptr)
	{
	  m_to_tag = std::string(tag_pos + strlen(">;tag="));
	}
	m_to = std::string(start_position + strlen("To: "));
      }
      else if (strstr(start_position, "From: ") == start_position)
      {
	m_from = std::string(start_position + strlen("From: "));
      }
      else if (strstr(start_position, "Via: ") == start_position)
      {
	m_via = std::string(start_position + strlen("Via: "));
      }
      else if (strstr(start_position, "CSeq: ") == start_position)
      {
	m_cseq = std::string(start_position + strlen("CSeq: "));
      }
      else if (strstr(start_position, "Call-ID: ") == start_position)
      {
	m_call_id = std::string(start_position + strlen("Call-ID: "));
      }
      else if (line_number == 1)
      {
	//first line, but no respone
	if (strstr(start_position, "NOTIFY ") == start_position)
	{
	  m_method = Method::NOTIFY;
	}
	else if (strstr(start_position, "BYE ") == start_position)
	{
	  m_method = Method::BYE;
	}
	else if (strstr(start_position, "INFO ") == start_position)
	{
	  m_method = Method::INFO;
	}
    
      }

      //go to next line
      start_position = next_start_position;
      end_position = strstr(start_position, LINE_ENDING);
    } while(end_position);

    //no line only containing the line ending found :(
    return false;
  }

  Status get_status() const
  {
    return m_status;
  }

  Method get_method() const
  {
    return m_method;
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

private:
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

  
  const char* m_buffer;
  const size_t m_buffer_length;

  Status m_status;
  Method m_method;
  std::string m_realm;
  std::string m_nonce;
  std::string m_contact;
  std::string m_to_tag;
  std::string m_cseq;
  std::string m_call_id;
  std::string m_to;
  std::string m_from;
  std::string m_via;

  static constexpr const char* LINE_ENDING = "\r\n";
  static constexpr size_t LINE_ENDING_LEN = 2;
  
  
  static constexpr const char* TAG = "SipPacket";  
};
