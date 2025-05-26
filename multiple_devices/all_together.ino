#include <Arduino_GFX_Library.h>

// TFT Display pin definitions
#define TFT_SCLK  18
#define TFT_MOSI  23
#define TFT_MISO  19
#define TFT_CS    15
#define TFT_DC    21
#define TFT_RST   2

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 320

// Set up the SPI bus and display using your variable names
Arduino_ESP32SPI bus(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, TFT_MISO);
Arduino_ILI9341 display(&bus, TFT_RST);

void setup() {
  display.begin();
  display.setRotation(0); // Portrait: 240x320
  display.fillScreen(WHITE);

  display.setTextColor(BLACK);
  display.setTextSize(2);

  const char* msg = "Hello World";
  int16_t x1, y1;
  uint16_t w, h;

  // Calculate text bounds to center the text
  display.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (SCREEN_WIDTH - w) / 2;
  int16_t y = (SCREEN_HEIGHT - h) / 2;

  display.setCursor(x, y);
  display.print(msg);
}

void loop() {
  // Nothing needed here
}
