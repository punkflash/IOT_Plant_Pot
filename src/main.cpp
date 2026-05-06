#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include "DHT.h"

// WiFi Configuration
const char* ssid = "Bublik";       // Change this
const char* password = "32707632";  // Change this

// Pin Definitions
#define DHTPIN 4
#define DHTTYPE DHT11

#define WATER_PIN 34
#define PHOTORESISTOR_PIN 32
#define LED_PIN 16
#define BUZZER_PIN 25  // Changed from GPIO 17 to GPIO 25
#define PUMP_PIN 26

// Thresholds
#define LIGHT_THRESHOLD 600
#define WATER_THRESHOLD 200 // Adjust based on your sensor

// Web Server
WebServer server(80);

DHT dht(DHTPIN, DHTTYPE);

// Variables to store current sensor readings
float g_temperature = 0.0;
float g_humidity = 0.0;
int g_waterValue = 0;
int g_lightValue = 0;
bool g_waterLow = false;
bool g_buzzerActive = false;
bool g_pumpActive = false;
bool g_manualPumpRunning = false;
bool pumpAutoMode = true;  // Auto mode enabled by default
unsigned long pumpStartTime = 0;  // Track when pump was turned on
unsigned long manualPumpStartTime = 0;
const unsigned long PUMP_RUN_TIME = 2000;  // Pump runs for 2 seconds (2000 ms)
unsigned long lastWiFiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 5000;  // Check WiFi every 5 seconds

// Sensor smoothing variables (exponential moving average)
float smoothedTemperature = 0.0;
float smoothedHumidity = 0.0;
int smoothedWaterValue = 0;
int smoothedLightValue = 0;
const float ALPHA = 0.3;  // Smoothing factor (0.0-1.0, lower = more smoothing)

// Settings variables
int waterThreshold = 200;
int lightThreshold = 600;
int pumpThreshold = 100;  // Pump activates when water is below this level
bool buzzerAlertEnabled = true;
bool waterAlertEnabled = true;

const char* SETTINGS_FILE = "/settings.json";

// Web Server Handlers
void handleRoot() {
  if (SPIFFS.exists("/index.html")) {
    File file = SPIFFS.open("/index.html", "r");
    server.streamFile(file, "text/html");
    file.close();
  } else {
    server.send(404, "text/plain", "index.html not found");
  }
}

void saveSettings() {
  JsonDocument doc;
  doc["waterThreshold"] = waterThreshold;
  doc["lightThreshold"] = lightThreshold;
  doc["pumpThreshold"] = pumpThreshold;
  doc["buzzerAlertEnabled"] = buzzerAlertEnabled;
  doc["waterAlertEnabled"] = waterAlertEnabled;
  
  File file = SPIFFS.open(SETTINGS_FILE, "w");
  if (!file) {
    Serial.println("Failed to save settings");
    return;
  }
  
  serializeJson(doc, file);
  file.close();
  Serial.println("Settings saved to SPIFFS");
}

void loadSettings() {
  if (!SPIFFS.exists(SETTINGS_FILE)) {
    Serial.println("Settings file not found, using defaults");
    return;
  }
  
  File file = SPIFFS.open(SETTINGS_FILE, "r");
  if (!file) {
    Serial.println("Failed to open settings file");
    return;
  }
  
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    Serial.println("Failed to parse settings file");
    return;
  }
  
  waterThreshold = doc["waterThreshold"] | 200;
  lightThreshold = doc["lightThreshold"] | 600;
  pumpThreshold = doc["pumpThreshold"] | 100;
  buzzerAlertEnabled = doc["buzzerAlertEnabled"] | true;
  waterAlertEnabled = doc["waterAlertEnabled"] | true;
  
  Serial.println("Settings loaded from SPIFFS");
  Serial.print("Water Threshold: ");
  Serial.println(waterThreshold);
  Serial.print("Light Threshold: ");
  Serial.println(lightThreshold);
  Serial.print("Buzzer Alert: ");
  Serial.println(buzzerAlertEnabled ? "ON" : "OFF");
  Serial.print("Water Alert: ");
  Serial.println(waterAlertEnabled ? "ON" : "OFF");
}

