#pragma once
#include <Arduino.h>

// ==========================================
// OLED 1.3" Display Configuration (SH1106 / SH110X)
// ==========================================
// NOTE: 1.3" OLEDs sold as "SSD1306" are almost always SH1106 controllers.
// Using SH110X driver fixes the 2-pixel shift and edge noise bugs.

// Choose free ESP32-S3 GPIO pins for I2C (Avoid pins used by Camera and LiDAR)
#define OLED_SDA_PIN 47  // Change to your desired I2C SDA pin
#define OLED_SCL_PIN 21  // Change to your desired I2C SCL pin
#define OLED_I2C_ADDR 0x3C // Default I2C address for 1.3" OLED (sometimes 0x3D)

void initDisplay();
void displayShowBooting();
void displayShowWiFiConnecting(const char *ssid);
void displayShowWiFiConnected(const IPAddress &ip);
void displayUpdate(int distanceCm, int strength, bool inRange, const char *diseaseResult, float confidence);
void displayShowDiseaseAlert(const char *diseaseName, float confidence);
