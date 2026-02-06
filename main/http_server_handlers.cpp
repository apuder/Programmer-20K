#include "sdkconfig.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_err.h"
#include "wifi.h"
#include "serial.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "cJSON.h"
#include "upload_receiver.h"
#include "http_server_handlers.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <cstring>
#include <string>
#include <cstdio>

static const char *TAG = "HTTP";

static httpd_handle_t server = NULL;
static SemaphoreHandle_t serial_ws_mutex = NULL;
static int serial_ws_fd = -1;


// The build system (EMBED_TXTFILES) provides symbols _binary_index_html_start/_end
extern const uint8_t _binary_index_html_start[] asm("_binary_index_html_start");
extern const uint8_t _binary_index_html_end[]   asm("_binary_index_html_end");

extern const uint8_t _binary_favicon_svg_start[] asm("_binary_favicon_svg_start");
extern const uint8_t _binary_favicon_svg_end[]   asm("_binary_favicon_svg_end");


static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t len = (size_t)(_binary_index_html_end - _binary_index_html_start);
    return httpd_resp_send(req, (const char*)_binary_index_html_start, len);
}

static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/svg+xml");
    size_t len = (size_t)(_binary_favicon_svg_end - _binary_favicon_svg_start);
    return httpd_resp_send(req, (const char*)_binary_favicon_svg_start, len);
}

static void set_serial_ws_fd(int fd)
{
    if (serial_ws_mutex) xSemaphoreTake(serial_ws_mutex, portMAX_DELAY);
    serial_ws_fd = fd;
    if (serial_ws_mutex) xSemaphoreGive(serial_ws_mutex);
}

static int get_serial_ws_fd()
{
    int fd = -1;
    if (!serial_ws_mutex) return serial_ws_fd;
    if (xSemaphoreTake(serial_ws_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        fd = serial_ws_fd;
        xSemaphoreGive(serial_ws_mutex);
    }
    return fd;
}

static esp_err_t serial_ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        // Handshake request
        int fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "Serial WS open fd=%d", fd);
        set_serial_ws_fd(fd);
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {0};
    frame.type = HTTPD_WS_TYPE_BINARY;
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) return ret;

    if (frame.len) {
        frame.payload = (uint8_t*)malloc(frame.len + 1);
        if (!frame.payload) return ESP_ERR_NO_MEM;
        ret = httpd_ws_recv_frame(req, &frame, frame.len);
        if (ret == ESP_OK) frame.payload[frame.len] = 0;
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        ESP_LOGI(TAG, "Serial WS closed");
        set_serial_ws_fd(-1);
    } else if (frame.type == HTTPD_WS_TYPE_PING) {
        frame.type = HTTPD_WS_TYPE_PONG;
        httpd_ws_send_frame(req, &frame);
    } else if (frame.type == HTTPD_WS_TYPE_TEXT || frame.type == HTTPD_WS_TYPE_BINARY) {
        // Forward incoming text/binary data to UART TX
        if (frame.payload && frame.len > 0) {
            serial_transmit(frame.payload, frame.len);
        }
    }

    if (frame.payload) free(frame.payload);
    return ESP_OK;
}

/* GET /status
 * Returns minimal server status JSON with mode information
 */
static esp_err_t status_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "server", "running");
    bool is_sta = wifi_is_sta_mode();
    cJSON_AddStringToObject(root, "mode", is_sta ? "access" : "hotspot");
    char *out = cJSON_PrintUnformatted(root);
    send_json(req, out ? out : "{\"server\":\"running\"}");
    if (out) free(out);
    cJSON_Delete(root);
    return ESP_OK;
}

/* Wi‑Fi code has been moved to main/wifi.cpp — http server simply registers the endpoints.
 * see wifi.h for the API used below
 */

/* /wifi POST handler implemented in main/wifi.cpp */

/* /wifi DELETE handler implemented in main/wifi.cpp */

/* POST /upload — streams incoming request body and hands bytes to the upload_receiver interface.
 * The UI posts each file as multipart/form-data — server receives the raw multipart body chunks and forwards
 * them in streaming fashion to the configured upload handler. This keeps RAM low and avoids filesystem
 * dependencies in the default firmware.
 */
