#include "common.h"
#include "network.h"

IMU mpu;
SdCard tf;
Pixel rgb;
// Config g_cfg;       // 全局配置文件
Network g_network;  // 网络连接
FlashFS g_flashCfg; // flash中的文件系统（替代原先的Preferences）
Display screen;     // 屏幕对象
Ambient ambLight;   // 光纤传感器对象

boolean doDelayMillisTime(unsigned long interval, unsigned long *previousMillis, boolean state)
{
    unsigned long currentMillis = millis();
    if (currentMillis - *previousMillis >= interval)
    {
        *previousMillis = currentMillis;
        state = !state;
    }
    return state;
}

int dateDiff(struct tm* date1, struct tm* date2)
{
    int y1, m1, d1;
    int y2, m2, d2;
    m1 = (date1->tm_mon + 9) % 12;
    y1 = (date1->tm_year - m1/10);
    d1 = 365 * y1 + y1/4 -y1/100 + y1/400 + (m1*306+5)/10 + (date1->tm_mday - 1);

    m2 = (date2->tm_mon +9) % 12;
    if (date2->tm_year == 0) {
        if (date2->tm_mon < date1->tm_mon || (date2->tm_mon == date1->tm_mon && date2->tm_mon < date1->tm_mon)) {
            y2 = date1->tm_year + 1 - m2/10;
        }
        else {
            y2 = date1->tm_year - m2/10;
        }
    }
    else {
        y2 = date2->tm_year - m2/10;
    }
    d2 = 365*y2 +y2/4 -y2/100 + y2/400 +(m2*306+5)/10 + (date2->tm_mday - 1);
    return (d2 -d1);
}


bool tmfromString(const char *date_str, struct tm *date)
{
    // TODO: add support for "a", "a.b", "a.b.c" formats

    uint16_t acc = 0; // Accumulator
    uint8_t dots = 0;

    while (*date_str)
    {
        char c = *date_str++;
        if (c >= '0' && c <= '9')
        {
            acc = acc * 10 + (c - '0');
        }
        else if (c == '.')
        {
            if (dots == 0) {
                date->tm_year = acc;
            }
            else if (dots == 1) {
                date->tm_mon = acc;
            }
            if (dots == 2) {
                // Too much dots (there must be 3 dots)
                return false;
            }
            acc = 0;
            ++dots;
        }
        else
        {
            // Invalid char
            return false;
        }
    }

    if (dots != 2) {
        // Too few dots (there must be 3 dots)
        return false;
    }
    date->tm_mday = acc;
    return true;
}

#if GFX

#include <Arduino_GFX_Library.h>

Arduino_HWSPI *bus = new Arduino_HWSPI(TFT_DC /* DC */, TFT_CS /* CS */, TFT_SCLK, TFT_MOSI, TFT_MISO);
Arduino_ST7789 *tft = new Arduino_ST7789(bus, TFT_RST /* RST */, 3 /* rotation */, true /* IPS */,
                                         240 /* width */, 240 /* height */,
                                         0 /* col offset 1 */, 80 /* row offset 1 */);

#else
#include <TFT_eSPI.h>
/*
TFT pins should be set in path/to/Arduino/libraries/TFT_eSPI/User_Setups/Setup24_ST7789.h
*/
TFT_eSPI *tft = new TFT_eSPI(240, 280);
#endif