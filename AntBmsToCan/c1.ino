/*
Name:       AntBmsToCan.ino
Created:    11/Oct/2023
Author:     Daniel Young

This file is part of AntBmsToCan which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

Notes

First, go and customise settings at the top of this script until you see END OF BASIC SETTINGS!

With thanks to the following whose heavy lifting both inspired this particular development:
Sebastian Muszynski - https://github.com/syssi/esphome-ant-bms
JuWorkshop - https://github.com/JuWorkshop/AntBms-to-Ve.Can-Victron-Venus
SetFireLabs - https://www.setfirelabs.com/green-energy/pylontech-can-reading-can-replication

*/

#include <SPI.h>
#include <mcp_can.h>
#include <HardwareSerial.h>
#include <serial_io.h>
// START OF BASIC SETTINGS

// If you want to use WiFi to report information temporarily to an MQTT broker, uncomment the following line
// DO NOT USE OUTSIDE OF TESTING - YOU DON'T WANT YOUR INVERTER GOING INTO ERROR JUST BECAUSE WIFI OR MQTT WAS UNAVAILABLE!
//#define USE_WIFI_AND_MQTT

#ifdef USE_WIFI_AND_MQTT
#include <WiFi.h>
#include <PubSubClient.h>

// Update with your WiFi Access Point details
#define WIFI_SSID       "ASUS_90_2G"
#define WIFI_PASSWORD   "116811681168"

// Update with your MQTT Broker details
#define MQTT_SERVER "192.168.1.176"
#define MQTT_PORT   1883
d#efine MQTT_USERNAME   "battery"           // Empty string for none.
#define MQTT_PASSWORD   "Switch1"

// The device name is used as the MQTT base topic and presence on the network.
// If you need more than one AntBmsToCan on your network, give them unique names.
#define DEVICE_NAME "AntBmsToCan"
endif

// "Charge Voltage Limit (CVL)"
// This is the millivolts representing max charge of an individual cell.
// For example, this was built for an LTO setup of 22 in series, where the max voltage per cell was requested at 2.618V
// The number here is multiplied by the number of cells reported by the BMS to give a maximum voltage.
// (ChargeVsetpoint), i.e. 22 * 2618 = 57596 / 1000 = 57.596 V
#define CHARGE_VOLTAGE_LIMIT_CVL_IN_MILLIVOLTS 3550


// "Charge Current Limit (CCL)"
// This is the max charge current in 0.1A (tenths) of an amp.
// (chargecurrent), i.e. 400 = 40A
#define CHARGE_CURRENT_LIMIT_IN_TENTHS_OF_AN_AMP 260

// "Discharge Current Limit (DCL)"
// This is the max discharge current in 0.1A (tenths) of an amp.
// (discurrent), i.e. 400 = 40A
#define DISCHARGE_CURRENT_LIMIT_IN_TENTHS_OF_AN_AMP 260 

// "Discharge Voltage Limit (DVL)"
// This is the millivolts representing min charge of an individual cell.
// For example, this was built for an LTO setup of 22 in series, where the min voltage per cell was requested at 1.950V
// The number here is multiplied by the number of cells reported by the BMS to give a minimum voltage.
// (DischVsetpoint), i.e. 22 * 1950 = 42900 / 1000 = 42.900 V
#define DISCHARGE_VOLTAGE_LIMIT_DVL_IN_MILLIVOLTS 2900

// How frequently to poll BMS information and send via CAN
// Default, 1 second (though could be made less frequent)
#define BMS_QUERY_INTERVAL 1000

// END OF BASIC SETTINGS

// START OF ADVANCED SETTINGS

// How long to wait before we give up waiting for a BMS response
// Default 250 (1/4 second)
#define BMS_TIMEOUT 250

// For debugging, uses a fixed message customisable in readBms()
define USE_FIXED_MESSAGE_FOR_DEBUGGING true

// If no BMS response, or a garbage BMS response which failed validation, print results and send MQTT anyway?
// Usually you won't want this.  It is good for debugging if for some reason values are not coming through.
#define WHEN_BMS_READ_FAILED_PRINT_VALUES_TO_SERIAL_AND_SEND_TO_MQTT_IF_USING_ANYWAY false

// The BMS may run a later firmware which exposes power as a value
// This set to false will use voltage * current to derive power.  Set to true will use the reading.
// I'd leave as true but if you get no power, try swapping to false
#define SUPPORTS_NEW_COMMANDS false

// If you don't want to poll the BMS and send to CAN unless a pin is given 3.3v, leave this as is
//#define USE_SHORTING_PIN
//#define SHORTING_PIN 35 //13


ifdef USE_WIFI_AND_MQTT
// On boot will request a buffer size of (MAX_MQTT_PAYLOAD_SIZE + MQTT_HEADER_SIZE) for MQTT, and
// MAX_MQTT_PAYLOAD_SIZE for building payloads.  If these fail and your device doesn't boot, you can assume you've set this too high.
#define MAX_MQTT_PAYLOAD_SIZE 4096
#define MIN_MQTT_PAYLOAD_SIZE 512
#define MQTT_HEADER_SIZE 512

// How frequently to send a response over MQTT
// Default 10 seconds
#define MQTT_SEND_INTERVAL 10000
endif
// END OF ADVANCED SETTINGS



// START OF FIXED SETTINGS, SHOULD NOT NEED MODIFYING

// Set CS to GPIO5
#define MCP2515_CS 10
#define MCP2515_SCK 11
#define MCP2515_MOSI 12 //5
#define MCP2515_MISO 13
// Set INT to GPIO4 (Not needed, but coded in anyway)
#define MCP2515_INT 9


// Status will be posted to the following topic
#define MQTT_BMS_DETAILS "/bms/details"
#define MQTT_BMS_NOTENOUGHDATA "/bms/notenoughdata"
#define MQTT_BMS_FAILEDCHECKSUM "/bms/failedchecksum"
#define MQTT_BMS_FAILEDHEADER "/bms/failedheader"
#define MQTT_CAN_FAILSEND "/can/failsend"
#define MQTT_CAN_TXERRORCOUNT "/can/txerrorcount"

#define BMS_ERRORTYPE_NOTENOUGHDATA 1
#define BMS_ERRORTYPE_FAILEDCHECKSUM 2
#define BMS_ERRORTYPE_FAILEDHEADER 3
#define CAN_ERRORTYPE_CANFAILSEND 4
#define CAN_ERRORTYPE_CANTXERRORCOUNT 5

// BMS RX and TX Pins
#define BMS_RX_PIN 19
#define BMS_TX_PIN 20

// Baud Rates
#define BMS_BAUD_RATE 19200 //19200
#define ESP_DEBUGGING_BAUD_RATE 115200

#define KELVIN_OFFSET 273.15

// This is fixed by the AntBMS protocol
#define MAX_CELLS_SUPPORTED_BY_BMS 30

// This is fixed by the AntBMS protocol
#define MAX_TEMPERATURE_SENSORS_SUPPORTED_BY_BMS 6

// Positions of each temperature sensor reading in the array
#define TEMPERATURE_MOSFET 0
#define TEMPERATURE_BALANCER 1
#define TEMPERATURE_SENSOR_1 2
#define TEMPERATURE_SENSOR_2 3
#define TEMPERATURE_SENSOR_3 4
#define TEMPERATURE_SENSOR_4 5

// How long a BMS message is, this is fixed at 140 as per the AntBMS protocol
#define BMS_MESSAGE_LENGTH 140

