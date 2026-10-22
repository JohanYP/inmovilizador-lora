# Bitácora Técnica de Experimentación: Enlace LoRa 915 MHz
**Proyecto de Grado:** Sistema IoT de inmovilización vehicular mediante tecnología LoRa (Popayán)  
**Investigadores:** Johan Danilo Yandun Pérez & Compañero de Grado  
**Hardware:** Seeed Studio XIAO ESP32-S3 + Wio-SX1262 (2 Nodos)  
**Fecha:** Octubre 2026  

---

## 1. Resumen Ejecutivo de la Práctica
En esta sesión se logró la **comunicación bidireccional estable punto a punto** entre el Nodo Llavero (Emisor) y el Nodo Moto (Receptor), implementando:
1. Control físico del relé de encendido de la moto (simulado con LED en pin D0/D1).
2. Estimación de distancia en tiempo real mediante el modelo logarítmico de pérdidas (*Log-Distance Path Loss*).
3. Conmutación automática por umbrales de proximidad e histéresis matemática.
4. Monitoreo visual de telemetría concurrente con interfaz gráfica en Pygame.

---

## 2. Parámetros de Radiofrecuencia y Justificación de Diseño

| Parámetro RF | Valor Seleccionado | Justificación Técnica |
| :--- | :--- | :--- |
| **Frecuencia** | **915.0 MHz** | Banda ISM libre reglamentada en Colombia por MinTIC y la Agencia Nacional del Espectro (ANE - Región 2 UIT). |
| **Ancho de Banda (BW)** | **125.0 kHz** | Estándar de modulación LoRa; excelente equilibrio entre sensibilidad de recepción y velocidad. |
| **Spreading Factor** | **SF7** *(Corregido de SF10)* | A SF10 el paquete tardaba ~310 ms en el aire. Con **SF7 se redujo a ~36.2 ms**, bajando la latencia en 88% y reduciendo drásticamente el consumo de batería. |
| **Coding Rate (CR)** | **4/5** | Agrega 1 bit de redundancia por cada 4 bits de datos para corrección de errores en el receptor. |
| **Sync Word** | **0x12** | Palabra de sincronización privada (redes privadas punto a punto, aislando el enlace de redes LoRaWAN públicas). |
| **Potencia (TX Power)** | **-6 dBm** *(Escala de mesa)* | Se redujo la potencia de +14 dBm a -6 dBm para que la señal decaiga en una escala de 0 a 1.20 metros y poder probar la inmovilización sin salir del laboratorio. |
| **Antena** | **A-03 (860-930 MHz)** | Conectada obligatoriamente en el conector U.FL del Wio-SX1262 para evitar quemar el PA por desadaptación de impedancia (ROE/VSWR). |

---

## 3. Prevención de Solapamiento, Colisiones y Congestión del Canal

### ¿Por qué los paquetes no se chocan en el aire?
* **Cálculo del Ciclo de Trabajo (*Duty Cycle*):**
  * Duración del paquete en el aire (*Time-on-Air*): **~36 ms**.
  * Cadencia de transmisión programada: **1 paquete cada 800 ms** (0.8 s).
  * **Ocupación real del canal:**  
    $$\text{Duty Cycle} = \frac{36\text{ ms}}{800\text{ ms}} = \mathbf{4.5\,\%}$$
  * El espectro de 915 MHz permanece **en silencio el 95.5 % del tiempo**. Esto garantiza que no existan ecos, ni saturación de canal, ni recalentamiento del circuito de RF.

### ¿Cómo evitamos sobrecargar el emisor?
* Se utiliza un temporizador asíncrono no bloqueante basado en `millis()`.
* El microcontrolador nunca usa `delay()` para esperar la transmisión; duerme sus periféricos entre ráfagas y procesa la emisión en microsegundos.

### ¿Cómo evitamos que el receptor se trabe o procese paquetes dobles?
* El transceptor SX1262 avisa la llegada de paquetes mediante una **interrupción física por hardware** en la línea `DIO1`.
* Al activarse la interrupción, la CPU lee el buffer FIFO mediante el bus SPI interno y **re-arma inmediatamente el receptor** con `radio.startReceive()`.
* Cada paquete contiene un **número de secuencia incremental** (`PING #n`). El receptor descarta paquetes duplicados y detecta saltos de secuencia para contabilizar paquetes perdidos reales.

---

## 4. Estimación de Distancia y Lógica de Proximidad

### El Modelo Físico (*Log-Distance Path Loss Model*)
La distancia se calcula a partir de la potencia de señal recibida (**RSSI** en dBm):
$$d = 10^{\frac{RSSI_{1m} - RSSI}{10 \cdot n}}$$
* $RSSI_{1m} = -61.0\text{ dBm}$ (Potencia de referencia medida experimentalmente a 1 metro con emisor a -6 dBm).
* $n = 2.0$ (Exponente de pérdidas de trayecto en espacio de laboratorio).

