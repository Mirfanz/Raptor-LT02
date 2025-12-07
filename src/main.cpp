#include <Arduino.h>
#include <WiFi.h>
#include <mqtt_client.h>
#include <Wire.h>
#include <NewPing.h>
#include <DHT.h>
#include <math.h>
#include <limits.h>
#include "secrets.h"

#define PIN_EMERGENCY_BUTTON 26
#define PIN_LOAD_BUTTON 25
#define PIN_LED 12
#define PIN_TRIGGER 14
#define PIN_ECHO 27
#define LOAD_PRESS_TIME 100
#define PIN_DHT 13

struct lineData {
  bool status;  
  bool bucket;
  bool emergency;
};

lineData line = {false, false, false};

bool isLoaded = false;

esp_mqtt_client_handle_t mqtt;

NewPing sonar(PIN_TRIGGER, PIN_ECHO, 30);

wl_status_t wifiStatus;

DHT dht(PIN_DHT, DHT22);
int lastTemperature = INT_MIN;
int lastHumidity = INT_MIN;

void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
void handle_mqtt_message(esp_mqtt_event_handle_t message);
void handle_wifi_loop();
void handle_dht();
void handle_sonar();
void handle_find_btn();
void handle_load_btn();

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_EMERGENCY_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LOAD_BUTTON, INPUT_PULLUP);

  dht.begin();

  WiFi.setHostname("Raptor LT02 Line2");
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  Serial.println("Connecting to WiFi...");
  
  esp_mqtt_client_config_t mqtt_cfg = {};
  mqtt_cfg.uri = MQTT_URI;
  mqtt_cfg.lwt_topic = "raptorfx02/" LINE_CODE "/status";
  mqtt_cfg.lwt_msg = "offline";
  mqtt_cfg.lwt_qos = 1;
  mqtt_cfg.lwt_retain = 1;
  mqtt_cfg.keepalive = 10;
  
  mqtt = esp_mqtt_client_init(&mqtt_cfg);
  esp_mqtt_client_register_event(mqtt, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
  esp_mqtt_client_start(mqtt);
}

void loop() {
  handle_wifi_loop();
  handle_sonar();
  handle_dht();
  handle_find_btn();
  handle_load_btn();
}

void handle_sonar(){
  static unsigned long lastSonarCheck = 0;
  if (millis() - lastSonarCheck < 500) return;

  lastSonarCheck = millis();
  unsigned int distance = sonar.ping_cm();
  bool bucketNow = !distance;
  // JIka bucket berubah status
  if(bucketNow != line.bucket) {
    line.bucket = bucketNow;
    isLoaded = false;
    Serial.print("Bucket status changed: ");
    Serial.println(line.bucket);
    if (wifiStatus == WL_CONNECTED) {
      esp_mqtt_client_publish(mqtt, "raptorfx02/" LINE_CODE "/bucket", line.bucket ? "1" : "0", 0, 1, 1);
    }
  }
  
}

void handle_dht(){
  static unsigned long lastDHTCheck = 0;
  if(millis() - lastDHTCheck < 3000) return;
  lastDHTCheck = millis();
  // Cek sensor suhu
  float temperature_f = dht.readTemperature();
  if (isnan(temperature_f)) {
    Serial.println("DHT: Failed to read temperature");
  } else {
    int temperature = (int)round(temperature_f);
    if (temperature != lastTemperature) {
      lastTemperature = temperature;
      Serial.print("Temperature: ");
      Serial.println(temperature);
      if (wifiStatus == WL_CONNECTED) {
        char tempStr[8];
        snprintf(tempStr, sizeof(tempStr), "%d", temperature);
        esp_mqtt_client_publish(mqtt, "raptorfx02/" LINE_CODE "/temperature", tempStr, 0, 1, 1);
      }
    }
  }

  // Cek sensor kelembaban
  float humidity_f = dht.readHumidity();
  if (isnan(humidity_f)) {
    Serial.println("DHT: Failed to read humidity");
  } else {
    int humidity = (int)round(humidity_f);
    if (humidity != lastHumidity) {
      lastHumidity = humidity;
      Serial.print("Humidity: ");
      Serial.println(humidity);
      if (wifiStatus == WL_CONNECTED) {
        char humStr[8];
        snprintf(humStr, sizeof(humStr), "%d", humidity);
        esp_mqtt_client_publish(mqtt, "raptorfx02/" LINE_CODE "/humidity", humStr, 0, 1, 1);
      }
    }
  }
}

