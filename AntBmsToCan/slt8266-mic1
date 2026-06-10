#include <ESP8266WiFi.h>  // Thư viện WiFi cho ESP8266
#include <PubSubClient.h> // Thư viện MQTT Broker
#include <SPI.h>
#include <mcp_can.h>
#include <SoftwareSerial.h> 

// ==========================================
// CẤU HÌNH WIFI VÀ MQTT BROKER
// ==========================================
#define WIFI_SSID "ASUS_90_2G"
#define WIFI_PASSWORD "116811681168"

#define MQTT_SERVER "10.0.0.176"
#define MQTT_PORT 1883
#define MQTT_USERNAME "mqqt_solintek" 
#define MQTT_PASSWORD "155678"
#define DEVICE_NAME "AntBmsToCan1"

#define BMS_QUERY_INTERVAL 1000
#define MQTT_SEND_INTERVAL 10000 // Chu kỳ gửi dữ liệu lên MQTT (10 giây)
#define BMS_TIMEOUT 250
#define CAN_ADDR 1 

// ==========================================
// SƠ ĐỒ CHÂN PHẦN CỨNG ESP8266 (NODEMCU)
// ==========================================
#define MCP2515_CS 15   // Chân CS nối vào chân D8 (GPIO15)
#define MCP2515_INT 2   // Chân ngắt INT nối vào chân D4 (GPIO2)
#define BMS_RX_PIN 5    // Chân RX nối vào chân D1 (GPIO5)
#define BMS_TX_PIN 4    // Chân TX nối vào chân D2 (GPIO4)

#define BMS_BAUD_RATE 19200 
#define ESP_DEBUGGING_BAUD_RATE 115200

#define MAX_CELLS_SUPPORTED_BY_BMS 30
#define MAX_TEMPERATURE_SENSORS_SUPPORTED_BY_BMS 6
#define BMS_MESSAGE_LENGTH 140

#define TEMPERATURE_MOSFET 0
#define TEMPERATURE_BALANCER 1
#define TEMPERATURE_SENSOR_1 2

SoftwareSerial _bms(BMS_RX_PIN, BMS_TX_PIN);
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

// Hàm tự động kết nối WiFi và MQTT Broker
void reconnectMqtt() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    return;
  }
  if (!_mqttClient.connected()) {
    Serial.print("Connecting to MQTT...");
    if (_mqttClient.connect(DEVICE_NAME, MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc="); Serial.println(_mqttClient.state());
    }
  }
}
// Hàm đóng gói dữ liệu định kỳ và đẩy lên MQTT Broker
void sendMqttData() {
  if (!_mqttClient.connected()) return;

  char topic[64];
  char payload[512]; // Cấp phát bộ nhớ đệm an toàn cho ESP8266

  snprintf(topic, sizeof(topic), "%s/bms/details", DEVICE_NAME);
  
  // Tạo chuỗi JSON chứa các thông số quan trọng của pin
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
  Serial.println("MQTT Data Sent successfully.");
}

