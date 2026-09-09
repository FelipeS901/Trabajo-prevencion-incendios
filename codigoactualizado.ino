/* ==========================================================================
   FUNDAMENTOS DE IoT - 2do SEMESTRE 2026
   Deteccion de incendio forestal - FSM con pantalla OLED
   Revision docente sobre la version del equipo (umbral adaptativo MQ-2)

   Cambios respecto de la version entregada:
   1. Rs/R0 se calcula en milivolts reales (analogReadMilliVolts), no en
      cuentas crudas, y con Vc explicito. La expresion (4095 - adc)/adc
      suponia que 4095 cuentas equivalen a Vc, lo que es falso: 4095
      equivale a la referencia del ADC del ESP32, no a los 5 V del MQ-2.
   2. Vc entra como constante VC_SENSOR_MV, que se fija con el valor medido
      con multimetro en la salida del LM2596. No se estima ni se supone.
   3. Precalentamiento no bloqueante antes de calibrar la linea base.
      La calibracion a los 3 s del arranque tomaba un transitorio termico.
   4. Calibracion sin delay(), integrada a la maquina de estados.
   5. Un solo parametro declara la caida minima (CAIDA_MINIMA_HUMO).
   6. Guardias de saturacion y de circuito abierto en el canal del MQ-2.
   7. ALARMA_CONFIRMADA queda enclavada: exige rearme manual.
   8. Zumbador por LEDC (ledcWriteTone), sin conmutacion manual en el lazo.

   NOTA: el grafo de estados del README debe actualizarse. Ahora son seis
   estados, no cuatro.
   ========================================================================== */

#include "DHTesp.h"
#include <U8x8lib.h>   // libreria "U8g2" (Oliver Kraus), modo texto U8x8
#include <math.h>

// ---------------------------------------------------------------------------
// 1. Configuracion
// ---------------------------------------------------------------------------
const uint8_t PIN_LLAMA   = 27;    // Llama IR (DO) -> GPIO 27
const uint8_t PIN_MQ2     = 34;    // MQ-2 (AO) -> GPIO 34 (ADC1, solo entrada)
const uint8_t PIN_DHT     = 15;    // DHT22 -> GPIO 15
const uint8_t PIN_BUZZER  = 25;    // Zumbador pasivo -> GPIO 25
const uint8_t PIN_LED_OK  = 32;    // Estado normal
const uint8_t PIN_LED_AL  = 33;    // Alerta / error
const uint8_t PIN_BOTON   = 4;     // Boton multifuncion (paro / rearme)

#define TIPO_DHT DHTesp::DHT22

// --- Tension de alimentacion del MQ-2 ----------------------------------------
// El MQ-2 cuelga de la salida de 5 V del LM2596, que es un regulador conmutado:
// Vc se mantiene estable mientras la bateria no baje del dropout del regulador.
// MEDIR con multimetro esa salida y escribir aqui el valor real en milivolts.
// Si el multimetro marca 4,98 V, esta constante vale 4980, no 5000.
const float    VC_SENSOR_MV = 5000.0;

// --- Umbrales -----------------------------------------------------------------
const float    UMBRAL_TEMP_CRIT    = 60.0;    // grados C
const float    CAIDA_MINIMA_HUMO   = 0.30;    // 30 % de caida de Rs respecto de R0
const float    UMBRAL_MIN          = 0.50;    // Cotas del umbral adaptativo
const float    UMBRAL_MAX          = 0.85;

const uint32_t TIMEOUT_SOSPECHA_MS = 30000;   // 30 s
const uint32_t PERIODO_MUESTREO_MS = 3000;    // Minimo del DHT22
const uint8_t  MAX_INVALIDAS       = 8;       // Persistencia de fallo de sensor
const uint32_t T_ANTIRREBOTE_MS    = 80;

// --- Precalentamiento y calibracion ------------------------------------------
// El MQ-2 requiere estabilizacion termica. Tres minutos es el minimo operativo
// del laboratorio; el R0 realmente estable exige varias horas. Documenten en el
// README el tiempo de estabilizacion que midan en banco.
const uint32_t T_PRECALENTAMIENTO_MS = 180000;  // 3 min
const uint16_t N_MUESTRAS_CAL        = 60;
const uint32_t PERIODO_CAL_MS        = 50;      // 60 x 50 ms = 3 s de promediado

