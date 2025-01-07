#define MQTT_MAX_PACKET_SIZE 1024

#include "Donald_MQTT.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"
static const char *TAG = "donald_mqtt";
const string DonaldWIFI::commandsTopicBase = "sensors/commands";
string DonaldWIFI::commandsTopic = "sensors/commands";
std::vector <commandFunction> DonaldWIFI::MQTTCommands = * new std::vector <commandFunction>();

#define CONFIG_EXAMPLE_WIFI_CONN_MAX_RETRY 6
// ************************ Topic ****************************
// See https://www.hivemq.com/blog/mqtt-essentials-part-5-mqtt-topics-best-practices/ on recommendations for topic names. Use specific topics not
// general ones.

// Public method which needs to be called from a client application to initiate wifi and MQTT
// connections. A client application will typically call this from its Arduino setup()
// function


static int s_retry_num = 0;
static esp_netif_t *s_example_sta_netif = NULL;
static SemaphoreHandle_t s_semph_get_ip_addrs = NULL;

#define EXAMPLE_WIFI_SCAN_METHOD WIFI_FAST_SCAN
#define EXAMPLE_WIFI_CONNECT_AP_SORT_METHOD WIFI_CONNECT_AP_BY_SIGNAL
#define EXAMPLE_NETIF_DESC_STA "example_netif_sta"
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#define EXAMPLE_ESP_WIFI_SSID      "BT-CKF95Z"
#define EXAMPLE_ESP_WIFI_PASS      "Tx7cKXMTknmdPr"

DonaldWIFI::DonaldWIFI(string clientID) 
            :  cachedRSSI(-100), WifiStrengthInterval(30000000), MQTTConnectPollInterval(10000000), autoUpdate(false)
{
  this->mQTTClientID = clientID;

  commandsTopic = commandsTopicBase + "/" + mQTTClientID;
  
  CreateTimer(reportWifiStatusTimer, reportWifiStatusTimerTask, WifiStrengthInterval, "Donald_MQTT reportWifiStatusTimer"); 
  CreateTimer(reportWifiStatusTimer, mQTTReconnectTimerTask, MQTTConnectPollInterval, "Donald_MQTT mQTTReconnectTimer"); 
  
	esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
	esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
	esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
	esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
	esp_log_level_set("transport", ESP_LOG_VERBOSE);
	esp_log_level_set("outbox", ESP_LOG_VERBOSE);

  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
	
  Begin();
  
  uint8_t MACArray[6];
  esp_wifi_get_mac(WIFI_IF_STA, MACArray);
  MAC = MACTostring(MACArray);
  SetupMQTT();
}

