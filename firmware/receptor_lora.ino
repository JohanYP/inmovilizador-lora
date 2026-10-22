#include <SPI.h>
#include <RadioLib.h>
#include <Preferences.h>
#include "mbedtls/md.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Mapeo B2B oficial: XIAO ESP32-S3 + Wio-SX1262
#define LORA_SCK   7
#define LORA_MISO  8
#define LORA_MOSI  9
#define LORA_CS   41
#define LORA_DIO1 39
#define LORA_RST  42
#define LORA_BUSY 40

#define PIN_RELE_D0    1 // D0: Relé de ignición
#define PIN_RELE_D1    2 // D1: Respaldo relé
#define PIN_BOTON_WIO 21 // Botón usuario para emparejamiento / Reset PIN

// UUIDs oficiales de ESP32 Moto
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_STATUS_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHAR_LOGS_UUID      "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e"
#define CHAR_AUTH_UUID      "2a37f5a8-b641-479e-a81d-8f2c3b4e5a6f"

SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
Preferences prefs;

String pskDinamica = "UNIMAYOR_POPAYAN_2026_MOTO_KEY";
String phoneOwnerKey = "MOTO_OWNER_KEY_2026_POPAYAN";
String pinMaestro = "123456";
bool emparejado = false;

// Variables de Control Anti-Fuerza Bruta para el PIN
int fallosPinConsecutivos = 0;
unsigned long bloqueoPinHastaMs = 0;

// Variables BLE para el 3er Factor de Autenticación (3FA)
BLEServer* pServer = nullptr;
BLECharacteristic* pCharStatus = nullptr;
BLECharacteristic* pCharLogs = nullptr;
BLECharacteristic* pCharAuth = nullptr;

volatile bool bleFisicoConectado = false;
volatile bool celularAutorizado = false;
bool celularAutorizadoPrevio = false;
bool manualOverrideActivo = false;
bool manualRelayState = false;
bool resetPendienteConfirmacion = false;
unsigned long tiempoInicioResetPendienteMs = 0;
unsigned long tiempoConexionBLE = 0;
String bleNonceDesafio = "";

enum EstadoFSM {
  ESTADO_BLOQUEADO,
  ESTADO_AUTENTICANDO,
  ESTADO_AUTORIZADO,
  ESTADO_BLOQUEO_INTENTO_FALLIDO,
  ESTADO_MODO_EMPAREJAMIENTO
};

EstadoFSM estadoActual = ESTADO_BLOQUEADO;

int intentosFallidos = 0;
const int MAX_INTENTOS_PERMITIDOS = 3;
unsigned long inicioBloqueoMs = 0;
const unsigned long TIEMPO_PENALIZACION_MS = 30000;

uint32_t totalAutorizadosNVS = 0;
uint32_t totalFallidosNVS = 0;
uint32_t totalBloqueosFuerzaBrutaNVS = 0;

volatile bool paqueteRecibido = false;
void IRAM_ATTR alRecibirPaquete() {
  paqueteRecibido = true;
}

float rssiFiltrado = -65.0;
const float ALFA_EMA = 0.35;
const float UMBRAL_PROXIMIDAD_ENCENDER = -62.0;
const float UMBRAL_PROXIMIDAD_APAGAR   = -67.0;

unsigned long ultimoPaqueteValidoMs = 0;
String nonceActivo = "";
unsigned long tiempoNonceMs = 0;

bool modoMinimoConsumoMoto = false;
unsigned long inicioAusenciaMs = 0;

unsigned long tiempoPulsadoInicio = 0;
bool botonPresionadoPreviamente = false;
unsigned long inicioEmparejamientoMs = 0;
String llaveGeneradaTemp = "";

// Backoff Exponencial para fuerza bruta de PIN: hasta 24 horas (1 día completo)
unsigned long calcularEsperaPin(int fallos) {
  if (fallos == 1) return 5000;         // 5 segundos
  if (fallos == 2) return 30000;        // 30 segundos
  if (fallos == 3) return 300000;       // 5 minutos
  if (fallos == 4) return 1800000;      // 30 minutos
  if (fallos == 5) return 7200000;      // 2 horas
  return 86400000;                      // 24 horas (1 día)
}

float estimarDistanciaLora(float rssi) {
  if (rssi > -35.0) return 0.20;
  float d = pow(10.0, (-62.0 - rssi) / (10.0 * 2.2));
  if (d < 0.20) d = 0.20;
  if (d > 20.0) d = 20.0;
  return d;
}

