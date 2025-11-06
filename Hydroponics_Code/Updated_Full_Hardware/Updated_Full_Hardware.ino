/*
 * ESP32 NPK Sensor with Firebase Real-time Connection
 * UPDATED: Manual Injection with Custom PPM Values
 * 
 * NEW FEATURES:
 * - Manual injection based on user input from web app
 * - Additive dosing: current value + user input = target
 * - Individual nutrient injection and batch "Inject All"
 * - Firebase paths: controls/manualInject/{N,P,K} and controls/manualInjectAll
 * 
 * Hardware Connections:
 * ESP32 GPIO16 (RX) -> RX on RS485 module
 * ESP32 GPIO17 (TX) -> TX on RS485 module
 * ESP32 GPIO18 -> Nitrogen Pump Relay
 * ESP32 GPIO19 -> Phosphorus Pump Relay
 * ESP32 GPIO14 -> Potassium Pump Relay
 * ESP32 GPIO13, 26 -> Water Pump Relays
 */

#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <TimeLib.h>
#include <SimpleTimer.h>
#include <time.h>

FirebaseData stream;
FirebaseData modeStream;
FirebaseData cropStream;
FirebaseData pumpStream;
FirebaseData manualInjectStream;  // NEW: Stream for manual injection
FirebaseData manualInjectAllStream;  // NEW: Stream for inject all

// RS485 Module
#define RXD2 16
#define TXD2 17

// Dosing Pump Relay
#define PUMP_N_PIN 18
#define PUMP_P_PIN 19
#define PUMP_K_PIN 27

// Dosing parameters
#define DOSE_PULSE_DURATION 4000   // Dose for 2 seconds at a time
#define DOSE_WAIT_TIME 8000        // Wait 8 seconds for mixing/reading
#define TARGET_TOLERANCE 10.0       // Stop when within ±10 PPM of target
#define MAX_DOSE_CYCLES 25         // Safety limit for manual mode

// Auto Dosing global variables
bool autoDosingInProgress = false;
String autoDosingNutrient = "";
int autoDosingNutrientIndex = 0;
unsigned long autoDosingStartTime = 0;

// Manual Dosing global variables
bool manualDosingInProgress = false;
String manualDosingNutrient = "";
float manualTargetPPM = 0;
unsigned long manualDosingStartTime = 0;

// Inject All global variables
bool injectAllInProgress = false;
int injectAllNutrientIndex = 0;
float injectAllTargets[3] = {0, 0, 0};  // N, P, K
String injectAllNutrients[3] = {"N", "P", "K"};

// ===== LOCAL TARGET VALUES FOR CROPS =====
struct CropTargets {
  float N;
  float P;
  float K;
};

CropTargets lettuce = {100.0, 21.5, 125.0};
CropTargets bokchoy = {180.0, 45.0, 220.0};

CropTargets currentTargets = lettuce;
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
String systemMode = "Auto";
bool firebaseReady = false;

// For automatic water pump 
bool waterPump = false;

// Water Pump Control
const int PUMP_RELAY_PIN1 = 13;
const int PUMP_RELAY_PIN2 = 26;
bool pumpRunning = false;
unsigned long pumpStartMillis = 0;
const unsigned long PUMP_DURATION_MS = 20UL * 60UL * 1000UL;

// Water Level Sensor
#define WATER_PIN 32
const float TANK_CAPACITY_L = 32.0;

int LOW_WATER_ADC = 700;
int HALF_WATER_ADC = 2000;
int NOMINAL_WATER_ADC = 3000;
int FULL_WATER_ADC = 4000;

// Daily Schedule Times
const int SCHEDULE_HH1 = 6, SCHEDULE_MM1 = 15;
const int SCHEDULE_HH2 = 12, SCHEDULE_MM2 = 0;
const int SCHEDULE_HH3 = 12, SCHEDULE_MM3 = 20;
int lastPump1Day = -1, lastPump2Day = -1;
bool waitingForInjection = false;
unsigned long waitStartMillis = 0;

// WiFi credentials
const char* ssid = "HONOR X9a 5G";
const char* password = "pipinows123";

// Firebase Project Settings
#define FIREBASE_HOST "hydroponic-4672a-default-rtdb.firebaseio.com"
#define API_KEY "AIzaSyDzXywrMU5Wsx_ylC925U_-TbUYgOsIAv8"
#define USER_EMAIL "irishstephany03@gmail.com"
#define USER_PASSWORD "#Kawal12345"