// --- Validez del canal del MQ-2 ----------------------------------------------
// Fuera de esta ventana la lectura no representa una concentracion: o el ADC
// esta saturado (posible sobretension en GPIO 34) o el canal esta abierto.
const uint16_t VOUT_MIN_VALIDO_MV = 50;
const uint16_t VOUT_MAX_VALIDO_MV = 3100;

const bool     LLAMA_ACTIVA_EN_BAJO = true;
const uint32_t FREC_BUZZER_HZ       = 2000;
const uint8_t  RES_BUZZER_BITS      = 10;

const uint32_t PERIODO_PANTALLA_MS = 500;
const uint32_t VELOCIDAD_I2C_HZ    = 400000;

// ---------------------------------------------------------------------------
// 2. Estado interno
// ---------------------------------------------------------------------------
enum Estado : uint8_t {
  PRECALENTANDO, CALIBRANDO, VIGILANDO, SOSPECHA, ALARMA_CONFIRMADA, ERROR_SEGURO
};

DHTesp   dht;
Estado   estado     = PRECALENTANDO;
uint32_t t_entrada  = 0;
uint32_t t_pantalla = 0;

float    temperatura     = 0.0;
uint16_t vout_mq2_mV     = 0;
float    razon_rs_r0     = 1.0;
bool     llama_detectada = false;
uint8_t  invalidas_dht   = 0;
uint8_t  invalidas_mq    = 0;

float    rs_r0_base   = 1.0;    // Rs/RL en aire limpio (R0 relativo)
float    sigma_rel    = 0.0;    // Dispersion relativa de la linea base
float    umbral_rs_r0 = 1.0 - CAIDA_MINIMA_HUMO;   // Se ajusta al calibrar

// Acumuladores de la calibracion no bloqueante
uint16_t cal_n     = 0;
float    cal_suma  = 0.0;
float    cal_suma2 = 0.0;
uint32_t t_cal     = 0;

U8X8_SSD1306_128X64_NONAME_HW_I2C oled(U8X8_PIN_NONE);

// ---------------------------------------------------------------------------
// 3. Funciones auxiliares
// ---------------------------------------------------------------------------
const char* nombreEstado(Estado e) {
  switch (e) {
    case PRECALENTANDO:     return "PRECALENTANDO";
    case CALIBRANDO:        return "CALIBRANDO";
    case VIGILANDO:         return "VIGILANDO";
    case SOSPECHA:          return "SOSPECHA";
    case ALARMA_CONFIRMADA: return "ALARMA_CONFIRM";
    case ERROR_SEGURO:      return "ERROR_SEGURO";
  }
  return "?";
}

void cambiar(Estado e, const char* motivo) {
  Serial.printf("[%8lu ms] TRANSICION: %s -> %s   (%s)\n",
                millis(), nombreEstado(estado), nombreEstado(e), motivo);
  estado     = e;
  t_entrada  = millis();
  t_pantalla = 0;
}

// Rs referida a RL, adimensional: Rs/RL = (Vc - Vout) / Vout.
// RL se cancela despues al dividir por la linea base, de modo que no hace
// falta conocer su valor. Vc no se cancela: por eso entra explicito.
float rsRelativa(float vout_mV) {
  if (vout_mV < 1.0)             return -1.0;   // Canal abierto o en corto
  if (vout_mV >= VC_SENSOR_MV)   return -1.0;   // Vout no puede superar a Vc
  return (VC_SENSOR_MV - vout_mV) / vout_mV;
}

bool lecturaMQ2Valida(uint16_t vout_mV) {
  return (vout_mV >= VOUT_MIN_VALIDO_MV) && (vout_mV <= VOUT_MAX_VALIDO_MV);
}

void telemetria() {
  Serial.printf("[%8lu ms] T=%5.1fC Vmq2=%4umV Rs/R0=%5.2f (umbral %4.2f) "
                "Llama=%s | %-14s | invD=%u invM=%u\n",
                millis(), temperatura, vout_mq2_mV, razon_rs_r0, umbral_rs_r0,
                llama_detectada ? "SI" : "NO",
                nombreEstado(estado), invalidas_dht, invalidas_mq);
}

