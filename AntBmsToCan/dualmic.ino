#include <SPI.h>
#include <mcp_can.h>

// =================================================================
// TỰ ĐỘNG KHAI BÁO THƯ VIỆN & CỔNG THEO CHIP (CẤM SỬA ĐOẠN NÀY)
// =================================================================
#ifdef ESP8266
  #include <ESP8266WiFi.h>
  #include <WiFiClient.h>
  #include <ESP8266WebServer.h>
  #include <ESP8266HTTPUpdateServer.h>
  #include <SoftwareSerial.h>
  #define BMS_RX_PIN 5    // Chân RX nối D1 (GPIO5) của NodeMCU
  #define BMS_TX_PIN 4    // Chân TX nối D2 (GPIO4) của NodeMCU
  #define MCP2515_CS 15   // Chân CS nối D8 (GPIO15) của NodeMCU
  SoftwareSerial _bms(BMS_RX_PIN, BMS_TX_PIN);
  ESP8266WebServer _httpServer(80);
  ESP8266HTTPUpdateServer _httpUpdater;
#else
  #include <WiFi.h>
  #include <WebServer.h>
  #include <HTTPUpdateServer.h>
  #include <HardwareSerial.h>
  #define MCP2515_CS 5    // Chân CS nối chân số 5 của ESP32
  HardwareSerial _bms(2); // Dùng UART2 (Chân 16 RX2, Chân 17 TX2) của ESP32
  WebServer _httpServer(80);
  HTTPUpdateServer _httpUpdater;
#endif

#include <PubSubClient.h>

// ==========================================
// CẤU HÌNH WIFI VÀ MQTT BROKER CỦA BẠN
// ==========================================
#define WIFI_SSID "ASUS_90_2G"
#define WIFI_PASSWORD "116811681168"

#define MQTT_SERVER "10.0.0.176"
#define MQTT_PORT 1883
#define MQTT_USERNAME "mqqt_solintek"
#define MQTT_PASSWORD "155678"
#define DEVICE_NAME "AntBmsToCan1"

#define BMS_QUERY_INTERVAL 1000
#define MQTT_SEND_INTERVAL 10000 
#define BMS_TIMEOUT 250
#define CAN_ADDR 1

#define BMS_BAUD_RATE 19200
#define ESP_DEBUGGING_BAUD_RATE 115200

#define MAX_CELLS_SUPPORTED_BY_BMS 30
#define MAX_TEMPERATURE_SENSORS_SUPPORTED_BY_BMS 6
#define BMS_MESSAGE_LENGTH 140

#define TEMPERATURE_MOSFET 0
#define TEMPERATURE_BALANCER 1
#define TEMPERATURE_SENSOR_1 2

WiFiClient _wifiClient;
PubSubClient _mqttClient(_wifiClient);

unsigned long _bmsValidResponseCounter = 0;
unsigned long _bmsInvalidResponseCounter = 0;
unsigned long _canSuccessCounter = 0;
unsigned long _canFailureCounter = 0;
unsigned long _lastMqttSent = 0;
struct bmsResponse
{
  uint16_t rawTotalVoltage;
  float totalVoltage;
  uint8_t cells;
  float cellVoltage[MAX_CELLS_SUPPORTED_BY_BMS];
  uint32_t rawCurrent;
  float current;
  float rawCurrentAsFloat;
  uint8_t rawSoc;
  float soc;
  float totalBatteryCapacitySetting;
  float capacityRemaining;
  float batteryCycleCapacity;
  float totalRuntime;
  uint16_t rawTemperatureMosfet;
  float temperatures[MAX_TEMPERATURE_SENSORS_SUPPORTED_BY_BMS];
  uint8_t chargeMosfetStatus;
  bool chargingSwitch;
  uint8_t dischargeMosfetStatus;
  bool dischargingSwitch;
  uint8_t balancerStatus;
  bool balancerSwitch;
  float power;
  uint8_t cellWithHighestVoltage;
  uint16_t rawMaxCellVoltage;
  float maxCellVoltage;
  uint8_t cellWithLowestVoltage;
  uint16_t rawMinCellVoltage;
  float minCellVoltage;
  float deltaCellVoltage;
  float averageCellVoltage;
  uint8_t batteryStrings;
};