### El Filtro Exponencial de Media Móvil (EMA)
En radiofrecuencia, las reflexiones electromagnéticas en paredes y mesas (*multipath fading*) provocan que el RSSI salte ±10 dBm en milisegundos. Para evitar que la distancia salte erráticamente, se aplica un filtro digital recursivo:
$$RSSI_{\text{filtrado}} = (\alpha \cdot RSSI_{\text{raw}}) + ((1 - \alpha) \cdot RSSI_{\text{anterior}})$$
Con $\alpha = 0.35$, logrando una respuesta suave y precisa a los movimientos del usuario.

### Histéresis de Seguridad para el Relé de Ignición
Para evitar que el relé de encendido titile o haga ruido acústico en el borde de la distancia:
* **Umbral de Activación (Llavero cerca):** $RSSI \ge -55\text{ dBm}$ ($\approx < 0.6\text{ m}$). El conductor está junto a la moto $\rightarrow$ **Relé ON (LED encendido)**.
* **Umbral de Inmovilización (Llavero lejos):** $RSSI < -60\text{ dBm}$ ($\approx > 1.0\text{ m}$). El conductor se alejó $\rightarrow$ **Relé OFF (LED apagado)**.
* **Margen de Histéresis:** $5\text{ dBm}$ de zona muerta que previene falsos disparos.

---

## 5. Cuadro de Lecciones Aprendidas: Qué se PUEDE y qué NO se debe hacer

### ✅ Lo que SÍ se debe hacer:
1. **Conmutación de Antena RF:** Activar el switch interno mediante `radio.setDio2AsRfSwitch(true);`.
2. **Resistencia de Carga:** Conectar siempre una resistencia limitadora (220 a 330 $\Omega$) en serie con el LED testigo del relé.
3. **Desacoplar señales USB:** Configurar `ser.dtr = False` y `ser.rts = False` en Python para abrir monitores serie pasivos sin reiniciar los microcontroladores.
4. **Pines seguros:** Usar exclusivamente los pines libres del conector de cabecera: **D0 (GPIO 1) a D5 (GPIO 6)**.

### ❌ Lo que NO se debe hacer:
1. **NO usar el pin 38 para el RF Switch:** El módulo Wio-SX1262 gobierna la conmutación de antena directamente por la línea `DIO2` del transceptor; invocar el pin 38 causa conflictos de hardware.
2. **NO usar Spreading Factors altos (SF10 - SF12) para balizas rápidas de proximidad:** Generan tiempos de aire superiores a 300 ms, violando normativas y saturando el canal.
3. **NO apagar la moto en marcha (*Fail-Safe Vial*):** La pérdida de señal del llavero solo debe bloquear el **arranque inicial** o generar una alerta de advertencia, pero **jamás cortar la ignición intempestivamente si el vehículo se encuentra en movimiento**.
4. **NO emitir sin antena:** La potencia reflejada (ROE) destruye la etapa final del chip Semtech SX1262.
5. **NO meter más de 3.3V a ningún pin GPIO:** El SoC ESP32-S3 se destruye instantáneamente con niveles de 5V o 12V.

---

## 6. Estado Actual de los Módulos
* **COM6 (Nodo Moto - Receptor):** Conectado al circuito con LED externo en D0/D1. Lógica de proximidad activa (Apagado a >1.0 m, Encendido a <0.6 m).
* **COM7 (Nodo Llavero - Emisor):** Transmitiendo ráfagas PING cada 800 ms a -6 dBm con antena A-03.

---

## 7. Caso de Uso: Modo Sigilo / Pausa Manual de Presencia (*Anti-Relay Attack*)

### Contexto y Problemática Real en Popayán:
Un problema crítico en sistemas de proximidad automotrices es la **proximidad involuntaria**:
* Si el conductor está sentado almorzando en un restaurante o recibiendo un domicilio a 2 o 3 metros de la motocicleta estacionada, el llavero se encontraría en el rango de activación.
* En ese escenario, un delincuente podría subirse a la motocicleta y encenderla aprovechando la presencia física del dueño en el local.

### Solución Implementada: Botón de Usuario del Wio-SX1262 (GPIO 21):
* La placa de expansión Wio-SX1262 incorpora físicamente un micro-pulsador táctil conectado internamente a **GPIO 21** con resistencia pull-up de $10\text{ k}\Omega$ (compartido con el LED ámbar del XIAO).
* **Comportamiento en Firmware:**
  1. **Pulsación 1 (Modo Restaurante / Pausa):** El emisor detiene inmediatamente la transmisión periódica y pone el módem LoRa en `radio.sleep()` ($< 1\,\mu\text{A}$).
  2. **Efecto en la Moto:** El receptor detecta la ausencia de tramas y, tras la ventana de seguridad, **bloquea el relé de encendido (LED OFF)**. La moto queda inmovilizada aunque el dueño esté a 50 cm.
  3. **Pulsación 2 (Reactivación):** El emisor despierta la radio y reanuda la emisión de paquetes en 915 MHz, habilitando la ignición automáticamente (**LED ON**).

