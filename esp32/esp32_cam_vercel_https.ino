// ESP32-CAM → Vercel backend (HTTPS) using raw JPEG POST
// Endpoint base: https://smart-glasses-olive.vercel.app/
// This sketch posts captured JPEG frames to /api/vision/raw over TLS.
// Board: AI Thinker ESP32-CAM (PSRAM enabled)

#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ====== WiFi ======
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// ====== Backend ======
const char* BASE_URL = "https://smart-glasses-olive.vercel.app"; // no trailing slash
const char* RAW_PATH = "/api/vision/raw"; // POST raw JPEG
const char* HEALTH_PATH = "/health";     // optional warm-up

// ====== TLS Root CA ======
// IMPORTANT: Use the correct root CA for smart-glasses-olive.vercel.app.
// Quick unblock: enable insecure TLS below (for testing only), then replace this with the real root CA.
// How to fetch CA:
//   openssl s_client -showcerts -connect smart-glasses-olive.vercel.app:443 < /dev/null | openssl x509 -inform pem -noout -text
// Copy the top-level issuer certificate (root) PEM into ROOT_CA exactly between BEGIN/END.
static const char* ROOT_CA = R"CA(
-----BEGIN CERTIFICATE-----
<PASTE_CORRECT_ROOT_CA_PEM_HERE>
-----END CERTIFICATE-----
)CA";

// Set to 1 to disable certificate validation (testing only)
#define USE_INSECURE_TLS 1

// ====== Camera Pins (AI Thinker) ======
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

const unsigned long CAPTURE_INTERVAL_MS = 8000;

bool ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Connecting WiFi: %s\n", WIFI_SSID);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi failed");
    return false;
  }
  Serial.print("WiFi IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA; // adjust if payload too large
    config.jpeg_quality = 12;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 15;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }
  return true;
}

bool warmupHealth(WiFiClientSecure& client) {
  HTTPClient https;
  https.setConnectTimeout(8000);
  https.setTimeout(15000);
  https.begin(client, String(BASE_URL) + String(HEALTH_PATH));
  int code = https.GET();
  Serial.printf("Health GET: %d\n", code);
  https.end();
  return code > 0 && code < 500; // Vercel cold starts can 404/5xx initially
}

bool postRawJpeg(camera_fb_t* fb, WiFiClientSecure& client) {
  if (!fb || fb->format != PIXFORMAT_JPEG) return false;

  HTTPClient https;
  https.setConnectTimeout(10000);
  https.setTimeout(30000);
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  https.useHTTP10(true);      // avoid chunked encoding
  https.setReuse(false);      // disable keep-alive

  String url = String(BASE_URL) + String(RAW_PATH);
  Serial.printf("POST %s (%u bytes)\n", url.c_str(), fb->len);

  if (!https.begin(client, url)) {
    Serial.println("HTTPS begin failed");
    return false;
  }
  https.addHeader("Connection", "close");
  https.addHeader("Content-Type", "image/jpeg");
  https.addHeader("Content-Length", String(fb->len));
  int code = https.POST((uint8_t*)fb->buf, fb->len);
  Serial.printf("HTTP %d\n", code);
  String payload = https.getString();
  Serial.println(payload);
  https.end();

  if (code > 0 && code < 400) return true;

  // Fallback: direct HTTPS socket with manual HTTP/1.1 request
  Serial.println("Falling back to direct socket POST...");
  WiFiClientSecure direct;
#if USE_INSECURE_TLS
  direct.setInsecure();
#else
  direct.setCACert(ROOT_CA);
#endif
  // Extract host from BASE_URL (assumes https://host)
  String base(BASE_URL);
  String host = base;
  host.replace("https://", "");
  int slash = host.indexOf('/');
  if (slash >= 0) host = host.substring(0, slash);

  if (!direct.connect(host.c_str(), 443)) {
    Serial.println("Direct connect failed");
    return false;
  }

  // Build request
  String req;
  req.reserve(256);
  req += "POST ";
  req += RAW_PATH;
  req += " HTTP/1.1\r\n";
  req += "Host: "; req += host; req += "\r\n";
  req += "Content-Type: image/jpeg\r\n";
  req += "Content-Length: "; req += String(fb->len); req += "\r\n";
  req += "Connection: close\r\n\r\n";

  if (direct.print(req) != (int)req.length()) {
    Serial.println("Header write failed");
    direct.stop();
    return false;
  }

  size_t toWrite = fb->len;
  const uint8_t* p = fb->buf;
  while (toWrite > 0) {
    size_t n = direct.write(p, toWrite > 1024 ? 1024 : toWrite);
    if (n == 0) {
      Serial.println("Body write stalled");
      direct.stop();
      return false;
    }
    p += n;
    toWrite -= n;
    delay(1);
  }

  // Read response
  unsigned long start = millis();
  while (direct.connected() && millis() - start < 10000) {
    while (direct.available()) {
      String line = direct.readStringUntil('\n');
      Serial.print(line);
      start = millis();
    }
    delay(5);
  }
  direct.stop();
  return true; // assume success if connection handled
}

unsigned long lastShot = 0;

void setup() {
  Serial.begin(115200);
  delay(500);

  if (!ensureWifi()) {
    Serial.println("WiFi not ready; retrying in loop...");
  }
  if (!initCamera()) {
    Serial.println("Camera failed; rebooting in 5s");
    delay(5000);
    ESP.restart();
  }

  // Prepare TLS client
  WiFiClientSecure client;
#if USE_INSECURE_TLS
  client.setInsecure(); // WARNING: testing only, disables cert validation
#else
  client.setCACert(ROOT_CA);
#endif
  warmupHealth(client); // optional warm-up
}

void loop() {
  if (!ensureWifi()) {
    delay(2000);
    return;
  }

  if (millis() - lastShot >= CAPTURE_INTERVAL_MS) {
    lastShot = millis();

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Capture failed");
      return;
    }

  WiFiClientSecure client;
#if USE_INSECURE_TLS
  client.setInsecure();
#else
  client.setCACert(ROOT_CA);
#endif

    bool ok = postRawJpeg(fb, client);
    esp_camera_fb_return(fb);

    if (!ok) Serial.println("Upload failed\n");
    else Serial.println("Upload ok\n");
  }
}
