#include "lidar.h"

HardwareSerial LunaSerial(1); // Use Hardware UART1

void initLidar()
{
    LunaSerial.begin(LUNA_BAUDRATE, SERIAL_8N1, LUNA_RX_PIN, LUNA_TX_PIN);
    Serial.printf("[LiDAR] TF-Luna initialized on RX (GPIO %d), TX (GPIO %d) @ %d baud\n",
                  LUNA_RX_PIN, LUNA_TX_PIN, LUNA_BAUDRATE);
    Serial.printf("[LiDAR] Hydroponic plant stick trigger range: %d cm - %d cm\n",
                  PLANT_MIN_DIST_CM, PLANT_MAX_DIST_CM);
}

bool readTFLuna(TFLunaData &data)
{
    // TF-Luna Frame (9 bytes):
    // 0x59 0x59 Dist_L Dist_H Strength_L Strength_H Temp_L Temp_H Checksum
    while (LunaSerial.available() >= 9)
    {
        if (LunaSerial.read() == 0x59)
        {
            if (LunaSerial.peek() == 0x59)
            {
                LunaSerial.read(); // consume second 0x59
                uint8_t buf[7];
                LunaSerial.readBytes(buf, 7);

                uint8_t checksum = 0x59 + 0x59;
                for (int i = 0; i < 6; i++)
                {
                    checksum += buf[i];
                }

                if (checksum == buf[6])
                {
                    data.distance = buf[0] | (buf[1] << 8);
                    data.strength = buf[2] | (buf[3] << 8);
                    data.temp_c = ((buf[4] | (buf[5] << 8)) / 8.0f) - 256.0f;
                    data.valid = true;
                    return true;
                }
            }
        }
    }
    return false;
}