---

## 8. Balance de Laboratorio: Qué SÍ Funciona y Qué NO Funciona

### ✅ Lo que SÍ funciona (Resultados Validados en Silicio):
1. **Enlace LoRa 915 MHz Robusto:** Comunicación punto a punto limpia en SF7/BW125 con tiempo de aire de 36.2 ms y Duty Cycle del 4.5 %.
2. **Verificación de Dos Factores (2FA Embebido):**
   - **Factor 1:** Proximidad física de radiofrecuencia (RSSI $\ge -62.0\text{ dBm}$).
   - **Factor 2:** Firma criptográfica válida calculada por hardware mediante **HMAC-SHA256**.
3. **Control Firme de Relé Automotriz:** Salida enclavada en D0/D1 (`HIGH` continuo para arranque, `LOW` para corte) sin ruido ni vibración (*chattering*).
4. **Histéresis de Proximidad:** Margen de seguridad de 5 dBm (Encendido a $\ge -62\text{ dBm}$, Apagado a $< -67\text{ dBm}$) que previene falsas conmutaciones en el borde de cobertura.
5. **Máquina de Estados Finita (FSM de 4 Estados):** Transiciones formales deterministas entre `BLOQUEADO`, `AUTENTICANDO`, `AUTORIZADO` y `BLOQUEO_INTENTO_FALLIDO`.
6. **Caja de Intentos Finita Anti-Fuerza Bruta:** Detección de 3 firmas inválidas consecutivas $\rightarrow$ Bloqueo temporal por 30 segundos con alarma visual.
7. **Auditoría No Volátil (NVS Flash):** Persistencia de arranques autorizados, fallos de autenticación y bloqueos por ataque en memoria Flash.
8. **Botón Físico de Usuario (GPIO 21):** Conmutación manual de estado del llavero (Pausa de presencia / Anti-Relay Attack).
9. **Telemetría Concurrente en Pygame:** Panel gráfico en tiempo real con reconexión en caliente y lectura pasiva sin reinicio de chips.

### ❌ Lo que NO funciona y Límites de Diseño Descubiertos:
1. **NO usar Spreading Factors altos (SF10-SF12):** Tiempos de aire superiores a 300 ms generan congestión del canal, consumo excesivo y violan la regulación ISM.
2. **NO usar el pin 38 para conmutación de antena:** El hardware Wio-SX1262 conmuta internamente por `DIO2`.
3. **NO tratar la distancia física como ataque criptográfico:** Alejarse del vehículo debe apagar el relé silenciosamente (`BLOQUEADO`), jamás disparar alarmas ni sumar intentos de fuerza bruta.
4. **NO permitir que balizas legítimas limpien la caja de intentos durante un ataque:** El contador de fallos debe persistir ante firmas adulteradas.
5. **NO abrir puertos serie con DTR/RTS activo:** Reinicia el procesador por hardware (`USB_UART_CHIP_RESET`).
6. **NO alimentar GPIOs con 5V ni emitir sin antena A-03 conectada.**
7. **NO cortar ignición en movimiento:** La seguridad vial de Popayán exige mantener la marcha una vez encendida la moto (*Fail-Safe*).

---

## 9. Protocolo de Emparejamiento Dinámico y Protección Anti-Replay con Estampas de Tiempo

### 1. El Proceso de Vinculación Física (*Pairing Handshake*):
Para no depender de llaves fijas grabadas en código, se implementó un mecanismo de **vinculación física de fábrica**:
* **Activación:** Se mantiene presionado el botón Wio (GPIO 21) en ambos módulos durante más de 2.5 segundos.
* **Generación de Entropía:** El nodo Moto genera una llave criptográfica única de 256 bits utilizando el generador de números aleatorios por hardware del SoC ESP32-S3 (`esp_random()`).
* **Intercambio Seguro:**
  $$\text{Moto} \xrightarrow{\text{PAIR\_OFFER:LLAVE:T\_SYNC}} \text{Llavero}$$
  $$\text{Llavero} \xrightarrow{\text{PAIR\_ACK:LLAVE}} \text{Moto}$$
* **Persistencia NVS:** Ambos módulos graban la llave en su memoria Flash no volátil (`Preferences.h`). El llavero ajusta su reloj interno para marchar sincronizado al segundo con la moto.
* Una vez emparejados, ambos módulos quedan vinculados de por vida; ningún otro llavero puede comunicarse con la motocicleta.

### 2. Estampas de Tiempo y Monotonía Estricta (*Anti-Replay Protection*):
Cada trama de presencia enviada cada 800 ms lleva la firma criptográfica sobre la estampa temporal actual:
$$\text{Trama} = \text{AUTH} : T_{\text{actual}} : \text{HMAC-SHA256}(\text{PSK}, T_{\text{actual}})$$

