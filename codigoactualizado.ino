/* ==========================================================================
   FUNDAMENTOS DE IoT - 2do SEMESTRE 2026
   GT2 - Detección de Incendio conformada al Grafo de Estados con Pantalla OLED
   Adaptado con Autocalibración Rs/R0 (Opción A), T_CRIT=60.0°C y Timeout=30s
   ========================================================================== */

#include "DHTesp.h"
#include <U8x8lib.h>   // librería "U8g2" (Oliver Kraus), modo texto U8x8
#include <math.h>

// ---------------------------------------------------------------------------
// 1. Configuración de Hardware
// ---------------------------------------------------------------------------
const uint8_t PIN_LLAMA   = 27;    // Entr. Digital (Llama IR DO) -> GPIO 27
const uint8_t PIN_MQ2     = 34;    // Entr. Analógica (MQ-2 AO) -> GPIO 34 (ADC1)
const uint8_t PIN_DHT     = 15;    // Sensor Temperatura/Humedad -> GPIO 15
const uint8_t PIN_BUZZER  = 25;    // Zumbador pasivo -> GPIO 25

const uint8_t PIN_LED_OK  = 32;    // Estado Normal
const uint8_t PIN_LED_AL  = 33;    // Estado Alerta / Error
const uint8_t PIN_BOTON   = 4;     // Botón multifunción (Paro / Rearme)

#define TIPO_DHT DHTesp::DHT22

// --- Parámetros y Umbrales Dinámicos ---
const float    UMBRAL_TEMP_CRIT     = 60.0;   // Temp >= 60.0 °C 
const uint32_t TIMEOUT_SOSPECHA_MS  = 30000;  // Timeout 30s (millis() - t_entrada >= 30000ms)

const uint32_t PERIODO_MUESTREO_MS  = 2000;   // Muestreo mínimo DHT22 (>= 2s)
const uint8_t  MAX_INVALIDAS        = 8;      // Persistencia: 3 lecturas inválidas
const uint32_t T_ANTIRREBOTE_MS     = 80;     // Antirrebote de botón

const bool     LLAMA_ACTIVA_EN_BAJO = true;   // Salida digital Llama IR activa en LOW
const bool     BUZZER_PASIVO        = true;   // Zumbador pasivo (requiere onda cuadrada)
const uint32_t FREC_BUZZER_HZ       = 2000;   // Frecuencia del tono

// --- Pantalla ---
const uint32_t PERIODO_PANTALLA_MS = 500;
const uint32_t VELOCIDAD_I2C_HZ    = 400000;  // 400 kHz: máximo del SSD1306

// --- Variables de Calibración MQ-2 (Opción A: Razón Adimensional Rs/R0) ---
float adc0_linea_base = 200.0; // Valor por defecto hasta ejecutar setup()
float sigma_adc0      = 0.0;
float umbral_rs_r0    = 0.70;  // Umbral dinámico adaptativo (caída del 30% en Rs/R0)
float razon_rs_r0     = 1.0;   // Razón actual calculada

// ---------------------------------------------------------------------------
// 2. Máquina de Estados
// ---------------------------------------------------------------------------
enum Estado : uint8_t { VIGILANDO, SOSPECHA, ALARMA_CONFIRMADA, ERROR_SEGURO };

DHTesp   dht;
Estado   estado      = VIGILANDO;
uint32_t t_entrada   = 0;
uint32_t t_pantalla  = 0;    // Último refresco de la pantalla

float    temperatura     = 0.0;
uint16_t humo_adc        = 0;
bool     llama_detectada = false;
uint8_t  invalidas       = 0;

// Objeto de la pantalla OLED SSD1306 por I2C
U8X8_SSD1306_128X64_NONAME_HW_I2C oled(U8X8_PIN_NONE);

const char* nombreEstado(Estado e) {
  switch (e) {
    case VIGILANDO:          return "VIGILANDO";
    case SOSPECHA:           return "SOSPECHA";
    case ALARMA_CONFIRMADA: return "ALARMA_CONFIRM";
    case ERROR_SEGURO:       return "ERROR_SEGURO";
  }
  return "?";
}

void cambiar(Estado e, const char* motivo) {
  Serial.printf("[%8lu ms] TRANSICION: %s -> %s   (%s)\n",
                millis(), nombreEstado(estado), nombreEstado(e), motivo);
  estado     = e;
  t_entrada  = millis();
  t_pantalla = 0;   // Fuerza el refresco inmediato en la pantalla
}

