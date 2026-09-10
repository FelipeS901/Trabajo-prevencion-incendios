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

## Ítem 1: Verificación Física del Sensor
Durante la sesión experimentamos un percance de hardware: al conectar el sensor MQ-2 a las pilas de 3,7V con su respectivo divisor de voltaje, una de las pilas se descargó. Esto limitó severamente el rango de detección a solo 8 cm, lo que impidió completar la calibración física de forma óptima según la cápsula. 


## integrantes:
- Fiorella Bovet, Victoria Rojas, Felipe Stuardo, Arthur Urtubia y Javier Vidal