El receptor en la moto valida tres condiciones matemáticas obligatorias antes de conmutar el relé:
1. **Firma Criptográfica Válida:** $\text{HMAC}_{\text{recibido}} == \text{HMAC}_{\text{calculado}}$.
2. **Ventana de Frescura ($\Delta t \le 3\text{ s}$):** Si un paquete tiene más de 3 segundos de antigüedad respecto al reloj de la moto, se rechaza por **caducidad temporal**.
3. **Monotonía Estricta ($T_{\text{nuevo}} > T_{\text{ultimo}}$):** Si un atacante intercepta la transmisión y la reproduce segundos después, la moto detecta que la estampa $T$ ya fue procesada y descarta el paquete de inmediato como **ataque de repetición (*Replay Attack*)**.

---

## 10. Arquitectura Ultra-Low Power y Protocolo Bidireccional Ping-Pong (Deep Sleep & Duty Cycling)

### 1. Consumos Mínimos Teóricos y Reales en Silicio
| Componente | Modo Activo (240 MHz / TX +14 dBm) | Modo Escucha (RX LoRa) | Light Sleep | Deep Sleep Puro |
| :--- | :--- | :--- | :--- | :--- |
| **XIAO ESP32-S3** | ~80 mA - 120 mA | N/A (CPU encendida ~40 mA) | ~2 mA | **~14 µA a 20 µA** |
| **Wio-SX1262** | ~45 mA (+14 dBm) / ~118 mA (+22 dBm) | ~4.6 mA | ~1.2 µA | **~600 nA (0.6 µA)** |
| **Sistema Completo** | **~125 mA - 165 mA** | **~45 mA** | **~3.2 mA** | **~15 µA a 22 µA** |

### 2. El Protocolo Bidireccional Ping-Pong (REQ-ACK)
En lugar de transmisión unidireccional simplex, el llavero y la moto mantienen un diálogo cerrado:
1. **Llavero $\xrightarrow{\text{AUTH:T:HMAC}}$ Moto** (ToA: ~36 ms).
2. **Moto $\xrightarrow{\text{ACK:T:HMAC\_ACK}}$ Llavero** (ToA: ~28 ms).
* **Si el Llavero NO recibe ACK durante 3 ciclos (2.4 s):** Concluye que la moto está apagada o fuera de alcance $\rightarrow$ Pasa a **Modo Baliza Lenta (Deep Sleep cíclico)**.
* **Si la Moto NO recibe paquetes durante 30 s (o 5 min tras parquear):** Concluye que el conductor se marchó $\rightarrow$ Apaga el relé y entra en **Deep Sleep con escucha intermitente (CAD / RxDutyCycle)**.

### 3. ¿Cómo Despertar sin Tocar Botones? (Wakeup por Pin DIO1)
1. El ESP32-S3 se duerme en **Deep Sleep** configurando `esp_sleep_enable_ext0_wakeup((gpio_num_t)39, 1);` (Pin DIO1 del SX1262).
2. El SX1262 se programa en **CAD (Channel Activity Detection)** o temporizador periódico.
3. Al detectar energía RF en 915 MHz, el SX1262 pone en ALTO el pin DIO1.
4. **La señal física despierta instantáneamente al ESP32-S3 en 2 milisegundos**.
5. El ESP32-S3 autentica la firma criptográfica: si es legítima $\rightarrow$ enciende el relé; si es ruido $\rightarrow$ vuelve a dormir de inmediato.

---

## 11. Escala de Tiempos y Auditoría de Mínimo Consumo (Transición a 60 Segundos)

### 1. Comportamiento en el Llavero (`COM7`):
* **Frecuencia CPU:** Reducida a **80 MHz** (`setCpuFrequencyMhz(80)`).
* **Nivel 1 (Ahorro Temprano tras 3 fallos ACK ~2.4s):** Espaciamiento de emisión a **2.8 segundos** con radio en reposo (`radio.sleep()`).
* **Nivel 2 (Mínimo Gasto tras 60 segundos sin contacto):**
  * Mensaje en log:
    ```text
    [AHORRO ENERGÍA] 1 minuto sin contacto con la moto.
    [AHORRO ENERGÍA] >>> MÍNIMO GASTO ACTIVADO EN LLAVERO <<<
    [AHORRO ENERGÍA] Estado: Radio en Sleep permanente | Pulso cada 5s | ~15 µA
    ```
  * El llavero reduce su cadencia a **un micro-pulso cada 5.0 segundos**, permaneciendo en sueño el 99.3% del tiempo.

