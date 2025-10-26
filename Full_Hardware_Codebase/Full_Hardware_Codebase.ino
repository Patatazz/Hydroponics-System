/*
 * ESP32 NPK Sensor with Firebase Real-time Connection
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
#include <TimeLib.h>
#include <SimpleTimer.h>
#include <time.h>

FirebaseData stream;       // main realtime database stream
FirebaseData modeStream;   
FirebaseData cropStream;   
FirebaseData pumpStream;   // Setup Firebase stream for water pump control

//RS485 Moduke
#define RXD2 16
#define TXD2 17

//Dosing Pump Relay
#define PUMP_N_PIN 18  // GPIO pin for Nitrogen pump
#define PUMP_P_PIN 19  // GPIO pin for Phosphorus pump
#define PUMP_K_PIN 14  // GPIO pin for Potassium pump

// Dosing parameters
#define DOSE_PULSE_DURATION 2000   // Dose for 2 seconds at a time
#define DOSE_WAIT_TIME 8000        // Wait 8 seconds for mixing/reading
#define TARGET_TOLERANCE 5.0       // Stop when within ±5 PPM of target
#define MAX_DOSE_CYCLES 20        // Safety limit: max 20 dose cycles

// Auto Dosing global variable
bool autoDosingInProgress = false;
String autoDosingNutrient = "";
int autoDosingNutrientIndex = 0; // 0=N, 1=P, 2=K
unsigned long autoDosingStartTime = 0;

// ===== LOCAL TARGET VALUES FOR CROPS =====
struct CropTargets {
  float N;
  float P;
  float K;
};

// Define target PPM values for each crop (stored on ESP32)
CropTargets lettuce = {100.0, 21.5, 125.0};
CropTargets bokchoy = {200.0, 65.0, 175.0};

// Current selected crop targets
CropTargets currentTargets = lettuce;  // Default to lettuce
String currentCrop = "lettuce";

// Global variables
unsigned long lastDoseTime = 0;
unsigned long lastReadTime = 0;
bool isDosing = false;
bool isWaiting = false;
String currentNutrient = "";
float targetPPM = 0;
int doseCycleCount = 0;

// System mode tracking
String systemMode = "Auto";  // Can be "Auto" or "Manual"
bool firebaseReady = false;

// Water Pump Control
const int PUMP_RELAY_PIN1 = 13;
const int PUMP_RELAY_PIN2 = 26;
bool pumpRunning = false;
unsigned long pumpStartMillis = 0;
const unsigned long PUMP_DURATION_MS = 20UL * 60UL * 1000UL; // 20 minutes


//Water Level Sensor
#define WATER_PIN 32
const float TANK_CAPACITY_L = 32.0;

/**
 * ADC VALUES (Calibrated for 32L Tank)
 * Assuming 805 is 0% (Empty) and 550 is 100% (Full).
 * Inayos ang thresholds para mas detalyadong water status: FULL, NOMINAL, HALF, LOW.
 */
int LOW_WATER_ADC = 700; // ~25% capacity (~8L remaining) - Critical refill needed.
int HALF_WATER_ADC = 2000; // ~50% capacity (~16L remaining).
int NOMINAL_WATER_ADC = 3000; // ~75% capacity (~24L remaining).
int FULL_WATER_ADC = 4000; // ~90% capacity (~30L remaining).

// Daily Schedule Times
const int SCHEDULE_HH1 = 6, SCHEDULE_MM1 = 15; // 6:15 AM
const int SCHEDULE_HH2 = 12, SCHEDULE_MM2 = 0;  // 12:00 PM
int lastPump1Day = -1, lastPump2Day = -1;
bool waitingForInjection = false;
unsigned long waitStartMillis = 0;

// WiFi credentials
const char* ssid = "wifi";
const char* password = "pass";

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

SimpleTimer timer;

//NPK Value
bool success = false;
uint16_t data[3];
// Current sensor readings
float currentN = 0;
float currentP = 0;
float currentK = 0;

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

