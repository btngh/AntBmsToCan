/*
===================================================================================
             SƠ ĐỒ ĐẤU NỐI PHẦN CỨNG AN TOÀN CHO CHIP ESP32-S3
===================================================================================
1. KẾT NỐI ESP32-S3 VỚI MODULE CAN BUS MCP2515 (Giao tiếp SPI thạch anh 8MHz)
   - VCC  -> 5V / VBUS trên ESP32-S3
   - GND  -> GND chung
   - CS   -> GPIO 10
   - SO (MISO) -> GPIO 13
   - SI (MOSI) -> GPIO 11
   - SCK  -> GPIO 12
   - INT  -> GPIO 14
2. KẾT NỐI ESP32-S3 VỚI MẠCH ANTBMS (Giao tiếp Serial / UART1 AN TOÀN)
   - TX AntBMS -> GPIO 16 (RX1 của ESP32-S3)
   - RX AntBMS -> GPIO 15 (TX1 của ESP32-S3)
   - GND       -> GND chung (Bắt buộc nối mass chung)
===================================================================================
*/

#include <SPI.h>
#include <mcp_can.h>
#include <HardwareSerial.h>

// BẬT WIFI VÀ MQTT
#define USE_WIFI_AND_MQTT

#ifdef USE_WIFI_AND_MQTT
#include <WiFi.h>
#include <PubSubClient.h>

#define WIFI_SSID       "ASUS_90_2G"
#define WIFI_PASSWORD   "116811681168"
#define MQTT_SERVER     "192.168.1.176"
#define MQTT_PORT       1883
#define MQTT_USERNAME   "battery" 
#define MQTT_PASSWORD   "Switch1"
#define DEVICE_NAME     "AntBmsToCan"
#endif

#define CHARGE_VOLTAGE_LIMIT_CVL_IN_MILLIVOLTS 3550
#define CHARGE_CURRENT_LIMIT_IN_TENTHS_OF_AN_AMP 260
#define DISCHARGE_CURRENT_LIMIT_IN_TENTHS_OF_AN_AMP 260
#define DISCHARGE_VOLTAGE_LIMIT_DVL_IN_MILLIVOLTS 2900
#define BMS_QUERY_INTERVAL 1000

#define BMS_TIMEOUT 250
#define USE_FIXED_MESSAGE_FOR_DEBUGGING true
#define WHEN_BMS_READ_FAILED_PRINT_VALUES_TO_SERIAL_AND_SEND_TO_MQTT_IF_USING_ANYWAY false

#ifdef USE_WIFI_AND_MQTT
#define MAX_MQTT_PAYLOAD_SIZE 4096
#define MIN_MQTT_PAYLOAD_SIZE 512
#define MQTT_HEADER_SIZE 512
#endif

#define MCP2515_CS 10
#define MCP2515_INT 14

// ĐỊNH NGHĨA CHÂN UART AN TOÀN CHO ESP32-S3 (TRÁNH CHÂN BOOT 17, 18)
#define BMS_RX_PIN 16
#define BMS_TX_PIN 15
#define BMS_BAUD_RATE 19200 
#define ESP_DEBUGGING_BAUD_RATE 115200

#define MAX_CELLS_SUPPORTED_BY_BMS 30
#define MAX_TEMPERATURE_SENSORS_SUPPORTED_BY_BMS 6
#define TEMPERATURE_MOSFET 0
#define TEMPERATURE_SENSOR_1 2
#define BMS_MESSAGE_LENGTH 140
enum SystemStatus { NORMAL, FAULT };
enum CanAliveStatus { CAN_STILL_ALIVE, CAN_DEAD };

struct BatteryStatus {
  uint16_t voltage_dV;
  int32_t reported_current_dA;
  int16_t temperature_max_dC;
  int16_t temperature_min_dC;
  uint32_t reported_soc;
  uint32_t soh_pptt;
  uint16_t cell_max_voltage_mV;
  uint16_t cell_min_voltage_mV;
  uint16_t max_charge_current_dA;
  uint16_t max_discharge_current_dA;
};

struct SysStatusStruct {
  SystemStatus system_status;
  CanAliveStatus CAN_inverter_still_alive;
};

struct DatalayerStruct {
  BatteryStatus battery;
  SysStatusStruct system;
};

DatalayerStruct datalayer;
uint16_t charge_cutoff_voltage_dV = 0;
uint16_t discharge_cutoff_voltage_dV = 0;
uint16_t user_selected_inverter_cells = 22; 
uint16_t user_selected_inverter_modules = 1;
uint16_t user_selected_inverter_cells_per_module = 22;
uint16_t user_selected_inverter_voltage_level = 480;
uint16_t user_selected_inverter_ah_capacity = 100;