// Sensor settings
#define SENSOR_ADDRESS 0x01
#define SENSOR_BAUD 4800
#define MODBUS_READ_HOLDING 0x03
#define REG_NPK_START 0x001E
#define REG_NPK_COUNT 3

SimpleTimer timer;

// NPK Value
bool success = false;
uint16_t data[3];
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

  Serial.println("NPK Sensor with Manual Injection Control");
  Serial.println("==========================================\n");
  
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

  while (!Firebase.ready()) {
    Serial.print(".");
    delay(500);
  }
  
  firebaseReady = true;
  Serial.println("\nFirebase connected!");

  // Setup streams
  setupFirebaseStreams();

  // Read initial states
  readInitialStates();

  Serial.println("System ready for commands");
  Serial.println("Mode: " + systemMode);
  Serial.println("Crop: " + currentCrop);

  fbdo.setBSSLBufferSize(1024, 1024);

  // Set up recurring timer
  timer.setInterval(5000L, []() {
    // if (!timeIsSynced) {
    //   Serial.println("⚠ Time not synced yet, attempting to sync...");
    //   syncTimeWithNTP();
    //   return;
    // }

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
      Serial.print(" mg/kg, P: ");
      Serial.print(data[1]);
      Serial.print(" mg/kg, K: ");
      Serial.print(data[2]);
      Serial.print(" mg/kg | Water: ");
      Serial.println(water_adc);

      updateFirebaseRealtime(data[0], data[1], data[2], water_adc, water_level);
    } else {
      Serial.println("Modbus Error: Failed to read NPK values");
    }

    if (pumpRunning && !manualPumpControl && millis() - pumpStartMillis >= PUMP_DURATION_MS) {
      stopPumps();
    }
    
    
    checkSchedule();
  });
  
  delay(1000);
}

void loop() {
  timer.run();

  // Handle automatic scheduled dosing
  if (autoDosingInProgress) {
    handleAutoDosingProcess();
  }

  // NEW: Handle manual dosing
  if (manualDosingInProgress) {
    handleManualDosingProcess();
  }

  // NEW: Handle inject all
  if (injectAllInProgress) {
    handleInjectAllProcess();
  }
}

//------------------------- Setup Firebase Streams -------------------------
void setupFirebaseStreams() {
  // Pump stream
  if (!Firebase.RTDB.beginStream(&pumpStream, "/controls/pump")) {
    Serial.printf("Pump stream error: %s\n", pumpStream.errorReason().c_str());
  }
  Firebase.RTDB.setStreamCallback(&pumpStream, pumpStreamCallback, streamTimeoutCallback);

  // Mode stream
  if (!Firebase.RTDB.beginStream(&modeStream, "/controls/modeAuto")) {
    Serial.printf("Mode stream error: %s\n", modeStream.errorReason().c_str());
  }
  Firebase.RTDB.setStreamCallback(&modeStream, modeStreamCallback, streamTimeoutCallback);

  // Crop stream
  if (!Firebase.RTDB.beginStream(&cropStream, "/controls/crop")) {
    Serial.printf("Crop stream error: %s\n", cropStream.errorReason().c_str());
  }
  Firebase.RTDB.setStreamCallback(&cropStream, cropStreamCallback, streamTimeoutCallback);

  // SINGLE manual inject stream (handles both individual and batch)
  if (!Firebase.RTDB.beginStream(&manualInjectStream, "/controls/manualInjection")) {
    Serial.printf("Manual inject stream error: %s\n", manualInjectStream.errorReason().c_str());
  }
  Firebase.RTDB.setStreamCallback(&manualInjectStream, manualInjectStreamCallback, streamTimeoutCallback);
}

void readInitialStates() {
  if (Firebase.RTDB.getString(&fbdo, "/controls/modeAuto")) {
    systemMode = fbdo.stringData();
    Serial.print("Initial Mode: ");
    Serial.println(systemMode);
  }

  if (Firebase.RTDB.getString(&fbdo, "/controls/crop")) {
    updateCropTargets(fbdo.stringData());
  }

  if (Firebase.RTDB.getInt(&fbdo, "/controls/pump")) {
    int pumpValue = fbdo.intData();
    if (systemMode == "Manual") {
      if (pumpValue == 1) {
        startPumps();
      } else {
        stopPumps();
      }
    }
  }
}

//----------------- Helper functions to check Firebase readiness ----------------------

