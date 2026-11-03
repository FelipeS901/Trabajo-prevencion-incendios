#include "DHTesp.h"
#include <U8x8lib.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include "config.h"


// Pines utilizados por los sensores y actuadores.
const uint8_t PIN_LLAMA  = 27;
const uint8_t PIN_MQ2    = 34;
const uint8_t PIN_DHT    = 15;
const uint8_t PIN_BUZZER = 25;
const uint8_t PIN_LED_OK = 32;
const uint8_t PIN_LED_AL = 33;
const uint8_t PIN_BOTON  = 4;


// Tipo de sensor DHT conectado.
#define TIPO_DHT DHTesp::DHT22


// Tension real de alimentacion del MQ-2 en milivoltios.
const float VC_SENSOR_MV = 5000.0;

// Tiempo de precalentamiento del MQ-2.
const uint32_t T_PRECALENTAMIENTO_MS = 180000;

// Numero de muestras usadas para calcular la linea base.
const uint16_t N_MUESTRAS_CAL = 60;

// Tiempo entre muestras durante la calibracion.
const uint32_t PERIODO_CAL_MS = 50;

// Rango valido de tension del MQ-2.
const uint16_t VOUT_MIN_VALIDO_MV = 50;
const uint16_t VOUT_MAX_VALIDO_MV = 3100;

// Caida minima de Rs/R0 para detectar humo.
const float CAIDA_MINIMA_HUMO = 0.30;

// Limites del umbral adaptativo.
const float UMBRAL_MIN = 0.50;
const float UMBRAL_MAX = 0.85;


// Umbral de temperatura critica.
const float UMBRAL_TEMP_CRIT = 60.0;

// Tiempo maximo del estado SOSPECHA.
const uint32_t TIMEOUT_SOSPECHA_MS = 30000;

// Periodos de lectura y publicacion.
const uint32_t PERIODO_LECTURA_DHT_MS = 30000;
const uint32_t PERIODO_LECTURA_LLAMA_MS = 5000;
const uint32_t PERIODO_LECTURA_MQ2_MS = 3000;
const uint32_t PERIODO_PUBLICACION_MS = 5000;

// Numero de errores consecutivos permitidos.
const uint8_t MAX_INVALIDAS = 8;

// Tiempo antirrebote del boton.
const uint32_t T_ANTIRREBOTE_MS = 80;


// Configuracion WiFi y MQTT.
const uint32_t REINTENTO_WIFI_MS = 15000;
const uint32_t ESPERA_INICIAL = 2000;
const uint32_t ESPERA_MAXIMA = 30000;


// Configuracion del buzzer.
const uint32_t FREC_BUZZER_HZ = 2000;
const uint8_t RES_BUZZER_BITS = 10;


// Configuracion de la pantalla OLED.
const uint32_t PERIODO_PANTALLA_MS = 500;
const uint32_t VELOCIDAD_I2C_HZ = 400000;


// El sensor de llama se considera activo en nivel bajo.
const bool LLAMA_ACTIVA_EN_BAJO = true;


// Objetos de sensores, MQTT y pantalla.
DHTesp dht;
WiFiClient red;
PubSubClient mqtt(red);
U8X8_SSD1306_128X64_NONAME_HW_I2C oled(U8X8_PIN_NONE);


// Estados de la maquina de estados.
enum Estado : uint8_t {
  PRECALENTANDO,
  CALIBRANDO,
  VIGILANDO,
  SOSPECHA,
  ALARMA_CONFIRMADA,
  ERROR_SEGURO
};


// Estado actual del sistema.
Estado estado = PRECALENTANDO;


// Variables de identificacion MQTT.
String clientId;
String topicDatos;
String topicEstado;
String topicCmd;


// Tiempo actual de espera para reconectar MQTT.
uint32_t esperaReconexion = ESPERA_INICIAL;


// Temporizadores del sistema.
uint32_t tEntradaEstado = 0;
uint32_t tLecturaDHT = 0;
uint32_t tLecturaLlama = 0;
uint32_t tLecturaMQ2 = 0;
uint32_t tPublicacion = 0;
uint32_t tWiFi = 0;
uint32_t tReconexion = 0;
uint32_t tCalibracion = 0;
uint32_t tPantalla = 0;
uint32_t tUltimoBoton = 0;