bool manualPumpControl = false; 
bool signupOK = false;
bool timeIsSynced = false;


void setup() {
  Serial.begin(115200);
  Serial2.begin(SENSOR_BAUD, SERIAL_8N1, RXD2, TXD2);
  
  delay(2000);
  
  pinMode(PUMP_N_PIN, OUTPUT);
  pinMode(PUMP_P_PIN, OUTPUT);
  pinMode(PUMP_K_PIN, OUTPUT);

  pinMode(PUMP_RELAY_PIN1, OUTPUT);
  pinMode(PUMP_RELAY_PIN2, OUTPUT);

  // Ensure all pumps are off initially
  digitalWrite(PUMP_N_PIN, HIGH);
  digitalWrite(PUMP_P_PIN, HIGH);
  digitalWrite(PUMP_K_PIN, HIGH);

  digitalWrite(PUMP_RELAY_PIN1, HIGH);
  digitalWrite(PUMP_RELAY_PIN2, HIGH);

  Serial.println("NPK Sensor with Firebase Real-time");
  Serial.println("====================================\n");
  
  // Connect to WiFi
  connectWiFi();

  syncTimeWithNTP();
  
  // Configure Firebase
  config.host = FIREBASE_HOST;
  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback;
  
  Serial.println("Connecting to Firebase...");
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Wait for authentication
  while (!Firebase.ready()) {
    Serial.print(".");
    delay(500);
  }
  
  firebaseReady = true;

  if (!Firebase.RTDB.beginStream(&pumpStream, "/controls/pump")) {
    Serial.printf("Pump stream begin error: %s\n", pumpStream.errorReason().c_str());
  }
  Firebase.RTDB.setStreamCallback(&pumpStream, pumpStreamCallback, streamTimeoutCallback);
  
  // Setup Firebase stream for nutrient dosing commands
  if (!Firebase.RTDB.beginStream(&stream, "/controls/dose")) {
    Serial.printf("Stream begin error: %s\n", stream.errorReason().c_str());
  }
  // Setup Firebase listener for nutrient dosing commands
  Firebase.RTDB.setStreamCallback(&stream, streamCallback, streamTimeoutCallback);

  // Setup Firebase stream for mode changes (Auto/Manual)
  if (!Firebase.RTDB.beginStream(&modeStream, "/controls/modeAuto")) {
    Serial.printf("Stream begin error: %s\n", stream.errorReason().c_str());
  }
  Firebase.RTDB.setStreamCallback(&modeStream, modeStreamCallback, streamTimeoutCallback);

  // Setup Firebase stream for crop changes
  if (!Firebase.RTDB.beginStream(&cropStream, "/controls/crop")) {
    Serial.printf("Stream begin error: %s\n", stream.errorReason().c_str());
  }
  Firebase.RTDB.setStreamCallback(&cropStream, cropStreamCallback, streamTimeoutCallback);

  // Read initial mode and crop from Firebase
  if (Firebase.RTDB.getString(&fbdo, "/controls/modeAuto")) {
    systemMode = fbdo.stringData();
    Serial.print("Initial Mode: ");
    Serial.println(systemMode);
  }

  if (Firebase.RTDB.getString(&fbdo, "/controls/crop")) {
    updateCropTargets(fbdo.stringData());
  }

  // Read initial pump state from Firebase
  if (Firebase.RTDB.getInt(&fbdo, "/controls/pump")) {
    int pumpValue = fbdo.intData();
    if (systemMode == "Manual") {
      if (pumpValue == 1) {
        startPumps();
      } else {
        stopPumps();
      }
    }
    Serial.print("Initial Pump State: ");
    Serial.println(pumpValue);
  }

  Serial.println("System ready for dosing commands");
  Serial.println("Mode: " + systemMode);
  Serial.println("Crop: " + currentCrop);


  // Set larger buffer for faster response
  fbdo.setBSSLBufferSize(1024, 1024);
  
  Serial.println("Firebase connected!\n");

  // Set up recurring timer for polling and checks (5 seconds interval)
  timer.setInterval(5000L, []() {

    // CHECK TIME SYNC FIRST - if not synced, try to sync again
    if (!timeIsSynced) {
      Serial.println("⚠ Time not synced yet, attempting to sync...");
      syncTimeWithNTP();
      return; // Don't run other logic until time is synced
    }

    // Show current time (helpful for debugging)
    Serial.print("Current time: ");
    Serial.print(hour());
    Serial.print(":");
    if (minute() < 10) Serial.print("0");
    Serial.print(minute());
    Serial.print(":");
    if (second() < 10) Serial.print("0");
    Serial.print(second());
    Serial.print(" | Date: ");
    Serial.print(month());
    Serial.print("/");
    Serial.print(day());
    Serial.print("/");
    Serial.println(year());

    
    int water_adc = analogRead(WATER_PIN);
    String water_level = getWaterIndicator(water_adc);

    readNPKSensors();
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
      Serial.print("Water Level Value: ");
      Serial.println(water_adc);

      updateFirebaseRealtime(data[0], data[1], data[2], water_level);

    } else {
      Serial.println("Modbus Error: Failed to read NPK values");
    }
    // Check pump duration
   if (pumpRunning && !manualPumpControl && millis() - pumpStartMillis >= PUMP_DURATION_MS) {
      stopPumps();
    }
    
    checkSchedule();
  });
  
  delay(1000);
}