void DonaldWIFI::CreateTimer(esp_timer_handle_t timer, esp_timer_cb_t callback, uint64_t period, const char* debugName)
{
  esp_timer_create_args_t timerArgs = {};
  timerArgs.callback = callback;
  timerArgs.arg = this;
  timerArgs.dispatch_method = ESP_TIMER_TASK;
  timerArgs.name = debugName;
  timerArgs.skip_unhandled_events = true;
  ESP_ERROR_CHECK(esp_timer_create(&timerArgs, &timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(timer, period));
}

// Returns all parameters, settings, state for debug purposes
string DonaldWIFI::GetStatus()
{
	string space = " ";
	string retVal = serverBaseUrl + space + componentName + space + to_string(currentVersion) + space + mQTTClientID;
	return retVal;
}
  
void DonaldWIFI::SetOTAParameters(string serverBaseUrl, string componentName, int currentVersion, bool autoUpdate)
{
  this->serverBaseUrl = serverBaseUrl;
  this->componentName = componentName;
  this->currentVersion = currentVersion;
  this->autoUpdate = autoUpdate;
}

void DonaldWIFI::CheckForUpdates() 
{
//   string componentBaseUrl = serverBaseUrl + componentName;
//   string imageURL = componentBaseUrl + ".bin";
  
//   DebugMessage( "Preparing to update with image: " +  imageURL);

//   //string OTATopic = wifiTopicBase + "/OTA/" + mQTTClientID;
//   //MQTTPublish(OTATopic,  imageURL);

// #ifdef ESP8266
//       t_httpUpdate_return ret = ESPhttpUpdate.update( wifiClient, imageURL );
// #else
//       t_httpUpdate_return ret = httpUpdate.update(wifiClient, imageURL );
// 	#endif

//   switch(ret) {
//     case HTTP_UPDATE_FAILED:
//       #ifdef ESP8266
//         DebugMessage("HTTP_UPDATE_FAILED Error " + (string)ESPhttpUpdate.getLastError() + " " + ESPhttpUpdate.getLastErrorstring());
//       #else
//         DebugMessage("HTTP_UPDATE_FAILED Error " + (string)httpUpdate.getLastError() + " " + httpUpdate.getLastErrorstring());
//       #endif
//       break;
//     case HTTP_UPDATE_NO_UPDATES:
//       DebugMessage("HTTP_UPDATE_NO_UPDATES");
//       break;
//     case HTTP_UPDATE_OK:
//       DebugMessage("HTTP_UPDATE_OK");
//       break;
//   }
}

string DonaldWIFI::MACTostring(uint8_t MACArray[6])
{
  string s;
  for (uint8_t i = 0; i < 6; ++i)
  {
    char buf[3];
    sprintf(buf, "%02X", MACArray[i]); // J-M-L: slight modification, added the 0 in the format for padding 
    s += buf;
    if (i < 5) s += ':';
  }
  return s;
}

void DonaldWIFI::reportWifiStatusTimerTask(void* arg)
{
	((DonaldWIFI*)arg)->ReportWifiStatus();
}

void DonaldWIFI::ReportWifiStatus()
{
	if(autoUpdate)
    {
      PublishHeartbeat();
      CheckForUpdates();
    }
}

void DonaldWIFI::mQTTReconnectTimerTask(void* arg)
{
	((DonaldWIFI*)arg)->mQTTReconnect();
}

// ********************* WIFI RELATED **********************************
void DonaldWIFI::Begin()
{
    ESP_ERROR_CHECK(wifi_connect());
}

/**
 * @brief Checks the netif description if it contains specified prefix.
 * All netifs created withing common connect component are prefixed with the module TAG,
 * so it returns true if the specified netif is owned by this module
 */
bool DonaldWIFI::is_our_netif(const char *prefix, esp_netif_t *netif)
{
    return strncmp(prefix, esp_netif_get_desc(netif), strlen(prefix) - 1) == 0;
}

void DonaldWIFI::on_wifi_disconnect(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    s_retry_num++;
    if (s_retry_num > CONFIG_EXAMPLE_WIFI_CONN_MAX_RETRY) {
        ESP_LOGI(TAG, "WiFi Connect failed %d times, stop reconnect.", s_retry_num);
        /* let example_wifi_sta_do_connect() return */
        if (s_semph_get_ip_addrs) {
            xSemaphoreGive(s_semph_get_ip_addrs);
        }

        return;
    }
    ESP_LOGI(TAG, "Wi-Fi disconnected, trying to reconnect...");
    esp_err_t err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
        return;
    }
    ESP_ERROR_CHECK(err);
}

void DonaldWIFI::on_wifi_connect(void *esp_netif, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
}

// Callback which is called when we are assigned an IP address or the IP address changes
void DonaldWIFI::on_sta_got_ip(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{

    s_retry_num = 0;
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    if (!is_our_netif(EXAMPLE_NETIF_DESC_STA, event->esp_netif)) {
        return;
    }
    ESP_LOGI(TAG, "Received IPv4 event: Interface \"%s\" address: " IPSTR, esp_netif_get_desc(event->esp_netif), IP2STR(&event->ip_info.ip));

	// Store Ip address for future MQTT publications:
	char ipaddrBuffer[128];
	sprintf(ipaddrBuffer, IPSTR, IP2STR(&event->ip_info.ip));
	IPAddressAsString = ipaddrBuffer;
	
    if (s_semph_get_ip_addrs) {
        xSemaphoreGive(s_semph_get_ip_addrs);
    } else {
        ESP_LOGI(TAG, "- IPv4 address: " IPSTR ",", IP2STR(&event->ip_info.ip));
    }
}

esp_err_t DonaldWIFI::wifi_connect(void)
{
	// C:\Espressif\frameworks\esp-idf\components\esp_wifi\include\esp_wifi_types.h
    ESP_LOGI(TAG, "Start example_connect.");
    wifi_start();
	
	wifi_sta_config_t sta_config;

	memcpy(sta_config.ssid, EXAMPLE_ESP_WIFI_SSID, sizeof(EXAMPLE_ESP_WIFI_SSID));
	memcpy(sta_config.password, EXAMPLE_ESP_WIFI_PASS, sizeof(EXAMPLE_ESP_WIFI_PASS));
	sta_config.scan_method = EXAMPLE_WIFI_SCAN_METHOD;
	sta_config.sort_method = EXAMPLE_WIFI_CONNECT_AP_SORT_METHOD;
	sta_config.threshold.rssi = 80;
	sta_config.threshold.authmode = EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD;
	
    wifi_config_t wifi_config = {
        .sta = sta_config,
    };

    return wifi_sta_do_connect(wifi_config, true);
}

void DonaldWIFI::wifi_start(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    // Warning: the interface desc is used in tests to capture actual connection details (IP, gw, mask)
    esp_netif_config.if_desc = EXAMPLE_NETIF_DESC_STA;
    esp_netif_config.route_prio = 128;
    s_example_sta_netif = esp_netif_create_wifi(WIFI_IF_STA, &esp_netif_config);
    esp_wifi_set_default_wifi_sta_handlers();

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void DonaldWIFI::wifi_stop(void)
{
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        return;
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_wifi_deinit());
    ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(s_example_sta_netif));
    esp_netif_destroy(s_example_sta_netif);
    s_example_sta_netif = NULL;
}

