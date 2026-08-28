#include "Arduino.h"
#include "esp_camera.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "base64.h"
#include "ArduinoJson.h"
#include "esp_https_server.h"

#include "secrets.h"

// // WiFi Credentials
// const char *WIFI_SSID = "Maximizer_2.4G";
// const char *WIFI_PASSWORD = "WhyYouAskForIt";

// // Roboflow Settings
// const char *ROBOFLOW_API_KEY = "2MhnDp32Hp98OchCY5d4";
// const char *ROBOFLOW_MODEL = "plant-disease-detection-k6wnw";
const int ROBOFLOW_VERSION = 1;
const float CONFIDENCE_THRESHOLD = 0.5;

const unsigned long DETECT_INTERVAL_MS = 300;

#define BUZZER_PIN 2

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

    String encoded = base64::encode(fb->buf, fb->len);
    esp_camera_fb_return(fb);

    String url = String("https://detect.roboflow.com/") + ROBOFLOW_MODEL + "/" + ROBOFLOW_VERSION + "?api_key=" + ROBOFLOW_API_KEY;

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.setTimeout(15000);

    Serial.println("[Detect] Sending frame to Roboflow.");
    int httpCode = http.POST(encoded);

    if (httpCode != 200)
    {
        Serial.printf("[Detect] HTTP error: %d\n", httpCode);
        Serial.println(http.getString());
        http.end();
        return;
    }

    String response = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error)
    {
        Serial.print("[Detect] JSON parse failed: ");
        Serial.println(error.c_str());
        return;
    }

    JsonArray predictions = doc["predictions"].as<JsonArray>();
    if (predictions.isNull() || predictions.size() == 0)
    {
        Serial.println("[Detect] No predictions returned (likely healthy / nothing detected)");
        return;
    }

    // Find the highest-confidence prediction
    const char *bestClass = nullptr;
    float bestConfidence = 0.0;
    for (JsonObject p : predictions)
    {
        float conf = p["confidence"] | 0.0;
        if (conf > bestConfidence)
        {
            bestConfidence = conf;
            bestClass = p["class"] | "unknown";
        }
    }

    if (bestClass == nullptr)
    {
        Serial.println("[Detect] Could not determine top prediction");
        return;
    }

    Serial.printf("[Detect] Top result: %s (%.1f%% confidence)\n", bestClass, bestConfidence * 100);

    bool looksHealthy = (strcasestr(bestClass, "healthy") != nullptr);

    if (!looksHealthy && bestConfidence >= CONFIDENCE_THRESHOLD)
    {
        Serial.printf("[ALERT] Possible disease detected: %s\n", bestClass);
        soundAlert();
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    Serial.println();

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
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 10;
        config.fb_count = 2;
        config.grab_mode = CAMERA_GRAB_LATEST;
    }
    else
    {
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 12;
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

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to wifi...");
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(300);
        Serial.print(".");
    }
    Serial.println();
    Serial.println("Wifi connected");

    startWebServer();

    Serial.print("View the stream at: http://");
    Serial.print(WiFi.localIP());
    Serial.println("  (index page shows the live image)");
    Serial.print("Raw MJPEG stream:   http://");
    Serial.print(WiFi.localIP());
    Serial.println(":81/stream");

    Serial.printf("Disease check will run every %lu seconds\n", DETECT_INTERVAL_MS / 1000);
}

unsigned long lastDetectTime = 0;

void loop()
{
    if (millis() - lastDetectTime >= DETECT_INTERVAL_MS)
    {
        lastDetectTime = millis();
        if (WiFi.status() == WL_CONNECTED)
        {
            runDiseaseCheck();
        }
        else
        {
            Serial.println("[Detect] WiFi not connected, skipping check");
        }
    }
    delay(10000);
}