bool isFirebaseHealthy() {
  if (!Firebase.ready()) {
    Serial.println("Firebase not ready");
    return false;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected");
    return false;
  }
  
  return true;
}

//------------------------- Water Level -------------------------
String getWaterIndicator(int adc) {
  if (adc >= FULL_WATER_ADC) {
    return "FULL";
  } else if (adc <= NOMINAL_WATER_ADC && adc >= HALF_WATER_ADC) {
    return "NOMINAL";
  } else if (adc <= HALF_WATER_ADC && adc >= LOW_WATER_ADC) {
    return "HALF";
  } else if (adc <= LOW_WATER_ADC) {
    return "LOW";
  } else {
    return "CRITICAL";
  }
}

//------------------------- Sync Time -------------------------
void syncTimeWithNTP() {
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
    
    setTime(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
            timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
    
    timeIsSynced = true;
    
    Serial.println("✓ Time synchronized");
    Serial.print("Current time: ");
    Serial.print(hour());
    Serial.print(":");
    if (minute() < 10) Serial.print("0");
    Serial.println(minute());
  } else {
    timeIsSynced = false;
    Serial.println("✗ Failed to sync time");
  }
}

//------------------------- Scheduling -------------------------
void checkSchedule() {
  if (systemMode != "Auto") return;
  syncTimeWithNTP();
  int h = hour(), m = minute(), today = day();
  
  // Morning cycle
  if (h == SCHEDULE_HH1 && m == SCHEDULE_MM1 && lastPump1Day != today) {
    startPumps();
    waitStartMillis = millis();
    waitingForInjection = true;
    lastPump1Day = today;
    autoDosingStartTime = millis();
    Serial.println("[Schedule] 6:15 AM pump started, waiting 30s before injection");
  }
  
  if (waitingForInjection && millis() - waitStartMillis >= 30000UL) {
    waitingForInjection = false;
    autoDosingInProgress = true;
    autoDosingNutrientIndex = 0;
    autoDosingNutrient = "N";
    Serial.println("[Schedule] Starting automatic NPK injection");
    startAutoDosePulse();
  }
  
  // Midday cycle
  if (h == SCHEDULE_HH2 && m == SCHEDULE_MM2 && lastPump2Day != today) {
    startPumps();
    lastPump2Day = today;
    waterPump = true;

    Serial.println("[Schedule] 12:00 PM pump started");
  }
  if (h == SCHEDULE_HH3 && m == SCHEDULE_MM3 && waterPump != false) {
    stopPumps();
    waterPump = false;
    Serial.println("[Schedule] 12:20 PM pump stopped");
  }
}

//------------------------- Water Pump -------------------------
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

//------------------------- WiFi Connection -------------------------
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
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi Failed!");
  }
}

//------------------------- Firebase Upload -------------------------
void updateFirebaseRealtime(uint16_t nitrogen, uint16_t phosphorus, uint16_t potassium, int water_adc, String WaterLevel) {
  if (Firebase.ready()) {
    FirebaseJson json;
    json.set("N", nitrogen);
    json.set("P", phosphorus);
    json.set("K", potassium);
    json.set("water_level", map(analogRead(WATER_PIN), 700, 4000, 0, 100));
    json.set("Water", WaterLevel);
    json.set("timestamp/.sv", "timestamp");
    
    if (Firebase.RTDB.setJSON(&fbdo, "/sensors", &json)) {
      Serial.println("✓ Firebase updated");
    } else {
      Serial.println("✗ Firebase update failed");
    }
  }
}

//------------------------- Read NPK Sensor -------------------------
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

void readNPKSensors() {
  success = false;
  for (int attempt = 0; attempt < 3; attempt++) {
    if (readNPKRegisters(data)) {
      success = true;
      currentN = (float)data[0];
      currentP = (float)data[1];
      currentK = (float)data[2];
      break;
    }
    delay(100);
  }
}

//------------------------- Firebase Callbacks -------------------------
void streamTimeoutCallback(bool timeout) {
  if (timeout) {
    Serial.println("Stream timeout, resuming...");
  }
}

void modeStreamCallback(FirebaseStream data) {
  if (data.dataTypeEnum() == firebase_rtdb_data_type_string) {
    String newMode = data.stringData();
    
    if (newMode != systemMode) {
      systemMode = newMode;
      Serial.print("Mode changed to: ");
      Serial.println(systemMode);
      
      // Stop any ongoing manual dosing
      if (systemMode == "Auto" && manualDosingInProgress) {
        Serial.println("Stopping manual dosing - switched to Auto");
        completeManualDosing(false);
      }
      
      if (systemMode == "Auto" && pumpRunning && manualPumpControl) {
        Serial.println("Stopping manual pump - switched to Auto");
        stopPumps();
        manualPumpControl = false;
      }
    }
  }
}

