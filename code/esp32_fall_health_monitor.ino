#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <MPU6050.h>
#include <MAX30105.h>
#include "heartRate.h"

MPU6050 mpu;
MAX30105 particleSensor;

// ---------------- WiFi & Telegram ----------------
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
String botToken = "YOUR_BOT_TOKEN";
String chatID = "CHAT ID";

// ---------------- Pins ----------------
#define BUZZER_PIN 25
#define CANCEL_BUTTON 0

// ---------------- Thresholds ----------------
#define IR_FINGER_THRESHOLD 20000
#define BPM_STABLE_TIME 3000   // 3 seconds

float FALL_THRESHOLD = 2.5;
float MOTION_THRESHOLD = 1.2;
int LOW_BPM = 45;
int HIGH_BPM = 120;

// ---------------- Heart ----------------
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
int beatAvg = 0;

unsigned long lastValidBPMTime = 0;

// ---------------- Fall ----------------
bool possibleFall = false;
unsigned long fallTime = 0;

// =================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH);
  pinMode(CANCEL_BUTTON, INPUT_PULLUP);

  Wire.begin(21, 22);

  // MPU6050
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("❌ MPU6050 failed");
    while (1);
  }
  Serial.println("✅ MPU6050 OK");

  // MAX30102
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("❌ MAX30102 failed");
    while (1);
  }
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);
  Serial.println("✅ MAX30102 OK");

  // WiFi (safe)
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n⚠️ WiFi failed – running offline");
  }

  Serial.println("System Ready\n");
}

// =================================================

void loop() {

  // ---------------- HEART SENSOR ----------------
  long irValue = particleSensor.getIR();
  Serial.print("IR: ");
  Serial.print(irValue);

  if (checkForBeat(irValue)) {
    long delta = millis() - lastBeat;
    lastBeat = millis();

    float bpm = 60 / (delta / 1000.0);

    if (bpm > 30 && bpm < 200) {
      rates[rateSpot++] = (byte)bpm;
      rateSpot %= RATE_SIZE;

      beatAvg = 0;
      for (byte i = 0; i < RATE_SIZE; i++)
        beatAvg += rates[i];
      beatAvg /= RATE_SIZE;

      lastValidBPMTime = millis();
    }
  }

  Serial.print(" | BPM: ");
  Serial.print(beatAvg);

  // ---------------- FALL SENSOR ----------------
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);

  float Ax = ax / 16384.0;
  float Ay = ay / 16384.0;
  float Az = az / 16384.0;
  float totalAcc = sqrt(Ax*Ax + Ay*Ay + Az*Az);

  Serial.print(" | Acc: ");
  Serial.println(totalAcc);

  // ---------------- FALL LOGIC ----------------
  if (totalAcc > FALL_THRESHOLD && !possibleFall) {
    possibleFall = true;
    fallTime = millis();
    Serial.println("⚠ Possible fall detected...");
  }

  if (possibleFall && millis() - fallTime > 10000) {
    if (totalAcc < MOTION_THRESHOLD) {
      Serial.println("🚨 CONFIRMED FALL (CRITICAL)");
      fallEmergency();
      sendTelegram("🚨 CRITICAL ALERT: Fall detected!");
    }
    possibleFall = false;
  }

  // ---------------- HEART LOGIC (FIXED) ----------------
  if (irValue > IR_FINGER_THRESHOLD &&
      beatAvg > 0 &&
      millis() - lastValidBPMTime > BPM_STABLE_TIME) {

    if (beatAvg < LOW_BPM || beatAvg > HIGH_BPM) {
      Serial.println("⚠ MEDIUM RISK: Abnormal heart rate");
      heartWarning();
      sendTelegram("⚠ Abnormal heart rate detected!");
    }
  }

  delay(40);
}

// =================================================

void heartWarning() {
  for (int i = 0; i < 3; i++) {
    if (digitalRead(CANCEL_BUTTON) == LOW) return;
    digitalWrite(BUZZER_PIN, LOW);
    delay(200);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(400);
  }
}

void fallEmergency() {
  unsigned long start = millis();
  while (millis() - start < 8000) {
    if (digitalRead(CANCEL_BUTTON) == LOW) return;
    digitalWrite(BUZZER_PIN, LOW);
    delay(300);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(150);
  }
}

void sendTelegram(String message) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "https://api.telegram.org/bot" + botToken +
                 "/sendMessage?chat_id=" + chatID +
                 "&text=" + message;
    http.begin(url);
    http.GET();
    http.end();
    Serial.println("📨 Telegram sent");
  }
}
