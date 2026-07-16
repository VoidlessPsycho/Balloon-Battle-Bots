/*
  reciever move the body
*/

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <ESP32Servo.h>
#include <math.h>

#define ESPNOW_CHANNEL 3

#define CONTROLLER_ID_LEFT  0
#define CONTROLLER_ID_RIGHT 1


const int BODY_SERVO_PIN = 6;
const int ARMS_SERVO_PIN = 7;


const int REST_ANGLE = 90; 

//tooning values
const float ACCEL_FORWARD_MAX = 15.0; 
const float GYRO_ROTATION_MAX = 3.0;  


const float ACCEL_RESET_THRESHOLD = 9.0; 


const float ACCEL_DEADZONE = 0.5;
const float GYRO_DEADZONE  = 0.1;


const float SMOOTHING_ALPHA = 0.3;


const unsigned long CONTROLLER_TIMEOUT_MS = 1000;

const bool DEBUG_PRINT_ANGLES = true;
const unsigned long DEBUG_PRINT_INTERVAL_MS = 200;
unsigned long lastDebugPrint = 0;

Servo bodyServo;
Servo armsServo;

typedef struct struct_message {
  uint8_t controllerId;
  float accelX;
  float gyroY;
} struct_message;

struct ControllerState {
  float accelX = 0;
  float gyroY = 0;
  unsigned long lastSeenMs = 0;
};

volatile ControllerState leftState;
volatile ControllerState rightState;

float smoothedBodyAngle = REST_ANGLE;
float smoothedArmsAngle = REST_ANGLE;

float applyDeadzone(float value, float deadzone) {
  return (fabs(value) < deadzone) ? 0.0f : value;
}

float mapFloat(float x, float inMin, float inMax, float outMin, float outMax) {
  x = constrain(x, inMin, inMax);
  return outMin + (x - inMin) * (outMax - outMin) / (inMax - inMin);
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(struct_message)) return;
  struct_message incoming;
  memcpy(&incoming, data, sizeof(incoming));

  unsigned long now = millis();
  if (incoming.controllerId == CONTROLLER_ID_LEFT) {
    leftState.accelX = incoming.accelX;
    leftState.gyroY = incoming.gyroY;
    leftState.lastSeenMs = now;
  } else if (incoming.controllerId == CONTROLLER_ID_RIGHT) {
    rightState.accelX = incoming.accelX;
    rightState.gyroY = incoming.gyroY;
    rightState.lastSeenMs = now;
  }
}

void setup() {
  Serial.begin(115200);

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  bodyServo.setPeriodHertz(50);
  armsServo.setPeriodHertz(50);
  bodyServo.attach(BODY_SERVO_PIN, 500, 2400);
  armsServo.attach(ARMS_SERVO_PIN, 500, 2400);
  bodyServo.write(REST_ANGLE);
  armsServo.write(REST_ANGLE);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_ps(WIFI_PS_NONE);

  Serial.print("Receiver MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("^ copy this into RECEIVER_MAC on BOTH controller sketches");

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);
}

void loop() {
  unsigned long now = millis();

  
  float rAccel = (now - rightState.lastSeenMs < CONTROLLER_TIMEOUT_MS) ? rightState.accelX : 0.0f;
  float lAccel = (now - leftState.lastSeenMs  < CONTROLLER_TIMEOUT_MS) ? leftState.accelX  : 0.0f;
  float rGyro  = (now - rightState.lastSeenMs < CONTROLLER_TIMEOUT_MS) ? rightState.gyroY  : 0.0f;
  float lGyro  = (now - leftState.lastSeenMs  < CONTROLLER_TIMEOUT_MS) ? leftState.gyroY   : 0.0f;

  //tilt to reset in case yk
  if (fabs(rAccel) >= ACCEL_RESET_THRESHOLD || fabs(lAccel) >= ACCEL_RESET_THRESHOLD) {
    
    rightState.accelX = 0.0f;
    rightState.gyroY = 0.0f;
    leftState.accelX = 0.0f;
    leftState.gyroY = 0.0f;
    
    rAccel = 0.0f;
    lAccel = 0.0f;
    rGyro = 0.0f;
    lGyro = 0.0f;

    //insta snap
    smoothedArmsAngle = REST_ANGLE;
    smoothedBodyAngle = REST_ANGLE;
    
    if (DEBUG_PRINT_ANGLES) {
      Serial.println("--- SYSTEM RESET TRIGGERED BY 90-DEGREE TILT ---");
    }
  }

  rAccel = applyDeadzone(rAccel, ACCEL_DEADZONE);
  lAccel = applyDeadzone(lAccel, ACCEL_DEADZONE);
  rGyro  = applyDeadzone(rGyro, GYRO_DEADZONE);
  lGyro  = applyDeadzone(lGyro, GYRO_DEADZONE);

  .
  float armsInput = rAccel - lAccel;
  float armsTarget = mapFloat(armsInput, -ACCEL_FORWARD_MAX, ACCEL_FORWARD_MAX, 0, 180);

  // Body rotation avg
  float bodyInput = (rGyro + lGyro) / 2.0f;
  float bodyTarget = mapFloat(bodyInput, -GYRO_ROTATION_MAX, GYRO_ROTATION_MAX, 0, 180);

  smoothedArmsAngle += SMOOTHING_ALPHA * (armsTarget - smoothedArmsAngle);
  smoothedBodyAngle += SMOOTHING_ALPHA * (bodyTarget - smoothedBodyAngle);

  armsServo.write((int)smoothedArmsAngle);
  bodyServo.write((int)smoothedBodyAngle);

  if (DEBUG_PRINT_ANGLES && (now - lastDebugPrint) > DEBUG_PRINT_INTERVAL_MS) {
    lastDebugPrint = now;
    Serial.print("arms=");
    Serial.print(smoothedArmsAngle, 1);
    Serial.print("  body=");
    Serial.println(smoothedBodyAngle, 1);
  }

  delay(10);
}