void handleData() {
  JsonDocument doc;
  float temperature = isnan(g_temperature) ? 0.0f : g_temperature;
  float humidity = isnan(g_humidity) ? 0.0f : g_humidity;

  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
  doc["water"] = g_waterValue;
  doc["light"] = g_lightValue;
  doc["water_low"] = g_waterLow;
  doc["buzzer"] = g_buzzerActive;
  doc["pump"] = g_pumpActive;
  doc["pump_manual_running"] = g_manualPumpRunning;
  doc["pump_auto_mode"] = pumpAutoMode;
  doc["buzzer_alert_enabled"] = buzzerAlertEnabled;
  doc["water_alert_enabled"] = waterAlertEnabled;
  doc["water_threshold"] = waterThreshold;
  doc["light_threshold"] = lightThreshold;
  doc["pump_threshold"] = pumpThreshold;
  doc["led"] = digitalRead(LED_PIN);
  doc["ip"] = WiFi.localIP().toString();

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleLEDToggle() {
  // Removed - LED now always operates in automatic mode
  JsonDocument doc;
  doc["mode"] = "automatic";
  doc["status"] = "LED operates automatically based on light level";
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleLEDauto() {
  // Removed - LED always auto, this endpoint kept for compatibility
  JsonDocument doc;
  doc["mode"] = "automatic";
  doc["status"] = "LED automatically controlled by light sensor";
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleWaterThreshold() {
  if (server.hasArg("value")) {
    waterThreshold = server.arg("value").toInt();
    Serial.print("Water threshold updated to: ");
    Serial.println(waterThreshold);
    saveSettings();
    
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing value parameter");
  }
}

void handleLightThreshold() {
  if (server.hasArg("value")) {
    lightThreshold = server.arg("value").toInt();
    Serial.print("Light threshold updated to: ");
    Serial.println(lightThreshold);
    saveSettings();
    
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing value parameter");
  }
}

void handlePumpThreshold() {
  if (server.hasArg("value")) {
    pumpThreshold = server.arg("value").toInt();
    Serial.print("Pump threshold updated to: ");
    Serial.println(pumpThreshold);
    saveSettings();
    
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing value parameter");
  }
}

void handleBuzzerToggle() {
  buzzerAlertEnabled = !buzzerAlertEnabled;
  saveSettings();
  
  JsonDocument doc;
  doc["buzzer_alert"] = buzzerAlertEnabled;
  doc["status"] = buzzerAlertEnabled ? "Buzzer alerts enabled" : "Buzzer alerts disabled";
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleWaterAlertToggle() {
  waterAlertEnabled = !waterAlertEnabled;
  saveSettings();
  
  JsonDocument doc;
  doc["water_alert"] = waterAlertEnabled;
  doc["status"] = waterAlertEnabled ? "Water alerts enabled" : "Water alerts disabled";
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

// Pump auto mode toggle
void handlePumpAutoMode() {
  pumpAutoMode = !pumpAutoMode;
  // Turn off pump if switching to auto mode
  if (pumpAutoMode) {
    if (!g_manualPumpRunning) {
      g_pumpActive = false;
      digitalWrite(PUMP_PIN, LOW);
    }
  }

  JsonDocument doc;
  doc["pump_auto_mode"] = pumpAutoMode;
  doc["status"] = pumpAutoMode ? "Auto Mode ON" : "Auto Mode OFF";

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handlePumpPulse() {
  JsonDocument doc;

  if (!g_manualPumpRunning) {
    g_manualPumpRunning = true;
    manualPumpStartTime = millis();
    g_pumpActive = true;
    digitalWrite(PUMP_PIN, HIGH);
    Serial.println("Pump: Manual 2-second pulse started.");
    doc["status"] = "Manual pump pulse started (2s)";
  } else {
    doc["status"] = "Pump pulse already running";
  }

  doc["pump"] = true;
  doc["manual_pulse"] = true;

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not Found");
}

void checkAndReconnectWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n⚠️  WiFi disconnected. Attempting to reconnect...");
    WiFi.reconnect();
    delay(1000);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✓ WiFi reconnected!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("\n✗ WiFi reconnection failed");
    }
  }
}

int readAverage(int pin) {
  // Warmed-up ADC readings to stabilize conversion
  analogRead(pin);
  delay(2);
  analogRead(pin);
  delay(2);
  analogRead(pin);
  delay(2);

  const int NUM_SAMPLES = 30;
  int samples[NUM_SAMPLES];

  // Collect samples with spacing
  for (int i = 0; i < NUM_SAMPLES; i++) {
    samples[i] = analogRead(pin);
    delay(1);
  }

  // Sort samples for median filtering (removes outliers)
  for (int i = 0; i < NUM_SAMPLES - 1; i++) {
    for (int j = 0; j < NUM_SAMPLES - i - 1; j++) {
      if (samples[j] > samples[j + 1]) {
        int temp = samples[j];
        samples[j] = samples[j + 1];
        samples[j + 1] = temp;
      }
    }
  }

  // Use median (middle value) for final reading
  // This removes outliers better than simple average
  int medianValue = samples[NUM_SAMPLES / 2];
  
  // Double-check by averaging middle values (interquartile mean)
  long sum = 0;
  int count = NUM_SAMPLES / 2;  // Take middle 50% of values
  for (int i = NUM_SAMPLES / 4; i < NUM_SAMPLES - NUM_SAMPLES / 4; i++) {
    sum += samples[i];
  }
  
  return sum / count;
}

float readDHTTemperature(int retries = 3) {
  float temp;
  for (int i = 0; i < retries; i++) {
    temp = dht.readTemperature();
    if (!isnan(temp)) {
      return temp;
    }
    delay(500);  // Wait before retry
  }
  return NAN;  // Return NaN if all retries failed
}

float readDHTHumidity(int retries = 3) {
  float hum;
  for (int i = 0; i < retries; i++) {
    hum = dht.readHumidity();
    if (!isnan(hum)) {
      return hum;
    }
    delay(500);  // Wait before retry
  }
  return NAN;  // Return NaN if all retries failed
}

int applyExponentialSmoothing(int newValue, int& smoothedValue) {
  if (smoothedValue == 0) {
    smoothedValue = newValue;  // Initialize on first read
  } else {
    smoothedValue = (int)(ALPHA * newValue + (1.0 - ALPHA) * smoothedValue);
  }
  return smoothedValue;
}

float applyExponentialSmoothingFloat(float newValue, float& smoothedValue) {
  if (smoothedValue == 0.0) {
    smoothedValue = newValue;  // Initialize on first read
  } else {
    smoothedValue = ALPHA * newValue + (1.0 - ALPHA) * smoothedValue;
  }
  return smoothedValue;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  Serial.println("SPIFFS mounted successfully");

  // Initialize pins
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);

  analogReadResolution(12); // значення 0–4095

  dht.begin();

  // WiFi Setup - Improved
  Serial.println("\n==================== WiFi Setup ====================");
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.println("Scanning available networks...");
  
  // Scan for WiFi networks
  int numNetworks = WiFi.scanNetworks();
  Serial.print("Found ");
  Serial.print(numNetworks);
  Serial.println(" networks:");
  
  for (int i = 0; i < numNetworks; i++) {
    Serial.print("  ");
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.print(WiFi.SSID(i));
    Serial.print(" (");
    Serial.print(WiFi.RSSI(i));
    Serial.println(" dBm)");
  }
  
  Serial.println("\nAttempting to connect...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);  // Turn off WiFi and disconnect from AP
  delay(100);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  int maxAttempts = 40;  // 20 seconds (500ms per attempt)
  
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  Serial.println("");
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✓ WiFi connected successfully!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.print("✗ Failed to connect after ");
    Serial.print(maxAttempts * 500);
    Serial.println("ms");
    Serial.print("WiFi Status: ");
    Serial.println(WiFi.status());
    // Status codes: 0=IDLE, 1=NO_SSID_AVAIL, 2=SCAN_COMPLETED, 3=CONNECTED, 4=CONNECT_FAILED, 5=CONNECTION_LOST, 6=DISCONNECTED
  }
  
  Serial.println("==================================================");

  // Load settings from SPIFFS
  loadSettings();

  // Web Server Setup
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/led/toggle", handleLEDToggle);
  server.on("/led/auto", handleLEDauto);
  server.on("/settings/water", handleWaterThreshold);
  server.on("/settings/light", handleLightThreshold);
  server.on("/settings/pump", handlePumpThreshold);
  server.on("/buzzer/toggle", handleBuzzerToggle);
  server.on("/water-alert/toggle", handleWaterAlertToggle);
  server.on("/pump/auto-mode", handlePumpAutoMode);
  server.on("/pump/pulse", handlePumpPulse);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Web server started");

  Serial.println("ESP32 sensors started");
}
void loop() {
  // Handle incoming web requests
  server.handleClient();

  // Check WiFi connection periodically
  if (millis() - lastWiFiCheck > WIFI_CHECK_INTERVAL) {
    lastWiFiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      checkAndReconnectWiFi();
    }
  }

  // Read DHT sensor FIRST (most sensitive to power issues)
  float temperature = readDHTTemperature();
  float humidity = readDHTHumidity();
  
  // Give DHT time to settle - but handle web requests during this time
  for (int i = 0; i < 5; i++) {
    delay(100);
    server.handleClient();
  }

  // Then read water sensor (most problematic)
  int rawWaterValue = readAverage(WATER_PIN);
  int waterValue = rawWaterValue;
  
  // Handle requests during wait
  for (int i = 0; i < 2; i++) {
    delay(100);
    server.handleClient();
  }

  // Then read light sensor
  int lightValue = readAverage(PHOTORESISTOR_PIN);
  
  // Handle requests during wait
  for (int i = 0; i < 2; i++) {
    delay(100);
    server.handleClient();
  }

  // Apply exponential smoothing for stable readings
  if (!isnan(temperature)) {
    temperature = applyExponentialSmoothingFloat(temperature, smoothedTemperature);
  } else {
    if (smoothedTemperature > 0.0) {
      temperature = smoothedTemperature;  // Use last good value
    } else {
      temperature = 0.0;
    }
  }
  
  if (!isnan(humidity)) {
    humidity = applyExponentialSmoothingFloat(humidity, smoothedHumidity);
  } else {
    if (smoothedHumidity > 0.0) {
      humidity = smoothedHumidity;  // Use last good value
    } else {
      humidity = 0.0;
    }
  }
  
  waterValue = applyExponentialSmoothing(waterValue, smoothedWaterValue);
  lightValue = applyExponentialSmoothing(lightValue, smoothedLightValue);

  // Update global variables
  g_temperature = temperature;
  g_humidity = humidity;
  g_waterValue = waterValue;
  g_lightValue = lightValue;

  // Keep manual pulse independent from auto mode logic.
  if (g_manualPumpRunning && (millis() - manualPumpStartTime >= PUMP_RUN_TIME)) {
    g_manualPumpRunning = false;
    g_pumpActive = false;
    digitalWrite(PUMP_PIN, LOW);
    Serial.println("Pump: Manual 2 seconds completed, stopping.");
  }

  // Pump control logic - only activate if auto mode is enabled and water is below threshold
  if (pumpAutoMode && !g_manualPumpRunning) {
    if (rawWaterValue < pumpThreshold && !g_pumpActive) {
      // Start pump if not already running
      g_pumpActive = true;
      pumpStartTime = millis();
      digitalWrite(PUMP_PIN, HIGH);
      Serial.println("Pump: Starting 2-second run...");
    } else if (g_pumpActive && (millis() - pumpStartTime >= PUMP_RUN_TIME)) {
      // Stop pump after 2 seconds
      g_pumpActive = false;
      digitalWrite(PUMP_PIN, LOW);
      Serial.println("Pump: 2 seconds completed, stopping.");
    }
    
    // Also stop pump if water level is above threshold
    if (rawWaterValue >= pumpThreshold && g_pumpActive) {
      g_pumpActive = false;
      digitalWrite(PUMP_PIN, LOW);
      Serial.println("Pump: Water level restored, stopping.");
    }
  } else if (!g_manualPumpRunning) {
    // In manual mode, keep pump off
    if (g_pumpActive) {
      g_pumpActive = false;
      digitalWrite(PUMP_PIN, LOW);
      Serial.println("Pump: Manual mode, stopping.");
    }
  }

  // Check for low water level (only if water alerts are enabled)
  g_waterLow = waterAlertEnabled && (waterValue < waterThreshold);

  // Control Buzzer - beep if water is low and both water and buzzer alerts are enabled
  if (g_waterLow && waterAlertEnabled && buzzerAlertEnabled) {
    g_buzzerActive = true;
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    delay(200);
  } else {
    g_buzzerActive = false;
    digitalWrite(BUZZER_PIN, LOW);
  }

  if (isnan(temperature)) {
    Serial.println("⚠️  Error reading temperature! (using last good value)");
  } else {
    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.println("°C");
  }

  if (isnan(humidity)) {
    Serial.println("⚠️  Error reading humidity! (using last good value)");
  } else {
    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.println("%");
  }

  Serial.print("Water: ");
  Serial.print(waterValue);
  Serial.print(" adc | Light: ");
  Serial.print(lightValue);
  Serial.println(" adc");

  if (g_pumpActive) {
    Serial.println("Pump: ON (water level is 0)");
  } else {
    Serial.println("Pump: OFF");
  }

  // Control LED - automatically based on light threshold (no manual override)
  if (lightValue < lightThreshold) {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("LED: ON (dark detected - AUTO)");
  } else {
    digitalWrite(LED_PIN, LOW);
    Serial.println("LED: OFF (bright - AUTO)");
  }

  if (g_waterLow && waterAlertEnabled) {
    Serial.println("⚠️  WATER LEVEL LOW!");
  }

  Serial.println("--------------------");
  
  // Main loop idle delay - handle web requests during this time for instant response
  for (int i = 0; i < 10; i++) {
    delay(100);
    server.handleClient();
  }
}