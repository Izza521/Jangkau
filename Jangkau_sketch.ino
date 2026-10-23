/*
  Jangkau smart step path - Zone P1 edge controller (Wokwi simulation)
  Sime Darby Property University Hackathon 2026 - People track

  What this rig shows:
   - Smart step tiles: 3 pavement tiles with load-cell (weighing-scale) sensors underneath.
     A step is detected by pressure only, so people are counted, never identified.
   - Energy harvesting: each step harvests a small amount of energy (estimate) that powers
     the tile's own sensor and the small LED glow under the walker's feet.
   - Lights follow the walker: at night the park lamps brighten ahead of whichever tile
     was stepped on, then dim after the walker has passed.
   - Quiet edge: the last 3 lamps (forest edge) stay low and amber for wildlife.
   - Quest glow: the green button (or a message from the app) makes the path glow gold.
   - Flash-flood safety: pond level (knob) above 1.00 m sounds the alarm and pauses quests.
   - Weather: temperature, humidity and "feels like" temperature (DHT22) for the dashboard.
   - Energy meter: compares real lamp energy with full brightness all night.
   - Edge-first: lighting and alarm logic runs here, so it keeps working if WiFi or MQTT drops.
   - Live data goes to the dashboard over MQTT.

  How to use while the simulation runs:
   - Click a load cell (Tile A, B or C), drag the weight above 20 kg (someone steps on it),
     then back to 0 (they step off). Try A, then B, then C to "walk" down the path.
   - Slide switch = day / night.
   - Green button = a neighbour quest is completed.
   - Knob = pond water level.
   - Click the DHT22 to change temperature and humidity.

  The same code runs on a real ESP32 with HX711 load-cell boards and a DHT22.
*/

#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <HX711.h>
#include <DHTesp.h>

// ---------- Team settings ----------
// CHANGE TEAM_ID to something unique (e.g. "jk-unimas-nullpointers") and type the same ID on the dashboard.
const char* TEAM_ID   = "elmina-demo";
const char* WIFI_SSID = "Wokwi-GUEST";   // Wokwi's built-in internet access
const char* WIFI_PASS = "";
const char* MQTT_HOST = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;

// ---------- Pins ----------
const int PIN_PIXELS = 13;
const int PIN_BUTTON = 14;
const int PIN_NIGHT  = 27;
const int PIN_WATER  = 34;
const int PIN_BUZZER = 18;
const int PIN_ALERT  = 5;
const int PIN_DHT    = 15;
const int NUM_TILES  = 3;
const int TILE_DT[NUM_TILES]   = {32, 25, 16};
const int TILE_SCK[NUM_TILES]  = {33, 26, 17};
const int TILE_LED[NUM_TILES]  = {19, 23, 4};    // small LED glow built into each tile
const int TILE_LAMP[NUM_TILES] = {0, 4, 8};      // which lamp each tile sits next to
const char* TILE_NAME[NUM_TILES] = {"A", "B", "C"};

// ---------- Path model ----------
const int   NUM_LAMPS     = 12;
const int   FOREST_FROM   = 9;       // lamps 9-11 are the forest edge ("quiet edge")
const float LAMP_WATTS    = 40.0;    // one park lamp
const float EVENING_LEVEL = 0.20;    // calm evening level
const float QUIET_MAX     = 0.35;    // highest level allowed at the forest edge
const float TIME_SCALE    = 60.0;    // demo only: 1 real second = 1 minute of park time
const float STEP_KG_ON    = 20.0;    // more than this on a tile = someone is standing on it
const float STEP_KG_OFF   = 10.0;    // less than this = they stepped off
const float HARVEST_J     = 3.0;     // estimated energy harvested per step (a few joules)
const float SCALE_PER_KG  = 420.0;   // Wokwi 50 kg load cell: raw 0-21000 = 0-50 kg
const float FLOOD_ON_M    = 1.00;    // pond level that starts a flood alert
const float FLOOD_OFF_M   = 0.95;    // level that ends it (a small gap stops flicker)