void cropStreamCallback(FirebaseStream data) {
  if (data.dataTypeEnum() == firebase_rtdb_data_type_string) {
    String newCrop = data.stringData();
    updateCropTargets(newCrop);
  }
}

void updateCropTargets(String crop) {
  if (crop != currentCrop) {
    currentCrop = crop;
    
    if (crop == "lettuce") {
      currentTargets = lettuce;
      Serial.println("Crop: Lettuce");
    } else if (crop == "bokchoy") {
      currentTargets = bokchoy;
      Serial.println("Crop: Bokchoy");
    }
    
    Serial.print("Targets - N:");
    Serial.print(currentTargets.N);
    Serial.print(" P:");
    Serial.print(currentTargets.P);
    Serial.print(" K:");
    Serial.println(currentTargets.K);
  }
}

void pumpStreamCallback(FirebaseStream data) {
  if (systemMode != "Manual") {
    return;
  }
  
  if (data.dataTypeEnum() == firebase_rtdb_data_type_integer) {
    int pumpValue = data.intData();
    
    if (pumpValue == 1) {
      manualPumpControl = true;
      startPumps();
      Serial.println("✓ Water pump ON (Manual)");
    } else if (pumpValue == 0) {
      manualPumpControl = false;
      stopPumps();
      Serial.println("✓ Water pump OFF (Manual)");
    }
  }
}

//------------------------- NEW: Manual Injection Callbacks -------------------------
void manualInjectStreamCallback(FirebaseStream data) {
  Serial.println("[Manual Inject] Stream update received");
  
  if (systemMode != "Manual") {
    Serial.println("REJECTED: System must be in Manual mode");
    return;
  }

  if (manualDosingInProgress) {
    Serial.println("REJECTED: Dosing already in progress");
    return;
  }

  // Use the stream data directly instead of making another Firebase call
  if (data.dataTypeEnum() == firebase_rtdb_data_type_json) {
    FirebaseJson &json = data.jsonObject();
    FirebaseJsonData result;
    
    // Check if this is a batch injection
    bool isBatchInjection = false;
    if (json.get(result, "batchInjection")) {
      isBatchInjection = result.to<bool>();
    }
    
    Serial.print("Batch Injection: ");
    Serial.println(isBatchInjection ? "YES (Inject All)" : "NO (Individual)");
    
    if (isBatchInjection) {
      // Handle "Inject All"
      injectAllTargets[0] = 0;
      injectAllTargets[1] = 0;
      injectAllTargets[2] = 0;
      bool hasTargets = false;

      if (json.get(result, "N/targetPPM")) {
        injectAllTargets[0] = result.to<float>();
        if (injectAllTargets[0] > 0) hasTargets = true;
      }

      if (json.get(result, "P/targetPPM")) {
        injectAllTargets[1] = result.to<float>();
        if (injectAllTargets[1] > 0) hasTargets = true;
      }

      if (json.get(result, "K/targetPPM")) {
        injectAllTargets[2] = result.to<float>();
        if (injectAllTargets[2] > 0) hasTargets = true;
      }

      if (hasTargets) {
        Serial.println("Starting BATCH injection (Inject All)");
        startInjectAllNutrient();
      }
    } else {
      // Handle individual nutrient injection
      Serial.println("Checking for INDIVIDUAL nutrient injection");
      
      // Check N
      if (json.get(result, "N/active") && result.to<bool>()) {
        if (json.get(result, "N/targetPPM")) {
          float targetPPM = result.to<float>();
          if (targetPPM > 0) {
            Serial.println("Individual N injection detected");
            startManualInjection("N", targetPPM);
            return;
          }
        }
      }

      // Check P
      if (json.get(result, "P/active") && result.to<bool>()) {
        if (json.get(result, "P/targetPPM")) {
          float targetPPM = result.to<float>();
          if (targetPPM > 0) {
            Serial.println("Individual P injection detected");
            startManualInjection("P", targetPPM);
            return;
          }
        }
      }

      // Check K
      if (json.get(result, "K/active") && result.to<bool>()) {
        if (json.get(result, "K/targetPPM")) {
          float targetPPM = result.to<float>();
          if (targetPPM > 0) {
            Serial.println("Individual K injection detected");
            startManualInjection("K", targetPPM);
            return;
          }
        }
      }
    }
  }
}

