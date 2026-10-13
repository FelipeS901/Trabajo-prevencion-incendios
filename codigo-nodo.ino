/* =====================================================================
   DEMO DHT22 + LLAMA IR + MQTT  ·  Semana 8  ·  v3 (Adaptación P23)
   Fundamentos de IoT · 2do semestre 2026
   ===================================================================== */

#include <WiFi.h>
#include <PubSubClient.h>  // mqtt
#include <ArduinoJson.h>
#include <DHT.h>
#include <math.h>
#include "config.h"

/* ---------------- 1. CONFIGURACION ---------------- */
// Pines del Proyecto P23
#define DHTPIN    15     // Pin de datos del DHT22
#define PIN_LLAMA 27     // Pin DO del sensor de llama IR
#define PIN_MQ2   34     // Pin analogico MQ-2 (ADC)

// Tiempos basados en el plan de datos del P23
const uint32_t PERIODO_LECTURA_DHT_MS = 30000; // DHT22 mide cada 30 s
const uint32_t PERIODO_LECTURA_IR_MS  = 5000;  // Llama IR mide cada 5 s
const uint32_t PERIODO_LECTURA_MQ2_MS = 3000;  // MQ-2 mide cada 3 s (vigilancia)
const uint32_t PERIODO_PUB_MS         = 5000;  // Publicar cada 5 s (mínimo permitido)

const uint32_t REINTENTO_WIFI_MS  = 15000; // cada 15s intenta conectarse wifi
const uint32_t ESPERA_INICIAL     = 2000;  // backoff MQTT: 2, 4, 8, 16, 30 s mqtt
const uint32_t ESPERA_MAXIMA      = 30000; // mqtt
const uint8_t  MAX_FALLOS         = 8;     // 8 lecturas invalidas seguidas => sensor_ok = 0

const uint32_t MQ2_PRECALENTAMIENTO_MS = 180000; // 180 s
const uint32_t MQ2_PERIODO_CAL_MS      = 50;     // 50 ms entre muestras de calibracion
const uint8_t  MQ2_MUESTRAS_CAL         = 60;    // 60 muestras de calibracion
const uint16_t MQ2_MV_MIN_VALIDO        = 50;    // mV
const uint16_t MQ2_MV_MAX_VALIDO        = 3100;  // mV
const float    MQ2_VREF_MV              = 3300.0f;
const float    CAIDA_MINIMA_HUMO        = 0.30f;
const float    UMBRAL_MIN_RS_R0         = 0.50f;
const float    UMBRAL_MAX_RS_R0         = 0.85f;

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
uint8_t fallosMQ2Seguidos = 0;
bool    sensorOk = false;
uint8_t estado_llama = 0;      // Guardaremos la llama como 0 o 1

enum FaseMQ2 : uint8_t {
  MQ2_PRECALENTANDO = 0,
  MQ2_CALIBRANDO,
  MQ2_VIGILANDO
};

FaseMQ2 faseMQ2 = MQ2_PRECALENTANDO;
uint32_t tInicioMQ2 = 0;
uint32_t tCalibracionMQ2 = 0;
uint32_t tLecturaMQ2 = 0;
uint8_t muestrasCalibracionMQ2 = 0;
float sumaRsCalMQ2 = 0.0f;
float sumaRs2CalMQ2 = 0.0f;

float r0_mq2 = NAN;
float rs_r0_base = NAN;
float desviacion_relativa_mq2 = NAN;
float umbral_adaptativo_rs_r0 = NAN;
float razon_rs_r0 = NAN;
bool  mq2Calibrado = false;
bool  mq2LecturaValida = false;

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
}

// 3.2 Lectura del Sensor Llama IR
void leerLlamaIR() {
  // Los sensores IR normalmente son activos en bajo (LOW = llama detectada)
  bool nivel = digitalRead(PIN_LLAMA);
  estado_llama = (nivel == LOW) ? 1 : 0; // Convertimos a 1/0 porque no se admiten booleanos
}

bool calcularRsDesdeMV(float mv, float &rs) {
  if (mv < MQ2_MV_MIN_VALIDO || mv > MQ2_MV_MAX_VALIDO) return false;
  if (mv <= 0.0f || mv >= MQ2_VREF_MV) return false;
  float rsLocal = (MQ2_VREF_MV - mv) / mv;
  if (!isfinite(rsLocal) || rsLocal <= 0.0f) return false;
  rs = rsLocal;
  return true;
}

void actualizarEstadoSensorOK() {
  bool dhtOk = (fallosSeguidos < MAX_FALLOS);
  bool mq2Ok = (!mq2Calibrado) || (fallosMQ2Seguidos < MAX_FALLOS);
  sensorOk = dhtOk && mq2Ok;
}