// SỬ DỤNG UART1 CHO CHIP ESP32-S3
HardwareSerial _bms(1); 

unsigned long _bmsValidResponseCounter = 0;
unsigned long _bmsInvalidResponseCounter = 0;
unsigned long _canSuccessCounter = 0;
unsigned long _canFailureCounter = 0;

#ifdef USE_WIFI_AND_MQTT
WiFiClient _wifi;
PubSubClient _mqtt(_wifi);
int _bufferSize;
int _maxPayloadSize;
char* _mqttPayload;
#endif

struct bmsResponse {
  uint16_t rawTotalVoltage;
  float totalVoltage;
  uint8_t cells;
  float cellVoltage[MAX_CELLS_SUPPORTED_BY_BMS];
  uint32_t rawCurrent;
  float current;
  uint8_t rawSoc;
  float soc;
  float temperatures[MAX_TEMPERATURE_SENSORS_SUPPORTED_BY_BMS];
  float maxCellVoltage;
  float minCellVoltage;
};
bmsResponse _receivedResponse;

MCP_CAN CAN0(MCP2515_CS);
// ======================== PORT HELPER FUNCTIONS ========================
static inline void write16_be(uint8_t *buf, uint16_t val) {
  buf[0] = (uint8_t)((val >> 8) & 0xFF);
  buf[1] = (uint8_t)(val & 0xFF);
}
static inline void write16_le(uint8_t *buf, uint16_t val) {
  buf[0] = (uint8_t)(val & 0xFF);
  buf[1] = (uint8_t)((val >> 8) & 0xFF);
}
static inline void write16(uint8_t *buf, uint16_t val) {
  write16_be(buf, val);
}
static inline uint16_t clamp16(int32_t v) {
  if (v < 0) return 0;
  if (v > 0xFFFF) return 0xFFFF;
  return (uint16_t)v;
}

// ======================== BUILDERS FOR SOLXPOW FRAMES ========================
void build_frame_4210(uint8_t *data) {
  write16(&data[0], clamp16((int32_t)datalayer.battery.voltage_dV));
  write16(&data[2], (uint16_t)((int16_t)datalayer.battery.reported_current_dA));
  write16(&data[4], clamp16((int32_t)(datalayer.battery.temperature_max_dC + 1000)));
  data[6] = (uint8_t)constrain((int)round(datalayer.battery.reported_soc / 100.0f), 0, 100);
  data[7] = (uint8_t)constrain((int)round(datalayer.battery.soh_pptt / 100.0f), 0, 100);
}

void build_frame_4220(uint8_t *data) {
  write16(&data[0], clamp16((int32_t)charge_cutoff_voltage_dV));
  write16(&data[2], clamp16((int32_t)discharge_cutoff_voltage_dV));
  write16(&data[4], clamp16((int32_t)datalayer.battery.max_charge_current_dA));
  write16(&data[6], clamp16((int32_t)datalayer.battery.max_discharge_current_dA));
}

void build_frame_4230(uint8_t *data) {
  write16(&data[0], clamp16((int32_t)datalayer.battery.cell_max_voltage_mV));
  write16(&data[2], clamp16((int32_t)datalayer.battery.cell_min_voltage_mV));
  data[4] = data[5] = data[6] = data[7] = 0;
}

void build_frame_4240(uint8_t *data) {
  write16(&data[0], clamp16((int32_t)datalayer.battery.temperature_max_dC));
  write16(&data[2], clamp16((int32_t)datalayer.battery.temperature_min_dC));
  data[4] = data[5] = data[6] = data[7] = 0;
}

void build_frame_4250(uint8_t *data) {
  uint8_t status = 0x03;
  if (datalayer.system.system_status == FAULT) status = 0x00;
  else if (datalayer.battery.reported_current_dA < 0) status = 0x01;
  else if (datalayer.battery.reported_current_dA > 0) status = 0x02;
  data[0] = status;
  data[1] = data[2] = data[3] = data[4] = data[5] = data[6] = data[7] = 0;
}