void loop() {
  timer.run();

   // Handle manual dosing (from web app)
  if (isDosing) {
    handleDosingProcess();
  }

  // Handle automatic scheduled dosing
  if (autoDosingInProgress) {
    handleAutoDosingProcess();
  }
}

//------------------------- Water Level --------------------------
String getWaterIndicator(int adc) {
  // ADC range: 805 (0%) to 550 (100%)
  
  if (adc >= FULL_WATER_ADC) {
    return "FULL"; // ~90% to 100% (30L+)
  } else if (adc <= NOMINAL_WATER_ADC && adc >= HALF_WATER_ADC) {
    return "NOMINAL"; // ~75% to 90% (24L - 30L)
  } else if (adc <= HALF_WATER_ADC && adc >= LOW_WATER_ADC) {
    return "HALF"; // ~50% to 75% (16L - 24L)
  } else if (adc <= LOW_WATER_ADC) {
    return "LOW"; // ~25% to 50% (8L - 16L)
  } else {
    return "CRITICAL"; // Below ~25% (less than 8L)
  }
}
//------------------------- Water Level --------------------------

//------------------------- Sync Time for Scheduling -------------------------
void syncTimeWithNTP() {
  // Configure time zone (Philippines is UTC+8)
  configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  
  Serial.print("Waiting for NTP time sync");
  time_t now = time(nullptr);
  int attempts = 0;
  while (now < 8 * 3600 * 2 && attempts < 20) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    attempts++;
  }
  Serial.println();
  
  if (now > 8 * 3600 * 2) {
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    // Set TimeLib clock
    setTime(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
            timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
    
    timeIsSynced = true;  // ADD THIS LINE - Mark time as synced
    
    Serial.println("✓ Time synchronized with NTP server");
    Serial.print("Current time: ");
    Serial.print(hour());
    Serial.print(":");
    if (minute() < 10) Serial.print("0");
    Serial.print(minute());
    Serial.print(":");
    if (second() < 10) Serial.print("0");
    Serial.println(second());
    Serial.print("Date: ");
    Serial.print(month());
    Serial.print("/");
    Serial.print(day());
    Serial.print("/");
    Serial.println(year());
  } else {
    timeIsSynced = false;  // ADD THIS LINE
    Serial.println("✗ Failed to sync time with NTP - will retry");
  }
}
//------------------------- Sync Time for Scheduling End -------------------------