void actualizarMQ2() {
  uint32_t ahora = millis();

  if (faseMQ2 == MQ2_PRECALENTANDO) {
    if (ahora - tInicioMQ2 >= MQ2_PRECALENTAMIENTO_MS) {
      faseMQ2 = MQ2_CALIBRANDO;
      tCalibracionMQ2 = ahora;
      muestrasCalibracionMQ2 = 0;
      sumaRsCalMQ2 = 0.0f;
      sumaRs2CalMQ2 = 0.0f;
      Serial.println("[MQ2] precalentamiento completado, iniciando calibracion");
    }
    return;
  }

  if (faseMQ2 == MQ2_CALIBRANDO) {
    if (ahora - tCalibracionMQ2 < MQ2_PERIODO_CAL_MS) return;
    tCalibracionMQ2 = ahora;

    float rs = NAN;
    float mv = (float)analogReadMilliVolts(PIN_MQ2);
    if (!calcularRsDesdeMV(mv, rs)) {
      Serial.printf("[MQ2] muestra calibracion invalida: %.0f mV\n", mv);
      return;
    }

    sumaRsCalMQ2 += rs;
    sumaRs2CalMQ2 += (rs * rs);
    muestrasCalibracionMQ2++;

    if (muestrasCalibracionMQ2 < MQ2_MUESTRAS_CAL) return;

    r0_mq2 = sumaRsCalMQ2 / (float)MQ2_MUESTRAS_CAL;
    float varianza = (sumaRs2CalMQ2 / (float)MQ2_MUESTRAS_CAL) - (r0_mq2 * r0_mq2);
    if (varianza < 0.0f) varianza = 0.0f;
    float sigma = sqrtf(varianza);

    if (!isfinite(r0_mq2) || r0_mq2 <= 0.0f) {
      Serial.println("[MQ2] calibracion invalida, reiniciando fase de calibracion");
      muestrasCalibracionMQ2 = 0;
      sumaRsCalMQ2 = 0.0f;
      sumaRs2CalMQ2 = 0.0f;
      return;
    }

    rs_r0_base = 1.0f;
    desviacion_relativa_mq2 = sigma / r0_mq2;
    float caidaAdaptativa = fmaxf(CAIDA_MINIMA_HUMO, 3.0f * desviacion_relativa_mq2);
    umbral_adaptativo_rs_r0 = rs_r0_base - caidaAdaptativa;
    umbral_adaptativo_rs_r0 = constrain(umbral_adaptativo_rs_r0, UMBRAL_MIN_RS_R0, UMBRAL_MAX_RS_R0);

    faseMQ2 = MQ2_VIGILANDO;
    mq2Calibrado = true;
    mq2LecturaValida = false;
    fallosMQ2Seguidos = 0;
    tLecturaMQ2 = ahora;

    Serial.printf("[MQ2] calibrado: R0=%.5f sigma_rel=%.5f umbral=%.5f\n",
                  r0_mq2, desviacion_relativa_mq2, umbral_adaptativo_rs_r0);
    return;
  }

  if (ahora - tLecturaMQ2 < PERIODO_LECTURA_MQ2_MS) return;
  tLecturaMQ2 = ahora;

  float rs = NAN;
  float mv = (float)analogReadMilliVolts(PIN_MQ2);
  if (!calcularRsDesdeMV(mv, rs) || !isfinite(r0_mq2) || r0_mq2 <= 0.0f) {
    mq2LecturaValida = false;
    if (fallosMQ2Seguidos < 255) fallosMQ2Seguidos++;
    Serial.printf("[MQ2] lectura invalida (%u seguidas), mV=%.0f\n",
                  (unsigned)fallosMQ2Seguidos, mv);
    return;
  }

  float razon = rs / r0_mq2;
  if (!isfinite(razon) || razon <= 0.0f) {
    mq2LecturaValida = false;
    if (fallosMQ2Seguidos < 255) fallosMQ2Seguidos++;
    Serial.printf("[MQ2] razon invalida (%u seguidas)\n", (unsigned)fallosMQ2Seguidos);
    return;
  }

  razon_rs_r0 = razon;
  mq2LecturaValida = true;
  fallosMQ2Seguidos = 0;
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
  if (mq2Calibrado && mq2LecturaValida && isfinite(razon_rs_r0)) {
    doc["razon_rs_r0"] = roundf(razon_rs_r0 * 1000.0f) / 1000.0f;
  }
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
  analogSetPinAttenuation(PIN_MQ2, ADC_11db);
  analogReadResolution(12);
  tInicioMQ2 = millis();

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
  actualizarEstadoSensorOK();

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
  actualizarMQ2();
  actualizarEstadoSensorOK();
  
  // Publicar cada 5 segundos con el último valor disponible de ambos
  if (ahora - tPub >= PERIODO_PUB_MS) { 
    tPub = ahora; 
    publicarDatos(); 
  }
}