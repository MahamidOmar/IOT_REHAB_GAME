#include <Arduino_GFX_Library.h>
#include <Keypad.h>

// --- PIN DEFINITIONS (TOP) ---
#define TFT_SCLK  18
#define TFT_MOSI  23
#define TFT_MISO  19
#define TFT_CS    15
#define TFT_DC    21
#define TFT_RST   2

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 320

// RGB Button pin definitions
#define RED_BUTTON_PIN    4
#define BLUE_BUTTON_PIN   22
#define GREEN_BUTTON_PIN  26

// Visual Memory Game
#define MAX_SEQUENCE_LENGTH 10
const char* colorNames[] = {"Red", "Blue", "Green"};
uint16_t colorValues[] = {RED, BLUE, GREEN}; // TFT color values

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

// Player selection
byte currentPlayer = 0;

// Visual memory game sequence
uint8_t colorSequence[MAX_SEQUENCE_LENGTH];
uint8_t colorSequenceLength = 5; // For example, 5 steps
uint8_t userSequence[MAX_SEQUENCE_LENGTH];
uint8_t userInputIndex = 0;

// State machine
enum State { PLAYER_SELECT, MENU, CODE_BREAKER, VISUAL_MEMORY, VISUAL_MEMORY_INPUT, VISUAL_MEMORY_RESULT };
State currentState = PLAYER_SELECT;

// --- Display helpers ---
void showBottomHints() {
  display.setTextSize(1);
  display.setTextColor(DARKGREY);
  int y = SCREEN_HEIGHT - 10;
  display.setCursor(0, y);
  display.print("* games menu");
  const char* logoutText = "# logout";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(logoutText, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(SCREEN_WIDTH - w, y);
  display.print(logoutText);
}

void showPlayerMenu() {
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);
  display.setCursor(20, 60);
  display.print("Select player:");
  display.setCursor(20, 100);
  display.print("1) Player 1");
  display.setCursor(20, 130);
  display.print("2) Player 2");
  display.setCursor(20, 160);
  display.print("3) Player 3");
  display.setCursor(20, 190);
  display.print("4) Player 4");
}

void showPlayerSelected(byte player) {
  display.fillScreen(WHITE);
  display.setTextColor(GREEN);
  display.setTextSize(2);
  display.setCursor(20, 100);
  display.print("Player ");
  display.print(player);
  display.print(" selected");
  delay(1200);
}

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
  showBottomHints();
}

void showMenuMessage(const char* msg) {
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
  delay(2000);
  display.fillScreen(WHITE);
  showBottomHints();
}

void generateNewRandomNumber() {
  int randomNumber = random(0, 1000);
  snprintf(randomNumberStr, sizeof(randomNumberStr), "%03d", randomNumber);
  Serial.print("New random number: ");
  Serial.println(randomNumberStr);
  int charWidth = 6 * 2;
  int charHeight = 8 * 2;
  int numChars = 3;
  int textWidth = charWidth * numChars;
  int textHeight = charHeight;
  int x = (SCREEN_WIDTH - textWidth) / 2;
  int y = 40;
  display.fillRect(x, y, textWidth, textHeight, WHITE);
  display.setCursor(x, y);
  display.setTextColor(BLACK);
  display.setTextSize(2);
  display.print("***");
  display.fillRect(0, 90, SCREEN_WIDTH, 90, WHITE);
  showBottomHints();
}

void showCodeBreakerResult(int exact, int partial) {
  int y = 90;
  display.fillRect(0, y, SCREEN_WIDTH, 60, WHITE);
  display.setTextSize(2);
  display.setTextColor(GREEN);
  display.setCursor(20, y);
  display.print("Exact: ");
  display.print(exact);
  display.setTextColor(YELLOW);
  display.setCursor(20, y + 30);
  display.print("Partial: ");
  display.print(partial);
}

void showLastTry(const char* guess) {
  int y = 150;
  display.fillRect(0, y, SCREEN_WIDTH, 30, WHITE);
  display.setTextColor(DARKGREY);
  display.setTextSize(2);
  display.setCursor(20, y);
  display.print("Last try: ");
  display.print(guess);
}

void showGeneratingNew() {
  int y = 150;
  display.fillRect(0, y, SCREEN_WIDTH, 30, WHITE);
  display.setTextColor(GREEN);
  display.setTextSize(2);
  display.setCursor(20, y);
  display.print("Generating new...");
}

// --- Matching logic ---
void countMatches(const char* guess, const char* answer, int& exact, int& partial) {
  exact = 0;
  partial = 0;
  bool answer_used[3] = {false, false, false};
  bool guess_used[3] = {false, false, false};
  for (int i = 0; i < 3; i++) {
    if (guess[i] == answer[i]) {
      exact++;
      answer_used[i] = true;
      guess_used[i] = true;
    }
  }
  for (int i = 0; i < 3; i++) {
    if (guess_used[i]) continue;
    for (int j = 0; j < 3; j++) {
      if (!answer_used[j] && guess[i] == answer[j]) {
        partial++;
        answer_used[j] = true;
        break;
      }
    }
  }
}