// END OF FIXED SETTINGS
// CODE ONWARDS
HardwareSerial _bms(2); // UART 2 (GPIO19 RX2, GPIO20 TX2)
// Totals for racking up
unsigned long _bmsValidResponseCounter = 0;
unsigned long _bmsInvalidResponseCounter = 0;
unsigned long _canSuccessCounter = 0;
unsigned long _canFailureCounter = 0;

#ifdef USE_WIFI_AND_MQTT
// Main WiFi object
WiFiClient _wifi;

// Main MQTT object
PubSubClient _mqtt(_wifi);

// Buffer Size (and therefore payload size calc)
int _bufferSize;
int _maxPayloadSize;

// I want to declare this once at a modular level, keep the heap somewhere in check.
char* _mqttPayload;

#endif

// ---------------------- BEGIN: SOLXPOW port helpers & builders ----------------------
// Uncomment to use low-byte then high-byte ordering (SOLXPOW uses this in some builds)
// #define INVERT_LOW_HIGH_BYTES
// Uncomment if your inverter expects a 30000 offset for currents
// #define SET_30K_OFFSET

static inline void write16_be(uint8_t *buf, uint16_t val) {
  buf[0] = (uint8_t)((val >> 8) & 0xFF);
  buf[1] = (uint8_t)(val & 0xFF);
}
static inline void write16_le(uint8_t *buf, uint16_t val) {
  buf[0] = (uint8_t)(val & 0xFF);
  buf[1] = (uint8_t)((val >> 8) & 0xFF);
}
static inline void write16(uint8_t *buf, uint16_t val) {
#ifdef INVERT_LOW_HIGH_BYTES
  write16_le(buf, val);
#else
  write16_be(buf, val);
#endif
}
static inline uint16_t clamp16(int32_t v) {
  if (v < 0) return 0;
  if (v > 0xFFFF) return 0xFFFF;
  return (uint16_t)v;
}

static inline void print8(const uint8_t *d) {
  for (int i = 0; i < 8; ++i) Serial.printf("%02X ", d[i]);
  Serial.println();
}

static void transmit_frame(uint32_t id, const uint8_t data[8]) {
  const uint8_t len = 8;
  const uint8_t ext = 1;
  Serial.printf("SEND ID 0x%08lX: ", id);
  print8(data);
  uint8_t canResult = CAN0.sendMsgBuf(id, ext, len, (uint8_t *)data);
  if (canResult != CAN_OK) printCanResultToSerialMCP(id, canResult);
}

// Builders for SOLXPOW frames (mirror SOLXPOW-CAN.cpp mapping)
void build_frame_4210(uint8_t data[8]) {
  uint16_t totalV = clamp16((int32_t)datalayer.battery.status.voltage_dV);
  write16(&data[0], totalV);

  int32_t cur_raw = (int32_t)datalayer.battery.status.reported_current_dA;
#ifdef SET_30K_OFFSET
  cur_raw += 30000;
#endif
  int16_t cur_signed = (int16_t)cur_raw;
  write16(&data[2], (uint16_t)cur_signed);

  int32_t temp_scaled = (int32_t)(datalayer.battery.status.temperature_max_dC + 1000);
  write16(&data[4], clamp16(temp_scaled));

  uint8_t soc_byte = (uint8_t)constrain((int)round(datalayer.battery.status.reported_soc / 100.0f), 0, 100);
  uint8_t soh_byte = (uint8_t)constrain((int)round(datalayer.battery.status.soh_pptt / 100.0f), 0, 100);
  data[6] = soc_byte;
  data[7] = soh_byte;
}

void build_frame_4220(uint8_t data[8]) {
  write16(&data[0], clamp16((int32_t)charge_cutoff_voltage_dV));
  write16(&data[2], clamp16((int32_t)discharge_cutoff_voltage_dV));
#ifdef SET_30K_OFFSET
  write16(&data[4], clamp16((int32_t)(datalayer.battery.status.max_charge_current_dA + 30000)));
  write16(&data[6], clamp16((int32_t)(30000 - datalayer.battery.status.max_discharge_current_dA)));
#else
  write16(&data[4], clamp16((int32_t)datalayer.battery.status.max_charge_current_dA));
  write16(&data[6], clamp16((int32_t)datalayer.battery.status.max_discharge_current_dA));
#endif
}

void build_frame_4230(uint8_t data[8]) {
  write16(&data[0], clamp16((int32_t)datalayer.battery.status.cell_max_voltage_mV));
  write16(&data[2], clamp16((int32_t)datalayer.battery.status.cell_min_voltage_mV));
  data[4]=data[5]=data[6]=data[7]=0;
}

void build_frame_4240(uint8_t data[8]) {
  write16(&data[0], clamp16((int32_t)datalayer.battery.status.temperature_max_dC));
  write16(&data[2], clamp16((int32_t)datalayer.battery.status.temperature_min_dC));
  data[4]=data[5]=data[6]=data[7]=0;
}

void build_frame_4250(uint8_t data[8]) {
  uint8_t status = 0x00;
  if (datalayer.system.status.system_status == FAULT) status = 0x00;
  else if (datalayer.battery.status.reported_current_dA < 0) status = 0x01;
  else if (datalayer.battery.status.reported_current_dA > 0) status = 0x02;
  else status = 0x03;
  data[0] = status;
  data[1]=data[2]=data[3]=data[4]=data[5]=data[6]=data[7]=0;
}

void build_frame_4260(uint8_t data[8]) { memset((void*)data, 0, 8); }

void build_frame_4270(uint8_t data[8]) {
  write16(&data[0], clamp16((int32_t)datalayer.battery.status.temperature_max_dC));
  write16(&data[2], clamp16((int32_t)datalayer.battery.status.temperature_min_dC));
  data[4]=data[5]=data[6]=data[7]=0;
}

void build_frame_4280(uint8_t data[8]) {
  data[0] = (datalayer.battery.status.max_charge_current_dA == 0) ? 0xAA : 0x00;
  data[1] = (datalayer.battery.status.max_discharge_current_dA == 0) ? 0xAA : 0x00;
  data[2]=data[3]=data[4]=data[5]=data[6]=data[7]=0;
}

void build_frame_4290(uint8_t data[8]) { memset((void*)data, 0, 8); }

void build_frame_7310(uint8_t data[8]) { memset((void*)data, 0, 8); }
void build_frame_7320(uint8_t data[8]) {
  memset((void*)data, 0, 8);
  if (user_selected_inverter_cells > 0) write16(&data[0], (uint16_t)user_selected_inverter_cells);
  if (user_selected_inverter_modules > 0) data[2] = (uint8_t)user_selected_inverter_modules;
  if (user_selected_inverter_cells_per_module > 0) data[3] = (uint8_t)user_selected_inverter_cells_per_module;
  if (user_selected_inverter_voltage_level > 0) write16(&data[4], (uint16_t)user_selected_inverter_voltage_level);
  if (user_selected_inverter_ah_capacity > 0) write16(&data[6], (uint16_t)user_selected_inverter_ah_capacity);
}
void build_frame_7330(uint8_t data[8]) { memset((void*)data, 0, 8); }
void build_frame_7340(uint8_t data[8]) { memset((void*)data, 0, 8); }
// ---------------------- END: SOLXPOW port helpers & builders ----------------------


// The main structure to store results from the BMS
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

// Main MCP_CAN object
MCP_CAN CAN0(MCP2515_CS);

