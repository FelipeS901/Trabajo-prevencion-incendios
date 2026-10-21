# Nodo inteligente de detección de incendios

## Sensores utilizados:
-DHT22 (temperatura y humedad)
-MQ-2 (humo)
-Sensor IR (llama)

## Funcionamiento
Si se detecta humo y llama simultaneamente:
- Se activa una alarma local (buzzer) junto a luces led
- Se envian datos a una plataforma web

## Umbrales sensores
DHT22:
- Temperatura critica: 60°C
MQ-2
- Caída mínima de humo: 30%
- Umbral adaptativo MQ-2: MIN: 0,50 ; MAX: 0,85

## Tiempos y seguridad del sistema
- Estado de sospecha: 30 segundos
- Muestreo : 3 segundos
- Tolerancia a errores de lectura: 8 lecturas
- Precalentamiento sensor MQ-2: 3 minutos (tiempo en el cual no lee ni emite alarmas)

## Wokwi (calibracion de dos puntos)
https://wokwi.com/projects/472554117536717825

Utilizando Wokwi se logro simular la calibracion de dos puntos usando un ESP32 y un potenciometro, gracias a esta simulacion se pudo
obtener los valores de m y b.
- m = 0,443
- b = -1,8764

## Maquina de estados
<img width="1531" height="615" alt="MaquinaDeEstados" src="https://github.com/user-attachments/assets/cca822cf-4a65-49e3-a47b-31532f9a3223" />

## Montaje Hardware
<img width="1036" height="1600" alt="MontajeProyecto" src="https://github.com/user-attachments/assets/565418c7-54a0-4ea9-8792-dcc32a2bfc59" />


## MQ-2 grafico calibración
<<img width="1425" height="1050" alt="mq-2 grafico" src="https://github.com/user-attachments/assets/2c5c33af-5276-4490-a4d3-8a7d1393f83c" />

## Justificación de Umbrales e Histéresis

Las decisiones para configurar los tiempos, umbrales y guardias de seguridad en nuestra máquina de estados fueron actualizadas a partir de las revisiones docentes y el análisis de los sensores:

-Precalentamiento y Calibración: Precalentamiento (T_PRECALENTAMIENTO_MS = 180000): Se fijó un tiempo no bloqueante de 3 minutos (180 s) para permitir que el elemento calefactor del MQ-2 alcance el equilibrio térmico y limpie impurezas superficiales, evitando falsas alarmas durante el transitorio de encendido.

- Línea Base y Umbral Adaptativo: Tras el precalentamiento, se promedian 60 muestras (N_MUESTRAS_CAL) para calcular la resistencia R0 en aire limpio. El umbral de humo se define como una caída del 30 % (CAIDA_MINIMA_HUMO = 0.30) respecto a R0, sumado a 3 sigmas (3 * sigma_rel) de la dispersión del montaje. Este valor se encuentra entre UMBRAL_MIN = 0.50 y UMBRAL_MAX = 0.85 para evitar falsas alarmas.

- Umbrales Críticos de Sensores: Temperatura (UMBRAL_TEMP_CRIT = 60.0 °C): Se decidió mantener 60.0 °C para evitar falsas alarmas por altas temperaturas ambientales de verano y asegurar que solo se active la señal de peligro ante un evento crítico real.

- Llama IR (LLAMA_ACTIVA_EN_BAJO = true): Operación por salida digital en nivel bajo (LOW), detecta la radiacion inflaroja del fuego.

- Persistencia de Fallos (MAX_INVALIDAS = 8): Tolerancia de 8 muestras consecutivas con error o fuera de rango (equivalente a 24 segundos a un ritmo de muestreo de PERIODO_MUESTREO_MS = 3000 ms). Esto filtra ruido transitorio, pero commuta el sistema a ERROR_SEGURO ante la desconexión o falla persistente de un sensor.
  
## Evidencia Ítem 6: Transición a Estado de Error (GT2)
<img width="826" height="280" alt="image" src="https://github.com/user-attachments/assets/ae5123e1-3c97-4a95-a9d8-04b836fef744" />

## Evidencia Ítem 7: Fusión de Sensores
<img width="1022" height="385" alt="image" src="https://github.com/user-attachments/assets/1941e115-c1e5-4e83-9dc3-21d70cef51ce" />

## Evidencia Ítem 8: Histéresis Temporal
<img width="1022" height="457" alt="image" src="https://github.com/user-attachments/assets/0e1f6cda-b810-43fd-9fa8-d3c8b8e311b2" />

## Tabla de Tópicos MQTT (GT4)
| Nombre del Tópico | QoS | Retained | Descripción del Payload |
| :--- | :---: | :---: | :--- |
| `curso/E33/P23/nodo1` | 1 | Sí | JSON plano con datos físicos calibrados (temperatura, humedad, razon_rs_r0, llama, rssi_dbm, sensor_ok) |
| `curso/E33/P23/nodo1/estado` | 1 | Sí | String de texto simple con el estado actual de la máquina de estados (FSM) |
| `curso/E33/P23/nodo1/cmd` | 1 | No | Reservado para recepción de comandos entrantes |

## integrantes:
- Fiorella Bovet, Victoria Rojas, Felipe Stuardo, Arthur Urtubia y Javier Vidal

### Hito 1: Evaluación Regular 1 — NP1: Nodo local

