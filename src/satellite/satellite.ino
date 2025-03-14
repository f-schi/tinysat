#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "esp_camera.h"
#include "Base64.h"
#include "ArduinoJson.h"
#include "time.h"
#include <HTTPClient.h>
#include <vl53l8cx.h>


// vl53l8cx Setup
#define DEV_I2C Wire
#define SerialPort Serial
#define LPN_PIN A3
#define PWREN_PIN 11
VL53L8CX vl53l8cx(&DEV_I2C, LPN_PIN);

// HiveMQ MQTT Credentials
const char* mqtt_server = "********";
const int mqtt_port = 8883;
const char* mqtt_user = "satellite";
const char* mqtt_password = "********";
const char* mqtt_topic_image = "satellite/image";
const char* mqtt_topic_depth = "satellite/depth";

// time variables
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;  
const int   daylightOffset_sec = 0;

// WiFi Credentials
const char *ssid = "********";
const char *password = "********";


// WiFi and MQTT Clients
WiFiClientSecure espClient;
PubSubClient client(espClient);

// Camera Pin Configuration
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      10
#define SIOD_GPIO_NUM      40
#define SIOC_GPIO_NUM      39
#define Y9_GPIO_NUM        48
#define Y8_GPIO_NUM        11
#define Y7_GPIO_NUM        12
#define Y6_GPIO_NUM        14
#define Y5_GPIO_NUM        16
#define Y4_GPIO_NUM        18
#define Y3_GPIO_NUM        17
#define Y2_GPIO_NUM        15
#define VSYNC_GPIO_NUM     38
#define HREF_GPIO_NUM      47
#define PCLK_GPIO_NUM      13

// Function to connect to WiFi
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

String getLocation() {
    HTTPClient http;
    http.begin("http://ip-api.com/json");  // Use HTTP instead of HTTPS

    int httpResponseCode = http.GET();
    if (httpResponseCode != 200) {
        Serial.print("Error in HTTP request, code: ");
        Serial.println(httpResponseCode);
        http.end();
        return "N/A,N/A";  // Return fallback value
    }

    String response = http.getString();
    http.end();

    // Parse JSON response
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        Serial.println("Failed to parse JSON");
        return "N/A,N/A";  // Return fallback value
    }

    float latitude = doc["lat"];
    float longitude = doc["lon"];
    
    return String(latitude, 6) + "," + String(longitude, 6);  // Single string format
}

String getCurrentTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return "N/A";
    }
    char timeString[30];
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeString);
}

// Function to connect to MQTT
void reconnectMQTT() {
    while (!client.connected()) {
        Serial.print("Connecting to MQTT...");
        if (client.connect("XIAO_ESP32S3", mqtt_user, mqtt_password)) {
            Serial.println("Connected to MQTT Broker!");
        } else {
            Serial.print("Failed, retrying in 5 seconds...");
            delay(5000);
        }
    }
}


// Initialize Camera
void setupCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_QQVGA;
    config.jpeg_quality = 10;
    config.fb_count = 1;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x", err);
        return;
    } else {
        Serial.println("Camera initialized.");
    }
}

String getDepthMatrix() {
    VL53L8CX_ResultsData Result;
    uint8_t NewDataReady = 0;
    uint8_t status;
    String depthMatrixStr = "";

    // Check if new data is ready
    status = vl53l8cx.check_data_ready(&NewDataReady);

    if ((!status) && (NewDataReady != 0)) {
        vl53l8cx.get_ranging_data(&Result);

        uint8_t zones_per_line;
        uint8_t number_of_zones = VL53L8CX_RESOLUTION_8X8;
        zones_per_line = (number_of_zones == 16) ? 4 : 8;

        for (uint8_t j = 0; j < number_of_zones; j += zones_per_line) {
            for (int8_t k = (zones_per_line - 1); k >= 0; k--) {
                int index = VL53L8CX_NB_TARGET_PER_ZONE * (j + k);
                long distance = (long)(&Result)->distance_mm[index];

                // Add distance value to the string with commas separating the values
                depthMatrixStr += String(distance);
                if (k != 0) {  // If it's not the last element, add a comma
                    depthMatrixStr += ",";
                }
            }
            depthMatrixStr += "\n"; // Add newline after each row
        }
    }
    return depthMatrixStr;
}

void setupToF() {
  // Initialize the VL53L8CX sensor
  vl53l8cx.begin();
  vl53l8cx.init();
  // Set the ranging frequency and resolution
  vl53l8cx.set_ranging_frequency_hz(30); // Sets the ranging frequency to 30Hz
  vl53l8cx.set_resolution(VL53L8CX_RESOLUTION_8X8); // Set the resolution to 8x8
  vl53l8cx.start_ranging(); // Start the ranging
}

// Function to capture an image and return Base64 encoded string
String captureImage() {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed!");
        return "";
    }
    String base64Image = base64::encode(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return base64Image;
}


// Function to build JSON file
String buildJsonImage(String timestamp, String location, String base64Image) {
    StaticJsonDocument<4096> doc;
    doc["timestamp"] = timestamp;
    doc["location"] = location;
    doc["image"] = base64Image;

    String jsonString;
    serializeJson(doc, jsonString);
    return jsonString;
}

// Function to build JSON file
String buildJsonDepth(String timestamp, String location, String depthMatrix) {
    StaticJsonDocument<4096> doc;
    doc["timestamp"] = timestamp;
    doc["location"] = location;
    doc["depth"] = depthMatrix;

    String jsonString;
    serializeJson(doc, jsonString);
    return jsonString;
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

void setup() {
    Serial.begin(115200);
    setupWiFi();
    espClient.setInsecure();

    setupTime();

    client.setServer(mqtt_server, mqtt_port);
    DEV_I2C.begin();
    delay(500);
    setupCamera();
    delay(500);
    setupToF();

    delay(1000);
}

void loop() {
    if (!client.connected()) {
        reconnectMQTT();
    }
    client.loop();

    String currentTime = getCurrentTime();
    Serial.println("Current Time: " + currentTime);

    String currentLocation = getLocation();
    Serial.println("Current Location: " + currentLocation);
    delay(500);
    String imageBase64 = captureImage();
    Serial.println("Image: " + imageBase64);

    String jsonPayloadImage = buildJsonImage(currentTime, currentLocation, imageBase64);
    Serial.println("Image Payload: " + jsonPayloadImage);

    Serial.print("Image Payload Size: ");
    Serial.println(jsonPayloadImage.length()); 
    publishToMQTT(mqtt_topic_image, jsonPayloadImage);

    String depthMatrix = getDepthMatrix();
    Serial.println("Depth: " + depthMatrix);
    String jsonPayloadDepth = buildJsonDepth(currentTime, currentLocation, depthMatrix);
    Serial.println("Depth Payload: " + jsonPayloadDepth);

    Serial.print("Depth Payload Size: ");
    Serial.println(jsonPayloadDepth.length()); 
    publishToMQTT(mqtt_topic_depth, jsonPayloadDepth);


    Serial.print("Free heap: ");
    Serial.println(ESP.getFreeHeap());

    Serial.println("---------------------");

    delay(60000); // Send JSON every minute
}