/*
printCanResultToSerialMCP()

Prints the return code and description according to the MCP_CAN library.
Extended for a custom id of -1 for reads.
*/
void printCanResultToSerialMCP(unsigned long id, byte canResult)
{
    char msgString[128];

    sprintf(msgString, "0x%.8lX", id);

    if (id >= 0)
    {
        Serial.print("CAN Packet ");Serial.print(msgString);Serial.print(",  Result: ");Serial.print(canResult);Serial.print(", ");
    }
    else if (id < 0)
    {
        Serial.print("CAN Read Result ");
    }

    switch (canResult) {
    case CAN_OK: Serial.print("CAN_OK\r\n");
        break;
    case CAN_FAILINIT: Serial.print("CAN_FAILINIT\r\n");
        break;
    case CAN_FAILTX: Serial.print("CAN_FAILTX\r\n");
        break;
    case CAN_MSGAVAIL: Serial.print("CAN_MSGAVAIL\r\n");
        break;
    case CAN_NOMSG: Serial.print("CAN_NOMSG\r\n");
        break;
    case CAN_CTRLERROR: Serial.print("CAN_CTRLERROR\r\n");
        break;
    case CAN_GETTXBFTIMEOUT: Serial.print("CAN_GETTXBFTIMEOUT\r\n");
        break;
    case CAN_SENDMSGTIMEOUT: Serial.print("CAN_SENDMSGTIMEOUT\r\n");
        break;
    case CAN_FAIL: Serial.print("CAN_FAIL\r\n");
        break;
    }
    Serial.println("");
}

/*
lowByteOfUint16T()

Returns low byte of a uint16_t, due to not being sure about inbuilt lowByte function.
*/
uint8_t lowByteOfUint16T(uint16_t input)
{
    return input & 0xff;
}

/*
highByteOfUint16T()
Returns high byte of a uint16_t, due to not being sure about inbuilt highByte function.
*/
uint8_t highByteOfUint16T(uint16_t input)
{
    return (input >> 8) & 0xff;
}

/*
calcChecksum()

Calculates the relevant checksum for the frame
*/
uint16_t calcChecksum(const uint8_t data[], const uint16_t len) {
    uint16_t checksum = 0;
    for (uint16_t i = 4; i < len; i++) {
        checksum = checksum + data[i];
    }
    return checksum;
}

