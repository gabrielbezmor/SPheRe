// no_ambiental.ino  ·  bibliotecas: PubSubClient, ArduinoJson,
// Adafruit_BME680, Adafruit_SHT31, Adafruit_TSL2561_U, SparkFun_SGP30_Arduino_Library
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_TSL2561_U.h>
#include <Adafruit_BME680.h>
#include <Adafruit_SHT31.h>
#include "SparkFun_SGP30_Arduino_Library.h"
#include <SPI.h>

const char* NO        = "env00";    
const char* SSID      = "SECRET_SSID";   
const char* SENHA     = "SECRET_SENHA";
const char* MQTT_HOST = "SECRET_MQTT_HOST";
const int   MQTT_PORTA = 1883;
const unsigned long INTERVALO = 30000;

#define SEALEVELPRESSURE_HPA (1013.25)
#define LED_BUILTIN 2

WiFiClient rede;
PubSubClient mqtt(rede);
Adafruit_BME680 bme;
Adafruit_SHT31  sht31;
Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(0x49, 12345);  // sensor 2 = 0x39 | sensor 1 = 0x49
SGP30 mySensor;

double tempBME = 0, resBME = 0, humBME = 0, pressBME = 0, tslLUX = 0, tempSHT31 = 0, humSHT31 = 0;
double sgpCO2 = 0, sgpEthanol = 0, sgpH2 = 0, sgpTVOC = 0;
int ledGpio = 2;

unsigned long ultimoEnvio = 0;

// -- NOVO: flags de presenca, preenchidas uma vez no setup() --
bool temBME680  = false;
bool temSHT31   = false;
bool temTSL2561 = false;
bool temSGP30   = false;

// -- NOVO: so aquece/inicializa o SGP30 se ele foi detectado no barramento --
void sgp30Init() {
  pinMode(ledGpio, OUTPUT);
  digitalWrite(ledGpio, 1);
  Wire.begin();

  temSGP30 = mySensor.begin();
  if (!temSGP30) {
    Serial.println("SGP30 nao detectado - pulando inicializacao");
    digitalWrite(ledGpio, 0);
    return;
  }

  mySensor.initAirQuality();
  Serial.println("SGP30 detectado - aquecendo (20 leituras de descarte, ~20s)...");
  for (int i = 0; i < 20; i++) {
    delay(1000);
    mySensor.measureAirQuality();
    Serial.print("descartou leitura ");
    Serial.println(i);
  }
  digitalWrite(ledGpio, 0);
}

void sgp30Readings() {
  mySensor.measureAirQuality();
  sgpCO2 = mySensor.CO2;
  sgpTVOC = mySensor.TVOC;
  Serial.print("CO2: "); Serial.print(sgpCO2);
  Serial.print(" ppm\tTVOC: "); Serial.print(sgpTVOC); Serial.println(" ppb");

  mySensor.measureRawSignals();
  sgpEthanol = mySensor.ethanol;
  sgpH2 = mySensor.H2;
  Serial.print("Raw H2: "); Serial.print(sgpH2);
  Serial.print(" \tRaw Ethanol: "); Serial.println(sgpEthanol);
}

void configureSensor(void) {
  tsl.enableAutoRange(true);
  tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_13MS);
  Serial.println("------------------------------------");
  Serial.println("TSL2561: Gain=Auto  Timing=13ms");
  Serial.println("------------------------------------");
}

void readtslLUX() {
  sensors_event_t event;
  tsl.getEvent(&event);
  if (event.light) {
    tslLUX = event.light;
    Serial.print(tslLUX); Serial.println(" lux");
  } else {
    Serial.println("TSL2561: sensor saturado");
  }
}

void readSHT31() {
  tempSHT31 = sht31.readTemperature();
  humSHT31 = sht31.readHumidity();

  if (!isnan(tempSHT31)) { Serial.print("Temp *C = "); Serial.print(tempSHT31); Serial.print("\t\t"); }
  else Serial.println("Falha ao ler temperatura SHT31");

  if (!isnan(humSHT31)) { Serial.print("Hum. % = "); Serial.println(humSHT31); }
  else Serial.println("Falha ao ler umidade SHT31");
}

void readBME680() {
  tempBME = 0; resBME = 0; humBME = 0; pressBME = 0;
  unsigned long endTime = bme.beginReading();
  if (endTime == 0) { Serial.println(F("Falha ao iniciar leitura BME680")); return; }
  if (!bme.endReading()) { Serial.println(F("Falha ao concluir leitura BME680")); return; }

  tempBME  = bme.temperature;
  resBME   = bme.gas_resistance / 1000.0;
  humBME   = bme.humidity;
  pressBME = bme.pressure / 100.0;

  Serial.print(F("BME680  T=")); Serial.print(tempBME);
  Serial.print(F("C  P=")); Serial.print(pressBME);
  Serial.print(F("hPa  U=")); Serial.print(humBME);
  Serial.print(F("%  Gas=")); Serial.print(resBME); Serial.println(F("KOhm"));
}

