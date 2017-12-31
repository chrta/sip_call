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

#include <array>
#include <string>
#include <cstring>

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "esp_log.h"

static constexpr const int RX_BUFFER_SIZE = 2048;
static constexpr const int TX_BUFFER_SIZE = 2048;


template<std::size_t SIZE>
class Buffer
{
public:
    Buffer()
    {
        clear();
    }

    void clear()
    {
        m_buffer[0] = '\0';
    }

    Buffer<SIZE>& operator<<(const char* str)
    {
        strncat(m_buffer.data(), str, m_buffer.size() - strlen(m_buffer.data()) - 1);
        return *this;
    }
    Buffer<SIZE>& operator<<(const std::string& str)
    {
        strncat(m_buffer.data(), str.c_str(), m_buffer.size() - strlen(m_buffer.data()) - 1);
        return *this;
    }
    Buffer<SIZE>& operator<<(uint16_t i)
    {
        snprintf(m_buffer.data() + strlen(m_buffer.data()), m_buffer.size() - strlen(m_buffer.data()), "%d", i);
        return *this;
    }
    Buffer<SIZE>& operator<<(uint32_t i)
    {
        snprintf(m_buffer.data() + strlen(m_buffer.data()), m_buffer.size() - strlen(m_buffer.data()), "%d", i);
        return *this;
    }

    const char* data() const
    {
        return m_buffer.data();
    }

    size_t size() const
    {
        return strlen(m_buffer.data());
    }

private:
    std::array<char, SIZE> m_buffer;
};

using TxBufferT = Buffer<TX_BUFFER_SIZE>;

class LwipUdpClient
{
public:

    LwipUdpClient(const std::string& server_ip, const std::string& server_port, uint16_t local_port)
    : m_server_port(server_port)
    , m_server_ip(server_ip)
    , m_local_port(local_port)
    , m_socket(INVALID_SOCKET)
    {
        bzero(&m_dest_addr, sizeof(m_dest_addr));
    }

    ~LwipUdpClient()
    {
    }

    void set_server_ip(const std::string& server_ip)
    {
        if (is_initialized())
        {
            deinit();
        }
        m_server_ip = server_ip;
    }

    void deinit()
    {
        if (!is_initialized())
        {
            return;
        }
        close(m_socket);
        m_socket = INVALID_SOCKET;
    }

    bool init()
    {
        if (m_socket >= 0)
        {
            ESP_LOGW(TAG, "Socket already initialized");
            return false;
        }
        struct addrinfo hints;
        bzero(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;

        struct addrinfo *res;
        struct in_addr *addr;
        ESP_LOGI(TAG, "Doing DNS lookup for host=%s port=%s", m_server_ip.c_str(), m_server_port.c_str());

        int err = getaddrinfo(m_server_ip.c_str(), m_server_port.c_str(), &hints, &res);

        if(err != 0 || res == NULL)
        {
            ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
            return false;
        }

        /* Code to print the resolved IP.
         * Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
        addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

        m_socket = socket(res->ai_family, res->ai_socktype, 0);
        if(m_socket < 0) {
            ESP_LOGE(TAG, "... Failed to allocate socket.");
            freeaddrinfo(res);
            return false;
        }
        ESP_LOGI(TAG, "... allocated socket %d\r\n", m_socket);

        /*Destination*/
        bzero(&m_dest_addr, sizeof(m_dest_addr));
        m_dest_addr.sin_family = AF_INET;
        m_dest_addr.sin_len = sizeof(m_dest_addr);
        m_dest_addr.sin_addr.s_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
        m_dest_addr.sin_port = ((struct sockaddr_in *)res->ai_addr)->sin_port;

        freeaddrinfo(res);

        /*Source*/
        struct sockaddr_in local_addr;
        bzero(&local_addr, sizeof(local_addr));
        local_addr.sin_family = AF_INET;
        local_addr.sin_len = sizeof(local_addr);
        local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        local_addr.sin_port = htons(m_local_port);

        if (bind(m_socket, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0)
        {
            ESP_LOGE(TAG, "... Failed to bind");
            close(m_socket);
            m_socket = INVALID_SOCKET;
            return false;
        }

        return true;
    }

    bool is_initialized() const
    {
        return m_socket >= 0;
    }


    std::string receive(uint32_t timeout_msec)
    {
        FD_ZERO(&m_rx_fds);
        FD_SET(m_socket, &m_rx_fds);

        m_rx_timeval.tv_sec = timeout_msec / 1000;
        m_rx_timeval.tv_usec = (timeout_msec - (m_rx_timeval.tv_sec * 1000))* 1000;

        int readable = select(m_socket + 1, &m_rx_fds, nullptr, nullptr, &m_rx_timeval);

        if (readable < 0)
        {
            ESP_LOGW(TAG, "Select error: %d, errno=%d", readable, errno);
        }
        if (readable <= 0)
        {
            return "";
        }

        ssize_t len = recv(m_socket, m_rx_buffer.data(), m_rx_buffer.size(), 0);
        if (len <= 0)
        {
            ESP_LOGD(TAG, "Received no data: %d, errno=%d", len, errno);
            return "";
        }
        m_rx_buffer[len] = '\0';
        ESP_LOGD(TAG, "Received %d byte", len);
        ESP_LOGV(TAG, "Received following data: %s", m_rx_buffer.data());

        return std::string(m_rx_buffer.data(), len);
    }

    TxBufferT& get_new_tx_buf()
    {
        m_tx_buffer.clear();
        return m_tx_buffer;
    }

    bool send_buffered_data()
    {
        ESP_LOGD(TAG, "Sending %d byte", m_tx_buffer.size());
        ESP_LOGV(TAG, "Sending following data: %s", m_tx_buffer.data());
        ssize_t result = sendto(m_socket, m_tx_buffer.data(), m_tx_buffer.size(), 0, (struct sockaddr *)&m_dest_addr, sizeof(m_dest_addr));
        if (result < 0)
        {
            ESP_LOGD(TAG, "Failed to send data %d, errno=%d", result, errno);
        }
        return result == m_tx_buffer.size();
    }


private:
    const std::string m_server_port;
    std::string m_server_ip;
    const uint16_t m_local_port;

    TxBufferT m_tx_buffer;
    std::array<char, RX_BUFFER_SIZE> m_rx_buffer;
    int m_socket;
    sockaddr_in m_dest_addr;

    fd_set m_rx_fds;
    struct timeval m_rx_timeval;

    static constexpr const char* TAG = "UdpSocket";
    static constexpr const int INVALID_SOCKET = -1;
};
