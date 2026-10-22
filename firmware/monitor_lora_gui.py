import sys
import time
import threading
import re
import collections
import pygame
import serial
import serial.tools.list_ports

# ==============================================================================
# CONFIGURACION DE INTERFAZ Y COLORES
# ==============================================================================
ANCHO, ALTO = 1060, 680
FPS = 60

COLOR_BG         = (24, 24, 37)
COLOR_CARD       = (35, 36, 54)
COLOR_PANEL      = (30, 30, 46)
COLOR_BORDE      = (69, 71, 90)
COLOR_TEXT_PRIN  = (205, 214, 244)
COLOR_TEXT_DIM   = (166, 173, 200)
COLOR_CYAN       = (137, 220, 235)
COLOR_GREEN      = (166, 227, 161)
COLOR_RED        = (243, 139, 168)
COLOR_YELLOW     = (249, 226, 175)
COLOR_ACCENT     = (203, 166, 247)

class EstadoSistema:
    def __init__(self):
        self.lock = threading.Lock()
        self.emisor_online = False
        self.receptor_online = False
        
        self.rele_ignicion = False
        self.rssi = -100.0
        self.snr = 0.0
        self.distancia = 0.0
        self.zona = "DESCONOCIDO"
        self.prr = 0.0
        self.rx_count = 0
        self.lost_count = 0
        self.ultimo_rx_ts = 0
        
        # Nuevas metricas criptograficas y de tiempo
        self.modo_emparejamiento = False
        self.par_vinculado = True
        self.timestamp_actual = 0
        self.delta_tiempo = 0
        
        self.logs_emisor = collections.deque(maxlen=28)
        self.logs_receptor = collections.deque(maxlen=28)

estado = EstadoSistema()

def hilo_serial(port_name, es_emisor):
    while True:
        ser = None
        try:
            ser = serial.Serial()
            ser.port = port_name
            ser.baudrate = 115200
            ser.timeout = 1
            ser.dtr = False
            ser.rts = False
            ser.open()

            with estado.lock:
                if es_emisor:
                    estado.emisor_online = True
                    estado.logs_emisor.append((time.strftime("%H:%M:%S"), f"Conectado a {port_name}", COLOR_CYAN))
                else:
                    estado.receptor_online = True
                    estado.logs_receptor.append((time.strftime("%H:%M:%S"), f"Conectado a {port_name}", COLOR_GREEN))

            while True:
                linea = ser.readline().decode('utf-8', errors='ignore').strip()
                if linea:
                    ts = time.strftime("%H:%M:%S")
                    with estado.lock:
                        if es_emisor:
                            color_l = COLOR_YELLOW if "[SIMULADOR" in linea or "[BOTON" in linea else COLOR_TEXT_PRIN
                            if "[EMPAREJAMIENTO]" in linea:
                                color_l = COLOR_ACCENT
                                if "VINCULACION" in linea or "VINCULADO" in linea:
                                    estado.par_vinculado = True
                                    estado.modo_emparejamiento = False
                            estado.logs_emisor.append((ts, linea, color_l))
                        else:
                            if linea.startswith("[METRICAS]"):
                                partes = linea.replace("[METRICAS] ", "").split("|")
                                datos = {}
                                for p in partes:
                                    if ":" in p:
                                        k, v = p.split(":", 1)
                                        datos[k] = v
                                try:
                                    estado.rssi = float(datos.get("RSSI", estado.rssi))
                                    estado.snr = float(datos.get("SNR", estado.snr))
                                    estado.distancia = float(datos.get("DIST", estado.distancia))
                                    estado.zona = datos.get("ZONA", estado.zona)
                                    estado.prr = float(datos.get("PRR", estado.prr))
                                    estado.rele_ignicion = (datos.get("RELE") == "ON")
                                    estado.rx_count = int(datos.get("RX", estado.rx_count))
                                    estado.lost_count = int(datos.get("LOST", estado.lost_count))
                                    estado.ultimo_rx_ts = time.time()
                                except Exception:
                                    pass
                            else:
                                color_linea = COLOR_TEXT_PRIN
                                if "[ALERTA" in linea or "[SISTEMA CRITICO" in linea:
                                    color_linea = COLOR_RED
                                elif "[FSM: AUTORIZADO]" in linea:
                                    color_linea = COLOR_GREEN
                                    # Parsear timestamp si existe
                                    m_n = re.search(r'Nonce:([0-9a-fA-F]+)', linea)
                                    if m_n:
                                        estado.zona = "AUTORIZADO_2FA"
                                        estado.timestamp_actual = m_n.group(1)
                                elif "[EMPAREJAMIENTO]" in linea:
                                    color_linea = COLOR_ACCENT
                                    if "MODO EMPAREJAMIENTO" in linea:
                                        estado.modo_emparejamiento = True
                                    elif "COMPLETADA" in linea or "EXITO" in linea:
                                        estado.modo_emparejamiento = False
                                        estado.par_vinculado = True

                                estado.logs_receptor.append((ts, linea, color_linea))

        except (serial.SerialException, OSError):
            with estado.lock:
                if es_emisor:
                    estado.emisor_online = False
                    estado.logs_emisor.append((time.strftime("%H:%M:%S"), f"Desconectado ({port_name})", COLOR_RED))
                else:
                    estado.receptor_online = False
                    estado.logs_receptor.append((time.strftime("%H:%M:%S"), f"Desconectado ({port_name})", COLOR_RED))
            time.sleep(1.5)
        finally:
            if ser and ser.is_open:
                try:
                    ser.close()
                except Exception:
                    pass

