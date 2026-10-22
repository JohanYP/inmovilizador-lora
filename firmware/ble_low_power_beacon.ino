#include <RadioLib.h>
#include <SPI.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "esp_pm.h"

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

// UUIDs para el servicio de autenticacion vehicular
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool celularConectado = false;
unsigned long ultimoParpadeo = 0;

class ServidorCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    celularConectado = true;
    digitalWrite(LED_PIN, LOW); // Enciende fijo al conectar
    Serial.println("\n[BLE] ¡Celular CONECTADO en enlace estable!");
  }

  void onDisconnect(BLEServer* pServer) {
    celularConectado = false;
    digitalWrite(LED_PIN, HIGH);
    Serial.println("\n[BLE] Celular desconectado. Reanudando baliza continua...");
    pServer->getAdvertising()->start();
  }
};

void setup() {
  // 1. Optimizacion de reloj: Reducir frecuencia de CPU de 240 MHz a 80 MHz
  // Reduce el consumo del procesador en mas de un 60% sin afectar el rendimiento
  setCpuFrequencyMhz(80);

  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // Apagado

  unsigned long t = millis();
  while (!Serial && (millis() - t < 2500));

  Serial.println();
  Serial.println("==================================================");
  Serial.println("   MODO BAJO CONSUMO CONTINUO: BLE + LoRa SLEEP");
  Serial.println("   Baliza BLE Estable (Sin desconexiones)");
  Serial.println("==================================================");

  // 2. Dormir el chip LoRa SX1262 a ultra bajo consumo (< 1 uA)
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  Serial.print("[HW LoRa] Inicializando y durmiendo transceptor SX1262... ");
  int st = radio.begin(915.0, 125.0, 7, 5, 0x12, 14);
  if (st == RADIOLIB_ERR_NONE) {
    radio.sleep(); // Modem LoRa completamente apagado
    Serial.println("OK (< 1 uA).");
  } else {
    Serial.printf("Aviso LoRa: %d\n", st);
  }

  // 3. Inicializar Servidor Bluetooth Low Energy
  Serial.print("[BLE] Configurando baliza de bajo consumo permanente... ");
  BLEDevice::init("MOTO-INMOVILIZADOR");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServidorCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();

  // 4. EL SECRETO DE INGENIERIA: Advertising Interval optimizado
  // En lugar de reiniciar el microcontrolador, se ajusta el intervalo de emision:
  // 0x0640 = 1600 * 0.625 ms = 1000 ms (Emite un pulso cada 1 segundo)
  // Entre pulso y pulso la radio BLE duerme sola a nivel de hardware
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinInterval(0x0640); // 1.0 segundo
  pAdvertising->setMaxInterval(0x0C80); // 2.0 segundos
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  pAdvertising->start();

  Serial.println("ACTIVA.");
  Serial.println("[BLE] Dispositivo 'MOTO-INMOVILIZADOR' visible de forma permanente.");
  Serial.println("==================================================");
}

void loop() {
  // Mientras no este conectado, hace un pulso diminuto de LED cada 2 segundos
  if (!celularConectado) {
    if (millis() - ultimoParpadeo > 2000) {
      ultimoParpadeo = millis();
      digitalWrite(LED_PIN, LOW);
      delay(20); // Destello de apenas 20 ms
      digitalWrite(LED_PIN, HIGH);
    }
  } else {
    // Si el celular esta conectado, enviamos telemetria cada segundo
    static unsigned long ultimaTelemetria = 0;
    if (millis() - ultimaTelemetria > 1000) {
      ultimaTelemetria = millis();
      String datos = "ESTADO:VIGILANDO | BATT_SIM:12.6V | TS:" + String(millis());
      pCharacteristic->setValue(datos.c_str());
      pCharacteristic->notify();
    }
  }

  delay(50); // Mantiene el loop liviano
}