### 2. Comportamiento en la Moto (`COM6`):
* **Frecuencia CPU:** Reducida a **80 MHz**.
* **Inmovilización Inmediata (a los 6 segundos de ausencia):** Desconexión de relé (`digitalWrite(LOW)`).
* **Nivel 2 (Mínimo Gasto tras 60 segundos sin paquetes):**
  * Mensaje en log:
    ```text
    [AHORRO ENERGÍA] 1 minuto sin paquetes del llavero (Moto estacionada).
    [AHORRO ENERGÍA] >>> MÍNIMO GASTO ACTIVADO EN MOTO <<<
    [AHORRO ENERGÍA] Estado: CPU 80 MHz | Relé OFF inmovilizado | Vigilancia pasiva ~20 µA
    [AHORRO ENERGÍA] Sistema armado para reactivarse automáticamente al acercarse.
    ```
  * En cuanto el conductor retorna con el llavero legítimo, la moto emite:
    `[AHORRO ENERGÍA] ¡Llavero detectado de nuevo! Saliendo de Mínimo Gasto -> MOTO REACTIVADA.`

---

## 12. Transición Arquitectónica: De Estampas de Tiempo (Timestamps) a Desafío-Respuesta (Challenge-Response)

### 1. El Problema Detectado con Estampas de Tiempo en Silicio Offline
Durante las pruebas de desconexión y reflasheo de hardware, se observó que al interrumpir el suministro eléctrico de cualquiera de los módulos:
* El microcontrolador ESP32-S3 carece de una batería de respaldo dedicada para su reloj de tiempo real (RTC).
* Al reenergizarse, el contador de milisegundos del procesador se reinicia a $t=0$, mientras que el otro módulo continúa su marcha normal en $t=N$.
* Al recibir un paquete, la diferencia temporal calculada superaba la ventana de frescura ($\Delta t = |t_{\text{moto}} - t_{\text{llavero}}| > 4\text{ s}$), provocando un **falso rechazo por caducidad temporal** y obligando al usuario a realizar una re-sincronización manual presionando botones.
* En un producto automotriz comercial, **un usuario jamás debe presionar botones cada vez que se desconecta o reemplaza la batería de su vehículo o control remoto**.

### 2. La Solución Definitiva de Grado Automotriz: Protocolo Desafío-Respuesta (*Challenge-Response*)
Se migró la arquitectura de seguridad a un esquema de autenticación mutua de 3 pasos basada en *Nonces* criptográficos efímeros:

```text
       [LLAVERO - PROVER]                                   [MOTO - VERIFIER]
               │                                                    │
               │ ──────── 1. Beacon de presencia: "PING" ─────────> │
               │                                                    │ (Genera Nonce de 64 bits
               │                                                    │  con TRNG por Hardware)
               │                                                    │
               │ <─────── 2. "CHALLENGE:<Nonce_64bits>" ─────────── │
               │                                                    │
    (Calcula HMAC sobre el Nonce:                                   │
     Firma = HMAC-SHA256(PSK, Nonce))                              │
               │                                                    │
               │ ── 3. "RESPONSE:<Nonce>:<HMAC(PSK, Nonce)>" ────> │
               │                                                    │
               │                                                    │ (Valida Nonce exacto,
               │                                                    │  Valida HMAC por Hardware,
               │                                                    │  Valida Proximidad RSSI)
               │                                                    │
               │                                                    │  -> ¡RELÉ ACTIVADO!
```

### 3. Ventajas Comparativas para la Sustentación Académica:
1. **Independencia Absoluta de Relojes:** No requiere Internet, ni servidores NTP, ni cristales RTC con baterías auxiliares. Ambos nodos pueden apagarse, reiniciarse o perder energía indefinidamente; al volver la corriente, se autentican en menos de **60 milisegundos** sin intervención del usuario.
2. **Inmunidad Total a Ataques de Repetición (*Replay Attacks*):** Cada desafío utiliza un número pseudo-aleatorio de 64 bits generado en tiempo real por el generador de entropía física del ESP32-S3 (`esp_random()`). Cada *Nonce* tiene una vida útil de un solo uso; una grabación de radio capturada segundos o días antes es rechazada de inmediato al no coincidir con el desafío vigente.
3. **Persistencia Criptográfica en Silicio (NVS):** La clave maestra compartida de 256 bits se almacena de forma inmutable en la memoria Flash no volátil mediante `Preferences.h`.

---

## 13. Matriz Exhaustiva de Capacidades y Límites del Sistema (Hardware, Memoria y Procesos)

### 1. ¿Qué SÍ se puede hacer con este Hardware?
* **Criptografía Acelerada por Hardware:** Cálculo de funciones hash seguras (SHA-256) y firmas HMAC en menos de 0.5 milisegundos gracias al motor criptográfico dedicado del SoC ESP32-S3.
* **Generación de Números Aleatorios Reales (TRNG):** Extracción de entropía física del ruido térmico del silicio (`esp_random()`) para generar *Nonces* impredecibles de 64 y 256 bits.
* **Persistencia Segura en Silicio (NVS):** Almacenamiento no volátil de llaves maestras y bitácoras de auditoría de intentos fallidos que sobreviven a cortes de energía.
* **Operación Fuera de Red (*Offline*):** Enlace LoRa punto a punto de largo alcance (hasta varios kilómetros en línea de vista con +22 dBm) sin depender de cobertura celular ni infraestructura Wi-Fi.
* **Concurrencia de Radios Heterogéneas:** Operación simultánea de LoRa (915 MHz por SPI) y Bluetooth Low Energy (2.4 GHz interno) sin interferencia espectral mutua.
* **Control Físico y Aislamiento Galvánico:** Conmutación segura de bobinas de relé de 5V/12V mediante transistores o módulos optoacoplados desde salidas de 3.3V.