/*
sendCanMessage()

Replaced implementation: uses build_frame_*() and transmit_frame() to send SOLXPOW-compatible frames.
Keeps original logging and CAN result handling.
*/
bool sendCanMessage()
{
    uint16_t SOH = 100; // SOH place holder
    unsigned char alarmBuf[4] = { 0, 0, 0, 0 };
    unsigned char warningBuf[4] = { 0, 0, 0, 0 };
    unsigned char bmsnameBuf[8] = { 'P', 'Y', 'L', 'O', 'N', ' ', ' ', ' ' };

    bool result = true;
    uint8_t data[8];
    byte canResult;
    char msgString[128];
    unsigned long id;
    byte len;
    byte ext = 0;

    // Setup frames
    if (result) {
        delay(2);
        id = 0x7310; ext = 1; len = 8;
        build_frame_7310(data);
        Serial.printf("SEND %s ID 0x%.8lX  DLC: %1d  Data:", ext == 1 ? "Extended" : "Standard", id, len);
        print8(data);
        canResult = CAN0.sendMsgBuf(id, ext, len, data);
        result = (canResult == CAN_OK);
        if (canResult != CAN_OK) printCanResultToSerialMCP(id, canResult);
        else {
#ifdef USE_WIFI_AND_MQTT
            sendMqttState(CAN_ERRORTYPE_CANFAILSEND, 0, id, canResult, 0);
#endif
        }
    }

    if (result) {
        delay(2);
        id = 0x7320; ext = 1; len = 8;
        build_frame_7320(data);
        Serial.printf("SEND %s ID 0x%.8lX  DLC: %1d  Data:", ext == 1 ? "Extended" : "Standard", id, len);
        print8(data);
        canResult = CAN0.sendMsgBuf(id, ext, len, data);
        result = (canResult == CAN_OK);
        if (canResult != CAN_OK) printCanResultToSerialMCP(id, canResult);
        else {
#ifdef USE_WIFI_AND_MQTT
            sendMqttState(CAN_ERRORTYPE_CANFAILSEND, 0, id, canResult, 0);
#endif
        }
    }

    // System data frames
    if (result) { delay(2); id = 0x4210; ext = 1; len = 8; build_frame_4210(data); Serial.printf("SEND %s ID 0x%.8lX  DLC: %1d  Data:", ext == 1 ? "Extended" : "Standard", id, len); print8(data); canResult = CAN0.sendMsgBuf(id, ext, len, data); result = (canResult == CAN_OK); if (canResult != CAN_OK) printCanResultToSerialMCP(id, canResult); else { #ifdef USE_WIFI_AND_MQTT sendMqttState(CAN_ERRORTYPE_CANFAILSEND, 0, id, canResult, 0); #endif } }
    if (result) { delay(2); id = 0x4220; ext = 1; len = 8; build_frame_4220(data); Serial.printf("SEND %s ID 0x%.8lX  DLC: %1d  Data:", ext == 1 ? "Extended" : "Standard", id, len); print8(data); canResult = CAN0.sendMsgBuf(id, ext, len, data); result = (canResult == CAN_OK); if (canResult != CAN_OK) printCanResultToSerialMCP(id, canResult); else { #ifdef USE_WIFI_AND_MQTT sendMqttState(CAN_ERRORTYPE_CANFAILSEND, 0, id, canResult, 0); #endif } }
    if (result) { delay(2); id = 0x4230; ext = 1; len = 8; build_frame_4230(data); Serial.printf("SEND %s ID 0x%.8lX  DLC: %1d  Data:", ext == 1 ? "Extended" : "Standard", id, len); print8(data); canResult = CAN0.sendMsgBuf(id, ext, len, data); result = (canResult == CAN_OK); if (canResult != CAN_OK) printCanResultToSerialMCP(id, canResult); else { #ifdef USE_WIFI_AND_MQTT sendMqttState(CAN_ERRORTYPE_CANFAILSEND, 0, id, canResult, 0); #endif } }
    if (result) { delay(2); id = 0x4240; ext = 1; len = 8; build_frame_4240(data); Serial.printf("SEND %s ID 0x%.8lX  DLC: %1d  Data:", ext == 1 ? "Extended" : "Standard", id, len); print8(data); canResult = CAN0.sendMsgBuf(id, ext, len, data); result = (canResult == CAN_OK); if (canResult != CAN_OK) printCanResultToSerialMCP(id, canResult); else { #ifdef USE_WIFI_AND_MQTT sendMqttState(CAN_ERRORTYPE_CANFAILSEND, 0, id, canResult, 0); #endif } }
    if (result) { delay(2); id = 0x4250; ext = 1; len = 8; build_frame_4250(data); Serial.printf("SEND %s ID 0x%.8lX  DLC: %1d  Data:", ext == 1 ? "Extended" : "Standard", id, len); print8(data); canResult = CAN0.sendMsgBuf(id, ext, len, data); result = (canResult == CAN_OK); if (canResult != CAN_OK) printCanResultToSerialMCP(id, canResult); else { #ifdef USE_WIFI_AND_MQTT sendMqttState(CAN_ERRORTYPE_CANFAILSEND, 0, id, canResult, 0); #endif } }
    if (result) { delay(2); id = 0x4260; ext = 1; len = 8; build_frame_4260(data); Serial.printf("SEND %s ID 0x%.8lX  DLC: %1d  Data:", ext == 1 ? "Extended" : "Standard", id, len); print8(data); canResult = CAN0.sendMsgBuf(id, ext, len, data); result = (canResult == CAN_OK); if (canResult != CAN_OK) printCanResultToSerialMCP(id, canResult); else { #ifdef USE_WIFI_AND_MQTT sendMqttState(CAN_ERRORTYPE_CANFAILSEND, 0, id, canResult, 0); #endif } }
    if (result) { delay(2); id = 0x4270; ext = 1; len = 8; build_frame_4270(data); Serial.printf("SEND %s ID 0x%.8lX  DLC: %1d  Data:", ext == 1 ? "Extended" : "Standard", id, len); print8(data); canResult = CAN0.sendMsgBuf(id, ext, len, data); result = (canResult == CAN_OK); if (canResult != CAN_OK) printCanResultToSerialMCP(id, canResult); else { #ifdef USE_WIFI_AND_MQTT sendMqttState(CAN_ERRORTYPE_CANFAILSEND, 0, id, canResult, 0); #endif } }
    if (result) { delay(2); id = 0x4280; ext = 1; len = 8; build_frame_4280(data); Serial.printf("SEND %s ID 0x%.8lX  DLC: %1d  Data:", ext == 1 ? "Extended" : "Standard", id, len); print8(data); canResult = CAN0.sendMsgBuf(id, ext, len, data); result = (canResult == CAN_OK); if (canResult != CAN_OK) printCanResultToSerialMCP(id, canResult); else { #ifdef USE_WIFI_AND_MQTT sendMqttState(CAN_ERRORTYPE_CANFAILSEND, 0, id, canResult, 0); #endif } }
    if (result) { delay(2); id = 0x4290; ext = 1; len = 8; build_frame_4290(data); Serial.printf("SEND %s ID 0x%.8lX  DLC: %1d  Data:", ext == 1 ? "Extended" : "Standard", id, len); print8(data); canResult = CAN0.sendMsgBuf(id, ext, len, data); result = (canResult == CAN_OK); if (canResult != CAN_OK) printCanResultToSerialMCP(id, canResult); else { #ifdef USE_WIFI_AND_MQTT sendMqttState(CAN_ERRORTYPE_CANFAILSEND, 0, id, canResult, 0); #endif } }

    // Keep original errorCountTX reporting
    if (CAN0.errorCountTX() > 0) {
        Serial.print("errorCountTX: "); Serial.println(CAN0.errorCountTX());
#ifdef USE_WIFI_AND_MQTT
        sendMqttState(CAN_ERRORTYPE_CANTXERRORCOUNT, 0, 0, 0, CAN0.errorCountTX());
#endif
    }

    return result;
}

void SolxpowInverter::map_can_frame_to_variable(CAN_frame rx_frame) {
  switch (rx_frame.ID) {
    case 0x4200:  //Message originating from inverter. Depending on which data is required, act accordingly
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE;
      if (rx_frame.data.u8[0] == 0x02) {
        send_setup_info();
      }
      if (rx_frame.data.u8[0] == 0x00) {
        send_system_data();
      }
      break;
    default:
      break;
  }
}

void SolxpowInverter::transmit_can(unsigned long currentMillis) {
  // No periodic sending, we only react on received can messages
}

void SolxpowInverter::send_setup_info() {  //Ensemble information
  uint8_t data[8];
  build_frame_7310(data); transmit_frame(0x7310, data);
  build_frame_7320(data); transmit_frame(0x7320, data);
  build_frame_7330(data); transmit_frame(0x7330, data);
  build_frame_7340(data); transmit_frame(0x7340, data);
}

void SolxpowInverter::send_system_data() {  //System equipment information
  uint8_t data[8];
  build_frame_4210(data); transmit_frame(0x4210, data);
  build_frame_4220(data); transmit_frame(0x4220, data);
  build_frame_4230(data); transmit_frame(0x4230, data);
  build_frame_4240(data); transmit_frame(0x4240, data);
  build_frame_4250(data); transmit_frame(0x4250, data);
  build_frame_4260(data); transmit_frame(0x4260, data);
  build_frame_4270(data); transmit_frame(0x4270, data);
  build_frame_4280(data); transmit_frame(0x4280, data);
  build_frame_4290(data); transmit_frame(0x4290, data);
}

bool SolxpowInverter::setup() {
  if (user_selected_inverter_cells > 0) {
    SOLXPOW_7320.data.u8[0] = user_selected_inverter_cells & 0xff;
    SOLXPOW_7320.data.u8[1] = (uint8_t)(user_selected_inverter_cells >> 8);
  }

  if (user_selected_inverter_modules > 0) {
    SOLXPOW_7320.data.u8[2] = user_selected_inverter_modules;
  }

  if (user_selected_inverter_cells_per_module > 0) {
    SOLXPOW_7320.data.u8[3] = user_selected_inverter_cells_per_module;
  }

  if (user_selected_inverter_voltage_level > 0) {
    SOLXPOW_7320.data.u8[4] = user_selected_inverter_voltage_level & 0xff;
    SOLXPOW_7320.data.u8[5] = (uint8_t)(user_selected_inverter_voltage_level >> 8);
  }

  if (user_selected_inverter_ah_capacity > 0) {
    SOLXPOW_7320.data.u8[6] = user_selected_inverter_ah_capacity & 0xff;
    SOLXPOW_7320.data.u8[7] = (uint8_t)(user_selected_inverter_ah_capacity >> 8);
  }

  return true;
}

/*
setup()

The setup function runs once when you press reset or power the board
*/
void setup()
{
    // UART 0 for USB debugging (GPIO1 TX, GPIO3 RX)
    Serial.begin(ESP_DEBUGGING_BAUD_RATE);

    // UART 2
    _bms.begin(BMS_BAUD_RATE, SERIAL_8N1, BMS_RX_PIN, BMS_TX_PIN); // (19200,SERIAL_8N1,RXD2,TXD2)
    _bms.setTimeout(BMS_TIMEOUT); // How long to wait for received data

    // LED Output
    pinMode(LED_BUILTIN, OUTPUT);


ifdef USE_WIFI_AND_MQTT
    // Configure WIFI
    setupWifi();

    // Configure MQTT to the address and port specified above
    _mqtt.setServer(MQTT_SERVER, MQTT_PORT);
#ifdef DEBUG
    sprintf(_debugOutput, "About to request buffer");
    Serial.println(_debugOutput);
#endif
    for (_bufferSize = (MAX_MQTT_PAYLOAD_SIZE + MQTT_HEADER_SIZE); _bufferSize >= MIN_MQTT_PAYLOAD_SIZE + MQTT_HEADER_SIZE; _bufferSize = _bufferSize - 1024)
    {
#ifdef DEBUG
        sprintf(_debugOutput, "Requesting a buffer of : %d bytes", _bufferSize);
        Serial.println(_debugOutput);
#endif

        if (_mqtt.setBufferSize(_bufferSize))
        {

            _maxPayloadSize = _bufferSize - MQTT_HEADER_SIZE;
#ifdef DEBUG
            sprintf(_debugOutput, "_bufferSize: %d,\r\n\r\n_maxPayload (Including null terminator): %d", _bufferSize, _maxPayloadSize);
            Serial.println(_debugOutput);
#endif

            // Example, 2048, if declared as 2048 is positions 0 to 2047, and position 2047 needs to be zero.  2047 usable chars in payload.
            _mqttPayload = new char[_maxPayloadSize];
            emptyPayload();

            break;
        }
        else
        {
#ifdef DEBUG
            sprintf(_debugOutput, "Coudln't allocate buffer of %d bytes", _bufferSize);
            Serial.println(_debugOutput);
#endif
        }
    }


    // And any messages we are subscribed to will be pushed to the mqttCallback function for processing
    _mqtt.setCallback(mqttCallback);

    // Connect to MQTT
    mqttReconnect();
#endif

    // Trigger input
#ifdef USE_SHORTING_PIN
    pinMode(SHORTING_PIN, INPUT_PULLDOWN);
    digitalWrite(SHORTING_PIN, LOW);
#endif

    // Kick off the CAN port
    byte errorCode;
    errorCode = CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ);
    while (errorCode != CAN_OK)
    {
        Serial.print("CAN configuration error: ");
        Serial.println(errorCode, HEX);
        delay(500);
        errorCode = CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ);
    }


    if (errorCode == 0)
    {
        // One shot transmission
        CAN0.enOneShotTX();

        // Set NORMAL mode, we are in loopback mode by default.
        CAN0.setMode(MCP_NORMAL);

        pinMode(MCP2515_INT, INPUT_PULLUP); // Configuring pin for /INT input, though interrupts not used in favour of a poll.

        Serial.println("MCP2515 Initialized Successfully!");

        delay(500);
    }


    return;
}


/*
loop()

Main program loop
*/
void loop()
{
    static unsigned long lastBmsQuery = 0;
    int shortingPinValue;

#ifdef USE_WIFI_AND_MQTT

    // Make sure WiFi is good
    if (WiFi.status() != WL_CONNECTED)
    {
        setupWifi();
    }

    // make sure mqtt is still connected
    if ((!_mqtt.connected()) || !_mqtt.loop())
    {
        mqttReconnect();
    }
#endif

    // Time to query BMS?  Works based on time elapsed and optionally, a shorting pin given 3.3V
#ifdef USE_SHORTING_PIN
    shortingPinValue = digitalRead(SHORTING_PIN);
#else
    shortingPinValue = HIGH;
#endif

    // If the interval has passed then read the BMS and onward send
    if (checkTimer(&lastBmsQuery, BMS_QUERY_INTERVAL) && shortingPinValue == HIGH)
    {
        readBms();
    }
    
    // And check for incoming messages - Not required but useful for debugging nonetheless
    // It is probably right to consume them out of the buffer too.
    checkReceiveMCPCAN();

    delay(10);
}



/*
checkReceiveMCPCAN()

Checks for incoming messages from the inverter.

As we don't need to action anything from the inverter, this does nothing apart from dump to serial for reference.
*/
void checkReceiveMCPCAN()
{
    byte canResult;

    // CAN RX Variables
    long unsigned int rxId;
    unsigned char len;
    unsigned char ext;
    int msgCount = 0;
    uint8_t rxBuf[8];

    // Serial output string buffer
    char msgString[128];
 
    if (CAN0.checkReceive() == CAN_MSGAVAIL)
    {
        // I tried flirting with the interrupt pin however it wasn't worth it when a quick checkReceive can action the same thing
        // Given the simplicity of this project it wasn't worth coding in interrupt handling.
        //Serial.println(digitalRead(MCP2515_INT));

        // Read the message
        canResult = CAN0.readMsgBuf(&rxId, &ext, &len, rxBuf);

        // Consume any which are waiting in order
        while (canResult == CAN_OK)
        {
            msgCount++;
            sprintf(msgString, "%s ID: 0x%.8lX  DLC: %1d  Data:", ext == 1 ? "Extended" : "Standard", (rxId & 0x1FFFFFFF), len);
            Serial.print(msgString);

            if ((rxId & 0x40000000) == 0x40000000)
            {
                // Determine if message is a remote request frame.
                sprintf(msgString, " REMOTE REQUEST FRAME");
                Serial.print(msgString);
            }
            else
            {
                // Get the contents
                for (byte i = 0; i < len; i++)
                {
                    sprintf(msgString, " 0x%.2X", rxBuf[i]);
                    Serial.print(msgString);
                }
            }

            Serial.println();
            canResult = CAN0.readMsgBuf(&rxId, &len, rxBuf);
        }

        if (canResult != CAN_OK && canResult != CAN_NOMSG)
        {
            printCanResultToSerialMCP(-1, canResult);
        }
        
        if (msgCount > 1)
        {
            // End of a batch, give a space.
            Serial.println();
        }
    }

    return;
}



/*
checkTimer()

Check to see if the elapsed interval has passed since the passed in millis() value. If it has, return true and update the lastRun.
Note that millis() overflows after 50 days, so we need to deal with that too... in our case we just zero the last run, which means the timer
could be shorter but it's not critical... not worth the extra effort of doing it properly for once in 50 days.
*/
bool checkTimer(unsigned long* lastRun, unsigned long interval)
{
    unsigned long now = millis();

    if (*lastRun > now)
        *lastRun = 0;

    if (now >= *lastRun + interval)
    {
        *lastRun = now;
        return true;
    }

    return false;
}



/*
readBms()

Grabs data from the BMS and onward posts to CAN
*/
/*
readBms()
Grabs data from the BMS and onward posts to CAN
*/
/*
readBms()
Grabs data from the BMS and onward posts to CAN
*/
/*
readBms()
Grabs data from the BMS and onward posts to CAN
*/
/*
readBms()
Grabs data from the BMS and onward posts to CAN
*/
void readBms()
{
  bool goodCrc = false;
  bool goodHeader = true;

  // AntBms Request Data Command
  const byte requestCommand[] = { 0x5A, 0x5A, 0x00, 0x00, 0x01, 0x01 };
  
  // Received Messages begin with this
  const byte startMark[] = { 0xAA, 0x55, 0xAA, 0xFF };
  
  // Buffer for incoming data
  byte incomingBuffer[ BMS_MESSAGE_LENGTH ] = { 0 };
  
  // Testing fixed messages, see USE_FIXED_MESSAGE_FOR_DEBUGGING above.
  byte fixedTestMessage[ BMS_MESSAGE_LENGTH ] = { 0xAA, 0x55, 0xAA, 0xFF, 0x02, 0x30, 0x09, 0xE4, 0x09, 0xE5, 0x09, 0xE5, 0x09, 0xE4, 0x09, 0xE6, 0x09, 0xE6, 0x09, 0xC4, 0x09, 0xE8, 0x09, 0xE8, 0x09, 0xE9, 0x09, 0xE8, 0x09, 0xE9, 0x09, 0xFE, 0x0A, 0x0B, 0x0A, 0x05, 0x0A, 0x09, 0x0A, 0x06, 0x0A, 0x0D, 0x09, 0xDE, 0x0A, 0x0A, 0x0A, 0x04, 0x0A, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2B, 0x63, 0x07, 0x27, 0x0E, 0x00, 0x06, 0xF4, 0xFA, 0x25, 0x00, 0xE8, 0xAF, 0xE2, 0x01, 0xFF, 0xC9, 0x8B, 0x00, 0x14, 0x00, 0x13, 0x00, 0x11, 0x00, 0x11, 0x00, 0x12, 0x00, 0x12, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xF0, 0x12, 0x0A, 0x0D, 0x07, 0x09, 0xC4, 0x09, 0xF1, 0x16, 0xFF, 0xFF, 0x00, 0x7E, 0x00, 0x7A, 0x02, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1D, 0x8D };
  size_t bytesReceived;
  char hexChar;
  uint32_t tempCalc = 0;

  auto ant_get_16bit = [&]( size_t i) -> uint16_t {
    return ( uint16_t(incomingBuffer[ i + 0 ]) << 8) | ( uint16_t(incomingBuffer[ i + 1 ]) << 0);
  };
  auto ant_get_32bit = [&]( size_t i) -> uint32_t {
    return ( uint32_t( ant_get_16bit(i + 0)) << 16) | ( uint32_t( ant_get_16bit(i + 2)) << 0);
  };

  _bms. flush();
  while (_bms. available()) {
    _bms. read();
  }

  // Gửi xung lệnh yêu cầu dữ liệu đến vi điều khiển phụ của mạch AntBMS
  _bms. write(requestCommand, sizeof(requestCommand));

  // Phân tách luồng xử lý giữa dữ liệu thật và dữ liệu giả lập Debug
  if (USE_FIXED_MESSAGE_FOR_DEBUGGING) {
    delay(10);
    memcpy(incomingBuffer, fixedTestMessage, BMS_MESSAGE_LENGTH);
    bytesReceived = BMS_MESSAGE_LENGTH;
  } else {
    bytesReceived = _bms. readBytes(incomingBuffer, BMS_MESSAGE_LENGTH);
  }

  // Khởi chạy thuật toán validate cấu trúc dữ liệu gói tin nhận về
  if (bytesReceived == BMS_MESSAGE_LENGTH) {
    for (int i = 0; i < 4; i++) {
      if (incomingBuffer[ i ] != startMark[ i ]) {
        goodHeader = false;
      }
    }

    if (goodHeader) {
      uint16_t crcCalculated = calcChecksum(incomingBuffer, BMS_MESSAGE_LENGTH - 2);
      uint16_t crcReceived = ant_get_16bit(BMS_MESSAGE_LENGTH - 2);
      if (crcCalculated == crcReceived) {
        goodCrc = true;
      }
    }
  }

  // Thực hiện bóc tách mảng Byte khi kiểm tra tính toàn vẹn gói tin thành công
  if (goodCrc) {
    _bmsValidResponseCounter++;

    // Trích xuất các tham số dung lượng điện năng cơ bản
    _receivedResponse. rawTotalVoltage = ant_get_16bit(4);
    _receivedResponse. totalVoltage = _receivedResponse. rawTotalVoltage / 10.0;

    for (int i = 0; i < MAX_CELLS_SUPPORTED_BY_BMS; i++) {
      _receivedResponse. cellVoltage[ i ] = ant_get_16bit(6 + (i * 2)) / 1000.0;
    }

    _receivedResponse. rawCurrent = ant_get_32bit(70);
    _receivedResponse. current = ((int32_t)_receivedResponse. rawCurrent) / 10.0;
    
    // SỬA LỖI: Lấy phần tử cụ thể từ mảng thay vì lấy toàn bộ mảng buffer
    _receivedResponse. rawSoc = incomingBuffer[74]; 
    _receivedResponse. soc = _receivedResponse. rawSoc;

    _receivedResponse. maxCellVoltage = ant_get_16bit(116) / 1000.0;
    _receivedResponse. minCellVoltage = ant_get_16bit(119) / 1000.0;
    _receivedResponse. cells = incomingBuffer[122]; // SỬA LỖI: Lấy chính xác Byte chứa số lượng cell pin

    _receivedResponse. temperatures[ TEMPERATURE_MOSFET ] = (int16_t)ant_get_16bit(82);
    _receivedResponse. temperatures[ TEMPERATURE_SENSOR_1 ] = (int16_t)ant_get_16bit(86);

    // -------------------------------------------------------------------------
    // ĐỒNG BỘ MAPPING LÊN MẠNG CAN BUS HỆ THỐNG SOLXPOW
    // -------------------------------------------------------------------------
    datalayer. battery. status. voltage_dV = ( uint16_t) round(_receivedResponse. totalVoltage * 10.0);
    datalayer. battery. status. reported_current_dA = ( int32_t) round(_receivedResponse. current * 10.0);
    datalayer. battery. status. reported_soc = ( uint32_t) round(_receivedResponse. soc * 100.0);
    datalayer. battery. status. soh_pptt = 10000; 

    datalayer. battery. status. cell_max_voltage_mV = ( uint16_t) round(_receivedResponse. maxCellVoltage * 1000.0);
    datalayer. battery. status. cell_min_voltage_mV = ( uint16_t) round(_receivedResponse. minCellVoltage * 1000.0);

    datalayer. battery. status. temperature_max_dC = ( int16_t) round(_receivedResponse. temperatures[ TEMPERATURE_MOSFET ] * 10.0);
    datalayer. battery. status. temperature_min_dC = ( int16_t) round(_receivedResponse. temperatures[ TEMPERATURE_SENSOR_1 ] * 10.0);

    datalayer. battery. status. max_charge_current_dA = CHARGE_CURRENT_LIMIT_IN_TENTHS_OF_AN_AMP;
    datalayer. battery. status. max_discharge_current_dA = DISCHARGE_CURRENT_LIMIT_IN_TENTHS_OF_AN_AMP;

    charge_cutoff_voltage_dV = ( uint16_t)(( CHARGE_VOLTAGE_LIMIT_CVL_IN_MILLIVOLTS * _receivedResponse. cells) / 100);
    discharge_cutoff_voltage_dV = ( uint16_t)(( DISCHARGE_VOLTAGE_LIMIT_DVL_IN_MILLIVOLTS * _receivedResponse. cells) / 100);

    Serial. printf("\n[DEBUG-CAN] totalVoltage=%f, current=%f, soc=%f, maxCell=%f, minCell=%f\n", 
                  _receivedResponse. totalVoltage, _receivedResponse. current, _receivedResponse. soc, 
                  _receivedResponse. maxCellVoltage, _receivedResponse. minCellVoltage);
  } else {
    _bmsInvalidResponseCounter++;
  }

  // Xuất bản dữ liệu cũ ra cổng COM và kích hoạt các cổng phần cứng gửi khung CAN đi
  printValuesToSerialAndSendToMQTTIfUsing();
  
  const bool canResult = sendCanMessage();
  if (canResult) {
    _canSuccessCounter++;
  } else {
    _canFailureCounter++;
  }
  
  digitalWrite( LED_BUILTIN, digitalRead( LED_BUILTIN) == LOW ? HIGH : LOW);
  return;
}

///////////////
/*
printValuesToSerialAndSendToMQTTIfUsing()

Nicely prints the results from the BMS to the serial port and, if using, sends to MQTT.
*/
void printValuesToSerialAndSendToMQTTIfUsing()
{
    long milli_start = millis();

    Serial.println("--- Start Formatted ---");
    
    Serial.print("rawTotalVoltage = ");Serial.print(_receivedResponse.rawTotalVoltage, DEC);Serial.println("");
    Serial.print("rawCurrent = ");Serial.print(_receivedResponse.rawCurrent, DEC);Serial.println("");
    Serial.print("rawCurrentAsFloat = ");Serial.print(_receivedResponse.rawCurrentAsFloat, DEC);Serial.println("");
    Serial.print("rawSoc = ");Serial.print(_receivedResponse.rawSoc, DEC);Serial.println("");
    Serial.print("rawTemperatureMosfet = ");Serial.print(_receivedResponse.rawTemperatureMosfet, DEC);Serial.println("");
    Serial.print("rawMaxCellVoltage = ");Serial.print(_receivedResponse.rawMaxCellVoltage, DEC);Serial.println("");
    Serial.print("rawMinCellVoltage = ");Serial.print(_receivedResponse.rawMinCellVoltage, DEC);Serial.println("");

    Serial.print("totalVoltage = ");Serial.print(_receivedResponse.totalVoltage, DEC);Serial.println(" V");
    Serial.print("cells = ");Serial.print((_receivedResponse.cells), DEC);Serial.println("");
    for (int i = 0; i < MAX_CELLS_SUPPORTED_BY_BMS; ++i) {
        Serial.print("cellVoltage["); Serial.print(i+1); Serial.print("] = "); Serial.print(_receivedResponse.cellVoltage[i], DEC); Serial.println(" V");
    }
    Serial.print("current = ");Serial.print(_receivedResponse.current, DEC);Serial.println(" A");
    Serial.print("soc = ");Serial.print(_receivedResponse.soc, DEC);Serial.println(" %");
    Serial.print("totalBatteryCapacitySetting = ");Serial.print(_receivedResponse.totalBatteryCapacitySetting, DEC);Serial.println(" Ah");
    Serial.print("capacityRemaining = ");Serial.print(_receivedResponse.capacityRemaining, DEC);Serial.println(" Ah");
    Serial.print("batteryCycleCapacity = ");Serial.print(_receivedResponse.batteryCycleCapacity, DEC);Serial.println(" Ah");
    Serial.print("totalRuntime = ");Serial.print(_receivedResponse.batteryCycleCapacity, DEC);Serial.println(" s");
    Serial.print("temperatures[0] (MOSFET) = ");Serial.print(_receivedResponse.temperatures[TEMPERATURE_MOSFET], DEC);Serial.println(" C");
    Serial.print("temperatures[1] (BALANCER) = ");Serial.print(_receivedResponse.temperatures[TEMPERATURE_BALANCER], DEC);Serial.println(" C");
    Serial.print("temperatures[2] (1) = ");Serial.print(_receivedResponse.temperatures[TEMPERATURE_SENSOR_1], DEC);Serial.println(" C");
    Serial.print("temperatures[3] (2) = ");Serial.print(_receivedResponse.temperatures[TEMPERATURE_SENSOR_2], DEC);Serial.println(" C");
    Serial.print("temperatures[4] (3) = ");Serial.print(_receivedResponse.temperatures[TEMPERATURE_SENSOR_3], DEC);Serial.println(" C");
    Serial.print("temperatures[5] (4) = ");Serial.print(_receivedResponse.temperatures[TEMPERATURE_SENSOR_4], DEC);Serial.println(" C");
    Serial.print("chargeMosfetStatus = ");Serial.print(_receivedResponse.chargeMosfetStatus, DEC);Serial.println("");
    Serial.print("chargingSwitch = ");Serial.print(_receivedResponse.chargingSwitch, DEC);Serial.println(" 1/0");
    Serial.print("dischargeMosfetStatus = ");Serial.print(_receivedResponse.dischargeMosfetStatus, DEC);Serial.println("");
    Serial.print("dischargingSwitch = ");Serial.print(_receivedResponse.dischargingSwitch, DEC);Serial.println(" 1/0");
    Serial.print("balancerStatus = ");Serial.print(_receivedResponse.balancerStatus, DEC);Serial.println("");
    Serial.print("balancerSwitch = ");Serial.print(_receivedResponse.balancerSwitch, DEC);Serial.println(" 1/0");
    Serial.print("power = ");Serial.print(_receivedResponse.power, DEC);Serial.println(" W");
    Serial.print("cellWithHighestVoltage = ");Serial.print(_receivedResponse.cellWithHighestVoltage, DEC);Serial.println("");
    Serial.print("maxCellVoltage = ");Serial.print(_receivedResponse.maxCellVoltage, DEC);Serial.println(" V");
    Serial.print("cellWithLowestVoltage = ");Serial.print(_receivedResponse.cellWithLowestVoltage, DEC);Serial.println("");
    Serial.print("minCellVoltage = ");Serial.print(_receivedResponse.minCellVoltage, DEC);Serial.println(" V");
    Serial.print("deltaCellVoltage = ");Serial.print(_receivedResponse.deltaCellVoltage, DEC);Serial.println(" V");
    Serial.print("averageCellVoltage = ");Serial.print(_receivedResponse.averageCellVoltage, DEC);Serial.println(" V");
    Serial.print("batteryStrings = ");Serial.print(_receivedResponse.batteryStrings, DEC);Serial.println("");

    Serial.print("Valid Responses = ");Serial.println(_bmsValidResponseCounter);
    Serial.print("Invalid Responses = ");Serial.println(_bmsInvalidResponseCounter);
    Serial.print("Ms = ");Serial.println((millis()) - (milli_start));
    Serial.println("--- End Formatted ---");

#ifdef USE_WIFI_AND_MQTT
    static unsigned long lastMqttSent = 0;

    char topicResponse[100] = ""; // 100 should cover a topic name
    char stateAddition[1024] = ""; // 256 should cover individual additions to be added to the payload.

    strcpy(topicResponse, DEVICE_NAME MQTT_BMS_DETAILS);

    // Time to query BMS?
    if (checkTimer(&lastMqttSent, MQTT_SEND_INTERVAL))
    {
        addToPayload("{\r\n");

        sprintf(stateAddition, "    \"rawTotalVoltage\": %d,\r\n", _receivedResponse.rawTotalVoltage);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"rawCurrent\": %d,\r\n", _receivedResponse.rawCurrent);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"rawCurrentAsFloat\": %d,\r\n", _receivedResponse.rawCurrentAsFloat);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"rawSoc\": %d,\r\n", _receivedResponse.rawSoc);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"rawTemperatureMosfet\": %d,\r\n", _receivedResponse.rawTemperatureMosfet);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"rawMaxCellVoltage\": %d,\r\n", _receivedResponse.rawMaxCellVoltage);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"rawMinCellVoltage\": %d,\r\n", _receivedResponse.rawMinCellVoltage);addToPayload(stateAddition);

        sprintf(stateAddition, "    \"totalVoltage\": %f,\r\n", _receivedResponse.totalVoltage);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"cells\": %d,\r\n", _receivedResponse.cells);addToPayload(stateAddition);
        for (int i = 0; i < MAX_CELLS_SUPPORTED_BY_BMS; ++i) {
            sprintf(stateAddition, "    \"cellVoltage%d\": %f,\r\n", i+1, _receivedResponse.cellVoltage[i]); addToPayload(stateAddition);
        }
        sprintf(stateAddition, "    \"current\": %f,\r\n", _receivedResponse.current);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"soc\": %d,\r\n", _receivedResponse.soc);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"totalBatteryCapacitySetting\": %f,\r\n", _receivedResponse.totalBatteryCapacitySetting);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"capacityRemaining\": %f,\r\n", _receivedResponse.capacityRemaining);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"batteryCycleCapacity\": %f,\r\n", _receivedResponse.batteryCycleCapacity);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"totalRuntime\": %f,\r\n", _receivedResponse.totalRuntime);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"temperatures1Mosfet\": %f,\r\n", _receivedResponse.temperatures[TEMPERATURE_MOSFET]);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"temperatures2Balancer\": %f,\r\n", _receivedResponse.temperatures[TEMPERATURE_BALANCER]);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"temperatures3Sensor1\": %f,\r\n", _receivedResponse.temperatures[TEMPERATURE_SENSOR_1]);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"temperatures4Sensor2\": %f,\r\n", _receivedResponse.temperatures[TEMPERATURE_SENSOR_2]);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"temperatures5Sensor3\": %f,\r\n", _receivedResponse.temperatures[TEMPERATURE_SENSOR_3]);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"temperatures6Sensor4\": %f,\r\n", _receivedResponse.temperatures[TEMPERATURE_SENSOR_4]);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"chargeMosfetStatus\": %d,\r\n", _receivedResponse.chargeMosfetStatus);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"chargingSwitch\": %d,\r\n", _receivedResponse.chargingSwitch);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"dischargeMosfetStatus\": %d,\r\n", _receivedResponse.dischargeMosfetStatus);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"dischargingSwitch\": %d,\r\n", _receivedResponse.dischargingSwitch);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"balancerStatus\": %d,\r\n", _receivedResponse.balancerStatus);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"balancerSwitch\": %d,\r\n", _receivedResponse.balancerSwitch);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"power\": %f,\r\n", _receivedResponse.power);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"cellWithHighestVoltage\": %d,\r\n", _receivedResponse.cellWithHighestVoltage);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"maxCellVoltage\": %f,\r\n", _receivedResponse.maxCellVoltage);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"cellWithLowestVoltage\": %d,\r\n", _receivedResponse.cellWithLowestVoltage);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"minCellVoltage\": %f,\r\n", _receivedResponse.minCellVoltage);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"deltaCellVoltage\": %f,\r\n", _receivedResponse.deltaCellVoltage);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"averageCellVoltage\": %f,\r\n", _receivedResponse.averageCellVoltage);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"batteryStrings\": %d,\r\n", _receivedResponse.batteryStrings);addToPayload(stateAddition);

        sprintf(stateAddition, "    \"_bmsValidResponseCounter\": %d,\r\n", _bmsValidResponseCounter);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"_bmsInvalidResponseCounter\": %d,\r\n", _bmsInvalidResponseCounter);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"_canSuccessCounter\": %d,\r\n", _canSuccessCounter);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"_canFailureCounter\": %d\r\n", _canFailureCounter);addToPayload(stateAddition);

        addToPayload("}");
        sendMqtt(topicResponse);
    }
