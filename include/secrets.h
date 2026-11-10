#pragma once

// --- WiFi ---
#define WIFI_SSID "54545"
#define WIFI_PASSWORD "1234567890"

// --- Tanova AI API ---
#define API_HOST "tanovaai.onrender.com" // tanpa http:// atau https://
#define API_PORT 443                   // 443 untuk HTTPS, 3000 untuk HTTP lokal
#define API_USE_TLS 1                  // 1 = HTTPS, 0 = HTTP biasa
#define API_KEY ""                     // isi jika auth x-api-key di server diaktifkan
#define API_PATH "/api/analyze"
#define DEVICE_ID "esp32-cam-01"