// Variables del DHT22.
float temperatura = 0.0;
float humedad = 0.0;
uint8_t invalidasDHT = 0;
bool dhtOk = false;


// Estado del sensor de llama.
uint8_t llamaDetectada = 0;


// Variables del MQ-2.
uint16_t voutMQ2_mV = 0;
float razonRsR0 = 1.0;
float rsR0Base = 1.0;
float sigmaRel = 0.0;
float umbralRsR0 = 1.0 - CAIDA_MINIMA_HUMO;

bool mq2Listo = false;
bool mq2Ok = false;
bool humoDetectado = false;

uint8_t invalidasMQ2 = 0;


// Acumuladores de calibracion.
uint16_t calN = 0;
float calSuma = 0.0;
float calSuma2 = 0.0;


// Solicitud de rearme recibida por MQTT.
bool rearmeMQTSPendiente = false;


// Devuelve el nombre de un estado.
const char* nombreEstado(Estado e) {
  switch (e) {
    case PRECALENTANDO:
      return "PRECALENTANDO";

    case CALIBRANDO:
      return "CALIBRANDO";

    case VIGILANDO:
      return "VIGILANDO";

    case SOSPECHA:
      return "SOSPECHA";

    case ALARMA_CONFIRMADA:
      return "ALARMA_CONFIRM";

    case ERROR_SEGURO:
      return "ERROR_SEGURO";
  }

  return "?";
}


// Cambia el estado y registra la transicion.
void cambiarEstado(Estado nuevoEstado, const char* motivo) {
  Serial.printf(
    "[FSM] %lu ms: %s -> %s (%s)\n",
    millis(),
    nombreEstado(estado),
    nombreEstado(nuevoEstado),
    motivo
  );

  estado = nuevoEstado;
  tEntradaEstado = millis();
  tPantalla = 0;
}


// Calcula Rs/RL utilizando la tension real del sensor.
float calcularRsRelativa(float vout_mV) {
  if (vout_mV < 1.0) {
    return -1.0;
  }

  if (vout_mV >= VC_SENSOR_MV) {
    return -1.0;
  }

  return (VC_SENSOR_MV - vout_mV) / vout_mV;
}


// Verifica que la lectura analogica sea valida.
bool lecturaMQ2Valida(uint16_t vout_mV) {
  return (
    vout_mV >= VOUT_MIN_VALIDO_MV &&
    vout_mV <= VOUT_MAX_VALIDO_MV
  );
}


// Realiza una muestra de calibracion sin bloquear.
bool calibrarMQ2Paso() {
  if (millis() - tCalibracion < PERIODO_CAL_MS) {
    return false;
  }

  tCalibracion = millis();

  uint16_t vout = analogReadMilliVolts(PIN_MQ2);
  float rsRel = calcularRsRelativa((float)vout);

  if (!lecturaMQ2Valida(vout) || rsRel < 0.0) {
    if (invalidasMQ2 < 255) {
      invalidasMQ2++;
    }

    return false;
  }

  invalidasMQ2 = 0;

  calSuma += rsRel;
  calSuma2 += rsRel * rsRel;
  calN++;

  if (calN < N_MUESTRAS_CAL) {
    return false;
  }

  rsR0Base = calSuma / calN;

  float varianza =
    (calSuma2 / calN) -
    (rsR0Base * rsR0Base);

  float sigma =
    varianza > 0.0 ? sqrt(varianza) : 0.0;

  sigmaRel =
    rsR0Base > 0.0 ?
    sigma / rsR0Base :
    0.0;

  umbralRsR0 =
    1.0 -
    (CAIDA_MINIMA_HUMO + 3.0 * sigmaRel);

  if (umbralRsR0 > UMBRAL_MAX) {
    umbralRsR0 = UMBRAL_MAX;
  }

  if (umbralRsR0 < UMBRAL_MIN) {
    umbralRsR0 = UMBRAL_MIN;
  }

  Serial.printf(
    "[MQ-2] Base=%.4f Sigma=%.4f Umbral=%.3f\n",
    rsR0Base,
    sigmaRel,
    umbralRsR0
  );

  mq2Listo = true;

  return true;
}


