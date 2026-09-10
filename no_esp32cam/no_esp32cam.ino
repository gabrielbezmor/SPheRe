// no_camera.ino  ·  placa: AI Thinker ESP32-CAM  ·  sincronizado com o Raspberry
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include <secrets.h>

const char* NO    = "cam03";   
const char* SSID  = "SECRET_SSID";
const char* SENHA = "SECRET_SENHA";
const char* API   = "SECRET_API_URL";
const unsigned long INTERVALO = 15UL * 60UL * 1000UL;  // fallback - so entra em acao se o Pi nao disparar
unsigned long ultimaCaptura = 0;

WebServer servidor(80);

//IP fixo no firmware (Metodo B da secao 1.2) 
#include <IPAddress.h>
IPAddress meuIP(192, 168, 0, 13);      // .12 para cam02, .13 para cam03
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);

// pinagem AI Thinker
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM   0
#define SIOD_GPIO_NUM  26
#define SIOC_GPIO_NUM  27
#define Y9_GPIO_NUM    35
#define Y8_GPIO_NUM    34
#define Y7_GPIO_NUM    39
#define Y6_GPIO_NUM    36
#define Y5_GPIO_NUM    21
#define Y4_GPIO_NUM    19
#define Y3_GPIO_NUM    18
#define Y2_GPIO_NUM     5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM  23
#define PCLK_GPIO_NUM  22

bool iniciarCamera() {
  camera_config_t c;
  c.ledc_channel = LEDC_CHANNEL_0; c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;   c.pin_pclk  = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM; c.pin_href  = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM; c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM;   c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size   = FRAMESIZE_SVGA;
  c.jpeg_quality = 10;
  c.fb_count     = 1;
    esp_err_t resultado = esp_camera_init(&c);
  if (resultado != ESP_OK) {
    Serial.printf("Falha na camera, codigo: 0x%x\n", resultado);
  }
  return resultado == ESP_OK;

}

bool enviarImagem() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return false;

  HTTPClient http;
  http.begin(API);
  http.setTimeout(15000);

  String limite = "----sphere";
  String cabeca = "--" + limite + "\r\n"
    "Content-Disposition: form-data; name=\"no\"\r\n\r\n" + String(NO) + "\r\n"
    "--" + limite + "\r\n"
    "Content-Disposition: form-data; name=\"arquivo\"; filename=\"" + String(NO) + ".jpg\"\r\n"
    "Content-Type: image/jpeg\r\n\r\n";
  String rodape = "\r\n--" + limite + "--\r\n";

  int total = cabeca.length() + fb->len + rodape.length();
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
  camera_fb_t* descarte = esp_camera_fb_get();   // descarta frame com exposicao antiga
  if (descarte) esp_camera_fb_return(descarte);

  bool ok = enviarImagem();
  ultimaCaptura = millis();   // reseta o fallback - evita duplicar captura em seguida
  servidor.send(ok ? 200 : 500, "text/plain", ok ? "ok" : "falha");
}

void setup() {
  Serial.begin(115200);
  if (!iniciarCamera()) { delay(3000); ESP.restart(); }

  // WiFi.config(meuIP, gateway, subnet);   // descomente se usar IP fixo no firmware
  WiFi.begin(SSID, SENHA);
  Serial.println(WiFi.macAddress());
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(300);

  servidor.on("/capture", HTTP_GET, aoReceberCapture);
  servidor.begin();

  ultimaCaptura = millis() - INTERVALO;  // forca a primeira captura logo apos conectar
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); delay(2000); return; }
  servidor.handleClient();   // -- NOVO: atende o GET /capture a cada iteracao --

  if (millis() - ultimaCaptura < INTERVALO) return;   // fallback - so age se o Pi nunca disparou
  ultimaCaptura = millis();

  camera_fb_t* descarte = esp_camera_fb_get();
  if (descarte) esp_camera_fb_return(descarte);

  enviarImagem();
}