//------------------------- NEW: Manual Injection Logic -------------------------
void startManualInjection(String nutrient, float userInputPPM) {
  if (!isFirebaseHealthy()) {
    Serial.println("Cannot start injection: Firebase not healthy");
    return;
  }

  manualDosingInProgress = true;
  manualDosingNutrient = nutrient;
  
  // Get current sensor value
  float currentValue = 0;
  if (nutrient == "N") currentValue = currentN;
  else if (nutrient == "P") currentValue = currentP;
  else if (nutrient == "K") currentValue = currentK;

  // Calculate target: current + user input
  manualTargetPPM = currentValue + userInputPPM;
  
  doseCycleCount = 0;
  isWaiting = false;
  manualDosingStartTime = millis();

  Serial.println("\n=== Manual Injection Started ===");
  Serial.print("Nutrient: ");
  Serial.println(nutrient);
  Serial.print("Current Value: ");
  Serial.print(currentValue);
  Serial.println(" PPM");
  Serial.print("User Input: +");
  Serial.print(userInputPPM);
  Serial.println(" PPM");
  Serial.print("Target Value: ");
  Serial.print(manualTargetPPM);
  Serial.println(" PPM");
  Serial.println("================================\n");

  // Set status flag
  Firebase.RTDB.setBool(&fbdo, "/controls/dose/" + nutrient, true);

  // Start first dose pulse
  startManualDosePulse();
}

void startManualDosePulse() {
  isWaiting = false;
  doseCycleCount++;
  lastDoseTime = millis();
  
  Serial.print("[Manual] Cycle ");
  Serial.print(doseCycleCount);
  Serial.print(": ");
  
  if (manualDosingNutrient == "N") {
    digitalWrite(PUMP_N_PIN, LOW);
    Serial.println("N pump ON");
  } else if (manualDosingNutrient == "P") {
    digitalWrite(PUMP_P_PIN, LOW);
    Serial.println("P pump ON");
  } else if (manualDosingNutrient == "K") {
    digitalWrite(PUMP_K_PIN, LOW);
    Serial.println("K pump ON");
  }
}

void stopManualDosePulse() {
  digitalWrite(PUMP_N_PIN, HIGH);
  digitalWrite(PUMP_P_PIN, HIGH);
  digitalWrite(PUMP_K_PIN, HIGH);
  
  Serial.println("[Manual] Pump OFF - waiting for mixing...");
  
  isWaiting = true;
  lastDoseTime = millis();
}

void handleManualDosingProcess() {
  if (!manualDosingInProgress) return;

  // Check if mode changed to Auto
  if (systemMode == "Auto") {
    Serial.println("Mode changed to Auto - stopping manual dosing");
    completeManualDosing(false);
    return;
  }
  
  // Get current value
  float currentValue = 0;
  if (manualDosingNutrient == "N") currentValue = currentN;
  else if (manualDosingNutrient == "P") currentValue = currentP;
  else if (manualDosingNutrient == "K") currentValue = currentK;
  
  // Check if target reached
  if (currentValue >= (manualTargetPPM - TARGET_TOLERANCE)) {
    Serial.println("[Manual] Target reached!");
    completeManualDosing(true);
    return;
  }
  
  // Safety check: max cycles
  if (doseCycleCount >= MAX_DOSE_CYCLES) {
    Serial.println("[Manual] Max cycles reached!");
    completeManualDosing(false);
    return;
  }
  
  // If dosing (pump is on)
  if (!isWaiting) {
    if (millis() - lastDoseTime >= DOSE_PULSE_DURATION) {
      stopManualDosePulse();
    }
  } 
  // If waiting for mixing
  else {
    if (millis() - lastDoseTime >= DOSE_WAIT_TIME) {
      // Re-read sensor
      if (manualDosingNutrient == "N") currentValue = currentN;
      else if (manualDosingNutrient == "P") currentValue = currentP;
      else if (manualDosingNutrient == "K") currentValue = currentK;
      
      Serial.print("[Manual] After cycle ");
      Serial.print(doseCycleCount);
      Serial.print(": ");
      Serial.print(currentValue);
      Serial.print(" PPM (Target: ");
      Serial.print(manualTargetPPM);
      Serial.println(" PPM)");
      
      // Continue dosing if below target
      if (currentValue < (manualTargetPPM - TARGET_TOLERANCE)) {
        startManualDosePulse();
      } else {
        completeManualDosing(true);
      }
    }
  }
}

