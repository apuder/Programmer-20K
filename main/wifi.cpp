#include "wifi.h"
#include "event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "lwip/ip4_addr.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

static const char *TAG = "wifi";

static bool wifi_initialized = false;
static bool wifi_connected = false;
static char wifi_ip[64] = {0};
static SemaphoreHandle_t wifi_mutex = NULL;
static bool wifi_sta_mode = false; // cached flag for whether credentials are configured

// forward-declare handlers so register/unregister can use stable references
static esp_err_t wifi_post_handler(httpd_req_t *req);
static esp_err_t wifi_delete_handler(httpd_req_t *req);

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_connected = false;
            ESP_LOGI(TAG, "STA disconnected, attempting reconnect");
            evt_signal(EVT_WIFI_DOWN);
            // try reconnect
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            uint32_t addr = event->ip_info.ip.addr;
            uint8_t b0 = addr & 0xff;
            uint8_t b1 = (addr >> 8) & 0xff;
            uint8_t b2 = (addr >> 16) & 0xff;
            uint8_t b3 = (addr >> 24) & 0xff;
            snprintf(wifi_ip, sizeof(wifi_ip), "%u.%u.%u.%u", b0, b1, b2, b3);
            wifi_connected = true;
            ESP_LOGI(TAG, "STA got IP: %s", wifi_ip);
            evt_signal(EVT_WIFI_UP);
        } else if (event_id == IP_EVENT_AP_STAIPASSIGNED) {
            ip_event_ap_staipassigned_t* evt = (ip_event_ap_staipassigned_t*)event_data;
            uint32_t ipaddr = evt->ip.addr;
            uint8_t *a = (uint8_t*)&ipaddr;
#if 0
            ESP_LOGI(TAG, "AP DHCP gave client %02x:%02x:%02x:%02x:%02x:%02x IP %u.%u.%u.%u",
                     evt->mac[0], evt->mac[1], evt->mac[2], evt->mac[3], evt->mac[4], evt->mac[5],
                     a[0], a[1], a[2], a[3]);
#endif
            ESP_LOGI(TAG, "AP DHCP gave client IP %u.%u.%u.%u",
                                       a[0], a[1], a[2], a[3]);
        }
    }
}

static esp_err_t mdns_setup(const char* hostname)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns init failed: %s", esp_err_to_name(err));
        return err;
    }
    mdns_hostname_set(hostname);
    mdns_instance_name_set("Programmer-20K");
    ESP_LOGI(TAG, "mDNS started (hostname=%s.local)", hostname);
    return ESP_OK;
}

esp_err_t wifi_init_base()
{
    if (wifi_initialized) return ESP_OK;

    // Create mutex if it doesn't exist
    if (wifi_mutex == NULL) {
        wifi_mutex = xSemaphoreCreateMutex();
        if (wifi_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create wifi mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t err;

    // Make sure NVS is available for storing credentials
    err = nvs_flash_init();
    if (err != ESP_OK && err != ESP_ERR_NVS_NO_FREE_PAGES && err != ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs init err %s", esp_err_to_name(err));
    }

    esp_netif_init();
    esp_event_loop_create_default();

    // create default netif instances so STA/AP modes have network interfaces
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }

    // register handlers using compatibility API (older IDF)
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &wifi_event_handler, NULL);

    wifi_initialized = true;
    return ESP_OK;
}