// Lee el MQ-2 y determina si existe humo.
void leerMQ2() {
  if (!mq2Listo) {
    return;
  }

  voutMQ2_mV =
    analogReadMilliVolts(PIN_MQ2);

  float rsRel =
    calcularRsRelativa(
      (float)voutMQ2_mV
    );

  if (
    !lecturaMQ2Valida(voutMQ2_mV) ||
    rsRel < 0.0
  ) {
    mq2Ok = false;

    if (invalidasMQ2 < 255) {
      invalidasMQ2++;
    }

    return;
  }

  mq2Ok = true;
  invalidasMQ2 = 0;

  razonRsR0 =
    rsR0Base > 0.0 ?
    rsRel / rsR0Base :
    1.0;

  humoDetectado =
    razonRsR0 <= umbralRsR0;
}


// Lee el DHT22 utilizando DHTesp.
void leerDHT22() {
  float lecturaT =
    dht.getTemperature();

  float lecturaH =
    dht.getHumidity();

  if (
    isnan(lecturaT) ||
    isnan(lecturaH)
  ) {
    if (invalidasDHT < 255) {
      invalidasDHT++;
    }

    Serial.printf(
      "[DHT22] Lectura invalida: %u\n",
      invalidasDHT
    );

  } else {
    temperatura = lecturaT;
    humedad = lecturaH;
    invalidasDHT = 0;
  }

  dhtOk =
    invalidasDHT < MAX_INVALIDAS;
}


// Lee el sensor de llama.
void leerLlamaIR() {
  bool nivel =
    digitalRead(PIN_LLAMA);

  llamaDetectada =
    LLAMA_ACTIVA_EN_BAJO ?
    (nivel == LOW ? 1 : 0) :
    (nivel == HIGH ? 1 : 0);
}


// Cuenta cuantos sensores indican peligro.
uint8_t contarSensoresEnPeligro() {
  uint8_t cantidad = 0;

  if (
    mq2Listo &&
    mq2Ok &&
    humoDetectado
  ) {
    cantidad++;
  }

  if (llamaDetectada == 1) {
    cantidad++;
  }

  if (temperatura >= UMBRAL_TEMP_CRIT) {
    cantidad++;
  }

  return cantidad;
}


// Detecta una pulsacion nueva del boton.
bool botonPulsado() {
  static bool nivelAnterior = false;

  bool nivelActual =
    digitalRead(PIN_BOTON) == HIGH;

  bool flanco =
    nivelActual &&
    !nivelAnterior &&
    millis() - tUltimoBoton >= T_ANTIRREBOTE_MS;

  if (flanco) {
    tUltimoBoton = millis();
  }

  nivelAnterior = nivelActual;

  return flanco;
}


// Activa o desactiva el buzzer.
void buzzer(bool encendido) {
  ledcWriteTone(
    PIN_BUZZER,
    encendido ?
    FREC_BUZZER_HZ :
    0
  );
}


// Activa el buzzer de forma intermitente.
void buzzerIntermitente() {
  bool encendido =
    ((millis() / 300) % 2) == 0;

  buzzer(encendido);
}


// Controla los LEDs.
void leds(bool normal, bool alerta) {
  digitalWrite(
    PIN_LED_OK,
    normal ? HIGH : LOW
  );

  digitalWrite(
    PIN_LED_AL,
    alerta ? HIGH : LOW
  );
}


// Escribe una linea en la pantalla OLED.
void lineaOLED(
  uint8_t fila,
  const char* texto
) {
  char buffer[17];

  snprintf(
    buffer,
    sizeof(buffer),
    "%-16s",
    texto
  );

  oled.drawString(
    0,
    fila * 2,
    buffer
  );
}


