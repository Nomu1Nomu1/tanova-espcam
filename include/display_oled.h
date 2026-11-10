#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "pins.h"

// OLED SH1106 128x64 (1.3")
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ---------------- Baterai ----------------

// Baca tegangan baterai (rata-rata 16 sampel, dalam Volt)
float bacaTeganganBaterai()
{
    uint32_t total = 0;
    for (int i = 0; i < 16; i++)
    {
        total += analogReadMilliVolts(PIN_BATERAI);
    }
    float mv = total / 16.0;
    return (mv * RASIO_DIVIDER) / 1000.0;
}

// Konversi tegangan ke persen memakai kurva discharge LiPo/Li-ion 1S (interpolasi)
int hitungPersenBaterai(float v)
{
    static const float tV[] = {4.20, 4.15, 4.11, 4.08, 4.02, 3.98, 3.95, 3.91, 3.87, 3.85, 3.84,
                               3.82, 3.80, 3.79, 3.77, 3.75, 3.73, 3.71, 3.69, 3.61, 3.27};
    static const int tP[] = {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50,
                             45, 40, 35, 30, 25, 20, 15, 10, 5, 0};
    const int n = sizeof(tV) / sizeof(tV[0]);
    if (v >= tV[0])
        return 100;
    if (v <= tV[n - 1])
        return 0;
    for (int i = 0; i < n - 1; i++)
    {
        if (v <= tV[i] && v > tV[i + 1])
        {
            float f = (v - tV[i + 1]) / (tV[i] - tV[i + 1]);
            return (int)(tP[i + 1] + f * (tP[i] - tP[i + 1]) + 0.5f);
        }
    }
    return 0;
}

// ---- Status baterai yang distabilkan ----
static float vFilt = 0;           // tegangan setelah smoothing (EMA)
static int persenTampil = -1;     // persen yang ditampilkan (stabil)
static bool sedangCharging = false;
static unsigned long lastSampleMs = 0, lastTrendMs = 0;
static float vTrendRef = 0;

static void updateBaterai()
{
    unsigned long now = millis();
    if (vFilt > 0 && now - lastSampleMs < 500)
        return;
    lastSampleMs = now;

    float v = bacaTeganganBaterai();
    vFilt = (vFilt == 0) ? v : (vFilt * 0.9f + v * 0.1f); // smoothing agar tidak naik-turun

    // Deteksi charging
#if PIN_CHARGING >= 0
    sedangCharging = (digitalRead(PIN_CHARGING) == LOW); // TP4056 CHRG aktif LOW
#else
    // Tanpa pin charge: deteksi dari tren tegangan tiap 10 detik
    if (vTrendRef == 0)
    {
        vTrendRef = vFilt;
        lastTrendMs = now;
    }
    if (now - lastTrendMs >= 10000)
    {
        float d = vFilt - vTrendRef;
        if (d >= 0.02f)
            sedangCharging = true;
        else if (d <= -0.02f)
            sedangCharging = false;
        else if (sedangCharging && vFilt < 4.1f && d < 0.005f)
            sedangCharging = false;
        vTrendRef = vFilt;
        lastTrendMs = now;
    }
#endif

    // Persen hanya turun saat discharge, hanya naik saat charging
    int p = hitungPersenBaterai(vFilt);
    if (persenTampil < 0)
        persenTampil = p;
    else if (sedangCharging)
        persenTampil = max(persenTampil, p);
    else
        persenTampil = min(persenTampil, p);

    Serial.printf("[Battery] %.2f V | %d%% | %s\n", vFilt, persenTampil,
                  sedangCharging ? "charging" : "discharge");
}

// Gambar status baterai di baris bawah layar
void tampilkanBaterai()
{
    updateBaterai();

    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 56);
    if (sedangCharging)
        display.print(F("CHG "));
    display.print(persenTampil);
    display.print(F("%"));

    const int x = 98, y = 56, w = 26, h = 8;
    display.drawRect(x, y, w, h, SH110X_WHITE);
    display.fillRect(x + w, y + 2, 2, h - 4, SH110X_WHITE); // kutub +

    int isi = map(persenTampil, 0, 100, 0, w - 4);
    if (sedangCharging)
    {
        // Animasi: isi bergerak dari level saat ini sampai penuh, lalu ulang
        int frame = (millis() / 300) % 5; // 0..4
        isi = isi + ((w - 4 - isi) * frame) / 4;

        // Ikon petir di kiri baterai
        display.drawLine(92, 56, 89, 60, SH110X_WHITE);
        display.drawLine(89, 60, 93, 60, SH110X_WHITE);
        display.drawLine(93, 60, 90, 63, SH110X_WHITE);
    }
    display.fillRect(x + 2, y + 2, isi, h - 4, SH110X_WHITE);
}

