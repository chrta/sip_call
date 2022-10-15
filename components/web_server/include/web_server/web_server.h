/*
   Copyright Christian Taedcke <hacking@taedcke.com>

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

#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_ota_ops.h>

extern const char index_start[] asm("_binary_index_html_start");
extern const char index_end[] asm("_binary_index_html_end");

template <class SipClientT>
class WebServer
{
private:
    using WebServerT = WebServer<SipClientT>;

    /*
     * Structure holding server handle
     * and internal socket fd in order
     * to use out of request send
     */
    struct async_resp_arg
    {
        httpd_handle_t hd;
        int fd;
        SipClientEvent event;

        bool is_used() const
        {
            return (hd != nullptr) && (fd != 0);
        }
    };

public:
    explicit WebServer(SipClientT& client)
        : client { client } {};

    WebServer(const WebServer&) = delete;
    WebServer(WebServer&&) = delete;

    WebServer& operator=(const WebServer&) = delete;
    WebServer& operator=(WebServer&&) = delete;

    bool start()
    {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.stack_size = 8192;
        config.lru_purge_enable = true;

        if (server != nullptr)
        {
            stop();
        }

        ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
        if (httpd_start(&server, &config) != ESP_OK)
        {
            ESP_LOGW(TAG, "Error starting server!");
            return false;
        }
        // Set URI handlers
        ESP_LOGD(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &index_page);
        httpd_register_uri_handler(server, &ws);
        httpd_register_uri_handler(server, &firmware_upload);

        return true;
    }

    bool stop()
    {
        esp_err_t err = httpd_stop(server);

        server = nullptr;

        if (err != ESP_OK)
        {
            ESP_LOGI(TAG, "Error stopping server (%d)!", err);
            return false;
        }

        return true;
    }

    void handle(SipClientT& /* client */, const SipClientEvent& event)
    {
        notify_subscriber(event);
    };