// Actualiza la pantalla OLED.
void actualizarPantalla() {
  if (
    millis() - tPantalla <
    PERIODO_PANTALLA_MS
  ) {
    return;
  }

  tPantalla = millis();

  char texto[24];

  lineaOLED(
    0,
    nombreEstado(estado)
  );

  if (estado == PRECALENTANDO) {
    uint32_t transcurrido =
      millis() - tEntradaEstado;

    uint32_t restante =
      transcurrido < T_PRECALENTAMIENTO_MS ?
      (T_PRECALENTAMIENTO_MS - transcurrido) / 1000 :
      0;

    lineaOLED(
      1,
      "Estabilizando"
    );

    snprintf(
      texto,
      sizeof(texto),
      "Faltan:%lus",
      restante
    );

    lineaOLED(
      2,
      texto
    );

  } else if (estado == CALIBRANDO) {
    lineaOLED(
      1,
      "Aire limpio"
    );

    snprintf(
      texto,
      sizeof(texto),
      "Muestra:%u/%u",
      calN,
      N_MUESTRAS_CAL
    );

    lineaOLED(
      2,
      texto
    );

  } else {
    snprintf(
      texto,
      sizeof(texto),
      "T:%4.1fC R:%4.2f",
      temperatura,
      razonRsR0
    );

    lineaOLED(
      1,
      texto
    );

    snprintf(
      texto,
      sizeof(texto),
      "L:%u H:%u P:%u/3",
      llamaDetectada,
      humoDetectado ? 1 : 0,
      contarSensoresEnPeligro()
    );

    lineaOLED(
      2,
      texto
    );
  }

  snprintf(
    texto,
    sizeof(texto),
    "Estado:%lus",
    (millis() - tEntradaEstado) / 1000
  );

  lineaOLED(
    3,
    texto
  );
}


// Mantiene la conexion WiFi.
void mantenerWiFi() {
  if (
    WiFi.status() ==
    WL_CONNECTED
  ) {
    return;
  }

  if (
    millis() - tWiFi <
    REINTENTO_WIFI_MS
  ) {
    return;
  }

  tWiFi = millis();

  Serial.println(
    "[WiFi] Sin red, reintentando..."
  );

  WiFi.reconnect();
}


// Mantiene la conexion MQTT.
void mantenerMQTT() {
  if (
    mqtt.connected()
  ) {
    return;
  }

  if (
    WiFi.status() !=
    WL_CONNECTED
  ) {
    return;
  }

  if (
    millis() - tReconexion <
    esperaReconexion
  ) {
    return;
  }

  tReconexion = millis();

  Serial.printf(
    "[MQTT] Conectando como %s ... ",
    clientId.c_str()
  );

  if (
    mqtt.connect(
      clientId.c_str(),
      MQTT_USER,
      MQTT_PASS,
      topicEstado.c_str(),
      1,
      true,
      "offline"
    )
  ) {
    Serial.println("OK");

    mqtt.publish(
      topicEstado.c_str(),
      "online",
      true
    );

    mqtt.subscribe(
      topicCmd.c_str(),
      1
    );

    esperaReconexion =
      ESPERA_INICIAL;

  } else {
    Serial.printf(
      "FALLO rc=%d, reintento en %u s\n",
      mqtt.state(),
      (unsigned)(esperaReconexion / 1000)
    );

    esperaReconexion *= 2;

    if (
      esperaReconexion >
      ESPERA_MAXIMA
    ) {
      esperaReconexion =
        ESPERA_MAXIMA;
    }
  }
}


// Recibe comandos MQTT.
void recibirComando(
  char* topic,
  byte* payload,
  unsigned int largo
) {
  JsonDocument doc;

  DeserializationError error =
    deserializeJson(
      doc,
      payload,
      largo
    );

  if (error) {
    Serial.printf(
      "[CMD] JSON invalido en %s: %s\n",
      topic,
      error.c_str()
    );

    return;
  }

  const char* comando =
    doc["comando"] | "";

  Serial.printf(
    "[CMD] Recibido: %s\n",
    comando
  );

  if (
    strcmp(comando, "rearme") == 0
  ) {
    rearmeMQTSPendiente = true;
  }
}


// Reinicia el proceso de calibracion.
void reiniciarCalibracion() {
  invalidasMQ2 = 0;
  mq2Listo = false;
  mq2Ok = false;
  humoDetectado = false;

  calN = 0;
  calSuma = 0.0;
  calSuma2 = 0.0;

  tCalibracion = millis();
}


