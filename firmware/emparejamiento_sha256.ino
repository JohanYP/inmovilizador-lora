#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>
#include "mbedtls/sha256.h"

#define LED_PIN     LED_BUILTIN // GPIO 21
#define PIN_BOTON   1           // Pin D0 (GPIO 1) con jumper a GND para Factory Reset

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
Preferences prefs;

bool celularConectado = false;
bool dispositivoEmparejado = false;
String llaveCriptograficaNVS = "";
bool ignicionAutorizada = false;

// Funcion para calcular SHA-256 por hardware en el ESP32-S3
String generarSHA256(String semilla) {
  byte hash[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0); // 0 = SHA-256
  mbedtls_sha256_update(&ctx, (const unsigned char*)semilla.c_str(), semilla.length());
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);

  char hexString[65];
  for (int i = 0; i < 32; i++) {
    sprintf(hexString + (i * 2), "%02x", hash[i]);
  }
  hexString[64] = '\0';
  return String(hexString);
}

// Cargar o verificar estado de emparejamiento desde memoria Flash NVS
void cargarConfiguracionNVS() {
  prefs.begin("inmov-moto", false);
  dispositivoEmparejado = prefs.getBool("emparejado", false);
  llaveCriptograficaNVS = prefs.getString("llave_sha", "");
  prefs.end();

  Serial.println("\n[NVS] Leyendo memoria Flash permanente...");
  if (dispositivoEmparejado && llaveCriptograficaNVS.length() == 64) {
    Serial.println("[ESTADO] DISPOSITIVO YA EMPAREJADO.");
    Serial.print("[NVS] Llave SHA-256 almacenada: ");
    Serial.println(llaveCriptograficaNVS.substring(0, 16) + "... (64 chars)");
  } else {
    Serial.println("[ESTADO] MODO FABRICA: Esperando primera conexion para emparejamiento.");
    dispositivoEmparejado = false;
  }
}

// Restablecer de fabrica (borrar la llave de la memoria Flash)
void borrarEmparejamientoNVS() {
  prefs.begin("inmov-moto", false);
  prefs.clear();
  prefs.end();
  dispositivoEmparejado = false;
  llaveCriptograficaNVS = "";
  ignicionAutorizada = false;
  Serial.println("\n[RESET] Memoria Flash borrada. Dispositivo vuelto a estado de fabrica.");
}

class ServidorCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    celularConectado = true;
    Serial.println("\n[BLE] Celular conectado.");
    digitalWrite(LED_PIN, LOW); // LED encendido al conectar
  }

  void onDisconnect(BLEServer* pServer) {
    celularConectado = false;
    ignicionAutorizada = false;
    Serial.println("\n[BLE] Celular desconectado. Ignicion bloqueada.");
    digitalWrite(LED_PIN, HIGH);
    pServer->getAdvertising()->start();
  }
};

class CaracteristicaCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) {
    String payload = pChar->getValue();
    payload.trim();

    if (payload.length() == 0) return;

    Serial.print("\n[MENSAJE RECIBIDO]: ");
    Serial.println(payload);

    String respuesta = "";

