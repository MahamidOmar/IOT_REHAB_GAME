#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// TFT Display Pins (Configured in TFT_eSPI's User_Setup.h)
// ------------------------------------------------
// Normally defined in User_Setup.h, but shown here for reference:
// #define TFT_MISO 19  // (if using display SPI)
// #define TFT_MOSI 23
// #define TFT_SCLK 18
// #define TFT_CS   15
// #define TFT_DC   2
// #define TFT_RST  4

// Touchscreen Pins (XPT2046)
// ------------------------------------------------
#define XPT2046_IRQ  19   // T_IRQ
#define XPT2046_MISO 18  // T_DO (Note: Often shares TFT_MISO)
#define XPT2046_MOSI 5  // T_DIN
#define XPT2046_CS   4  // T_CS
#define XPT2046_CLK  2  // T_CLK

// Initialize hardware
SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  
  // Start TFT Display
  tft.init();
  tft.setRotation(1); // Landscape
  tft.fillScreen(TFT_WHITE);
  
  // Start Touchscreen SPI
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(1); // Match TFT orientation

  // Display "Hello World"
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.drawCentreString("Hello World", tft.width()/2, tft.height()/2, 2);
}

void loop() {
  // Optional: Add touch handling here
  if(touchscreen.touched()) {
    TS_Point p = touchscreen.getPoint();
    Serial.printf("Touch: X=%d Y=%d\n", p.x, p.y);
  }
}
