idf_component_register(SRCS "Donald_MQTT.cpp"
						INCLUDE_DIRS "."
						REQUIRES esp_event esp_timer esp_wifi mqtt nvs_flash)
