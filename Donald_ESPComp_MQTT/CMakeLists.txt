idf_component_register(SRCS "Donald_MQTT.cpp"
						INCLUDE_DIRS "."
    					REQUIRES esp_event esp_wifi mqtt
    					PRIV_REQUIRES esp_netif esp_system esp_timer freertos log lwip nvs_flash
)
