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

// Set up the SPI bus and display
Arduino_ESP32SPI bus(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, TFT_MISO);
Arduino_ILI9341 display(&bus, TFT_RST);

// Buffer for storing 3 keys
char inputBuffer[4]; // 3 chars + null terminator
byte inputIndex = 0;

// Store the random number as a string for comparison
char randomNumberStr[4];

// State machine
enum State { MENU, CODE_BREAKER };
State currentState = MENU;

// --- Display helpers ---
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

void showMenuMessage(const char* msg) {
  // Clear message area (bottom of the screen)
  int msgHeight = 40;
  display.fillRect(0, SCREEN_HEIGHT - msgHeight, SCREEN_WIDTH, msgHeight, WHITE);
  display.setTextColor(BLUE);
  display.setTextSize(2);
  display.setCursor(20, SCREEN_HEIGHT - msgHeight + 10);
  display.print(msg);
}

void showCodeBreakerTitle() {
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);
  display.setCursor(30, 100);
  display.print("Code breaker game");
  delay(2000); // Show for 2 seconds
  display.fillScreen(WHITE);
}

void generateNewRandomNumber() {
  int randomNumber = random(0, 1000);
  snprintf(randomNumberStr, sizeof(randomNumberStr), "%03d", randomNumber);

  // Print the new random number to the Serial Monitor
  Serial.print("New random number: ");
  Serial.println(randomNumberStr);

  // Clear just the area where the stars are displayed
  int charWidth = 6 * 2;
  int charHeight = 8 * 2;
  int numChars = 3;
  int textWidth = charWidth * numChars;
  int textHeight = charHeight;

  // Move the stars higher up (e.g., 40 pixels from the top)
  int x = (SCREEN_WIDTH - textWidth) / 2;
  int y = 40;

  display.fillRect(x, y, textWidth, textHeight, WHITE); // Clear previous
  display.setCursor(x, y);
  display.setTextColor(BLACK);
  display.setTextSize(2);
  display.print("***");
}

void showCodeBreakerResult(const char* msg) {
  // Show result message below the stars
  int y = 90;
  display.fillRect(0, y, SCREEN_WIDTH, 30, WHITE);
  display.setCursor(20, y);
  display.setTextColor(BLUE);
  display.setTextSize(2);
  display.print(msg);
}

void setup() {
  Serial.begin(9600);
  display.begin();
  display.setRotation(0); // Portrait: 240x320
  randomSeed(analogRead(0));
  showMenu();
  currentState = MENU;
}

void loop() {
  char key = keypad.getKey();
  switch (currentState) {
    case MENU:
      if (key) {
        Serial.print("Key pressed: ");
        Serial.println(key);

        if (key == '1') {
          showCodeBreakerTitle();
          generateNewRandomNumber();
          inputIndex = 0;
          currentState = CODE_BREAKER;
        } else if (key == '2') {
          showMenuMessage("Not implemented yet");
        } else {
          showMenuMessage("Please choose 1 or 2");
        }
      }
      break;

    case CODE_BREAKER:
      if (key) {
        if (inputIndex < 3 && key >= '0' && key <= '9') {
          inputBuffer[inputIndex++] = key;
        }
        if (inputIndex == 3) {
          inputBuffer[3] = '\0'; // Null-terminate

          if (strcmp(inputBuffer, randomNumberStr) == 0) {
            showCodeBreakerResult("Success");
            Serial.println("You won!");
            delay(1500);
            generateNewRandomNumber(); // Generate and display a new number
          } else {
            // Compare inputBuffer to randomNumberStr for matches
            int matchCount = 0;
            for (int i = 0; i < 3; i++) {
              if (inputBuffer[i] == randomNumberStr[i]) {
                matchCount++;
              }
            }
            char buf[32];
            snprintf(buf, sizeof(buf), "Matches: %d", matchCount);
            showCodeBreakerResult(buf);

            Serial.print("Input: ");
            Serial.print(inputBuffer);
            Serial.print(" | Random: ");
            Serial.print(randomNumberStr);
            Serial.print(" | Matches: ");
            Serial.println(matchCount);

            // Do NOT generate a new number, let user try again
          }
          inputIndex = 0; // Reset for next 3 keys
        }
      }
      break;
  }
}