bmsResponse _receivedResponse;
MCP_CAN CAN0(MCP2515_CS);

uint8_t lowByteOfUint16T(uint16_t input) { return input & 0xff; }
uint8_t highByteOfUint16T(uint16_t input) { return (input >> 8) & 0xff; }
void reconnectNetwork() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Connecting to WiFi: "); Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 5000) {
      delay(500);
      Serial.print(".");
    }
    if(WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi Connected! IP Address: ");
      Serial.println(WiFi.localIP());
    }
    return;
  }
  
  if (!_mqttClient.connected()) {
    Serial.print("Connecting to MQTT Server...");
    if (_mqttClient.connect(DEVICE_NAME, MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc="); Serial.println(_mqttClient.state());
    }
  }
}
void sendMqttData() {
  if (!_mqttClient.connected()) return;

  char topic[64];
  char payload[512]; 

  snprintf(topic, sizeof(topic), "%s/bms/details", DEVICE_NAME);
  
  snprintf(payload, sizeof(payload),
    "{\"volt\":%.1f,\"curr\":%.1f,\"soc\":%d,\"max_c_v\":%.3f,\"min_c_v\":%.3f,\"t_mos\":%.1f,\"valid_msg\":%lu,\"can_ok\":%lu}",
    _receivedResponse.totalVoltage,
    _receivedResponse.current,
    _receivedResponse.rawSoc,
    _receivedResponse.maxCellVoltage,
    _receivedResponse.minCellVoltage,
    _receivedResponse.temperatures[TEMPERATURE_MOSFET],
    _bmsValidResponseCounter,
    _canSuccessCounter
  );

  _mqttClient.publish(topic, payload);
  Serial.println("MQTT Payload sent safely.");
}

