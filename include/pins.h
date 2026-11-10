#pragma once
#include <Arduino.h>

// Pin sistem daya & aksesoris (ESP32-S3-CAM, bebas dari pin kamera)
const int PIN_TOMBOL  = 1;  // Push Button ke GND
const int PIN_KIPAS   = 2;  // Resistor 1k -> Transistor (Kipas)
const int PIN_BATERAI = 3;  // ADC1, dari pembagi tegangan baterai
const int PIN_BUZZER  = 41; // Buzzer (+)

// Pin status charging (opsional, mis. CHRG dari TP4056, aktif LOW). -1 = tidak ada,
// status charging dideteksi dari tren tegangan.
#define PIN_CHARGING -1

// I2C OLED SH1106
const int PIN_OLED_SDA = 14;
const int PIN_OLED_SCL = 21;

// Pengaturan baterai (LiPo/Li-ion 1S)
const float RASIO_DIVIDER = 2.0;  // R1 = R2 = 100k -> tegangan dibagi 2
const float V_BAT_KOSONG  = 3.30; // 0%
const float V_BAT_PENUH   = 4.20; // 100%