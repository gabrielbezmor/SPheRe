
import time, json, threading, pathlib, yaml
from datetime import datetime
import matplotlib
matplotlib.use("Agg")   # backend headless - obrigatorio rodando via systemd, sem tela
import matplotlib.pyplot as plt
import numpy as np
import board, busio, adafruit_amg88xx
import requests
import paho.mqtt.client as mqtt

cfg = yaml.safe_load(open(pathlib.Path.home() / "SPheRe/config/rodada01.yaml"))
DEST = pathlib.Path(cfg["caminhos"]["termica"]).expanduser()
INTERVALO_MIN = cfg["sincronizacao"]["intervalo_min"]
NOS = cfg["sincronizacao"]["nos"]               {"cam01": "192.168.0.11", ...}

def inicializar_termica():
    i2c = busio.I2C(board.SCL, board.SDA)
    sensor = adafruit_amg88xx.AMG88XX(i2c, addr=cfg["termica"]["endereco"])
    print("[+] Sensor AMG8833 inicializado")
    return sensor

def calcular_segundos_ate_proximo_disparo(intervalo_min):
    agora = datetime.now()
    minutos_passados = agora.minute % intervalo_min
    minutos_restantes = intervalo_min - minutos_passados
    seg = (minutos_restantes * 60) - agora.second - agora.microsecond / 1_000_000.0
    if seg <= 0:
        seg += intervalo_min * 60
    return seg

def salvar_imagem_termica(matriz, agora):
    pasta = DEST / agora.strftime("%Y%m%d")
    pasta.mkdir(parents=True, exist_ok=True)
    destino = pasta / f"termica_{agora.strftime('%H%M%S')}.png"
    fig, ax = plt.subplots(figsize=(6, 6))
    hm = ax.imshow(matriz, cmap="inferno", interpolation="gaussian", vmin=15, vmax=45)
    fig.colorbar(hm, ax=ax, label="Temperatura (°C)")
    ax.set_title("Visualizacao Termica AMG8833"); ax.axis("off")
    fig.savefig(destino, bbox_inches="tight", dpi=150)
    plt.close(fig)   # fecha a figura a cada iteracao senao vaza memoria rodando dias seguidos
    print(f"[TERMICA] Imagem salva: {destino}")
    return destino

def requisitar_no(nome, ip):
    try:
        r = requests.get(f"http://{ip}/capture", timeout=35)
        status = "ok" if r.status_code == 200 else f"erro {r.status_code}"
    except Exception as e:
        status = f"falha: {e}"
    print(f"[{datetime.now().strftime('%H:%M:%S')}] [{nome}] {status}")

def disparar_nos_sincronizados():
    threads = [threading.Thread(target=requisitar_no, args=(nome, ip)) for nome, ip in NOS.items()]
    for t in threads: t.start()
    for t in threads: t.join(timeout=40)   # espera terminar antes do proximo ciclo

cliente_mqtt = mqtt.Client()
cliente_mqtt.connect(cfg["mqtt"]["host"], cfg["mqtt"]["porta"])
cliente_mqtt.loop_start()

def publicar_termica(agora, planos):
    carga = {
        "ts": agora.isoformat(),
        "media": round(sum(planos) / len(planos), 2),
        "minimo": round(min(planos), 2),
        "maximo": round(max(planos), 2),
    }
    cliente_mqtt.publish("sphere/thermal/frame", json.dumps(carga))

def main():
    sensor = inicializar_termica()
    print(f"[*] Orquestrador sincronizado. Intervalo: {INTERVALO_MIN} min. Nos: {list(NOS.keys())}")

    while True:
        espera = calcular_segundos_ate_proximo_disparo(INTERVALO_MIN)
        alvo = datetime.fromtimestamp(datetime.now().timestamp() + espera).strftime("%H:%M:%S")
        print(f"[*] Dormindo {espera:.1f}s ate {alvo}")
        time.sleep(espera)

        agora = datetime.now()
        matriz = np.array(sensor.pixels)
        planos = [v for linha in matriz for v in linha]

        salvar_imagem_termica(matriz, agora)
        publicar_termica(agora, planos)
        disparar_nos_sincronizados()

if __name__ == "__main__":
    main(
