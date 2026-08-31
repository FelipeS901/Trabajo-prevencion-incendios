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

## Evidencia Ítem 6: Transición a Estado de Error (GT2)
<img width="709" height="231" alt="image" src="https://github.com/user-attachments/assets/70d1883a-9f82-490c-a4f4-d5414c43ae3b" />

## Evidencia Ítem 7: Fusión de Sensores
<img width="772" height="575" alt="image" src="https://github.com/user-attachments/assets/8bc976f4-47a9-4c6a-8bd4-53f1a9715016" />

## Evidencia Ítem 8: Histéresis Temporal
<img width="764" height="598" alt="image" src="https://github.com/user-attachments/assets/8bf7dc8b-ec9e-4d6d-9131-1a75aa94e70e" />



## integrantes:
- Fiorella Bovet, Victoria Rojas, Felipe Stuardo, Arthur Urtubia y Javier Vidal