// "Lights follow the walker" timing
const unsigned long STEP_MS = 180;   // delay between one lamp and the next
const unsigned long RISE_MS = 300;   // fade up
const unsigned long HOLD_MS = 6000;  // stay bright
const unsigned long FALL_MS = 900;   // fade back down
const unsigned long QUEST_GLOW_MS = 4000;
const unsigned long TILE_GLOW_MS  = 1500;

Adafruit_NeoPixel strip(NUM_LAMPS, PIN_PIXELS, NEO_GRB + NEO_KHZ800);
LiquidCrystal_I2C lcd(0x27, 16, 2);
HX711 tiles[NUM_TILES];
DHTesp dht;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

String topicStatus, topicEvents, topicCmd, topicOnline;

// ---------- State ----------
unsigned long now = 0;
bool night = false;
bool flood = false;
bool buzzing = false;
unsigned long steps = 0;
unsigned long quests = 0;
unsigned long tileSteps[NUM_TILES] = {0, 0, 0};
bool tilePressed[NUM_TILES] = {false, false, false};
unsigned long tileGlowUntil[NUM_TILES] = {0, 0, 0};
unsigned long lampRise[NUM_LAMPS];   // when each lamp starts brightening (0 = idle)
unsigned long lampEnd[NUM_LAMPS];    // when each lamp starts dimming again
unsigned long questUntil = 0;
float levels[NUM_LAMPS];
float waterM = 0;
float tempC = 0, humidity = 0, feelsC = 0;
float harvestJ = 0;                  // energy harvested by the tiles
float energyWh = 0;                  // real lamp energy (park time)
float baselineWh = 0;                // same lamps at full brightness
float powerW = 0;
unsigned long stepTimes[30];         // recent step times, for steps per minute
int stepIdx = 0;

bool btnLast = HIGH;
unsigned long btnChanged = 0;
bool wifiWasUp = false;

unsigned long tSensor = 0, tLights = 0, tLcd = 0, tPublish = 0, tWifi = 0, tMqtt = 0, tDht = 0;

// ---------- MQTT helpers ----------
void publishEvent(const char* type, const char* detail) {
  if (!mqtt.connected()) return;
  char buf[200];
  snprintf(buf, sizeof(buf),
           "{\"type\":\"%s\",\"detail\":\"%s\",\"steps\":%lu,\"quests\":%lu,\"water\":%.2f}",
           type, detail, steps, quests, waterM);
  mqtt.publish(topicEvents.c_str(), buf);
}

// 0..1 brightness boost for one lamp from the "follow the walker" wave
float lampBoost(int i) {
  if (lampRise[i] == 0 || now < lampRise[i]) return 0;
  unsigned long t = now - lampRise[i];
  if (t < RISE_MS) return t / (float)RISE_MS;
  if (now < lampEnd[i]) return 1.0;
  unsigned long f = now - lampEnd[i];
  if (f < FALL_MS) return 1.0 - f / (float)FALL_MS;
  return 0;
}

bool anyAhead() {
  for (int i = 0; i < NUM_LAMPS; i++) if (lampBoost(i) > 0) return true;
  return false;
}

const char* modeName() {
  if (flood) return "FLOOD";
  if (now < questUntil) return "QUEST";
  if (night && anyAhead()) return "AHEAD";
  if (night) return "NIGHT";
  return "DAY";
}

int stepsPerMinute() {
  int n = 0;
  for (int i = 0; i < 30; i++) {
    if (stepTimes[i] != 0 && now - stepTimes[i] < 60000UL) n++;
  }
  return n;
}

// ---------- Actions ----------
void startQuest(const char* source) {
  if (flood) {
    publishEvent("quest_paused", source);   // no quests near water during a flood alert
    return;
  }
  quests++;
  questUntil = now + QUEST_GLOW_MS;
  publishEvent("quest", source);
}