// Ejecuta la maquina de estados.
void ejecutarFSM(bool pulsoManual) {
  switch (estado) {

    case PRECALENTANDO:
      buzzer(false);

      leds(
        false,
        ((millis() / 500) % 2) == 0
      );

      if (
        millis() - tEntradaEstado >=
        T_PRECALENTAMIENTO_MS
      ) {
        reiniciarCalibracion();

        cambiarEstado(
          CALIBRANDO,
          "precalentamiento terminado"
        );
      }

      break;


    case CALIBRANDO:
      buzzer(false);
      leds(true, true);

      if (
        calibrarMQ2Paso()
      ) {
        cambiarEstado(
          VIGILANDO,
          "linea base establecida"
        );
      }

      if (
        invalidasMQ2 >=
        MAX_INVALIDAS
      ) {
        cambiarEstado(
          ERROR_SEGURO,
          "MQ-2 invalido durante calibracion"
        );
      }

      break;


    case VIGILANDO:
      buzzer(false);
      leds(true, false);

      if (
        contarSensoresEnPeligro() >= 1
      ) {
        cambiarEstado(
          SOSPECHA,
          "sensor peligroso detectado"
        );
      }

      break;


    case SOSPECHA:
      buzzer(false);
      leds(true, true);

      if (
        contarSensoresEnPeligro() >= 2
      ) {
        cambiarEstado(
          ALARMA_CONFIRMADA,
          "fusion de dos sensores"
        );

      } else if (
        millis() - tEntradaEstado >=
        TIMEOUT_SOSPECHA_MS
      ) {
        cambiarEstado(
          VIGILANDO,
          "timeout sin confirmacion"
        );
      }

      break;


    case ALARMA_CONFIRMADA:
      buzzer(true);
      leds(false, true);

      if (
        (pulsoManual || rearmeMQTSPendiente) &&
        contarSensoresEnPeligro() < 2
      ) {
        rearmeMQTSPendiente = false;

        cambiarEstado(
          VIGILANDO,
          "rearme con peligro disipado"
        );
      }

      break;


    case ERROR_SEGURO:
      buzzerIntermitente();
      leds(false, true);

      if (
        pulsoManual ||
        rearmeMQTSPendiente
      ) {
        rearmeMQTSPendiente = false;

        invalidasDHT = 0;
        dhtOk = false;

        reiniciarCalibracion();

        cambiarEstado(
          PRECALENTANDO,
          "rearme manual o MQTT"
        );
      }

      break;
  }


  if (
    estado == VIGILANDO ||
    estado == SOSPECHA
  ) {
    if (
      invalidasDHT >=
      MAX_INVALIDAS
    ) {
      cambiarEstado(
        ERROR_SEGURO,
        "DHT22 invalido persistentemente"
      );

    } else if (
      invalidasMQ2 >=
      MAX_INVALIDAS
    ) {
      cambiarEstado(
        ERROR_SEGURO,
        "MQ-2 saturado o desconectado"
      );

    } else if (
      pulsoManual
    ) {
      cambiarEstado(
        ERROR_SEGURO,
        "paro manual"
      );
    }
  }
}


// Publica los datos del nodo mediante MQTT.
void publicarDatos() {
  if (
    !mqtt.connected()
  ) {
    return;
  }

  JsonDocument doc;

  doc["temperatura"] =
    roundf(temperatura * 10.0f) / 10.0f;

  doc["humedad"] =
    roundf(humedad * 10.0f) / 10.0f;

  doc["razon_rs_r0"] =
    roundf(razonRsR0 * 100.0f) / 100.0f;

  doc["llama"] =
    llamaDetectada;

  doc["rssi_dbm"] =
    WiFi.RSSI();

  doc["sensor_ok"] =
    (dhtOk && mq2Listo && mq2Ok) ? 1 : 0;

  doc["dht_ok"] =
    dhtOk ? 1 : 0;

  doc["mq2_ok"] =
    mq2Ok ? 1 : 0;

  doc["mq2_listo"] =
    mq2Listo ? 1 : 0;

  doc["mq2_mv"] =
    voutMQ2_mV;

  doc["mq2_umbral"] =
    roundf(umbralRsR0 * 100.0f) / 100.0f;

  doc["humo"] =
    humoDetectado ? 1 : 0;

  doc["sensores_peligro"] =
    contarSensoresEnPeligro();

  // Estado numerico del 0 al 5 para InfluxDB y Telegraf.
  doc["estado"] =
    estado;

  char payload[512];

  size_t n =
    serializeJson(
      doc,
      payload,
      sizeof(payload)
    );

  // La publicacion principal es retenida.
  if (
    mqtt.publish(
      topicDatos.c_str(),
      (const uint8_t*)payload,
      n,
      true
    )
  ) {
    Serial.printf(
      "[PUB] %s -> %s\n",
      topicDatos.c_str(),
      payload
    );

  } else {
    Serial.println(
      "[PUB] ERROR publish()"
    );
  }
}


