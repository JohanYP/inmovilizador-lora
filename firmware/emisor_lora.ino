#include <SPI.h>
#include <RadioLib.h>
#include <Preferences.h>
#include "mbedtls/md.h"

// Mapeo B2B oficial: XIAO ESP32-S3 + Wio-SX1262
#define LORA_SCK   7
#define LORA_MISO  8
#define LORA_MOSI  9
#define LORA_CS   41
#define LORA_DIO1 39
#define LORA_RST  42
#define LORA_BUSY 40

#define PIN_BOTON_WIO  21

SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
Preferences prefs;

String pskDinamica = "UNIMAYOR_POPAYAN_2026_MOTO_KEY";
bool emparejado = false;

bool transmisionActiva = true;
bool modoEmparejamiento = false;
unsigned long inicioModoEmparejamiento = 0;

bool simularAtaqueActivo = false;
int fallosConsecutivos = 0;
bool modoMinimoConsumoLlavero = false;
unsigned long inicioDesconexionMs = 0;

unsigned long tiempoPulsadoInicio = 0;
bool botonPresionadoPreviamente = false;
int conteoClics = 0;
unsigned long ultimoClicMs = 0;

volatile bool paqueteRecibido = false;
void IRAM_ATTR alRecibirPaquete() {
  paqueteRecibido = true;
}

String calcularHMAC(const String& key, const String& payload) {
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

void cargarConfiguracion() {
  prefs.begin("llave_cfg", false);
  emparejado = prefs.getBool("paired", false);
  if (emparejado) {
    pskDinamica = prefs.getString("psk", pskDinamica);
    Serial.println("\n[NVS] Llavero emparejado en Flash permanente.");
    Serial.print("[NVS] PSK activa: ");
    Serial.println(pskDinamica.substring(0, 12) + "...");
  } else {
    Serial.println("\n[NVS] Llavero en modo de fabrica.");
  }
  prefs.end();
}

void guardarEmparejamiento(const String& nuevaKey) {
  prefs.begin("llave_cfg", false);
  prefs.putBool("paired", true);
  prefs.putString("psk", nuevaKey);
  prefs.end();
  pskDinamica = nuevaKey;
  emparejado = true;
}

void setup() {
  setCpuFrequencyMhz(80);

  Serial.begin(115200);
  pinMode(PIN_BOTON_WIO, INPUT_PULLUP);

  cargarConfiguracion();

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);

  int estado = radio.begin(915.0, 125.0, 7, 5, 0x12, 0, 8, 1.8);
  if (estado != RADIOLIB_ERR_NONE) {
    while (true) {
      Serial.printf("[ERROR] Fallo hardware SX1262 (Codigo %d)\n", estado);
      delay(2000);
    }
  }

  radio.setDio2AsRfSwitch(true);
  radio.sleep();

  Serial.println("\n==================================================");
  Serial.println("   LLAVERO CON PROTOCOLO DESAFÍO-RESPUESTA (COM7)");
  Serial.println("   - Frecuencia CPU: 80 MHz (Bajo Consumo)");
  Serial.println("   - Flujo: PING -> DESAFÍO -> RESPUESTA FIRMADA");
  Serial.println("   - 1 Clic: Encender/Apagar | 2 Clics: Simular Ataque");
  Serial.println("==================================================");
}

