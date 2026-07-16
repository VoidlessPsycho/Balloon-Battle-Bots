/*
LEFT CONTROLLER (Controller_L)
you put SCL on 6 and SDA on 8 for some reason but supposed to be 7 hehe six sevennnnn
5v

*/

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

#define ESPNOW_CHANNEL 3

// recievwer mac
uint8_t RECEIVER_MAC[] = {0x90, 0x64, 0x9B, 0x07, 0x1F, 0x14};

// six sevennnn
const int SDA_PIN = 8;
const int SCL_PIN = 6;

#define CONTROLLER_ID_LEFT  0
#define CONTROLLER_ID_RIGHT 1
const uint8_t MY_CONTROLLER_ID = CONTROLLER_ID_LEFT;


const bool DEBUG_PRINT_MPU = true;
const unsigned long DEBUG_PRINT_INTERVAL_MS = 200;
unsigned long lastDebugPrint = 0;

Adafruit_MPU6050 mpu;

typedef struct struct_message {
  uint8_t controllerId;
  float accelX;
  float gyroY;
} struct_message;

struct_message outgoingData;

void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAILED");
}

void printAllMPUValues(const sensors_event_t &accel, const sensors_event_t &gyro, const sensors_event_t &temp) {
  Serial.println(F("----- MPU6050 (Controller_L) -----"));
  Serial.print(F("Accel (m/s^2)  X: "));
  Serial.print(accel.acceleration.x, 3);
  Serial.print(F("  Y: "));
  Serial.print(accel.acceleration.y, 3);
  Serial.print(F("  Z: "));
  Serial.println(accel.acceleration.z, 3);

  Serial.print(F("Gyro  (rad/s)  X: "));
  Serial.print(gyro.gyro.x, 3);
  Serial.print(F("  Y: "));
  Serial.print(gyro.gyro.y, 3);
  Serial.print(F("  Z: "));
  Serial.println(gyro.gyro.z, 3);

  Serial.print(F("Temp (C): "));
  Serial.println(temp.temperature, 2);
  Serial.println(F("-----------------------------------"));
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  if (!mpu.begin()) {
    Serial.println("either ur wiring is bad or the mp6050 check ts");
    while (1) delay(10);
  }
  Serial.println("MPU6050 identified somehow");

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_ps(WIFI_PS_NONE);

  Serial.print("Controller_L mac address if u need it:");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed bro lock in");
    return;
  }
  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, RECEIVER_MAC, 6);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add ESP-NOW, yo lock in");
    return;
  }

  outgoingData.controllerId = MY_CONTROLLER_ID;
}

void loop() {
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  if (DEBUG_PRINT_MPU && (millis() - lastDebugPrint) > DEBUG_PRINT_INTERVAL_MS) {
    lastDebugPrint = millis();
    printAllMPUValues(accel, gyro, temp);
  }

  outgoingData.accelX = accel.acceleration.x;
  outgoingData.gyroY  = gyro.gyro.y;
  esp_now_send(RECEIVER_MAC, (uint8_t *)&outgoingData, sizeof(outgoingData));

  delay(10); // 100hz streaming
}