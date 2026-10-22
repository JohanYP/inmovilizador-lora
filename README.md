# Inmovilizador IoT LoRa para Motocicletas (Popayán)

Proyecto de grado de Ingeniería Informática - Unimayor.

## Hardware Base
- **SoC Maestro:** Seeed Studio XIAO ESP32-S3 (Xtensa LX7 dual-core @ 240MHz)
- **Transceptor LoRa:** Seeed Studio Wio-SX1262 (Semtech SX1262 @ 915.0 MHz)
- **Acople:** Conector directo B2B (Board-to-Board)
- **Antenas:**
  - Antena A-03 (860-930 MHz): conectada al Wio-SX1262 (LoRa)
  - Antena A-02 (2.4 GHz): conectada al XIAO ESP32-S3 (Wi-Fi / BLE)

## Mapeo de Pines B2B
- `LORA_CS`: GPIO 41
- `LORA_DIO1`: GPIO 39
- `LORA_RST`: GPIO 42
- `LORA_BUSY`: GPIO 40
- `LORA_SCK`: GPIO 7
- `LORA_MISO`: GPIO 8
- `LORA_MOSI`: GPIO 9

## Estructura del Espacio de Trabajo
- `docs/`: Documentación técnica, guías de configuración y marco metodológico.
- `firmware/`: Código fuente de producción para emisor (llavero) y receptor (moto).
- `pruebas/`: Scripts y sketches para caracterización de límites (Wi-Fi, BLE, LoRa, consumo, latencia, alcance).
- `esquemas/`: Diagramas de conexión eléctrica, etapas de potencia vehicular y circuito de corte.