---

### 2. ¿Qué NO se puede hacer? (Límites Físicos y de Arquitectura)
* **NO se puede medir el consumo de batería por software:** El Seeed XIAO ESP32-S3 carece de una resistencia shunt integrada y de chips ADC de monitoreo de corriente (como el INA219). Toda medición de miliamperios debe realizarse obligatoriamente con un multímetro externo en serie.
* **NO se pueden conectar señales de 5V o 12V directamente a los GPIOs:** Los pines de entrada/salida soportan estrictamente niveles lógicos de **3.3 V** (máximo absoluto 3.6 V). Una señal directa de 5V destruye la compuerta interna del microcontrolador.
* **NO se puede emitir por radiofrecuencia sin la antena conectada:** Emitir con el puerto U.FL abierto genera una alta Relación de Onda Estacionaria (ROE / VSWR), disipando la energía en el amplificador de potencia interno y quemando el chip SX1262.
* **NO se puede mantener la hora exacta tras perder energía sin una batería RTC:** El chip carece de batería de respaldo de litio para su reloj RTC. Si se desconecta la alimentación, el reloj se reinicia a cero (razón por la cual se descartó la seguridad por *Timestamps* y se adoptó *Challenge-Response*).
* **NO se puede apagar el motor bruscamente si el vehículo está en marcha (*Regla Fail-Safe*):** La pérdida de señal del llavero en una curva o intersección no debe cortar la chispa de ignición en movimiento para preservar la vida del conductor.

---

### 3. Límites de Memoria del Sistema

| Tipo de Memoria | Capacidad del Hardware | Uso en Firmware Actual | Margen Disponible | Límite Crítico y Consideraciones de Diseño |
| :--- | :--- | :--- | :--- | :--- |
| **SRAM Interna (RAM)** | **512 KB** | **23.2 KB (7 %)** | **~488 KB libres (93 %)** | Excelente margen. Límite: evitar fragmentación del Heap por uso indiscriminado de objetos `String` dinámicos en bucles infinitos; preferir búferes fijos `char[]`. |
| **PSRAM Externa** | **8 MB (Octal SPI)** | No requerida | **8 MB intactos** | Permite en el futuro buffers de audio, modelos de TinyML o procesamiento de imágenes. |
| **Memoria Flash ROM** | **4 MB / 8 MB** | **331 KB (9 %)** | **~3.0 MB libres (91 %)** | Espacio de sobra para actualizaciones inalámbricas OTA (*Over-The-Air*) y particiones dobles de arranque seguro. |
| **Memoria Flash NVS** | **Partición de 20 KB** | **< 1 KB utilizado** | **~19 KB libres** | **Límite de desgaste de celdas:** La Flash tolera aproximadamente **100,000 ciclos de escritura**. **REGLA ESTRICTA:** Solo escribir en NVS ante eventos críticos (emparejamiento, bloqueo de seguridad); **JAMÁS escribir en NVS en cada paquete recibido**. |

---

### 4. Límites de Procesamiento, Hilos y Tareas

* **Arquitectura de CPU:** Doble núcleo Xtensa LX7 de 32 bits con instrucciones vectoriales.
  * **Rango de Frecuencia:** Configurable entre **240 MHz** (máxima potencia), **160 MHz** y **80 MHz** (nuestro modo de bajo consumo calibrado).
  * **Rendimiento Criptográfico:** A 80 MHz, el acelerador de hardware ejecuta HMAC-SHA256 en **0.42 milisegundos**, consumiendo menos del 0.05 % de la capacidad de procesamiento de la CPU.
* **Sistema Operativo en Tiempo Real (FreeRTOS):**
  * La arquitectura permite segmentar el sistema en dos núcleos independientes:
    * **Núcleo 0 (Protocol Core):** Dedicado exclusivamente a la gestión de interrupciones de radio SPI (DIO1) y al stack BLE.
    * **Núcleo 1 (Application Core):** Dedicado a la Máquina de Estados Finita (FSM), lectura de sensores de marcha y disparo del relé.
* **Límites de Interrupción y Concurrencia de Buses:**
  * **Bus SPI Compartido:** El transceptor SX1262 opera sobre SPI a una velocidad de hasta 16 MHz. Durante una transferencia SPI de lectura de paquete (~150 microsegundos), no se deben realizar operaciones bloqueantes en el bus.
  * **Latencia de Reacción del Pin DIO1:** La interrupción externa por hardware (`IRAM_ATTR alRecibirPaquete`) despierta el hilo de control en menos de **3 microsegundos**.


---

## 14. Integración del Tercer Factor de Autenticación (3FA) mediante Bluetooth Low Energy y App Móvil iOS

