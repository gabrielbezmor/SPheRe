from fastapi import FastAPI, UploadFile, File, Form, HTTPException
from datetime import datetime
import pathlib, yaml, json
import paho.mqtt.publish as publicar
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS

cfg = yaml.safe_load(open(pathlib.Path.home() / "SPheRe/config/rodada01.yaml"))
IMAGENS = pathlib.Path(cfg["caminhos"]["imagens"]).expanduser()

influx = InfluxDBClient(url=cfg["influx"]["url"],
                        token=cfg["influx"]["token"],
                        org=cfg["influx"]["org"])
escrita_influx = influx.write_api(write_options=SYNCHRONOUS)

app = FastAPI(title="SPheRe Rodada 01")

@app.post("/captura")
async def receber(no: str = Form(...), arquivo: UploadFile = File(...)):
    if no not in cfg["camaras"]:
        raise HTTPException(400, f"no desconhecido: {no}")

    agora = datetime.now()
    pasta = IMAGENS / agora.strftime("%Y%m%d")
    pasta.mkdir(parents=True, exist_ok=True)
    destino = pasta / f"{no}_{agora.strftime('%H%M%S')}.jpg"

    conteudo = await arquivo.read()
    destino.write_bytes(conteudo)

    publicar.single(
        f"sphere/{no}/status",
        json.dumps({"ts": agora.isoformat(), "bytes": len(conteudo)}),
        hostname=cfg["mqtt"]["host"],
    )

    try:
        ponto = (
            Point("image_capture")
            .tag("node", no)
            .field("filepath", str(destino))
            .field("bytes", len(conteudo))
            .time(agora)
        )
        escrita_influx.write(bucket=cfg["influx"]["bucket"], record=ponto)
    except Exception as e:
        print(f"[api] falha ao gravar image_capture no Influx: {e}") 

    return {"ok": True, "arquivo": destino.name, "bytes": len(conteudo)}

@app.get("/estado")
def estado():
    hoje = IMAGENS / datetime.now().strftime("%Y%m%d")
    if not hoje.exists():
        return {"capturas": {}}
    contagem = {}
    for caminho in hoje.glob("*.jpg"):
        no = caminho.name.split("_")[0]
        contagem[no] = contagem.get(no, 0) + 1
    return {"capturas": contagem, "rodada": cfg["rodada"]}