esp_err_t DonaldWIFI::wifi_sta_do_connect(wifi_config_t wifi_config, bool wait)
{
    if (wait) {
        s_semph_get_ip_addrs = xSemaphoreCreateBinary();
        if (s_semph_get_ip_addrs == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    s_retry_num = 0;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_sta_got_ip, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &on_wifi_connect, s_example_sta_netif));


    ESP_LOGI(TAG, "Connecting to %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connect failed! ret:%x", ret);
        return ret;
    }
    if (wait) {
        ESP_LOGI(TAG, "Waiting for IP(s)");

        xSemaphoreTake(s_semph_get_ip_addrs, portMAX_DELAY);

        if (s_retry_num > CONFIG_EXAMPLE_WIFI_CONN_MAX_RETRY) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

void DonaldWIFI::log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

// ********************* MQTT RELATED **********************************
 void DonaldWIFI::SetupMQTT(void)
 {
  // From C:\Espressif\frameworks\esp-idf\components\mqtt\esp-mqtt\include\mqtt_client.h:
  esp_mqtt_client_config_t mqtt_cfg;
  memset(&mqtt_cfg, 0, sizeof(esp_mqtt_client_config_t));
  mqtt_cfg.broker.address.hostname = mQTTServer;
  mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
  mqtt_cfg.broker.address.port = mQTTPort;
  mqtt_cfg.broker.verification.use_global_ca_store = false;
  mqtt_cfg.session.last_will.msg_len = 1024;
  mqtt_cfg.buffer.size = 1024;
  mqtt_cfg.buffer.out_size = 1024;
  mqttClient = esp_mqtt_client_init(&mqtt_cfg);
  
  if(NULL != mqttClient)
  {
	  /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
	  esp_mqtt_client_register_event(mqttClient, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
	  esp_mqtt_client_start(mqttClient);
	  Subscribe(commandsTopic);
  }
  else
  {
	 ESP_LOGE(TAG, "Failed to initialize mqtt, null handle returned by esp_mqtt_client_init");
  }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
void DonaldWIFI::mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
		cachedMQTTConnected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
		cachedMQTTConnected = false;
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
		mQTTCallback(event->topic, event->topic_len, event->data, event->data_len);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

// Callback to receive MQTT notification from the MQTT broker we a connected to
// Current implementation looks for notification on a command topic and  forwards
// those notification to the ProcessCommand method for action:
void DonaldWIFI::mQTTCallback(char* topic, unsigned int topicLength, char* payload, unsigned int payLoadLength)
{
  string topicAsstring(topic, topicLength);

  if(commandsTopic == topicAsstring)
  {
	string payloadAsstring(payload, payLoadLength);
	ESP_LOGI(TAG, "DonaldWIFI::mQTTCallback: received command. Payload: %s", payloadAsstring.c_str() );
	processCommand(payloadAsstring);
  }
  else
  {
	ESP_LOGI(TAG, "DonaldWIFI::mQTTCallback: unrecognised topic %s does not match: %s", topicAsstring.c_str(), commandsTopic.c_str());
  }
}

// Commands can be single strings or can be strings followed by a single argument, separated by the '=' character
// for example sleepAfterEnd=true / false
void DonaldWIFI::processCommand(string commandstring)
{
  string command;
  string param;
  int indexOfSeparator = commandstring.find_first_of('=');
  if(indexOfSeparator !=-1) // -1 means that the command does not contain '='
  {
    command = commandstring.substr(0, indexOfSeparator);
    param = commandstring.substr(indexOfSeparator+1);
  }
  else
  {
    command = commandstring;
  }

  if(command.length() > 0)
  {
    // Call each registered command handler
    for (unsigned int i = 0; i < MQTTCommands.size(); i++)
    {
      commandFunction commandFunc = MQTTCommands[i];
      commandFunc(command);
    }
  }
}

// Registers a command handler to be called every time a MQTT message is received
// on the command topic. Clients of this library call this function and provide
// a pointer to a callback function, which is to be called to process commands
void DonaldWIFI::OnMQTTCommand(commandFunction commandCallback)
{
  MQTTCommands.push_back(commandCallback);
  ESP_LOGI(TAG, "Added command handler, total command handlers = %u", MQTTCommands.size());
}

// Attempts to reconnect to the MQTT server if it is not connected already.
// Enure we do not introduce delays into the main device Loop()
void DonaldWIFI::mQTTReconnect()
{
    if(!cachedMQTTConnected)
	{
		ESP_LOGI(TAG, "mQTTReconnect attempting to reconnect");
		// Attempt to connect
		esp_err_t result = esp_mqtt_client_reconnect(mqttClient);
		if (ESP_OK == result)
		{
		  // ... and resubscribe
		  Subscribe(commandsTopic);
		} 
		else 
		{
		  ESP_LOGE(TAG, "mQTTReconnect failed to reconnect");
		} 
	}
}

void DonaldWIFI::Subscribe(string topic)
{
  int msg_id = esp_mqtt_client_subscribe(mqttClient, topic.c_str(), 0);
  ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
}

// Publishes the given payload on the given MQTT topic. 
bool DonaldWIFI::MQTTPublish(string topic, string payload)
{
    bool result = false;
	ESP_LOGI(TAG, "MQTTPublish topic: %s Payload: %s", topic.c_str(), payload.c_str());

    if (!cachedMQTTConnected)
    {
       mQTTReconnect();
    }

    if (cachedMQTTConnected)
    {
	  int qos = 0;
	  int retain = false;
	  int pubResult = esp_mqtt_client_publish(mqttClient, topic.c_str(), payload.c_str(), payload.length(), qos, retain);

      if(pubResult > -1)
      {
		ESP_LOGI(TAG, "MQTT publish succeeded, topic: %s Payload: %s", topic.c_str(), payload.c_str());
		result = true;
      }
      else
      {
		ESP_LOGE(TAG, "MQTT publish failed, topic: %s Payload: %s", topic.c_str(), payload.c_str());
      }
    }
    return result;
}

// Returns a json document containing device location information
// Needs to include the time as retrieved from the satellite network so that 
// fixes from different gps devices can be correlated against each other
// for greater accuracy. 
// {"device": "BubblesRoomTemp","MAC": "EC:FA:BC:58:A3:99","ip": "192.168.1.166","BSSID": "04:09:86:22:E0:F9","rssi": -78,"days": 1,"hours": 14, "minutes": "59"}
string DonaldWIFI::getWifiJSON()
{
  const int64_t minuteDivider = 1000 * 1000 * 60;
  const int64_t hourDivider = minuteDivider * 60;
  const int64_t dayDivider = hourDivider * 24;
  int64_t now = esp_timer_get_time();
  
  // Get the BSSID of the wifi access point we are connected to
  wifi_ap_record_t info;
  char bssidChars[18] = { 0 };
  if(!esp_wifi_sta_get_ap_info(&info)) {
      sprintf(bssidChars, "%02X:%02X:%02X:%02X:%02X:%02X", info.bssid[0], info.bssid[1], info.bssid[2], info.bssid[3], info.bssid[4], info.bssid[5]);
  }

  string retVal = "{";
  retVal = retVal + "\"device\": \"" + mQTTClientID + "\"";
  retVal = retVal + ",\"MAC\": \"" + MAC + "\"";
  retVal = retVal + ",\"ip\": \"" + IPAddressAsString + "\"";
  retVal = retVal + ",\"BSSID\": \"" + bssidChars + "\"";
  retVal = retVal + ",\"rssi\": " + to_string(info.rssi);
  retVal = retVal + ",\"days\": " + to_string(now / dayDivider);
  retVal = retVal + ",\"hours\": " + to_string((now / hourDivider) % 24);
  retVal = retVal + ",\"minutes\": " + to_string((now / minuteDivider) % 60);
  retVal = retVal + "}";
  return retVal;
}

bool DonaldWIFI::GetMQTTConnected(void)
{
  return cachedMQTTConnected;
}

// Publishes heartbeat information to MQTT
// Information includes wifi strength info
void DonaldWIFI::PublishHeartbeat()
{
    string wifiPayload = getWifiJSON();
    string wifiTopic = wifiTopicBase + "/" + mQTTClientID;
    MQTTPublish(wifiTopic,  wifiPayload);
}