String calcularHMACLocal(const String& key, const String& payload) {
  byte hmacResult[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char*)key.c_str(), key.length());
  mbedtls_md_hmac_update(&ctx, (const unsigned char*)payload.c_str(), payload.length());
  mbedtls_md_hmac_finish(&ctx, hmacResult);
  mbedtls_md_free(&ctx);

  char hexStr[17];
  for (int i = 0; i < 8; i++) {
    sprintf(hexStr + (i * 2), "%02x", hmacResult[i]);
  }
  hexStr[16] = '\0';
  return String(hexStr);
}

String generarNonce64() {
  uint32_t r1 = esp_random();
  uint32_t r2 = esp_random();
  char buf[17];
  sprintf(buf, "%08x%08x", r1, r2);
  return String(buf);
}

void enviarLogBLE(const String& mensaje) {
  Serial.println(mensaje);
  if (celularAutorizado && pCharLogs != nullptr) {
    pCharLogs->setValue(mensaje.c_str());
    pCharLogs->notify();
  }
}

void actualizarEstadoBLE() {
  if (pCharStatus != nullptr) {
    String authStr = celularAutorizado ? "DUENO_OK" : "NO_AUTORIZADO";
    String releStr = (digitalRead(PIN_RELE_D0) == HIGH) ? "ON" : "OFF";
    String zonaStr = (estadoActual == ESTADO_AUTORIZADO) ? "AUTORIZADO_2FA" : ((rssiFiltrado >= UMBRAL_PROXIMIDAD_ENCENDER) ? "CERCA" : "LEJANO");
    float distLora = estimarDistanciaLora(rssiFiltrado);
    String payload = "STATUS:" + releStr + ":" + zonaStr + ":" + String(rssiFiltrado, 1) + ":" + String(totalAutorizadosNVS) + ":" + authStr + ":" + String(distLora, 2);
    pCharStatus->setValue(payload.c_str());
    if (celularAutorizado) {
      pCharStatus->notify();
    }
  }
}

void fijarRele(bool activo) {
  digitalWrite(PIN_RELE_D0, activo ? HIGH : LOW);
  digitalWrite(PIN_RELE_D1, activo ? HIGH : LOW);
  actualizarEstadoBLE();
}

void guardarPhoneKey(const String& nuevaKey) {
  prefs.begin("moto_cfg", false);
  prefs.putString("phone_key", nuevaKey);
  prefs.end();
  phoneOwnerKey = nuevaKey;
}

void guardarPinMaestro(const String& nuevoPin) {
  prefs.begin("moto_cfg", false);
  prefs.putString("pin", nuevoPin);
  prefs.end();
  pinMaestro = nuevoPin;
}

// Restablecimiento de Fábrica por Hardware (Presionar botón 10 segundos)
void restablecerFabricaHardware() {
  Serial.println("\n==================================================");
  Serial.println("[RESET HARDWARE] >>> INICIANDO RESTABLECIMIENTO DE FÁBRICA <<<");
  Serial.println("[RESET HARDWARE] Botón físico presionado 10 segundos continuos.");

  // Parpadeo de confirmación en LED de ignición
  for (int i = 0; i < 5; i++) {
    digitalWrite(PIN_RELE_D0, HIGH);
    delay(100);
    digitalWrite(PIN_RELE_D0, LOW);
    delay(100);
  }

  // Una sola escritura en Flash NVS para cero desgaste
  prefs.begin("moto_cfg", false);
  pinMaestro = "123456";
  phoneOwnerKey = "MOTO_OWNER_KEY_2026_POPAYAN";
  prefs.putString("pin", pinMaestro);
  prefs.putString("phone_key", phoneOwnerKey);
  prefs.putUInt("bloqueos", 0);
  prefs.end();

  fallosPinConsecutivos = 0;
  bloqueoPinHastaMs = 0;
  celularAutorizado = false;
  manualOverrideActivo = false;

  Serial.println("[RESET HARDWARE] PIN restaurado a '123456'.");
  Serial.println("[RESET HARDWARE] Penalizaciones borradas. Sistema listo para nuevo aprovisionamiento.");
  Serial.println("==================================================");
}

