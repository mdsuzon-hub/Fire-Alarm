#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <ESP8266WiFi.h>
#include <DHT.h>
#include <FirebaseESP8266.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// WiFi credentials
const char* ssid = "*_*";
const char* password = "@honey@bee";

// Firebase config/auth objects
FirebaseConfig config;
FirebaseAuth auth;
FirebaseData fbdo;

// Firebase credentials ------------------------------------------------------------
#define FIREBASE_HOST "DB-HOST"
#define FIREBASE_AUTH "DB_AUTH"

// DHT11 Sensor
#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Flame Sensor (Digital)
#define FLAME_SENSOR_PIN D5

// Smoke Sensor (Analog)
#define SMOKE_SENSOR A0

// Alert devices
#define RED_LED   D6
#define GREEN_LED D7
#define BUZZER    D8

unsigned long wifiStartTime = 0;
bool wifiConnected = false;

// Threshold for smoke detection ------------------------------------------
const int SMOKE_THRESHOLD = 600;

// Include your bitmaps & definitions here:
#include "diu.h"

void drawBootAnimation() {
  display.clearDisplay();

  for (int r = 0; r < 32; r += 2) {
    display.clearDisplay();
    display.drawCircle(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, r, SH110X_WHITE);
    display.display();
    delay(30);
  }

  for (int x = 0; x <= SCREEN_WIDTH; x += 4) {
    display.fillRect(0, SCREEN_HEIGHT / 2 - 5, x, 10, SH110X_WHITE);
    display.display();
    delay(10);
  }

  display.clearDisplay();
  int x = (SCREEN_WIDTH - epd_bitmap_contrast_drop_line_width) / 2;
  int y = (SCREEN_HEIGHT - epd_bitmap_contrast_drop_line_height) / 2;
  display.drawBitmap(x, y, epd_bitmap_contrast_drop_line,
                     epd_bitmap_contrast_drop_line_width,
                     epd_bitmap_contrast_drop_line_height,
                     SH110X_WHITE);
  display.display();
  delay(3000);
}

void showWiFiConnecting() {
  display.clearDisplay();
  int x = (SCREEN_WIDTH - signal_wifi_width) / 2;
  int y = (SCREEN_HEIGHT - signal_wifi_height) / 2 - 10;
  display.drawBitmap(x, y, signal_wifi, signal_wifi_width, signal_wifi_height, SH110X_WHITE);

  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor((SCREEN_WIDTH - 9 * 6) / 2, y + 35);
  display.print("Connecting");
  display.display();
}

void showWiFiError() {
  display.clearDisplay();
  int x = (SCREEN_WIDTH - signal_wifi_error_width) / 2;
  int y = (SCREEN_HEIGHT - signal_wifi_error_height) / 2 - 10;
  display.drawBitmap(x, y, signal_wifi_error, signal_wifi_error_width, signal_wifi_error_height, SH110X_WHITE);

  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor((SCREEN_WIDTH - 24 * 6) / 2, y + 35);
  display.print("WiFi connect error");
  display.display();
}

void fireAlert() {
  for (int i = 0; i < 5; i++) {
    digitalWrite(RED_LED, HIGH);
    tone(BUZZER, 1000);
    delay(100);
    digitalWrite(RED_LED, LOW);
    noTone(BUZZER);
    delay(100);
  }
}

void smokeAlert() {
  digitalWrite(GREEN_LED, HIGH);
  tone(BUZZER, 500);
  delay(300);
  digitalWrite(GREEN_LED, LOW);
  noTone(BUZZER);
  delay(700);
}

