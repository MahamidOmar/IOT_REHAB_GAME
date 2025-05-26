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

// Player selection
byte currentPlayer = 0;

// State machine
enum State { PLAYER_SELECT, MENU, CODE_BREAKER };
State currentState = PLAYER_SELECT;

// --- Display helpers ---
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

  display.fillRect(0, 90, SCREEN_WIDTH, 90, WHITE); // Clear result/last try area
}

void showCodeBreakerResult(int exact, int partial) {
  int y = 90;
  display.fillRect(0, y, SCREEN_WIDTH, 60, WHITE); // Clear both lines

  display.setTextSize(2);

  display.setTextColor(GREEN); // Exact in green
  display.setCursor(20, y);
  display.print("Exact: ");
  display.print(exact);

  display.setTextColor(YELLOW); // Partial in yellow
  display.setCursor(20, y + 30); // Next line
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

  // First pass: exact matches
  for (int i = 0; i < 3; i++) {
    if (guess[i] == answer[i]) {
      exact++;
      answer_used[i] = true;
      guess_used[i] = true;
    }
  }
  // Second pass: partial matches
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

void setup() {
  Serial.begin(9600);
  display.begin();
  display.setRotation(0);
  randomSeed(analogRead(0));
  showPlayerMenu();
  currentState = PLAYER_SELECT;
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
          showMenuMessage("Not implemented yet");
        } else {
          showMenuMessage("Please choose 1 or 2");
        }
      }
      break;

    case CODE_BREAKER:
      if (key) {
        // Navigation
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
        // Normal game logic
        if (inputIndex < 3 && key >= '0' && key <= '9') {
          inputBuffer[inputIndex++] = key;
        }
        if (inputIndex == 3) {
          inputBuffer[3] = '\0';

          if (strcmp(inputBuffer, randomNumberStr) == 0) {
            showCodeBreakerResult(3, 0); // All exact
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
