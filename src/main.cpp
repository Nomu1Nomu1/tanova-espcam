#include "Arduino.h"
#include "esp_camera.h"
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include "HTTPClient.h"
#include "ArduinoJson.h"
#include "esp_https_server.h"

#include "secrets.h"
#include "driver/rtc_io.h"
#include "pins.h"
#include "display_oled.h"

// --- Tanova AI API (isi di secrets.h: API_HOST wajib, sisanya opsional) ---
//   #define API_HOST "tanova-ai.example.com"   // tanpa http(s)://
//   #define API_PORT 443                       // 443 untuk HTTPS, 3000/80 untuk HTTP
//   #define API_USE_TLS 1                      // 0 jika HTTP biasa
//   #define API_KEY "..."                      // kosongkan jika auth dinonaktifkan
#ifndef API_HOST
#error "Define API_HOST in secrets.h"
#endif
#ifndef API_PORT
#define API_PORT 443
#endif
#ifndef API_USE_TLS
#define API_USE_TLS 1
#endif
#ifndef API_KEY
#define API_KEY ""
#endif
#ifndef API_PATH
#define API_PATH "/api/analyze"
#endif
#ifndef DEVICE_ID
#define DEVICE_ID "esp32-cam-01"
#endif

const float CONFIDENCE_THRESHOLD = 0.5;

// Jeda deteksi otomatis, dihitung setelah request selesai.
// Gemini free tier: 15 request/menit, 1500/hari -> 60 detik = ~1440/hari.
const unsigned long DETECT_INTERVAL_MS = 60UL * 1000UL;

String latestDisease = "Standby";
float latestConfidence = 0.0;
unsigned long lastDisplayUpdate = 0;

#define BUZZER_PIN PIN_BUZZER

#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 15
#define SIOD_GPIO_NUM 4
#define SIOC_GPIO_NUM 5

#define Y9_GPIO_NUM 16
#define Y8_GPIO_NUM 17
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 12
#define Y5_GPIO_NUM 10
#define Y4_GPIO_NUM 8
#define Y3_GPIO_NUM 9
#define Y2_GPIO_NUM 11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM 7
#define PCLK_GPIO_NUM 13

httpd_handle_t stream_httpd = NULL;
httpd_handle_t index_httpd = NULL;

#define PART_BOUNDARY "123456789000000000000987654321"

static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32-S3 OV5640 Stream</title>
  <style>
    body { background:#111; color:#eee; font-family:sans-serif; text-align:center; }
    img { max-width:100%; height:auto; border:2px solid #444; border-radius:8px; margin-top:20px; }
  </style>
</head>
<body>
  <h2>ESP32-S3 Camera Stream</h2>
  <img src="/stream">
</body>
</html>
)rawliteral";

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char part_buf[64];

    res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK)
        return res;

    while (true)
    {
        fb = esp_camera_fb_get();
        if (!fb)
        {
            Serial.println("Cam capture failed");
            res = ESP_FAIL;
        }
        else
        {
            if (fb->format != PIXFORMAT_JPEG)
            {
                Serial.println("Non JPEG frame, aborting stream");
                esp_camera_fb_return(fb);
                res = ESP_FAIL;
            }
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        }
        if (res == ESP_OK)
        {
            size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        }
        if (fb)
        {
            esp_camera_fb_return(fb);
            fb = NULL;
        }
        if (res != ESP_OK)
            break;
    }
    return res;
}

void startWebServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.server_port = 80;
    config.ctrl_port = 32768;
    httpd_uri_t index_uri = {"/", HTTP_GET, index_handler, NULL};
    if (httpd_start(&index_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(index_httpd, &index_uri);
    }

    config.server_port = 81;
    config.ctrl_port = 32769;
    httpd_uri_t stream_uri = {"/stream", HTTP_GET, stream_handler, NULL};
    if (httpd_start(&stream_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}

void soundAlert()
{
    for (int i = 0; i < 3; i++)
    {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(200);
        digitalWrite(BUZZER_PIN, LOW);
        delay(200);
    }
}

void runDiseaseCheck()
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        Serial.println("[Detect] Camera capture failed, skipping this check.");
        return;
    }

    size_t imgLen = fb->len;
    Serial.printf("[Detect] Free heap: %u bytes, frame size: %u bytes\n", ESP.getFreeHeap(), imgLen);
    displayShowAnalyzing();

    // Kirim JPEG mentah (tanpa base64) ke API Tanova AI
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    WiFiClient *client = &plainClient;
#if API_USE_TLS
    secureClient.setInsecure();
    client = &secureClient;
#endif

    Serial.printf("[Detect] Connecting to %s:%d ...\n", API_HOST, API_PORT);
    unsigned long connectStart = millis();
    bool connected = client->connect(API_HOST, API_PORT, 10000);
    Serial.printf("[Detect] Connect result: %s, took %lu ms\n",
                  connected ? "OK" : "FAILED", millis() - connectStart);

    if (!connected)
    {
        Serial.println("[Detect] Connect failed/timeout, aborting this cycle.");
        client->stop();
        esp_camera_fb_return(fb);
        return;
    }

    // Write HTTP request manually
    client->printf("POST %s HTTP/1.1\r\n", API_PATH);
    client->printf("Host: %s\r\n", API_HOST);
    client->println("Content-Type: image/jpeg");
    client->printf("Content-Length: %u\r\n", imgLen);
    client->printf("x-device-id: %s\r\n", DEVICE_ID);
    if (strlen(API_KEY) > 0)
        client->printf("x-api-key: %s\r\n", API_KEY);
    client->println("Connection: close");
    client->println(); // end of headers

    // Send body in small chunks (4KB)
    Serial.println("[Detect] Sending frame to API...");
    unsigned long postStart = millis();
    const size_t CHUNK_SIZE = 4096;
    size_t offset = 0;
    bool sendOk = true;
    while (offset < imgLen)
    {
        size_t toSend = min(CHUNK_SIZE, imgLen - offset);
        size_t written = client->write(fb->buf + offset, toSend);
        if (written == 0)
        {
            Serial.println("[Detect] Write failed during chunked send");
            sendOk = false;
            break;
        }
        offset += written;
    }
    esp_camera_fb_return(fb); // frame buffer bisa dilepas setelah terkirim

    if (!sendOk)
    {
        client->stop();
        return;
    }

    Serial.printf("[Detect] Payload sent (%u bytes), took %lu ms. Waiting for response...\n",
                  offset, millis() - postStart);

    // Gemini butuh beberapa detik, timeout 30 detik
    unsigned long responseStart = millis();
    while (client->connected() && !client->available())
    {
        if (millis() - responseStart > 30000)
        {
            Serial.println("[Detect] Response timeout");
            client->stop();
            return;
        }
        delay(10);
    }

    // Read HTTP status line
    String statusLine = client->readStringUntil('\n');
    int httpCode = 0;
    if (statusLine.startsWith("HTTP/"))
    {
        httpCode = statusLine.substring(9, 12).toInt();
    }
    Serial.printf("[Detect] HTTP %d, response took %lu ms\n", httpCode, millis() - responseStart);

    // Skip remaining headers
    while (client->connected())
    {
        String line = client->readStringUntil('\n');
        if (line == "\r" || line.length() == 0)
            break;
    }

    // Read body
    String response = client->readString();
    client->stop();

    // Buang chunk-size marker jika server/proxy memakai chunked encoding
    int jsonStart = response.indexOf('{');
    int jsonEnd = response.lastIndexOf('}');
    if (jsonStart >= 0 && jsonEnd > jsonStart)
        response = response.substring(jsonStart, jsonEnd + 1);

    // Hanya ambil field yang dibutuhkan (field "text" panjang, tidak dipakai)
    JsonDocument filter;
    // Backend Tanova (/api/v1/ai/analyze): 201 = tersimpan, 200 + is_plant:false = bukan tanaman
    filter["saved"] = true;
    filter["is_plant"] = true;
    filter["label"] = true;
    filter["confidence"] = true;
    filter["category"] = true;
    filter["details"]["plant"] = true;
    filter["message"] = true;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response, DeserializationOption::Filter(filter));
    if (error)
    {
        Serial.print("[Detect] JSON parse failed: ");
        Serial.println(error.c_str());
        latestDisease = "API Error";
        latestConfidence = 0.0;
        return;
    }

    // 200 + is_plant:false = gambar bukan tanaman (tidak disimpan backend)
    if (httpCode == 200 && doc["is_plant"] == false)
    {
        Serial.println("[Detect] Not a plant, ignoring.");
        latestDisease = "Bukan Tanaman";
        latestConfidence = 0.0;
        return;
    }

    // 201 = diagnosis tersimpan di backend
    if (httpCode != 201)
    {
        const char *errMsg = doc["message"] | "unknown";
        Serial.printf("[Detect] API error %d: %s\n", httpCode, errMsg);
        latestDisease = "API Error";
        latestConfidence = 0.0;
        return;
    }

    const char *category = doc["category"] | "";
    const char *plant = doc["details"]["plant"] | "unknown";
    float confidence = doc["confidence"] | 0.0f; // backend sudah 0-1

    if (strcmp(category, "leaf_health") == 0)
    {
        Serial.printf("[Detect] Healthy: %s (%.1f%% confidence)\n", plant, confidence * 100);
        latestDisease = "Healthy/Clear";
        latestConfidence = confidence;
        return;
    }

    const char *problem = doc["label"] | "unknown";
    latestDisease = String(problem);
    latestConfidence = confidence;

    Serial.printf("[Detect] Top result: %s on %s (%.1f%% confidence)\n", problem, plant, confidence * 100);

    if (confidence >= CONFIDENCE_THRESHOLD)
    {
        Serial.printf("[ALERT] Possible disease detected: %s\n", problem);
        displayShowDiseaseAlert(problem, confidence);
        soundAlert();
    }
}

