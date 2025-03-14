#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "Base64.h"
#include "ArduinoJson.h"
#include "time.h"
#include <HTTPClient.h>


// HiveMQ MQTT Credentials
const char* mqtt_server = "*******";
const int mqtt_port = 8883;
const char* mqtt_user = "groundsegment";
const char* mqtt_password = "*******";
const char* mqtt_topic_image = "satellite/image";
const char* mqtt_topic_depth = "satellite/depth";
const char* mqtt_topic_observation = "ground/observation";

// time variables
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;   
const int   daylightOffset_sec = 0;

// WiFi Credentials
const char *ssid = "*******";
const char *password = "*******";

// WiFi and MQTT Clients
WiFiClientSecure espClient;
PubSubClient client(espClient);

String imageJson = "";
String depthJson = "";
bool imageReceived = false;
bool depthReceived = false;
unsigned long startTime;
bool skip_publish = false;

struct ObservationData {
    String timestampImage;
    String timestampValidation;
    String locationImage;
    String imageBase64;
    String depthMatrix;
};


void setupWiFi() {
    Serial.print("Connecting to WiFi...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("Connected to WiFi!");
}

void setupTime() {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.println("Waiting for time sync...");
    delay(2000);
}
String getCurrentTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        skip_publish = true;
        return "N/A";
    }
    char timeString[30];
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeString);
}
// Callback function for incoming MQTT messages
void callback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    Serial.print("Message received on topic: ");
    Serial.println(topic);

    if (String(topic) == mqtt_topic_image) {
        if (!imageReceived) {  // Prevent overwriting before processing
            imageJson = message;
            imageReceived = true;
        }
    } else if (String(topic) == mqtt_topic_depth) {
        if (!depthReceived) {  // Prevent overwriting before processing
            depthJson = message;
            depthReceived = true;
        }
    }
} 

// Function to check if JSON is valid and complete
bool isJsonComplete(String jsonStr, String keys[], int keyCount) {
    StaticJsonDocument<4096> doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
        Serial.println("Invalid JSON format.");
        skip_publish = true;
        return false;
    }

    for (int i = 0; i < keyCount; i++) {
        if (!doc.containsKey(keys[i])) {
            Serial.print("Missing key: ");
            Serial.println(keys[i]);
            return false;
        }

        // Check if the value is "N/A" or an empty string
        String value = doc[keys[i]].as<String>();
        if (value == "N/A" || value.length() == 0) {
            Serial.print("Invalid value for key: ");
            Serial.println(keys[i]);
            Serial.println(value);
            skip_publish = true;
            return false;
        }
    }

    return true;
}

void waitForData() {
    unsigned long startWaitTime = millis();
    unsigned long lastLogTime = startWaitTime;
    unsigned long countdownStart = 0;
    bool countdownStarted = false;

    Serial.println("Waiting for image and depth data...");

    while (true) {
        client.loop();  // Keep the MQTT connection active

        // Log every 60 seconds if no data received yet
        if (!imageReceived && !depthReceived) {
            if (millis() - lastLogTime >= 60000) {
                Serial.println("Still waiting: No data received in the last 60 seconds.");
                lastLogTime = millis();  // Reset log timer
            }
            continue; // Keep waiting indefinitely
        }

        // If either image or depth is received, start the countdown
        if (!countdownStarted && (imageReceived || depthReceived)) {
            countdownStart = millis();
            countdownStarted = true;
            Serial.println("First data received. Starting 5s countdown for remaining data...");
        }

        // If both messages received, exit function
        if (imageReceived && depthReceived) {
            Serial.println("Both messages received.");
            return;
        }

        // If countdown started, check for timeout (20s)
        if (countdownStarted && millis() - countdownStart > 5000) {
            Serial.println("Error: Timeout waiting for remaining data.");
            skip_publish = true;
            return;
        }
    }
}

ObservationData processData() {
    String keysImage[] = {"timestamp", "location", "image"};
    String keysDepth[] = {"timestamp", "location", "depth"};

    if (!isJsonComplete(imageJson, keysImage, 3) || !isJsonComplete(depthJson, keysDepth, 3)) {
        Serial.println("Error: Incomplete JSON data.");
        skip_publish = true;
        return {"", "", "", "", ""};
    }

    StaticJsonDocument<4096> imgDoc, depthDoc;
    deserializeJson(imgDoc, imageJson);
    deserializeJson(depthDoc, depthJson);

    String timestampImage = imgDoc["timestamp"].as<String>();
    String locationImage = imgDoc["location"].as<String>();
    String imageBase64 = imgDoc["image"].as<String>();

    String timestampDepth = depthDoc["timestamp"].as<String>();
    String locationDepth = depthDoc["location"].as<String>();
    String depthMatrix = depthDoc["depth"].as<String>();

    if (timestampImage != timestampDepth || locationImage != locationDepth) {
        Serial.println("Error: Timestamp or location mismatch.");
        skip_publish = true;
        return {"", "", "", "", ""};
    }

    String timestampValidation = getCurrentTime();
    return {timestampImage, timestampValidation, locationImage, imageBase64, depthMatrix};
}

// Function to send JSON file via MQTT
void publishToMQTT(const char* mqtt_topic, String jsonPayload) {
    if (jsonPayload.length() > 0) {
        bool success = client.publish(mqtt_topic, jsonPayload.c_str());
        if (success) {
            Serial.println("JSON successfully published!");
        } else {
            Serial.println("Failed to publish JSON!");
        }
    }
}
// Function to build JSON file
String buildJson(String timestampObs, String timestampVal, String location, String base64Image, String depthMatrix) {

    StaticJsonDocument<4096> doc;
    doc["timestampObservation"] = timestampObs;
    doc["timestampValidation"] = timestampVal;
    doc["locationObservation"] = location;
    doc["image"] = base64Image;
    doc["depth"] = depthMatrix;

    String jsonString;
    serializeJson(doc, jsonString);
    return jsonString;
}
// Function to connect to MQTT
void reconnectMQTT() {
    while (!client.connected()) {
        Serial.print("Connecting to MQTT...");
        if (client.connect("senseBox MCU-S2 ESP32S2", mqtt_user, mqtt_password)) {
            Serial.println("Connected to MQTT Broker!");
        } else {
            Serial.print("Failed, retrying in 5 seconds...");
            delay(5000);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    setupWiFi();
    espClient.setInsecure();
    setupTime();

    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    reconnectMQTT();
    client.subscribe("satellite/image");
    client.subscribe("satellite/depth");
}

void loop() {
    if (!client.connected()) {
        reconnectMQTT();
    }
    client.loop();
    skip_publish = false;
    imageReceived = false;
    depthReceived = false;
    imageJson = "";
    depthJson = "";

    waitForData();
    
    ObservationData obsData = processData();
    if (obsData.timestampImage == "") {
        Serial.println("Skipping processing due to invalid data.");
        return;
    }

    String jsonPayload = buildJson(obsData.timestampImage, obsData.timestampValidation, obsData.locationImage, obsData.imageBase64, obsData.depthMatrix);
    Serial.println("Observation Payload: " + jsonPayload);
    Serial.print("Payload size: ");
    Serial.println(jsonPayload.length());
    
    if (!skip_publish) {
      publishToMQTT(mqtt_topic_observation, jsonPayload);
    }

    Serial.print("Free heap: ");
    Serial.println(ESP.getFreeHeap());
    Serial.println("---------------------");
}

