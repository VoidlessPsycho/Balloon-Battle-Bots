/*
JAB CONTROLLER
you put SCL on 6 and SDA on 7 hehe six sevennnn
5v
*/

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

#define ESPNOW_CHANNEL 3

// Add mac address of transmittetr aand reviever
//E0:72:A1:72:9B:FC transmitter B
//90:64:9B:07:1F:14 transmitter A
uint8_t RECEIVER_MAC[] = {0x90, 0x64, 0x9B, 0x07, 0x1F, 0x14};

//remov noise
const float JAB_THRESHOLD = 6.7;

//remov misfiore
const unsigned long DEBOUNCE_MS = 500;

// how far the body can "lean" in either direction before we clamp it,
// in m/s^2 of X-axis acceleration. Tune this by watching Serial output
// while tilting the sensor to its extremes.
const float TILT_ACCEL_RANGE = 8.0;

// CHANGED: Using a C-style string instead of an Arduino String object
const char* TYPE = "JAB";

//six sevennnn
const int SDA_PIN = 7;
const int SCL_PIN = 6;

Adafruit_MPU6050 mpu;
unsigned long lastJabTime = 0;

typedef struct struct_message {
  char command[10];
  float tiltX;   // raw X-axis acceleration, sent every loop for the body servo
} struct_message;

struct_message outgoingData;

void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  if (!mpu.begin()) {
    Serial.println("MPU6050 not found — check wiring!");
    while (1) delay(10);
  }
  Serial.println("MPU6050 found.");

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_ps(WIFI_PS_NONE);

  Serial.print("Sender MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, RECEIVER_MAC, 6);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add ESP-NOW peer");
    return;
  }

  strcpy(outgoingData.command, "NONE");
  outgoingData.tiltX = 0;
}

void loop() {
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  float gyroX = gyro.gyro.x;
  float accelY = accel.acceleration.y;

  // Uncomment while tuning JAB_THRESHOLD / TILT_ACCEL_RANGE:
  Serial.print(accelY); Serial.print("\t"); Serial.println(gyroX);

  // default: no discrete event this packet
  strcpy(outgoingData.command, "NONE");

  if (accelY > JAB_THRESHOLD && (millis() - lastJabTime) > DEBOUNCE_MS) {
    lastJabTime = millis();
    Serial.print("Jab detected, accelY=");
    Serial.println(accelY);
    
    // This will now compile perfectly
    strcpy(outgoingData.command, TYPE);
  }

  // always send the current X tilt so the body servo can track it live
  outgoingData.tiltX = gyroX;

  esp_now_send(RECEIVER_MAC, (uint8_t *)&outgoingData, sizeof(outgoingData));

  delay(10); // 100hz sampining
}