// Callbacks del Servidor BLE
class ServidorBLECallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      bleFisicoConectado = true;
      celularAutorizado = false;
      tiempoConexionBLE = millis();
      bleNonceDesafio = generarNonce64();

      Serial.println("\n[BLE ENLACE] Dispositivo móvil conectado físicamente.");
      Serial.println("[BLE SEGURIDAD] Preparando desafío para el Propietario...");

      if (pCharAuth != nullptr) {
        String desafioPayload = "CHALLENGE:" + bleNonceDesafio;
        pCharAuth->setValue(desafioPayload.c_str());
      }
    }

    void onDisconnect(BLEServer* pServer) {
      bleFisicoConectado = false;
      celularAutorizado = false;
      Serial.println("\n[BLE ENLACE] Dispositivo móvil desconectado. Inmovilización 3FA activa.");
      pServer->startAdvertising();
    }
};

// Callback de Autenticación, Aprovisionamiento y Comandos Face ID
class AuthCallbacks: public BLECharacteristicCallbacks {
    void onRead(BLECharacteristic *pCharacteristic) {
      if (bleNonceDesafio.length() == 0) {
        bleNonceDesafio = generarNonce64();
      }
      String payload = "CHALLENGE:" + bleNonceDesafio;
      pCharacteristic->setValue(payload.c_str());
    }