static esp_err_t upload_post_handler(httpd_req_t *req)
{
    // Determine target from query param "target" or default to flash
    char buf[64];
    memset(buf, 0, sizeof(buf));
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        ESP_LOGI(TAG, "query: %s", buf);
    }

    // naive parse for target param
    std::string target = "flash";
    if (strlen(buf) > 0) {
        const char *p = strstr(buf, "target=");
        if (p) {
            p += strlen("target=");
            const char *e = strchr(p, '&');
            if (!e) e = p + strlen(p);
            target = std::string(p, e-p);
        }
    }

    // Determine an optional filename hint (query param `name`) — receiver may use or ignore it.
    char namebuf[128];
    namebuf[0] = '\0';
    if (strlen(buf) > 0) {
        const char *p = strstr(buf, "name=");
        if (p) {
            p += strlen("name=");
            const char *e = strchr(p, '&');
            size_t len = e ? (size_t)(e - p) : strlen(p);
            if (len > 0 && len < sizeof(namebuf)) memcpy(namebuf, p, len);
            namebuf[len < sizeof(namebuf) ? len : sizeof(namebuf)-1] = '\0';
        }
    }

    // Initialize receiver for streaming uploads
    if (!upload_receiver_init(target.c_str(), namebuf[0] ? namebuf : NULL)) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_json(req, "{\"success\":false,\"error\":\"receiver init\"}");
    }

    const int buf_len = 1024;
    char *read_buf = (char*)malloc(buf_len);
    if (!read_buf) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"success\":false,\"error\":\"alloc\"}");
    }

    int total = 0;
    int r;
    while ((r = httpd_req_recv(req, read_buf, buf_len)) > 0) {
        total += r;
        // stream chunk into the receiver
        size_t consumed = upload_receiver_write((const uint8_t*)read_buf, (size_t)r);
        if (consumed != (size_t)r) {
            ESP_LOGW(TAG, "Receiver failed to accept bytes (expected %d got %u)", r, (unsigned)consumed);
            r = -1; // mark failure
            break;
        }
        // otherwise we discard the bytes
    }

    // notify receiver of completion/failure
    if (r < 0) upload_receiver_finish(false);
    else upload_receiver_finish(true);
    free(read_buf);

    if (r < 0) {
        ESP_LOGW(TAG, "Receive error: %d", r);
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_json(req, "{\"success\":false,\"error\":\"recv\"}");
    }

    // Determine filename (receiver may report an assigned name)
    cJSON *out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "success", true);
    cJSON_AddStringToObject(out, "target", target.c_str());
    cJSON_AddNumberToObject(out, "bytes", total);

    char *s = cJSON_PrintUnformatted(out);
    send_json(req, s ? s : "{\"success\":true}\n");
    if (s) free(s);
    cJSON_Delete(out);
    return ESP_OK;
}

/* helper to mount nvs (used by wifi handlers) and start http server */
extern "C" esp_err_t start_http_server()
{
    init_wifi();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // lower stack usage for constrained systems
    config.stack_size = 4096;
    config.max_uri_handlers = 16;

    // Ensure network stack / event loop are initialized before the HTTP server creates sockets
    // HTTP server only; wifi setup/initialization is performed by the wifi module.

    if (!serial_ws_mutex) {
        serial_ws_mutex = xSemaphoreCreateMutex();
    }

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    httpd_uri_t index_get = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &index_get);

    httpd_uri_t index_html_get = {
        .uri = "/index.html",
        .method = HTTP_GET,
        .handler = index_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &index_html_get);

    httpd_uri_t favicon_get = {
        .uri = "/favicon.svg",
        .method = HTTP_GET,
        .handler = favicon_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &favicon_get);

    // Also register for /favicon.ico since browsers request this by default
    httpd_uri_t favicon_ico = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &favicon_ico);

    httpd_uri_t status_get = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &status_get);

    // register wifi handlers implemented in main/wifi.cpp
    register_wifi_http_handlers(server);

    httpd_uri_t upload = {
        .uri = "/upload",
        .method = HTTP_POST,
        .handler = upload_post_handler
    };
    httpd_register_uri_handler(server, &upload);

    httpd_uri_t serial_ws = {
        .uri = "/serial",
        .method = HTTP_GET,
        .handler = serial_ws_handler,
        .user_ctx = NULL,
        .is_websocket = true
    };
    httpd_register_uri_handler(server, &serial_ws);

    ESP_LOGI(TAG, "HTTP server started");

    // No Wi‑Fi initialization here; wifi handlers are responsible for initializing Wi‑Fi
    return ESP_OK;
}

/* optional server stop */
extern "C" esp_err_t stop_http_server()
{
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
    set_serial_ws_fd(-1);
    // no filesystem to unmount in this simplified handler (uploads are handed off via upload_receiver)
    return ESP_OK;
}

extern "C" void log_serial_monitor(uint8_t *data, int len)
{
    if (!data || len <= 0 || !server) return;
    int fd = get_serial_ws_fd();
    if (fd < 0) return;

    httpd_ws_frame_t frame = {0};
    frame.type = HTTPD_WS_TYPE_BINARY;
    frame.payload = data;
    frame.len = len;

    esp_err_t err = httpd_ws_send_frame_async(server, fd, &frame);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "serial ws send failed: %s", esp_err_to_name(err));
        set_serial_ws_fd(-1);
    }
}