// --------------- Scheduling and Communication (Auto Mode) --------------------

void checkSchedule() {
  if (systemMode != "Auto") return; // Only run in Auto mode
  
  int h = hour(), m = minute(), today = day();
  
  // Morning cycle (Pump ON + Injection)
  if (h == SCHEDULE_HH1 && m == SCHEDULE_MM1 && lastPump1Day != today) {
    startPumps();
    waitStartMillis = millis();
    waitingForInjection = true;
    lastPump1Day = today;
    autoDosingStartTime = millis();
    Serial.println("[Schedule] 6:15 AM pump started, waiting 30s before injection");
  }
  
  // Wait 30 seconds for water circulation before starting injection
  if (waitingForInjection && millis() - waitStartMillis >= 30000UL) {
    waitingForInjection = false;
    autoDosingInProgress = true;
    autoDosingNutrientIndex = 0; // Start with Nitrogen
    autoDosingNutrient = "N";
    Serial.println("[Schedule] Starting automatic NPK injection sequence");
    Serial.println("[Schedule] Phase 1: Dosing Nitrogen");
    startAutoDosePulse();
  }
  
  // Midday cycle (Pump ON only, no injection)
  if (h == SCHEDULE_HH2 && m == SCHEDULE_MM2 && lastPump2Day != today) {
    startPumps();
    lastPump2Day = today;
    Serial.println("[Schedule] 12:00 PM pump started");
  }
}
// --------------- Scheduling and Communication (Auto Mode) --------------------


//------------------------- Water Pump Logic --------------------------
void startPumps() {
  digitalWrite(PUMP_RELAY_PIN1, LOW);
  digitalWrite(PUMP_RELAY_PIN2, LOW);
  pumpRunning = true;
  pumpStartMillis = millis();
  Serial.println("[Pump] ON");
}

void stopPumps() {
  digitalWrite(PUMP_RELAY_PIN1, HIGH);
  digitalWrite(PUMP_RELAY_PIN2, HIGH);
  pumpRunning = false;
  Serial.println("[Pump] OFF");
}
//------------------------- Water Pump Logic End --------------------------


// ---------------- Wifi Connection Logic --------------------------------
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
// ---------------- Wifi Connection Logic End --------------------------------


// ---------------- Sendor Data Upload Logic --------------------------------
void updateFirebaseRealtime(uint16_t nitrogen, uint16_t phosphorus, uint16_t potassium, String WaterLevel) {
  if (Firebase.ready()) {
    // Create JSON object for batch update (faster than individual updates)
    FirebaseJson json;
    json.set("N", nitrogen);
    json.set("P", phosphorus);
    json.set("K", potassium);
    json.set("waterLevel" + String(map(analogRead(WATER_PIN), 700, 4000, 0, 100)) + ",");
    json.set("Water", WaterLevel);
    json.set("timestamp/.sv", "timestamp");  // Server timestamp
    
    // Update all values at once
    if (Firebase.RTDB.setJSON(&fbdo, "/sensors", &json)) {
      Serial.println("✓ Firebase updated successfully!");
    } else {
      Serial.println("✗ Firebase update failed");
      Serial.println("Reason: " + fbdo.errorReason());
    }
  } else {
    Serial.println("Firebase not ready");
  }
}
// ---------------- Sendor Data Upload Logic End --------------------------------


// ---------------- Read NPK Sensor Logic --------------------------------
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

// Read NPK sensor
void readNPKSensors() {
  success = false;
  for (int attempt = 0; attempt < 3; attempt++) {
    if (readNPKRegisters(data)) {
      success = true;
      // Update current values
      currentN = (float)data[0];
      currentP = (float)data[1];
      currentK = (float)data[2];
      break;
    }
    delay(100);
  }
}

// ---------------- Read NPK Sensor Logic End --------------------------------