    void onWrite(BLECharacteristic *pCharacteristic) {
      String rxValue = pCharacteristic->getValue().c_str();
      if (rxValue.length() > 0) {

        // CASO 1: Solicitud de Desafío al conectar
        if (rxValue == "REQ_CHALLENGE") {
          bleNonceDesafio = generarNonce64();
          String desafioPayload = "CHALLENGE:" + bleNonceDesafio;
          pCharacteristic->setValue(desafioPayload.c_str());
          pCharacteristic->notify();
          Serial.println("[BLE] Desafío enviado a la app.");
          return;
        }

        // CASO 2: APROVISIONAMIENTO CON PIN ("PROVISION:<pin>")
        if (rxValue.startsWith("PROVISION:")) {
          String pinIngresado = rxValue.substring(10);

          // Verificar si está en penalización por fuerza bruta
          if (millis() < bloqueoPinHastaMs) {
            unsigned long segsRestantes = (bloqueoPinHastaMs - millis()) / 1000;
            String msg = "PROV_BLOCKED:" + String(segsRestantes);
            pCharacteristic->setValue(msg.c_str());
            pCharacteristic->notify();
            Serial.printf("[FUERZA BRUTA BLE] Intento rechazado. Bloqueo activo por %lu s.\n", segsRestantes);
            return;
          }

          // Validación del PIN Maestro
          if (pinIngresado == pinMaestro) {
            fallosPinConsecutivos = 0;
            bloqueoPinHastaMs = 0;

            // Generar clave aleatoria única de dueño para este dispositivo
            String nuevaLlaveOwner = generarNonce64() + generarNonce64();
            guardarPhoneKey(nuevaLlaveOwner);

            celularAutorizado = true;
            String resp = "PROV_OK:" + nuevaLlaveOwner;
            pCharacteristic->setValue(resp.c_str());
            pCharacteristic->notify();

            enviarLogBLE("[APROVISIONAMIENTO] ¡PIN VÁLIDO! Celular vinculado como Propietario Exclusivo.");
            actualizarEstadoBLE();
            return;
          } else {
            // PIN INCORRECTO: Backoff Exponencial
            fallosPinConsecutivos++;
            unsigned long duracionBloqueo = calcularEsperaPin(fallosPinConsecutivos);
            bloqueoPinHastaMs = millis() + duracionBloqueo;
            unsigned long segsBloqueo = duracionBloqueo / 1000;

            String resp = "PROV_FAIL:" + String(fallosPinConsecutivos) + ":" + String(segsBloqueo);
            pCharacteristic->setValue(resp.c_str());
            pCharacteristic->notify();

            Serial.printf("[ALERTA PIN] PIN incorrecto (#%d). Penalización exponencial: %lu s (hasta 24h).\n",
                          fallosPinConsecutivos, segsBloqueo);
            return;
          }
        }

        // CASO 3: AUTENTICACIÓN ORDINARIA POR HMAC ("AUTH:<nonce>:<hmac>")
        if (rxValue.startsWith("AUTH:")) {
          int p1 = rxValue.indexOf(':');
          int p2 = rxValue.indexOf(':', p1 + 1);
          if (p2 > 0) {
            String nonceRecibido = rxValue.substring(p1 + 1, p2);
            String hmacRecibido = rxValue.substring(p2 + 1);

            if (nonceRecibido == bleNonceDesafio && bleNonceDesafio.length() > 0) {
              String hmacEsperado = calcularHMACLocal(phoneOwnerKey, nonceRecibido);
              if (hmacRecibido == hmacEsperado) {
                celularAutorizado = true;
                bleNonceDesafio = ""; // Consumido
                pCharacteristic->setValue("AUTH_OK:PROPIETARIO_CONFIRMADO");
                pCharacteristic->notify();
                enviarLogBLE("[3FA SEGURIDAD] Teléfono del Dueño autenticado por HMAC-SHA256 con éxito.");
                actualizarEstadoBLE();
                return;
              }
            }
          }

          Serial.println("[ALERTA SEGURIDAD BLE] Firma de dueño inválida.");
          pCharacteristic->setValue("AUTH_FAIL:ACCESO_DENEGADO");
          pCharacteristic->notify();
          celularAutorizado = false;
          return;
        }

        // CASO 4: COMANDOS BIOMÉTRICOS FACE ID ("CMD:<accion>")
        if (rxValue.startsWith("CMD:") && celularAutorizado) {
          String comando = rxValue.substring(4);
          if (comando == "IGNITION_OFF") {
            manualOverrideActivo = true;
            manualRelayState = false;
            fijarRele(false);
            enviarLogBLE("[FACE ID BIOMETRÍA] Motor apagado remotamente por el dueño con Face ID.");
            pCharacteristic->setValue("CMD_OK:OFF");
            pCharacteristic->notify();
            return;
          } else if (comando == "IGNITION_ON") {
            if (rssiFiltrado >= UMBRAL_PROXIMIDAD_ENCENDER && (millis() - ultimoPaqueteValidoMs < 8000)) {
              manualOverrideActivo = true;
              manualRelayState = true;
              estadoActual = ESTADO_AUTORIZADO;
              fijarRele(true);
              enviarLogBLE("[FACE ID BIOMETRÍA] Ignición encendida con Face ID (Llavero verificado en rango).");
              pCharacteristic->setValue("CMD_OK:ON");
              pCharacteristic->notify();
            } else {
              enviarLogBLE("[FACE ID RECHAZADO] No se puede encender: Llavero fuera de rango seguro.");
              pCharacteristic->setValue("CMD_ERR:LLAVERO_LEJOS");
              pCharacteristic->notify();
            }
            return;
          } else if (comando == "CONFIRM_RESET") {
            if (resetPendienteConfirmacion || celularAutorizado) {
              resetPendienteConfirmacion = false;
              restablecerFabricaHardware();
              pCharacteristic->setValue("CMD_OK:RESET_CONFIRMADO");
              pCharacteristic->notify();
              enviarLogBLE("[FACE ID EXITOSO] Restablecimiento de fábrica autorizado con Face ID.");
            }
            return;
          } else if (comando == "CANCEL_RESET") {
            resetPendienteConfirmacion = false;
            pCharacteristic->setValue("CMD_OK:RESET_CANCELADO");
            pCharacteristic->notify();
            enviarLogBLE("[SEGURIDAD] Restablecimiento de fábrica cancelado por el propietario.");
            return;
          } else if (comando == "AUTO") {
            manualOverrideActivo = false;
            enviarLogBLE("[CONTROL] Retornando a control automático por proximidad.");
            pCharacteristic->setValue("CMD_OK:AUTO");
            pCharacteristic->notify();
            return;
          }
        }
      }
    }
};