#endif

}

#ifdef USE_WIFI_AND_MQTT
/*
setupWifi

Connect to WiFi
*/
void setupWifi()
{
    // We start by connecting to a WiFi network
#ifdef DEBUG
    sprintf(_debugOutput, "Connecting to %s", WIFI_SSID);
    Serial.println(_debugOutput);
#endif

    // Set up in Station Mode - Will be connecting to an access point
    WiFi.mode(WIFI_STA);

    // And connect to the details defined at the top
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);


    // We can't have a lack of wifi stopping the device speaking between BMS and inverter
    // So try the connect and move on, even if failure.

    if (WiFi.status() == WL_CONNECTED)
    {
        // Set the hostname for this Arduino
        WiFi.hostname(DEVICE_NAME);

        // Output some debug information
#ifdef DEBUG
        Serial.print("WiFi connected, IP is");
        Serial.print(WiFi.localIP());
#endif
    }
}


/*
emptyPayload

Clears every char so end of string can be easily found
*/
void emptyPayload()
{
    for (int i = 0; i < _maxPayloadSize; i++)
    {
        _mqttPayload[i] = '\0';
    }
}


/*
mqttCallback()

// This function is executed when an MQTT message arrives on a topic that we are subscribed to.
*/
void mqttCallback(char* topic, byte* message, unsigned int length)
{
    return;
}