def main():
    pygame.init()
    pygame.display.set_caption("Inmovilizador LoRa 915 MHz - Panel Criptográfico Oficial (Popayán)")
    pantalla = pygame.display.set_mode((ANCHO, ALTO))
    reloj = pygame.time.Clock()

    fuente_titulo = pygame.font.SysFont("Segoe UI, Arial", 18, bold=True)
    fuente_card_val = pygame.font.SysFont("Segoe UI, Arial", 24, bold=True)
    fuente_card_sub = pygame.font.SysFont("Segoe UI, Arial", 12)
    fuente_badge = pygame.font.SysFont("Segoe UI, Arial", 12, bold=True)
    fuente_log = pygame.font.SysFont("Consolas, Courier New", 12)

    t_receptor = threading.Thread(target=hilo_serial, args=("COM6", False), daemon=True)
    t_emisor = threading.Thread(target=hilo_serial, args=("COM7", True), daemon=True)
    t_receptor.start()
    t_emisor.start()

    corriendo = True
    while corriendo:
        reloj.tick(FPS)
        for evento in pygame.event.get():
            if evento.type == pygame.QUIT:
                corriendo = False
            elif evento.type == pygame.KEYDOWN and evento.key == pygame.K_ESCAPE:
                corriendo = False

        with estado.lock:
            em_on = estado.emisor_online
            rec_on = estado.receptor_online
            rele = estado.rele_ignicion
            rssi = estado.rssi
            snr = estado.snr
            dist = estado.distancia
            zona = estado.zona
            prr = estado.prr
            rx = estado.rx_count
            lost = estado.lost_count
            modo_pair = estado.modo_emparejamiento
            par_vinc = estado.par_vinculado
            t_act = estado.timestamp_actual
            delta_t = estado.delta_tiempo
            logs_em = list(estado.logs_emisor)
            logs_rec = list(estado.logs_receptor)

        pantalla.fill(COLOR_BG)

        # 1. HEADER CON BADGES DE ESTADO Y EMPAREJAMIENTO
        pygame.draw.rect(pantalla, COLOR_CARD, (0, 0, ANCHO, 55))
        pygame.draw.line(pantalla, COLOR_BORDE, (0, 55), (ANCHO, 55), 1)

        txt_tit = fuente_titulo.render("INMOVILIZADOR LoRa 915 MHz - 2FA + HMAC-SHA256", True, COLOR_TEXT_PRIN)
        pantalla.blit(txt_tit, (20, 16))

        # Badge Vinculacion Par
        color_vinc = COLOR_ACCENT if modo_pair else (COLOR_GREEN if par_vinc else COLOR_YELLOW)
        txt_vinc = "MODO VINCULACIÓN ACTIVO" if modo_pair else ("PAR VINCULADO (NVS)" if par_vinc else "SIN VINCULAR")
        surf_vinc = fuente_badge.render(txt_vinc, True, color_vinc)
        pygame.draw.rect(pantalla, COLOR_PANEL, (490, 14, 210, 28), border_radius=6)
        pygame.draw.rect(pantalla, color_vinc, (490, 14, 210, 28), width=1, border_radius=6)
        pantalla.blit(surf_vinc, (500, 20))

        # Badges COM
        color_rec_badge = COLOR_GREEN if rec_on else COLOR_RED
        surf_rec = fuente_badge.render("COM6: MOTO OK" if rec_on else "COM6: MOTO OFF", True, color_rec_badge)
        pygame.draw.rect(pantalla, COLOR_PANEL, (710, 14, 160, 28), border_radius=6)
        pygame.draw.rect(pantalla, color_rec_badge, (710, 14, 160, 28), width=1, border_radius=6)
        pantalla.blit(surf_rec, (720, 20))

        color_em_badge = COLOR_CYAN if em_on else COLOR_RED
        surf_em = fuente_badge.render("COM7: LLAVERO OK" if em_on else "COM7: LLAVERO OFF", True, color_em_badge)
        pygame.draw.rect(pantalla, COLOR_PANEL, (880, 14, 160, 28), border_radius=6)
        pygame.draw.rect(pantalla, color_em_badge, (880, 14, 160, 28), width=1, border_radius=6)
        pantalla.blit(surf_em, (890, 20))

        # 2. CUATRO TARJETAS DE METRICAS
        ancho_card = (ANCHO - 60) // 4
        y_card = 70
        h_card = 100

        # Tarjeta 1: Relé
        pygame.draw.rect(pantalla, COLOR_CARD, (20, y_card, ancho_card, h_card), border_radius=8)
        pygame.draw.rect(pantalla, COLOR_BORDE, (20, y_card, ancho_card, h_card), width=1, border_radius=8)
        pantalla.blit(fuente_card_sub.render("RELÉ IGNICIÓN (PIN D0)", True, COLOR_TEXT_DIM), (35, y_card + 12))
        color_rele = COLOR_GREEN if rele else COLOR_RED
        txt_rele = "ACTIVADO (ON)" if rele else "BLOQUEADO (OFF)"
        pantalla.blit(fuente_card_val.render(txt_rele, True, color_rele), (35, y_card + 35))
        pantalla.blit(fuente_card_sub.render("Ignición autorizada" if rele else "Motor inmovilizado", True, COLOR_TEXT_DIM), (35, y_card + 72))

        # Tarjeta 2: Proximidad
        x2 = 20 + ancho_card + 10
        pygame.draw.rect(pantalla, COLOR_CARD, (x2, y_card, ancho_card, h_card), border_radius=8)
        pygame.draw.rect(pantalla, COLOR_BORDE, (x2, y_card, ancho_card, h_card), width=1, border_radius=8)
        pantalla.blit(fuente_card_sub.render("DISTANCIA ESTIMADA", True, COLOR_TEXT_DIM), (x2 + 15, y_card + 12))
        txt_dist = f"~ {dist:.2f} m" if dist > 0 else "-- m"
        pantalla.blit(fuente_card_val.render(txt_dist, True, COLOR_CYAN), (x2 + 15, y_card + 35))
        color_zona = COLOR_GREEN if "CONTACTO" in zona or "AUTORIZADO" in zona else (COLOR_YELLOW if "PROXIMIDAD" in zona else COLOR_RED)
        pantalla.blit(fuente_card_sub.render(f"ZONA: {zona}", True, color_zona), (x2 + 15, y_card + 72))

        # Tarjeta 3: Senal RF
        x3 = x2 + ancho_card + 10
        pygame.draw.rect(pantalla, COLOR_CARD, (x3, y_card, ancho_card, h_card), border_radius=8)
        pygame.draw.rect(pantalla, COLOR_BORDE, (x3, y_card, ancho_card, h_card), width=1, border_radius=8)
        pantalla.blit(fuente_card_sub.render("POTENCIA RF (RSSI + SNR)", True, COLOR_TEXT_DIM), (x3 + 15, y_card + 12))
        txt_rssi = f"{rssi:.1f} dBm" if rssi > -119 else "-- dBm"
        pantalla.blit(fuente_card_val.render(txt_rssi, True, COLOR_TEXT_PRIN), (x3 + 15, y_card + 35))
        pantalla.blit(fuente_card_sub.render(f"SNR: {snr:+.2f} dB (Ruido)", True, COLOR_ACCENT), (x3 + 15, y_card + 72))

        # Tarjeta 4: Seguridad Temporal y Anti-Replay
        x4 = x3 + ancho_card + 10
        pygame.draw.rect(pantalla, COLOR_CARD, (x4, y_card, ancho_card, h_card), border_radius=8)
        pygame.draw.rect(pantalla, COLOR_BORDE, (x4, y_card, ancho_card, h_card), width=1, border_radius=8)
        pantalla.blit(fuente_card_sub.render("PROTOCOLO DESAFÍO-RESPUESTA", True, COLOR_TEXT_DIM), (x4 + 15, y_card + 12))
        txt_nonce = f"Nonce: {t_act}" if t_act else "Cripto Activo"
        pantalla.blit(fuente_card_val.render(txt_nonce, True, COLOR_GREEN), (x4 + 15, y_card + 35))
        pantalla.blit(fuente_card_sub.render("TRNG 64-bit | Inmune a cortes", True, COLOR_TEXT_DIM), (x4 + 15, y_card + 72))

        # 3. LOGS EN VIVO SPLIT SCREEN
        y_logs = 185
        h_logs = ALTO - y_logs - 20
        w_panel = (ANCHO - 50) // 2

        # Panel Izquierdo: RECEPTOR (MOTO - COM6)
        x_rec_p = 20
        pygame.draw.rect(pantalla, COLOR_PANEL, (x_rec_p, y_logs, w_panel, h_logs), border_radius=8)
        pygame.draw.rect(pantalla, COLOR_BORDE, (x_rec_p, y_logs, w_panel, h_logs), width=1, border_radius=8)
        pygame.draw.rect(pantalla, COLOR_CARD, (x_rec_p, y_logs, w_panel, 35), border_top_left_radius=8, border_top_right_radius=8)
        pantalla.blit(fuente_badge.render("LOGS MOTO - RECEPTOR CON LED (COM6)", True, COLOR_GREEN), (x_rec_p + 15, y_logs + 8))

        y_linea_r = y_logs + 45
        for ts, txt, col in logs_rec:
            texto_comp = f"[{ts}] {txt}"
            if len(texto_comp) > 62:
                texto_comp = texto_comp[:59] + "..."
            pantalla.blit(fuente_log.render(texto_comp, True, col), (x_rec_p + 12, y_linea_r))
            y_linea_r += 16
            if y_linea_r > y_logs + h_logs - 15:
                break

        # Panel Derecho: EMISOR (LLAVERO - COM7)
        x_em_p = x_rec_p + w_panel + 10
        pygame.draw.rect(pantalla, COLOR_PANEL, (x_em_p, y_logs, w_panel, h_logs), border_radius=8)
        pygame.draw.rect(pantalla, COLOR_BORDE, (x_em_p, y_logs, w_panel, h_logs), width=1, border_radius=8)
        pygame.draw.rect(pantalla, COLOR_CARD, (x_em_p, y_logs, w_panel, 35), border_top_left_radius=8, border_top_right_radius=8)
        pantalla.blit(fuente_badge.render("LOGS LLAVERO - EMISOR (COM7)", True, COLOR_CYAN), (x_em_p + 15, y_logs + 8))

        y_linea_e = y_logs + 45
        for ts, txt, col in logs_em:
            texto_comp = f"[{ts}] {txt}"
            if len(texto_comp) > 62:
                texto_comp = texto_comp[:59] + "..."
            pantalla.blit(fuente_log.render(texto_comp, True, col), (x_em_p + 12, y_linea_e))
            y_linea_e += 16
            if y_linea_e > y_logs + h_logs - 15:
                break

        pygame.display.flip()

    pygame.quit()
    sys.exit()

if __name__ == "__main__":
    main()
