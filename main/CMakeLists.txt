idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS ""
                       EMBED_TXTFILES "index.html"
                       REQUIRES jtag esp_http_server nvs_flash esp_wifi esp_netif mdns json)
