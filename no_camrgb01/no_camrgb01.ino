// no_cam01_irrigacao.ino  ·  placa: XIAO_ESP32S3 (Sense)  ·  sincronizado com o Raspberry
#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <secrets.h>

// ---------- Identidade e rede ----------
const char* NO        = "cam01";
const char* SSID      = "SECRET_SSID";
const char* SENHA     = "SECRET_SENHA";
const char* API_URL   = "SECRET_API_URL";
const char* MQTT_HOST = "SECRET_MQTT_HOST";

WebServer servidor(80);

//IP fixo no firmware (Metodo B da secao 1.2) 
#include <IPAddress.h>
IPAddress meuIP(192, 168, 0, 11);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);

// ---------- Pinos da extensora - CONFIRMADOS por teste de continuidade (placa desenergizada) ----------
// Tabela de conversao D-number -> GPIO no XIAO ESP32S3:
// D0=1 D1=2 D2=3 D3=4 D4=5(SDA) D5=6(SCL) D6=43(TX) D7=44(RX) D8=7 D9=8 D10=9
const int PINO_BOMBA = 8;   // Actr1 -> D9 -> GPIO8
const int PINO_SOLO  = 1;   // Snsr1 -> D0 -> GPIO1 (ADC1)

// ---------- Calibracao do sensor capacitivo (substrato real, ver documento de calibracao) ----------
int LEITURA_SECO    = 3200;   // AJUSTAR: leitura com substrato seco ao ar
int LEITURA_MOLHADO = 1400;   // AJUSTAR: leitura com substrato saturado e drenado 24h (capacidade de campo)

// MAD (Management Allowed Depletion) extrapolado por analogia com hortaliças de raiz rasa
// (alface ~30%, cenoura ~35%, pimentao/tomate ~25%) - nao ha MAD publicado especificamente para rabanete.
const int LIMITE_LIGA    = 72;   // 100 - MAD*100, MAD=27,5%
const int LIMITE_REARME  = 90;   // proximo da capacidade de campo
const unsigned long DURACAO_BOMBA_MS = 4000;
const unsigned long ESPERA_CICLO_MS  = 600000;   // 10 min entre acionamentos

// ---------- Intervalos ----------
const unsigned long INTERVALO_FOTO    = 15UL * 60UL * 1000UL;  // fallback - so age se o Pi nao disparar
const unsigned long INTERVALO_UMIDADE = 60UL * 1000UL;         // independente do ciclo de fotos

// ---------- Pinagem da camera - XIAO ESP32S3 Sense ----------
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    10
#define SIOD_GPIO_NUM    40
#define SIOC_GPIO_NUM    39
#define Y9_GPIO_NUM      48
#define Y8_GPIO_NUM      11
#define Y7_GPIO_NUM      12
#define Y6_GPIO_NUM      14
#define Y5_GPIO_NUM      16
#define Y4_GPIO_NUM      18
#define Y3_GPIO_NUM      17
#define Y2_GPIO_NUM      15
#define VSYNC_GPIO_NUM   38
#define HREF_GPIO_NUM    47
#define PCLK_GPIO_NUM    13

WiFiClient rede;
PubSubClient mqtt(rede);

unsigned long ultimaFoto = 0;
unsigned long ultimaLeituraUmidade = 0;
unsigned long ultimoAcionamento = 0;
bool armado = true;

// ---------------- Camera ----------------
bool iniciarCamera() {
  camera_config_t c;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk  = XCLK_GPIO_NUM;   c.pin_pclk  = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM;  c.pin_href  = HREF_GPIO_NUM;
  c.pin_sscb_sda = SIOD_GPIO_NUM; c.pin_sscb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn  = PWDN_GPIO_NUM;   c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size   = FRAMESIZE_SVGA;
  c.jpeg_quality = 10;
  c.fb_count     = 2;
  c.grab_mode    = CAMERA_GRAB_LATEST;
  return esp_camera_init(&c) == ESP_OK;
}

bool enviarFoto() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return false;

  HTTPClient http;
  http.begin(API_URL);
  http.setTimeout(15000);

  String limite = "----sphere";
  String cabeca = "--" + limite + "\r\n"
    "Content-Disposition: form-data; name=\"no\"\r\n\r\n" + String(NO) + "\r\n"
    "--" + limite + "\r\n"
    "Content-Disposition: form-data; name=\"arquivo\"; filename=\"" + String(NO) + ".jpg\"\r\n"
    "Content-Type: image/jpeg\r\n\r\n";
  String rodape = "\r\n--" + limite + "--\r\n";

  size_t total = cabeca.length() + fb->len + rodape.length();
  uint8_t* corpo = (uint8_t*) malloc(total);
  if (!corpo) { esp_camera_fb_return(fb); return false; }

  memcpy(corpo, cabeca.c_str(), cabeca.length());
  memcpy(corpo + cabeca.length(), fb->buf, fb->len);
  memcpy(corpo + cabeca.length() + fb->len, rodape.c_str(), rodape.length());

  http.addHeader("Content-Type", "multipart/form-data; boundary=" + limite);

  // -- NOVO: retry - ate 3 tentativas, reaproveitando o mesmo corpo montado --
  int codigo = -1;
  for (int tentativa = 1; tentativa <= 3 && codigo != 200; tentativa++) {
    codigo = http.POST(corpo, total);
    if (codigo != 200 && tentativa < 3) delay(500);
  }

  free(corpo);
  esp_camera_fb_return(fb);
  http.end();
  return codigo == 200;
}