// ---------------------------------------------------------------------------
// 3. Funciones de Calibración MQ-2 (Opción A)
// ---------------------------------------------------------------------------
void calibrarMQ2() {
  Serial.println("\n[CALIBRACION] Midiendo linea base de MQ-2 en aire limpio...");
  const uint8_t MUESTRAS = 60;
  float lecturas[MUESTRAS];
  float suma = 0.0;

  for (uint8_t i = 0; i < MUESTRAS; i++) {
    lecturas[i] = (float)analogRead(PIN_MQ2);
    suma += lecturas[i];
    delay(50); // Muestreo continuo durante 3 segundos
  }

  // 1. Media de la línea base (ADC0)
  adc0_linea_base = suma / MUESTRAS;
  if (adc0_linea_base < 1.0) adc0_linea_base = 1.0; // Evita división por cero

  // 2. Dispersión (Desviación estándar sigma)
  float suma_sq = 0.0;
  for (uint8_t i = 0; i < MUESTRAS; i++) {
    suma_sq += pow(lecturas[i] - adc0_linea_base, 2);
  }
  sigma_adc0 = sqrt(suma_sq / MUESTRAS);

  // 3. Cálculo de Umbral Dinámico derivado de la dispersión del montaje
  float margen_ruido = (3.0 * sigma_adc0) / adc0_linea_base;
  umbral_rs_r0 = 1.0 - (0.20 + margen_ruido); // Caída mínima del 20% + margen 3*sigma

  // Límite de seguridad para el umbral
  if (umbral_rs_r0 > 0.85) umbral_rs_r0 = 0.85;
  if (umbral_rs_r0 < 0.50) umbral_rs_r0 = 0.50;

  Serial.printf("[CALIBRACION] ADC0 Base: %.2f | Sigma: %.2f | Umbral Rs/R0: %.3f\n\n",
                adc0_linea_base, sigma_adc0, umbral_rs_r0);
}

float calcularRazonRsR0(uint16_t adc_actual) {
  if (adc_actual <= 0) return 1.0;
  
  // Expresión simplificada de la razón Rs/R0 (Cancela Vc y RL)
  float rs_actual = (4095.0 - (float)adc_actual) / (float)adc_actual;
  float r0_base   = (4095.0 - adc0_linea_base) / adc0_linea_base;

  if (r0_base <= 0) return 1.0;
  return rs_actual / r0_base;
}

// ---------------------------------------------------------------------------
// 4. Entradas, Salidas y Fusión (No Bloqueantes)
// ---------------------------------------------------------------------------
void telemetria();

void leerSensores() {
  static uint32_t t_muestreo = 0;
  if (millis() - t_muestreo < PERIODO_MUESTREO_MS) return;
  t_muestreo = millis();

  // DHT22: Lectura con verificación
  float lecturaT = dht.getTemperature();
  if (isnan(lecturaT)) { 
    invalidas++; 
  } else { 
    temperatura = lecturaT; 
    invalidas = 0; 
  }

  // MQ-2: Lectura ADC y cálculo de Razón Adimensional
  humo_adc     = analogRead(PIN_MQ2);
  razon_rs_r0  = calcularRazonRsR0(humo_adc);

  // Llama IR: Lectura digital con polaridad de comparador
  bool nivel_llama = digitalRead(PIN_LLAMA);
  llama_detectada  = (LLAMA_ACTIVA_EN_BAJO) ? (nivel_llama == LOW) : (nivel_llama == HIGH);

  telemetria();
}

void telemetria() {
  Serial.printf("[%8lu ms] T=%5.1fC Humo=%4u (Rs/R0=%4.2f) Llama=%s | %-17s | inv=%u\n",
                millis(), temperatura, humo_adc, razon_rs_r0,
                llama_detectada ? "SI" : "NO", 
                nombreEstado(estado), invalidas);
}

// ---------------------------------------------------------------------------
// 4b. Pantalla I2C (4 líneas de texto)
// ---------------------------------------------------------------------------
void linea(uint8_t fila, const char* texto) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", texto);
  oled.drawString(0, fila * 2, buf); // Fila 0, 2, 4, 6 en coordenadas U8x8
}

// Fusión de 2-3 sensores (Humo Rs/R0, Llama, Temp)
uint8_t contarSensoresEnPeligro() {
  uint8_t conteo = 0;
  if (razon_rs_r0 <= umbral_rs_r0)    conteo++;
  if (llama_detectada)                conteo++;
  if (temperatura >= UMBRAL_TEMP_CRIT) conteo++;
  return conteo;
}