void showSensorValues(bool showWiFi, bool flameDetected, int smokeValue, float temp, float humid) {
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  int margin_left = 2;
  int line_spacing = 11;
  int y = 5;

  if (showWiFi && !flameDetected && smokeValue <= SMOKE_THRESHOLD) {
    int rssi = WiFi.RSSI();
    display.setCursor((SCREEN_WIDTH - 70) / 2, 0);
    display.print("WiFi: ");
    display.print(rssi);
    display.print(" dBm");
  } else {
    display.setCursor((SCREEN_WIDTH - 50) / 2, 0);
    if (!flameDetected && smokeValue <= SMOKE_THRESHOLD)
      display.print("No WiFi");
  }

  if (flameDetected) {
    int iconX = (SCREEN_WIDTH - fire_width) / 2;
    display.drawBitmap(iconX, y, fire, fire_width, fire_height, SH110X_WHITE);
    y += fire_height + 2;
    display.setCursor(margin_left, y);
    display.print("Alert: Leave room!");
    display.display();
    return;
  }

  if (smokeValue > SMOKE_THRESHOLD) {
    int iconX = (SCREEN_WIDTH - wind_width) / 2;
    display.drawBitmap(iconX, y, wind, wind_width, wind_height, SH110X_WHITE);
    y += wind_height + 2;
    display.setCursor(margin_left, y);
    display.print("Warning: Leave room!");
    display.display();
    return;
  }

  y += line_spacing;

  display.setCursor(margin_left, y);
  if (isnan(temp)) {
    display.print("Temp sensor error!");
  } else {
    display.print("Temp: ");
    display.print(temp);
    display.print(" C");
  }
  y += line_spacing;

  display.setCursor(margin_left, y);
  if (isnan(humid)) {
    display.print("Humidity sensor error!");
  } else {
    display.print("Humidity: ");
    display.print(humid);
    display.print(" %");
  }
  y += line_spacing;

  display.setCursor(margin_left, y);
  display.print("Smoke: ");
  display.print(smokeValue);

  display.display();
}

void setup() {
  Serial.begin(115200);

  display.begin(0x3C, true);
  display.clearDisplay();
  drawBootAnimation();

  dht.begin();

  pinMode(FLAME_SENSOR_PIN, INPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(BUZZER, LOW);

  WiFi.begin(ssid, password);
  wifiStartTime = millis();

  // Firebase setup
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("[Firebase] Initialized with host: " + String(FIREBASE_HOST));
}

void loop() {
  bool flameDetected = (digitalRead(FLAME_SENSOR_PIN) == LOW);
  int smokeValue = analogRead(SMOKE_SENSOR);
  float temp = dht.readTemperature();
  float humid = dht.readHumidity();

  Serial.println("===== SENSOR READINGS =====");
  if (isnan(temp) || isnan(humid)) {
    Serial.println("❌ Failed to read from DHT sensor!");
  } else {
    Serial.print("🌡 Temperature: "); Serial.print(temp); Serial.println(" °C");
    Serial.print("💧 Humidity: "); Serial.print(humid); Serial.println(" %");
  }
  Serial.print("🔥 Flame: "); Serial.println(flameDetected ? "Detected!" : "None");
  Serial.print("💨 Smoke Level: "); Serial.print(smokeValue);
  if (smokeValue > SMOKE_THRESHOLD) Serial.println(" ⚠️ Warning!");
  else Serial.println(" ✅ Air Quality Normal");
  Serial.println("==========================\n");

  // Upload to Firebase if WiFi is connected
  if (WiFi.status() == WL_CONNECTED) {
    FirebaseJson json;
    json.set("fireDetected", flameDetected);
    json.set("smokeDetected", smokeValue > SMOKE_THRESHOLD);
    json.set("excessiveTemperature", (!isnan(temp) && temp > 35.0)); // example: temp > 50°C -------------------------------------
    json.set("temperature", temp);
    json.set("humidity", humid);
    json.set("smokeLevel", smokeValue);

    if (Firebase.setJSON(fbdo, "/", json)) {
      Serial.println("[Firebase] Data updated successfully");
    } else {
      Serial.println("[Firebase] Data update FAILED: " + fbdo.errorReason());
    }
  } else {
    Serial.println("[Firebase] Skipped update — No WiFi");
  }


  if (flameDetected) {
    fireAlert();
    digitalWrite(RED_LED, HIGH);
  } else {
    digitalWrite(RED_LED, LOW);
  }
  if (smokeValue > SMOKE_THRESHOLD) {
    smokeAlert();
    digitalWrite(GREEN_LED, HIGH);
  } else {
    digitalWrite(GREEN_LED, LOW);
  }

  if (WiFi.status() != WL_CONNECTED) {
    unsigned long elapsed = millis() - wifiStartTime;
    if (elapsed > 60000) {
      wifiConnected = false;
      showWiFiError();
      delay(2000);
      showSensorValues(false, flameDetected, smokeValue, temp, humid);
    } else {
      showWiFiConnecting();
      delay(500);
    }
  } else {
    wifiConnected = true;
    showSensorValues(true, flameDetected, smokeValue, temp, humid);
    delay(2000);
  }
}
