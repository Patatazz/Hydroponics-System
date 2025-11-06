/*
 * ESP32 NPK Sensor Reader with Firebase Upload
 * Reads N, P, K values and sends to Firebase Realtime Database
 * 
 * Hardware Connections:
 * ESP32 GPIO32 (RX) -> RX on RS485 module
 * ESP32 GPIO33 (TX) -> TX on RS485 module
 * ESP32 3.3V -> VCC on RS485 module
 * ESP32 GND -> GND on RS485 module
 */

#include <WiFi.h>
#include <HTTPClient.h>

#define RXD2 16
#define TXD2 17

// WiFi credentials
const char* ssid = "PLDTHOMEFIBRWENG";
const char* password = "july030731longlong312003";

// Firebase settings
const char* firebaseHost = "https://hydroponic-4672a-default-rtdb.firebaseio.com";
const char* firebaseAuth = "YOUR_DATABASE_SECRET";  // Optional, use "" if open rules

// Sensor settings
#define SENSOR_ADDRESS 0x01
#define SENSOR_BAUD 4800
#define MODBUS_READ_HOLDING 0x03
#define REG_NPK_START 0x001E
#define REG_NPK_COUNT 3

// Variables
unsigned long lastUploadTime = 0;
const unsigned long uploadInterval = 10000; // Upload every 10 seconds

void setup() {
  Serial.begin(115200);
  Serial2.begin(SENSOR_BAUD, SERIAL_8N1, RXD2, TXD2);
  
  delay(2000);
  
  Serial.println("NPK Sensor with Firebase Upload");
  Serial.println("=================================\n");
  
  // Connect to WiFi
  connectWiFi();
  
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
    Serial.print(" mg/kg, ");
    
    Serial.print("P: ");
    Serial.print(data[1]);
    Serial.print(" mg/kg, ");
    
    Serial.print("K: ");
    Serial.print(data[2]);
    Serial.println(" mg/kg");
    
    // Upload to Firebase at specified interval
    if (millis() - lastUploadTime >= uploadInterval) {
      uploadToFirebase(data[0], data[1], data[2]);
      lastUploadTime = millis();
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

void uploadToFirebase(uint16_t nitrogen, uint16_t phosphorus, uint16_t potassium) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected. Reconnecting...");
    connectWiFi();
    return;
  }
  
  HTTPClient http;
  
  // Create Firebase URL with path
  String url = String(firebaseHost) + "/sensorData.json";
  if (strlen(firebaseAuth) > 0) {
    url += "?auth=" + String(firebaseAuth);
  }
  
  // Create JSON payload
  String jsonPayload = "{";
  jsonPayload += "\"nitrogen\":" + String(nitrogen) + ",";
  jsonPayload += "\"phosphorus\":" + String(phosphorus) + ",";
  jsonPayload += "\"potassium\":" + String(potassium) + ",";
  jsonPayload += "\"timestamp\":" + String(millis());
  jsonPayload += "}";
  
  Serial.println("\nUploading to Firebase...");
  Serial.println("URL: " + url);
  Serial.println("Data: " + jsonPayload);
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  int httpResponseCode = http.POST(jsonPayload);
  
  if (httpResponseCode > 0) {
    Serial.print("✓ Upload successful! Response code: ");
    Serial.println(httpResponseCode);
    String response = http.getString();
    Serial.println("Response: " + response);
  } else {
    Serial.print("✗ Upload failed. Error code: ");
    Serial.println(httpResponseCode);
    Serial.println("Error: " + http.errorToString(httpResponseCode));
  }
  
  http.end();
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