//-------------------------- Dosing Pump Logic -----------------------------------
// Firebase stream callback for mode changes
void streamTimeoutCallback(bool timeout) {
  if (timeout) {
    Serial.println("Stream timeout, resuming...");
  }
  
  if (!stream.httpConnected()) {
    Serial.printf("Error code: %d, reason: %s\n", stream.httpCode(), stream.errorReason().c_str());
  }
}

// 2. MODE STREAM CALLBACK
void modeStreamCallback(FirebaseStream data) {
  if (data.dataTypeEnum() == firebase_rtdb_data_type_string) {
    String newMode = data.stringData();
    
    if (newMode != systemMode) {
      systemMode = newMode;
      Serial.print("Mode changed to: ");
      Serial.println(systemMode);
      
      // If switched to Auto while dosing, stop dosing
      if (systemMode == "Auto" && isDosing) {
        Serial.println("Mode changed to Auto - stopping manual dosing");
        completeDosing(false, getCurrentNutrientValue());
      }
      // If switched to Auto, stop water pump if it's running manually
      if (systemMode == "Auto" && pumpRunning) {
        Serial.println("Mode changed to Auto - stopping manual water pump");
        stopPumps();
      }
    }
  }
}

// 3. CROP STREAM CALLBACK
void cropStreamCallback(FirebaseStream data) {
  if (data.dataTypeEnum() == firebase_rtdb_data_type_string) {
    String newCrop = data.stringData();
    updateCropTargets(newCrop);
  }
}

// 4. UPDATE CROP TARGETS
void updateCropTargets(String crop) {
  if (crop != currentCrop) {
    currentCrop = crop;
    
    if (crop == "lettuce") {
      currentTargets = lettuce;
      Serial.println("Crop changed to: Lettuce");
    } else if (crop == "bokchoy") {
      currentTargets = bokchoy;
      Serial.println("Crop changed to: Bokchoy");
    }
    
    Serial.print("New targets - N:");
    Serial.print(currentTargets.N);
    Serial.print(" P:");
    Serial.print(currentTargets.P);
    Serial.print(" K:");
    Serial.println(currentTargets.K);
  }
}

// 5. STREAM CALLBACK (simplified version)
void streamCallback(FirebaseStream data) {
  Serial.println("Dosing command received...");
  
  // Check if system is in Manual mode
  if (systemMode != "Manual") {
    Serial.println("REJECTED: System must be in Manual mode for manual dosing");
    return;
  }
  
  // The stream is listening to /controls/dose which contains N, P, K
  // We need to read the entire dose object manually
  if (Firebase.RTDB.getJSON(&fbdo, "/controls/dose")) {
    Serial.println("Reading dose values from Firebase...");
    
    FirebaseJson json = fbdo.jsonObject();
    FirebaseJsonData result;

    bool nState = false, pState = false, kState = false;

    if (json.get(result, "N")) {
      nState = result.to<bool>();
      Serial.print("N state: ");
      Serial.println(nState);
    }
    
    if (json.get(result, "P")) {
      pState = result.to<bool>();
      Serial.print("P state: ");
      Serial.println(pState);
    }
    
    if (json.get(result, "K")) {
      kState = result.to<bool>();
      Serial.print("K state: ");
      Serial.println(kState);
    }
    
    // Only process if not already dosing
    if (!isDosing) {
      // Priority: N -> P -> K (only one at a time)
      if (nState) {
        Serial.println("✓ Nutrient N dosing triggered!");
        startTargetDosing("N", currentTargets.N);
      } else if (pState) {
        Serial.println("✓ Nutrient P dosing triggered!");
        startTargetDosing("P", currentTargets.P);
      } else if (kState) {
        Serial.println("✓ Nutrient K dosing triggered!");
        startTargetDosing("K", currentTargets.K);
      } else {
        Serial.println("No nutrient flags are true");
      }
    } else {
      Serial.println("Already dosing, ignoring new command");
    }
  } else {
    Serial.println("Failed to read dose values from Firebase");
    Serial.println(fbdo.errorReason());
  }
}

