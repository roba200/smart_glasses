# Smart Glasses Backend (ESP32-CAM → OpenAI Vision)

A minimal Node.js server that accepts images from an ESP32-CAM and forwards them to an OpenAI Vision model, returning the textual response.

## Features
- `POST /api/vision/raw`: send raw JPEG bytes (Content-Type: image/jpeg)
- `POST /api/vision/upload`: multipart/form-data with field `image`
- `POST /api/vision/base64`: JSON body `{ image_base64, mime?, prompt? }`
- `GET /health`: health check
- `GET /test`: browser page to upload a local image and see the response

## Requirements
- Node.js 18.17+
- An OpenAI API key with access to a vision-capable model (defaults to `gpt-4o-mini`)

## Setup
1. Copy `.env.example` to `.env` and set `OPENAI_API_KEY`.
   - Optional tuning for concise replies:
     - `VISION_REPLY_STYLE` (default: "Respond in one short sentence (max 15 words).")
     - `VISION_DEFAULT_PROMPT` (default: "Describe this image.")
     - `VISION_MAX_TOKENS` (default: 60)
2. Install deps and run:

```powershell
npm install
npm run dev
```

## ESP32-CAM Examples
Below are simple examples in Arduino-style pseudocode. Adjust to your actual WiFi and camera setup.

### Raw JPEG POST (recommended, simplest)
```
// Capture frame -> bytes[] and length
// Set your server IP and port
String server = "http://<your-pc-ip>:3000/api/vision/raw";

HTTPClient http;
http.setReuse(false);
http.begin(server);
http.addHeader("Content-Type", "image/jpeg");
int httpCode = http.POST((uint8_t*)fb->buf, fb->len);
String payload = http.getString();
http.end();
```

### Multipart upload
```
String boundary = "----esp32camboundary";
String url = "http://<your-pc-ip>:3000/api/vision/upload";
HTTPClient http;
http.begin(url);
http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

WiFiClient * stream = http.getStreamPtr();
String head = "--" + boundary + "\r\n" +
  "Content-Disposition: form-data; name=\"image\"; filename=\"frame.jpg\"\r\n" +
  "Content-Type: image/jpeg\r\n\r\n";
stream->print(head);
stream->write(fb->buf, fb->len);
String tail = "\r\n--" + boundary + "--\r\n";
stream->print(tail);
int httpCode = http.GETSize(); // Or perform proper POST, depending on library
```

### Base64 JSON (if easier on your client)
```
String b64 = base64::encode(fb->buf, fb->len);
String json = String("{\"image_base64\":\"") + b64 + "\"}";
HTTPClient http;
http.begin("http://<your-pc-ip>:3000/api/vision/base64");
http.addHeader("Content-Type", "application/json");
int code = http.POST(json);
String resp = http.getString();
http.end();
```

## Notes
- Ensure your PC and ESP32-CAM are on the same network, and the PC's firewall allows inbound connections to the chosen port.
- To change model or port, edit `.env` values.
