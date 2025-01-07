#ifndef DONALD_WIFI_H
  #define DONALD_WIFI_H
#include <string>
#include <vector>
#include <esp_event.h>
#include "esp_wifi.h"
#include "esp_timer.h"
#include "mqtt_client.h"

using namespace std;

typedef  void (*commandFunction)(string command);
typedef  void (*debugMessageFunction)(string debugMessage);

class DonaldWIFI
{
  public:

    DonaldWIFI(string clientID);
	
    void Begin();
    bool MQTTPublish(string topic, string payload);
    void SetOTAParameters(string serverBaseUrl, string componentName, int currentVersion = 1, bool autoUpdate = true);
    void CheckForUpdates();
    void PublishHeartbeat();
    void OnMQTTCommand(commandFunction);
    int32_t GetRSSI(void);
    bool GetMQTTConnected(void);
	void CreateTimer(esp_timer_handle_t timer, esp_timer_cb_t callback, uint64_t period, const char* debugName);
	
  private:
  
    static void processCommand(string commandstring);
	static void mQTTCallback(char* topic, unsigned int topicLength, char* payload, unsigned int payLoadLength);
	static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
	static void log_error_if_nonzero(const char *message, int error_code);
	static void on_sta_got_ip(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
	static void on_wifi_disconnect(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
	static void on_wifi_connect(void *esp_netif, esp_event_base_t event_base, int32_t event_id, void *event_data);
	static bool is_our_netif(const char *prefix, esp_netif_t *netif);
	static void reportWifiStatusTimerTask(void* arg);
	static void mQTTReconnectTimerTask(void* arg);
	static std::vector <commandFunction> MQTTCommands;
    static const string commandsTopicBase;
    static string commandsTopic;
	inline static bool cachedMQTTConnected = false;
	inline static string IPAddressAsString = "";
	

	void SetupMQTT(void);
	void mQTTReconnect(void);
    void ReportConfig(void);
	void ReportWifiStatus(void);
    void ResetToDefaults(void);
    void SetLoopTime(string loopTimeAsstring);
    string formatMessage();
    string GetStatus();
    string getWifiJSON();
    void Subscribe(string topic);
    string MACTostring(uint8_t MACArray[6]);
	void wifi_start(void);
	void wifi_stop(void);
	esp_err_t wifi_connect(void);
	esp_err_t wifi_sta_do_connect(wifi_config_t wifi_config, bool wait);

    const string wifiTopicBase = "wifi";
    esp_mqtt_client_handle_t mqttClient;
    int32_t cachedRSSI;
    const string ssid = "BT-CKF95Z"; 
    const char* password = "Tx7cKXMTknmdPr";
    const char* mQTTServer = "pimqttserver.local";
    const int mQTTPort = 1883;
    string MAC;
	esp_timer_handle_t reportWifiStatusTimer;
	esp_timer_handle_t mQTTReconnectTimer;
	
    // OTA Parameters
    string serverBaseUrl;
    string componentName;
    int currentVersion;
    
    // ************************ Remote Parameters ****************************
    // These variables can be modified by publishing an MQTT message to the commands topic from a remote device such as a PC or a Raspberry Pi
    // Commands:  
    //  resetToDefaults
    int64_t WifiStrengthInterval; // the time in microseconds to poll the wifistrength for
    int64_t MQTTConnectPollInterval; // the time in microseconds to poll MQTT connectivity
	bool autoUpdate;
    string mQTTClientID;
    // ************************ End Remote Parameters ****************************
};
#endif