// 6. START TARGET DOSING
void startTargetDosing(String nutrient, float target) {
  currentNutrient = nutrient;
  targetPPM = target;
  isDosing = true;
  isWaiting = false;
  doseCycleCount = 0;
  
  Serial.println("\n=== Target-Based Dosing Started ===");
  Serial.print("Mode: ");
  Serial.println(systemMode);
  Serial.print("Crop: ");
  Serial.println(currentCrop);
  Serial.print("Nutrient: ");
  Serial.println(nutrient);
  Serial.print("Target: ");
  Serial.print(target);
  Serial.println(" PPM");
  Serial.print("Current: ");
  Serial.print(getCurrentNutrientValue());
  Serial.println(" PPM");
  
  // Start first dose cycle
  startDosePulse();
}

// 7. HANDLE DOSING PROCESS (State Machine)
void handleDosingProcess() {
  // Check if mode changed to Auto during dosing
  if (systemMode == "Auto") {
    Serial.println("Mode changed to Auto - stopping manual dosing");
    completeDosing(false, getCurrentNutrientValue());
    return;
  }
  
  float currentValue = getCurrentNutrientValue();
  
  // Check if target is reached
  if (currentValue >= (targetPPM - TARGET_TOLERANCE)) {
    Serial.println("Target reached!");
    completeDosing(true, currentValue);
    return;
  }
  
  // Check if max cycles reached (safety)
  if (doseCycleCount >= MAX_DOSE_CYCLES) {
    Serial.println("Max dose cycles reached!");
    completeDosing(false, currentValue);
    return;
  }
  
  // If we're dosing (pump is on)
  if (!isWaiting) {
    // Check if dose pulse duration completed
    if (millis() - lastDoseTime >= DOSE_PULSE_DURATION) {
      stopDosePulse();
    }
  } 
  // If we're waiting for mixing/reading
  else {
    // Check if wait time completed
    if (millis() - lastDoseTime >= DOSE_WAIT_TIME) {
      // Check current value again
      currentValue = getCurrentNutrientValue();
      
      Serial.print("After cycle ");
      Serial.print(doseCycleCount);
      Serial.print(": ");
      Serial.print(currentValue);
      Serial.print(" PPM (Target: ");
      Serial.print(targetPPM);
      Serial.println(" PPM)");
      
      // If still below target, dose again
      if (currentValue < (targetPPM - TARGET_TOLERANCE)) {
        startDosePulse();
      } else {
        completeDosing(true, currentValue);
      }
    }
  }
}

// 8. START DOSE PULSE
void startDosePulse() {
  isWaiting = false;
  doseCycleCount++;
  lastDoseTime = millis();
  
  Serial.print("Cycle ");
  Serial.print(doseCycleCount);
  Serial.print(": ");
  
  // Turn on appropriate pump (LOW = ON for relay)
  if (currentNutrient == "N") {
    digitalWrite(PUMP_N_PIN, LOW);
    Serial.println("N pump ON");
  } else if (currentNutrient == "P") {
    digitalWrite(PUMP_P_PIN, LOW);
    Serial.println("P pump ON");
  } else if (currentNutrient == "K") {
    digitalWrite(PUMP_K_PIN, LOW);
    Serial.println("K pump ON");
  }
}

// 9. STOP DOSE PULSE
void stopDosePulse() {
  // Turn off all pumps (HIGH = OFF for relay)
  digitalWrite(PUMP_N_PIN, HIGH);
  digitalWrite(PUMP_P_PIN, HIGH);
  digitalWrite(PUMP_K_PIN, HIGH);
  
  Serial.println("Pump OFF - waiting for mixing...");
  
  isWaiting = true;
  lastDoseTime = millis();
}