/*
addToPayload()

Adds characters to the payload buffer
*/
void addToPayload(char* addition)
{
    int targetRequestedSize = strlen(_mqttPayload) + strlen(addition);

    // If max payload size is 2048 it is stored as (0-2047), however character 2048  (position 2047) is null terminator so 2047 chars usable usable
    if (targetRequestedSize > _maxPayloadSize - 1)
    {
        // Safely print using snprintf
        snprintf(_mqttPayload, _maxPayloadSize, "{\r\n    \"mqttError\": \"Length of payload exceeds %d bytes.  Length would be %d bytes.\"\r\n}", _maxPayloadSize - 1, targetRequestedSize);

        emptyPayload();
    }
    else
    {
        // Add to the payload by sprintf back on itself with the addition
        sprintf(_mqttPayload, "%s%s", _mqttPayload, addition);
    }
}


/*
mqttReconnect

This function reconnects the ESP32 to the MQTT broker
*/
void mqttReconnect()
{
    bool subscribed = false;
    char subscriptionDef[100];

    // Again, can't have the device stuck until MQTT established... so just try the once.
    // If it works, it works.
    // Just in case.
    _mqtt.disconnect();
    delay(500);


#ifdef DEBUG
    Serial.print("Attempting MQTT connection...");
#endif

    // Attempt to connect
    if (_mqtt.connect(DEVICE_NAME, MQTT_USERNAME, MQTT_PASSWORD))
    {
        Serial.println("Connected MQTT");
    }
}