// -- NOVO: handler da rota sincronizada --
void aoReceberCapture() {
  camera_fb_t* descarte = esp_camera_fb_get();
  if (descarte) esp_camera_fb_return(descarte);

  bool ok = enviarFoto();
  ultimaFoto = millis();   // reseta o fallback
  servidor.send(ok ? 200 : 500, "text/plain", ok ? "ok" : "falha");
}

// ---------------- Umidade e bomba (independente do ciclo de fotos) ----------------
int lerUmidade() {
  int v[9];
  for (int i = 0; i < 9; i++) { v[i] = analogRead(PINO_SOLO); delay(25); }
  for (int i = 1; i < 9; i++) {
    int chave = v[i], j = i - 1;
    while (j >= 0 && v[j] > chave) { v[j + 1] = v[j]; j--; }
    v[j + 1] = chave;
  }
  int bruto = v[4];
  return constrain(map(bruto, LEITURA_SECO, LEITURA_MOLHADO, 0, 100), 0, 100);
}

void acionarBomba() {
  digitalWrite(PINO_BOMBA, HIGH);
  mqtt.publish("sphere/irrig/evento", "{\"no\":\"cam01\",\"acao\":\"ligou\"}");
  delay(DURACAO_BOMBA_MS);
  digitalWrite(PINO_BOMBA, LOW);
  mqtt.publish("sphere/irrig/evento", "{\"no\":\"cam01\",\"acao\":\"desligou\"}");
  ultimoAcionamento = millis();
}

// ---------------- Wi-Fi / MQTT ----------------
void conectarWiFi() {
  // WiFi.config(meuIP, gateway, subnet);   // descomente se usar IP fixo no firmware
  WiFi.begin(SSID, SENHA);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(300);
}

void garantirMQTT() {
  if (!mqtt.connected()) mqtt.connect("sphere-cam01");
}

// ---------------- Setup / Loop ----------------
void setup() {
  Serial.begin(115200); delay(2000); Serial.println("boot ok");

  pinMode(PINO_BOMBA, OUTPUT);
  digitalWrite(PINO_BOMBA, LOW);          // estado seguro no boot
  analogSetPinAttenuation(PINO_SOLO, ADC_11db);

  if (!iniciarCamera()) {
    Serial.println("Falha ao iniciar a camera. Reiniciando em 3s...");
    delay(3000);
    ESP.restart();
  }

  conectarWiFi();
  mqtt.setServer(MQTT_HOST, 1883);

  servidor.on("/capture", HTTP_GET, aoReceberCapture);   // -- NOVO --
  servidor.begin();

  ultimaFoto = millis() - INTERVALO_FOTO;
  ultimaLeituraUmidade = millis() - INTERVALO_UMIDADE;
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); delay(2000); return; }
  garantirMQTT();
  mqtt.loop();
  servidor.handleClient();   // -- NOVO: atende o GET /capture, nao interfere na umidade --

  // ---- leitura de umidade + decisao de irrigacao - roda no proprio ritmo, sempre ----
  if (millis() - ultimaLeituraUmidade >= INTERVALO_UMIDADE) {
    ultimaLeituraUmidade = millis();
    int umid = lerUmidade();

    StaticJsonDocument<128> doc;
    doc["no"] = NO;
    doc["umidade_solo"] = umid;
    doc["armado"] = armado;
    char carga[128];
    serializeJson(doc, carga);
    mqtt.publish("sphere/cam01/telemetry", carga);

    if (armado && umid < LIMITE_LIGA && millis() - ultimoAcionamento > ESPERA_CICLO_MS) {
      acionarBomba();
      armado = false;
    }
    if (!armado && umid > LIMITE_REARME) armado = true;
  }

  // ---- captura e envio de foto - fallback, so age se o Pi nao disparou via /capture ----
  if (millis() - ultimaFoto >= INTERVALO_FOTO) {
    ultimaFoto = millis();
    camera_fb_t* descarte = esp_camera_fb_get();
    if (descarte) esp_camera_fb_return(descarte);
    enviarFoto();
  }
}