// ESP32-CAM (AI Thinker) → Vercel backend (HTTPS, base64 JSON)
// Serial-triggered capture (type "capture" in Serial Monitor). No OLED, no buzzer.
// Sends base64 image to: https://smart-glasses-olive.vercel.app/api/vision/base64
// Backend returns: { "text": "...short response..." }

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Base64.h>
#include "esp_camera.h"
#include <ArduinoJson.h>
#include <time.h>
#include <driver/i2s.h>

// ===== WiFi =====
const char* ssid = "SSID";
const char* password = "PASS";

// ===== Backend =====
const char* BACKEND_URL = "https://smart-glasses-olive.vercel.app/api/vision/base64"; // POST JSON
const char* TTS_URL     = "https://smart-glasses-olive.vercel.app/api/tts";           // POST text -> WAV
const char* MIME_TYPE = "image/jpeg";
// Default prompts (backend enforces brevity too)
static const char* PROMPT_DESCRIBE = "Describe the image in one short sentence.";
static const char* PROMPT_SOLVE    = "Solve the problem shown in this image. Provide only the final concise answer in one short sentence.";

// ===== Pins (AI Thinker) =====
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22
// Built-in flash LED (torch) on AI Thinker is GPIO 4
#define FLASH_GPIO_NUM 4

// ===== MAX98357A I2S Pins (adjust to your wiring) =====
#define I2S_BCLK  12
#define I2S_LRCLK 13
#define I2S_DOUT  15

// WAV playback config
static uint32_t g_sampleRate = 16000; // default; will be updated from WAV header when available
static uint8_t  g_channels   = 1;     // mono preferred for MAX98357A

// No button; trigger from Serial

// ===== Camera defaults (smaller payload for Vercel) =====
#define CAM_FRAME_SIZE     FRAMESIZE_QVGA  // use QQVGA if you still hit limits
#define CAM_JPEG_QUALITY   18              // higher = smaller file

// No OLED helpers; print to Serial instead.

// ===== Root CA (Let's Encrypt ISRG Root X1) =====
// Provided by user; required for proper TLS cert validation.
// Source: https://letsencrypt.org/certificates/ (ISRG Root X1)
static const char ISRG_ROOT_X1[] PROGMEM = R"PEM(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)PEM";

static String encodeImageToBase64(const uint8_t* data, size_t len) {
  return base64::encode(data, len);
}

static bool ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
  }
  return WiFi.status() == WL_CONNECTED;
}

static bool ensureTimeSynced() {
  static bool synced = false;
  if (synced) return true;
  // Set time via NTP so TLS cert validation succeeds
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  const unsigned long maxWait = 15000; // 15s
  unsigned long start = millis();
  time_t now;
  while ((now = time(nullptr)) < 1609459200 && millis() - start < maxWait) { // before 2021-01-01?
    delay(200);
  }
  if (now >= 1609459200) {
    synced = true;
    return true;
  }
  return false;
}

static bool initCamera() {
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
  config.frame_size   = CAM_FRAME_SIZE;
  config.jpeg_quality = CAM_JPEG_QUALITY;
  config.fb_count     = 1;
  return esp_camera_init(&config) == ESP_OK;
}

static bool initI2S() {
  // I2S setup for MAX98357A (I2S Philips standard)
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = (int)g_sampleRate,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT, // mono to right channel
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pin_config = { I2S_BCLK, I2S_LRCLK, I2S_DOUT, -1 };
  if (i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL) != ESP_OK) return false;
  if (i2s_set_pin(I2S_NUM_0, &pin_config) != ESP_OK) return false;
  i2s_set_clk(I2S_NUM_0, g_sampleRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
  return true;
}

static bool parseWavHeader(const uint8_t* data, size_t len, size_t& outDataOffset) {
  if (len < 44) return false;
  if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) return false;
  // fmt chunk at 12
  size_t pos = 12;
  while (pos + 8 <= len) {
    const char* id = (const char*)(data + pos);
    uint32_t sz = *(const uint32_t*)(data + pos + 4);
    pos += 8;
    if (memcmp(id, "fmt ", 4) == 0) {
      if (pos + sz > len) return false;
      uint16_t audioFormat = *(const uint16_t*)(data + pos + 0);
      uint16_t numChannels = *(const uint16_t*)(data + pos + 2);
      uint32_t sampleRate  = *(const uint32_t*)(data + pos + 4);
      // uint32_t byteRate = *(const uint32_t*)(data + pos + 8);
      // uint16_t blockAlign = *(const uint16_t*)(data + pos + 12);
      uint16_t bitsPerSample = *(const uint16_t*)(data + pos + 14);
      if (audioFormat != 1 || bitsPerSample != 16) return false; // PCM 16-bit only
      g_sampleRate = sampleRate;
      g_channels = numChannels;
      pos += sz;
    } else if (memcmp(id, "data", 4) == 0) {
      if (pos + sz > len) return false;
      outDataOffset = pos;
      return true;
    } else {
      pos += sz;
    }
  }
  return false;
}

