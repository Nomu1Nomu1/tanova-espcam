#pragma once
#include <Arduino.h>

// TF-Luna LiDAR Pin Configuration
// IMPORTANT: Do NOT use GPIO 17 & 18 because they are occupied by Camera Y8 & Y7!
#define LUNA_RX_PIN 1   // ESP32 RX (GPIO 1) <- Connect to TF-Luna TX
#define LUNA_TX_PIN 3   // ESP32 TX (GPIO 3) -> Connect to TF-Luna RX
#define LUNA_BAUDRATE 115200

// Hydroponic detection range threshold (in cm)
// When a plant stick is placed within this distance range, AI disease detection will trigger
#define PLANT_MIN_DIST_CM 10
#define PLANT_MAX_DIST_CM 45
#define PLANT_MIN_STRENGTH 100 // Ignore weak signals / out of range

struct TFLunaData {
    uint16_t distance = 0; // cm
    uint16_t strength = 0; // signal strength
    float temp_c = 0.0;    // temperature
    bool valid = false;
};

void initLidar();
bool readTFLuna(TFLunaData &data);