/*
sendMqtt

Sends whatever is in the modular level payload to the specified topic.
*/
void sendMqtt(char* topic)
{
    // Attempt a send
    if (!_mqtt.publish(topic, _mqttPayload))
    {
#ifdef DEBUG
        sprintf(_debugOutput, "MQTT publish failed to %s", topic);
        Serial.println(_debugOutput);
        Serial.println(_mqttPayload);
#endif
    }
    else
    {
#ifdef DEBUG
        //sprintf(_debugOutput, "MQTT publish success");
        //Serial.println(_debugOutput);
#endif
    }

    // Empty payload for next use.
    emptyPayload();
    return;
}




void sendMqttState(int state, size_t bytesReceived, unsigned long id, byte canResult, unsigned long txErrorCount)
{
    char topicResponse[100] = ""; // 100 should cover a topic name
    char stateAddition[1024] = ""; // 256 should cover individual additions to be added to the payload.


    if (state == BMS_ERRORTYPE_NOTENOUGHDATA)
    {
        strcpy(topicResponse, DEVICE_NAME MQTT_BMS_NOTENOUGHDATA);

        addToPayload("{\r\n");
        sprintf(stateAddition, "    \"bytesReceived\": %d\r\n", bytesReceived);addToPayload(stateAddition);
        addToPayload("}");
    }
    else if (state == BMS_ERRORTYPE_FAILEDCHECKSUM)
    {
        strcpy(topicResponse, DEVICE_NAME MQTT_BMS_FAILEDCHECKSUM);
        addToPayload("{\r\n");
        addToPayload("}");
    }
    else if (state == BMS_ERRORTYPE_FAILEDHEADER)
    {
        strcpy(topicResponse, DEVICE_NAME MQTT_BMS_FAILEDHEADER);
        addToPayload("{\r\n");
        addToPayload("}");
    }
    else if (state == CAN_ERRORTYPE_CANFAILSEND)
    {
        strcpy(topicResponse, DEVICE_NAME MQTT_CAN_FAILSEND);
        addToPayload("{\r\n");
        sprintf(stateAddition, "    \"id\": 0x%.8lX\r\n", id);addToPayload(stateAddition);
        sprintf(stateAddition, "    \"canResult\": %d\r\n", canResult);addToPayload(stateAddition);
        addToPayload("}");
    }
    else if (state == CAN_ERRORTYPE_CANTXERRORCOUNT)
    {
        strcpy(topicResponse, DEVICE_NAME MQTT_CAN_TXERRORCOUNT);
        addToPayload("{\r\n");
        sprintf(stateAddition, "    \"txErrorCount\": %d\r\n", txErrorCount);addToPayload(stateAddition);
        addToPayload("}");
    }
    


    // Send the error on its way
    sendMqtt(topicResponse);
}