    // =========================================================================
    // CASO 1: PRIMERA CONEXION (EMPAREJAMIENTO INICIAL)
    // =========================================================================
    if (!dispositivoEmparejado) {
      if (payload.startsWith("PAIR ")) {
        String datosSemilla = payload.substring(5);
        datosSemilla.trim();

        // 1. Calcular el hash SHA-256 con los datos recibidos (Fecha, Hora, Palabra secreta)
        String llaveGenerada = generarSHA256(datosSemilla);

        // 2. Guardar permanentemente en la memoria Flash (NVS) del ESP32
        prefs.begin("inmov-moto", false);
        prefs.putBool("emparejado", true);
        prefs.putString("llave_sha", llaveGenerada);
        prefs.end();

        dispositivoEmparejado = true;
        llaveCriptograficaNVS = llaveGenerada;
        ignicionAutorizada = true; // Queda autorizada en el emparejamiento inicial

        Serial.println("[CRIPTO] ¡EMPAREJAMIENTO EXITOSO!");
        Serial.print("[CRIPTO] Semilla recibida: ");
        Serial.println(datosSemilla);
        Serial.print("[CRIPTO] Hash SHA-256 generado: ");
        Serial.println(llaveGenerada);

        respuesta = "EMPAREJADO_OK: " + llaveGenerada;
      } else {
        respuesta = "DISPOSITIVO NO EMPAREJADO. Envie: PAIR <fecha-hora-palabra> (Ej: PAIR 2026-09-30-16:30-moto)";
      }
    } 
    // =========================================================================
    // CASO 2: DISPOSITIVO YA EMPAREJADO (SOLO ACEPTA LA LLAVE SHA-256 REGISTRADA)
    // =========================================================================
    else {
      if (payload.startsWith("AUTH ")) {
        String llaveRecibida = payload.substring(5);
        llaveRecibida.trim();

        // Comparacion criptografica en tiempo constante
        if (llaveRecibida.equalsIgnoreCase(llaveCriptograficaNVS)) {
          ignicionAutorizada = true;
          respuesta = "ACCESO_AUTORIZADO: Llave SHA-256 verificada. Moto encendida.";
          Serial.println("[SEGURIDAD] Autenticacion EXITOSA. Rele energizado.");
        } else {
          ignicionAutorizada = false;
          respuesta = "ACCESO_DENEGADO: Llave invalida. Intento no autorizado registrado.";
          Serial.println("[SEGURIDAD] ALERTA: Llave SHA-256 erronea.");
        }
      } 
      else if (payload.equalsIgnoreCase("STATUS")) {
        respuesta = "ESTADO: EMPAREJADO | IGNICION: " + String(ignicionAutorizada ? "ON" : "OFF");
      }
      else if (payload.equalsIgnoreCase("RESET")) {
        borrarEmparejamientoNVS();
        respuesta = "DISPOSITIVO REINICIADO A MODO FABRICA. Vuelva a emparejar con PAIR.";
      }
      else {
        respuesta = "DISPOSITIVO PROTEGIDO. Debe enviar: AUTH <su_llave_sha256>";
      }
    }

    // Notificar respuesta de vuelta al iPhone
    pChar->setValue(respuesta.c_str());
    pChar->notify();
  }
};

void setup() {
  setCpuFrequencyMhz(80);
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  pinMode(PIN_BOTON, INPUT_PULLUP);

  unsigned long t = millis();
  while (!Serial && (millis() - t < 2500));

  Serial.println();
  Serial.println("==================================================");
  Serial.println("   AUTENTICACION CRIPTOGRAFICA POR HARDWARE (SHA-256)");
  Serial.println("   Emparejamiento Inicial + Persistencia Flash NVS");
  Serial.println("==================================================");

  // Cargar configuracion persistente
  cargarConfiguracionNVS();

  // Iniciar BLE
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
  pAdvertising->setMinInterval(0x0640);
  pAdvertising->setMaxInterval(0x0C80);
  pAdvertising->start();

  Serial.println("[BLE] Baliza activa. Conectese desde nRF Connect.");
  Serial.println("==================================================");
}

void loop() {
  // Comprobar si se mantiene D0 a GND durante 3 segundos para Factory Reset fisico
  if (digitalRead(PIN_BOTON) == LOW) {
    delay(50);
    if (digitalRead(PIN_BOTON) == LOW) {
      Serial.println("[RESET FISICO] Boton D0 presionado. Mantenga presionado para borrar memoria...");
      unsigned long t0 = millis();
      while (digitalRead(PIN_BOTON) == LOW) {
        if (millis() - t0 > 3000) {
          borrarEmparejamientoNVS();
          for (int i = 0; i < 5; i++) {
            digitalWrite(LED_PIN, LOW);
            delay(100);
            digitalWrite(LED_PIN, HIGH);
            delay(100);
          }
          break;
        }
      }
    }
  }

  // Estado del LED:
  // Encendido fijo si esta autorizada la ignicion
  // Parpadeo rapido si esta en espera de primer emparejamiento
  // Parpadeo lento si esta emparejada pero bloqueada
  if (ignicionAutorizada) {
    digitalWrite(LED_PIN, LOW);
  } else if (!dispositivoEmparejado) {
    static unsigned long tFab = 0;
    if (millis() - tFab > 300) {
      tFab = millis();
      digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // Parpadeo continuo modo fabrica
    }
  } else {
    static unsigned long tVig = 0;
    if (millis() - tVig > 2000) {
      tVig = millis();
      digitalWrite(LED_PIN, LOW);
      delay(20);
      digitalWrite(LED_PIN, HIGH);
    }
  }

  delay(20);
}
