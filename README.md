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

## integrantes:
- Fiorella Bovet, Victoria Rojas, Felipe Stuardo, Arthur Urtubia y Javier Vidal