void leerSensores() {
  static uint32_t t_muestreo = 0;
  if (millis() - t_muestreo < PERIODO_MUESTREO_MS) return;
  t_muestreo = millis();

  // DHT22
  float lecturaT = dht.getTemperature();
  if (isnan(lecturaT)) { invalidas_dht++; }
  else                 { temperatura = lecturaT; invalidas_dht = 0; }

  // MQ-2: milivolts calibrados de fabrica, no cuentas crudas
  vout_mq2_mV  = analogReadMilliVolts(PIN_MQ2);
  float rs_rel = rsRelativa((float)vout_mq2_mV);

  if (!lecturaMQ2Valida(vout_mq2_mV) || rs_rel < 0.0) {
    invalidas_mq++;                 // Saturacion o canal abierto: no se inventa valor
  } else {
    invalidas_mq = 0;
    razon_rs_r0  = (rs_r0_base > 0.0) ? (rs_rel / rs_r0_base) : 1.0;
  }

  // Llama IR
  bool nivel_llama = digitalRead(PIN_LLAMA);
  llama_detectada  = LLAMA_ACTIVA_EN_BAJO ? (nivel_llama == LOW) : (nivel_llama == HIGH);

  telemetria();
}

// Fusion de sensores: humo, llama, temperatura
uint8_t contarSensoresEnPeligro() {
  uint8_t conteo = 0;
  if (razon_rs_r0 <= umbral_rs_r0)     conteo++;
  if (llama_detectada)                 conteo++;
  if (temperatura >= UMBRAL_TEMP_CRIT) conteo++;
  return conteo;
}

// Calibracion no bloqueante: una muestra cada PERIODO_CAL_MS.
// Devuelve true cuando termina.
bool calibrarPaso() {
  if (millis() - t_cal < PERIODO_CAL_MS) return false;
  t_cal = millis();

  uint16_t v    = analogReadMilliVolts(PIN_MQ2);
  float    r    = rsRelativa((float)v);
  if (!lecturaMQ2Valida(v) || r < 0.0) {
    invalidas_mq++;              // Calibrar sobre lecturas invalidas no tiene sentido
    return false;
  }

  cal_suma  += r;
  cal_suma2 += r * r;
  cal_n++;

  if (cal_n < N_MUESTRAS_CAL) return false;

  rs_r0_base   = cal_suma / cal_n;
  float var    = (cal_suma2 / cal_n) - (rs_r0_base * rs_r0_base);
  float sigma  = (var > 0.0) ? sqrt(var) : 0.0;
  sigma_rel    = (rs_r0_base > 0.0) ? (sigma / rs_r0_base) : 0.0;

  // Umbral adaptativo: caida declarada mas tres sigma del propio montaje
  umbral_rs_r0 = 1.0 - (CAIDA_MINIMA_HUMO + 3.0 * sigma_rel);
  if (umbral_rs_r0 > UMBRAL_MAX) umbral_rs_r0 = UMBRAL_MAX;
  if (umbral_rs_r0 < UMBRAL_MIN) umbral_rs_r0 = UMBRAL_MIN;

  Serial.printf("[CALIBRACION] Rs/RL base=%.4f  sigma_rel=%.4f  umbral=%.3f\n",
                rs_r0_base, sigma_rel, umbral_rs_r0);
  return true;
}

void linea(uint8_t fila, const char* texto) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", texto);
  oled.drawString(0, fila * 2, buf);
}

void pantalla() {
  if (millis() - t_pantalla < PERIODO_PANTALLA_MS) return;
  t_pantalla = millis();

  char txt[24];
  linea(0, nombreEstado(estado));

  if (estado == PRECALENTANDO) {
    uint32_t resta = (T_PRECALENTAMIENTO_MS - (millis() - t_entrada)) / 1000;
    linea(1, "Estabilizando");
    snprintf(txt, sizeof(txt), "Faltan: %lu s", resta);
    linea(2, txt);
  } else if (estado == CALIBRANDO) {
    linea(1, "Aire limpio");
    snprintf(txt, sizeof(txt), "Muestra %u/%u", cal_n, N_MUESTRAS_CAL);
    linea(2, txt);
  } else if (invalidas_dht > 0 || invalidas_mq > 0) {
    linea(1, "FALLO DE SENSOR");
    snprintf(txt, sizeof(txt), "DHT:%u MQ:%u /%u", invalidas_dht, invalidas_mq, MAX_INVALIDAS);
    linea(2, txt);
  } else {
    snprintf(txt, sizeof(txt), "T:%4.1fC R:%4.2f", temperatura, razon_rs_r0);
    linea(1, txt);
    snprintf(txt, sizeof(txt), "Llama:%-3s Pel:%u/3",
             llama_detectada ? "SI" : "NO", contarSensoresEnPeligro());
    linea(2, txt);
  }

  snprintf(txt, sizeof(txt), "T_Est: %lu s", (millis() - t_entrada) / 1000);
  linea(3, txt);
}