// ---------------- Helper teks ----------------

// Ambil nama utama (buang nama Latin dalam kurung) dan potong sesuai lebar
static String ringkas(const char *s, size_t maxLen)
{
    String t = s;
    int p = t.indexOf('(');
    if (p > 0)
        t = t.substring(0, p);
    t.trim();
    if (t.length() > maxLen)
        t = t.substring(0, maxLen - 1) + ".";
    return t;
}

// Cetak maksimal 2 baris (21 karakter per baris)
static void cetak2Baris(const char *s, int y)
{
    String t = s;
    int p = t.indexOf('(');
    if (p > 0)
        t = t.substring(0, p);
    t.trim();
    display.setTextSize(1);
    display.setCursor(0, y);
    display.print(t.substring(0, 21));
    if (t.length() > 21)
    {
        display.setCursor(0, y + 10);
        display.print(ringkas(t.substring(21).c_str(), 21));
    }
}

// ---------------- Layar ----------------

void initDisplay()
{
    pinMode(PIN_BATERAI, INPUT);
#if PIN_CHARGING >= 0
    pinMode(PIN_CHARGING, INPUT_PULLUP);
#endif
    analogSetPinAttenuation(PIN_BATERAI, ADC_11db); // range sampai ~3.1V

    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    delay(100); // Jeda pendek agar power OLED stabil
    if (!display.begin(0x3C, true))
    {
        Serial.println(F("OLED tidak ditemukan!"));
        return;
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(30, 28);
    display.println(F("TANOVA AI"));
    display.display();
}

void displayShowWiFiConnecting(const char *ssid)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 8);
    display.println(F("Menghubungkan WiFi"));
    display.setCursor(0, 24);
    display.println(ssid);
    tampilkanBaterai();
    display.display();
}

void displayShowWiFiConnected(IPAddress ip)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 8);
    display.println(F("WiFi Terhubung"));
    display.setCursor(0, 24);
    display.println(ip.toString());
    tampilkanBaterai();
    display.display();
    delay(1000);
}

void displayShowAnalyzing()
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(10, 20);
    display.println(F("Menganalisis..."));
    display.setCursor(10, 34);
    display.println(F("Mohon tunggu"));
    tampilkanBaterai();
    display.display();
}

// Layar utama: jarak, status tanaman, hasil singkat, baterai
void displayUpdate(int secondsLeft, const char *disease, float confidence)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);

    // Baris 1: judul
    display.setCursor(0, 0);
    display.print(F("TANOVA AI"));

    // Baris 2: hitung mundur scan berikutnya
    display.setCursor(0, 10);
    display.printf("Scan lagi: %ds", secondsLeft);
    display.drawFastHLine(0, 20, SCREEN_WIDTH, SH110X_WHITE);

    // Hasil deteksi singkat
    String d = disease;
    int pct = (int)(confidence * 100.0f + 0.5f);
    bool sehat = (d == "Healthy/Clear");
    bool info = (d == "Standby" || d == "Bukan Tanaman" || d == "API Error");

    if (sehat)
    {
        display.setTextSize(2);
        display.setCursor(0, 25);
        display.printf("SEHAT %d%%", pct);
        display.setTextSize(1);
        display.setCursor(0, 44);
        display.print(F("Tanaman sehat"));
    }
    else if (info)
    {
        display.setTextSize(1);
        display.setCursor(0, 30);
        display.print(d);
    }
    else
    {
        display.setTextSize(2);
        display.setCursor(0, 25);
        display.printf("SAKIT %d%%", pct);
        display.setTextSize(1);
        display.setCursor(0, 44);
        display.print(ringkas(disease, 21));
    }

    tampilkanBaterai();
    display.display();
}

// Layar peringatan saat penyakit terdeteksi
void displayShowDiseaseAlert(const char *diseaseName, float confidence)
{
    display.clearDisplay();

    display.fillRect(0, 0, SCREEN_WIDTH, 12, SH110X_WHITE);
    display.setTextSize(1);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(18, 2);
    display.print(F("! PENYAKIT TANAMAN !"));

    display.setTextColor(SH110X_WHITE);
    cetak2Baris(diseaseName, 18);

    display.setTextSize(1);
    display.setCursor(0, 44);
    display.printf("Keyakinan: %d%%", (int)(confidence * 100.0f + 0.5f));

    tampilkanBaterai();
    display.display();
}