void setup() {
  Serial.begin(115200);

  delay(500);

  Serial.println(
    "\n==== NODO P23: DHT22 + LLAMA + MQ-2 + MQTT ===="
  );

  dht.setup(
    PIN_DHT,
    TIPO_DHT
  );

  pinMode(
    PIN_LLAMA,
    INPUT
  );

  pinMode(
    PIN_MQ2,
    INPUT
  );

  pinMode(
    PIN_BOTON,
    INPUT_PULLDOWN
  );

  pinMode(
    PIN_LED_OK,
    OUTPUT
  );

  pinMode(
    PIN_LED_AL,
    OUTPUT
  );

  analogSetPinAttenuation(
    PIN_MQ2,
    ADC_11db
  );

  ledcAttach(
    PIN_BUZZER,
    FREC_BUZZER_HZ,
    RES_BUZZER_BITS
  );

  buzzer(false);

  oled.setBusClock(
    VELOCIDAD_I2C_HZ
  );

  oled.begin();

  oled.setFont(
    u8x8_font_chroma48medium8_r
  );

  oled.clear();

  clientId =
    String(MQTT_USER) +
    "-" +
    NODO;

  topicDatos =
    String("curso/") +
    MQTT_USER +
    "/" +
    PROYECTO +
    "/" +
    NODO;

  topicEstado =
    topicDatos +
    "/estado";

  topicCmd =
    topicDatos +
    "/cmd";

  Serial.printf(
    "[ID] Client ID: %s\n",
    clientId.c_str()
  );

  Serial.printf(
    "[ID] Datos: %s\n",
    topicDatos.c_str()
  );

  WiFi.mode(
    WIFI_STA
  );

  WiFi.setAutoReconnect(
    true
  );

  WiFi.setSleep(
    false
  );

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASS
  );

  uint32_t inicioWiFi =
    millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - inicioWiFi < 10000
  ) {
    delay(200);
    Serial.print(".");
  }

  Serial.println();

  if (
    WiFi.status() ==
    WL_CONNECTED
  ) {
    Serial.print(
      "[WiFi] IP: "
    );

    Serial.println(
      WiFi.localIP()
    );

  } else {
    Serial.println(
      "[WiFi] Sin red; se continuara intentando"
    );
  }

  tWiFi =
    millis();

  mqtt.setServer(
    MQTT_HOST,
    MQTT_PORT
  );

  mqtt.setCallback(
    recibirComando
  );

  mqtt.setBufferSize(
    512
  );

  mqtt.setKeepAlive(
    15
  );

  mqtt.setSocketTimeout(
    3
  );

  tEntradaEstado =
    millis();

  cambiarEstado(
    PRECALENTANDO,
    "arranque"
  );
}


void loop() {
  bool pulsoManual =
    botonPulsado();

  mantenerWiFi();
  mantenerMQTT();
  mqtt.loop();

  uint32_t ahora =
    millis();


  if (
    ahora - tLecturaDHT >=
    PERIODO_LECTURA_DHT_MS
  ) {
    tLecturaDHT =
      ahora;

    leerDHT22();
  }


  if (
    ahora - tLecturaLlama >=
    PERIODO_LECTURA_LLAMA_MS
  ) {
    tLecturaLlama =
      ahora;

    leerLlamaIR();
  }


  if (
    estado == CALIBRANDO
  ) {
    calibrarMQ2Paso();
  }


  if (
    mq2Listo &&
    ahora - tLecturaMQ2 >=
    PERIODO_LECTURA_MQ2_MS
  ) {
    tLecturaMQ2 =
      ahora;

    leerMQ2();
  }


  ejecutarFSM(
    pulsoManual
  );


  actualizarPantalla();


  if (
    ahora - tPublicacion >=
    PERIODO_PUBLICACION_MS
  ) {
    tPublicacion =
      ahora;

    publicarDatos();
  }
}