void pantalla() {
  if (millis() - t_pantalla < PERIODO_PANTALLA_MS) return;
  t_pantalla = millis();

  char txt[24];
  
  // Línea 1: Estado FSM
  linea(0, nombreEstado(estado));

  if (invalidas > 0) {
    linea(1, "SENS: DHT ERROR");
    snprintf(txt, sizeof(txt), "Lect. Inv: %u/3", invalidas);
    linea(2, txt);
  } else {
    // Línea 2: Temp y Razón Rs/R0
    snprintf(txt, sizeof(txt), "T:%4.1fC R:%4.2f", temperatura, razon_rs_r0);
    linea(1, txt);

    // Línea 3: Llama IR y Conteo de Peligro
    snprintf(txt, sizeof(txt), "Llama:%-3s Pel:%u/3", 
             llama_detectada ? "SI" : "NO", 
             contarSensoresEnPeligro());
    linea(2, txt);
  }

  // Línea 4: Tiempo en estado actual
  snprintf(txt, sizeof(txt), "T_Est: %lu s", (millis() - t_entrada) / 1000);
  linea(3, txt);
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

// ---------------------------------------------------------------------------
// 5. Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  dht.setup(PIN_DHT, TIPO_DHT);
  
  pinMode(PIN_BOTON, INPUT_PULLDOWN);
  pinMode(PIN_LLAMA, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_OK, OUTPUT);
  pinMode(PIN_LED_AL, OUTPUT);

  // Inicialización de pantalla OLED I2C
  oled.setBusClock(VELOCIDAD_I2C_HZ);
  oled.begin();
  oled.setFont(u8x8_font_chroma48medium8_r);
  oled.clear();
  linea(0, "INCENDIO FSM");
  linea(1, "ARRANQUE...");

  Serial.println("\n========================================================");
  Serial.println(" Deteccion de Incendio Forestal - FSM Grafo de Estados");
  Serial.printf(" Pantalla: OLED SSD1306 128x64 en I2C 0x3C a %lu Hz\n", VELOCIDAD_I2C_HZ);
  Serial.println("========================================================");

  // Calibración inicial del sensor MQ-2 en aire limpio
  calibrarMQ2();

  cambiar(VIGILANDO, "arranque");
}

// ---------------------------------------------------------------------------
// 6. Lazo Principal (FSM 1 a 1 con el Grafo)
// ---------------------------------------------------------------------------
void loop() {
  leerSensores();
  uint8_t sensores_activos = contarSensoresEnPeligro();

  switch (estado) {

    case VIGILANDO:
      buzzer(false); 
      leds(true, false);

      // Transición: Rs/R0 <= Umbral OR Llama == true OR Temp >= 60.0 °C
      if (sensores_activos >= 1) {
        cambiar(SOSPECHA, "Rs/R0 <= Umbral OR Llama == true OR temp >= 60.0 C");
      }
      break;

    case SOSPECHA:
      buzzer(false); 
      leds(true, true);

      // Transición 1: Fusión de 2-3 sensores -> ALARMA_CONFIRMADA
      if (sensores_activos >= 2) {
        cambiar(ALARMA_CONFIRMADA, "Fusion de 2-3 sensores (Humo, Llama, Temp)");
      } 
      // Transición 2: Timeout 30s (millis() - t_entrada >= 30000ms) -> VIGILANDO
      else if (millis() - t_entrada >= TIMEOUT_SOSPECHA_MS) {
        cambiar(VIGILANDO, "Timeout 30s (millis() - t_entrada >= 30000ms)");
      }
      break;

    case ALARMA_CONFIRMADA:
      buzzer(true); 
      leds(false, true);

      // Transición: Peligro parcialmente disipado (< 2 sensores) -> SOSPECHA
      if (sensores_activos < 2) {
        cambiar(SOSPECHA, "Peligro parcialmente disipado (< 2 sensores)");
      }
      break;

    case ERROR_SEGURO:
      buzzerIntermitente(); 
      leds(false, true);

      // Transición: botón de rearme -> VIGILANDO
      if (boton()) { 
        invalidas = 0; 
        cambiar(VIGILANDO, "boton de rearme"); 
      }
      break;
  }

  // Guardias Globales (Transición hacia ERROR_SEGURO desde cualquier estado)
  if (estado != ERROR_SEGURO) {
    bool pulsado = boton();
    if (invalidas >= MAX_INVALIDAS) {
      cambiar(ERROR_SEGURO, "3 lecturas invalidas consecutivas (lectura invalida)");
    } else if (pulsado) {
      cambiar(ERROR_SEGURO, "boton de paro o emergencia");
    }
  }

  pantalla();   // Refresco temporizado de la pantalla OLED
}
