/* ==========================================================================
   FUNDAMENTOS DE IoT - 2do SEMESTRE 2026
   GT2 - Deteccion de Incendio conformada al Grafo de Estados
   ========================================================================== */

#include "DHTesp.h"

// ---------------------------------------------------------------------------
// 1. Configuracion de Hardware (Segun Hoja de Conexiones P23)
// ---------------------------------------------------------------------------
const uint8_t PIN_LLAMA   = 27;    // Entr. Digital (Llama IR DO) -> GPIO 27
const uint8_t PIN_MQ2     = 34;    // Entr. Analogica (MQ-2 AO) -> GPIO 34 (ADC1)
const uint8_t PIN_DHT     = 15;    // Sensor Temperatura/Humedad -> GPIO 15
const uint8_t PIN_BUZZER  = 25;    // Zumbador pasivo -> GPIO 25

const uint8_t PIN_LED_OK  = 32;    // Estado Normal
const uint8_t PIN_LED_AL  = 33;    // Estado Alerta / Error
const uint8_t PIN_BOTON   = 4;     // Boton multifuncion (Paro / Rearme)

#define TIPO_DHT DHTesp::DHT22

// Parametros y Umbrales segun Grafo y Guia de Sensores
const uint16_t UMBRAL_HUMO_ADC      = 1500;   // Humo (MQ-2) >= 1500 ADC counts
const float    UMBRAL_TEMP_CRIT     = 45.0;   // Temp >= 45.0 °C
const uint32_t TIMEOUT_SOSPECHA_MS  = 10000;  // Timeout 10s (millis() - t_entrada >= 10000ms)

const uint32_t PERIODO_MUESTREO_MS  = 2000;   // Muestreo minimo DHT22 (>= 2s)
const uint8_t  MAX_INVALIDAS        = 3;      // Persistencia: 3 lecturas invalidas
const uint32_t T_ANTIRREBOTE_MS     = 100;     // Antirrebote de boton

const bool     LLAMA_ACTIVA_EN_BAJO = true;   // Salida digital Llama IR activa en LOW
const bool     BUZZER_PASIVO        = true;   // Zumbador pasivo (requiere onda cuadrada)
const uint32_t FREC_BUZZER_HZ       = 2000;   // Frecuencia del tono

// ---------------------------------------------------------------------------
// 2. Maquina de Estados (Enum de 4 estados del Grafo)
// ---------------------------------------------------------------------------
enum Estado : uint8_t { VIGILANDO, SOSPECHA, ALARMA_CONFIRMADA, ERROR_SEGURO };

DHTesp   dht;
Estado   estado      = VIGILANDO;
uint32_t t_entrada   = 0;

float    temperatura     = 0.0;
uint16_t humo_adc        = 0;
bool     llama_detectada = false;
uint8_t  invalidas       = 0;

const char* nombreEstado(Estado e) {
  switch (e) {
    case VIGILANDO:          return "VIGILANDO";
    case SOSPECHA:           return "SOSPECHA";
    case ALARMA_CONFIRMADA: return "ALARMA_CONFIRMADA";
    case ERROR_SEGURO:       return "ERROR_SEGURO";
  }
  return "?";
}

void cambiar(Estado e, const char* motivo) {
  Serial.printf("[%8lu ms] TRANSICION: %s -> %s   (%s)\n",
                millis(), nombreEstado(estado), nombreEstado(e), motivo);
  estado    = e;
  t_entrada = millis();
}

// ---------------------------------------------------------------------------
// 3. Entradas, Salidas y Fusion (Funciones No Bloqueantes)
// ---------------------------------------------------------------------------
void telemetria();

void leerSensores() {
  static uint32_t t_muestreo = 0;
  if (millis() - t_muestreo < PERIODO_MUESTREO_MS) return;
  t_muestreo = millis();

  // DHT22: Lectura con verificacion
  float lecturaT = dht.getTemperature();
  if (isnan(lecturaT)) { 
    invalidas++; 
  } else { 
    temperatura = lecturaT; 
    invalidas = 0; 
  }

  // MQ-2: Lectura ADC directa en GPIO 34
  humo_adc = analogRead(PIN_MQ2);

  // Llama IR: Lectura digital con polaridad de comparador
  bool nivel_llama = digitalRead(PIN_LLAMA);
  llama_detectada  = (LLAMA_ACTIVA_EN_BAJO) ? (nivel_llama == LOW) : (nivel_llama == HIGH);

  telemetria();
}

void telemetria() {
  Serial.printf("[%8lu ms] T=%5.1fC Humo=%4u Llama=%s | %-17s | invalidas=%u\n",
                millis(), temperatura, humo_adc, 
                llama_detectada ? "SI" : "NO", 
                nombreEstado(estado), invalidas);
}

