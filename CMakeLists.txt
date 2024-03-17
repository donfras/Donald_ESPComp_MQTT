idf_component_register(SRCS "Donald_MQTT.cpp"
						INCLUDE_DIRS "."
						REQUIRES esp_event esp_wifi mqtt nvs_flash)
