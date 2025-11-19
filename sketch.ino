#include <WiFi.h>
#include <PubSubClient.h>

// CONFIG WIFI/MQTT
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASS     = "";

const char* MQTT_BROKER   = "test.mosquitto.org";
const uint16_t MQTT_PORT  = 1883;
const char* MQTT_CLIENTID = "equalpath-smartdesk-001";

const char* TOPIC_PUBLISH = "equalpath/smartdesk/data";
const char* TOPIC_COMMAND = "equalpath/smartdesk/cmd";

// PINOS
const int LED_PIN = 2;
const int BTN_PIN = 14;
const int POT_PIN = 34;   // temperatura simulada
const int LDR_PIN = 35;   // luminosidade

// OBJETOS
WiFiClient espClient;
PubSubClient mqtt(espClient);


// ESTADOS
bool studying = false;
bool lastBtnState = HIGH;
unsigned long lastPub = 0;
unsigned long lastBlink = 0;

float tempC = 0;
int lightLevel = 0;
float tempScore = 0;
float lightScore = 0;
float focusScore = 0;

bool ledState = LOW;

const unsigned long PUB_INTERVAL_MS = 1200;

// FUNÇÕES DE SCORE
float calcTempScore(float t) {
  if (t >= 22 && t <= 24) return 100;

  float s;
  if (t < 22) s = 100 - (22 - t) * 12;
  else        s = 100 - (t - 24) * 12;

  if (s < 0) s = 0;
  if (s > 100) s = 100;
  return s;
}

float calcLightScore(int lvl) {
  if (lvl >= 40 && lvl <= 70) return 100;

  float s;
  if (lvl < 40) s = lvl * 2.5;
  else          s = 100 - (lvl - 70) * 3;

  if (s < 0) s = 0;
  if (s > 100) s = 100;
  return s;
}

// WIFI
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Conectando ao Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi conectado!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// MQTT CALLBACK
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";

  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  msg.trim();

  Serial.print("Comando recebido MQTT: ");
  Serial.println(msg);

  if (msg == "start" || msg == "1") {
    studying = true;
  }
  else if (msg == "stop" || msg == "0") {
    studying = false;
  }

  Serial.print("Estado atualizado -> studying = ");
  Serial.println(studying ? "SIM" : "NAO");
}

// MQTT RECONNECT
void connectMQTT() {
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);

  while (!mqtt.connected()) {
    Serial.print("Conectando ao MQTT...");
    if (mqtt.connect(MQTT_CLIENTID)) {
      Serial.println(" conectado!");
      mqtt.subscribe(TOPIC_COMMAND);
      Serial.println("Assinado: equalpath/smartdesk/cmd");
    } else {
      Serial.print(" falhou, rc=");
      Serial.println(mqtt.state());
      delay(2000);
    }
  }
}

// BOTÃO (toggle com debounce)
void updateButton() {
  static unsigned long lastTime = 0;
  const unsigned long debounceMs = 80;

  bool reading = digitalRead(BTN_PIN);

  if (reading != lastBtnState) {
    lastTime = millis();
  }

  if ((millis() - lastTime) > debounceMs) {
    static bool lastStable = HIGH;

    if (lastStable == HIGH && reading == LOW) {
      studying = !studying;
      Serial.print("Clique! Agora estudando: ");
      Serial.println(studying ? "SIM" : "NAO");
    }

    lastStable = reading;
  }

  lastBtnState = reading;
}

// SENSORES
void readSensors() {
  int potRaw = analogRead(POT_PIN);
  tempC = map(potRaw, 0, 4095, 18, 30);

  int ldrRaw = analogRead(LDR_PIN);
  lightLevel = map(ldrRaw, 0, 4095, 0, 100);

  tempScore = calcTempScore(tempC);
  lightScore = calcLightScore(lightLevel);
  focusScore = (tempScore + lightScore) / 2.0;
}

// LED 
void updateLed() {
  unsigned long now = millis();

  if (!studying) {
    digitalWrite(LED_PIN, LOW);
    ledState = LOW;
    return;
  }

  if (focusScore >= 70) {
    digitalWrite(LED_PIN, HIGH);
  }
  else if (focusScore >= 40) {
    if (now - lastBlink > 500) {
      lastBlink = now;
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
    }
  }
  else {
    if (now - lastBlink > 150) {
      lastBlink = now;
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
    }
  }
}


// PUBLICAR MQTT 
void publishData() {
  char payload[256];

  // Converte millis() para HH:MM:SS
  unsigned long totalSec = millis() / 1000;
  unsigned long sec  = totalSec % 60;
  unsigned long min  = (totalSec / 60) % 60;
  unsigned long hour = totalSec / 3600;

  char timeStr[16];
  snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu:%02lu", hour, min, sec);

  // Monta JSON 
  snprintf(payload, sizeof(payload),
    "{\"studying\":%s,"
    "\"temperatureC\":%.1f,\"tempScore\":%.1f,"
    "\"lightPercent\":%d,\"lightScore\":%.1f,"
    "\"focusScore\":%.1f,"
    "\"uptime\":\"%s\"}",
    studying ? "true" : "false",
    tempC, tempScore,
    lightLevel, lightScore,
    focusScore,
    timeStr
  );

  bool ok = mqtt.publish(TOPIC_PUBLISH, payload);

  if (ok) {
    Serial.print("Publicado MQTT: ");
    Serial.println(payload);
  } else {
    Serial.println("Falha ao publicar");
  }
}


// SETUP
void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BTN_PIN, INPUT_PULLUP);

  connectWiFi();
  connectMQTT();
}

// LOOP
void loop() {
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  updateButton();
  readSensors();
  updateLed();

  if (millis() - lastPub > PUB_INTERVAL_MS) {
    lastPub = millis();
    publishData();
  }
}