import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import pandas as pd
import seaborn as sns
import numpy as np

sns.set_theme(style="whitegrid")

df_raw = pd.read_csv("data.csv", comment="#", low_memory=False)

df_raw["_value"] = pd.to_numeric(df_raw["_value"], errors="coerce")
df_raw["_time"] = pd.to_datetime(
    df_raw["_time"], utc=True, format="ISO8601", errors="coerce"
).dt.tz_convert("America/Sao_Paulo")

df_minuto = df_raw.groupby(
    [pd.Grouper(key="_time", freq="1min"), "_field"]
)["_value"].mean().unstack("_field")
df_minuto.columns.name = None
print("=== Colunas extraídas após o Pivot ===")
print(df_minuto.columns.tolist())


def configurar_eixo_tempo(ax):
    ax.xaxis.set_major_locator(mdates.HourLocator(byhour=[0, 12]))
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%d/%m %H:%M"))
    ax.tick_params(axis="x", rotation=45)

# 3. Cálculo das Diferenças
if "bme_temp" in df_minuto.columns and "sht_temp" in df_minuto.columns:
    df_minuto["dif_temp"] = (df_minuto["bme_temp"] - df_minuto["sht_temp"]).abs()
    print(
        f"\nErro Médio Absoluto Temp (BME680 vs SHT31): {df_minuto['dif_temp'].mean():.2f} °C"
    )

if "bme_umid" in df_minuto.columns and "sht_umid" in df_minuto.columns:
    df_minuto["dif_umid"] = (df_minuto["bme_umid"] - df_minuto["sht_umid"]).abs()
    print(
        f"Erro Médio Absoluto Umid (BME680 vs SHT31): {df_minuto['dif_umid'].mean():.2f} %"
    )

# 4. Geração dos Gráficos
fig_temp, ax_temp = plt.subplots(figsize=(10, 4))

cols_temp = [c for c in ["bme_temp", "sht_temp"] if c in df_minuto.columns]
if cols_temp:
    df_minuto[cols_temp].rename(
        columns={"bme_temp": "BME680", "sht_temp": "SHT31"}
    ).plot(ax=ax_temp, linewidth=1.5)
    ax_temp.set_title("Temperatura (°C)")
    ax_temp.set_ylabel("°C")
    ax_temp.set_xlabel("Data")
    configurar_eixo_tempo(ax_temp)

fig_temp.tight_layout()

fig_umid, ax_umid = plt.subplots(figsize=(10, 4))
cols_umid = [c for c in ["bme_umid", "sht_umid"] if c in df_minuto.columns]
if cols_umid:
    df_minuto[cols_umid].rename(
        columns={"bme_umid": "BME680", "sht_umid": "SHT31"}
    ).plot(ax=ax_umid, linewidth=1.5)
    ax_umid.set_title("Umidade Relativa (%)")
    ax_umid.set_ylabel("%")
    ax_umid.set_xlabel("Data")
    configurar_eixo_tempo(ax_umid)

fig_umid.tight_layout()

if "bme_temp" in df_minuto.columns and "bme_umid" in df_minuto.columns:
    fig_temp_umid, ax_temp_umid = plt.subplots(figsize=(10, 4))
    df_minuto["bme_temp"].plot(
        ax=ax_temp_umid, color="#d62728", linewidth=1.5, label="Temperatura (°C)"
    )
    ax_temp_umid.set_title("Temperatura e Umidade")
    ax_temp_umid.set_xlabel("Data")
    ax_temp_umid.set_ylabel("Temperatura (°C)", color="#d62728")
    ax_temp_umid.tick_params(axis="y", labelcolor="#d62728")

    ax_umid_secondary = ax_temp_umid.twinx()
    df_minuto["bme_umid"].plot(
        ax=ax_umid_secondary,
        color="#1f77b4",
        linewidth=1.5,
        label="Umidade (%)",
    )
    ax_umid_secondary.set_ylabel("Umidade (%)", color="#1f77b4")
    ax_umid_secondary.tick_params(axis="y", labelcolor="#1f77b4")

    lines_temp, labels_temp = ax_temp_umid.get_legend_handles_labels()
    lines_umid, labels_umid = ax_umid_secondary.get_legend_handles_labels()
    ax_temp_umid.legend(lines_temp + lines_umid, labels_temp + labels_umid, loc="best")
    configurar_eixo_tempo(ax_temp_umid)
    fig_temp_umid.tight_layout()

