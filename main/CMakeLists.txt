idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_dashboard_plus nvs_flash esp_wifi esp_event esp_netif esp_timer espressif__mdns
)