// --- Visual Memory Game: Generate, show, and check sequence ---
void generateRandomColorSequence(uint8_t sequence[], uint8_t length) {
  for (uint8_t i = 0; i < length; i++) {
    sequence[i] = random(0, 3); // 0 = Red, 1 = Blue, 2 = Green
  }
  // Print to Serial
  Serial.print("Color sequence: ");
  for (uint8_t i = 0; i < length; i++) {
    Serial.print(colorNames[sequence[i]]);
    if (i < length - 1) Serial.print(", ");
  }
  Serial.println();
}

void showColorOnDisplay(uint8_t colorIndex) {
  display.fillScreen(colorValues[colorIndex]);
  display.setTextSize(3);
  display.setTextColor(WHITE);
  display.setCursor(60, 140);
  display.print(colorNames[colorIndex]);
}

void showVisualMemoryResult(bool correct) {
  display.fillScreen(WHITE);
  display.setTextSize(2);
  if (correct) {
    display.setTextColor(GREEN);
    display.setCursor(40, 120);
    display.print("Correct!");
  } else {
    display.setTextColor(RED);
    display.setCursor(40, 120);
    display.print("Wrong!");
  }
  showBottomHints();
  delay(2000);
  showMenu();
  currentState = MENU;
}

void setup() {
  Serial.begin(9600);
  display.begin();
  display.setRotation(0);
  randomSeed(analogRead(0));
  showPlayerMenu();
  currentState = PLAYER_SELECT;

  pinMode(RED_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BLUE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(GREEN_BUTTON_PIN, INPUT_PULLUP);
}

void loop() {
  char key = keypad.getKey();
  switch (currentState) {
    case PLAYER_SELECT:
      if (key && key >= '1' && key <= '4') {
        currentPlayer = key - '0';
        showPlayerSelected(currentPlayer);
        showMenu();
        currentState = MENU;
      }
      break;

    case MENU:
      if (key) {
        if (key == '#') {
          showPlayerMenu();
          currentState = PLAYER_SELECT;
          inputIndex = 0;
          break;
        }
        Serial.print("Key pressed: ");
        Serial.println(key);

        if (key == '1') {
          showCodeBreakerTitle();
          generateNewRandomNumber();
          inputIndex = 0;
          currentState = CODE_BREAKER;
        } else if (key == '2') {
          // Visual memory game: generate sequence, show on screen, then wait for input
          display.fillScreen(WHITE);
          display.setTextColor(BLUE);
          display.setTextSize(2);
          display.setCursor(20, 100);
          display.print("Visual memory game");
          showBottomHints();
          delay(1000);

          generateRandomColorSequence(colorSequence, colorSequenceLength);
          for (uint8_t i = 0; i < colorSequenceLength; i++) {
            showColorOnDisplay(colorSequence[i]);
            delay(2000);
          }
          display.fillScreen(WHITE);
          display.setTextColor(BLACK);
          display.setTextSize(2);
          display.setCursor(20, 100);
          display.print("Repeat the sequence!");
          showBottomHints();
          userInputIndex = 0;
          currentState = VISUAL_MEMORY_INPUT;
        } else {
          showMenuMessage("Please choose 1 or 2");
        }
      }
      break;

    case VISUAL_MEMORY_INPUT:
      // Read RGB buttons
      if (userInputIndex < colorSequenceLength) {
        if (digitalRead(RED_BUTTON_PIN) == LOW) {
          userSequence[userInputIndex++] = 0;
          while (digitalRead(RED_BUTTON_PIN) == LOW); // Wait for release
        } else if (digitalRead(BLUE_BUTTON_PIN) == LOW) {
          userSequence[userInputIndex++] = 1;
          while (digitalRead(BLUE_BUTTON_PIN) == LOW);
        } else if (digitalRead(GREEN_BUTTON_PIN) == LOW) {
          userSequence[userInputIndex++] = 2;
          while (digitalRead(GREEN_BUTTON_PIN) == LOW);
        }
      }
      if (userInputIndex == colorSequenceLength) {
        // Check result
        bool correct = true;
        for (uint8_t i = 0; i < colorSequenceLength; i++) {
          if (userSequence[i] != colorSequence[i]) {
            correct = false;
            break;
          }
        }
        showVisualMemoryResult(correct);
      }
      break;

    case CODE_BREAKER:
      if (key) {
        if (key == '*') {
          showMenu();
          currentState = MENU;
          inputIndex = 0;
          break;
        }
        if (key == '#') {
          showPlayerMenu();
          currentState = PLAYER_SELECT;
          inputIndex = 0;
          break;
        }
        if (inputIndex < 3 && key >= '0' && key <= '9') {
          inputBuffer[inputIndex++] = key;
        }
        if (inputIndex == 3) {
          inputBuffer[3] = '\0';
          if (strcmp(inputBuffer, randomNumberStr) == 0) {
            showCodeBreakerResult(3, 0);
            showGeneratingNew();
            Serial.println("You won!");
            delay(1500);
            generateNewRandomNumber();
          } else {
            int exact = 0, partial = 0;
            countMatches(inputBuffer, randomNumberStr, exact, partial);
            showCodeBreakerResult(exact, partial);
            showLastTry(inputBuffer);
            Serial.print("Input: ");
            Serial.print(inputBuffer);
            Serial.print(" | Random: ");
            Serial.print(randomNumberStr);
            Serial.print(" | Exact: ");
            Serial.print(exact);
            Serial.print(" | Partial: ");
            Serial.println(partial);
          }
          inputIndex = 0;
        }
      }
      break;
  }
}
