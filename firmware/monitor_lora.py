import sys
import time
import threading
import serial
import serial.tools.list_ports

# Colores ANSI para terminal
CYAN = "\033[96m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
RED = "\033[91m"
MAGENTA = "\033[95m"
BOLD = "\033[1m"
RESET = "\033[0m"

# Habilitar soporte ANSI en terminales de Windows
import os
os.system("")

def monitor_puerto(port_name, label, color):
    """Monitorea un puerto serie continuamente con auto-reconexion."""
    while True:
        try:
            print(f"{color}{BOLD}[CONECTANDO]{RESET} Buscando {label} en {port_name} (115200)...")
            ser = serial.Serial(port_name, 115200, timeout=1)
            print(f"{color}{BOLD}[EN LINEA]{RESET} Conectado con exito a {label} ({port_name}).")
            
            while True:
                linea = ser.readline().decode('utf-8', errors='ignore').strip()
                if linea:
                    timestamp = time.strftime("%H:%M:%S")
                    print(f"{color}[{timestamp} | {label}]{RESET} {linea}")
                    
        except serial.SerialException as e:
            print(f"{YELLOW}[AVISO]{RESET} {label} ({port_name}) desconectado o en flasheo. Esperando reconexion...")
            time.sleep(1.5)
        except Exception as e:
            print(f"{RED}[ERROR {label}]{RESET} {e}")
            time.sleep(2.0)

def main():
    print(f"\n{BOLD}{MAGENTA}======================================================{RESET}")
    print(f"{BOLD}   MONITOR EN VIVO LoRa 915 MHz - Popayan IoT{RESET}")
    print(f"{BOLD}   Emisor (COM6) <======> Receptor (COM7){RESET}")
    print(f"{BOLD}{MAGENTA}======================================================{RESET}\n")

    # Si se pasan argumentos especificos: python monitor_lora.py COM6 "EMISOR"
    if len(sys.argv) >= 3:
        puerto = sys.argv[1]
        etiqueta = sys.argv[2]
        color = CYAN if "EMISOR" in etiqueta.upper() else GREEN
        monitor_puerto(puerto, etiqueta, color)
        return

    # Modo unificado: monitorea ambos puertos en paralelo con dos hilos
    hilo_emisor = threading.Thread(
        target=monitor_puerto, 
        args=("COM6", "EMISOR (LLAVERO)", CYAN), 
        daemon=True
    )
    hilo_receptor = threading.Thread(
        target=monitor_puerto, 
        args=("COM7", "RECEPTOR (MOTO)  ", GREEN), 
        daemon=True
    )

    hilo_emisor.start()
    hilo_receptor.start()

    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        print(f"\n{YELLOW}[MONITOR]{RESET} Detenido por el usuario.")

if __name__ == "__main__":
    main()