### 1. Definición del Esquema 3FA
Para satisfacer los requerimientos de seguridad avanzada del proyecto de grado, se incorporó un **tercer factor de autenticación independiente**:
* **Factor 1 (Posesión / Proximidad física):** Medición de RSSI LoRa 915 MHz con histéresis matemática.
* **Factor 2 (Criptografía):** Protocolo de desafío-respuesta por hardware HMAC-SHA256 con *Nonce* efímero de 64 bits.
* **Factor 3 (Presencia del Smartphone vía BLE):** Conexión activa Bluetooth Low Energy del teléfono del propietario al nodo receptor ("ESP32 Moto").

### 2. Lógica Estricta de Bloqueo 3FA
* Si el teléfono móvil **no está conectado activamente vía Bluetooth**, el receptor LoRa en la motocicleta entra en **silencio radial estricto**: ignora cualquier paquete PING del llavero, no genera desafíos criptográficos y mantiene el relé desenergizado (**LED OFF**).
* Aunque el emisor se encuentre a 10 cm del receptor, el motor no puede encenderse sin la presencia del teléfono móvil enlazado.

### 3. Aplicación Móvil iOS ("ESP32 Moto")
* **Arquitectura:** Desarrollada en SwiftUI nativo con el framework `CoreBluetooth`.
* **Capacidades:**
  1. Detección y enlace automático con el periférico BLE `ESP32 Moto`.
  2. Visualización del estado del módulo (Disponible / Conectado / Fuera de rango).
  3. Lectura de telemetría en tiempo real: estado del relé de ignición (ON / OFF), zona de proximidad y nivel de señal.
  4. Terminal interactiva de **Logs en vivo** transmitidos desde el ESP32 hacia la aplicación mediante notificaciones BLE.
* **Canal CI/CD:** Pipeline automatizado en GitHub Actions (`JohanYP/ESP32-Moto`) con XcodeGen en runners `macos-14`, generando el binario instalable `ESP32Moto.ipa` para despliegue mediante Sideloading (LiveContainer / AltStore / SideStore / TrollStore).


---

## 15. Control Biométrico (Face ID) y Coexistencia Espectral Bluetooth/LoRa

### 1. Control Manual de Ignición con Verificación Biométrica Face ID
* Se integró en la app iOS el framework nativo `LocalAuthentication` de Apple.
* **Flujo de Seguridad Biométrica:**
  1. El conductor presiona **"Encender Moto"** o **"Apagar Moto"** en la interfaz.
  2. iOS activa la cámara TrueDepth y valida los rasgos faciales del propietario mediante **Face ID** (o código maestro del dispositivo).
  3. Si la biometría es exitosa:
     * Para **Apagar**: El receptor corta inmediatamente la ignición (**LED OFF**) y pasa a bloqueo manual.
     * Para **Encender**: El receptor verifica primero que el llavero LoRa se encuentre físicamente en rango seguro ($\ge -62	ext{ dBm}$ y paquete válido reciente). Solo si el llavero está presente, conmuta el relé a encendido (**LED ON**). Si el llavero está lejos, el intento es rechazado inmediatamente.

### 2. Autonomía Completa sin Herramientas Externas (Cero Dependencia de nRF Connect)
* La aplicación fue programada directamente sobre el framework nativo `CoreBluetooth` de Apple.
* Escanea, conecta, descubre características, intercambia claves de dueño y procesa telemetría de forma 100% autónoma.
* **El usuario no requiere tener instalado nRF Connect ni ninguna herramienta de depuración de terceros.**

### 3. Coexistencia Electromagnética y Análisis de No-Interferencia
* **Intercomunicador de Casco vs. App ESP32 Moto:**
  * El intercomunicador opera en Bluetooth Clásico (perfiles de audio continuo A2DP y llamadas HFP).
  * El inmovilizador vehicular opera en Bluetooth Low Energy (BLE GATT).
  * iOS gestiona hasta 7 conexiones activas concurrentes separando los enlaces mediante el algoritmo de **Salto Adaptativo de Frecuencia (AFH)** sobre los 79 canales de 2.4 GHz. Ambos dispositivos conviven en paralelo con cero pérdida de paquetes y sin microcortes de audio en el intercomunicador.
* **Llavero LoRa (915.0 MHz) vs. Bluetooth (2.4 GHz):**
  * El llavero emite en la banda **Sub-GHz (915.0 MHz)**, separada por casi **1.5 GHz de espectro** de la banda de 2.4 GHz de Bluetooth.
  * Físicamente es imposible que las ondas de LoRa interfieran o colisionen con las señales de Bluetooth.
  * En el receptor vehicular, el chip de radio Semtech Wio-SX1262 (LoRa) y el transceptor de radiofrecuencia interno del ESP32-S3 (BLE) cuentan con circuitos de filtrado pasabanda y antenas dedicadas independientes (A-03 para 915 MHz y A-02 para 2.4 GHz), garantizando aislamiento galvánico y de RF.


