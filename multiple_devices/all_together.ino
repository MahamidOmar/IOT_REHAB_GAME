#include <Arduino_GFX_Library.h>
#include <Keypad.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_wpa2.h" // Only needed for WPA2-Enterprise

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
#define GREEN_BUTTON_PIN  5

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

char inputBuffer[4]; // 3 chars + null terminator
byte inputIndex = 0;
char randomNumberStr[4];
byte currentPlayer = 0;
uint8_t colorSequence[MAX_SEQUENCE_LENGTH];
uint8_t colorSequenceLength = 5;
uint8_t currentStep = 0;
unsigned long lastDebounceTime[3] = {0, 0, 0};
const unsigned long debounceDelay = 200;
int lastButtonState[3] = {HIGH, HIGH, HIGH};

enum State { PLAYER_SELECT, MENU, CODE_BREAKER, VISUAL_MEMORY, VISUAL_MEMORY_INPUT, VISUAL_MEMORY_RESULT };
State currentState = PLAYER_SELECT;

// WiFi credentials (for WPA2-Enterprise)
#define WIFI_SSID      "eduroam"
#define WIFI_USERNAME  "omarm@campus.technion.ac.il"
#define WIFI_PASSWORD  "JonSnow04112001#"

// Firestore info
const char* projectId   = "iot-rehab-game";
const char* apiKey      = "AIzaSyBXmChDOo054ZKjiiCma295qYqMzf7kjZs";
const char* collection  = "Players";

#define MAX_PLAYERS 4

String playerNames[MAX_PLAYERS];
int playerCount = 0;

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

void showInputProgress(const char* inputBuffer, byte inputIndex) {
  int y = 190;
  int numChars = 3;
  int charWidth = 12;
  int totalWidth = numChars * charWidth + (numChars - 1) * charWidth / 2;
  int x = (SCREEN_WIDTH - totalWidth) / 2;

  display.setTextSize(2);
  display.setTextColor(BLACK);
  display.fillRect(0, y, SCREEN_WIDTH, 30, WHITE);

  display.setCursor(x, y);
  for (int i = 0; i < 3; i++) {
    if (i < inputIndex) {
      display.print(inputBuffer[i]);
    } else {
      display.print("_");
    }
    if (i < 2) display.print(" ");
  }
}

void showGeneratingNew() {
  int y = 150;
  display.fillRect(0, y, SCREEN_WIDTH, 30, WHITE);
  display.setTextColor(GREEN);
  display.setTextSize(2);
  display.setCursor(20, y);
  display.print("Generating new...");
}

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

void generateRandomColorSequence(uint8_t sequence[], uint8_t length) {
  for (uint8_t i = 0; i < length; i++) {
    sequence[i] = random(0, 3);
  }
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

// Fetch and parse player names from Firestore
void fetchPlayersFromFirestore() {
  String url = "https://firestore.googleapis.com/v1/projects/" + String(projectId) +
               "/databases/(default)/documents/" + collection + "?key=" + apiKey;

  HTTPClient http;
  http.begin(url);
  int httpResponseCode = http.GET();
  String payload = http.getString();
  http.end();

  Serial.println("Firestore Players Collection Response:");
  Serial.println(payload);

  playerCount = 0;
  StaticJsonDocument<8192> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (!error && doc.containsKey("documents")) {
    JsonArray docs = doc["documents"].as<JsonArray>();
    for (JsonObject d : docs) {
      if (d.containsKey("fields") && d["fields"].containsKey("name")) {
        String name = d["fields"]["name"]["stringValue"].as<String>();
        if (playerCount < MAX_PLAYERS) {
          playerNames[playerCount++] = name;
        }
      }
    }
  } else {
    Serial.print("deserializeJson() failed or no documents: ");
    Serial.println(error.c_str());
  }

  Serial.println("Player names:");
  for (int i = 0; i < playerCount; i++) {
    Serial.println(playerNames[i]);
  }
}

// Show player selection menu with fetched names
void showPlayerMenu() {
  inputIndex = 0;
  inputBuffer[0] = inputBuffer[1] = inputBuffer[2] = inputBuffer[3] = '\0';

  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);
  display.setCursor(20, 60);
  display.print("Select player:");
  int y = 100;
  for (int i = 0; i < playerCount; i++) {
    display.setCursor(20, y);
    display.print(String(i + 1) + ") " + playerNames[i]);
    y += 30;
  }
}

