#include <Arduino.h>

#define LUNA_RX 18   // ESP32 RX <- TF-Luna TX
#define LUNA_TX 17   // ESP32 TX -> TF-Luna RX
HardwareSerial LunaSerial(2); // UART2

uint16_t distance_cm = 0;
uint16_t strength = 0;
int16_t temp_c = 0;

bool readTFLuna() {
  // Frame: 0x59 0x59 Dist_L Dist_H Strength_L Strength_H Temp_L Temp_H Checksum
  if (LunaSerial.available() >= 9) {
    if (LunaSerial.read() == 0x59 && LunaSerial.read() == 0x59) {
      uint8_t buf[7];
      LunaSerial.readBytes(buf, 7);

      uint8_t checksum = 0x59 + 0x59;
      for (int i = 0; i < 6; i++) checksum += buf[i];

      if (checksum == buf[6]) {
        distance_cm = buf[0] | (buf[1] << 8);
        strength    = buf[2] | (buf[3] << 8);
        temp_c      = (buf[4] | (buf[5] << 8)) / 8 - 256;
        return true;
      }
    }
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  LunaSerial.begin(115200, SERIAL_8N1, LUNA_RX, LUNA_TX);
  delay(200);
  Serial.println("TF-Luna ready");
}

void loop() {
  if (readTFLuna()) {
    // strength < 100 or > 60000 means unreliable reading (too close/far/weak signal)
    if (strength > 100) {
      Serial.printf("Distance: %d cm | Strength: %d | Temp: %.1f C\n",
                     distance_cm, strength, temp_c / 100.0);
    } else {
      Serial.println("Weak signal, ignoring reading");
    }
  }
  delay(100); // TF-Luna default output rate is 100Hz, throttle as needed
}