// Un unico flanco por iteracion del lazo
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
  ledcWriteTone(PIN_BUZZER, on ? FREC_BUZZER_HZ : 0);
}

void buzzerIntermitente() {
  buzzer(((millis() / 300) % 2) == 0);
}

void leds(bool ok, bool alerta) {
  digitalWrite(PIN_LED_OK, ok ? HIGH : LOW);
  digitalWrite(PIN_LED_AL, alerta ? HIGH : LOW);
}

// ---------------------------------------------------------------------------
// 4. Programa
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  dht.setup(PIN_DHT, TIPO_DHT);

  pinMode(PIN_BOTON, INPUT_PULLDOWN);
  pinMode(PIN_LLAMA, INPUT);
  pinMode(PIN_LED_OK, OUTPUT);
  pinMode(PIN_LED_AL, OUTPUT);

  ledcAttach(PIN_BUZZER, FREC_BUZZER_HZ, RES_BUZZER_BITS);
  buzzer(false);

  analogSetPinAttenuation(PIN_MQ2, ADC_11db);

  oled.setBusClock(VELOCIDAD_I2C_HZ);
  oled.begin();
  oled.setFont(u8x8_font_chroma48medium8_r);
  oled.clear();

  Serial.println("\n========================================================");
  Serial.println(" Deteccion de incendio forestal - FSM");
  Serial.printf(" Caida minima declarada: %.0f %% | Vc = %.0f mV\n",
                CAIDA_MINIMA_HUMO * 100.0, VC_SENSOR_MV);
  Serial.println("========================================================");

  cambiar(PRECALENTANDO, "arranque");
}

void loop() {
  bool pulso = boton();   // Se lee una sola vez por iteracion

  switch (estado) {

    case PRECALENTANDO:
      buzzer(false);
      leds(false, ((millis() / 500) % 2) == 0);   // Parpadeo: no esta vigilando aun
      if (millis() - t_entrada >= T_PRECALENTAMIENTO_MS) {
        cal_n = 0; cal_suma = 0.0; cal_suma2 = 0.0; t_cal = millis();
        cambiar(CALIBRANDO, "fin de precalentamiento");
      }
      break;

    case CALIBRANDO:
      buzzer(false);
      leds(true, true);
      if (calibrarPaso()) cambiar(VIGILANDO, "linea base establecida");
      else if (invalidas_mq >= MAX_INVALIDAS)
        cambiar(ERROR_SEGURO, "MQ-2 fuera de rango durante la calibracion");
      break;

    case VIGILANDO:
      leerSensores();
      buzzer(false);
      leds(true, false);
      if (contarSensoresEnPeligro() >= 1)
        cambiar(SOSPECHA, "al menos un sensor en peligro");
      break;

    case SOSPECHA:
      leerSensores();
      buzzer(false);
      leds(true, true);
      if (contarSensoresEnPeligro() >= 2)
        cambiar(ALARMA_CONFIRMADA, "fusion de dos o mas sensores");
      else if (millis() - t_entrada >= TIMEOUT_SOSPECHA_MS)
        cambiar(VIGILANDO, "timeout de 30 s sin confirmacion");
      break;

    case ALARMA_CONFIRMADA:
      leerSensores();
      buzzer(true);
      leds(false, true);
      // Enclavada: no se sale sola. Requiere rearme manual y que el peligro
      // haya cedido. En este estado el boton es rearme, no paro.
      if (pulso && contarSensoresEnPeligro() < 2)
        cambiar(VIGILANDO, "rearme manual con peligro disipado");
      break;

    case ERROR_SEGURO:
      buzzerIntermitente();       // La salida segura no es silencio
      leds(false, true);
      if (pulso) {
        invalidas_dht = 0;
        invalidas_mq  = 0;
        cambiar(PRECALENTANDO, "rearme: se recalibra la linea base");
      }
      break;
  }

  // Guardias globales hacia ERROR_SEGURO
  if (estado == VIGILANDO || estado == SOSPECHA) {
    if (invalidas_dht >= MAX_INVALIDAS)
      cambiar(ERROR_SEGURO, "DHT22 invalido de forma persistente");
    else if (invalidas_mq >= MAX_INVALIDAS)
      cambiar(ERROR_SEGURO, "MQ-2 saturado o desconectado");
    else if (pulso)
      cambiar(ERROR_SEGURO, "paro manual");
  }

  pantalla();
}
