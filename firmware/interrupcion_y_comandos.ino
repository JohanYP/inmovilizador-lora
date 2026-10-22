#include <RadioLib.h>
#include <SPI.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Mapeo B2B oficial: XIAO ESP32-S3 + Wio-SX1262
#define LORA_SCK    7
#define LORA_MISO   8
#define LORA_MOSI   9
#define LORA_CS    41
#define LORA_DIO1  39
#define LORA_RST   42
#define LORA_BUSY  40

#define LED_PIN     LED_BUILTIN // GPIO 21
#define PIN_BOTON   1           // Pin D0 (GPIO 1) para simular boton con jumper a GND

SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;

bool celularConectado = false;
bool modoEmisionActiva = true; // Controla si enviamos telemetria periódica
volatile bool botonPresionado = false;

// Clave secreta precompartida para autenticacion (simulacion 2FA)
const String CLAVE_AUTORIZADA = "MOTO2026";
bool sistemaAutorizado = false;

// Interrupcion por hardware (disparada al conectar D0 a GND)
void IRAM_ATTR manejarInterrupcionBoton() {
  botonPresionado = true;
}

class ServidorCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    celularConectado = true;
    Serial.println("\n[BLE] Celular conectado.");
    digitalWrite(LED_PIN, LOW); // LED encendido al conectar
  }

  void onDisconnect(BLEServer* pServer) {
    celularConectado = false;
    sistemaAutorizado = false; // Revocar autorizacion al desconectar
    Serial.println("\n[BLE] Celular desconectado. Esperando reconexion...");
    digitalWrite(LED_PIN, HIGH);
    pServer->getAdvertising()->start();
  }
};

// Callback cuando el iPhone ENVIA un comando o clave al ESP32
class CaracteristicaCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) {
    String comando = pChar->getValue();
    comando.trim();

    if (comando.length() == 0) return;

    Serial.print("[COMANDO RECIBIDO DESDE IPHONE]: ");
    Serial.println(comando);

    String respuesta = "";

    // 1. Comando STOP: Silencia la telemetria continua (Solo escucha)
    if (comando.equalsIgnoreCase("STOP")) {
      modoEmisionActiva = false;
      respuesta = "OK: MODO SILENCIOSO. Telemetria detenida. Solo escuchando comandos.";
      Serial.println("[ESTADO] Telemetria silenciada por comando del usuario.");
    }
    // 2. Comando START: Reanuda la emision
    else if (comando.equalsIgnoreCase("START")) {
      modoEmisionActiva = true;
      respuesta = "OK: MODO ACTIVO. Telemetria reanudada.";
      Serial.println("[ESTADO] Telemetria reanudada.");
    }
    // 3. Comando ESTADO
    else if (comando.equalsIgnoreCase("ESTADO")) {
      respuesta = "ESTADO: " + String(modoEmisionActiva ? "ACTIVO" : "SILENCIOSO") + 
                  " | ENCENDIDO: " + (sistemaAutorizado ? "AUTORIZADO" : "BLOQUEADO");
    }
    // 4. Comando de Autenticacion segura (Simulacion 2FA)
    else if (comando.startsWith("AUTH ")) {
      String intentoClave = comando.substring(5);
      intentoClave.trim();

      if (intentoClave == CLAVE_AUTORIZADA) {
        sistemaAutorizado = true;
        respuesta = "¡EXITO! Llave validada. IGNICION ACTIVADA (Moto lista para encender).";
        Serial.println("[SEGURIDAD] Autenticacion EXITOSA. Rele de ignicion habilitado.");
      } else {
        sistemaAutorizado = false;
        respuesta = "ERROR: Clave no autorizada. Intento registrado en auditoria.";
        Serial.println("[SEGURIDAD] ALERTA: Intento de autenticacion fallido.");
      }
    }
    // 5. Comando no reconocido
    else {
      respuesta = "COMANDO INVALIDO. Opciones: STOP, START, ESTADO, AUTH <clave>";
    }

    // Enviar respuesta al celular
    pChar->setValue(respuesta.c_str());
    pChar->notify();
  }
};

void setup() {
  setCpuFrequencyMhz(80); // Frecuencia eficiente

  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  // Configurar pin D0 (GPIO 1) con resistencia PULL-UP interna para interrupcion
  pinMode(PIN_BOTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BOTON), manejarInterrupcionBoton, FALLING);

  unsigned long t = millis();
  while (!Serial && (millis() - t < 2500));

  Serial.println();
  Serial.println("==================================================");
  Serial.println("   SISTEMA DE CONTROL POR INTERRUPCIONES Y COMANDOS");
  Serial.println("   - Comandos BLE: STOP, START, ESTADO, AUTH <clave>");
  Serial.println("   - Interrupcion fisica: Conecte D0 a GND");
  Serial.println("==================================================");

  // Inicializar y dormir LoRa (< 1 uA)
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  if (radio.begin(915.0, 125.0, 7, 5, 0x12, 14) == RADIOLIB_ERR_NONE) {
    radio.sleep();
  }

  // Inicializar BLE
  BLEDevice::init("MOTO-INMOVILIZADOR");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServidorCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new CaracteristicaCallbacks());
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinInterval(0x0640); // 1.0 s
  pAdvertising->setMaxInterval(0x0C80); // 2.0 s
  pAdvertising->start();

  Serial.println("[LISTO] Esperando conexion de nRF Connect o interrupcion en D0.");
}

void loop() {
  // 1. Atender interrupcion fisica de hardware (Boton o jumper en D0 a GND)
  if (botonPresionado) {
    botonPresionado = false;
    delay(50); // Antirrebote simple
    if (digitalRead(PIN_BOTON) == LOW) {
      modoEmisionActiva = !modoEmisionActiva;
      Serial.print("\n[INTERRUPCION FISICA] Boton accionado. Modo emision ahora es: ");
      Serial.println(modoEmisionActiva ? "ACTIVO" : "SILENCIOSO (STOP)");

      if (celularConectado) {
        String msg = "[FISICO] Estado conmutado a: " + String(modoEmisionActiva ? "ACTIVO" : "SILENCIOSO");
        pCharacteristic->setValue(msg.c_str());
        pCharacteristic->notify();
      }
    }
  }

  // 2. Si el modo de emision esta activo y hay celular conectado, enviamos telemetria
  static unsigned long ultimaTelemetria = 0;
  if (celularConectado && modoEmisionActiva) {
    if (millis() - ultimaTelemetria > 1500) {
      ultimaTelemetria = millis();
      String telemetria = "VIGILANDO | IGNICION:" + String(sistemaAutorizado ? "ON" : "OFF") + 
                          " | UPTIME:" + String(millis() / 1000) + "s";
      pCharacteristic->setValue(telemetria.c_str());
      pCharacteristic->notify();
    }
  }

  // Indicador de estado en LED
  if (sistemaAutorizado) {
    digitalWrite(LED_PIN, LOW); // LED encendido fijo si esta autorizada la moto
  } else if (!celularConectado) {
    // Parpadeo tenue de reposo
    static unsigned long tLed = 0;
    if (millis() - tLed > 2000) {
      tLed = millis();
      digitalWrite(LED_PIN, LOW);
      delay(15);
      digitalWrite(LED_PIN, HIGH);
    }
  }

  delay(20);
}
