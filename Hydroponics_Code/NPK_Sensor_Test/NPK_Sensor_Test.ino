/*
 * ESP32 NPK Sensor Reader via RS485
 * Based on working Arduino code - uses addresses 0x001E, 0x001F, 0x0020
 * 
 * Hardware Connections:
 * ESP32 GPIO32 (RX) -> RX on RS485 module
 * ESP32 GPIO33 (TX) -> TX on RS485 module
 * ESP32 3.3V -> VCC on RS485 module
 * ESP32 GND -> GND on RS485 module
 * 
 * RS485 A/B -> NPK Sensor A/B (Yellow/Blue wires)
 * NPK Sensor: Brown(+), Black(GND), Yellow(A), Blue(B)
 */

#define RXD2 16
#define TXD2 17

// Sensor settings
#define SENSOR_ADDRESS 0x01
#define SENSOR_BAUD 4800

// Modbus function code
#define MODBUS_READ_HOLDING 0x03

// Working NPK register addresses (from functional code)
#define REG_NPK_START 0x001E  // Start address for N, P, K
#define REG_NPK_COUNT 3       // Read 3 registers

void setup() {
  Serial.begin(115200);  // Match the original code's Serial Monitor baud
  Serial2.begin(SENSOR_BAUD, SERIAL_8N1, RXD2, TXD2);
  
  delay(2000);
  
  Serial.println("NPK Sensor Test");
  Serial.println("Reading from registers 0x001E, 0x001F, 0x0020");
  Serial.println("=====================================\n");
  
  delay(1000);
}

void loop() {
  uint16_t data[3];
  
  // Try reading up to 3 times before reporting error
  bool success = false;
  for (int attempt = 0; attempt < 3; attempt++) {
    if (readNPKRegisters(data)) {
      success = true;
      break;
    }
    delay(100); // Short delay between retry attempts
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
  } else {
    Serial.println("Modbus Error: Failed to read NPK values after 3 attempts");
  }
  
  delay(2000); // Read every 2 seconds
  
  // Additional delay for sensor recovery
  delay(500);
}

// Read NPK registers (0x001E, 0x001F, 0x0020)
bool readNPKRegisters(uint16_t* data) {
  // Build Modbus RTU query
  uint8_t query[8];
  query[0] = SENSOR_ADDRESS;
  query[1] = MODBUS_READ_HOLDING;
  query[2] = (REG_NPK_START >> 8) & 0xFF;  // Start address high byte
  query[3] = REG_NPK_START & 0xFF;         // Start address low byte
  query[4] = 0x00;                         // Number of registers high byte
  query[5] = REG_NPK_COUNT;                // Number of registers low byte (3)
  
  // Calculate CRC
  uint16_t crc = calculateCRC(query, 6);
  query[6] = crc & 0xFF;        // CRC low byte
  query[7] = (crc >> 8) & 0xFF; // CRC high byte
  
  // Clear receive buffer thoroughly
  while (Serial2.available()) Serial2.read();
  delay(50);  // Longer delay after clearing
  
  // Send query
  Serial2.write(query, 8);
  Serial2.flush();
  delay(50);  // Small delay after sending
  
  // Wait for response (increased timeout)
  unsigned long startTime = millis();
  while (Serial2.available() < 11 && (millis() - startTime) < 1500) {
    delay(10);
  }
  
  // Expected response length: Address(1) + Function(1) + ByteCount(1) + Data(6) + CRC(2) = 11 bytes
  int expectedLen = 11;
  
  if (Serial2.available() < expectedLen) {
    Serial.print("Available bytes: ");
    Serial.println(Serial2.available());
    return false;
  }
  
  // Read response
  uint8_t response[20];
  int len = 0;
  while (Serial2.available() && len < 20) {
    response[len++] = Serial2.read();
  }
  
  // Verify response
  if (len < expectedLen) {
    return false;
  }
  
  if (response[0] != SENSOR_ADDRESS || response[1] != MODBUS_READ_HOLDING) {
    return false;
  }
  
  // Verify CRC
  uint16_t receivedCRC = response[len - 2] | (response[len - 1] << 8);
  uint16_t calculatedCRC = calculateCRC(response, len - 2);
  
  if (receivedCRC != calculatedCRC) {
    return false;
  }
  
  // Extract NPK data
  // Response format: [addr][func][bytecount][N_high][N_low][P_high][P_low][K_high][K_low][CRC_low][CRC_high]
  data[0] = (response[3] << 8) | response[4];  // Nitrogen
  data[1] = (response[5] << 8) | response[6];  // Phosphorus
  data[2] = (response[7] << 8) | response[8];  // Potassium
  
  return true;
}

// Calculate Modbus CRC16
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