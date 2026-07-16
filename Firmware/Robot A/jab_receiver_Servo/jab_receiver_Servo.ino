/*
reciever
  pwm servo sg90 tower pro ultra
  arm servo  -> pwm pin 6
  body servo -> pwm pin 5   <-- wire this one up, adjust pin if needed
*/

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <ESP32Servo.h>

// Must match ESPNOW_CHANNEL in the sender sketches.
#define ESPNOW_CHANNEL 3

// ---- ARM SERVO CONFIG ----
const int ARM_SERVO_PIN = 6;
const int REST_ANGLE = 90;    // neutral / guard position
const int JAB_ANGLE   = 30;   // jab thrust position (60° one way from rest)
const int BLOCK_ANGLE = 150;  // block position — mirrored, 60° the OPPOSITE way from rest
const unsigned long JAB_HOLD_MS   = 150;
const unsigned long RETURN_PAUSE_MS = 200;

// ---- BODY SERVO CONFIG ----
const int BODY_SERVO_PIN = 5;
const int BODY_REST_ANGLE = 90;
const int BODY_TILT_SWING = 60;      // max degrees either side of rest
const float TILT_ACCEL_RANGE = 8.0;  // must match sender's TILT_ACCEL_RANGE

Servo jabServo;
Servo bodyServo;

volatile bool jabTriggered = false;
volatile bool blockTriggered = false;
volatile float latestTiltX = 0;

typedef struct struct_message {
  char command[10];
  float tiltX;
} struct_message;

struct_message incomingData;

float mapf(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

int tiltToBodyAngle(float accelX) {
  float a = constrain(accelX, -TILT_ACCEL_RANGE, TILT_ACCEL_RANGE);
  return (int)mapf(a, -TILT_ACCEL_RANGE, TILT_ACCEL_RANGE,
                    BODY_REST_ANGLE - BODY_TILT_SWING,
                    BODY_REST_ANGLE + BODY_TILT_SWING);
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(incomingData)) return;
  memcpy(&incomingData, data, sizeof(incomingData));

  latestTiltX = incomingData.tiltX;

  if (strcmp(incomingData.command, "JAB") == 0) {
    jabTriggered = true;
  } else if (strcmp(incomingData.command, "BLOCK") == 0) {
    blockTriggered = true;
  }
}

void setup() {
  Serial.begin(115200);

  ESP32PWM::allocateTimer(0);
  jabServo.setPeriodHertz(50);
  jabServo.attach(ARM_SERVO_PIN, 500, 2400);
  jabServo.write(REST_ANGLE);

  ESP32PWM::allocateTimer(1);
  bodyServo.setPeriodHertz(50);
  bodyServo.attach(BODY_SERVO_PIN, 500, 2400);
  bodyServo.write(BODY_REST_ANGLE);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_ps(WIFI_PS_NONE);

  Serial.print("Receiver MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("^ copy this into RECEIVER_MAC on BOTH sender sketches");

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);
}

void loop() {
  if (jabTriggered) {
    jabTriggered = false;
    performMove(JAB_ANGLE);
    Serial.println("yeah im jabbing rn");
  }
  if (blockTriggered) {
    blockTriggered = false;
    performMove(BLOCK_ANGLE);
    Serial.println("yeah im blocking rn");
  }

  bodyServo.write(tiltToBodyAngle(latestTiltX));
}

void performMove(int targetAngle) {
  jabServo.write(targetAngle);
  delay(JAB_HOLD_MS);
  jabServo.write(REST_ANGLE);
  delay(RETURN_PAUSE_MS);
}