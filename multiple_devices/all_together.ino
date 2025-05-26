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

// Set up the SPI bus and display
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

void showMenu() {
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);

  int y = 40;
  display.setCursor(20, y);
  display.print("Choose your game:");
  y += 40;
  display.setCursor(20, y);
  display.print("1) Code breaker");
  y += 30;
  display.setCursor(20, y);
  display.print("2) Visual memory");
}

void showMessage(const char* msg) {
  // Clear message area (bottom of the screen)
  int msgHeight = 40;
  display.fillRect(0, SCREEN_HEIGHT - msgHeight, SCREEN_WIDTH, msgHeight, WHITE);
  display.setTextColor(BLUE);
  display.setTextSize(2);
  display.setCursor(20, SCREEN_HEIGHT - msgHeight + 10);
  display.print(msg);
}

void setup() {
  Serial.begin(9600);
  display.begin();
  display.setRotation(0); // Portrait: 240x320
  showMenu();
}

void loop() {
  char key = keypad.getKey();
  if (key) {
    Serial.print("Key pressed: ");
    Serial.println(key);

    if (key == '1' || key == '2') {
      showMessage("Good selection");
    } else {
      showMessage("Please choose 1 or 2");
    }
  }
}
