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

// If |gyroZ| stays under this, the controller is treated as "parked" / not
// being twisted, and its accelX/gyroY baseline keeps getting re-captured.
// The moment gyroZ leaves this window, the baseline freezes wherever it
// last was — that frozen reading becomes the user's new "zero" pose.
// NOTE: gyro only measures rotation RATE, not orientation, so this detects
// "not currently turning," which is not the same thing as "lying flat" in
// every physical case. If you actually need true flatness (device resting
// on a table regardless of whether it was just spun), that has to come
// from the accelerometer (gravity vector) instead — happy to add that if
// this doesn't behave the way you want in practice.
const float FLAT_GYROZ_THRESHOLD = 0.15; // rad/s (~8.6 deg/s) — tune to taste


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
  float gyroZ;
} struct_message;

struct ControllerState {
  float accelX = 0;
  float gyroY = 0;
  float gyroZ = 0;

  // captured "zero" reference — see FLAT_GYROZ_THRESHOLD above
  float baselineAccelX = 0;
  float baselineGyroY = 0;
  bool isFlat = true; // start parked so we don't jump on boot

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
    leftState.gyroY  = incoming.gyroY;
    leftState.gyroZ  = incoming.gyroZ;
    leftState.lastSeenMs = now;
  } else if (incoming.controllerId == CONTROLLER_ID_RIGHT) {
    rightState.accelX = incoming.accelX;
    rightState.gyroY  = incoming.gyroY;
    rightState.gyroZ  = incoming.gyroZ;
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

  bool rTimedOut = (now - rightState.lastSeenMs) >= CONTROLLER_TIMEOUT_MS;
  bool lTimedOut = (now - leftState.lastSeenMs)  >= CONTROLLER_TIMEOUT_MS;

  float rAccelRaw = rTimedOut ? 0.0f : rightState.accelX;
  float lAccelRaw = lTimedOut ? 0.0f : leftState.accelX;
  float rGyroRaw  = rTimedOut ? 0.0f : rightState.gyroY;
  float lGyroRaw  = lTimedOut ? 0.0f : leftState.gyroY;
  float rGyroZ    = rTimedOut ? 0.0f : rightState.gyroZ;
  float lGyroZ    = lTimedOut ? 0.0f : leftState.gyroZ;

  // tilt to reset in case yk (this runs on RAW readings, before zeroing)
  if (fabs(rAccelRaw) >= ACCEL_RESET_THRESHOLD || fabs(lAccelRaw) >= ACCEL_RESET_THRESHOLD) {

    rightState.accelX = 0.0f;
    rightState.gyroY  = 0.0f;
    leftState.accelX  = 0.0f;
    leftState.gyroY   = 0.0f;

    rAccelRaw = 0.0f;
    lAccelRaw = 0.0f;
    rGyroRaw  = 0.0f;
    lGyroRaw  = 0.0f;

    //insta snap
    smoothedArmsAngle = REST_ANGLE;
    smoothedBodyAngle = REST_ANGLE;

    if (DEBUG_PRINT_ANGLES) {
      Serial.println("--- SYSTEM RESET TRIGGERED BY 90-DEGREE TILT ---");
    }
  }

  // --- Zero / "parked" handling ---
  // While gyroZ stays near 0, keep sampling the current reading as the
  // baseline. The instant it leaves that window, the baseline stops
  // updating and stays frozen at whatever it last was — that becomes
  // "zero" for the user until the next time it goes near-still again.
  rightState.isFlat = fabs(rGyroZ) < FLAT_GYROZ_THRESHOLD;
  leftState.isFlat  = fabs(lGyroZ) < FLAT_GYROZ_THRESHOLD;

  if (rightState.isFlat) {
    rightState.baselineAccelX = rAccelRaw;
    rightState.baselineGyroY  = rGyroRaw;
  }
  if (leftState.isFlat) {
    leftState.baselineAccelX = lAccelRaw;
    leftState.baselineGyroY  = lGyroRaw;
  }

  float rAccel = rAccelRaw - rightState.baselineAccelX;
  float lAccel = lAccelRaw - leftState.baselineAccelX;
  float rGyro  = rGyroRaw  - rightState.baselineGyroY;
  float lGyro  = lGyroRaw  - leftState.baselineGyroY;

  rAccel = applyDeadzone(rAccel, ACCEL_DEADZONE);
  lAccel = applyDeadzone(lAccel, ACCEL_DEADZONE);
  rGyro  = applyDeadzone(rGyro, GYRO_DEADZONE);
  lGyro  = applyDeadzone(lGyro, GYRO_DEADZONE);

  // Whichever hand is tilted more (by magnitude) wins — you only need to
  // move one controller to drive the arms, the other can just sit still.
  bool rightIsDriving = fabs(rAccel) >= fabs(lAccel);
  float armsInput = rightIsDriving ? rAccel : lAccel;
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
    Serial.print(" (");
    Serial.print(rightIsDriving ? "R" : "L");
    Serial.print(" driving)");
    Serial.print("  body=");
    Serial.print(smoothedBodyAngle, 1);
    Serial.print("  [R flat=");
    Serial.print(rightState.isFlat ? "Y" : "N");
    Serial.print(" L flat=");
    Serial.print(leftState.isFlat ? "Y" : "N");
    Serial.println("]");
  }

  delay(10);
}