void completeManualDosing(bool success) {
  manualDosingInProgress = false;
  isWaiting = false;
  
  // Turn off all pumps
  digitalWrite(PUMP_N_PIN, HIGH);
  digitalWrite(PUMP_P_PIN, HIGH);
  digitalWrite(PUMP_K_PIN, HIGH);
  
  float finalValue = 0;
  if (manualDosingNutrient == "N") finalValue = currentN;
  else if (manualDosingNutrient == "P") finalValue = currentP;
  else if (manualDosingNutrient == "K") finalValue = currentK;
  
  unsigned long totalTime = (millis() - manualDosingStartTime) / 1000;
  
  Serial.println("\n=== Manual Injection Complete ===");
  Serial.print("Nutrient: ");
  Serial.println(manualDosingNutrient);
  Serial.print("Final Value: ");
  Serial.print(finalValue);
  Serial.println(" PPM");
  Serial.print("Target: ");
  Serial.print(manualTargetPPM);
  Serial.println(" PPM");
  Serial.print("Cycles Used: ");
  Serial.println(doseCycleCount);
  Serial.print("Total Time: ");
  Serial.print(totalTime);
  Serial.println(" seconds");
  Serial.print("Success: ");
  Serial.println(success ? "YES" : "NO");
  Serial.println("=================================\n");
  
  // Reset flag in Firebase
  Firebase.RTDB.setBool(&fbdo, "/controls/dose/" + manualDosingNutrient, false);
  
  // Clear data from manualInject path
  Firebase.RTDB.deleteNode(&fbdo, "/controls/manualInjection/" + manualDosingNutrient);
  
  manualDosingNutrient = "";
  manualTargetPPM = 0;
  doseCycleCount = 0;
}

//------------------------- NEW: Inject All Logic -------------------------
void startInjectAll() {
  injectAllInProgress = true;
  injectAllNutrientIndex = 0;
  
  Serial.println("\n=== Inject All Started ===");
  Serial.print("N Target: +");
  Serial.print(injectAllTargets[0]);
  Serial.println(" PPM");
  Serial.print("P Target: +");
  Serial.print(injectAllTargets[1]);
  Serial.println(" PPM");
  Serial.print("K Target: +");
  Serial.print(injectAllTargets[2]);
  Serial.println(" PPM");
  Serial.println("==========================\n");
  
  // Find first nutrient with target > 0
  while (injectAllNutrientIndex < 3 && injectAllTargets[injectAllNutrientIndex] <= 0) {
    injectAllNutrientIndex++;
  }
  
  if (injectAllNutrientIndex < 3) {
    startInjectAllNutrient();
  } else {
    completeInjectAll();
  }
}

void startInjectAllNutrient() {
  injectAllInProgress = true;
  String nutrient = injectAllNutrients[injectAllNutrientIndex];
  float userInputPPM = injectAllTargets[injectAllNutrientIndex];
  
  // Get current value
  float currentValue = 0;
  if (nutrient == "N") currentValue = currentN;
  else if (nutrient == "P") currentValue = currentP;
  else if (nutrient == "K") currentValue = currentK;
  
  // Calculate target
  manualTargetPPM = currentValue + userInputPPM;
  manualDosingNutrient = nutrient;
  
  doseCycleCount = 0;
  isWaiting = false;
  
  Serial.print("[Inject All] Starting ");
  Serial.print(nutrient);
  Serial.print(": Current=");
  Serial.print(currentValue);
  Serial.print(" Target=");
  Serial.println(manualTargetPPM);
  
  // Set status flag
  Firebase.RTDB.setBool(&fbdo, "/controls/manualInjection/" + nutrient, true);
  // Firebase.RTDB.setBool(&fbdo, "/controls/dose/" + nutrient, true);
  
  startInjectAllDosePulse();
}

void startInjectAllDosePulse() {
  isWaiting = false;
  doseCycleCount++;
  lastDoseTime = millis();
  
  Serial.print("[Inject All] Cycle ");
  Serial.print(doseCycleCount);
  Serial.print(": ");
  
  if (manualDosingNutrient == "N") {
    digitalWrite(PUMP_N_PIN, LOW);
    Serial.println("N pump ON");
  } else if (manualDosingNutrient == "P") {
    digitalWrite(PUMP_P_PIN, LOW);
    Serial.println("P pump ON");
  } else if (manualDosingNutrient == "K") {
    digitalWrite(PUMP_K_PIN, LOW);
    Serial.println("K pump ON");
  }
}

