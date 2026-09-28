/* =====================================================================
   DEMO DHT22 + LLAMA IR + MQTT  ·  Semana 8  ·  v3 (Adaptación P23)
   Fundamentos de IoT · 2do semestre 2026
   ===================================================================== */

#include <WiFi.h>
#include <PubSubClient.h>  // mqtt
#include <ArduinoJson.h>
#include <DHT.h>
#include "config.h"

/* ---------------- 1. CONFIGURACION ---------------- */
// Pines del Proyecto P23
#define DHTPIN    15     // Pin de datos del DHT22
#define PIN_LLAMA 27     // Pin DO del sensor de llama IR

// Tiempos basados en el plan de datos del P23
const uint32_t PERIODO_LECTURA_DHT_MS = 30000; // DHT22 mide cada 30 s
const uint32_t PERIODO_LECTURA_IR_MS  = 5000;  // Llama IR mide cada 5 s
const uint32_t PERIODO_PUB_MS         = 5000;  // Publicar cada 5 s (mínimo permitido)

const uint32_t REINTENTO_WIFI_MS  = 15000; // cada 15s intenta conectarse wifi
const uint32_t ESPERA_INICIAL     = 2000;  // backoff MQTT: 2, 4, 8, 16, 30 s mqtt
const uint32_t ESPERA_MAXIMA      = 30000; // mqtt
const uint8_t  MAX_FALLOS         = 8;     // 8 lecturas invalidas seguidas => sensor_ok = 0

/* ---------------- 2. ESTADO INTERNO ---------------- */
DHT          dht(DHTPIN, DHTTYPE);
WiFiClient   red;
PubSubClient mqtt(red);

String clientId, topicDatos, topicEstado, topicCmd;

uint32_t tLecturaDHT = 0, tLecturaIR = 0, tPub = 0, tWiFi = 0, tReconexion = 0;
uint32_t esperaReconexion = ESPERA_INICIAL;

float   temperatura = NAN;     // ultima lectura VALIDA
float   humedad     = NAN;
uint8_t fallosSeguidos = MAX_FALLOS; 
bool    sensorOk = false;
uint8_t estado_llama = 0;      // Guardaremos la llama como 0 o 1

/* ---------------- 3. FUNCIONES AUXILIARES ---------------- */

// 3.1 Lectura del DHT22
void leerDHT22() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    if (fallosSeguidos < 255) fallosSeguidos++;
    Serial.printf("[DHT22] lectura invalida (%u seguidas)\n", (unsigned)fallosSeguidos);
  } else {
    temperatura = t;
    humedad = h;
    fallosSeguidos = 0;
  }
  sensorOk = (fallosSeguidos < MAX_FALLOS);
}

// 3.2 Lectura del Sensor Llama IR
void leerLlamaIR() {
  // Los sensores IR normalmente son activos en bajo (LOW = llama detectada)
  bool nivel = digitalRead(PIN_LLAMA);
  estado_llama = (nivel == LOW) ? 1 : 0; // Convertimos a 1/0 porque no se admiten booleanos
}

// 3.3 Comandos entrantes
void recibirComando(char* topic, byte* payload, unsigned int largo) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, largo);
  if (error) {
    Serial.printf("[cmd] JSON invalido en %s: %s\n", topic, error.c_str());
    return;
  }
  Serial.printf("[cmd] recibido en %s\n", topic);
}

// 3.4 WiFi sin bloquear 
void mantenerWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  uint32_t ahora = millis();
  if (ahora - tWiFi < REINTENTO_WIFI_MS) return;
  tWiFi = ahora;
  Serial.println("[wifi] sin red, reintentando...");
  WiFi.reconnect();
}

// 3.5 MQTT sin bloquear
void mantenerMQTT() {
  if (mqtt.connected()) return;
  if (WiFi.status() != WL_CONNECTED) return;
  uint32_t ahora = millis();
  if (ahora - tReconexion < esperaReconexion) return;
  tReconexion = ahora;

  Serial.printf("[mqtt] conectando como %s ... ", clientId.c_str());
  if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS,
                   topicEstado.c_str(), 1, true, "offline")) {
    Serial.println("OK");
    mqtt.publish(topicEstado.c_str(), "online", true); 
    mqtt.subscribe(topicCmd.c_str(), 1); 
    esperaReconexion = ESPERA_INICIAL;
  } else {
    Serial.printf("FALLO rc=%d, reintento en %u s\n",
                  mqtt.state(), (unsigned)(esperaReconexion / 1000));
    esperaReconexion = (esperaReconexion * 2 > ESPERA_MAXIMA) ? ESPERA_MAXIMA : esperaReconexion * 2;
  }
}

// 3.6 Publicacion: JSON plano
void publicarDatos() {
  if (!mqtt.connected()) return;

  JsonDocument doc;
  
  // Usamos el nombre del sensor/variable como clave para identificar qué envía qué
  if (sensorOk) {
    doc["temperatura"] = roundf(temperatura * 10.0f) / 10.0f; 
    doc["humedad"]     = roundf(humedad * 10.0f) / 10.0f;
  }
  
  doc["llama_ir"]  = estado_llama; // Se envía como 0 o 1[cite: 11]
  doc["sensor_ok"] = sensorOk ? 1 : 0;
  doc["rssi_dbm"]  = WiFi.RSSI();

  char payload[256];
  // serializeJson(doc, buf) genera el QoS implicito = 1 en ArduinoJson 7.x
  size_t n = serializeJson(doc, payload, sizeof(payload));

  // publish(topico, datos, largo, RETAINED)[cite: 1]
  if (mqtt.publish(topicDatos.c_str(), (const uint8_t*)payload, n, true)) {
    Serial.printf("[pub] %s -> %s\n", topicDatos.c_str(), payload);
  } else {
    Serial.println("[pub] ERROR publish() (buffer o sesion)");
  }
}

/* ---------------- 4. PROGRAMA ---------------- */
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n==== NODO P23: Llama IR + DHT22 ====");

  dht.begin();
  pinMode(PIN_LLAMA, INPUT);

  // el tópico de equipo debe ser exactamente: curso/E33/P23/nodo1
  clientId    = String(MQTT_USER) + "-" + NODO; 
  topicDatos  = String("curso/") + MQTT_USER + "/" + PROYECTO + "/" + NODO;
  topicEstado = topicDatos + "/estado";
  topicCmd    = topicDatos + "/cmd";
  Serial.printf("[id] Client ID: %s\n[id] Datos    : %s\n", clientId.c_str(), topicDatos.c_str());

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 10000) { 
    delay(200);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[wifi] IP = ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[wifi] sin red todavia; el nodo sigue midiendo y reintenta");
  }
  tWiFi = millis();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(recibirComando);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(15); 
  mqtt.setSocketTimeout(3);
}

void loop() {
  mantenerWiFi();
  mantenerMQTT();
  mqtt.loop(); 

  uint32_t ahora = millis();
  
  // Tiempos independientes de muestreo para cada sensor
  if (ahora - tLecturaIR >= PERIODO_LECTURA_IR_MS) { 
    tLecturaIR = ahora; 
    leerLlamaIR(); 
  }
  if (ahora - tLecturaDHT >= PERIODO_LECTURA_DHT_MS) { 
    tLecturaDHT = ahora; 
    leerDHT22(); 
  }
  
  // Publicar cada 5 segundos con el último valor disponible de ambos
  if (ahora - tPub >= PERIODO_PUB_MS) { 
    tPub = ahora; 
    publicarDatos(); 
  }
}