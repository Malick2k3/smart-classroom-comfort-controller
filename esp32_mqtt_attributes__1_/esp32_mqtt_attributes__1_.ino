/*
 * Smart Classroom Comfort Controller
 * ----------------------------------
 * 2-day MVP for the IoT final project.
 *
 * What this firmware does:
 * - Reads temperature + humidity from DHT11
 * - Reads room occupancy from a PIR sensor
 * - Controls a relay-driven fan
 * - Shows live status on an OLED
 * - Uses green/yellow/red LEDs for comfort level
 * - Publishes telemetry to MQTT over Wi-Fi
 * - Accepts remote fan threshold updates over MQTT
 *
 * Required libraries (Arduino Library Manager):
 * - PubSubClient by Nick O'Leary
 * - DHT sensor library by Adafruit
 * - Adafruit GFX Library
 * - Adafruit SSD1306
 *
 * Notes:
 * - Update the Wi-Fi and MQTT credentials below before uploading.
 * - Do not commit real Wi-Fi passwords or private broker credentials.
 * - Adjust the pin mapping to match your actual wiring.
 * - Many relay modules are active LOW. Change RELAY_ACTIVE_LEVEL if needed.
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <math.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------- Wi-Fi ----------------
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ---------------- MQTT -----------------
const char* MQTT_SERVER = "YOUR_MQTT_HOST";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "YOUR_MQTT_USERNAME";
const char* MQTT_PASS = "YOUR_MQTT_PASSWORD";
const char* MQTT_CLIENT_ID = "esp32-smart-classroom-01";

const char* TOPIC_TELEMETRY = "niang-lo-hanne-ndour/classroom/comfort/telemetry";
const char* TOPIC_FAN_STATUS = "niang-lo-hanne-ndour/classroom/comfort/status/fan";
const char* TOPIC_OCCUPANCY_STATUS = "niang-lo-hanne-ndour/classroom/comfort/status/occupancy";
const char* TOPIC_COMFORT_STATUS = "niang-lo-hanne-ndour/classroom/comfort/status/comfort";
const char* TOPIC_THRESHOLD_STATUS = "niang-lo-hanne-ndour/classroom/comfort/status/threshold";
const char* TOPIC_STATE_STATUS = "niang-lo-hanne-ndour/classroom/comfort/status/state";
const char* TOPIC_PROFILE_STATUS = "niang-lo-hanne-ndour/classroom/comfort/status/system_mode";
const char* TOPIC_PROFILE_CONFIG = "niang-lo-hanne-ndour/classroom/comfort/config/system_mode";

// ---------------- Pin Map --------------

constexpr uint8_t DHT_PIN = 4;
constexpr uint8_t DHT_TYPE = DHT11;
constexpr uint8_t PIR_PIN = 27;
constexpr uint8_t RELAY_PIN = 26;
constexpr uint8_t GREEN_LED_PIN = 25;
constexpr uint8_t YELLOW_LED_PIN = 33;
constexpr uint8_t RED_LED_PIN = 32;
constexpr uint8_t POT_PIN = 34;
constexpr uint8_t OLED_SDA_PIN = 21;
constexpr uint8_t OLED_SCL_PIN = 22;


constexpr uint8_t RELAY_ACTIVE_LEVEL = LOW;
constexpr uint8_t RELAY_INACTIVE_LEVEL = HIGH;

// ---------------- Timing ----------------
constexpr unsigned long PIR_INTERVAL_MS = 150;
constexpr unsigned long DHT_INTERVAL_MS = 2000;
constexpr unsigned long POT_INTERVAL_MS = 1000;
constexpr unsigned long DISPLAY_INTERVAL_MS = 500;
constexpr unsigned long TELEMETRY_INTERVAL_MS = 3000;
constexpr unsigned long VACANCY_TIMEOUT_MS = 5UL * 60UL * 1000UL;
constexpr unsigned long WIFI_RETRY_MS = 8000;
constexpr unsigned long MQTT_RETRY_MS = 5000;
constexpr unsigned long WIFI_STATUS_REPORT_MS = 3000;
constexpr unsigned long PIR_WARMUP_MS = 30000;
constexpr unsigned long OCCUPANCY_HOLD_MS = 10000;

// ---------------- Thresholds -----------
float fanThresholdC = 26.0f;
constexpr float THRESHOLD_MIN_C = 24.0f;
constexpr float THRESHOLD_MAX_C = 32.0f;
constexpr float COMFORT_WARM_C = 27.0f;
constexpr float COMFORT_HOT_C = 30.0f;

// ---------------- OLED -----------------
constexpr int SCREEN_WIDTH = 128;
constexpr int SCREEN_HEIGHT = 64;
constexpr int OLED_RESET = -1;

enum ComfortLevel {
  COMFORT_COMFORTABLE,
  COMFORT_WARM,
  COMFORT_HOT
};

enum ClassroomState {
  STATE_IDLE,
  STATE_OCCUPIED,
  STATE_COOLING
};

enum SystemMode {
  MODE_REAL,
  MODE_DEMO_EMPTY_COOL,
  MODE_DEMO_OCCUPIED_COMFY,
  MODE_DEMO_OCCUPIED_WARM,
  MODE_DEMO_OCCUPIED_HOT
};

struct SensorSnapshot {
  float temperatureC = 0.0f;
  float humidity = 0.0f;
  bool occupied = false;
  bool fanOn = false;
  bool dhtValid = false;
  ComfortLevel comfort = COMFORT_COMFORTABLE;
  ClassroomState state = STATE_IDLE;
};

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

SensorSnapshot current;
bool displayReady = false;
SystemMode systemMode = MODE_REAL;

bool lastPublishedFan = false;
bool lastPublishedOccupied = false;
float lastPublishedThreshold = fanThresholdC;
ComfortLevel lastPublishedComfort = COMFORT_COMFORTABLE;
ClassroomState lastPublishedState = STATE_IDLE;
SystemMode lastPublishedProfile = MODE_REAL;
bool lastLoggedFan = false;
ComfortLevel lastLoggedComfort = COMFORT_COMFORTABLE;
ClassroomState lastLoggedState = STATE_IDLE;

unsigned long lastPirReadMs = 0;
unsigned long lastDhtReadMs = 0;
unsigned long lastPotReadMs = 0;
unsigned long lastDisplayRefreshMs = 0;
unsigned long lastTelemetryMs = 0;
unsigned long lastWiFiAttemptMs = 0;
unsigned long lastMQTTAttemptMs = 0;
unsigned long lastWiFiStatusReportMs = 0;
unsigned long lastOccupiedMs = 0;
unsigned long lastMotionDetectedMs = 0;
unsigned long bootMs = 0;

bool pirWarmupComplete() {
  return (millis() - bootMs) >= PIR_WARMUP_MS;
}

const char* wifiStatusToString(wl_status_t status) {
  switch (status) {
    case WL_CONNECTED:      return "CONNECTED";
    case WL_NO_SSID_AVAIL:  return "SSID_NOT_FOUND";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST:return "CONNECTION_LOST";
    case WL_DISCONNECTED:   return "DISCONNECTED";
    case WL_IDLE_STATUS:    return "IDLE";
    default:                return "UNKNOWN";
  }
}

const char* comfortToString(ComfortLevel comfort) {
  switch (comfort) {
    case COMFORT_COMFORTABLE: return "comfortable";
    case COMFORT_WARM:        return "warm";
    case COMFORT_HOT:         return "hot";
    default:                  return "unknown";
  }
}

const char* systemModeToString(SystemMode mode) {
  switch (mode) {
    case MODE_REAL:                 return "real";
    case MODE_DEMO_EMPTY_COOL:      return "demo_empty_cool";
    case MODE_DEMO_OCCUPIED_COMFY:  return "demo_occupied_comfy";
    case MODE_DEMO_OCCUPIED_WARM:   return "demo_occupied_warm";
    case MODE_DEMO_OCCUPIED_HOT:    return "demo_occupied_hot";
    default:                  return "real";
  }
}

const char* stateToString(ClassroomState state) {
  switch (state) {
    case STATE_IDLE:     return "IDLE";
    case STATE_OCCUPIED: return "OCCUPIED";
    case STATE_COOLING:  return "COOLING";
    default:             return "UNKNOWN";
  }
}

uint8_t relayLevelFor(bool fanOn) {
  return fanOn ? RELAY_ACTIVE_LEVEL : RELAY_INACTIVE_LEVEL;
}

float mapPotToThreshold(int rawValue) {
  const float ratio = constrain(static_cast<float>(rawValue) / 4095.0f, 0.0f, 1.0f);
  const float mapped = THRESHOLD_MIN_C + ratio * (THRESHOLD_MAX_C - THRESHOLD_MIN_C);
  return roundf(mapped * 10.0f) / 10.0f;
}

void setComfortLeds(ComfortLevel comfort) {
  digitalWrite(GREEN_LED_PIN, comfort == COMFORT_COMFORTABLE ? HIGH : LOW);
  digitalWrite(YELLOW_LED_PIN, comfort == COMFORT_WARM ? HIGH : LOW);
  digitalWrite(RED_LED_PIN, comfort == COMFORT_HOT ? HIGH : LOW);
}

void refreshDisplay() {
  if (!displayReady) {
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.printf("Temp: %.1f C\n", current.temperatureC);
  display.printf("Hum : %.0f %%\n", current.humidity);
  display.printf("Occ : %s\n", current.occupied ? "YES" : "NO");
  display.printf("Fan : %s\n", current.fanOn ? "ON" : "OFF");
  display.printf("Mode: %s\n", stateToString(current.state));
  if (!pirWarmupComplete()) {
    display.printf("PIR warmup...");
  } else {
    display.printf("Thr : %.1f P", fanThresholdC);
  }
  display.display();
}

void publishRetained(const char* topic, const char* payload) {
  bool ok = mqtt.publish(topic, payload, true);
  Serial.printf("[MQTT] %s %s = %s\n", ok ? "PUB OK " : "PUB FAIL", topic, payload);
}

void publishRetainedBool(const char* topic, bool value) {
  publishRetained(topic, value ? "true" : "false");
}

void publishRetainedFloat(const char* topic, float value, uint8_t decimals = 1) {
  char payload[16];
  dtostrf(value, 0, decimals, payload);
  publishRetained(topic, payload);
}

void publishStatusIfChanged() {
  if (!mqtt.connected()) {
    return;
  }

  if (current.fanOn != lastPublishedFan) {
    publishRetainedBool(TOPIC_FAN_STATUS, current.fanOn);
    lastPublishedFan = current.fanOn;
  }

  if (current.occupied != lastPublishedOccupied) {
    publishRetainedBool(TOPIC_OCCUPANCY_STATUS, current.occupied);
    lastPublishedOccupied = current.occupied;
  }

  if (current.comfort != lastPublishedComfort) {
    publishRetained(TOPIC_COMFORT_STATUS, comfortToString(current.comfort));
    lastPublishedComfort = current.comfort;
  }

  if (current.state != lastPublishedState) {
    publishRetained(TOPIC_STATE_STATUS, stateToString(current.state));
    lastPublishedState = current.state;
  }

  if (systemMode != lastPublishedProfile) {
    publishRetained(TOPIC_PROFILE_STATUS, systemModeToString(systemMode));
    lastPublishedProfile = systemMode;
  }

  if (fabs(fanThresholdC - lastPublishedThreshold) > 0.05f) {
    publishRetainedFloat(TOPIC_THRESHOLD_STATUS, fanThresholdC);
    lastPublishedThreshold = fanThresholdC;
  }
}

void publishTelemetry() {
  if (!mqtt.connected()) {
    return;
  }

  char payload[256];
  snprintf(
    payload,
    sizeof(payload),
    "{\"temperature\":%.2f,\"humidity\":%.2f,\"occupied\":%s,\"fan_on\":%s,"
    "\"comfort\":\"%s\",\"state\":\"%s\",\"threshold\":%.2f,\"system_mode\":\"%s\"}",
    current.temperatureC,
    current.humidity,
    current.occupied ? "true" : "false",
    current.fanOn ? "true" : "false",
    comfortToString(current.comfort),
    stateToString(current.state),
    fanThresholdC,
    systemModeToString(systemMode)
  );

  bool ok = mqtt.publish(TOPIC_TELEMETRY, payload);
  Serial.printf("[MQTT] %s %s\n", ok ? "PUB OK " : "PUB FAIL", payload);
  publishStatusIfChanged();
}

void handleSystemModeUpdate(const byte* payload, unsigned int length) {
  char buffer[32];
  const unsigned int copyLength = min(length, static_cast<unsigned int>(sizeof(buffer) - 1));
  memcpy(buffer, payload, copyLength);
  buffer[copyLength] = '\0';

  if (strcmp(buffer, "real") == 0) {
    systemMode = MODE_REAL;
  } else if (strcmp(buffer, "demo_empty_cool") == 0) {
    systemMode = MODE_DEMO_EMPTY_COOL;
  } else if (strcmp(buffer, "demo_occupied_comfy") == 0) {
    systemMode = MODE_DEMO_OCCUPIED_COMFY;
  } else if (strcmp(buffer, "demo_occupied_warm") == 0) {
    systemMode = MODE_DEMO_OCCUPIED_WARM;
  } else if (strcmp(buffer, "demo_occupied_hot") == 0) {
    systemMode = MODE_DEMO_OCCUPIED_HOT;
  } else {
    Serial.printf("[CFG] Ignored invalid system mode payload: %s\n", buffer);
    return;
  }

  Serial.printf("[CFG] System mode updated to %s\n", systemModeToString(systemMode));
  applyOutputs();
  publishStatusIfChanged();
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("[MQTT] RX topic=%s length=%u\n", topic, length);

  if (strcmp(topic, TOPIC_PROFILE_CONFIG) == 0) {
    handleSystemModeUpdate(payload, length);
  }
}

void connectWiFiIfNeeded() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastWiFiStatusReportMs >= WIFI_STATUS_REPORT_MS) {
    lastWiFiStatusReportMs = now;
    Serial.printf("[WiFi] Status: %s\n", wifiStatusToString(WiFi.status()));
  }

  if (now - lastWiFiAttemptMs < WIFI_RETRY_MS) {
    return;
  }

  lastWiFiAttemptMs = now;
  Serial.printf("[WiFi] Connecting to %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectMQTTIfNeeded() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastMQTTAttemptMs < MQTT_RETRY_MS) {
    return;
  }

  lastMQTTAttemptMs = now;
  Serial.printf("[MQTT] Connecting to %s:%u ... ", MQTT_SERVER, MQTT_PORT);

  const bool hasMqttAuth = strlen(MQTT_USER) > 0 && strcmp(MQTT_USER, "YOUR_MQTT_USERNAME") != 0;
  const bool connected = hasMqttAuth
    ? mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)
    : mqtt.connect(MQTT_CLIENT_ID);

  if (connected) {
    Serial.println("connected");
    mqtt.subscribe(TOPIC_PROFILE_CONFIG);
    publishRetainedFloat(TOPIC_THRESHOLD_STATUS, fanThresholdC);
    publishRetained(TOPIC_STATE_STATUS, stateToString(current.state));
    publishRetained(TOPIC_COMFORT_STATUS, comfortToString(current.comfort));
    publishRetained(TOPIC_PROFILE_STATUS, systemModeToString(systemMode));
    publishRetainedBool(TOPIC_OCCUPANCY_STATUS, current.occupied);
    publishRetainedBool(TOPIC_FAN_STATUS, current.fanOn);
  } else {
    Serial.printf("failed rc=%d\n", mqtt.state());
  }
}

void applyDemoScenarioValues() {
  switch (systemMode) {
    case MODE_DEMO_EMPTY_COOL:
      current.temperatureC = 24.0f;
      current.humidity = 50.0f;
      current.occupied = false;
      break;
    case MODE_DEMO_OCCUPIED_COMFY:
      current.temperatureC = max(20.0f, min(fanThresholdC - 1.0f, COMFORT_WARM_C - 0.5f));
      current.humidity = 55.0f;
      current.occupied = true;
      break;
    case MODE_DEMO_OCCUPIED_WARM:
      current.temperatureC = (COMFORT_WARM_C + COMFORT_HOT_C) / 2.0f;
      current.humidity = 60.0f;
      current.occupied = true;
      break;
    case MODE_DEMO_OCCUPIED_HOT:
      current.temperatureC = max(fanThresholdC + 2.0f, COMFORT_HOT_C + 1.0f);
      current.humidity = 65.0f;
      current.occupied = true;
      break;
    case MODE_REAL:
    default:
      return;
  }

  current.dhtValid = true;
}

void readPotentiometer() {
  const int rawValue = analogRead(POT_PIN);
  const float newThreshold = mapPotToThreshold(rawValue);

  if (fabs(newThreshold - fanThresholdC) >= 0.1f) {
    fanThresholdC = newThreshold;
    Serial.printf("[POT] Threshold set to %.1f C (raw=%d)\n", fanThresholdC, rawValue);
    applyOutputs();
    publishStatusIfChanged();
  }
}

void updateComfortLevel() {
  if (current.temperatureC >= COMFORT_HOT_C) {
    current.comfort = COMFORT_HOT;
  } else if (current.temperatureC >= COMFORT_WARM_C) {
    current.comfort = COMFORT_WARM;
  } else {
    current.comfort = COMFORT_COMFORTABLE;
  }
}

void evaluateStateMachine() {
  const unsigned long now = millis();

  if (current.occupied) {
    lastOccupiedMs = now;
    current.state = current.temperatureC >= fanThresholdC ? STATE_COOLING : STATE_OCCUPIED;
    current.fanOn = current.temperatureC >= fanThresholdC;
    return;
  }

  const bool withinVacancyWindow = (now - lastOccupiedMs) < VACANCY_TIMEOUT_MS;
  if (withinVacancyWindow && current.temperatureC >= fanThresholdC) {
    current.state = STATE_COOLING;
    current.fanOn = true;
  } else {
    current.state = STATE_IDLE;
    current.fanOn = false;
  }
}

void applyOutputs() {
  updateComfortLevel();
  evaluateStateMachine();
  digitalWrite(RELAY_PIN, relayLevelFor(current.fanOn));
  setComfortLeds(current.comfort);

  if (current.comfort != lastLoggedComfort || current.fanOn != lastLoggedFan || current.state != lastLoggedState) {
    Serial.printf(
      "[STATE] mode=%s comfort=%s fan=%s occupied=%s temp=%.1f threshold=%.1f\n",
      stateToString(current.state),
      comfortToString(current.comfort),
      current.fanOn ? "ON" : "OFF",
      current.occupied ? "YES" : "NO",
      current.temperatureC,
      fanThresholdC
    );
    lastLoggedComfort = current.comfort;
    lastLoggedFan = current.fanOn;
    lastLoggedState = current.state;
  }
}

void readPirSensor() {
  const unsigned long now = millis();
  const bool previous = current.occupied;

  if (systemMode != MODE_REAL) {
    applyDemoScenarioValues();

    if (current.occupied != previous) {
      Serial.printf("[PIR] Occupancy changed: %s\n", current.occupied ? "YES" : "NO");
    }

    applyOutputs();
    return;
  }

  if (!pirWarmupComplete()) {
    current.occupied = false;
    lastMotionDetectedMs = 0;

    if (current.occupied != previous) {
      Serial.println("[PIR] Occupancy changed: NO");
    }

    applyOutputs();
    return;
  }

  const bool motionDetected = digitalRead(PIR_PIN) == HIGH;

  if (motionDetected) {
    lastMotionDetectedMs = now;
  }

  current.occupied = (now - lastMotionDetectedMs) < OCCUPANCY_HOLD_MS;

  if (current.occupied != previous) {
    Serial.printf("[PIR] Occupancy changed: %s\n", current.occupied ? "YES" : "NO");
  }

  applyOutputs();
}

void readDhtSensor() {
  if (systemMode != MODE_REAL) {
    applyDemoScenarioValues();
    applyOutputs();
    return;
  }

  const float temperature = dht.readTemperature();
  const float humidity = dht.readHumidity();

  if (!isnan(temperature) && !isnan(humidity)) {
    current.temperatureC = temperature;
    current.humidity = humidity;
    current.dhtValid = true;
    Serial.printf("[DHT] Temp=%.1f C  Hum=%.1f %%\n", current.temperatureC, current.humidity);
  } else {
    current.dhtValid = false;
    Serial.println("[DHT] Failed to read from DHT11, keeping last valid values.");
  }

  applyOutputs();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Smart Classroom Comfort Controller ===");

  pinMode(PIR_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(POT_PIN, INPUT);

  digitalWrite(RELAY_PIN, RELAY_INACTIVE_LEVEL);
  setComfortLeds(COMFORT_COMFORTABLE);

  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  displayReady = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!displayReady) {
    Serial.println("[OLED] SSD1306 allocation failed");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Booting...");
    display.display();
  }

  dht.begin();

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setBufferSize(512);
  mqtt.setCallback(mqttCallback);

  bootMs = millis();
  lastOccupiedMs = millis();
  connectWiFiIfNeeded();
}

void loop() {
  connectWiFiIfNeeded();
  connectMQTTIfNeeded();

  if (mqtt.connected()) {
    mqtt.loop();
  }

  const unsigned long now = millis();

  if (now - lastPirReadMs >= PIR_INTERVAL_MS) {
    lastPirReadMs = now;
    readPirSensor();
    publishStatusIfChanged();
  }

  if (now - lastDhtReadMs >= DHT_INTERVAL_MS) {
    lastDhtReadMs = now;
    readDhtSensor();
    publishStatusIfChanged();
  }

  if (now - lastPotReadMs >= POT_INTERVAL_MS) {
    lastPotReadMs = now;
    readPotentiometer();
  }

  if (now - lastDisplayRefreshMs >= DISPLAY_INTERVAL_MS) {
    lastDisplayRefreshMs = now;
    refreshDisplay();
  }

  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;
    publishTelemetry();
  }
}