void lightAheadFrom(int lamp) {
  int from = lamp > 0 ? lamp - 1 : 0;            // also keep the lamp just behind lit
  for (int i = from; i < NUM_LAMPS; i++) {
    unsigned long rise = now + (unsigned long)max(0, i - lamp) * STEP_MS;
    unsigned long end = rise + RISE_MS + HOLD_MS;
    bool lit = lampRise[i] != 0 && now >= lampRise[i] + RISE_MS && now < lampEnd[i];
    if (lit) {
      if (end > lampEnd[i]) lampEnd[i] = end;     // already bright: just stay on longer
    } else {
      lampRise[i] = rise;
      lampEnd[i] = end;
    }
  }
}

void onStep(int tile) {
  steps++;
  tileSteps[tile]++;
  stepTimes[stepIdx] = now;
  stepIdx = (stepIdx + 1) % 30;
  harvestJ += HARVEST_J;                          // the tile harvests a little energy...
  tileGlowUntil[tile] = now + TILE_GLOW_MS;       // ...and uses it for its own LED glow
  if (night) lightAheadFrom(TILE_LAMP[tile]);
  publishEvent("step", TILE_NAME[tile]);
}

// ---------- Inputs ----------
void updateSensors() {
  for (int t = 0; t < NUM_TILES; t++) {
    if (!tiles[t].is_ready()) continue;
    float kg = tiles[t].get_units(1);
    if (!tilePressed[t] && kg > STEP_KG_ON) {
      tilePressed[t] = true;
      onStep(t);
    } else if (tilePressed[t] && kg < STEP_KG_OFF) {
      tilePressed[t] = false;
    }
  }

  bool nightNow = digitalRead(PIN_NIGHT) == HIGH;
  if (nightNow != night) {
    night = nightNow;
    publishEvent(night ? "night" : "day", "switch");
  }

  int raw = analogRead(PIN_WATER);
  float m = raw / 4095.0 * 1.5;              // knob range = 0.00 to 1.50 m
  waterM = waterM * 0.7 + m * 0.3;           // smooth the reading
  if (!flood && waterM > FLOOD_ON_M) {
    flood = true;
    publishEvent("flood_on", "pond");
  } else if (flood && waterM < FLOOD_OFF_M) {
    flood = false;
    publishEvent("flood_off", "pond");
  }
}

void updateWeather() {
  TempAndHumidity d = dht.getTempAndHumidity();
  if (dht.getStatus() != 0) return;          // keep the last good reading
  tempC = d.temperature;
  humidity = d.humidity;
  feelsC = dht.computeHeatIndex(tempC, humidity, false);
}

void updateButton() {
  bool b = digitalRead(PIN_BUTTON);
  if (b != btnLast && now - btnChanged > 50) {
    btnChanged = now;
    btnLast = b;
    if (b == LOW) startQuest("button");
  }
}

// ---------- Lights ----------
void updateLights(unsigned long dtMs) {
  float base = night ? EVENING_LEVEL : 0.0;
  bool questGlow = now < questUntil;
  float sum = 0;

  for (int i = 0; i < NUM_LAMPS; i++) {
    float lvl = base;
    if (night) lvl = max(lvl, base + (1.0f - base) * lampBoost(i));
    if (questGlow) {
      float pulse = 0.55 + 0.35 * sin((now % 1000) / 1000.0 * 2 * PI);
      lvl = max(lvl, pulse);
    }
    if (flood && night) lvl = 1.0;                     // keep the path fully lit for safety
    if (i >= FOREST_FROM) lvl = min(lvl, QUIET_MAX);   // quiet edge for wildlife
    levels[i] = lvl;
    sum += lvl;
  }

  // Energy: real lamp use vs the same lamps at full brightness (only at night)
  powerW = sum * LAMP_WATTS;
  float hours = dtMs / 3600000.0 * TIME_SCALE;
  energyWh += powerW * hours;
  if (night) baselineWh += NUM_LAMPS * LAMP_WATTS * hours;

  bool blink = (now / 400) % 2 == 0;
  for (int i = 0; i < NUM_LAMPS; i++) {
    int r = 255, g = 170, b = 80;                        // warm white
    if (i >= FOREST_FROM) { r = 255; g = 110; b = 30; }  // amber at the forest edge
    if (questGlow) { r = 255; g = 150; b = 0; }          // gold quest glow
    float s = levels[i];
    if (flood && i < 2) { r = 255; g = 40; b = 0; s = blink ? 1.0 : 0.05; }  // flood marker
    strip.setPixelColor(i, strip.Color((uint8_t)(r * s), (uint8_t)(g * s), (uint8_t)(b * s)));
  }
  strip.show();

  for (int t = 0; t < NUM_TILES; t++) {
    digitalWrite(TILE_LED[t], (now < tileGlowUntil[t] || tilePressed[t]) ? HIGH : LOW);
  }
}

