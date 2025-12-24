idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS "include"
                       EMBED_TXTFILES "index.html"
                       EMBED_FILES "bridge.bin"
                       REQUIRES esp_http_server nvs_flash esp_wifi esp_netif mdns json)