void stopInjectAllDosePulse() {
  digitalWrite(PUMP_N_PIN, HIGH);
  digitalWrite(PUMP_P_PIN, HIGH);
  digitalWrite(PUMP_K_PIN, HIGH);
  
  Serial.println("[Inject All] Pump OFF - waiting...");
  
  isWaiting = true;
  lastDoseTime = millis();
}

void handleInjectAllProcess() {
  if (!injectAllInProgress) return;

  // Check if mode changed
  if (systemMode == "Auto") {
    Serial.println("Mode changed - stopping Inject All");
    completeInjectAll();
    return;
  }
  
  // Get current value
  float currentValue = 0;
  if (manualDosingNutrient == "N") currentValue = currentN;
  else if (manualDosingNutrient == "P") currentValue = currentP;
  else if (manualDosingNutrient == "K") currentValue = currentK;
  
  // Check if current nutrient target reached
  if (currentValue >= (manualTargetPPM - TARGET_TOLERANCE)) {
    Serial.print("[Inject All] ");
    Serial.print(manualDosingNutrient);
    Serial.println(" target reached!");
    
    // Reset flag for this nutrient
    Firebase.RTDB.setBool(&fbdo, "/controls/manualInjection/" + manualDosingNutrient, false);
    
    // Move to next nutrient
    injectAllNutrientIndex++;
    doseCycleCount = 0;
    
    // Find next nutrient with target > 0
    while (injectAllNutrientIndex < 3 && injectAllTargets[injectAllNutrientIndex] <= 0) {
      injectAllNutrientIndex++;
    }
    
    if (injectAllNutrientIndex < 3) {
      startInjectAllNutrient();
    } else {
      completeInjectAll();
    }
    return;
  }
  
  // Safety check
  if (doseCycleCount >= MAX_DOSE_CYCLES) {
    Serial.println("[Inject All] Max cycles for this nutrient!");
    Firebase.RTDB.setBool(&fbdo, "/controls/manualInjection/" + manualDosingNutrient, false);
    
    // Move to next
    injectAllNutrientIndex++;
    doseCycleCount = 0;
    
    while (injectAllNutrientIndex < 3 && injectAllTargets[injectAllNutrientIndex] <= 0) {
      injectAllNutrientIndex++;
    }
    
    if (injectAllNutrientIndex < 3) {
      startInjectAllNutrient();
    } else {
      completeInjectAll();
    }
    return;
  }
  
  // Dosing logic
  if (!isWaiting) {
    if (millis() - lastDoseTime >= DOSE_PULSE_DURATION) {
      stopInjectAllDosePulse();
    }
  } else {
    if (millis() - lastDoseTime >= DOSE_WAIT_TIME) {
      // Re-read
      if (manualDosingNutrient == "N") currentValue = currentN;
      else if (manualDosingNutrient == "P") currentValue = currentP;
      else if (manualDosingNutrient == "K") currentValue = currentK;
      
      Serial.print("[Inject All] After cycle ");
      Serial.print(doseCycleCount);
      Serial.print(": ");
      Serial.print(currentValue);
      Serial.print(" PPM (Target: ");
      Serial.print(manualTargetPPM);
      Serial.println(" PPM)");
      
      if (currentValue < (manualTargetPPM - TARGET_TOLERANCE)) {
        startInjectAllDosePulse();
      }
    }
  }
}

void completeInjectAll() {
  injectAllInProgress = false;
  isWaiting = false;
  
  // Turn off all pumps
  digitalWrite(PUMP_N_PIN, HIGH);
  digitalWrite(PUMP_P_PIN, HIGH);
  digitalWrite(PUMP_K_PIN, HIGH);
  
  Serial.println("\n=== Inject All Complete ===");
  Serial.print("Final N: ");
  Serial.print(currentN);
  Serial.println(" PPM");
  Serial.print("Final P: ");
  Serial.print(currentP);
  Serial.println(" PPM");
  Serial.print("Final K: ");
  Serial.print(currentK);
  Serial.println(" PPM");
  Serial.println("===========================\n");
  
  // Clear all flags
  Firebase.RTDB.setBool(&fbdo, "/controls/manualInjection/N", false);
  Firebase.RTDB.setBool(&fbdo, "/controls/manualInjection/P", false);
  Firebase.RTDB.setBool(&fbdo, "/controls/manualInjection/K", false);
  
  // Clear inject all data
  Firebase.RTDB.deleteNode(&fbdo, "/controls/manualInjection");
  
  injectAllNutrientIndex = 0;
  manualDosingNutrient = "";
  manualTargetPPM = 0;
  doseCycleCount = 0;
}