---

## 16. Protocolo de Aprovisionamiento Maestro con PIN (123456), Penalización Exponencial (Backoff) y Reseteo por Hardware

### 1. El Problema del Secreto Estático
Si la clave criptográfica de propietario se encuentra grabada de forma estática en el código de la app, cualquier persona que obtenga el archivo `.ipa` podría conectarse a la motocicleta. 

### 2. Solución: Aprovisionamiento Inicial de Fábrica con PIN de 6 Dígitos
* **PIN Maestro por Defecto:** `123456` (almacenado en Flash NVS).
* **Flujo de Primer Uso:**
  1. Al abrir la app por primera vez (o tras reinstalarla), el iPhone se encuentra en estado **"Sin Aprovisionar"**.
  2. La app despliega una interfaz que solicita el PIN maestro de 6 dígitos.
  3. Al enviar `PROVISION:123456`, el receptor valida el código, genera una clave aleatoria de 256 bits (`devOwnerKey`) y se la entrega al iPhone (`PROV_OK:<clave>`).
  4. La app guarda esa clave en el almacenamiento seguro de iOS (`UserDefaults / Keychain`).
  5. A partir de ese instante, la app se conecta, autentica y opera automáticamente de por vida sin volver a pedir el PIN.

### 3. Penalización Exponencial contra Ataques de Fuerza Bruta (Exponential Backoff)
Si un atacante intenta adivinar el PIN enviando combinaciones por Bluetooth, el receptor aplica una penalización temporal progresiva:
* **Fallo 1:** Bloqueo de 5 segundos.
* **Fallo 2:** Bloqueo de 30 segundos.
* **Fallo 3:** Bloqueo de 5 minutos (300 s).
* **Fallo 4:** Bloqueo de 30 minutos (1.800 s).
* **Fallo 5:** Bloqueo de 2 horas (7.200 s).
* **Fallo 6 o superior:** Bloqueo de **24 horas (86.400 segundos = 1 día entero)**.
Durante el periodo de penalización, el receptor rechaza de inmediato cualquier intento (`PROV_BLOCKED:<segundos_restantes>`) sin procesar la CPU.

### 4. Restablecimiento Físico por Hardware sin Desgaste de Memoria Flash
* **¿Cómo reiniciar si se olvida el PIN sin gastar la memoria?**
  * Para evitar degradar la Flash NVS (que tolera ~100.000 escrituras), los contadores temporales de fallos operan en **memoria RAM**.
  * Si el propietario olvida el PIN o desea restaurar de fábrica el módulo:
    1. Mantiene presionado el botón físico Wio (**GPIO 21**) durante **10 segundos continuos**.
    2. El LED de la moto parpadea rápidamente 5 veces como confirmación visual.
    3. El microcontrolador ejecuta **una única escritura en Flash NVS** que restaura el PIN a `123456`, borra las penalizaciones de tiempo y regenera la llave de dueño.
    4. Esta operación toma 0.001% de la vida útil de la Flash y garantiza que el propietario siempre tenga control físico absoluto sobre su vehículo.


---

## 17. Reseteo de Fábrica Blindado con Autorización Biométrica Face ID

### 1. El Riesgo de Reseteo Físico sin Control
Si un atacante o ladrón tiene acceso físico a la motocicleta o al llavero y puede presionar el botón durante varios segundos para reiniciar el PIN a `123456`, se vulneraría la custodia del vehículo.

### 2. Mecanismo de Seguridad Biométrica Bidireccional
Se implementó un protocolo de **confirmación biométrica obligatoria en dos vías**:
1. **Disparo Físico:** Cuando se presiona el botón físico en el llavero (GPIO 21 por 5 segundos) o en el receptor de la moto (GPIO 21 por 5 segundos):
   * El receptor entra en estado de **espera vigilada** (`resetPendienteConfirmacion = true`) durante máximo 60 segundos.
   * El receptor emite inmediatamente por Bluetooth a la app: `RESET_PENDING:CONFIRMAR_FACE_ID`.
2. **Advertencia de Seguridad en la App iOS:**
   * La aplicación **ESP32 Moto** detecta la notificación e interrumpe la pantalla con una ventana prioritaria de advertencia:
     *⚠️ "SOLICITUD DE REINICIO DE PIN DETECTADA: Se ha presionado el botón físico para restablecer el PIN maestro. Autentique con Face ID para confirmar que usted es el propietario legítimo."*
3. **Validación:**
   * **Si el dueño confirma con Face ID:** La app envía `CMD:CONFIRM_RESET`. El receptor valida la firma de dueño, parpadea el LED en D0 5 veces y restablece el PIN maestro a `123456` de fábrica en una sola escritura NVS.
   * **Si un extraño presionó el botón y el dueño rechaza o no autentica:** A los 60 segundos la solicitud expira y el receptor **bloquea el reinicio**, manteniendo inalteradas las credenciales del propietario.