static void i2sPlayPCM16(const uint8_t* pcm, size_t len) {
  size_t written = 0;
  while (written < len) {
    size_t toWrite = len - written;
    if (toWrite > 1024) toWrite = 1024;
    size_t out = 0;
    i2s_write(I2S_NUM_0, pcm + written, toWrite, &out, portMAX_DELAY);
    written += out;
  }
}

static bool speakText(const String& text) {
  if (!ensureWifi()) return false;
  if (!ensureTimeSynced()) {
    Serial.println("Warning: time not synced; TLS validation may fail");
  }
  HTTPClient http;
  http.setReuse(false);
  WiFiClientSecure client;
  client.setCACert(ISRG_ROOT_X1);
  if (!http.begin(client, TTS_URL)) return false;
  http.addHeader("Content-Type", "application/json");
  http.setConnectTimeout(15000);
  http.setTimeout(60000);
  http.useHTTP10(true);

  // Request WAV for simple ESP32 playback
  String payload = String('{') + "\"text\":\"" + jsonEscape(text) + "\",\"format\":\"wav\"}";
  int code = http.POST(payload);
  if (!(code > 0 && code < 400)) {
    String err = http.getString();
    http.end();
    Serial.print("TTS HTTP error: "); Serial.println(code);
    Serial.println(err);
    return false;
  }
  // Read entire body into buffer (ensure size reasonable for ESP32 RAM)
  WiFiClient* stream = http.getStreamPtr();
  const int contentLen = http.getSize();
  if (contentLen <= 0 || contentLen > 350000) { // limit ~350KB
    http.end();
    Serial.println("Invalid or too large audio");
    return false;
  }
  std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[contentLen]);
  if (!buf) { http.end(); Serial.println("No RAM for audio"); return false; }
  int read = 0; unsigned long start = millis();
  while (read < contentLen && millis() - start < 30000) {
    int n = stream->read(buf.get() + read, contentLen - read);
    if (n > 0) { read += n; start = millis(); }
    else delay(1);
  }
  http.end();
  if (read != contentLen) { Serial.println("Audio read incomplete"); return false; }

  // Parse WAV header
  size_t dataOffset = 0;
  if (!parseWavHeader(buf.get(), read, dataOffset)) {
    Serial.println("WAV parse failed");
    return false;
  }
  // Reconfigure I2S if sample rate changed
  i2s_set_clk(I2S_NUM_0, g_sampleRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
  const uint8_t* pcm = buf.get() + dataOffset;
  const size_t pcmLen = read - dataOffset;
  i2s_zero_dma_buffer(I2S_NUM_0);
  i2sPlayPCM16(pcm, pcmLen);
  i2s_zero_dma_buffer(I2S_NUM_0);
  return true;
}

static String jsonEscape(const String& s) {
  String out; out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '\\' || c == '"') { out += '\\'; out += c; }
    else if (c == '\n') { out += "\\n"; }
    else if (c == '\r') { out += "\\r"; }
    else if (c == '\t') { out += "\\t"; }
    else { out += c; }
  }
  return out;
}

