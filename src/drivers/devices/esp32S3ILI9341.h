#ifndef _ESP32_S3_ILI9341
#define _ESP32_S3_ILI9341

#define PIN_BUTTON_1 0   // bouton BOOT : clic = écran suivant, double-clic = rotation, 5s = reset config WiFi

// Tactile résistif XPT2046 (bus SPI séparé du bus SPI de l'écran, géré
// directement dans esp32S3ILI9341Driver.cpp — pas via le TouchHandler générique)
#define TOUCH_CLK    18   // T_CLK
#define ETOUCH_CS    15   // T_CS
#define TOUCH_MOSI   17   // T_DIN
#define TOUCH_MISO   16   // T_DO
#define TOUCH_IRQ    21   // T_IRQ

#endif