bool tahanTombol5Detik(const __FlashStringHelper *label); // forward declaration

// Deep Sleep sungguhan: layar mati, bangun lagi saat tombol ditekan
static void mulaiDeepSleep()
{
    display.clearDisplay();
    display.display();

    // Tunggu tombol dilepas
    while (digitalRead(PIN_TOMBOL) == LOW)
    {
        delay(50);
    }
    delay(200);

    // Wake-up saat tombol ditekan (LOW)
    rtc_gpio_pullup_en((gpio_num_t)PIN_TOMBOL);
    rtc_gpio_pulldown_dis((gpio_num_t)PIN_TOMBOL);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_TOMBOL, 0);
    esp_deep_sleep_start();
}

// Layar OFF: hanya progress bar baterai + animasi charging.
// Tampil minimal 30 detik (diperpanjang tiap tombol di-tap) dan terus tampil selama charging.
// Tahan tombol 5 detik = nyalakan sistem kembali.
static void layarChargingOff()
{
    delay(1500); // beban baru dilepas, biarkan tegangan stabil dulu
    resetDeteksiCharging();

    unsigned long start = millis();
    while (true)
    {
        tampilkanLayarCharging();

        if (digitalRead(PIN_TOMBOL) == LOW)
        {
            if (tahanTombol5Detik(F("Tahan untuk ON...")))
            {
                ESP.restart(); // boot normal
            }
            start = millis(); // tap: perpanjang tampilan
        }

        if (!statusCharging() && millis() - start > 30000)
            break;

        delay(100);
    }
}

// Matikan sistem: kipas off, WiFi off, tampilkan layar charging, lalu Deep Sleep
void masukDeepSleep()
{
    digitalWrite(PIN_KIPAS, LOW);

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(10, 25);
    display.println(F("Sistem Dimatikan..."));
    display.display();

    digitalWrite(PIN_BUZZER, HIGH);
    delay(300);
    digitalWrite(PIN_BUZZER, LOW);
    delay(700);

    // Tunggu tombol dilepas agar tidak langsung dianggap tap
    while (digitalRead(PIN_TOMBOL) == LOW)
    {
        delay(50);
    }
    delay(200);

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    layarChargingOff();
    mulaiDeepSleep();
}

// Tampilkan progress bar tahan tombol; return true jika ditahan 5 detik
bool tahanTombol5Detik(const __FlashStringHelper *label)
{
    unsigned long mulaiTekan = millis();
    while (digitalRead(PIN_TOMBOL) == LOW)
    {
        unsigned long durasi = millis() - mulaiTekan;
        if (durasi >= 5000)
            return true;
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SH110X_WHITE);
        display.setCursor(0, 15);
        display.println(label);
        int bar = map(durasi, 0, 5000, 0, 128);
        display.fillRect(0, 35, bar, 10, SH110X_WHITE);
        display.display();
        delay(50);
    }
    return false;
}