// 10. COMPLETE DOSING (Simplified - only resets dose flag)
void completeDosing(bool success, float finalValue) {
  isDosing = false;
  isWaiting = false;
  
  // Ensure all pumps are off
  digitalWrite(PUMP_N_PIN, HIGH);
  digitalWrite(PUMP_P_PIN, HIGH);
  digitalWrite(PUMP_K_PIN, HIGH);
  
  Serial.println("\n=== Dosing Complete ===");
  Serial.print("Nutrient: ");
  Serial.println(currentNutrient);
  Serial.print("Final Value: ");
  Serial.print(finalValue);
  Serial.println(" PPM");
  Serial.print("Target: ");
  Serial.print(targetPPM);
  Serial.println(" PPM");
  Serial.print("Cycles Used: ");
  Serial.println(doseCycleCount);
  Serial.print("Success: ");
  Serial.println(success ? "YES" : "NO");
  
  // ONLY reset the specific nutrient flag - NO other Firebase writes
  String path = "/controls/dose/" + currentNutrient;
  if (Firebase.RTDB.setBool(&fbdo, path, false)) {
    Serial.print("✓ Reset flag: ");
    Serial.println(path);
  } else {
    Serial.print("✗ Failed to reset flag: ");
    Serial.println(path);
  }
  
  currentNutrient = "";
  targetPPM = 0;
  doseCycleCount = 0;
}

// 11. GET CURRENT NUTRIENT VALUE
float getCurrentNutrientValue() {
  if (currentNutrient == "N") return (float)data[0];
  if (currentNutrient == "P") return (float)data[1];
  if (currentNutrient == "K") return (float)data[2];
  return 0;
}

//-------------------------- Dosing Pump Logic End -----------------------------------

//-------------------------- Water Pump Logic ----------------------
void pumpStreamCallback(FirebaseStream data) {
  Serial.println("Water pump command received...");
  
  // Check if system is in Manual mode
  if (systemMode != "Manual") {
    Serial.println("REJECTED: Water pump control requires Manual mode");
    Serial.print("Current mode: ");
    Serial.println(systemMode);
    return;
  }
  
  if (data.dataTypeEnum() == firebase_rtdb_data_type_integer) {
    int pumpValue = data.intData();
    
    Serial.print("Pump value: ");
    Serial.println(pumpValue);
    
    if (pumpValue == 1) {
      manualPumpControl = true;  // Mark as manual control
      startPumps();
      Serial.println("✓ Water pump turned ON (Manual control)");
    } else if (pumpValue == 0) {
      manualPumpControl = false;  // Clear manual control flag
      stopPumps();
      Serial.println("✓ Water pump turned OFF (Manual control)");
    }
  }
}
//-------------------------- Water Pump Logic ----------------------

//-------------------------- Automatic Dosing Logic -----------------------------

void startAutoDosePulse() {
  isWaiting = false;
  doseCycleCount++;
  lastDoseTime = millis();
  
  Serial.print("[Auto Dose] Cycle ");
  Serial.print(doseCycleCount);
  Serial.print(": ");
  
  // Turn on appropriate pump based on current nutrient
  if (autoDosingNutrient == "N") {
    digitalWrite(PUMP_N_PIN, LOW);
    Serial.println("N pump ON");
  } else if (autoDosingNutrient == "P") {
    digitalWrite(PUMP_P_PIN, LOW);
    Serial.println("P pump ON");
  } else if (autoDosingNutrient == "K") {
    digitalWrite(PUMP_K_PIN, LOW);
    Serial.println("K pump ON");
  }
}

// Add this new function to stop auto dose pulse:
void stopAutoDosePulse() {
  // Turn off all pumps
  digitalWrite(PUMP_N_PIN, HIGH);
  digitalWrite(PUMP_P_PIN, HIGH);
  digitalWrite(PUMP_K_PIN, HIGH);
  
  Serial.println("[Auto Dose] Pump OFF - waiting for mixing...");
  
  isWaiting = true;
  lastDoseTime = millis();
}

