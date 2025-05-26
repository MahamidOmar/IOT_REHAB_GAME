#include <Arduino_GFX_Library.h>
#include <Keypad.h>

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

// Keypad definitions
const byte ROWS = 4;
const byte COLS = 3;

char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};

byte rowPins[ROWS] = {13, 12, 14, 27};
byte colPins[COLS] = {25, 33, 32};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

void setup() {
  Serial.begin(9600);

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

  // Draw a label for the key display area
  display.setTextSize(1);
  display.setTextColor(DARKGREY);
  display.setCursor((SCREEN_WIDTH - 60) / 2, y + 40);
  display.print("Last key pressed:");
}

void loop() {
  char key = keypad.getKey();
  if (key) {
    Serial.print("Key pressed: ");
    Serial.println(key);

    // Clear the area where the key is displayed
    int keyBoxWidth = 32;
    int keyBoxHeight = 32;
    int keyBoxX = (SCREEN_WIDTH - keyBoxWidth) / 2;
    int keyBoxY = (SCREEN_HEIGHT / 2) + 60;

    display.fillRect(keyBoxX, keyBoxY, keyBoxWidth, keyBoxHeight, WHITE);

    // Draw the key in large font, centered in the box
    display.setTextSize(3);
    display.setTextColor(BLACK);
    int16_t x1, y1;
    uint16_t w, h;
    char keyStr[2] = {key, '\0'};
    display.getTextBounds(keyStr, 0, 0, &x1, &y1, &w, &h);
    int textX = keyBoxX + (keyBoxWidth - w) / 2;
    int textY = keyBoxY + (keyBoxHeight - h) / 2;
    display.setCursor(textX, textY);
    display.print(keyStr);
  }
}