//------------------------- Automatic Dosing Logic -------------------------
void startAutoDosePulse() {
  isWaiting = false;
  doseCycleCount++;
  lastDoseTime = millis();
  
  Serial.print("[Auto Dose] Cycle ");
  Serial.print(doseCycleCount);
  Serial.print(": ");
  
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

void stopAutoDosePulse() {
  digitalWrite(PUMP_N_PIN, HIGH);
  digitalWrite(PUMP_P_PIN, HIGH);
  digitalWrite(PUMP_K_PIN, HIGH);
  
  Serial.println("[Auto Dose] Pump OFF - waiting...");
  
  isWaiting = true;
  lastDoseTime = millis();
}

void handleAutoDosingProcess() {
  if (!autoDosingInProgress) return;
  
  float currentValue = 0;
  float targetValue = 0;
  
  if (autoDosingNutrient == "N") {
    currentValue = currentN;
    targetValue = currentTargets.N;
  } else if (autoDosingNutrient == "P") {
    currentValue = currentP;
    targetValue = currentTargets.P;
  } else if (autoDosingNutrient == "K") {
    currentValue = currentK;
    targetValue = currentTargets.K;
  }
  
  // Check if target reached
  if (currentValue >= (targetValue - TARGET_TOLERANCE)) {
    Serial.print("[Auto Dose] ");
    Serial.print(autoDosingNutrient);
    Serial.print(" target reached! Current: ");
    Serial.print(currentValue);
    Serial.print(" Target: ");
    Serial.println(targetValue);
    
    // Move to next nutrient
    autoDosingNutrientIndex++;
    doseCycleCount = 0;
    
    if (autoDosingNutrientIndex == 1) {
      autoDosingNutrient = "P";
      Serial.println("[Schedule] Phase 2: Dosing Phosphorus");
      startAutoDosePulse();
      return;
    } else if (autoDosingNutrientIndex == 2) {
      autoDosingNutrient = "K";
      Serial.println("[Schedule] Phase 3: Dosing Potassium");
      startAutoDosePulse();
      return;
    } else if (autoDosingNutrientIndex >= 3) {
      completeAutoDosing();
      return;
    }
  }
  
  // Dosing logic
  if (!isWaiting) {
    if (millis() - lastDoseTime >= DOSE_PULSE_DURATION) {
      stopAutoDosePulse();
    }
  } else {
    if (millis() - lastDoseTime >= DOSE_WAIT_TIME) {
      // Re-read sensor
      if (autoDosingNutrient == "N") currentValue = currentN;
      else if (autoDosingNutrient == "P") currentValue = currentP;
      else if (autoDosingNutrient == "K") currentValue = currentK;
      
      Serial.print("[Auto Dose] After cycle ");
      Serial.print(doseCycleCount);
      Serial.print(": ");
      Serial.print(autoDosingNutrient);
      Serial.print(" = ");
      Serial.print(currentValue);
      Serial.print(" PPM (Target: ");
      Serial.print(targetValue);
      Serial.println(" PPM)");
      
      if (currentValue < (targetValue - TARGET_TOLERANCE)) {
        startAutoDosePulse();
      }
    }
  }
}

void completeAutoDosing() {
  autoDosingInProgress = false;
  isWaiting = false;
  
  // Turn off all pumps
  digitalWrite(PUMP_N_PIN, HIGH);
  digitalWrite(PUMP_P_PIN, HIGH);
  digitalWrite(PUMP_K_PIN, HIGH);
  
  unsigned long totalTime = (millis() - autoDosingStartTime) / 1000;
  
  Serial.println("\n=== Automatic NPK Injection Complete ===");
  Serial.print("Total time: ");
  Serial.print(totalTime);
  Serial.println(" seconds");
  Serial.print("Final - N: ");
  Serial.print(currentN);
  Serial.print(" P: ");
  Serial.print(currentP);
  Serial.print(" K: ");
  Serial.println(currentK);
  Serial.println("=========================================\n");
  
  autoDosingNutrient = "";
  autoDosingNutrientIndex = 0;
  doseCycleCount = 0;
}