bool boton() {
  static uint32_t t_ultimo   = 0;
  static bool     nivel_prev = false;
  bool nivel  = (digitalRead(PIN_BOTON) == HIGH);
  bool flanco = nivel && !nivel_prev && (millis() - t_ultimo >= T_ANTIRREBOTE_MS);
  if (flanco) t_ultimo = millis();
  nivel_prev = nivel;
  return flanco;
}

void buzzer(bool on) {
  if (!BUZZER_PASIVO) { 
    digitalWrite(PIN_BUZZER, on ? HIGH : LOW); 
    return; 
  }
  static uint32_t t_us  = 0;
  static bool     nivel = false;
  if (!on) { 
    digitalWrite(PIN_BUZZER, LOW); 
    nivel = false; 
    return; 
  }

  const uint32_t semiperiodo_us = 500000UL / FREC_BUZZER_HZ;
  if (micros() - t_us >= semiperiodo_us) {
    t_us  = micros();
    nivel = !nivel;
    digitalWrite(PIN_BUZZER, nivel ? HIGH : LOW);
  }
}

void leds(bool ok, bool alerta) {
  digitalWrite(PIN_LED_OK, ok ? HIGH : LOW);
  digitalWrite(PIN_LED_AL, alerta ? HIGH : LOW);
}

void buzzerIntermitente() { 
  buzzer((millis() / 300) % 2); 
}

// Fusion de 2-3 sensores (Humo, Llama, Temp)
uint8_t contarSensoresEnPeligro() {
  uint8_t conteo = 0;
  if (humo_adc >= UMBRAL_HUMO_ADC) conteo++;
  if (llama_detectada)              conteo++;
  if (temperatura >= UMBRAL_TEMP_CRIT) conteo++;
  return conteo;
}

// ---------------------------------------------------------------------------
// 4. Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  dht.setup(PIN_DHT, TIPO_DHT);
  
  pinMode(PIN_BOTON, INPUT_PULLDOWN);
  pinMode(PIN_LLAMA, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_OK, OUTPUT);
  pinMode(PIN_LED_AL, OUTPUT);

  Serial.println("\n========================================================");
  Serial.println(" Deteccion de Incendio Forestal - FSM Grafo de Estados");
  Serial.println("========================================================");

  cambiar(VIGILANDO, "arranque");
}

// ---------------------------------------------------------------------------
// 5. Lazo Principal (FSM 1 a 1 con el Grafo)
// ---------------------------------------------------------------------------
void loop() {
  leerSensores();
  uint8_t sensores_activos = contarSensoresEnPeligro();

  switch (estado) {

    case VIGILANDO:
      buzzer(false); 
      leds(true, false);

      // Transicion: Humo >= 1500 ADC OR Llama == true OR Temp >= 45.0 °C
      if (sensores_activos >= 1) {
        cambiar(SOSPECHA, "Humo >= 1500 OR Llama == true OR temp >= 45.0 C");
      }
      break;

    case SOSPECHA:
      buzzer(false); 
      leds(true, true);

      // Transicion 1: Fusion de 2-3 sensores -> ALARMA_CONFIRMADA
      if (sensores_activos >= 2) {
        cambiar(ALARMA_CONFIRMADA, "Fusion de 2-3 sensores (Humo, Llama, Temp)");
      } 
      // Transicion 2: Timeout 10s (millis() - t_entrada >= 10000ms) -> VIGILANDO
      else if (millis() - t_entrada >= TIMEOUT_SOSPECHA_MS) {
        cambiar(VIGILANDO, "Timeout 10s (millis() - t_entrada >= 10000ms)");
      }
      break;

    case ALARMA_CONFIRMADA:
      buzzer(true); 
      leds(false, true);

      // Transicion: Peligro parcialmente disipado (< 2 sensores) -> SOSPECHA
      if (sensores_activos < 2) {
        cambiar(SOSPECHA, "Peligro parcialmente disipado (< 2 sensores)");
      }
      break;

    case ERROR_SEGURO:
      buzzerIntermitente(); 
      leds(false, true);

      // Transicion: boton de rearme -> VIGILANDO
      if (boton()) { 
        invalidas = 0; 
        cambiar(VIGILANDO, "boton de rearme"); 
      }
      break;
  }

  // Guardias Globales (Transicion hacia ERROR_SEGURO desde cualquier estado)
  if (estado != ERROR_SEGURO) {
    bool pulsado = boton();
    if (invalidas >= MAX_INVALIDAS) {
      cambiar(ERROR_SEGURO, "3 lecturas invalidas consecutivas (lectura invalida)");
    } else if (pulsado) {
      cambiar(ERROR_SEGURO, "boton de paro o emergencia");
    }
  }
}