// Bangun dari Deep Sleep: tahan tombol 5 detik = nyala.
// Tap singkat = tampilkan layar charging, lalu tidur lagi.
void bootPowerGate()
{
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0)
    {
        if (!tahanTombol5Detik(F("Tahan untuk ON...")))
        {
            layarChargingOff();
            mulaiDeepSleep();
        }
    }
}

// Tombol ditahan 5 detik -> matikan. Return true jika hanya di-tap (dilepas < 5 detik)
bool checkPowerButton()
{
    if (digitalRead(PIN_TOMBOL) == LOW)
    {
        delay(30); // debounce
        if (digitalRead(PIN_TOMBOL) != LOW)
            return false;
        if (tahanTombol5Detik(F("Tahan untuk OFF...")))
        {
            masukDeepSleep();
        }
        return true;
    }
    return false;
}

void setup()
{
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    Serial.println();

    // Initialize 1.3" OLED Display (SH1106)
    initDisplay();

    // Inisialisasi pin I/O (tombol, kipas, buzzer)
    pinMode(PIN_TOMBOL, INPUT_PULLUP);
    pinMode(PIN_KIPAS, OUTPUT);
    digitalWrite(PIN_KIPAS, LOW);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    // Validasi tombol saat bangun dari Deep Sleep
    bootPowerGate();

    // Sistem resmi nyala: kipas ON + beep
    digitalWrite(PIN_KIPAS, HIGH);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(150);
    digitalWrite(BUZZER_PIN, LOW);
    while (digitalRead(PIN_TOMBOL) == LOW)
    {
        delay(50);
    }

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;

    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;

    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    if (psramFound())
    {
        config.frame_size = FRAMESIZE_VGA; // VGA agar stream lebih lancar
        config.jpeg_quality = 15;
        config.fb_count = 2;
        config.grab_mode = CAMERA_GRAB_LATEST;
    }
    else
    {
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 15;
        config.fb_count = 1;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        Serial.println("Check: PSRAM enabled as OPI in board settings, and pin mapping matches your board.");
        return;
    }

    // Camera Flip
    // s->set_vflip(s, 1);
    // s->set_hmirror(s, 1);

    displayShowWiFiConnecting(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to wifi...");
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(300);
        Serial.print(".");
    }
    Serial.println();
    Serial.println("Wifi connected");
    WiFi.setSleep(false); // matikan WiFi power saving agar stream tidak lag

    // Use Google DNS to avoid router DNS issues
    IPAddress dns1(8, 8, 8, 8);
    IPAddress dns2(8, 8, 4, 4);
    WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns1, dns2);
    Serial.printf("DNS configured: %s / %s\n", dns1.toString().c_str(), dns2.toString().c_str());
    Serial.printf("IP: %s  Gateway: %s\n", WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str());

    displayShowWiFiConnected(WiFi.localIP());

    startWebServer();

    Serial.print("View the stream at: http://");
    Serial.print(WiFi.localIP());
    Serial.println("  (index page shows the live image)");
    Serial.print("Raw MJPEG stream:   http://");
    Serial.print(WiFi.localIP());
    Serial.println(":81/stream");
}

unsigned long lastDetectTime = 0;

void loop()
{
    // Tahan tombol 5 detik = matikan
    checkPowerButton();

    unsigned long sinceLast = millis() - lastDetectTime;
    int secondsLeft = (sinceLast >= DETECT_INTERVAL_MS) ? 0 : (int)((DETECT_INTERVAL_MS - sinceLast) / 1000UL) + 1;

    // Update OLED monitor display periodically (every 250ms, agar animasi charging mulus)
    if (millis() - lastDisplayUpdate >= 250)
    {
        lastDisplayUpdate = millis();
        displayUpdate(secondsLeft, latestDisease.c_str(), latestConfidence);
    }

    // Deteksi otomatis tiap DETECT_INTERVAL_MS
    if (sinceLast >= DETECT_INTERVAL_MS)
    {
        Serial.println("\n[Detect] >>> Scheduled scan <<<");

        if (WiFi.status() == WL_CONNECTED)
        {
            runDiseaseCheck();
        }
        else
        {
            Serial.println("[Detect] WiFi not connected, skipping check");
        }
        lastDetectTime = millis(); // mulai cooldown setelah request selesai
    }

    delay(20); // Responsive non-blocking loop
}