float savedPct() {
  if (baselineWh <= 0) return 0;
  float p = (1.0 - energyWh / baselineWh) * 100.0;
  return p < 0 ? 0 : p;
}

// ---------- Alarm ----------
void updateAlarm() {
  if (!flood) {
    digitalWrite(PIN_ALERT, LOW);
    if (buzzing) { noTone(PIN_BUZZER); buzzing = false; }
    return;
  }
  bool phase = (now / 400) % 2 == 0;
  digitalWrite(PIN_ALERT, phase ? HIGH : LOW);
  if (phase && !buzzing) { tone(PIN_BUZZER, 1200); buzzing = true; }
  else if (!phase && buzzing) { noTone(PIN_BUZZER); buzzing = false; }
}

// ---------- LCD ----------
void updateLcd() {
  char l1[17], l2[17];
  snprintf(l1, sizeof(l1), "%-6s stp:%5lu", modeName(), steps);
  char net = mqtt.connected() ? '*' : '!';   // * = sending to dashboard, ! = offline (still working)
  if ((now / 3000) % 2 == 0) {
    snprintf(l2, sizeof(l2), "S%3.0f%% Q%-2lu %4.0fJ%c", savedPct(), quests, harvestJ, net);
  } else {
    snprintf(l2, sizeof(l2), "%4.1fC%3.0f%% %4.2fm%c", tempC, humidity, waterM, net);
  }
  lcd.setCursor(0, 0);
  lcd.print(l1);
  lcd.setCursor(0, 1);
  lcd.print(l2);
  for (int i = strlen(l2); i < 16; i++) lcd.print(" ");
}

// ---------- Network (never blocks the lights for long) ----------
void onMqtt(char* topic, byte* payload, unsigned int len) {
  (void)topic;
  String msg;
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  if (msg.indexOf("quest") >= 0) startQuest("app");   // the web app says a neighbour quest finished
}

void networkLoop() {
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiWasUp) { wifiWasUp = false; Serial.println("WiFi lost. Lights keep working offline."); }
    if (now - tWifi > 15000) {
      tWifi = now;
      Serial.println("WiFi: connecting to Wokwi-GUEST...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
    }
    return;
  }
  if (!wifiWasUp) {
    wifiWasUp = true;
    Serial.print("WiFi connected, IP ");
    Serial.println(WiFi.localIP());
  }
  if (!mqtt.connected()) {
    if (now - tMqtt > 5000) {
      tMqtt = now;
      Serial.print("MQTT: connecting to ");
      Serial.print(MQTT_HOST);
      Serial.print(" as team ");
      Serial.println(TEAM_ID);
      String id = "jk-rig-" + String((uint32_t)random(0xFFFFFF), HEX);
      if (mqtt.connect(id.c_str(), topicOnline.c_str(), 0, true, "false")) {
        mqtt.publish(topicOnline.c_str(), "true", true);
        mqtt.subscribe(topicCmd.c_str());
        Serial.print("MQTT connected. Sending to topic ");
        Serial.println(topicStatus);
      } else {
        Serial.print("MQTT failed, code ");
        Serial.print(mqtt.state());
        Serial.println(". Retrying in 5 s.");
      }
    }
    return;
  }
  mqtt.loop();
}