if "bme_press" in df_minuto.columns:
    fig_press, ax_press = plt.subplots(figsize=(10, 4))
    df_minuto["bme_press"].plot(
        ax=ax_press, color="#9467bd", linewidth=1.5, label="Pressão"
    )
    ax_press.set_title("Pressão Atmosférica")
    ax_press.set_ylabel("Pressão (hPa)")
    ax_press.set_xlabel("Data")
    ax_press.legend()
    configurar_eixo_tempo(ax_press)
    fig_press.tight_layout()


# Supondo que df_minuto já seja o DataFrame pivotado e reamostrado por minuto

# 1. Métricas da Atmosfera (BME680)
temp_med = df_minuto["bme_temp"].mean()
temp_min = df_minuto["bme_temp"].min()
temp_max = df_minuto["bme_temp"].max()
temp_std = df_minuto["bme_temp"].std()

umid_med = df_minuto["bme_umid"].mean()
umid_min = df_minuto["bme_umid"].min()
umid_max = df_minuto["bme_umid"].max()

press_med = df_minuto["bme_press"].mean()
press_min = df_minuto["bme_press"].min()
press_max = df_minuto["bme_press"].max()

thermal_media_med = df_minuto["media"].mean()
thermal_media_min = df_minuto["media"].min()
thermal_media_max = df_minuto["media"].max()

temp_min_time = df_minuto["bme_temp"].idxmin()
temp_max_time = df_minuto["bme_temp"].idxmax()
umid_min_time = df_minuto["bme_umid"].idxmin()
umid_max_time = df_minuto["bme_umid"].idxmax()
press_min_time = df_minuto["bme_press"].idxmin()
press_max_time = df_minuto["bme_press"].idxmax()

# 2. Métricas de Comparação e Precisão (BME680 vs SHT31)
mae_temp = (df_minuto["bme_temp"] - df_minuto["sht_temp"]).abs().mean()
rmse_temp = np.sqrt(
    ((df_minuto["bme_temp"] - df_minuto["sht_temp"]) ** 2).mean()
)
mbe_temp = (df_minuto["bme_temp"] - df_minuto["sht_temp"]).mean()
corr_temp = df_minuto["bme_temp"].corr(df_minuto["sht_temp"])

mae_umid = (df_minuto["bme_umid"] - df_minuto["sht_umid"]).abs().mean()
corr_umid = df_minuto["bme_umid"].corr(df_minuto["sht_umid"])

print("==================================================")
print("       RESUMO ESTATÍSTICO PARA O RELATÓRIO        ")
print("==================================================")
print(
    f"Temperatura BME680: Média = {temp_med:.2f}°C | Min = {temp_min:.2f}°C | Max = {temp_max:.2f}°C (DP = {temp_std:.2f})"
)
print(
    f"Umidade BME680:     Média = {umid_med:.2f}%  | Min = {umid_min:.2f}%  | Max = {umid_max:.2f}%"
)
print(
    f"Pressão BME680:     Média = {press_med:.2f} hPa | Min = {press_min:.2f} hPa | Max = {press_max:.2f} hPa"
)
print(
    f"Câmera térmica (media): Média = {thermal_media_med:.2f} °C | Min = {thermal_media_min:.2f} °C | Max = {thermal_media_max:.2f} °C"
)
print(f"  Temperatura mínima: {temp_min:.2f} °C em {temp_min_time}")
print(f"  Temperatura máxima: {temp_max:.2f} °C em {temp_max_time}")
print(f"  Umidade mínima:     {umid_min:.2f}% em {umid_min_time}")
print(f"  Umidade máxima:     {umid_max:.2f}% em {umid_max_time}")
print(f"  Pressão mínima:     {press_min:.2f} hPa em {press_min_time}")
print(f"  Pressão máxima:     {press_max:.2f} hPa em {press_max_time}")
print("--------------------------------------------------")
print(
    f"MAE Temp:  {mae_temp:.2f} °C  | RMSE: {rmse_temp:.2f} °C | MBE: {mbe_temp:.2f} °C"
)
print(f"MAE Umid:  {mae_umid:.2f} %")
print(f"Correlação de Pearson Temp (BME680 vs SHT31): {corr_temp:.4f}")
print(f"Correlação de Pearson Umid (BME680 vs SHT31): {corr_umid:.4f}")
print("==================================================")