esp_err_t wifi_start_ap()
{
    ESP_LOGI(TAG, "Starting Wi-Fi AP: Programmer-20K");

    if (!wifi_initialized) {
        ESP_LOGW(TAG, "wifi not initialized, call wifi_init_base() first");
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t ap_config = {};
    strncpy((char*)ap_config.ap.ssid, "Programmer-20K", sizeof(ap_config.ap.ssid)-1);
    ap_config.ap.ssid_len = strlen("Programmer-20K");
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;

    // Prepare AP netif IP and DHCP server before bringing the AP up
    esp_netif_t *ap_if = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_if) {
        esp_netif_ip_info_t ip_info;
        IP4_ADDR(&ip_info.ip, 192,168,4,1);
        IP4_ADDR(&ip_info.gw, 192,168,4,1);
        IP4_ADDR(&ip_info.netmask, 255,255,255,0);

        esp_err_t e = esp_netif_dhcps_stop(ap_if);
#if defined(ESP_ERR_ESP_NETIF_DHCPS_STOPPED)
        if (e != ESP_OK && e != ESP_ERR_ESP_NETIF_DHCPS_STOPPED) {
#else
        if (e != ESP_OK) {
#endif
            ESP_LOGW(TAG, "dhcps_stop failed before AP start: %s", esp_err_to_name(e));
        }

        e = esp_netif_set_ip_info(ap_if, &ip_info);
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "set AP IP failed before AP start: %s", esp_err_to_name(e));
        }

        e = esp_netif_dhcps_start(ap_if);
#if defined(ESP_ERR_ESP_NETIF_DHCPS_STARTED)
        if (e != ESP_OK && e != ESP_ERR_ESP_NETIF_DHCPS_STARTED) {
#else
        if (e != ESP_OK) {
#endif
            ESP_LOGW(TAG, "dhcps_start failed before AP start: %s", esp_err_to_name(e));
        }
    } else {
        ESP_LOGW(TAG, "AP netif handle not found; DHCP setup will be attempted after AP start");
    }

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_err_t start_err = esp_wifi_start();
    if (start_err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_start failed: %s", esp_err_to_name(start_err));
    }

    // mDNS advertising (lowercase hostname for mDNS)
    mdns_setup("programmer-20k");

    wifi_connected = false;
    wifi_ip[0] = '\0';
    evt_signal(EVT_WIFI_NOT_INIT);
    return ESP_OK;
}

esp_err_t wifi_start_sta(const char* ssid, const char* pass)
{
    ESP_LOGI(TAG, "Starting Wi-Fi STA (ssid=%s)", ssid ? ssid : "(null)");

    if (!wifi_initialized) {
        ESP_LOGW(TAG, "wifi not initialized, call wifi_init_base() first");
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t sta_config = {};
    if (ssid) strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid)-1);
    if (pass) strncpy((char*)sta_config.sta.password, pass, sizeof(sta_config.sta.password)-1);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    esp_wifi_start();
    esp_wifi_connect();

    // setup mDNS hostname for discovery
    mdns_setup("programmer-20k");

    return ESP_OK;
}

/* HTTP handlers moved here: /wifi POST and DELETE */
static esp_err_t wifi_post_handler(httpd_req_t *req)
{
    const int max_read_len = 2048;
    int total = req->content_len;
    if (total <= 0 || total > max_read_len) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"invalid body\"}");
    }

    char *buf = (char*)malloc(total + 1);
    if (!buf) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"memory\"}");
    }

    int remaining = total; int r = 0;
    while (remaining > 0) {
        int ret = httpd_req_recv(req, buf + r, remaining);
        if (ret <= 0) { free(buf); return ESP_FAIL; }
        remaining -= ret; r += ret;
    }
    buf[total] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"invalid json\"}");
    }

    const cJSON *jssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    const cJSON *jpass = cJSON_GetObjectItemCaseSensitive(root, "password");
    const char *ssid = (jssid && cJSON_IsString(jssid)) ? jssid->valuestring : NULL;
    const char *password = (jpass && cJSON_IsString(jpass)) ? jpass->valuestring : NULL;

    if (!ssid) {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"missing ssid\"}");
    }

    esp_err_t err;
    nvs_handle_t nvs_handle;
    err = nvs_open("wifi_cfg", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_set_str(nvs_handle, "ssid", ssid);
        if (password) nvs_set_str(nvs_handle, "password", password);
        else nvs_erase_key(nvs_handle, "password");
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        wifi_sta_mode = true; // credentials stored, mark as STA mode
    } else {
        ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(err));
    }

    // Apply credentials immediately
    wifi_init_base();
    wifi_start_sta(ssid, password);

    // Return immediate state — connection may still be in progress
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddBoolToObject(resp, "connected", wifi_connected);
    if (wifi_connected) cJSON_AddStringToObject(resp, "ip", wifi_ip);
    char *resp_s = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp_s ? resp_s : "{\"success\":true}");
    if (resp_s) free(resp_s);
    cJSON_Delete(resp);
    cJSON_Delete(root);
    return ESP_OK;
}