| Integrante | Módulo o tramo desarrollado | Evidencia |
| :--- | :--- | :--- |
| **Felipe Stuardo** | Gestión inicial del repositorio, organización de carpetas y creación de entregas. | [Historial de commits](https://github.com/FelipeS901/Trabajo-prevencion-incendios/commits/main/) |
| **Arthur Urtubia** | Desarrollo del código base, apoyo en las conexiones de hardware y realización de los cálculos matemáticos (calibración MQ-2, umbrales) necesarios para el funcionamiento del sistema.. | [Código histórico de NP1](https://github.com/FelipeS901/Trabajo-prevencion-incendios/blob/main/CODIGOS/archivo-anterior/NP1_Codigo.ino) |
| **Javier Vidal** | Desarrollo del código base y apoyo en las conexiones de hardware. | [Código histórico de NP1](https://github.com/FelipeS901/Trabajo-prevencion-incendios/blob/main/CODIGOS/archivo-anterior/NP1_Codigo.ino) |
| **Fiorella Bovet** | Diseño de la máquina de estados, elaboración de diagramas, apoyo en la presentación y soldadura de componentes. | [Grafo de estados](https://github.com/FelipeS901/Trabajo-prevencion-incendios/blob/main/docs/Grafo-Estados.drawio) |
| **Victoria Rojas** | Elaboración de documentos de guía, organización del equipo, soldadura de componentes, elaboración de la presentación de NP1 y apoyo en el envío de entregas. | [Documentación del proyecto](https://github.com/FelipeS901/Trabajo-prevencion-incendios/tree/main/docs) |

---

### Hito 2: Evaluación Regular 2 — NP2: Sistema IoT conectado y persistido

En NP2, el equipo extendió el nodo local hacia un sistema conectado mediante
MQTT y persistencia de datos. La distribución del trabajo se documenta por
tramos funcionales y se respalda con archivos, funciones y commits del
repositorio.

| Integrante | Contribución en NP2 y correcciones de NP1 | Evidencia |
| :--- | :--- | :--- |
| **Arthur Urtubia** | Desarrollo del modelo 3D y apoyo en la integración de mejoras de hardware. | [Carpeta de documentación](https://github.com/FelipeS901/Trabajo-prevencion-incendios/tree/main/docs) |
| **Javier Vidal** | Apoyo en la configuración MQTT, conexiones y lógica de envío de datos al broker. | [Función `publicarDatos()`](https://github.com/FelipeS901/Trabajo-prevencion-incendios/blob/main/CODIGOS/E33_P23_Firmware.ino#L923-L1005) |
| **Fiorella Bovet** | Apoyo en la edición del firmware, reconexión no bloqueante y actualización del grafo FSM a seis estados. | [Firmware actualizado](https://github.com/FelipeS901/Trabajo-prevencion-incendios/blob/main/CODIGOS/E33_P23_Firmware.ino) · [Grafo FSM](https://github.com/FelipeS901/Trabajo-prevencion-incendios/blob/main/docs/Grafo-Estados.drawio) |
| **Felipe Stuardo** | Documentación de GT4, apoyo en la gestión del repositorio, organización de entregas y revisión de tags/releases. | [Historial del repositorio](https://github.com/FelipeS901/Trabajo-prevencion-incendios/commits/main/) |
| **Victoria Rojas** | Integración y actualización del firmware oficial, incluyendo el sensor MQ-2 y la coordinación de cambios finales del repositorio. | [Commits de Victoria Rojas](https://github.com/FelipeS901/Trabajo-prevencion-incendios/commits/main/?author=vctoriaing) |

#### Correcciones de NP1 incorporadas o en proceso de verificación

- **Grafo FSM:** la fuente editable representa seis estados:
  `PRECALENTANDO`, `CALIBRANDO`, `VIGILANDO`, `SOSPECHA`,
  `ALARMA_CONFIRMADA` y `ERROR_SEGURO`.
- **Umbral de humo:** el firmware informa el umbral como una razón
  adimensional `Rs/R0`, no como un valor en kΩ.
- **Calibración:** se utilizan `60` muestras, tomadas cada `50 ms`, después
  del precalentamiento del MQ-2.


## Identificación del proyecto
- Equipo: **E33**
- Proyecto: **P23**
- Asignatura: **Fundamentos de IoT**

## Firmware oficial y versiones históricas
- Firmware oficial: `CODIGOS/E33_P23_Firmware.ino`.
- Variante equivalente sin comentarios: `CODIGOS/E33_1510`.
- Versiones históricas/de desarrollo (no oficiales): `CODIGOS/archivo-anterior/codigo-nodo.ino` y `CODIGOS/archivo-anterior/NP1_Codigo.ino`.

## Estados FSM publicados en `estado`
En el firmware actual, el campo `estado` del payload se publica como **valor numérico** según el enum de seis estados:

- `0 PRECALENTANDO`
- `1 CALIBRANDO`
- `2 VIGILANDO`
- `3 SOSPECHA`
- `4 ALARMA_CONFIRMADA`
- `5 ERROR_SEGURO`

## Esquema de InfluxDB
- Bucket: `mediciones`
- Measurement: `mediciones`
- Tags:
  - `equipo=E33`
  - `proyecto=P23`
  - `nodo=nodo1`
- Fields: valores numéricos del payload.

## Documentación en `docs/`
La documentación consolidada del proyecto está en `docs/`:
- PDF de lanzamiento: [`docs/E33_P23_Lanzamiento_Proyecto.pdf`](docs/E33_P23_Lanzamiento_Proyecto.pdf)
- Editables disponibles:
  - [`docs/Arquitectura-Capas.drawio`](docs/Arquitectura-Capas.drawio)
  - [`docs/Arquitectura-capas.json`](docs/Arquitectura-capas.json)
  - [`docs/Grafo-Estados.drawio`](docs/Grafo-Estados.drawio)
  - [`docs/Grafo-Estados.json`](docs/Grafo-Estados.json)

> Aclaración MQTT: se mantiene la tabla de tópicos y el payload del tópico de datos es **JSON plano**.

