# Nodo inteligente de detección de incendios

## Sensores utilizados:
-DHT22 (temperatura y humedad)
-MQ-2 (humo)
-Sensor IR (llama)

## Funcionamiento
Si se detecta humo y llama simultaneamente:
- Se activa una alarma local (buzzer)
- Se envian datos a una plataforma web

## Wokwi (calibracion de dos puntos)
https://wokwi.com/projects/472554117536717825

Utilizando Wokwi se logro simular la calibracion de dos puntos usando un ESP32 y un potenciometro, gracias a esta simulacion se pudo
obtener los valores de m y b.
- m = 0,443
- b = -1,8764

## Grafo de estados (GT2)
<img width="1424" height="820" alt="Grafo de estados" src="https://github.com/user-attachments/assets/06a64acd-7d5b-4f4a-b7ab-09142bd5784f" />

## MQ-2 grafico calibración
<<img width="1425" height="1050" alt="mq-2 grafico" src="https://github.com/user-attachments/assets/2c5c33af-5276-4490-a4d3-8a7d1393f83c" />

## Ítem 1: Verificación Física del Sensor
Durante la sesión experimentamos un percance de hardware: al conectar el sensor MQ-2 a las pilas de 3,7V con su respectivo divisor de voltaje, una de las pilas se descargó. Esto limitó severamente el rango de detección a solo 8 cm, lo que impidió completar la calibración física de forma óptima según la cápsula. 

## Ítem 8: Justificación de Umbrales e Histéresis

Las decisiones para configurar los tiempos y umbrales en nuestra máquina de estados fueron tomadas basándonos directamente en las guías de laboratorio y los códigos de referencia entregados por los docentes en la Semana 4.

* **Histéresis temporal y Persistencia:** El tiempo de espera de 10 segundos (`TIMEOUT_SOSPECHA_MS = 10000`) y el límite de 3 lecturas inválidas (`MAX_INVALIDAS = 3`) los establecimos basándonos exactamente en los parámetros del código oficial de la asignatura (`p23_paso5_fusion_persistencia.ino`). Con esto aseguramos que el sistema tenga el tiempo recomendado para confirmar un incendio real y filtrar errores temporales sin quedarse bloqueado.
* **Umbrales de MQ-2 (1500 ADC) y Temperatura (45.0 °C):** Fijamos estos valores apoyándonos en los lineamientos de la "Guía de los Sensores" y nuestro documento de diseño. El objetivo es separar claramente una emergencia real de las variaciones normales del ambiente (como días muy calurosos o polvo doméstico), para evitar que el zumbador oscile por falsas alarmas.
  
## Evidencia Ítem 6: Transición a Estado de Error (GT2)
<img width="709" height="231" alt="image" src="https://github.com/user-attachments/assets/70d1883a-9f82-490c-a4f4-d5414c43ae3b" />

## Evidencia Ítem 7: Fusión de Sensores
<img width="772" height="575" alt="image" src="https://github.com/user-attachments/assets/8bc976f4-47a9-4c6a-8bd4-53f1a9715016" />

## Evidencia Ítem 8: Histéresis Temporal
<img width="764" height="598" alt="image" src="https://github.com/user-attachments/assets/8bf7dc8b-ec9e-4d6d-9131-1a75aa94e70e" />



## integrantes:
- Fiorella Bovet, Victoria Rojas, Felipe Stuardo, Arthur Urtubia y Javier Vidal