void build_frame_7320(uint8_t *data) {
  memset((void*)data, 0, 8);
  if (user_selected_inverter_cells > 0) write16(&data[0], user_selected_inverter_cells);
  data[2] = (uint8_t)user_selected_inverter_modules;
  data[3] = (uint8_t)user_selected_inverter_cells_per_module;
  if (user_selected_inverter_voltage_level > 0) write16(&data[4], user_selected_inverter_voltage_level);
  if (user_selected_inverter_ah_capacity > 1) write16(&data[6], user_selected_inverter_ah_capacity);
}
// ======================== WIFI & MQTT MANAGEMENT ========================
#ifdef USE_WIFI_AND_MQTT
void setupWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print("\nConnecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(500); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) Serial.println("\nWiFi Connected Successfully!");
}

void mqttReconnect() {
  while (!_mqtt.connected() && WiFi.status() == WL_CONNECTED) {
    Serial.print("Attempting MQTT connection...");
    if (_mqtt.connect(DEVICE_NAME, MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.println("Connected to MQTT Broker!");
    } else {
      Serial.printf("Failed, state=%d. Retry in 2 seconds...\n", _mqtt.state());
      delay(2000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {}

void printValuesToSerialAndSendToMQTTIfUsing() {
  Serial.printf("--- AntBMS Formatted --- Volt: %.2fV, Curr: %.2fA, SoC: %.1f%%\n", 
                _receivedResponse.totalVoltage, _receivedResponse.current, _receivedResponse.soc);
  if (!_mqtt.connected()) return;
  if (_mqttPayload != NULL) {
    memset(_mqttPayload, 0, _maxPayloadSize);
    snprintf(_mqttPayload, _maxPayloadSize, 
      "{\"v\":%.1f,\"c\":%.1f,\"soc\":%.1f,\"t1\":%.1f,\"vMax\":%.3f,\"vMin\":%.3f,\"cells\":%d}",
      _receivedResponse.totalVoltage, _receivedResponse.current, _receivedResponse.soc,
      _receivedResponse.temperatures[TEMPERATURE_MOSFET], _receivedResponse.maxCellVoltage, 
      _receivedResponse.minCellVoltage, _receivedResponse.cells);
    _mqtt.publish("tele/AntBmsToCan/INFO", _mqttPayload);
  }
}
#else
void printValuesToSerialAndSendToMQTTIfUsing() {
  Serial.printf("--- AntBMS Formatted --- Volt: %.2fV, Curr: %.2fA, SoC: %.1f%%\n", 
                _receivedResponse.totalVoltage, _receivedResponse.current, _receivedResponse.soc);
}
#endif

void printCanResultToSerialMCP(unsigned long id, byte canResult) {
  if (canResult != CAN_OK) Serial.printf("CAN Packet 0x%08lX, Error: %d\n", id, canResult);
}

uint16_t calcChecksum(const uint8_t data[], const uint16_t len) {
  uint16_t checksum = 0;
  for (uint16_t i = 4; i < len; i++) { checksum += data[i]; }
  return checksum;
}
// ======================== TRANSMIT CAN MESSAGE ========================
bool sendCanMessage() {
  bool result = true;
  uint8_t data[8];
  byte canResult;

  build_frame_7320(data);
  canResult = CAN0.sendMsgBuf(0x7320, 1, 8, data);
  if (canResult != CAN_OK) { result = false; printCanResultToSerialMCP(0x7320, canResult); }

  build_frame_4210(data);
  canResult = CAN0.sendMsgBuf(0x4210, 1, 8, data);
  if (canResult != CAN_OK) { result = false; printCanResultToSerialMCP(0x4210, canResult); }

  build_frame_4220(data);
  canResult = CAN0.sendMsgBuf(0x4220, 1, 8, data);
  if (canResult != CAN_OK) { result = false; printCanResultToSerialMCP(0x4220, canResult); }

  build_frame_4230(data);
  canResult = CAN0.sendMsgBuf(0x4230, 1, 8, data);
  if (canResult != CAN_OK) { result = false; printCanResultToSerialMCP(0x4230, canResult); }

  build_frame_4240(data);
  canResult = CAN0.sendMsgBuf(0x4240, 1, 8, data);
  if (canResult != CAN_OK) { result = false; printCanResultToSerialMCP(0x4240, canResult); }

  build_frame_4250(data);
  canResult = CAN0.sendMsgBuf(0x4250, 1, 8, data);
  if (canResult != CAN_OK) { result = false; printCanResultToSerialMCP(0x4250, canResult); }

  return result;
}

// ======================== LUỒNG ĐỌC VÀ ĐỒNG BỘ DATA BMS ========================
void readBms() {
  bool goodCrc = false; bool goodHeader = true;
  const byte requestCommand[] = { 0x5A, 0x5A, 0x00, 0x00, 0x01, 0x01 };
  const byte startMark[] = { 0xAA, 0x55, 0xAA, 0xFF };
  byte incomingBuffer[BMS_MESSAGE_LENGTH] = { 0 };
  byte fixedTestMessage[BMS_MESSAGE_LENGTH] = { 0xAA, 0x55, 0xAA, 0xFF, 0x02, 0x30, 0x09, 0xE4, 0x09, 0xE5, 0x09, 0xE5, 0x09, 0xE4, 0x09, 0xE6, 0x09, 0xE6, 0x09, 0xC4, 0x09, 0xE8, 0x09, 0xE8, 0x09, 0xE9, 0x09, 0xE8, 0x09, 0xE9, 0x09, 0xFE, 0x0A, 0x0B, 0x0A, 0x05, 0x0A, 0x09, 0x0A, 0x06, 0x0A, 0x0D, 0x09, 0xDE, 0x0A, 0x0A, 0x0A, 0x04, 0x0A, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2B, 0x63, 0x07, 0x27, 0x0E, 0x00, 0x06, 0xF4, 0xFA, 0x25, 0x00, 0xE8, 0xAF, 0xE2, 0x01, 0xFF, 0xC9, 0x8B, 0x00, 0x14, 0x00, 0x13, 0x00, 0x11, 0x00, 0x11, 0x00, 0x12, 0x00, 0x12, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xF0, 0x12, 0x0A, 0x0D, 0x07, 0x09, 0xC4, 0x09, 0xF1, 0x16, 0xFF, 0xFF, 0x00, 0x7E, 0x00, 0x7A, 0x02, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1D, 0x8D };
  size_t bytesReceived = 0;

  auto ant_get_16bit = [&](size_t i) -> uint16_t {
    return (uint16_t(incomingBuffer[i + 0]) << 8) | (uint16_t(incomingBuffer[i + 1]) << 0);
  };
  auto ant_get_32bit = [&](size_t i) -> uint32_t {
    return (uint32_t(ant_get_16bit(i + 0)) << 16) | (uint32_t(ant_get_16bit(i + 2)) << 0);
  };

  _bms.flush();
  while (_bms.available()) { _bms.read(); }
  _bms.write(requestCommand, sizeof(requestCommand));

  if (USE_FIXED_MESSAGE_FOR_DEBUGGING) {
    delay(10); memcpy(incomingBuffer, fixedTestMessage, BMS_MESSAGE_LENGTH); bytesReceived = BMS_MESSAGE_LENGTH;
  } else {
    bytesReceived = _bms.readBytes(incomingBuffer, BMS_MESSAGE_LENGTH);
  }

  if (bytesReceived == BMS_MESSAGE_LENGTH) {
    for (int i = 0; i < 4; i++) { if (incomingBuffer[i] != startMark[i]) goodHeader = false; }
    if (goodHeader) {
      uint16_t crcCalculated = calcChecksum(incomingBuffer, BMS_MESSAGE_LENGTH - 2);
      uint16_t crcReceived = ant_get_16bit(BMS_MESSAGE_LENGTH - 2);
      if (crcCalculated == crcReceived) goodCrc = true;
    }
  }

  if (goodCrc) {
    _bmsValidResponseCounter++;
    _receivedResponse.rawTotalVoltage = ant_get_16bit(4);
    _receivedResponse.totalVoltage = _receivedResponse.rawTotalVoltage / 10.0;
    _receivedResponse.rawCurrent = ant_get_32bit(70);
    _receivedResponse.current = ((int32_t)_receivedResponse.rawCurrent) / 10.0;
    _receivedResponse.rawSoc = incomingBuffer[74];
    _receivedResponse.soc = _receivedResponse.rawSoc;
    _receivedResponse.maxCellVoltage = ant_get_16bit(116) / 1000.0;
    _receivedResponse.minCellVoltage = ant_get_16bit(119) / 1000.0;
    _receivedResponse.cells = incomingBuffer[122];
    _receivedResponse.temperatures[TEMPERATURE_MOSFET] = (int16_t)ant_get_16bit(82);
    _receivedResponse.temperatures[TEMPERATURE_SENSOR_1] = (int16_t)ant_get_16bit(86);

    // MAPPING SANG CẤU TRÚC CAN BUS SOLXPOW
    datalayer.battery.voltage_dV = (uint16_t)round(_receivedResponse.totalVoltage * 10.0);
    datalayer.battery.reported_current_dA = (int32_t)round(_receivedResponse.current * 10.0);
    datalayer.battery.reported_soc = (uint32_t)round(_receivedResponse.soc * 100.0);
    datalayer.battery.soh_pptt = 10000;
    datalayer.battery.cell_max_voltage_mV = (uint16_t)round(_receivedResponse.maxCellVoltage * 1000.0);
    datalayer.battery.cell_min_voltage_mV = (uint16_t)round(_receivedResponse.minCellVoltage * 1000.0);
    datalayer.battery.temperature_max_dC = (int16_t)round(_receivedResponse.temperatures[TEMPERATURE_MOSFET] * 10.0);
    datalayer.battery.temperature_min_dC = (int16_t)round(_receivedResponse.temperatures[TEMPERATURE_SENSOR_1] * 10.0);
    datalayer.battery.max_charge_current_dA = CHARGE_CURRENT_LIMIT_IN_TENTHS_OF_AN_AMP;
    datalayer.battery.max_discharge_current_dA = DISCHARGE_CURRENT_LIMIT_IN_TENTHS_OF_AN_AMP;
    
    charge_cutoff_voltage_dV = (uint16_t)((CHARGE_VOLTAGE_LIMIT_CVL_IN_MILLIVOLTS * _receivedResponse.cells) / 100);
    discharge_cutoff_voltage_dV = (uint16_t)((DISCHARGE_VOLTAGE_LIMIT_DVL_IN_MILLIVOLTS * _receivedResponse.cells) / 100);
    if (_receivedResponse.rawSoc == 0) datalayer.system.system_status = FAULT; else datalayer.system.system_status = NORMAL;

    Serial.printf("\n[DEBUG-CAN] totalVoltage=%f, current=%f, soc=%f, maxCell=%f, minCell=%f\n", 
                  _receivedResponse.totalVoltage, _receivedResponse.current, _receivedResponse.soc, 
                  _receivedResponse.maxCellVoltage, _receivedResponse.minCellVoltage);
  } else {
    _bmsInvalidResponseCounter++;
  }

  printValuesToSerialAndSendToMQTTIfUsing();
  const bool canResult = sendCanMessage();
  if (canResult) _canSuccessCounter++; else _canFailureCounter++;
}
// ======================== SETUP & LOOP FUNCTIONS ========================
void setup() {
  Serial.begin(ESP_DEBUGGING_BAUD_RATE);
  delay(1000); 
  Serial.println("\n--- HỆ THỐNG KHỞI ĐỘNG (GIỮ CHÂN CŨ) ---");
  
  _bms.begin(BMS_BAUD_RATE, SERIAL_8N1, BMS_RX_PIN, BMS_TX_PIN);
  _bms.setTimeout(BMS_TIMEOUT);
  pinMode(LED_BUILTIN, OUTPUT);

#ifdef USE_WIFI_AND_MQTT
  setupWifi();
  _mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  _bufferSize = (MAX_MQTT_PAYLOAD_SIZE + MQTT_HEADER_SIZE);
  if (_mqtt.setBufferSize(_bufferSize)) {
    _maxPayloadSize = _bufferSize - MQTT_HEADER_SIZE;
    _mqttPayload = new char[_maxPayloadSize];
    memset(_mqttPayload, 0, _maxPayloadSize);
  }
  mqttReconnect();
#endif

  // GIỮ NGUYÊN CỤM CHÂN CŨ CỦA BẠN (12, 13, 11, 10)
  SPI.begin(12, 13, 11, 10); 
  
  // SỬA: Ép hạ tốc độ xung SPI xuống 1MHz để mạch đệm YF08F chạy kịp, không bị méo sóng
  SPI.setClockDivider(SPI_CLOCK_DIV8); 
  
  Serial.println("Dang quet thiet bi CAN MCP2515...");
  byte errorCode = CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ);
  
  // SỬA: Giới hạn thử lại 5 lần, nếu lỗi vẫn cho chạy tiếp để tránh treo cứng Serial
  int retryCount = 0;
  while (errorCode != CAN_OK && retryCount < 5) {
    Serial.printf(" CAN Lỗi cấu hình: 0x%X. Thử lại...\n", errorCode);
    delay(300);
    errorCode = CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ);
    retryCount++;
  }
  
  if (errorCode == CAN_OK) {
    CAN0.enOneShotTX();
    CAN0.setMode(MCP_NORMAL);
    pinMode(MCP2515_INT, INPUT_PULLUP);
    Serial.println(" [OK] MCP2515 Khởi tạo thành công!");
  } else {
    Serial.println(" [CẢNH BÁO] Không kết nối được MCP2515. Hãy kiểm tra chân OE mạch đệm!");
  }
}