void handle_find_btn(){
  bool emergencyState = !digitalRead(PIN_EMERGENCY_BUTTON);
  if (emergencyState != line.emergency) {
    line.emergency = emergencyState;
    Serial.print("Emergency status changed: ");
    Serial.println(line.emergency);
    digitalWrite(PIN_LED, line.emergency ? HIGH : LOW);
    if (wifiStatus == WL_CONNECTED) {
      esp_mqtt_client_publish(mqtt, "raptorfx02/" LINE_CODE "/finding", line.emergency ? "1" : "0", 0, 1, 1);
    }
  }
}

void handle_load_btn(){
  if(line.bucket || isLoaded) return;
  static unsigned long loadButtonPressedAt = 0;
  bool loadState = !digitalRead(PIN_LOAD_BUTTON);
  if (loadState) {
    if (!loadButtonPressedAt) {
      loadButtonPressedAt = millis();
    } else if (millis() - loadButtonPressedAt > LOAD_PRESS_TIME) {
      isLoaded = true;
      Serial.println("Bucket Loaded");
      if (wifiStatus == WL_CONNECTED) {
        esp_mqtt_client_publish(mqtt, "raptorfx02/" LINE_CODE "/bucket", "1", 0, 1, 1);
      }
    }
    } else {
      loadButtonPressedAt = 0;
  }
}

void handle_mqtt_message(esp_mqtt_event_handle_t message){
  Serial.print("MQTT data received: ");
  Serial.write(message->topic, message->topic_len);
  Serial.println();

  Serial.print("Line 2 - Status: "); Serial.print(line.status); Serial.print(", Bucket: "); Serial.print(line.bucket); Serial.print(", Emergency: "); Serial.println(line.emergency);

  Serial.println();
  Serial.println();
}

void handle_wifi_loop() {
  wifiStatus = WiFi.status();
  static wl_status_t lastStatus = WL_IDLE_STATUS;
  if (wifiStatus != lastStatus) {
    lastStatus = wifiStatus;
    switch (wifiStatus) {
      case WL_NO_SSID_AVAIL:
        Serial.println("WiFi: No SSID available");
        break;
      case WL_CONNECTED:
        Serial.println("WiFi: Connected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        break;
      case WL_CONNECT_FAILED:
        Serial.println("WiFi: Connection failed");
        break;
      case WL_CONNECTION_LOST:
        Serial.println("WiFi: Connection lost");
        break;
      case WL_DISCONNECTED:
        Serial.println("WiFi: Disconnected");
        break;
      default:
        Serial.print("WiFi: Status changed to ");
        Serial.println(wifiStatus);
        break;
    }
  }
}

void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
      Serial.println("MQTT connected");
      esp_mqtt_client_publish(mqtt, "raptorfx02/" LINE_CODE "/status", "online", 0, 1, 1);
      esp_mqtt_client_publish(mqtt, "raptorfx02/" LINE_CODE "/bucket", isLoaded ? "1" : line.bucket ? "1" : "0", 0, 1, 1);
      esp_mqtt_client_publish(mqtt, "raptorfx02/" LINE_CODE "/finding", line.emergency ? "1" : "0", 0, 1, 1);
      // esp_mqtt_client_subscribe(mqtt, "raptorfx02/status", 0);
      break;
    case MQTT_EVENT_DISCONNECTED:
      Serial.println("MQTT disconnected");
      break;
    case MQTT_EVENT_SUBSCRIBED:
      Serial.println("MQTT subscribed");
      break;
    case MQTT_EVENT_UNSUBSCRIBED:
      Serial.println("MQTT unsubscribed");
      break;
    case MQTT_EVENT_PUBLISHED:
      Serial.println("MQTT published");
      break;
    case MQTT_EVENT_DATA:
      handle_mqtt_message(event);
      break;
    case MQTT_EVENT_ERROR:
      Serial.println("MQTT error");
      break;
    default:
      Serial.printf("Other MQTT event id: %d\n", event->event_id);
      break;
    }
}