void printCanResultToSerialMCP(unsigned long id, byte canResult) {
  Serial.print("CAN Packet 0x"); Serial.print(id, HEX); Serial.print(" Result: "); Serial.println(canResult);
}
bool sendCanMessage()
{
  bool result = true;
  uint8_t data[8] = { 0 };
  byte canResult;
  unsigned long id;
  byte ext = 1; // Khung mở rộng 29-bit

  // --- Khối 1: ID 0x7310 + Addr (Bắt tay phiên bản phần cứng) ---
  if (result) {
    delay(2);
    id = 0x00007310 + CAN_ADDR; 
    uint8_t d1[8] = {1, 0, 0x02, 0x01, 0x01, 0x02, 1, 0}; 
    canResult = CAN0.sendMsgBuf(id, ext, 8, d1);
    result = (canResult == CAN_OK);
  }

  // --- Khối 2: ID 0x7320 + Addr (Bắt tay cấu hình dung lượng) ---
  if (result) {
    delay(2);
    id = 0x00007320 + CAN_ADDR; 
    uint8_t d2[8] = {0x01, 0x00, 0x01, 0x10, 0xE0, 0x01, 0x64, 0x00}; 
    canResult = CAN0.sendMsgBuf(id, ext, 8, d2);
    result = (canResult == CAN_OK);
  }

  // --- Khối 3 & 4: ID 0x7330 & 0x7340 (Tên hãng ASCII) ---
  if (result) {
    delay(2);
    uint8_t d3[8] = {'D', 'E', 'F', 'A', 'U', 'L', 'T', ' '};
    CAN0.sendMsgBuf(0x00007330 + CAN_ADDR, ext, 8, d3);
    delay(2);
    CAN0.sendMsgBuf(0x00007340 + CAN_ADDR, ext, 8, d3);
  }

  // --- Khối 5: ID 0x4210 + Addr (Điện áp tổng, Dòng điện kèm Offset -3000A, SoC, SoH) ---
  if (result) {
    delay(2);
    id = 0x00004210 + CAN_ADDR; 
    
    uint16_t totalVolt_01V = (uint16_t)(_receivedResponse.totalVoltage * 10.0); 
    
    // Thuật toán bù dải dòng điện cho biến tần áp cao: (Dòng thực tế + 3000) * 10
    float current_real = _receivedResponse.current; 
    uint16_t current_can = (uint16_t)((current_real + 3000.0) * 10.0);

    uint16_t t_bms = (uint16_t)(_receivedResponse.temperatures[TEMPERATURE_MOSFET] + 100.0) * 10;

    memset(data, 0, 8);
    data[0] = lowByteOfUint16T(totalVolt_01V);  data[1] = highByteOfUint16T(totalVolt_01V);
    data[2] = lowByteOfUint16T(current_can);    data[3] = highByteOfUint16T(current_can);
    data[4] = lowByteOfUint16T(t_bms);          data[5] = highByteOfUint16T(t_bms);
    data[6] = _receivedResponse.rawSoc; 
    data[7] = 100;                      

    canResult = CAN0.sendMsgBuf(id, ext, 8, data);
    result = (canResult == CAN_OK);
  }
  // --- Khối 6: ID 0x4220 + Addr (Giới hạn điện áp và Dòng sạc/xả cực đại) ---
  if (result) {
    delay(2);
    id = 0x00004220 + CAN_ADDR; 
    
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

  // --- Khối 7: ID 0x4230 + Addr (Cực trị điện áp Cell pin) ---
  if (result) {
    delay(2);
    id = 0x00004230 + CAN_ADDR; 
    uint16_t max_cell_v = (uint16_t)(_receivedResponse.maxCellVoltage * 1000.0); 
    uint16_t min_cell_v = (uint16_t)(_receivedResponse.minCellVoltage * 1000.0); 
    memset(data, 0, 8);
    data[0] = lowByteOfUint16T(max_cell_v); data[1] = highByteOfUint16T(max_cell_v);
    data[2] = lowByteOfUint16T(min_cell_v); data[3] = highByteOfUint16T(min_cell_v);
    data[4] = _receivedResponse.cellWithHighestVoltage; 
    data[6] = _receivedResponse.cellWithLowestVoltage;  
    canResult = CAN0.sendMsgBuf(id, ext, 8, data);
    result = (canResult == CAN_OK);
  }

  // --- Khối 8: ID 0x4240 + Addr (Cực trị nhiệt độ Cell pin) ---
  if (result) {
    delay(2);
    id = 0x00004240 + CAN_ADDR; 
    uint16_t t_max = (uint16_t)(_receivedResponse.temperatures[TEMPERATURE_SENSOR_1] + 100.0) * 10; 
    uint16_t t_min = (uint16_t)(_receivedResponse.temperatures[TEMPERATURE_SENSOR_1] + 100.0) * 10;
    memset(data, 0, 8);
    data[0] = lowByteOfUint16T(t_max); data[1] = highByteOfUint16T(t_max);
    data[2] = lowByteOfUint16T(t_min); data[3] = highByteOfUint16T(t_min);
    data[4] = 1; data[6] = 1;
    canResult = CAN0.sendMsgBuf(id, ext, 8, data);
    result = (canResult == CAN_OK);
  }

  // --- CÁC KHỐI PHỤ LẤP ĐẦY TRỐNG BẮT BUỘC ---
  memset(data, 0, 8);
  if (result) { delay(2); CAN0.sendMsgBuf(0x00004250 + CAN_ADDR, ext, 8, data); }
  if (result) { delay(2); CAN0.sendMsgBuf(0x00004260 + CAN_ADDR, ext, 8, data); }
  if (result) { delay(2); CAN0.sendMsgBuf(0x00004270 + CAN_ADDR, ext, 8, data); }
  if (result) { delay(2); CAN0.sendMsgBuf(0x00004280 + CAN_ADDR, ext, 8, data); }
  if (result) { delay(2); CAN0.sendMsgBuf(0x00004290 + CAN_ADDR, ext, 8, data); }
  if (result) { delay(2); CAN0.sendMsgBuf(0x000042A0 + CAN_ADDR, ext, 8, data); }

  if (result) _canSuccessCounter++; else _canFailureCounter++;
  return result;
}
void setup()
{
  Serial.begin(ESP_DEBUGGING_BAUD_RATE);
  _bms.begin(BMS_BAUD_RATE); 
  _bms.setTimeout(BMS_TIMEOUT); 

  // Kết nối WiFi ban đầu
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  _mqttClient.setServer(MQTT_SERVER, MQTT_PORT);

  // Khởi động CAN Bus 500Kbps theo tài liệu quy định
  byte errorCode = CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ);
  while (errorCode != CAN_OK) {
    delay(500);
    errorCode = CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ);
  }
  
  CAN0.enOneShotTX();
  CAN0.setMode(MCP_NORMAL);
  pinMode(MCP2515_INT, INPUT_PULLUP);
  Serial.println("MCP2515 & WiFi Ready!");
}

void loop()
{
  static unsigned long lastBmsQuery = 0;
  unsigned long now = millis();

  // Kiểm tra duy trì kết nối mạng ngầm liên tục
  reconnectMqtt();
  _mqttClient.loop();

  // Chu kỳ đọc BMS và đẩy dữ liệu CAN sang biến tần (1 giây)
  if (now - lastBmsQuery >= BMS_QUERY_INTERVAL) {
    lastBmsQuery = now;
    readBms();
  }

  // Chu kỳ đẩy dữ liệu lên MQTT Broker (10 giây)
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

    sendCanMessage();
  } else {
    _bmsInvalidResponseCounter++;
  }
}