void printCanResultToSerialMCP(unsigned long id, byte canResult) {
  Serial.print("CAN Transmit Error on ID 0x"); Serial.print(id, HEX); Serial.print(" Code: "); Serial.println(canResult);
}
bool sendCanMessage()
{
  bool result = true;
  uint8_t data[8] = { 0 };
  byte canResult;
  unsigned long id;
  byte ext = 1; // BẮT BUỘC: Khung mở rộng 29-bit (Extended ID) chuẩn hóa dải áp cao

  // --- Khối 1: ID 0x187310F0 (Bắt tay phiên bản phần cứng - Đúng 7 Byte) ---
  if (result) {
    delay(2);
    id = 0x187310F0; 
    uint8_t d1[7] = {1, 0, 0x02, 0x01, 0x01, 0x02, 0}; 
    canResult = CAN0.sendMsgBuf(id, ext, 7, d1);
    result = (canResult == CAN_OK);
  }

  // --- Khối 2: ID 0x187320F0 (Bắt tay cấu hình dung lượng pin - Đúng 6 Byte) ---
  if (result) {
    delay(2);
    id = 0x187320F0; 
    uint8_t d2[6] = {0x1E, 0x00, 0x01, 0x1E, 0x2D, 0x00}; 
    canResult = CAN0.sendMsgBuf(id, ext, 6, d2);
    result = (canResult == CAN_OK);
  }
  // --- Khối 3: ID 0x184210F0 (Điện áp tổng HV, Dòng điện kèm Offset -3000A, SoC, SoH) ---
  if (result) {
    delay(2);
    id = 0x184210F0; 
    
    // Quy đổi điện áp thô AntBMS sang đơn vị 0.1V hệ HV
    uint16_t totalVolt_01V = (uint16_t)(_receivedResponse.totalVoltage * 10.0); 
    
    // Thuật toán dịch dải dòng điện áp cao bắt buộc: (Dòng thực tế + 3000) * 10
    float current_real = _receivedResponse.current; 
    uint16_t current_can = (uint16_t)((current_real + 3000.0) * 10.0);

    uint16_t t_bms = (uint16_t)(_receivedResponse.temperatures[TEMPERATURE_MOSFET] + 100.0) * 10;

    memset(data, 0, 8);
    data[0] = lowByteOfUint16T(totalVolt_01V);  data[1] = highByteOfUint16T(totalVolt_01V);
    data[2] = lowByteOfUint16T(current_can);    data[3] = highByteOfUint16T(current_can);
    data[4] = lowByteOfUint16T(t_bms);          data[5] = highByteOfUint16T(t_bms);
    data[6] = _receivedResponse.rawSoc;         // Nhận % pin thực tế hiển thị lên Inverter
    data[7] = 100;                              // SOH mặc định 100%

    canResult = CAN0.sendMsgBuf(id, ext, 8, data);
    result = (canResult == CAN_OK);
  }

  // --- Khối 4: ID 0x184220F0 (Giới hạn bảo vệ điện áp sạc xả và bảo vệ dòng sạc xả) ---
  if (result) {
    delay(2);
    id = 0x184220F0; 
    uint16_t charge_cutoff_v = 4200;    
    uint16_t discharge_cutoff_v = 3200; 
    uint16_t max_charge_a = (uint16_t)((50.0 + 3000.0) * 10.0);    
    uint16_t max_discharge_a = (uint16_t)((100.0 + 3000.0) * 10.0); 

    memset(data, 0, 8);
    data[0] = lowByteOfUint16T(charge_cutoff_v);    data[1] = highByteOfUint16T(charge_cutoff_v);
    data[2] = lowByteOfUint16T(discharge_cutoff_v); data[3] = highByteOfUint16T(discharge_cutoff_v);
    data[4] = lowByteOfUint16T(max_charge_a);       data[5] = highByteOfUint16T(max_charge_a);
    data[6] = lowByteOfUint16T(max_discharge_a);    data[7] = highByteOfUint16T(max_discharge_a);

    canResult = CAN0.sendMsgBuf(id, ext, 8, data);
    result = (canResult == CAN_OK);
  }
  // --- Khối 5: ID 0x184230F0 (Cực trị điện áp đơn thể Cell pin thực tế từ AntBMS) ---
  if (result) {
    delay(2);
    id = 0x184230F0; 
    uint16_t max_cell_v = (uint16_t)(_receivedResponse.maxCellVoltage * 1000.0); 
    uint16_t min_cell_v = (uint16_t)(_receivedResponse.minCellVoltage * 1000.0); 
    memset(data, 0, 8);
    data[0] = lowByteOfUint16T(max_cell_v); data[1] = highByteOfUint16T(max_cell_v);
    data[2] = lowByteOfUint16T(min_cell_v); data[3] = highByteOfUint16T(min_cell_v);
    data[4] = _receivedResponse.cellWithHighestVoltage; 
    data[5] = 0;
    data[6] = _receivedResponse.cellWithLowestVoltage;  
    data[7] = 0;
    canResult = CAN0.sendMsgBuf(id, ext, 8, data);
    result = (canResult == CAN_OK);
  }

  // --- Khối 6: ID 0x184240F0 (Cực trị thông số nhiệt độ cảm biến thực tế) ---
  if (result) {
    delay(2);
    id = 0x184240F0; 
    uint16_t t_max = (uint16_t)(_receivedResponse.temperatures[TEMPERATURE_SENSOR_1] + 100.0) * 10; 
    uint16_t t_min = (uint16_t)(_receivedResponse.temperatures[TEMPERATURE_SENSOR_1] + 100.0) * 10;
    memset(data, 0, 8);
    data[0] = lowByteOfUint16T(t_max); data[1] = highByteOfUint16T(t_max);
    data[2] = lowByteOfUint16T(t_min); data[3] = highByteOfUint16T(t_min);
    data[4] = 1; data[5] = 0; data[6] = 1; data[7] = 0;
    canResult = CAN0.sendMsgBuf(id, ext, 8, data);
    result = (canResult == CAN_OK);
  }

  // --- CÁC KHỐI PHỤ TRỐNG BẮT BUỘC ĐỂ GIỮ KẾT NỐI BIẾN TẦN KHÔNG BÁO LỖI ALARM ---
  memset(data, 0, 8);
  if (result) { delay(2); CAN0.sendMsgBuf(0x184250F0, ext, 8, data); }
  if (result) { delay(2); CAN0.sendMsgBuf(0x184260F0, ext, 8, data); }
  if (result) { delay(2); CAN0.sendMsgBuf(0x184270F0, ext, 8, data); }
  if (result) { delay(2); CAN0.sendMsgBuf(0x184280F0, ext, 8, data); }
  if (result) { delay(2); CAN0.sendMsgBuf(0x184290F0, ext, 8, data); }
  if (result) { delay(2); CAN0.sendMsgBuf(0x1842A0, ext, 8, data); }

  if (result) _canSuccessCounter++; else _canFailureCounter++;
  return result;
}
void setup()
{
  Serial.begin(ESP_DEBUGGING_BAUD_RATE);
  
  // Tự động cấu hình khởi chạy cổng nối tiếp mềm hoặc cứng theo dòng chip chọn
  #ifdef ESP8266
    _bms.begin(BMS_BAUD_RATE);
  #else
    _bms.begin(BMS_BAUD_RATE, SERIAL_8N1, 16, 17); // RX2=16, TX2=17 trên ESP32
  #endif
  _bms.setTimeout(BMS_TIMEOUT);

  // Kết nối mạng
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  _mqttClient.setServer(MQTT_SERVER, MQTT_PORT);

  // Khởi chạy Web Server nạp code từ xa OTA: ip_mạch/firmware (Tài khoản mặc định: admin / admin)
  _httpUpdater.setup(&_httpServer, "/firmware", "admin", "admin");
  _httpServer.begin();

  // Khởi động module CAN Bus với tốc độ chuẩn 500Kbps phục vụ biến tần cao áp
  byte errorCode = CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ);
  while (errorCode != CAN_OK) {
    delay(500);
    errorCode = CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ);
  }
  
  CAN0.enOneShotTX();
  CAN0.setMode(MCP_NORMAL);
  Serial.println("Hybrid System Ready! Web OTA available at /firmware");
}

