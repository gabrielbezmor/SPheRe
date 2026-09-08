import json, pathlib, yaml, time
import paho.mqtt.client as mqtt
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS
from datetime import datetime

cfg = yaml.safe_load(open(pathlib.Path.home() / "SPheRe/config/rodada01.yaml"))
FILA = pathlib.Path(cfg["caminhos"]["buffer"]).expanduser()
FILA.mkdir(parents=True, exist_ok=True)

influx = InfluxDBClient(url=cfg["influx"]["url"],
                        token=cfg["influx"]["token"],
                        org=cfg["influx"]["org"])
escrita = influx.write_api(write_options=SYNCHRONOUS)

def enfileirar(medida, no, campos, ts):
    arquivo = FILA / f"{int(time.time()*1000)}.json"
    arquivo.write_text(json.dumps({"medida": medida, "no": no, "campos": campos, "ts": ts}))

def gravar(no, campos, ts=None, medida="sensor_data"):
    ts = ts or datetime.now().isoformat()
    ponto = Point(medida).tag("node", no).time(ts)
    for chave, valor in campos.items():
        if isinstance(valor, (int, float)):
            ponto = ponto.field(chave, float(valor))
        elif isinstance(valor, str):
            ponto = ponto.field(chave, valor)  
    try:
        escrita.write(bucket=cfg["influx"]["bucket"], record=ponto)
        return True
    except Exception:
        enfileirar(medida, no, campos, ts)
        return False

def drenar_fila():
    for arquivo in sorted(FILA.glob("*.json")):
        item = json.loads(arquivo.read_text())
        if gravar(item["no"], item["campos"], item["ts"], item.get("medida", "sensor_data")):
            arquivo.unlink()

def ao_receber(cliente, dados, msg):
    try:
        carga = json.loads(msg.payload.decode())
    except Exception:
        return
    partes = msg.topic.split("/")      # sphere/<no>/<tipo>
    if len(partes) < 3:
        return
    no = partes[1]
    if no == "thermal":
        gravar("thermal", {"media": carga["media"], "minimo": carga["minimo"], "maximo": carga["maximo"]}, carga.get("ts"))
    elif no == "irrig":
        acao = carga.get("acao", "desconhecido")
        gravar(
            carga.get("no", "cam01"),
            {"action": 1 if acao == "ligou" else 0, "action_str": acao},
            medida="irrigation_event",
        )
    else:
        gravar(no, {k: v for k, v in carga.items() if k != "no"})

cliente = mqtt.Client()
cliente.on_message = ao_receber
cliente.connect(cfg["mqtt"]["host"], cfg["mqtt"]["porta"])
cliente.subscribe("sphere/#")
cliente.loop_start()

while True:
    drenar_fila()
    time.sleep(30)