void handleAutoDosingProcess() {
  if (!autoDosingInProgress) return;
  
  // Get current nutrient value and target
  float currentValue = 0;
  float targetValue = 0;
  
  if (autoDosingNutrient == "N") {
    currentValue = (float)data[0];
    targetValue = currentTargets.N;
  } else if (autoDosingNutrient == "P") {
    currentValue = (float)data[1];
    targetValue = currentTargets.P;
  } else if (autoDosingNutrient == "K") {
    currentValue = (float)data[2];
    targetValue = currentTargets.K;
  }
  
  // Check if target is reached for current nutrient
  if (currentValue >= (targetValue - TARGET_TOLERANCE)) {
    Serial.print("[Auto Dose] ");
    Serial.print(autoDosingNutrient);
    Serial.print(" target reached! Current: ");
    Serial.print(currentValue);
    Serial.print(" PPM, Target: ");
    Serial.print(targetValue);
    Serial.println(" PPM");
    
    // Move to next nutrient
    autoDosingNutrientIndex++;
    doseCycleCount = 0; // Reset cycle count for next nutrient
    
    if (autoDosingNutrientIndex == 1) {
      // Move to Phosphorus
      autoDosingNutrient = "P";
      Serial.println("[Schedule] Phase 2: Dosing Phosphorus");
      startAutoDosePulse();
      return;
    } else if (autoDosingNutrientIndex == 2) {
      // Move to Potassium
      autoDosingNutrient = "K";
      Serial.println("[Schedule] Phase 3: Dosing Potassium");
      startAutoDosePulse();
      return;
    } else if (autoDosingNutrientIndex >= 3) {
      // All nutrients done
      completeAutoDosing();
      return;
    }
  }
  
  // If we're dosing (pump is on)
  if (!isWaiting) {
    // Check if dose pulse duration completed
    if (millis() - lastDoseTime >= DOSE_PULSE_DURATION) {
      stopAutoDosePulse();
    }
  } 
  // If we're waiting for mixing/reading
  else {
    // Check if wait time completed
    if (millis() - lastDoseTime >= DOSE_WAIT_TIME) {
      // Check current value again
      if (autoDosingNutrient == "N") {
        currentValue = (float)data[0];
      } else if (autoDosingNutrient == "P") {
        currentValue = (float)data[1];
      } else if (autoDosingNutrient == "K") {
        currentValue = (float)data[2];
      }
      
      Serial.print("[Auto Dose] After cycle ");
      Serial.print(doseCycleCount);
      Serial.print(": ");
      Serial.print(autoDosingNutrient);
      Serial.print(" = ");
      Serial.print(currentValue);
      Serial.print(" PPM (Target: ");
      Serial.print(targetValue);
      Serial.println(" PPM)");
      
      // If still below target, dose again (NO MAX CYCLE LIMIT for auto mode)
      if (currentValue < (targetValue - TARGET_TOLERANCE)) {
        startAutoDosePulse();
      }
    }
  }
}

// Add this new function to complete automatic dosing:
void completeAutoDosing() {
  autoDosingInProgress = false;
  isWaiting = false;
  
  // Ensure all dosing pumps are off
  digitalWrite(PUMP_N_PIN, HIGH);
  digitalWrite(PUMP_P_PIN, HIGH);
  digitalWrite(PUMP_K_PIN, HIGH);
  
  unsigned long totalTime = (millis() - autoDosingStartTime) / 1000; // in seconds
  
  Serial.println("\n=== Automatic NPK Injection Complete ===");
  Serial.print("Total time: ");
  Serial.print(totalTime);
  Serial.println(" seconds");
  Serial.print("Final values - N: ");
  Serial.print(data[0]);
  Serial.print(" PPM, P: ");
  Serial.print(data[1]);
  Serial.print(" PPM, K: ");
  Serial.print(data[2]);
  Serial.println(" PPM");
  Serial.println("========================================\n");
  
  autoDosingNutrient = "";
  autoDosingNutrientIndex = 0;
  doseCycleCount = 0;
}