void loop()
{
  unsigned long now = millis();
  static unsigned long lastBmsQuery = 0;

  _httpServer.handleClient(); // Xử lý dịch vụ nạp code từ xa liên tục
  reconnectNetwork();
  _mqttClient.loop();

  if (now - lastBmsQuery >= BMS_QUERY_INTERVAL) {
    lastBmsQuery = now;
    readBms();
  }

  if (now - _lastMqttSent >= MQTT_SEND_INTERVAL) {
    _lastMqttSent = now;
    sendMqttData();
  }
  delay(10);
}

void readBms()
{
  const byte requestCommand[] = { 0x5A, 0x5A, 0x00, 0x00, 0x01, 0x01 };
  byte incomingBuffer[BMS_MESSAGE_LENGTH] = { 0 };
  size_t bytesReceived;

  _bms.flush();
  while (_bms.available()) { _bms.read(); }
  _bms.write(requestCommand, sizeof(requestCommand));
  _bms.flush(); 

  bytesReceived = _bms.readBytes(incomingBuffer, BMS_MESSAGE_LENGTH); 

  if (bytesReceived >= 140 && incomingBuffer[0] == 0xAA && incomingBuffer[1] == 0x55) {
    _bmsValidResponseCounter++;
    
    uint16_t volt_raw = (incomingBuffer[4] << 8) | incomingBuffer[5];
    _receivedResponse.totalVoltage = (float)volt_raw * 0.1; 
    
    _receivedResponse.rawSoc = incomingBuffer[74];
    _receivedResponse.cells = incomingBuffer[72];
    _receivedResponse.maxCellVoltage = (float)((incomingBuffer[6] << 8) | incomingBuffer[7]) * 0.001;
    _receivedResponse.minCellVoltage = (float)((incomingBuffer[12] << 8) | incomingBuffer[13]) * 0.001;
    _receivedResponse.temperatures[TEMPERATURE_MOSFET] = (float)((int16_t)((incomingBuffer[94] << 8) | incomingBuffer[95]));
    _receivedResponse.temperatures[TEMPERATURE_SENSOR_1] = (float)((int16_t)((incomingBuffer[96] << 8) | incomingBuffer[97]));

    sendCanMessage();
  } else {
    _bmsInvalidResponseCounter++;
  }
}
