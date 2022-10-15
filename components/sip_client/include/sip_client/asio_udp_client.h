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
#include <cstring>
#include <functional>
#include <string>
#include <utility>

#include "esp_log.h"

static constexpr const int RX_BUFFER_SIZE = 2048;
static constexpr const int TX_BUFFER_SIZE = 2048;

#ifndef COMPILE_FOR_NATIVE
/* Workaround for asio esp-idf issue
 * https://github.com/espressif/esp-idf/issues/3557
 * This is already fixed in commit
 * https://github.com/espressif/esp-idf/commit/b7ef7feaebf9ad3f532c088ea3bc77021c34a69c
 * but leave this in to enable usage with older esp-idf.
 */
__attribute__((weak)) char* if_indextoname(unsigned int, char*) { return 0; }
#endif

template <std::size_t SIZE>
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

    template <typename T>
    typename std::enable_if<std::is_unsigned_v<T>, Buffer<SIZE>&>::type operator<<(T i)
    {
        snprintf(m_buffer.data() + strlen(m_buffer.data()), m_buffer.size() - strlen(m_buffer.data()), "%u", static_cast<unsigned>(i));
        return *this;
    }

    [[nodiscard]] const char* data() const
    {
        return m_buffer.data();
    }

    [[nodiscard]] size_t size() const
    {
        return strlen(m_buffer.data());
    }

private:
    std::array<char, SIZE> m_buffer;
};

using TxBufferT = Buffer<TX_BUFFER_SIZE>;

#ifndef COMPILE_FOR_NATIVE
class TcpIpAdapterInitializer
{
public:
    TcpIpAdapterInitializer()
    {
        ESP_ERROR_CHECK(esp_netif_init());
    }
};
#endif /* COMPILE_FOR_NATIVE */

class AsioUdpClient
{
public:
    AsioUdpClient(asio::io_context& io_context, std::string server_ip, std::string server_port, uint16_t local_port, std::function<void(std::string)> on_received)
        : m_io_context(io_context)
        , m_server_port(std::move(server_port))
        , m_server_ip(std::move(server_ip))
        , m_local_port(local_port)
        , m_socket { io_context }
        , m_on_received { std::move(on_received) }
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
        m_socket.close();
    }

    bool init()
    {
        asio::error_code ec;

        if (m_socket.is_open())
        {
            ESP_LOGW(TAG, "Socket already initialized");
            return false;
        }

        ESP_LOGI(TAG, "Doing DNS lookup for host=%s port=%s", m_server_ip.c_str(), m_server_port.c_str());
        asio::ip::udp::resolver resolver(m_io_context);
        const asio::ip::udp::resolver::results_type endpoints = resolver.resolve(asio::ip::udp::v4(), m_server_ip, m_server_port, ec);

        if (endpoints.empty() || ec)
        {
            ESP_LOGE(TAG, "DNS lookup failed: %s", ec.message().c_str());
            return false;
        }

        m_destination_endpoint = *endpoints.begin();

        ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", m_destination_endpoint.address().to_string().c_str());

        m_socket.open(asio::ip::udp::v4(), ec);

        if (ec)
        {
            ESP_LOGE(TAG, "... Failed to open socket: %s\r\n", ec.message().c_str());
            return false;
        }
        ESP_LOGI(TAG, "... opened socket %d\r\n", m_socket.native_handle());

        m_socket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), m_local_port), ec);

        if (ec)
        {
            ESP_LOGE(TAG, "... Failed to bind: %s", ec.message().c_str());
            m_socket.close();
            return false;
        }

        do_receive();
        return true;
    }

    [[nodiscard]] bool is_initialized() const
    {
        return m_socket.is_open();
    }

    void do_receive()
    {
        m_socket.async_receive_from(
            asio::buffer(m_rx_buffer), m_sender_endpoint,
            [this](std::error_code ec, std::size_t bytes_recvd) {
                if (!ec && bytes_recvd > 0)
                {
                    m_rx_buffer[bytes_recvd] = '\0';
                    ESP_LOGV(TAG, "Received %d byte", bytes_recvd);
                    ESP_LOGV(TAG, "Received following data: %s", m_rx_buffer.data());
                    if (m_on_received)
                    {
                        m_on_received(std::string(m_rx_buffer.data(), bytes_recvd));
                    }
                }
                do_receive();
            });
    }

    TxBufferT& get_new_tx_buf()
    {
        m_tx_buffer.clear();
        return m_tx_buffer;
    }

    bool send_buffered_data()
    {
        ESP_LOGV(TAG, "Sending %d byte", m_tx_buffer.size());
        ESP_LOGV(TAG, "Sending following data: %s", m_tx_buffer.data());
        const asio::socket_base::message_flags flags = 0;
        asio::error_code ec;

        const size_t result = m_socket.send_to(
            asio::buffer(m_tx_buffer.data(), m_tx_buffer.size()), m_destination_endpoint, flags, ec);

        if (ec || (result <= 0))
        {
            ESP_LOGD(TAG, "Failed to send data %d, error=%s", result, ec.message().c_str());
        }

        return result == m_tx_buffer.size();
    }

private:
#ifndef COMPILE_FOR_NATIVE
    TcpIpAdapterInitializer m_initializer;
#endif /* COMPILE_FOR_NATIVE */
    asio::io_context& m_io_context;
    const std::string m_server_port;
    std::string m_server_ip;
    const uint16_t m_local_port;

    TxBufferT m_tx_buffer;
    std::array<char, RX_BUFFER_SIZE> m_rx_buffer {};
    asio::ip::udp::socket m_socket;
    std::function<void(std::string)> m_on_received;
    asio::ip::udp::endpoint m_destination_endpoint;
    asio::ip::udp::endpoint m_sender_endpoint;

    static constexpr const char* TAG = "UdpSocket";
};
