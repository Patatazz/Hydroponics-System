/*
 * ESP32 NPK Sensor with Firebase Real-time Connection
 * For Hydroponic/Water-based NPK monitoring (PPM)
 * Uses Firebase ESP Client library for true real-time updates
 * 
 * IMPORTANT: Install this library first!
 * Arduino IDE: Tools -> Manage Libraries -> Search "Firebase ESP Client"
 * Install: "Firebase Arduino Client Library for ESP8266 and ESP32" by Mobizt
 * 
 * Hardware Connections:
 * ESP32 GPIO32 (RX) -> RX on RS485 module
 * ESP32 GPIO33 (TX) -> TX on RS485 module
 * ESP32 3.3V -> VCC on RS485 module
 * ESP32 GND -> GND on RS485 module
 */

#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define RXD2 16
#define TXD2 17

// WiFi credentials
const char* ssid = "PLDTHOMEFIBRWENG";
const char* password = "july030731longlong312003";

// Firebase Project Settings
#define FIREBASE_HOST "hydroponic-4672a-default-rtdb.firebaseio.com"
#define API_KEY "AIzaSyDzXywrMU5Wsx_ylC925U_-TbUYgOsIAv8"  // Get from Project Settings
#define USER_EMAIL "irishstephany03@gmail.com"     // Firebase user email
#define USER_PASSWORD "#Kawal12345"  // Firebase user password

// Sensor settings
#define SENSOR_ADDRESS 0x01
#define SENSOR_BAUD 4800
#define MODBUS_READ_HOLDING 0x03
#define REG_NPK_START 0x001E
#define REG_NPK_COUNT 3

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

bool signupOK = false;
unsigned long lastUpdateTime = 0;
const unsigned long updateInterval = 2000; // Update every 2 seconds

void setup() {
  Serial.begin(115200);
  Serial2.begin(SENSOR_BAUD, SERIAL_8N1, RXD2, TXD2);
  
  delay(2000);
  
  Serial.println("NPK Sensor - Hydroponic PPM Monitor");
  Serial.println("====================================\n");
  
  // Connect to WiFi
  connectWiFi();
  
  // Configure Firebase
  config.host = FIREBASE_HOST;
  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback;
  
  Serial.println("Connecting to Firebase...");
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  // Set larger buffer for faster response
  fbdo.setBSSLBufferSize(1024, 1024);
  
  Serial.println("Firebase connected!\n");
  
  delay(1000);
}

void loop() {
  uint16_t data[3];
  
  // Try reading up to 3 times
  bool success = false;
  for (int attempt = 0; attempt < 3; attempt++) {
    if (readNPKRegisters(data)) {
      success = true;
      break;
    }
    delay(100);
  }
  
  if (success) {
    Serial.print("N: ");
    Serial.print(data[0]);
    Serial.print(" ppm, ");
    
    Serial.print("P: ");
    Serial.print(data[1]);
    Serial.print(" ppm, ");
    
    Serial.print("K: ");
    Serial.print(data[2]);
    Serial.println(" ppm");
    
    // Update Firebase immediately with real-time stream
    if (millis() - lastUpdateTime >= updateInterval) {
      updateFirebaseRealtime(data[0], data[1], data[2]);
      lastUpdateTime = millis();
    }
  } else {
    Serial.println("Modbus Error: Failed to read NPK values");
  }
  
  delay(2000);
  delay(500);
}

void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi Connection Failed!");
  }
}

void updateFirebaseRealtime(uint16_t nitrogen, uint16_t phosphorus, uint16_t potassium) {
  if (Firebase.ready()) {
    // Create JSON object for batch update (faster than individual updates)
    FirebaseJson json;
    json.set("nitrogen_ppm", nitrogen);
    json.set("phosphorus_ppm", phosphorus);
    json.set("potassium_ppm", potassium);
    json.set("timestamp/.sv", "timestamp");  // Server timestamp
    
    // Update all values at once
    if (Firebase.RTDB.setJSON(&fbdo, "/sensorData", &json)) {
      Serial.println("✓ Firebase updated successfully!");
    } else {
      Serial.println("✗ Firebase update failed");
      Serial.println("Reason: " + fbdo.errorReason());
    }
  } else {
    Serial.println("Firebase not ready");
  }
}

bool readNPKRegisters(uint16_t* data) {
  uint8_t query[8];
  query[0] = SENSOR_ADDRESS;
  query[1] = MODBUS_READ_HOLDING;
  query[2] = (REG_NPK_START >> 8) & 0xFF;
  query[3] = REG_NPK_START & 0xFF;
  query[4] = 0x00;
  query[5] = REG_NPK_COUNT;
  
  uint16_t crc = calculateCRC(query, 6);
  query[6] = crc & 0xFF;
  query[7] = (crc >> 8) & 0xFF;
  
  while (Serial2.available()) Serial2.read();
  delay(50);
  
  Serial2.write(query, 8);
  Serial2.flush();
  delay(50);
  
  unsigned long startTime = millis();
  while (Serial2.available() < 11 && (millis() - startTime) < 1500) {
    delay(10);
  }
  
  int expectedLen = 11;
  
  if (Serial2.available() < expectedLen) {
    return false;
  }
  
  uint8_t response[20];
  int len = 0;
  while (Serial2.available() && len < 20) {
    response[len++] = Serial2.read();
  }
  
  if (len < expectedLen) {
    return false;
  }
  
  if (response[0] != SENSOR_ADDRESS || response[1] != MODBUS_READ_HOLDING) {
    return false;
  }
  
  uint16_t receivedCRC = response[len - 2] | (response[len - 1] << 8);
  uint16_t calculatedCRC = calculateCRC(response, len - 2);
  
  if (receivedCRC != calculatedCRC) {
    return false;
  }
  
  data[0] = (response[3] << 8) | response[4];
  data[1] = (response[5] << 8) | response[6];
  data[2] = (response[7] << 8) | response[8];
  
  return true;
}

uint16_t calculateCRC(uint8_t* data, int length) {
  uint16_t crc = 0xFFFF;
  
  for (int i = 0; i < length; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  
  return crc;
}