# Gráficos do SGP em janelas separadas
if "sgp_co2" in df_minuto.columns:
    fig_co2, ax_co2 = plt.subplots(figsize=(10, 4))
    df_minuto["sgp_co2"].plot(ax=ax_co2, color="#2ca02c", linewidth=1.5)
    ax_co2.set_title("Concentração Equivalente de CO2 (eCO2)")
    ax_co2.set_ylabel("ppm")
    ax_co2.set_xlabel("Data")
    configurar_eixo_tempo(ax_co2)
    fig_co2.tight_layout()

if "sgp_tvoc" in df_minuto.columns:
    fig_tvoc, ax_tvoc = plt.subplots(figsize=(10, 4))
    df_minuto["sgp_tvoc"].plot(ax=ax_tvoc, color="#1f77b4", linewidth=1.5)
    ax_tvoc.set_title("Total de Compostos Orgânicos Voláteis (TVOC)")
    ax_tvoc.set_ylabel("ppb")
    ax_tvoc.set_xlabel("Data")
    configurar_eixo_tempo(ax_tvoc)
    fig_tvoc.tight_layout()

if "tsl_lux" in df_minuto.columns:
    fig_lux, ax_lux = plt.subplots(figsize=(10, 4))
    df_minuto["tsl_lux"].plot(ax=ax_lux, color="#e6ab02", linewidth=1.5)
    ax_lux.set_title("Luminosidade (TSL2561)")
    ax_lux.set_ylabel("Lux")
    ax_lux.set_xlabel("Data")
    configurar_eixo_tempo(ax_lux)
    fig_lux.tight_layout()

if "tsl_lux" in df_minuto.columns and "sht_temp" in df_minuto.columns:
    fig_lux_temp, ax_lux_temp = plt.subplots(figsize=(10, 4))
    df_minuto["tsl_lux"].plot(
        ax=ax_lux_temp, color="#e6ab02", linewidth=1.5, label="Luminosidade (Lux)"
    )
    ax_lux_temp.set_title("Luminosidade e Temperatura (SHT31)")
    ax_lux_temp.set_xlabel("Data")
    ax_lux_temp.set_ylabel("Luminosidade (Lux)", color="#b07d00")
    ax_lux_temp.tick_params(axis="y", labelcolor="#b07d00")

    ax_sht_temp_secondary = ax_lux_temp.twinx()
    df_minuto["sht_temp"].plot(
        ax=ax_sht_temp_secondary,
        color="#d62728",
        linewidth=1.5,
        label="Temperatura SHT31 (°C)",
    )
    ax_sht_temp_secondary.set_ylabel("Temperatura (°C)", color="#d62728")
    ax_sht_temp_secondary.tick_params(axis="y", labelcolor="#d62728")

    lines_lux, labels_lux = ax_lux_temp.get_legend_handles_labels()
    lines_sht_temp, labels_sht_temp = (
        ax_sht_temp_secondary.get_legend_handles_labels()
    )
    ax_lux_temp.legend(
        lines_lux + lines_sht_temp, labels_lux + labels_sht_temp, loc="best"
    )
    configurar_eixo_tempo(ax_lux_temp)
    fig_lux_temp.tight_layout()

cols_raw = [c for c in ["sgp_h2", "sgp_ethanol"] if c in df_minuto.columns]
if cols_raw:
    fig_raw, ax_raw = plt.subplots(figsize=(10, 4))
    df_minuto[cols_raw].rename(
        columns={"sgp_h2": "H2", "sgp_ethanol": "Etanol"}
    ).plot(ax=ax_raw, linewidth=1.2)
    ax_raw.set_title("Sinais Brutos de Hidrogênio (H2) e Etanol")
    ax_raw.set_ylabel("Sinal Bruto (Ticks)")
    ax_raw.set_xlabel("Data")
    configurar_eixo_tempo(ax_raw)
    fig_raw.tight_layout()

if "sgp_h2" in df_minuto.columns and "sgp_ethanol" in df_minuto.columns:
    corr_h2_ethanol = df_minuto["sgp_h2"].corr(df_minuto["sgp_ethanol"])
    print(f"Correlação de Pearson H2 e Etanol: {corr_h2_ethanol:.4f}")

if "sgp_co2" in df_minuto.columns and "sgp_tvoc" in df_minuto.columns:
    corr_co2_tvoc = df_minuto["sgp_co2"].corr(df_minuto["sgp_tvoc"])
    print(f"Correlação de Pearson CO2 e TVOC: {corr_co2_tvoc:.4f}")

plt.show()