static bool postToBackend(const String& base64Image, const String& prompt, String& outResponse) {
  if (!ensureWifi()) return false;
  if (!ensureTimeSynced()) {
    Serial.println("Warning: time not synced; TLS validation may fail");
  }
  HTTPClient http;
  http.setReuse(false); // no keep-alive
  WiFiClientSecure client;
  client.setCACert(ISRG_ROOT_X1);
  if (!http.begin(client, BACKEND_URL)) return false;
  http.addHeader("Content-Type", "application/json");
  http.setConnectTimeout(15000); // 15s connect
  http.setTimeout(60000);        // 60s overall (OpenAI vision may take time)
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.useHTTP10(true); // avoid chunked

  // Build payload without storing large base64 in ArduinoJson to save RAM
  // Safe because base64 uses [A-Za-z0-9+/=]
  String payload;
  payload.reserve(64 + base64Image.length() + prompt.length());
  payload += "{\"image_base64\":\""; payload += base64Image; payload += "\",";
  payload += "\"mime\":\""; payload += MIME_TYPE; payload += "\",";
  payload += "\"prompt\":\""; payload += jsonEscape(prompt); payload += "\"}";

  int code = http.POST(payload);
  String body = http.getString();
  http.end();

  if (code > 0 && code < 400) {
    // Parse { text: "..." }
    DynamicJsonDocument resp(1024);
    DeserializationError err = deserializeJson(resp, body);
    if (!err && resp.containsKey("text")) {
      outResponse = resp["text"].as<String>();
    } else {
      outResponse = body; // fallback raw
    }
    return true;
  }

  // Fallback: direct HTTPS socket POST
  Serial.println("Falling back to direct socket POST...");
  String url = String(BACKEND_URL);
  String host = url; host.replace("https://", "");
  int slash = host.indexOf('/');
  String path = "/";
  if (slash >= 0) { path = host.substring(slash); host = host.substring(0, slash); }

  WiFiClientSecure direct;
  direct.setCACert(ISRG_ROOT_X1);
  if (!direct.connect(host.c_str(), 443)) {
    outResponse = "Direct connect failed";
    return false;
  }

  String req;
  req.reserve(256);
  req += "POST "; req += path; req += " HTTP/1.1\r\n";
  req += "Host: "; req += host; req += "\r\n";
  req += "Content-Type: application/json\r\n";
  req += "Content-Length: "; req += String(payload.length()); req += "\r\n";
  req += "Connection: close\r\n\r\n";

  if (direct.print(req) != (int)req.length()) { direct.stop(); outResponse = "Header write failed"; return false; }

  size_t toWrite = payload.length();
  const char* p = payload.c_str();
  while (toWrite > 0) {
    size_t n = direct.write((const uint8_t*)p, toWrite > 1024 ? 1024 : toWrite);
    if (n == 0) { direct.stop(); outResponse = "Body write stalled"; return false; }
    p += n; toWrite -= n; delay(1);
  }

  // Read status line
  int status = -1;
  unsigned long start = millis();
  while (direct.connected() && !direct.available() && millis() - start < 8000) delay(5);
  if (direct.available()) {
    String statusLine = direct.readStringUntil('\n');
    Serial.print(statusLine);
    int sp = statusLine.indexOf(' ');
    if (sp > 0 && statusLine.length() >= sp + 4) status = statusLine.substring(sp + 1, sp + 4).toInt();
  }
  // Capture rest (limited)
  String respBody; respBody.reserve(2048);
  start = millis(); bool headerDone = false;
  while (direct.connected() && millis() - start < 15000) {
    while (direct.available()) {
      String chunk = direct.readStringUntil('\n');
      if (!headerDone) {
        if (chunk == "\r" || chunk == "\n" || chunk == "\r\n") headerDone = true;
      } else {
        if (respBody.length() + chunk.length() < 4096) respBody += chunk;
      }
      start = millis();
    }
    delay(5);
  }
  direct.stop();

  if (status > 0 && status < 400) {
    DynamicJsonDocument resp(1024);
    if (deserializeJson(resp, respBody) == DeserializationError::Ok && resp.containsKey("text")) {
      outResponse = resp["text"].as<String>();
    } else {
      outResponse = respBody;
    }
    return true;
  } else {
    outResponse = String("HTTP ") + status + ": " + respBody;
    return false;
  }
}

static void captureAndSend(const String& prompt) {
  Serial.println("Capturing...");

  // Turn on flash to light the scene before capture
  digitalWrite(FLASH_GPIO_NUM, HIGH);
  delay(200); // allow auto-exposure to settle

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { Serial.println("Capture failed"); return; }

  // Optional: grab twice to avoid stale buffer
  esp_camera_fb_return(fb);
  fb = esp_camera_fb_get();
  if (!fb) { Serial.println("Capture failed"); return; }

  String b64 = encodeImageToBase64(fb->buf, fb->len);
  esp_camera_fb_return(fb);
  // Turn off flash after capture
  digitalWrite(FLASH_GPIO_NUM, LOW);
  if (b64.isEmpty()) { Serial.println("Encode failed"); return; }

  Serial.println("Processing...");
  String result;
  if (postToBackend(b64, prompt, result)) {
    Serial.print("Response: ");
    Serial.println(result);
    // Speak out the response
    if (!initI2S()) {
      Serial.println("I2S init failed (MAX98357A)");
    } else {
      if (!speakText(result)) {
        Serial.println("TTS playback failed");
      }
    }
  } else {
    Serial.print("API error: ");
    Serial.println(result);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting...");
  WiFi.setSleep(false); // improve TLS stability

  // Init flash pin (off)
  pinMode(FLASH_GPIO_NUM, OUTPUT);
  digitalWrite(FLASH_GPIO_NUM, LOW);

  if (!ensureWifi()) {
    Serial.println("WiFi fail");
  } else {
    Serial.println("WiFi OK");
  }

  if (!initCamera()) {
    Serial.println("Cam init fail");
    delay(3000);
    ESP.restart();
  }
  Serial.println("Type 'describe' to describe the image, or 'solve' to solve a problem shown in the image.");
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    if (cmd == "describe") {
      captureAndSend(String(PROMPT_DESCRIBE));
      Serial.println("Ready. Type 'describe' or 'solve' again.");
    } else if (cmd == "solve") {
      captureAndSend(String(PROMPT_SOLVE));
      Serial.println("Ready. Type 'describe' or 'solve' again.");
    } else if (cmd.length() > 0) {
      Serial.print("Unknown command: ");
      Serial.println(cmd);
      Serial.println("Type 'describe' or 'solve'.");
    }
  }
}
