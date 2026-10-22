#include <RadioLib.h>
#include <SPI.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>

// Mapeo B2B oficial: XIAO ESP32-S3 + Wio-SX1262
#define LORA_SCK    7
#define LORA_MISO   8
#define LORA_MOSI   9
#define LORA_CS    41
#define LORA_DIO1  39
#define LORA_RST   42
#define LORA_BUSY  40

#define LED_PIN    LED_BUILTIN // GPIO 21

SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);

// Variable que sobrevive en memoria RTC durante el Deep Sleep
RTC_DATA_ATTR int ciclos_sueno = 0;

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // Apagado

  // En Deep Sleep el inicio es inmediato tras despertar
  delay(1000);

  Serial.println();
  Serial.println("==================================================");
  Serial.println("   PRUEBA: CAD (LoRa SX1262) + DEEP SLEEP + BLE");
  Serial.println("   Laboratorio de Eficiencia Energetica - Unimayor");
  Serial.println("==================================================");

  ciclos_sueno++;
  Serial.printf("[SISTEMA] Despertar #%d de Deep Sleep completado.\n", ciclos_sueno);

  // 1. Inicializar bus SPI y transceptor LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  Serial.print("[HW LoRa] Configurando SX1262 a 915.0 MHz... ");
  int estado_lora = radio.begin(915.0, 125.0, 7, 5, 0x12, 14);

  if (estado_lora == RADIOLIB_ERR_NONE) {
    Serial.println("OK.");
  } else {
    Serial.printf("Error LoRa (%d)\n", estado_lora);
  }

  // 2. Ejecutar prueba CAD (Channel Activity Detection)
  // El SX1262 enciende el receptor apenas 2-3 milisegundos para oler el canal 915 MHz
  Serial.println("[CAD] Escaneando actividad en el canal de 915 MHz...");
  digitalWrite(LED_PIN, LOW); // Destello visual de actividad
  unsigned long t_cad_inicio = micros();
  int cad_resultado = radio.scanChannel();
  unsigned long t_cad_fin = micros();
  digitalWrite(LED_PIN, HIGH);

  Serial.printf("[CAD] Tiempo de escaneo en el aire: %lu microsegundos\n", (t_cad_fin - t_cad_inicio));

  if (cad_resultado == RADIOLIB_PREAMBLE_DETECTED) {
    Serial.println("[CAD] >>> ALERTA: Señal LoRa detectada en el canal <<<");
  } else if (cad_resultado == RADIOLIB_CHANNEL_FREE) {
    Serial.println("[CAD] Canal 915 MHz libre (sin señales de ataque ni transmision).");
  } else {
    Serial.printf("[CAD] Codigo de estado: %d\n", cad_resultado);
  }

  // 3. Poner el modem LoRa SX1262 en modo Sleep profundo (< 1 uA)
  Serial.println("[ENERGIA] Poniendo transceptor SX1262 en modo SLEEP (< 1 uA)...");
  radio.sleep();

  // 4. Rafaga breve de anuncio Bluetooth (Duty-Cycling: 3 segundos activo)
  Serial.println("[BLE] Emitiendo baliza publicitaria 'MOTO-DORMIDA' por 3 segundos...");
  BLEDevice::init("MOTO-DORMIDA");
  BLEServer *pServer = BLEDevice::createServer();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->start();

  // El LED parpadea indicando ventana activa de Bluetooth
  for (int i = 0; i < 6; i++) {
    digitalWrite(LED_PIN, LOW);
    delay(250);
    digitalWrite(LED_PIN, HIGH);
    delay(250);
  }

  pAdvertising->stop();
  BLEDevice::deinit(true); // Apagar radio Bluetooth completamente
  Serial.println("[BLE] Radio 2.4 GHz desenergizada.");

  // 5. Configurar despertar por temporizador (5 segundos) y entrar en Deep Sleep
  Serial.println("[ENERGIA] ESP32-S3 entrando en DEEP SLEEP por 5 segundos (~25 uA)...");
  Serial.println("--------------------------------------------------");
  Serial.flush(); // Esperar a que se vacie el buffer serie antes de apagar

  esp_sleep_enable_timer_wakeup(5ULL * 1000000ULL); // 5 segundos en microsegundos
  esp_deep_sleep_start();
}

void loop() {
  // En Deep Sleep el procesador nunca llega al loop
}