void iniciarBLE() {
  BLEDevice::init("ESP32 Moto");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServidorBLECallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pCharStatus = pService->createCharacteristic(
                      CHAR_STATUS_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharStatus->addDescriptor(new BLE2902());
  pCharStatus->setValue("STATUS:OFF:DESCONECTADO:0.0:0:NO_AUTH:0.0");

  pCharLogs = pService->createCharacteristic(
                      CHAR_LOGS_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharLogs->addDescriptor(new BLE2902());
  pCharLogs->setValue("[3FA] ESP32 Moto iniciado.");

  pCharAuth = pService->createCharacteristic(
                      CHAR_AUTH_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharAuth->addDescriptor(new BLE2902());
  pCharAuth->setCallbacks(new AuthCallbacks());

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("[3FA] BLE Servidor iniciado como 'ESP32 Moto'.");
}

void cargarNVS() {
  prefs.begin("moto_cfg", false);
  emparejado = prefs.getBool("paired", false);
  if (emparejado) {
    pskDinamica = prefs.getString("psk", pskDinamica);
    phoneOwnerKey = prefs.getString("phone_key", phoneOwnerKey);
    pinMaestro = prefs.getString("pin", pinMaestro);
    Serial.println("\n[NVS] Moto emparejada en Flash permanente.");
  } else {
    Serial.println("\n[NVS] Moto en estado de fabrica.");
  }
  totalAutorizadosNVS = prefs.getUInt("aut_ok", 0);
  totalFallidosNVS = prefs.getUInt("aut_err", 0);
  totalBloqueosFuerzaBrutaNVS = prefs.getUInt("bloqueos", 0);
  prefs.end();
}

void guardarEmparejamiento(const String& nuevaKey) {
  prefs.begin("moto_cfg", false);
  prefs.putBool("paired", true);
  prefs.putString("psk", nuevaKey);
  prefs.end();
  pskDinamica = nuevaKey;
  emparejado = true;
}

void registrarNVS(const char* clave, uint32_t valor) {
  prefs.begin("moto_cfg", false);
  prefs.putUInt(clave, valor);
  prefs.end();
}

void setup() {
  setCpuFrequencyMhz(80);

  pinMode(PIN_RELE_D0, OUTPUT);
  pinMode(PIN_RELE_D1, OUTPUT);
  pinMode(PIN_BOTON_WIO, INPUT_PULLUP);
  fijarRele(false);

  Serial.begin(115200);

  cargarNVS();
  iniciarBLE();

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);

  int estado = radio.begin(915.0, 125.0, 7, 5, 0x12, 0, 8, 1.8);
  if (estado != RADIOLIB_ERR_NONE) {
    while (true) {
      Serial.printf("[ERROR] Fallo hardware SX1262 (Codigo %d)\n", estado);
      delay(2000);
    }
  }

  radio.setDio2AsRfSwitch(true);
  radio.setPacketReceivedAction(alRecibirPaquete);
  radio.startReceive();

  Serial.println("\n==================================================");
  Serial.println("   MOTO INMOVILIZADOR - 3FA + PIN DE APROVISIONAMIENTO");
  Serial.println("   - PIN Maestro de Fábrica: 123456");
  Serial.println("   - Reseteo de PIN por Hardware: Botón Wio x 10s");
  Serial.println("   - Anti-Fuerza Bruta Exponencial: 5s -> 30s -> 5m -> 30m -> 2h -> 24h");
  Serial.println("==================================================");
}

void loop() {
  // Timeout para confirmación biométrica de reinicio (60 segundos)
  if (resetPendienteConfirmacion && (millis() - tiempoInicioResetPendienteMs > 60000)) {
    resetPendienteConfirmacion = false;
    enviarLogBLE("[SEGURIDAD] Solicitud de reinicio expiró sin confirmación de Face ID.");
    if (pCharAuth != nullptr) {
      pCharAuth->setValue("RESET_EXPIRED");
      pCharAuth->notify();
    }
  }

  // Transición de estado del celular del dueño
  if (celularAutorizado != celularAutorizadoPrevio) {
    celularAutorizadoPrevio = celularAutorizado;
    if (celularAutorizado) {
      enviarLogBLE("[3FA] Propietario enlazado vía Bluetooth. Moto autorizada para operar.");
    } else {
      enviarLogBLE("[3FA] Teléfono ausente o no autenticado. Inmovilización activa.");
      estadoActual = ESTADO_BLOQUEADO;
      fijarRele(false);
    }
  }

  // Detección de pulsación del botón Wio (Emparejamiento vs Restablecimiento de Fábrica)
  int lecturaBoton = digitalRead(PIN_BOTON_WIO);
  if (lecturaBoton == LOW) {
    if (!botonPresionadoPreviamente) {
      tiempoPulsadoInicio = millis();
      botonPresionadoPreviamente = true;
    } else {
      unsigned long duracion = millis() - tiempoPulsadoInicio;

      // Si se mantiene presionado >= 5 segundos: SOLICITAR RESTABLECIMIENTO CON FACE ID
      if (duracion >= 5000) {
        resetPendienteConfirmacion = true;
        tiempoInicioResetPendienteMs = millis();
        enviarLogBLE("[SEGURIDAD BIOMÉTRICA] Botón de la moto presionado. Solicitando autorización Face ID...");
        if (pCharAuth != nullptr) {
          pCharAuth->setValue("RESET_PENDING:CONFIRMAR_FACE_ID");
          pCharAuth->notify();
        }
        botonPresionadoPreviamente = false;
        tiempoPulsadoInicio = millis() + 5000;
      }
      // Si se mantiene entre 2 y 4 segundos: MODO EMPAREJAMIENTO LORA
      else if (duracion >= 2000 && estadoActual != ESTADO_MODO_EMPAREJAMIENTO && duracion < 5000) {
        estadoActual = ESTADO_MODO_EMPAREJAMIENTO;
        inicioEmparejamientoMs = millis();
        llaveGeneradaTemp = generarNonce64() + generarNonce64();
        fijarRele(false);
        enviarLogBLE("[EMPAREJAMIENTO] Modo emparejamiento LoRa activado en moto.");
      }
    }
  } else {
    botonPresionadoPreviamente = false;
  }

  // FLUJO 1: EMPAREJAMIENTO LORA
  if (estadoActual == ESTADO_MODO_EMPAREJAMIENTO) {
    digitalWrite(PIN_RELE_D0, (millis() % 400 < 200) ? HIGH : LOW);

    static unsigned long ultimoPairOffer = 0;
    if (millis() - ultimoPairOffer >= 700) {
      ultimoPairOffer = millis();
      String oferta = "PAIR_OFFER:" + llaveGeneradaTemp;
      radio.clearPacketReceivedAction();
      radio.transmit(oferta);
      radio.setPacketReceivedAction(alRecibirPaquete);
      radio.startReceive();
    }

    if (paqueteRecibido) {
      paqueteRecibido = false;
      String resp;
      if (radio.readData(resp) == RADIOLIB_ERR_NONE) {
        if (resp == ("PAIR_ACK:" + llaveGeneradaTemp)) {
          guardarEmparejamiento(llaveGeneradaTemp);
          estadoActual = ESTADO_BLOQUEADO;
          fijarRele(false);
          enviarLogBLE("[EMPAREJAMIENTO] Vinculación exitosa con llavero.");
        }
      }
      radio.startReceive();
    }

    if (millis() - inicioEmparejamientoMs > 30000) {
      estadoActual = ESTADO_BLOQUEADO;
      fijarRele(false);
      enviarLogBLE("[EMPAREJAMIENTO] Tiempo agotado. Regresando.");
    }
    return;
  }

  // FLUJO 2: PENALIZACION POR FUERZA BRUTA LORA
  if (estadoActual == ESTADO_BLOQUEO_INTENTO_FALLIDO) {
    fijarRele(false);
    digitalWrite(PIN_RELE_D0, (millis() % 200 < 100) ? HIGH : LOW);

    if (millis() - inicioBloqueoMs >= TIEMPO_PENALIZACION_MS) {
      estadoActual = ESTADO_BLOQUEADO;
      intentosFallidos = 0;
      fijarRele(false);
      enviarLogBLE("[SISTEMA] Penalización de 30s terminada. Retornando a BLOQUEADO.");
    }
    return;
  }

  // FLUJO 3: GESTION DE PAQUETES (CON REGLA ESTRICTA 3FA)
  if (paqueteRecibido) {
    paqueteRecibido = false;
    String tramaEntrante;
    int st = radio.readData(tramaEntrante);

    if (st == RADIOLIB_ERR_NONE) {
      float rssiRaw = radio.getRSSI();
      float snr = radio.getSNR();
      rssiFiltrado = (ALFA_EMA * rssiRaw) + ((1.0 - ALFA_EMA) * rssiFiltrado);

      // CASO ESPECIAL: Solicitud de Restablecimiento enviada desde el Llavero (Botón x 5s)
      if (tramaEntrante == "RESET_REQ") {
        resetPendienteConfirmacion = true;
        tiempoInicioResetPendienteMs = millis();
        enviarLogBLE("[SEGURIDAD BIOMÉTRICA] Botón del llavero presionado 5s. Solicitud de reinicio detectada.");
        if (pCharAuth != nullptr) {
          pCharAuth->setValue("RESET_PENDING:CONFIRMAR_FACE_ID");
          pCharAuth->notify();
        }
      }
      else if (tramaEntrante == "PING") {
        if (!celularAutorizado) {
          enviarLogBLE("[3FA BLOQUEO] PING de llavero detectado, pero CELULAR DEL DUEÑO NO AUTENTICADO. Silencio radial.");
        } else {
          nonceActivo = generarNonce64();
          tiempoNonceMs = millis();

          delay(10);
          radio.clearPacketReceivedAction();
          radio.transmit("CHALLENGE:" + nonceActivo);
          radio.setPacketReceivedAction(alRecibirPaquete);
        }
      }
      else if (tramaEntrante.startsWith("RESPONSE:")) {
        if (!celularAutorizado) {
          enviarLogBLE("[3FA BLOQUEO] Respuesta recibida, pero celular del dueño ausente. Relé en OFF.");
          fijarRele(false);
        } else {
          int p1 = tramaEntrante.indexOf(':');
          int p2 = tramaEntrante.indexOf(':', p1 + 1);

          if (p2 > 0) {
            String nonceRecibido = tramaEntrante.substring(p1 + 1, p2);
            String hmacRecibido = tramaEntrante.substring(p2 + 1);

            if (nonceRecibido == nonceActivo && nonceActivo.length() > 0 && (millis() - tiempoNonceMs < 300)) {
              String hmacEsperado = calcularHMACLocal(pskDinamica, nonceRecibido);

              if (hmacRecibido == hmacEsperado) {
                nonceActivo = ""; // Consumido

                if (modoMinimoConsumoMoto) {
                  modoMinimoConsumoMoto = false;
                  enviarLogBLE("[AHORRO ENERGÍA] Llavero detectado de nuevo. MOTO REACTIVADA.");
                }

                if (rssiFiltrado >= UMBRAL_PROXIMIDAD_ENCENDER) {
                  estadoActual = ESTADO_AUTORIZADO;
                  if (!manualOverrideActivo || manualRelayState) {
                    fijarRele(true);
                  }
                  ultimoPaqueteValidoMs = millis();
                  intentosFallidos = 0;
                  totalAutorizadosNVS++;

                  String logMsg = "[3FA AUTORIZADO] Nonce:" + nonceRecibido.substring(0, 8) + " | 3FA OK | Relé: ON";
                  enviarLogBLE(logMsg);
                }
                else if (rssiFiltrado < UMBRAL_PROXIMIDAD_APAGAR) {
                  if (estadoActual == ESTADO_AUTORIZADO) {
                    estadoActual = ESTADO_BLOQUEADO;
                    fijarRele(false);
                    inicioAusenciaMs = millis();
                    enviarLogBLE("[PROXIMIDAD] Llavero fuera de rango seguro -> Relé BLOQUEADO (LED OFF)");
                  }
                }
              } 
              else {
                nonceActivo = "";
                intentosFallidos++;
                totalFallidosNVS++;
                registrarNVS("aut_err", totalFallidosNVS);

                String errLog = "[ALERTA SEGURIDAD] ¡Firma Inválida en Respuesta! Fallo #" + String(intentosFallidos) + "/" + String(MAX_INTENTOS_PERMITIDOS);
                enviarLogBLE(errLog);

                if (intentosFallidos >= MAX_INTENTOS_PERMITIDOS) {
                  estadoActual = ESTADO_BLOQUEO_INTENTO_FALLIDO;
                  inicioBloqueoMs = millis();
                  totalBloqueosFuerzaBrutaNVS++;
                  registrarNVS("bloqueos", totalBloqueosFuerzaBrutaNVS);
                  enviarLogBLE("[SISTEMA CRÍTICO] >>> BLOQUEO TEMPORAL ACTIVADO POR 30 SEGUNDOS! <<<");
                }
              }
            } 
            else {
              enviarLogBLE("[ALERTA ANTI-REPLAY] Nonce no coincide o expiró. Paquete rechazado.");
            }
          }
        }
      }
    }
    radio.startReceive();
  }

  if (estadoActual == ESTADO_AUTORIZADO && (millis() - ultimoPaqueteValidoMs > 6000)) {
    estadoActual = ESTADO_BLOQUEADO;
    fijarRele(false);
    inicioAusenciaMs = millis();
    enviarLogBLE("[PROXIMIDAD] Ausencia de respuestas del llavero -> Relé BLOQUEADO (LED OFF)");
  }

  static unsigned long ultimoUpdateBLE = 0;
  if (millis() - ultimoUpdateBLE >= 1500) {
    ultimoUpdateBLE = millis();
    actualizarEstadoBLE();
  }
}
