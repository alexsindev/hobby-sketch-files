#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_Sensor.h>

// ── WiFi & MQTT config ────────────────────────────
const char* WIFI_SSID     = "No";
const char* WIFI_PASS     = "56781234";
// const char* WIFI_SSID     = "AIT-NON-CSIM";
// const char* WIFI_PASS     = "abcdefgh";
const char* MQTT_HOST     = "54.79.35.154";
const int   MQTT_PORT     = 1883;
const char* MQTT_USER     = "esp32_client";
const char* MQTT_PASS     = "mlops";
const char* TOPIC_CLIMATE = "sensors/esp32/climate";
const char* TOPIC_AIR     = "sensors/esp32/airquality";
const int   INTERVAL      = 1000*1; // 1 seconds in ms

// ── BME280 ────────────────────────────────────────
Adafruit_BME280 bme;

// ── PMS7003 ───────────────────────────────────────
#define PMS_RX 16
#define PMS_TX 17
HardwareSerial pmsSerial(2); // UART2

struct PMS7003Data {
  uint16_t pm1_0;
  uint16_t pm2_5;
  uint16_t pm10;
  bool valid;
};

// ── MQTT & WiFi clients ───────────────────────────
WiFiClient espClient;
PubSubClient mqtt(espClient);

// ── Connect WiFi ──────────────────────────────────
void connectWiFi() {
  Serial.print("Connecting to WiFi");
  Serial.print(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
}

// ── Connect MQTT ──────────────────────────────────
void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Connecting to MQTT...");
    if (mqtt.connect("esp32-wroom", MQTT_USER, MQTT_PASS)) {
      Serial.println("connected");
    } else {
      Serial.printf("failed, rc=%d. Retrying in 5s\n", mqtt.state());
      delay(5000);
    }
  }
}

// ── Read PMS7003 ──────────────────────────────────
PMS7003Data readPMS7003() {
    PMS7003Data data = {0, 0, 0, false};
    PMS7003Data last = {0, 0, 0, false};

    unsigned long start = millis();
    while (millis() - start < 500) {  // try for 500ms
        if (pmsSerial.available() < 32) {
            delay(10);
            continue;
        }
        if (pmsSerial.peek() != 0x42) {
            pmsSerial.read();
            continue;
        }
        uint8_t buf[32];
        pmsSerial.readBytes(buf, 32);
        if (buf[0] == 0x42 && buf[1] == 0x4D) {
            last.pm1_0 = (buf[4] << 8) | buf[5];
            last.pm2_5 = (buf[6] << 8) | buf[7];
            last.pm10  = (buf[8] << 8) | buf[9];
            last.valid = true;
        }
    }
    return last;
}

// ── Setup ─────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // BME280 init
  Wire.begin(21, 22);
  if (!bme.begin(0x76)) {
    Serial.println("BME280 not found! Check wiring.");
    while (1);
  }
  Serial.println("BME280 OK");

  // PMS7003 init
  pmsSerial.begin(9600, SERIAL_8N1, PMS_RX, PMS_TX);
  delay(3000); // give PMS7003 time to start up
  Serial.println("PMS7003 OK");

  // WiFi + MQTT
  connectWiFi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  connectMQTT();
}

// ── Loop ──────────────────────────────────────────
void loop() {
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  // Read BME280
  float temp     = bme.readTemperature();
  float humidity = bme.readHumidity();
  float pressure = bme.readPressure() / 100.0F;

  // Read PMS7003
  PMS7003Data pms = readPMS7003();

  // Publish climate
  char climatePayload[120];
  snprintf(climatePayload, sizeof(climatePayload),
    "{\"temperature_c\":%.2f,\"humidity_pct\":%.2f,\"pressure_hpa\":%.2f}",
    temp, humidity, pressure);
  mqtt.publish(TOPIC_CLIMATE, climatePayload);
  Serial.println("Climate: " + String(climatePayload));

  // Publish air quality
  if (pms.valid) {
    char airPayload[80];
    snprintf(airPayload, sizeof(airPayload),
      "{\"pm1_0\":%d,\"pm2_5\":%d,\"pm10\":%d}",
      pms.pm1_0, pms.pm2_5, pms.pm10);
    mqtt.publish(TOPIC_AIR, airPayload);
    Serial.println("Air: " + String(airPayload));
  } else {
    Serial.println("PMS7003: no valid frame");
  }

  delay(INTERVAL);
}
