
/*
Donald Fraser 18 August 2021
Test project for Donald_MQTT library

Tests
Wifi connectivity
Publishing to an MQTT topic
Subscribing to and receiving commands on an MQTT topic
Over the Air software update

Topics:
commandsTopic = "sensors/temperature/commands"; The IoT device running this sketch, subscribes to this topic treats messages received on this topic as commands to do something
temperaturesTopic = "sensors/temperature"; The IoT device running this sketch posts temperature information read from any attached sensors to this topic
configTopic = "sensors/config"; The IoT device running this sketch, posts current configuration information to this topic. It does this if configuration is updated by a command
and also if a command to publish configuration inforamtion is received.

Example mosquitto command lines
// Example mosquitto command lines which can be issued from a client Windows PC, Raspberry Pi etc.
// Sets loop time of this sensor to 1 second: mosquitto_pub.exe -t sensors/temperature/commands -m loopTime=10000 -q 1  
// Tells the sensor to publish its current config settings on the "sensors/config" topic: mosquitto_pub.exe -t sensors/temperature/commands -m reportConfig -q 1
// Tells the sensor to reset its config setting to their defaults: mosquitto_pub.exe -t sensors/temperature/commands -m resetToDefaults -q 1
// Subscribes to temperature reporting from this sensor: mosquitto_sub.exe -t sensors/temperature -q 1
// Subscribes to hello world reporting from this sensor: mosquitto_sub.exe -t outTopic -q 1
// Subscribes to configuration reporting from this sensor: mosquitto_sub.exe -t sensors/config -q 1
*/

#define MQTT_MAX_PACKET_SIZE 1024
#include <string>
#include <Donald_MQTT.h>
#include "esp_timer.h"
#include "esp_log.h"

// OTA information for automatically updating devices which use this sketch from a web server
const int version = 1; // Version number for this firmware
string OTAServerBase = "http://192.168.1.130/ESPImages/";// URI for the server where the firmware images will be published - on a NAS for example

long lastMsg = 0;
char msg[50];
int value = 0;

string mQTTClientID="MQTTTest";
DonaldWIFI* donaldWiFi;
void setup();
void onTimer(void* parameter);
void onNetworkChanged(void);

// ************************ Topic ****************************
string temperaturesTopic = "sensors/temperature/" + mQTTClientID;
// ************************ Topic ****************************

const int loopTime = 10 * 1000 * 1000; // esp32 timer has microsecond unit
static const char *TAG = "mqtt_test";

extern "C" void app_main(void)
{
	setup();
}

// Sends temperature to MQTT
void publishTemperatures()
{
    string mQTTPayload = "{\"device\": \"" + mQTTClientID + "\", \"sensors\": [1.00,1.00,1.00]}";
    bool result = donaldWiFi->MQTTPublish(temperaturesTopic, mQTTPayload );
    if(result == false)
    {
      ESP_LOGI(TAG, "MQTT publish on temperature topic failed");
    }
}

void OnMQTTCommand(string command)
{
  ESP_LOGI(TAG, "Received MQTT command");
}

void onNetworkChanged()
{
    ESP_LOGI(TAG, "Network Status changed");
}

void LogMemory(void)
{
	size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
	size_t largestFreeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
	ESP_LOGI(TAG, "Memory Free: %u Largest Block: %u", freeHeap, largestFreeBlock);
}

void setup() 
{
  LogMemory();
  donaldWiFi = new DonaldWIFI(mQTTClientID);
  LogMemory();

  donaldWiFi->OnNetworkChange(onNetworkChanged);
  donaldWiFi->SetOTAParameters(OTAServerBase, mQTTClientID, version);
  donaldWiFi->OnMQTTCommand(OnMQTTCommand);
  esp_timer_handle_t timer = nullptr;
  donaldWiFi->CreateTimer(timer, onTimer, loopTime, TAG);
}

void onTimer(void* parameter) 
{
	publishTemperatures();
	LogMemory();
}
