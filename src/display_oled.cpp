#include "display_oled.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

static Adafruit_SH1106G display(128, 64, &Wire, -1);
static bool displayReady = false;
static String currentIpStr = "Connecting...";

void initDisplay()
{
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    
    // Try initializing SH1106 at standard address (0x3C)
    if (!display.begin(OLED_I2C_ADDR, true))
    {
        Serial.println("[OLED] Failed to initialize SH1106 at 0x3C, trying 0x3D...");
        if (!display.begin(0x3D, true))
        {
            Serial.println("[OLED] SH1106 OLED not detected on I2C bus!");
            displayReady = false;
            return;
        }
    }

    displayReady = true;
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    Serial.println("[OLED] 1.3\" SH1106 OLED initialized successfully!");

    displayShowBooting();
}

void displayShowBooting()
{
    if (!displayReady) return;
    display.clearDisplay();
    display.fillRect(0, 0, 128, 12, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(10, 2);
    display.print("TANOVA ESP32-CAM");

    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 24);
    display.println("System Booting...");
    display.println("Initializing Camera");
    display.println("Starting LiDAR...");
    display.display();
}

void displayShowWiFiConnecting(const char *ssid)
{
    if (!displayReady) return;
    display.clearDisplay();
    display.fillRect(0, 0, 128, 12, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(10, 2);
    display.print("TANOVA ESP32-CAM");

    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 24);
    display.println("Connecting to WiFi:");
    display.printf("SSID: %s\n", ssid);
    display.println("Waiting for IP...");
    display.display();
}

void displayShowWiFiConnected(const IPAddress &ip)
{
    currentIpStr = ip.toString();
    if (!displayReady) return;
    display.clearDisplay();
    display.fillRect(0, 0, 128, 12, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(10, 2);
    display.print("TANOVA ESP32-CAM");

    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 22);
    display.println("WiFi: CONNECTED");
    display.printf("IP: %s\n", currentIpStr.c_str());
    display.println("Stream port: :81");
    display.display();
}

void displayUpdate(int distanceCm, int strength, bool inRange, const char *diseaseResult, float confidence)
{
    if (!displayReady) return;

    display.clearDisplay();

    // Top Title Bar
    display.fillRect(0, 0, 128, 11, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(4, 2);
    display.print("TANOVA ESP32-S3");

    // IP Address
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 15);
    display.printf("IP: %s\n", currentIpStr.c_str());

    // LiDAR distance status
    display.setCursor(0, 27);
    if (distanceCm < 0) {
        display.print("LiDAR: No Data");
    } else {
        display.printf("LiDAR: %d cm ", distanceCm);
        if (inRange) {
            display.print("[LOCKED]");
        } else {
            display.print("[WAIT]");
        }
    }

    // AI Disease Status
    display.setCursor(0, 41);
    if (diseaseResult == nullptr || strlen(diseaseResult) == 0) {
        display.print("AI: Standby...");
    } else {
        display.printf("AI: %.16s\n", diseaseResult);
        display.setCursor(0, 52);
        display.printf("Conf: %.1f%%", confidence * 100.0f);
    }

    // Bottom decorative line
    display.drawLine(0, 63, 127, 63, SH110X_WHITE);

    display.display();
}

void displayShowDiseaseAlert(const char *diseaseName, float confidence)
{
    if (!displayReady) return;

    display.clearDisplay();
    display.fillRect(0, 0, 128, 14, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(12, 3);
    display.print("!! DISEASE ALERT !!");

    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 24);
    display.println("Detected:");
    display.setTextSize(1);
    display.printf("%s\n", diseaseName ? diseaseName : "Unknown");
    display.printf("Confidence: %.1f%%\n", confidence * 100.0f);
    display.println("Action: Sound Alert");

    display.display();
}
