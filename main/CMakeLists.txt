idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS "include"
                       EMBED_FILES "bridge.bin" "index.html" "favicon.svg"
                       REQUIRES esp_http_server nvs_flash esp_wifi esp_netif json esp_driver_rmt esp_driver_uart esp_driver_gpio esp_timer)