private:
    esp_err_t index_get_handler(httpd_req_t* req)
    {
        const uint32_t len = index_end - index_start;

        httpd_resp_send(req, index_start, len);

        return ESP_OK;
    }

    esp_err_t ws_handler(httpd_req_t* req)
    {
        if (req->method == HTTP_GET)
        {
            ESP_LOGI(TAG, "Handshake done, the new connection was opened");
            return ESP_OK;
        }
        httpd_ws_frame_t ws_pkt = {};
        uint8_t* buf = NULL;
        ws_pkt.type = HTTPD_WS_TYPE_TEXT;
        /* Set max_len = 0 to get the frame len */
        esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
            return ret;
        }

        ESP_LOGI(TAG, "frame len is %d", ws_pkt.len);
        if (ws_pkt.len)
        {
            /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
            buf = static_cast<uint8_t*>(calloc(1, ws_pkt.len + 1));
            if (buf == nullptr)
            {
                ESP_LOGE(TAG, "Failed to calloc memory for buf");
                return ESP_ERR_NO_MEM;
            }
            ws_pkt.payload = buf;
            /* Set max_len = ws_pkt.len to get the frame payload */
            ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
                free(buf);
                return ret;
            }
            ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
        }

        /* Reply to PING. For PONG and CLOSE, it will be handled elsewhere. */
        if (ws_pkt.type == HTTPD_WS_TYPE_PING)
        {
            /* Send PONG to reply back. */
            /* Please refer to RFC6455 Section 5.5.2 for more details */
            httpd_ws_frame_t frame;
            frame.payload = ws_pkt.payload;

            /* Now turn the frame to PONG */
            frame.len = ws_pkt.len;
            frame.type = HTTPD_WS_TYPE_PONG;
            ESP_LOGI(TAG, "web socket Pong");
            return httpd_ws_send_frame(req, &frame);
        }
        else if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE)
        {
            /* Response to the CLOSE frame */
            /* Please refer to RFC6455 Section 5.5.1 for more details */
            httpd_ws_frame_t frame;

            frame.len = 0;
            frame.type = HTTPD_WS_TYPE_CLOSE;
            frame.payload = NULL;
            ESP_LOGI(TAG, "Closing web socket");
            remove_subscriber(req);
            return httpd_ws_send_frame(req, &frame);
        }

        return handle_ws_packet(req, &ws_pkt);
    }

    esp_err_t handle_ws_packet(httpd_req_t* req, httpd_ws_frame_t* pkt)
    {
        ESP_LOGI(TAG, "Packet type: %d", pkt->type);
        bool subscribed = false;
        cJSON* root = cJSON_Parse(reinterpret_cast<const char*>(pkt->payload));

        if (root == nullptr)
        {
            ESP_LOGW(TAG, "Invalid json: %s", pkt->payload);
        }

        cJSON* command = cJSON_GetObjectItem(root, "command");

        if (cJSON_IsString(command))
        {
            if (strcmp(command->valuestring, "subscribe") == 0)
            {
                ESP_LOGI(TAG, "Detected subscribe command");
                add_subscriber(req);
                subscribed = true;
            }
            else if (strcmp(command->valuestring, "reset") == 0)
            {
                esp_restart();
            }
        }

        cJSON_Delete(root);
        free(pkt->payload);

        if (subscribed)
        {
            std::string data = "{\"result\":\"subscribed\"}";
            pkt->payload = (uint8_t*)data.c_str();
            pkt->len = data.size();

            esp_err_t ret = httpd_ws_send_frame(req, pkt);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "httpd_ws_send_frame failed with %d", ret);
            }
            return ret;
        }

        return ESP_OK;
    }

    esp_err_t upload_handler(httpd_req_t* req)
    {
        char buf[2048];
        /* File cannot be larger than a limit */
        if (req->content_len > MAX_UPLOAD_FILE_SIZE)
        {
            ESP_LOGE(TAG, "File too large : %d bytes", req->content_len);
            /* Respond with 400 Bad Request */
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                "File too big!");
            /* Return failure to close underlying connection else the
             * incoming file content will keep the socket busy */
            return ESP_FAIL;
        }

        const esp_partition_t* ota_partition = esp_ota_get_next_update_partition(nullptr);
        if (ota_partition == nullptr)
        {
            ESP_LOGE(TAG, "Failed to determine update partition");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to determine update partition");
            return ESP_FAIL;
        }

        esp_ota_handle_t ota_handle;
        esp_err_t err = esp_ota_begin(ota_partition, req->content_len, &ota_handle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to begin ota (%d)", err);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to begin ota");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Receiving file...");

        int received;

        /* Content length of the request gives
         * the size of the file being uploaded */
        int remaining = req->content_len;

        while (remaining > 0)
        {

            ESP_LOGI(TAG, "Remaining size : %d", remaining);
            /* Receive the file part by part into a buffer */
            if ((received = httpd_req_recv(req, buf, std::min(remaining, (int)sizeof(buf)))) <= 0)
            {
                if (received == HTTPD_SOCK_ERR_TIMEOUT)
                {
                    /* Retry if timeout occurred */
                    continue;
                }

                err = esp_ota_abort(ota_handle);
                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to abort ota (%d)", err);
                }

                ESP_LOGE(TAG, "File reception failed!");
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
                return ESP_FAIL;
            }

            /* Write buffer content to file on storage */
            if (received > 0)
            {
                err = esp_ota_write(ota_handle, buf, received);
                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to write ota (%d)", err);

                    err = esp_ota_abort(ota_handle);
                    if (err != ESP_OK)
                    {
                        ESP_LOGE(TAG, "Failed to abort ota (%d)", err);
                    }

                    /* Respond with 500 Internal Server Error */
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write file to storage");
                    return ESP_FAIL;
                }
            }

            /* Keep track of remaining size of
             * the file left to be uploaded */
            remaining -= received;
        }

        /* Close file upon upload completion */
        err = esp_ota_end(ota_handle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to end ota (%d)", err);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to end ota");
            return ESP_FAIL;
        }

        err = esp_ota_set_boot_partition(ota_partition);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set boot partition (%d)", err);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set boot part");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "File reception complete");

        httpd_resp_set_status(req, "204 No Content");
        httpd_resp_send(req, nullptr, 0);
        return ESP_OK;
    }

    void add_subscriber(httpd_req_t* req)
    {
        for (auto& s : subscriber)
        {
            if (!s.is_used())
            {
                s.hd = req->handle;
                s.fd = httpd_req_to_sockfd(req);
                return;
            }
        }
    }

    void remove_subscriber(httpd_req_t* req)
    {
        for (auto& s : subscriber)
        {
            if ((s.hd == req->handle) && (s.fd == httpd_req_to_sockfd(req)))
            {
                s.hd = nullptr;
                s.fd = 0;
                return;
            }
        }
    }

    void notify_subscriber(const SipClientEvent& event)
    {
        for (auto& s : subscriber)
        {
            if (s.is_used())
            {
                s.event = event;
                trigger_async_send(s);
            }
        }
    }

    esp_err_t trigger_async_send(const async_resp_arg& arg)
    {
        async_resp_arg* resp_arg = static_cast<async_resp_arg*>(malloc(sizeof(struct async_resp_arg)));
        *resp_arg = arg;
        return httpd_queue_work(server, ws_async_send, resp_arg);
    }

    static esp_err_t static_index_get_handler(httpd_req_t* req)
    {
        return static_cast<WebServerT*>(req->user_ctx)->index_get_handler(req);
    }

    static esp_err_t static_ws_handler(httpd_req_t* req)
    {
        return static_cast<WebServerT*>(req->user_ctx)->ws_handler(req);
    }

    static esp_err_t static_upload_handler(httpd_req_t* req)
    {
        return static_cast<WebServerT*>(req->user_ctx)->upload_handler(req);
    }

    static std::string to_json(const SipClientEvent& event)
    {
        std::string data = "{\"sip_event\":\"";
        switch (event.event)
        {
        case SipClientEvent::Event::CALL_CANCELLED:
            data += "Call cancelled\"";
            break;
        case SipClientEvent::Event::CALL_END:
            data += "Call ended\"";
            break;
        case SipClientEvent::Event::CALL_START:
            data += "Call started\"";
            break;
        case SipClientEvent::Event::BUTTON_PRESS:
            data += "Button pressed\",\"button\":\"";
            data += event.button_signal;
            data += "\",\"duration\":" + std::to_string(event.button_duration);
            break;
        }

        data += ",\"min_free_heap\": " + std::to_string(esp_get_minimum_free_heap_size());
        data += ",\"free_heap\": " + std::to_string(esp_get_free_heap_size());

        return data + +"}";
    }

    static void ws_async_send(void* arg)
    {
        async_resp_arg* resp_arg = static_cast<async_resp_arg*>(arg);
        httpd_handle_t hd = resp_arg->hd;
        int fd = resp_arg->fd;
        const SipClientEvent event = resp_arg->event;
        std::string data = to_json(event);
        httpd_ws_frame_t ws_pkt;
        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
        ws_pkt.payload = (uint8_t*)data.c_str();
        ws_pkt.len = data.size();
        ws_pkt.type = HTTPD_WS_TYPE_TEXT;

        httpd_ws_send_frame_async(hd, fd, &ws_pkt);
        free(resp_arg);
    }

    const httpd_uri_t index_page {
        .uri = "/",
        .method = HTTP_GET,
        .handler = static_index_get_handler,
        .user_ctx = this,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr
    };

    const httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = static_ws_handler,
        .user_ctx = this,
        .is_websocket = true,
        .handle_ws_control_frames = true,
        .supported_subprotocol = nullptr
    };

    const httpd_uri_t firmware_upload = {
        .uri = "/upload/firmware.bin",
        .method = HTTP_POST,
        .handler = static_upload_handler,
        .user_ctx = this,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr
    };

    SipClientT& client;
    httpd_handle_t server { nullptr };

    std::array<async_resp_arg, 5> subscriber {};

    static constexpr const char* TAG = "WebServer";

    static constexpr uint32_t MAX_UPLOAD_FILE_SIZE = 2 * 1024 * 1024;
};