void setup() {
  Serial.begin(9600);
  display.begin();
  display.setRotation(0);
  randomSeed(analogRead(0));

  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)WIFI_USERNAME, strlen(WIFI_USERNAME));
  esp_wifi_sta_wpa2_ent_set_username((uint8_t *)WIFI_USERNAME, strlen(WIFI_USERNAME));
  esp_wifi_sta_wpa2_ent_set_password((uint8_t *)WIFI_PASSWORD, strlen(WIFI_PASSWORD));
  esp_wifi_sta_wpa2_ent_enable();
  WiFi.begin(WIFI_SSID);

  Serial.print("Connecting to WPA2-Enterprise Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWiFi connected!");

  fetchPlayersFromFirestore();
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
     if (key && key >= '1' && key <= '4' && key - '0' <= playerCount) {
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
          break;
        }
        Serial.print("Key pressed: ");
        Serial.println(key);

        if (key == '1') {
          showCodeBreakerTitle();
          generateNewRandomNumber();
          inputIndex = 0;
          showInputProgress(inputBuffer, inputIndex);
          currentState = CODE_BREAKER;
        } else if (key == '2') {
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
            if (i < colorSequenceLength - 1) {
              display.fillScreen(WHITE);
              delay(1000);
            }
          }
          display.fillScreen(WHITE);
          display.setTextColor(BLACK);
          display.setTextSize(2);
          display.setCursor(20, 100);
          display.print("Repeat the sequence!");
          showBottomHints();
          currentStep = 0;
          lastButtonState[0] = lastButtonState[1] = lastButtonState[2] = HIGH;
          currentState = VISUAL_MEMORY_INPUT;
        } else {
          showMenuMessage("Please choose 1 or 2");
        }
      }
      break;

    case VISUAL_MEMORY_INPUT: {
      int buttonPins[3] = {RED_BUTTON_PIN, BLUE_BUTTON_PIN, GREEN_BUTTON_PIN};
      const char* buttonNames[3] = {"Red button pressed", "Blue button pressed", "Green button pressed"};
      for (int i = 0; i < 3; i++) {
        int reading = digitalRead(buttonPins[i]);
        if (lastButtonState[i] == HIGH && reading == LOW && (millis() - lastDebounceTime[i]) > debounceDelay) {
          lastDebounceTime[i] = millis();
          Serial.println(buttonNames[i]);
          if (i == colorSequence[currentStep]) {
            currentStep++;
            if (currentStep == colorSequenceLength) {
              Serial.println("Success");
              showVisualMemoryResult(true);
            }
          } else {
            Serial.println("Wrong sequence try again");
            display.fillScreen(WHITE);
            display.setTextSize(2);
            display.setTextColor(RED);
            display.setCursor(20, 120);
            display.print("Wrong sequence");
            display.setCursor(20, 150);
            display.print("try again");
            showBottomHints();
            delay(1500);
            display.fillScreen(WHITE);
            display.setTextColor(BLACK);
            display.setTextSize(2);
            display.setCursor(20, 100);
            display.print("Repeat the sequence!");
            showBottomHints();
            currentStep = 0;
          }
        }
        lastButtonState[i] = reading;
      }
      break;
    }

    case CODE_BREAKER:
      if (key) {
        if (key == '*') {
          showMenu();
          currentState = MENU;
          inputIndex = 0;
          showInputProgress(inputBuffer, inputIndex);
          break;
        }
        if (key == '#') {
          showPlayerMenu();
          currentState = PLAYER_SELECT;
          // No call to showInputProgress here!
          break;
        }
        if (inputIndex < 3 && key >= '0' && key <= '9') {
          inputBuffer[inputIndex++] = key;
          showInputProgress(inputBuffer, inputIndex);
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
          showInputProgress(inputBuffer, inputIndex);
        }
      }
      break;
  }
}