void wifi_reset()
{
    esp_err_t err;
    nvs_handle_t nvs_handle;

    if (wifi_mutex && xSemaphoreTake(wifi_mutex, portMAX_DELAY) == pdTRUE) {
        err = nvs_open("wifi_cfg", NVS_READWRITE, &nvs_handle);
        if (err == ESP_OK) {
            nvs_erase_key(nvs_handle, "ssid");
            nvs_erase_key(nvs_handle, "password");
            nvs_commit(nvs_handle);
            nvs_close(nvs_handle);
            wifi_sta_mode = false; // credentials cleared, mark as AP mode
        }
        // remove stored data and start AP mode to allow clients to reconfigure
        wifi_init_base();
        wifi_start_ap();
    }
    if (wifi_mutex) {
         xSemaphoreGive(wifi_mutex);
    }
}

static esp_err_t wifi_delete_handler(httpd_req_t *req)
{
    wifi_reset();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t register_wifi_http_handlers(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;

    httpd_uri_t wifi_post = {
        .uri = "/wifi",
        .method = HTTP_POST,
        .handler = wifi_post_handler
    };
    httpd_register_uri_handler(server, &wifi_post);

    httpd_uri_t wifi_del = {
        .uri = "/wifi",
        .method = HTTP_DELETE,
        .handler = wifi_delete_handler
    };
    httpd_register_uri_handler(server, &wifi_del);

    return ESP_OK;
}

esp_err_t unregister_wifi_http_handlers(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    // httpd_unregister_uri in older IDF takes (server, uri) only — unregister by URI
    httpd_unregister_uri(server, "/wifi");
    return ESP_OK;
}

bool wifi_is_sta_mode()
{
    return wifi_sta_mode;
}

void init_wifi()
{
    // Initialize Wi‑Fi/NVS (wifi module will handle no-op if already initialized)
    esp_err_t werr = wifi_init_base();
    if (werr != ESP_OK) {
        ESP_LOGW(TAG, "wifi_init_base returned %s — attempting minimal netif/event init", esp_err_to_name(werr));
        // ensure TCP/IP stack + event loop exist so httpd can create sockets
        esp_err_t e = esp_netif_init();
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "esp_netif_init returned %s", esp_err_to_name(e));
        }
        e = esp_event_loop_create_default();
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "esp_event_loop_create_default returned %s", esp_err_to_name(e));
        }
        // Try initializing wifi again after ensuring netif/event loop are in place
        werr = wifi_init_base();
        if (werr != ESP_OK) {
            ESP_LOGW(TAG, "wifi_init_base retry failed: %s", esp_err_to_name(werr));
        }
    }

    // Check NVS for existing credentials and start appropriate mode (STA) or AP fallback
    nvs_handle_t nvs_handle;
    if (nvs_open("wifi_cfg", NVS_READONLY, &nvs_handle) == ESP_OK) {
        size_t required = 0;
        if (nvs_get_str(nvs_handle, "ssid", NULL, &required) == ESP_OK && required > 1) {
            char *ssid = (char*)malloc(required);
            if (ssid && nvs_get_str(nvs_handle, "ssid", ssid, &required) == ESP_OK) {
                size_t pass_len = 0; char *pass = NULL;
                if (nvs_get_str(nvs_handle, "password", NULL, &pass_len) == ESP_OK && pass_len > 1) {
                    pass = (char*)malloc(pass_len);
                    if (pass) nvs_get_str(nvs_handle, "password", pass, &pass_len);
                }
                ESP_LOGI(TAG, "Found stored Wi-Fi SSID '%s' in NVS - starting STA", ssid);
                wifi_sta_mode = true; // credentials found in NVS
                wifi_start_sta(ssid, pass);
                free(ssid); if (pass) free(pass);
                nvs_close(nvs_handle);
            } else {
                if (ssid) free(ssid);
                nvs_close(nvs_handle);
                ESP_LOGI(TAG, "NVS contains ssid key but failed to read value - starting AP fallback");
                wifi_sta_mode = false;
                wifi_start_ap();
            }
        } else {
            nvs_close(nvs_handle);
            ESP_LOGI(TAG, "No stored Wi‑Fi credentials, bringing up AP 'Programmer-20K'");
            wifi_sta_mode = false;
            wifi_start_ap();
        }
    } else {
        ESP_LOGI(TAG, "NVS open failed — starting AP fallback");
        wifi_sta_mode = false;
        wifi_start_ap();
    }
}