void publishStatus() {
  if (!mqtt.connected()) return;
  String lv;
  for (int i = 0; i < NUM_LAMPS; i++) {
    if (i) lv += ",";
    lv += String((int)(levels[i] * 100 + 0.5));
  }
  char buf[640];
  snprintf(buf, sizeof(buf),
           "{\"zone\":\"P1\",\"mode\":\"%s\",\"night\":%s,\"steps\":%lu,\"perMin\":%d,"
           "\"tiles\":[%lu,%lu,%lu],\"pressed\":[%d,%d,%d],\"quests\":%lu,\"powerW\":%.0f,"
           "\"fullW\":%.0f,\"energyWh\":%.1f,\"baselineWh\":%.1f,\"savedPct\":%.0f,"
           "\"harvestJ\":%.0f,\"water\":%.2f,\"flood\":%s,\"tempC\":%.1f,\"humidity\":%.0f,"
           "\"feelsC\":%.1f,\"levels\":[%s],\"uptime\":%lu}",
           modeName(), night ? "true" : "false", steps, stepsPerMinute(),
           tileSteps[0], tileSteps[1], tileSteps[2],
           tilePressed[0] ? 1 : 0, tilePressed[1] ? 1 : 0, tilePressed[2] ? 1 : 0,
           quests, powerW, night ? NUM_LAMPS * LAMP_WATTS : 0.0, energyWh, baselineWh,
           savedPct(), harvestJ, waterM, flood ? "true" : "false", tempC, humidity, feelsC,
           lv.c_str(), now / 1000);
  mqtt.publish(topicStatus.c_str(), buf);
}

// ---------- Setup and loop ----------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_NIGHT, INPUT);
  pinMode(PIN_ALERT, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  for (int t = 0; t < NUM_TILES; t++) {
    pinMode(TILE_LED[t], OUTPUT);
    tiles[t].begin(TILE_DT[t], TILE_SCK[t]);
    tiles[t].set_scale(SCALE_PER_KG);
  }
  for (int i = 0; i < NUM_LAMPS; i++) { lampRise[i] = 0; lampEnd[i] = 0; levels[i] = 0; }
  dht.setup(PIN_DHT, DHTesp::DHT22);

  strip.begin();
  strip.show();
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Jangkau");
  lcd.setCursor(0, 1);
  lcd.print("Step path start");

  for (int t = 0; t < NUM_TILES; t++) tiles[t].tare();   // empty tiles = 0 kg

  String base = String("jangkau/") + TEAM_ID + "/zone/P1";
  topicStatus = base + "/status";
  topicEvents = base + "/events";
  topicCmd    = base + "/cmd";
  topicOnline = base + "/online";

  randomSeed(micros());
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
  tWifi = millis();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqtt);
  mqtt.setBufferSize(768);
  mqtt.setSocketTimeout(3);
  mqtt.setKeepAlive(60);   // a throttled browser tab won't drop the connection so quickly

  night = digitalRead(PIN_NIGHT) == HIGH;
  waterM = analogRead(PIN_WATER) / 4095.0 * 1.5;
  updateWeather();
  delay(600);
  lcd.clear();
  Serial.println("Jangkau step path running. Lights work offline; dashboard needs MQTT.");
}

void loop() {
  now = millis();
  networkLoop();
  updateButton();

  if (now - tSensor >= 100)  { tSensor = now; updateSensors(); }
  if (now - tDht >= 2000)    { tDht = now; updateWeather(); }
  if (now - tLights >= 20)   { unsigned long dt = now - tLights; tLights = now; updateLights(dt); }
  updateAlarm();
  if (now - tLcd >= 500)     { tLcd = now; updateLcd(); }
  if (now - tPublish >= 1000) { tPublish = now; publishStatus(); }
}