void loop() {
  int lectura = digitalRead(PIN_BOTON_WIO);

  if (lectura == LOW) {
    if (!botonPresionadoPreviamente) {
      tiempoPulsadoInicio = millis();
      botonPresionadoPreviamente = true;
    } else {
      unsigned long duracion = millis() - tiempoPulsadoInicio;
      // Pulsación larga de 5 segundos en llavero: Solicitud de Restablecimiento con Face ID
      if (duracion >= 5000) {
        Serial.println("\n[LLAVERO] >>> BOTÓN PRESIONADO 5s: SOLICITANDO RESTABLECIMIENTO BIOMÉTRICO A LA MOTO <<<");
        radio.clearPacketReceivedAction();
        radio.transmit("RESET_REQ");
        radio.sleep();
        botonPresionadoPreviamente = false;
        tiempoPulsadoInicio = millis() + 4000; // Evitar disparos repetidos
        conteoClics = 0;
      }
      // Pulsación de 2 a 4 segundos: Modo Emparejamiento
      else if (duracion >= 2000 && !modoEmparejamiento && duracion < 4500) {
        modoEmparejamiento = true;
        inicioModoEmparejamiento = millis();
        paqueteRecibido = false;
        radio.setPacketReceivedAction(alRecibirPaquete);
        radio.startReceive();
        Serial.println("\n[EMPAREJAMIENTO] >>> MODO EMPAREJAMIENTO ACTIVADO EN LLAVERO <<<");
        conteoClics = 0;
      }
    }
  } else {
    if (botonPresionadoPreviamente) {
      unsigned long duracionPulsacion = millis() - tiempoPulsadoInicio;
      botonPresionadoPreviamente = false;
      if (duracionPulsacion < 1000 && !modoEmparejamiento) {
        conteoClics++;
        ultimoClicMs = millis();
      }
    }
  }

  // FLUJO 1: EMPAREJAMIENTO
  if (modoEmparejamiento) {
    if (paqueteRecibido) {
      paqueteRecibido = false;
      String paqueteEntrante;
      if (radio.readData(paqueteEntrante) == RADIOLIB_ERR_NONE) {
        if (paqueteEntrante.startsWith("PAIR_OFFER:")) {
          int p1 = paqueteEntrante.indexOf(':');
          if (p1 > 0) {
            String llaveOferta = paqueteEntrante.substring(p1 + 1);
            guardarEmparejamiento(llaveOferta);

            delay(40);
            radio.clearPacketReceivedAction();
            radio.transmit("PAIR_ACK:" + llaveOferta);
            radio.sleep();

            Serial.println("\n[EMPAREJAMIENTO] ¡VINCULACIÓN EXITOSA CON LA MOTO!");
            Serial.print("[EMPAREJAMIENTO] Llave maestra guardada en Flash NVS: ");
            Serial.println(llaveOferta);

            modoEmparejamiento = false;
            transmisionActiva = true;
          }
        }
      }
      if (modoEmparejamiento) radio.startReceive();
    }

    if (millis() - inicioModoEmparejamiento > 25000) {
      modoEmparejamiento = false;
      radio.sleep();
      Serial.println("\n[EMPAREJAMIENTO] Tiempo agotado. Regresando.");
    }
    return;
  }

  // FLUJO 2: CONTROL DE CLICS (Mute vs Ataque)
  if (conteoClics > 0 && (millis() - ultimoClicMs > 350)) {
    if (conteoClics == 1) {
      transmisionActiva = !transmisionActiva;
      if (transmisionActiva) {
        Serial.println("\n[BOTON] >>> LLAVERO ENCENDIDO <<<");
      } else {
        radio.sleep();
        Serial.println("\n[BOTON] >>> LLAVERO EN MODO SILENCIOSO / APAGADO <<<");
      }
    } 
    else if (conteoClics >= 2) {
      simularAtaqueActivo = true;
      Serial.println("\n[SIMULADOR ATAQUE] >>> ARMADO: El próximo desafío responderá FIRMA FALSA <<<");
    }
    conteoClics = 0;
  }

  // FLUJO 3: CICLO SYNCHRONOUS PING-CHALLENGE-RESPONSE
  static unsigned long ultimoCicloMs = 0;
  unsigned long cadencia = modoMinimoConsumoLlavero ? 4000 : (fallosConsecutivos >= 3 ? 2000 : 800);

  if (transmisionActiva && (millis() - ultimoCicloMs >= cadencia)) {
    ultimoCicloMs = millis();
    unsigned long tInicio = millis();

    // 1. Emitir PING solicitando presencia a la moto
    radio.clearPacketReceivedAction();
    radio.transmit("PING");

    // 2. Escuchar DESAFÍO durante hasta 90 ms
    paqueteRecibido = false;
    radio.setPacketReceivedAction(alRecibirPaquete);
    radio.startReceive();

    unsigned long tEspera = millis();
    bool desafioProcesado = false;

    while (millis() - tEspera < 90) {
      if (paqueteRecibido) {
        paqueteRecibido = false;
        String trama;
        if (radio.readData(trama) == RADIOLIB_ERR_NONE) {
          if (trama.startsWith("CHALLENGE:")) {
            String nonce = trama.substring(10);
            String hmac = "";

            if (simularAtaqueActivo) {
              hmac = "FIRMA_FALSA_9999";
              simularAtaqueActivo = false;
              Serial.println("\n[SIMULADOR ATAQUE] Desafío recibido -> Respondiendo con FIRMA ADULTERADA");
            } else {
              hmac = calcularHMAC(pskDinamica, nonce);
            }

            // 3. Responder con la firma calculada sobre el nonce recibido
            delay(15);
            radio.clearPacketReceivedAction();
            radio.transmit("RESPONSE:" + nonce + ":" + hmac);
            
            unsigned long rttTotal = millis() - tInicio;
            desafioProcesado = true;
            fallosConsecutivos = 0;

            if (modoMinimoConsumoLlavero) {
              modoMinimoConsumoLlavero = false;
              Serial.println("\n[AHORRO ENERGÍA] ¡Moto detectada de nuevo! Saliendo de Mínimo Consumo.");
            }

            Serial.printf("[DESAFÍO-RESPUESTA] Nonce:%s | Firma enviada | RTT: %lu ms\n",
                          nonce.substring(0, 8).c_str(), rttTotal);
            break;
          }
        }
      }
      delay(2);
    }

    // 4. Inmediatamente poner la radio a dormir para ultra-bajo consumo (0.6 uA)
    radio.clearPacketReceivedAction();
    radio.sleep();

    // 5. Gestion de ausencia de moto
    if (!desafioProcesado) {
      fallosConsecutivos++;
      if (fallosConsecutivos == 1) {
        inicioDesconexionMs = millis();
      }
      if (fallosConsecutivos >= 3 && (millis() - inicioDesconexionMs >= 60000) && !modoMinimoConsumoLlavero) {
        modoMinimoConsumoLlavero = true;
        Serial.println("\n==================================================");
        Serial.println("[AHORRO ENERGÍA] 1 minuto sin recibir desafíos de la moto.");
        Serial.println("[AHORRO ENERGÍA] >>> MÍNIMO GASTO ACTIVADO EN LLAVERO (Ciclo 4s / ~15 uA) <<<");
        Serial.println("==================================================");
      }
    }
  }
}