// -- NOVO: so chama a leitura do sensor que foi de fato detectado --
void readSensors() {
  if (temBME680)  readBME680();
  if (temSGP30)   sgp30Readings();
  if (temSHT31)   readSHT31();
  if (temTSL2561) readtslLUX();
}

// buffer circular para o que nao foi entregue
const int BUF_MAX = 40;
String buffer[BUF_MAX];
int bufInicio = 0, bufFim = 0, bufTam = 0;

void bufferGuardar(String carga) {
  if (bufTam == BUF_MAX) { bufInicio = (bufInicio + 1) % BUF_MAX; bufTam--; }
  buffer[bufFim] = carga;
  bufFim = (bufFim + 1) % BUF_MAX; bufTam++;
}

void bufferEsvaziar() {
  String topico = String("sphere/") + NO + "/telemetry";
  while (bufTam > 0 && mqtt.connected()) {
    if (!mqtt.publish(topico.c_str(), buffer[bufInicio].c_str())) break;
    bufInicio = (bufInicio + 1) % BUF_MAX; bufTam--;
  }
}

void conectarRede() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.begin(SSID, SENHA);
  unsigned long t0 = millis();
  Serial.print("Conectando ao wifi");
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
    Serial.print(".");
    delay(300);
  }
  if (millis() - t0 >= 10000) Serial.println("Conexao demorou demais");
  else Serial.println("Conectado!");
}

void conectarMqtt() {
  if (mqtt.connected()) return;
  String id = String("sphere-") + NO;
  if (mqtt.connect(id.c_str())) bufferEsvaziar();
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  delay(1000);
  Wire.begin();

  // -- NOVO: cada sensor e testado, e so configurado a fundo se detectado --
  temBME680 = bme.begin();
  if (temBME680) {
    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(0, 0);   // gas desligado: economiza e evita auto-aquecimento
    Serial.println("BME680 detectado");
  } else {
    Serial.println("BME680 nao detectado - pulando");
  }

  temSHT31 = sht31.begin(0x44);   // 0x45 para endereco alternativo
  Serial.println(temSHT31 ? "SHT31 detectado" : "SHT31 nao detectado - pulando");

  temTSL2561 = tsl.begin();
  if (temTSL2561) {
    configureSensor();
    Serial.println("TSL2561 detectado");
  } else {
    Serial.println("TSL2561 nao detectado - pulando");
  }

  sgp30Init();   // preenche temSGP30 internamente; so aquece se detectado

  Serial.println("== Resumo dos sensores detectados ==");
  Serial.print("BME680:  ");  Serial.println(temBME680  ? "OK" : "ausente");
  Serial.print("SHT31:   ");  Serial.println(temSHT31   ? "OK" : "ausente");
  Serial.print("TSL2561: ");  Serial.println(temTSL2561 ? "OK" : "ausente");
  Serial.print("SGP30:   ");  Serial.println(temSGP30   ? "OK" : "ausente");

  conectarRede();
  mqtt.setServer(MQTT_HOST, MQTT_PORTA);
}

void loop() {
  conectarRede();
  conectarMqtt();
  mqtt.loop();

  if (millis() - ultimoEnvio < INTERVALO) return;
  ultimoEnvio = millis();

  readSensors();
  Serial.println("Leituras finalizadas");

  StaticJsonDocument<384> doc;
  doc["no"] = NO;

  // -- NOVO: cada bloco de campos so entra no JSON se o sensor correspondente
  // foi detectado - nao ha mais dependencia do nome da placa (NO) --
  if (temBME680) {
    doc["bme_temp"]  = tempBME;
    doc["bme_umid"]  = humBME;
    doc["bme_press"] = pressBME;
  }
  if (temSHT31) {
    doc["sht_temp"] = tempSHT31;
    doc["sht_umid"] = humSHT31;
  }
  if (temTSL2561) {
    doc["tsl_lux"] = tslLUX;
  }
  if (temSGP30) {
    doc["sgp_co2"]     = sgpCO2;
    doc["sgp_ethanol"] = sgpEthanol;
    doc["sgp_h2"]      = sgpH2;
    doc["sgp_tvoc"]    = sgpTVOC;
  }
  doc["uptime"] = millis() / 1000;

  Serial.println("Json montado");
  char carga[384];
  serializeJson(doc, carga);

  String topico = String("sphere/") + NO + "/telemetry";
  if (!mqtt.connected() || !mqtt.publish(topico.c_str(), carga)) {
    Serial.println("Guardando pacote no buffer");
    bufferGuardar(String(carga));
  }
}
