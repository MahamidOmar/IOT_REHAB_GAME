#include <Arduino_GFX_Library.h>
#include <Keypad.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include "esp_wpa2.h"  // Only needed for WPA2-Enterprise
#include <WiFiClientSecure.h>
#include <vector>
#include <time.h>

unsigned long colorWordStepStartTime = 0;
enum CodeBreakerDifficulty {
  CB_EASY,
  CB_MEDIUM,
  CB_HARD
};
CodeBreakerDifficulty codeBreakerDifficulty = CB_EASY;  // Default
int codeBreakerMaxTries = 12;
enum LedReactionDifficulty {
  LED_EASY,
  LED_MEDIUM,
  LED_HARD
};
LedReactionDifficulty ledReactionDifficulty = LED_EASY;
const unsigned long ledReactionDurations[3] = { 2000, 1000, 500 };  // ms: easy, medium, hard
unsigned long ledReactionStepDuration = 2000;                       // default to easy

int colorWordStars = 0;
enum Difficulty {
  EASY,
  MEDIUM,
  HARD
};
Difficulty colorWordDifficulty = EASY;
bool ledReactionWaiting = false;  // true when in the white screen delay
unsigned long ledReactionDelayStart = 0;
const unsigned long ledReactionDelayDuration = 1000;  // 1 second

const unsigned long colorWordDurations[3] = { 7000, 4000, 2000 };  // ms: easy, medium, hard
// Global variables (near the top of your file)
unsigned long colorWordStepDuration = 7000;  // Default to easy (7 seconds)

// --- PIN DEFINITIONS (TOP) ---
#define TFT_SCLK 18
#define TFT_MOSI 23
#define TFT_MISO 19
#define TFT_CS 15
#define TFT_DC 21
#define TFT_RST 2

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

// RGB Button pin definitions
#define RED_BUTTON_PIN 4
#define BLUE_BUTTON_PIN 22
#define GREEN_BUTTON_PIN 5

// Led definitions
#define WS2812_PIN 16
#define LEDS_PER_RING 16
#define NUM_RINGS 3
#define WS2812_NUM_LEDS (LEDS_PER_RING * NUM_RINGS)
Adafruit_NeoPixel leds(WS2812_NUM_LEDS, WS2812_PIN, NEO_GRB + NEO_KHZ800);

// Visual Memory Game
#define MAX_SEQUENCE_LENGTH 10
const char *colorNames[] = { "Red", "Blue", "Green" };
uint16_t colorValues[] = { RED, BLUE, GREEN };  // TFT color values

// --- Color-Word Challenge definitions ---
#define COLOR_WORD_CHALLENGE_LENGTH 5
uint8_t cwcSeq1[COLOR_WORD_CHALLENGE_LENGTH];
uint8_t cwcSeq2[COLOR_WORD_CHALLENGE_LENGTH];

// --- Color-Word Challenge game variables ---
int colorWordWrongTries = 0;
uint8_t colorWordCurrentStep = 0;
const int colorWordMaxTries = 5;  // same as sequence length


bool offlineMode = false;



// --- Led Reaction game variables ---
unsigned long ledReactionStartTime = 0;
const unsigned long ledReactionGameDuration = 20000;  // 20 seconds in ms
int ledReactionCorrect = 0;
int ledReactionCurrentColor = 0;
bool ledReactionActive = false;

// Multiplayer game variables
byte multiplayerPlayer1 = 0;  // 1-based index
byte multiplayerPlayer2 = 0;  // 1-based index

// --- Code Breaker multiplayer game variables ---
// --- Secret numbers for each player (3 digits + null terminator) ---
char player1Secret[4] = "";  // Player 1's secret number (as a string)
char player2Secret[4] = "";  // Player 2's secret number (as a string)

// --- Current guess buffers for each player ---
char player1Guess[4] = "";  // Player 1's current guess (as a string)
char player2Guess[4] = "";  // Player 2's current guess (as a string)

bool cbSecretNeedsRedraw = true;

// --- Try counters (optional, for stats) ---
uint8_t player1Tries = 0;  // Number of guesses Player 1 has made
uint8_t player2Tries = 0;  // Number of guesses Player 2 has made

// --- Whose turn is it? ---
bool codeBreakerMultiplayerTurn = false;  // false = Player 1's turn, true = Player 2's turn

// --- Input buffer index for entering numbers ---
uint8_t cbMultiInputIndex = 0;  // For multiplayer code breaker digit entry

// --- (Optional) Game over flags ---
bool player1Guessed = false;
bool player2Guessed = false;

// --- Last guess results for each player ---
char player1LastGuess[4] = "";
int player1LastExact = 0;
int player1LastPartial = 0;

char player2LastGuess[4] = "";
int player2LastExact = 0;
int player2LastPartial = 0;

// Keypad definitions
const byte ROWS = 4;
const byte COLS = 3;

char keys[ROWS][COLS] = {
  { '1', '2', '3' },
  { '4', '5', '6' },
  { '7', '8', '9' },
  { '*', '0', '#' }
};

byte rowPins[ROWS] = { 14, 12, 27, 13 };
byte colPins[COLS] = { 25, 33, 32 };

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Set up the SPI bus and display
Arduino_ESP32SPI bus(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, TFT_MISO);
Arduino_ILI9341 display(&bus, TFT_RST);

char inputBuffer[4];  // 3 chars + null terminator
byte inputIndex = 0;
char randomNumberStr[4];
byte currentPlayer = 0;
uint8_t colorSequence[MAX_SEQUENCE_LENGTH];
uint8_t colorSequenceLength = 5;
uint8_t currentStep = 0;
unsigned long lastDebounceTime[3] = { 0, 0, 0 };
const unsigned long debounceDelay = 200;
int lastButtonState[3] = { HIGH, HIGH, HIGH };

enum State {
  MODE_SELECT,
  PLAYER1_SELECT,
  PLAYER2_SELECT,
  PLAYER_SELECT,
  MULTI_MENU,
  CODE_BREAKER_MULTI_SECRET1,
  CODE_BREAKER_MULTI_SECRET2,
  CODE_BREAKER_MULTI_TURN_P1,
  CODE_BREAKER_MULTI_TURN_P2,
  MENU,
  CODE_BREAKER,
  COLOR_WORD_DIFFICULTY_SELECT,
  COLOR_WORD_CHALLENGE,
  CODE_BREAKER_DIFFICULTY_SELECT,  // For single player Code Breaker difficulty menu
  VISUAL_MEMORY_DIFFICULTY_SELECT,
  VISUAL_MEMORY,
  VISUAL_MEMORY_INPUT,
  VISUAL_MEMORY_RESULT,
  LED_REACTION_DIFFICULTY_SELECT,

  COLOR_WORD_CHALLENGE_INPUT,
  LED_REACTION
};
State currentState = PLAYER_SELECT;

// WiFi credentials
#ifdef WOKWI_SIMULATION
// Simplified WiFi for Wokwi simulation with Private IoT Gateway
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define USE_WPA2_ENTERPRISE false
#else
// Real hardware WiFi credentials (for WPA2-Enterprise)
#define WIFI_SSID "eduroam"
#define WIFI_USERNAME "omarm@campus.technion.ac.il"
#define WIFI_PASSWORD "JonSnow04112001#"
#define USE_WPA2_ENTERPRISE true
#endif

// Firestore info
const char *projectId = "rehabgames-47d42";
const char *apiKey = "AIzaSyBkYfD2pq7ILcxd0LIjjBO_luA_f52juNg";
const char *collection = "users";

// Players info
// #define MAX_PLAYERS 4
// String playerNames[MAX_PLAYERS];
// int playerCount = MAX_PLAYERS;

// Players info
std::vector<String> playerNames;
std::vector<String> playerDocIds;
int playerCount = 0;  // Will be updated after fetching
int maxToShow = 0;
int playerDisplayOffset = 0;  // For paging through playerNames

// Game variables for number of tries
int codeBreakerWrongTries = 0;
int visualMemoryWrongTries = 0;
int maxWrongTries = 5;
int maxWrongTries_VM = 3;  // For Visual Memory game

// Call this once after WiFi connects
void setupTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");  // UTC
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.println("Waiting for NTP time sync...");
    delay(1000);
  }
}


// Get ISO 8601 string with timezone offset
String getCurrentISOTimeUTC() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "";
  }
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);  // Always UTC
  return String(buf);
}


void showLedReactionScore(int score) {
  display.fillScreen(WHITE);
  display.setTextSize(3);
  display.setTextColor(BLACK);
  String msg = "Score: " + String(score);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(msg.c_str(), 0, 0, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - w) / 2;
  int y = (SCREEN_HEIGHT - 3 * 8) / 2;
  display.setCursor(x, y);
  display.print(msg);
  delay(2000);  // Show for 2 seconds
}
void showLedReactionDifficultySelect() {
  display.fillScreen(WHITE);

  // Bigger buttons
  int btnW = 200, btnH = 64, radius = 16;
  int btnX = 20, btnY1 = 60, btnY2 = 150, btnY3 = 240;

  // --- Easy: Green button, big smiley face icon ---
  display.fillRoundRect(btnX, btnY1, btnW, btnH, radius, GREEN);
  int ex = btnX + 32, ey = btnY1 + btnH/2;
  display.fillCircle(ex, ey, 14, WHITE); // Face
  display.fillCircle(ex - 6, ey - 4, 2, BLACK); // Left eye
  display.fillCircle(ex + 6, ey - 4, 2, BLACK); // Right eye
  display.drawArc(ex, ey + 4, 7, 7, 30, 150, BLACK); // Smile
  display.setTextColor(WHITE);
  display.setTextSize(2);
  int easyTextX = btnX + 80;
  int easyTextY = btnY1 + 18;
  display.setCursor(easyTextX, easyTextY);
  display.print("Easy");
  display.setCursor(easyTextX, easyTextY + 28);
  display.print("2s");

  // --- Medium: Yellow button, big lightning bolt ---
  uint16_t yellow = 0xFFE0;
  display.fillRoundRect(btnX, btnY2, btnW, btnH, radius, yellow);
  int bx = btnX + 32, by = btnY2 + btnH/2 - 8;
  display.drawLine(bx, by, bx + 12, by + 24, BLACK);           // Top
  display.drawLine(bx + 12, by + 24, bx - 6, by + 24, BLACK);  // Middle
  display.drawLine(bx - 6, by + 24, bx + 4, by + 44, BLACK);   // Bottom
  display.setTextColor(WHITE);
  display.setTextSize(2);
  int medTextX = btnX + 80;
  int medTextY = btnY2 + 18;
  display.setCursor(medTextX, medTextY);
  display.print("Medium");
  display.setCursor(medTextX, medTextY + 28);
  display.print("1s");

  // --- Hard: Red button, big flame icon ---
  display.fillRoundRect(btnX, btnY3, btnW, btnH, radius, RED);
  int fx = btnX + 32, fy = btnY3 + btnH/2 + 8;
  display.fillTriangle(fx, fy - 24, fx - 16, fy + 16, fx + 16, fy + 16, ORANGE); // Big flame
  display.fillCircle(fx, fy - 4, 8, YELLOW); // Big flame core
  display.setTextColor(WHITE);
  display.setTextSize(2);
  int hardTextX = btnX + 80;
  int hardTextY = btnY3 + 18;
  display.setCursor(hardTextX, hardTextY);
  display.print("Hard");
  display.setCursor(hardTextX, hardTextY + 28);
  display.print("0.5s");
}




void showColorWordDifficultySelect() {
  display.fillScreen(WHITE);

  // Bigger buttons
  int btnW = 200, btnH = 64, radius = 16;
  int btnX = 20, btnY1 = 60, btnY2 = 150, btnY3 = 240;

  // --- Easy: Green button, big smiley face icon ---
  display.fillRoundRect(btnX, btnY1, btnW, btnH, radius, GREEN);
  int ex = btnX + 32, ey = btnY1 + btnH/2;
  display.fillCircle(ex, ey, 14, WHITE); // Face
  display.fillCircle(ex - 6, ey - 4, 2, BLACK); // Left eye
  display.fillCircle(ex + 6, ey - 4, 2, BLACK); // Right eye
  display.drawArc(ex, ey + 4, 7, 7, 30, 150, BLACK); // Smile
  display.setTextColor(WHITE);
  display.setTextSize(2);
  int easyTextX = btnX + 80;
  int easyTextY = btnY1 + 18;
  display.setCursor(easyTextX, easyTextY);
  display.print("Easy");
  display.setCursor(easyTextX, easyTextY + 28);
  display.print("7s");

  // --- Medium: Yellow button, big lightning bolt ---
  uint16_t yellow = 0xFFE0;
  display.fillRoundRect(btnX, btnY2, btnW, btnH, radius, yellow);
  int bx = btnX + 32, by = btnY2 + btnH/2 - 8;
  display.drawLine(bx, by, bx + 12, by + 24, BLACK);           // Top
  display.drawLine(bx + 12, by + 24, bx - 6, by + 24, BLACK);  // Middle
  display.drawLine(bx - 6, by + 24, bx + 4, by + 44, BLACK);   // Bottom
  display.setTextColor(WHITE);
  display.setTextSize(2);
  int medTextX = btnX + 80;
  int medTextY = btnY2 + 18;
  display.setCursor(medTextX, medTextY);
  display.print("Medium");
  display.setCursor(medTextX, medTextY + 28);
  display.print("4s");

  // --- Hard: Red button, big flame icon ---
  display.fillRoundRect(btnX, btnY3, btnW, btnH, radius, RED);
  int fx = btnX + 32, fy = btnY3 + btnH/2 + 8;
  display.fillTriangle(fx, fy - 24, fx - 16, fy + 16, fx + 16, fy + 16, ORANGE); // Big flame
  display.fillCircle(fx, fy - 4, 8, YELLOW); // Big flame core
  display.setTextColor(WHITE);
  display.setTextSize(2);
  int hardTextX = btnX + 80;
  int hardTextY = btnY3 + 18;
  display.setCursor(hardTextX, hardTextY);
  display.print("Hard");
  display.setCursor(hardTextX, hardTextY + 28);
  display.print("2s");
}


void showColorOnRings(int colorIndex) {
  uint32_t color;
  if (colorIndex == 0)
    color = leds.Color(255, 0, 0);  // Red
  else if (colorIndex == 1)
    color = leds.Color(0, 0, 255);  // Blue
  else
    color = leds.Color(0, 255, 0);  // Green
  for (int i = 0; i < WS2812_NUM_LEDS; i++)
    leds.setPixelColor(i, color);
  leds.show();
}

// --- Color-Word Challenge helpers ---
void generateColorWordChallengeSequences(uint8_t seq1[], uint8_t seq2[], uint8_t length) {
  for (uint8_t i = 0; i < length; i++) {
    seq1[i] = random(0, 3);
    uint8_t color2;
    do {
      color2 = random(0, 3);
    } while (color2 == seq1[i]);
    seq2[i] = color2;
  }
}

void showColorWordChallengeStep(uint8_t index) {
  display.fillScreen(WHITE);
  display.setTextSize(3);
  display.setTextColor(colorValues[cwcSeq2[index]]);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(colorNames[cwcSeq1[index]], 0, 0, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - w) / 2;
  int y = (SCREEN_HEIGHT - 3 * 8) / 2;
  display.setCursor(x, y);
  display.print(colorNames[cwcSeq1[index]]);
}

// --- Display helpers ---
void showBottomHints() {
  display.setTextSize(1);
  display.setTextColor(DARKGREY);
  int y = SCREEN_HEIGHT - 10;
  display.setCursor(0, y);
  display.print("* games menu");
  const char *logoutText = "# logout";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(logoutText, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(SCREEN_WIDTH - w, y);
  display.print(logoutText);
}

void showGuessScreen(uint8_t player, uint8_t tries) {
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);
  display.setCursor(20, 80);
  display.print("Player ");
  display.print(player);
  display.print(": Guess!");
  display.setCursor(20, 120);
  display.print("Tries: ");
  display.print(tries);
}

void cbMultiCountMatches(const char *guess, const char *secret, int &exact, int &partial) {
  exact = 0;
  partial = 0;
  bool guessUsed[3] = { false, false, false };
  bool secretUsed[3] = { false, false, false };

  // Exact matches
  for (int i = 0; i < 3; i++) {
    if (guess[i] == secret[i]) {
      exact++;
      guessUsed[i] = true;
      secretUsed[i] = true;
    }
  }
  // Partial matches
  for (int i = 0; i < 3; i++) {
    if (guessUsed[i])
      continue;
    for (int j = 0; j < 3; j++) {
      if (secretUsed[j])
        continue;
      if (guess[i] == secret[j]) {
        partial++;
        secretUsed[j] = true;
        break;
      }
    }
  }
}

void showSecretEntry(uint8_t player, const char *buffer, uint8_t bufLen) {
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);
  display.setCursor(20, 80);
  display.print("Player ");
  display.print(player);
  display.print(": Enter secret");

  // Draw masked input
  display.setTextSize(3);
  display.setTextColor(DARKGREY);
  display.setCursor(60, 150);
  for (uint8_t i = 0; i < 3; i++) {
    if (i < bufLen) {
      display.print("*");
    } else {
      display.print("-");
    }
    if (i < 2)
      display.print(" ");
  }
}

// Show the mode selection screen
void showModeSelect() {
  display.fillScreen(WHITE);

  // --- Title ---
  display.setTextColor(BLACK);
  display.setTextSize(2);  // Keep the title moderate
  const char *title = "Choose your mode";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 24);
  display.print(title);

  // --- Button dimensions ---
  int btnWidth = 180;
  int btnHeight = 48;
  int btnX = (SCREEN_WIDTH - btnWidth) / 2;
  int btnY1 = 70;
  int btnY2 = 140;
  int radius = 14;

  // --- Single Player Button (Blue) ---
  uint16_t singleColor = 0x3A99;  // Soft blue
  display.fillRoundRect(btnX, btnY1, btnWidth, btnHeight, radius, singleColor);
  display.drawRoundRect(btnX, btnY1, btnWidth, btnHeight, radius, 0x001F);  // Blue border

  // Single player icon (head and shoulders)
  int iconX = btnX + 28;
  int iconY = btnY1 + btnHeight / 2 - 4;
  display.fillCircle(iconX, iconY, 10, WHITE);                      // Head
  display.fillRoundRect(iconX - 10, iconY + 10, 20, 10, 5, WHITE);  // Shoulders/body

  // Label (smaller font)
  display.setTextSize(1);  // Make label smaller
  display.setTextColor(WHITE);
  display.setCursor(btnX + 55, btnY1 + btnHeight / 2 - 6);
  display.print("Single Player");

  // --- Multiplayer Button (Green) ---
  uint16_t multiColor = 0x07E0;  // Green
  display.fillRoundRect(btnX, btnY2, btnWidth, btnHeight, radius, multiColor);
  display.drawRoundRect(btnX, btnY2, btnWidth, btnHeight, radius, 0x03E0);  // Dark green border

  // Multiplayer icon (two heads)
  int iconX1 = btnX + 23, iconY1 = btnY2 + btnHeight / 2 - 3;
  int iconX2 = btnX + 38, iconY2 = btnY2 + btnHeight / 2 + 2;
  display.fillCircle(iconX1, iconY1, 8, WHITE);                    // Head 1
  display.fillCircle(iconX2, iconY2, 8, WHITE);                    // Head 2
  display.fillRoundRect(iconX1 - 8, iconY1 + 8, 16, 8, 4, WHITE);  // Body 1
  display.fillRoundRect(iconX2 - 8, iconY2 + 8, 16, 8, 4, WHITE);  // Body 2

  // Label (smaller font)
  display.setTextSize(1);  // Make label smaller
  display.setTextColor(WHITE);
  display.setCursor(btnX + 55, btnY2 + btnHeight / 2 - 6);
  display.print("Multiplayer");

  // --- Shortcut hint at bottom ---
  display.setTextSize(1);
  display.setTextColor(DARKGREY);
  const char *hint = "Press 1 for Single, 2 for Multi";
  display.getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, SCREEN_HEIGHT - 18);
  display.print(hint);


  // --- Offline Single Player Button (Grey) ---
uint16_t offlineColor = 0xC618;  // Light grey
int btnY3 = 210;
display.fillRoundRect(btnX, btnY3, btnWidth, btnHeight, radius, offlineColor);
display.setTextColor(BLACK);
display.setTextSize(2);
display.setCursor(btnX + 32, btnY3 + 14);
display.print("Offline");

}


void showMultiplayerPlayerSelect1(int selected = -1) {
  display.fillScreen(WHITE);

  // --- Title ---
  display.setTextColor(BLACK);
  display.setTextSize(2);
  const char *title = "Choose Player 1:";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 32);
  display.print(title);

  // --- Button dimensions ---
  int btnWidth = 180;
  int btnHeight = 48;
  int btnX = (SCREEN_WIDTH - btnWidth) / 2;
  int btnYStart = 80;
  int btnSpacing = 16;
  int radius = 14;

  // --- Button colors ---
  uint16_t btnColors[] = {0x3A99, 0xFFE0, 0xC618, 0xF800, 0x07E0}; // blue, yellow, grey, red, green

  int shown = 0;
  for (int i = 0; i < maxToShow; i++) {
    int btnY = btnYStart + i * (btnHeight + btnSpacing);
    uint16_t color = btnColors[i % 5];

    // Highlight selected player (if any)
    if (i == selected) {
      display.drawRoundRect(btnX-3, btnY-3, btnWidth+6, btnHeight+6, radius+3, BLACK);
    }

    display.fillRoundRect(btnX, btnY, btnWidth, btnHeight, radius, color);

    // Center player name in button
    display.setTextColor(BLACK);
    display.setTextSize(2);
    String name = playerNames[playerDisplayOffset + i];
    int16_t nx1, ny1;
    uint16_t nw, nh;
    display.getTextBounds(name.c_str(), 0, 0, &nx1, &ny1, &nw, &nh);
    int nameX = btnX + (btnWidth - nw) / 2;
    int nameY = btnY + (btnHeight - nh) / 2;
    display.setCursor(nameX, nameY);
    display.print(name);
    ++shown;
  }

  // --- Logout at bottom right ---
  const char *logoutText = "# logout";
  int16_t lx1, ly1;
  uint16_t lw, lh;
  display.setTextSize(1);
  display.setTextColor(DARKGREY);
  display.getTextBounds(logoutText, 0, 0, &lx1, &ly1, &lw, &lh);
  display.setCursor(SCREEN_WIDTH - lw, SCREEN_HEIGHT - 10);
  display.print(logoutText);
}
/*
// Show the multiplayer player selection screen, first for player 1 then for player 2
void showMultiplayerPlayerSelect1() {
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);
  display.setCursor(20, 60);
  display.print("Choose Player 1:");
  int y = 100;
  int shown = 0;
  // for (int i = playerDisplayOffset; i < playerNames.size() && shown < 4; i++) {
  for (int i = 0; i < maxToShow; i++) {
    display.setCursor(20, y);
    display.print(String(i + 1) + ") " + playerNames[playerDisplayOffset + i]);
    y += 30;
    ++shown;
  }
  // --- Add this for # logout at bottom right ---
  const char *logoutText = "# logout";
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(1);
  display.setTextColor(DARKGREY);
  display.getTextBounds(logoutText, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(SCREEN_WIDTH - w, SCREEN_HEIGHT - 10);
  display.print(logoutText);
}*/
/*
void showMultiplayerPlayerSelect2(byte excludeIndex) {
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);
  display.setCursor(20, 60);
  display.print("Choose Player 2:");
  int y = 100;
  int optionNum = 1;
  int shown = 0;
  for (int i = playerDisplayOffset; i < playerNames.size() && shown < 4; i++) {
    if (i == excludeIndex)
      continue;  // skip the chosen player
    display.setCursor(20, y);
    display.print(String(optionNum) + ") " + playerNames[i]);
    y += 30;
    optionNum++;
    ++shown;
  }
  // --- Add this for # logout at bottom right ---
  const char *logoutText = "# logout";
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(1);
  display.setTextColor(DARKGREY);
  display.getTextBounds(logoutText, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(SCREEN_WIDTH - w, SCREEN_HEIGHT - 10);
  display.print(logoutText);
}
*/
void showMultiplayerPlayerSelect2(byte excludeIndex, int selected = -1) {
  display.fillScreen(WHITE);

  // --- Title ---
  display.setTextColor(BLACK);
  display.setTextSize(2);
  const char *title = "Choose Player 2:";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 32);
  display.print(title);

  // --- Button dimensions ---
  int btnWidth = 180;
  int btnHeight = 48;
  int btnX = (SCREEN_WIDTH - btnWidth) / 2;
  int btnYStart = 80;
  int btnSpacing = 16;
  int radius = 14;

  // --- Button colors ---
  uint16_t btnColors[] = {0x3A99, 0xFFE0, 0xC618, 0xF800, 0x07E0}; // blue, yellow, grey, red, green

  int shown = 0;
  int optionNum = 0;
  for (int i = playerDisplayOffset; i < playerNames.size() && shown < maxToShow; i++) {
    if (i == excludeIndex)
      continue;  // skip the chosen player

    int btnY = btnYStart + shown * (btnHeight + btnSpacing);
    uint16_t color = btnColors[shown % 5];

    // Highlight selected player (if any)
    if (shown == selected) {
      display.drawRoundRect(btnX-3, btnY-3, btnWidth+6, btnHeight+6, radius+3, BLACK);
    }

    display.fillRoundRect(btnX, btnY, btnWidth, btnHeight, radius, color);

    // Center player name in button
    display.setTextColor(BLACK);
    display.setTextSize(2);
    String name = playerNames[i];
    int16_t nx1, ny1;
    uint16_t nw, nh;
    display.getTextBounds(name.c_str(), 0, 0, &nx1, &ny1, &nw, &nh);
    int nameX = btnX + (btnWidth - nw) / 2;
    int nameY = btnY + (btnHeight - nh) / 2;
    display.setCursor(nameX, nameY);
    display.print(name);

    shown++;
    optionNum++;
  }

  // --- Logout at bottom right ---
  const char *logoutText = "# logout";
  int16_t lx1, ly1;
  uint16_t lw, lh;
  display.setTextSize(1);
  display.setTextColor(DARKGREY);
  display.getTextBounds(logoutText, 0, 0, &lx1, &ly1, &lw, &lh);
  display.setCursor(SCREEN_WIDTH - lw, SCREEN_HEIGHT - 10);
  display.print(logoutText);
}



void showNextLedReactionColor() {
  ledReactionCurrentColor = random(0, 3);
  uint32_t color;
  if (ledReactionCurrentColor == 0)
    color = leds.Color(255, 0, 0);  // Red
  else if (ledReactionCurrentColor == 1)
    color = leds.Color(0, 0, 255);  // Blue
  else
    color = leds.Color(0, 255, 0);  // Green

  for (int i = 0; i < WS2812_NUM_LEDS; i++)
    leds.setPixelColor(i, color);
  leds.show();

  // Fill the entire TFT screen with the color (no text)
  display.fillScreen(colorValues[ledReactionCurrentColor]);
}
void showPlayerSelected(byte player)
{
  display.fillScreen(WHITE);

  // --- Draw a colored welcome banner ---
  uint16_t bannerColor = 0x3A99; // Soft blue
  int bannerH = 56;
  display.fillRoundRect(0, 40, SCREEN_WIDTH, bannerH, 12, bannerColor);

  // --- Draw a user icon (left of the banner) ---
  int iconX = 38, iconY = 68;
  display.fillCircle(iconX, iconY, 18, WHITE);                       // Head
  display.fillRoundRect(iconX - 18, iconY + 12, 36, 18, 9, WHITE);   // Shoulders

  // --- Welcome text, centered in the banner ---
  display.setTextSize(2);
  display.setTextColor(WHITE);
  const char* welcomeMsg = "Welcome back";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(welcomeMsg, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2 + 10, 60);
  display.print(welcomeMsg);

  // --- Player name, centered below banner, "bold" effect ---
  display.setTextSize(3); // Make it big for emphasis
  display.setTextColor(BLACK);
  String name;
  if (player >= 1 && player <= playerCount)
    name = playerNames[player - 1];
  else
    name = "Unknown Player";
  display.getTextBounds(name.c_str(), 0, 0, &x1, &y1, &w, &h);
  int nameX = (SCREEN_WIDTH - w) / 2;
  int nameY = 120;

  // Fake bold: print name twice, slightly offset
  display.setTextColor(DARKGREY);
  display.setCursor(nameX + 1, nameY + 1);
  display.print(name);
  display.setTextColor(BLACK);
  display.setCursor(nameX, nameY);
  display.print(name);

  delay(1200);
}





// Show single player games menu
void showMenu() {
  display.fillScreen(WHITE);
  display.setFont();      // Use default font
  display.setTextSize(2); // Size 2 for clear, readable text

  // Button and icon layout
  int btnW = 170, btnH = 54, radius = 12;
  int btnX = 54;
  int iconX = 20;
  int y1 = 40, y2 = 110, y3 = 180, y4 = 250;

  // --- Code Breaker ---
  uint16_t cbColor = 0x3A99; // Soft blue
  display.fillRoundRect(btnX, y1, btnW, btnH, radius, cbColor);
  // Icon (lock)
  int lockX = iconX + 8, lockY = y1 + btnH/2;
  display.fillRect(lockX - 5, lockY - 5, 10, 8, cbColor);
  display.drawArc(lockX - 1, lockY - 13, 5, 5, 210, 360, cbColor);
  display.drawLine(lockX + 1, lockY - 13, lockX + 1, lockY - 5, cbColor);
  display.setTextColor(WHITE);
  display.setCursor(btnX + 48, y1 + 18); // First word
  display.print("Code");
  display.setCursor(btnX + 48, y1 + 38); // Second word, below
  display.print("Breaker");

  // --- Visual Memory ---
  uint16_t vmColor = 0x07E0; // Green
  display.fillRoundRect(btnX, y2, btnW, btnH, radius, vmColor);
  int eyeX = iconX + 8, eyeY = y2 + btnH/2;
  display.drawEllipse(eyeX, eyeY, 6, 3, vmColor);
  display.fillCircle(eyeX, eyeY, 2, BLACK);
  display.setTextColor(WHITE);
  display.setCursor(btnX + 48, y2 + 18);
  display.print("Visual");
  display.setCursor(btnX + 48, y2 + 38);
  display.print("Memory");

  // --- Color Word ---
  uint16_t cwcColor = 0xFD20; // Orange
  display.fillRoundRect(btnX, y3, btnW, btnH, radius, cwcColor);
  int palX = iconX + 8, palY = y3 + btnH/2;
  display.fillCircle(palX, palY, 4, cwcColor);
  display.fillCircle(palX - 2, palY - 2, 1, RED);
  display.fillCircle(palX + 2, palY - 1, 1, BLUE);
  display.fillCircle(palX, palY + 2, 1, GREEN);
  display.setTextColor(WHITE);
  display.setCursor(btnX + 48, y3 + 18);
  display.print("Color");
  display.setCursor(btnX + 48, y3 + 38);
  display.print("Word");

  // --- Led Reaction ---
  uint16_t lrColor = 0xFFE0; // Yellow
  display.fillRoundRect(btnX, y4, btnW, btnH, radius, lrColor);
  int boltX = iconX + 8, boltY = y4 + btnH/2 - 4;
  display.drawLine(boltX, boltY, boltX + 4, boltY + 8, lrColor);
  display.drawLine(boltX + 4, boltY + 8, boltX - 2, boltY + 8, lrColor);
  display.drawLine(boltX - 2, boltY + 8, boltX + 1, boltY + 14, lrColor);
  display.setTextColor(WHITE);
  display.setCursor(btnX + 48, y4 + 18);
  display.print("Led");
  display.setCursor(btnX + 48, y4 + 38);
  display.print("Reaction");

  // --- Shortcut hint at bottom ---
  showBottomHints();
}


// Show multiplayer games menu
void showMultiplayerMenu() {
  display.fillScreen(WHITE);

  // --- Button dimensions and position ---
  int btnW = 200, btnH = 64, radius = 16;
  int btnX = (SCREEN_WIDTH - btnW) / 2;
  int btnY = 120;

  // --- Button color (green for multiplayer) ---
  uint16_t btnColor = 0x07E0; // Green
  display.fillRoundRect(btnX, btnY, btnW, btnH, radius, btnColor);

  // --- Multiplayer icon (two heads) ---
  int iconX1 = btnX + 38, iconY1 = btnY + btnH / 2 - 6;
  int iconX2 = btnX + 58, iconY2 = btnY + btnH / 2 + 6;
  display.fillCircle(iconX1, iconY1, 10, WHITE);                   // Head 1
  display.fillCircle(iconX2, iconY2, 10, WHITE);                   // Head 2
  display.fillRoundRect(iconX1 - 10, iconY1 + 10, 20, 10, 5, WHITE); // Body 1
  display.fillRoundRect(iconX2 - 10, iconY2 + 10, 20, 10, 5, WHITE); // Body 2

  // --- Button label (two lines) ---
  display.setTextSize(2);
  display.setTextColor(WHITE);
  int labelX = btnX + 90;
  int labelY = btnY + 18;
  display.setCursor(labelX, labelY);
  display.print("Code");
  display.setCursor(labelX, labelY + 28);
  display.print("Breaker");

  // --- Title ---
  display.setTextSize(2);
  display.setTextColor(BLACK);
  int16_t x1, y1;
  uint16_t w, h;
  const char *title = "Multiplayer Games";
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 60);
  display.print(title);

  // --- # logout at bottom right ---
  const char *logoutText = "# logout";
  display.setTextSize(1);
  display.setTextColor(DARKGREY);
  display.getTextBounds(logoutText, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(SCREEN_WIDTH - w, SCREEN_HEIGHT - 10);
  display.print(logoutText);
}

void showMenuMessage(const char *msg) {
  int msgHeight = 40;
  display.fillRect(0, SCREEN_HEIGHT - msgHeight, SCREEN_WIDTH, msgHeight, WHITE);
  display.setTextColor(BLUE);
  display.setTextSize(2);
  display.setCursor(20, SCREEN_HEIGHT - msgHeight + 10);
  display.print(msg);
}

void showColorWordTitle() {
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);

  const char *title = "Color Word";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - w) / 2;
  int y = 100;

  display.setCursor(x, y);
  display.print(title);
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
  display.setTextColor(RED);
  display.setCursor(20, y + 30);
  display.print("Partial: ");
  display.print(partial);
}

void showLastTry(const char *guess) {
  int y = 150;
  display.fillRect(0, y, SCREEN_WIDTH, 30, WHITE);
  display.setTextColor(DARKGREY);
  display.setTextSize(2);
  display.setCursor(20, y);
  display.print("Last try: ");
  display.print(guess);
}

void showCodeBreakerDifficultyMenu() {
  display.fillScreen(WHITE);

  // Bigger buttons
  int btnW = 200, btnH = 64, radius = 16;
  int btnX = 20, btnY1 = 60, btnY2 = 150, btnY3 = 240;

  // --- Easy: Green button, big key icon ---
  display.fillRoundRect(btnX, btnY1, btnW, btnH, radius, GREEN);
  display.fillCircle(btnX + 32, btnY1 + btnH/2, 12, WHITE); // Key head (bigger)
  display.drawLine(btnX + 44, btnY1 + btnH/2, btnX + 70, btnY1 + btnH/2, WHITE); // Key shaft (longer)
  display.drawLine(btnX + 60, btnY1 + btnH/2, btnX + 60, btnY1 + btnH/2 + 12, WHITE); // Key tooth (bigger)
  display.setTextColor(WHITE);
  display.setTextSize(2);
  int easyTextX = btnX + 80;
  int easyTextY = btnY1 + 18;
  display.setCursor(easyTextX, easyTextY);
  display.print("Easy");
  display.setCursor(easyTextX, easyTextY + 28);
  display.print("12 tries");

  // --- Medium: Yellow button, big lock icon ---
  uint16_t yellow = 0xFFE0;
  display.fillRoundRect(btnX, btnY2, btnW, btnH, radius, yellow);
  int lx = btnX + 32, ly = btnY2 + btnH/2 + 8;
  display.drawRect(lx - 14, ly - 14, 28, 28, WHITE); // Lock body (bigger)
  display.drawArc(lx, ly - 14, 14, 14, 180, 360, WHITE); // Lock shackle (bigger)
  display.setTextColor(WHITE);
  display.setTextSize(2);
  int medTextX = btnX + 80;
  int medTextY = btnY2 + 18;
  display.setCursor(medTextX, medTextY);
  display.print("Medium");
  display.setCursor(medTextX, medTextY + 28);
  display.print("8 tries");

  // --- Hard: Red button, big fire icon ---
  display.fillRoundRect(btnX, btnY3, btnW, btnH, radius, RED);
  int fx = btnX + 32, fy = btnY3 + btnH/2 + 8;
  display.fillTriangle(fx, fy - 24, fx - 16, fy + 16, fx + 16, fy + 16, ORANGE); // Big flame
  display.fillCircle(fx, fy - 4, 8, YELLOW); // Big flame core
  display.setTextColor(WHITE);
  display.setTextSize(2);
  int hardTextX = btnX + 80;
  int hardTextY = btnY3 + 18;
  display.setCursor(hardTextX, hardTextY);
  display.print("Hard");
  display.setCursor(hardTextX, hardTextY + 28);
  display.print("5 tries");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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
// Show the difficulty selection screen for Visual Memory
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void showVisualMemoryDifficultyMenu() {
  display.fillScreen(WHITE);

  // Bigger buttons
  int btnW = 200, btnH = 64, radius = 16;
  int btnX = 20, btnY1 = 60, btnY2 = 150, btnY3 = 240;

  // --- Easy: Green button, big eye icon ---
  display.fillRoundRect(btnX, btnY1, btnW, btnH, radius, GREEN);
  int ex = btnX + 32, ey = btnY1 + btnH/2;
  display.drawEllipse(ex, ey, 14, 7, WHITE); // Bigger eye outline
  display.fillCircle(ex, ey, 4, BLACK);      // Bigger pupil
  display.setTextColor(WHITE);
  display.setTextSize(2);
  int easyTextX = btnX + 80;
  int easyTextY = btnY1 + 18;
  display.setCursor(easyTextX, easyTextY);
  display.print("Easy");
  display.setCursor(easyTextX, easyTextY + 28);
  display.print("5 seq");

  // --- Medium: Yellow button, big brain icon ---
  uint16_t yellow = 0xFFE0;
  display.fillRoundRect(btnX, btnY2, btnW, btnH, radius, yellow);
  int bx = btnX + 32, by = btnY2 + btnH/2;
  display.fillCircle(bx - 7, by, 8, WHITE);
  display.fillCircle(bx + 7, by, 8, WHITE);
  display.setTextColor(WHITE);
  display.setTextSize(2);
  int medTextX = btnX + 80;
  int medTextY = btnY2 + 18;
  display.setCursor(medTextX, medTextY);
  display.print("Medium");
  display.setCursor(medTextX, medTextY + 28);
  display.print("8 seq");

  // --- Hard: Red button, big lightning icon ---
  display.fillRoundRect(btnX, btnY3, btnW, btnH, radius, RED);
  int lx = btnX + 32, ly = btnY3 + btnH/2 - 8;
  display.drawLine(lx, ly, lx + 12, ly + 24, ORANGE);           // Top
  display.drawLine(lx + 12, ly + 24, lx - 6, ly + 24, ORANGE);  // Middle
  display.drawLine(lx - 6, ly + 24, lx + 4, ly + 44, ORANGE);   // Bottom
  display.setTextColor(WHITE);
  display.setTextSize(2);
  int hardTextX = btnX + 80;
  int hardTextY = btnY3 + 18;
  display.setCursor(hardTextX, hardTextY);
  display.print("Hard");
  display.setCursor(hardTextX, hardTextY + 28);
  display.print("10 seq");
}


void showInputProgress(const char *inputBuffer, byte inputIndex) {
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
    if (i < 2)
      display.print(" ");
  }
}

void countMatches(const char *guess, const char *answer, int &exact, int &partial) {
  exact = 0;
  partial = 0;
  bool answer_used[3] = { false, false, false };
  bool guess_used[3] = { false, false, false };
  for (int i = 0; i < 3; i++) {
    if (guess[i] == answer[i]) {
      exact++;
      answer_used[i] = true;
      guess_used[i] = true;
    }
  }
  for (int i = 0; i < 3; i++) {
    if (guess_used[i])
      continue;
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
    if (i < length - 1)
      Serial.print(", ");
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

// Fetch and parse player names from Firestore
void fetchPlayersFromFirestore() {
  playerNames.clear();
  playerDocIds.clear();
  playerCount = 0;

  String url = "https://firestore.googleapis.com/v1/projects/" + String(projectId) + "/databases/(default)/documents/" + collection + "?key=" + apiKey;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, url);
  int httpResponseCode = http.GET();
  String payload = http.getString();
  int httpCode = http.GET();
  http.end();

  Serial.println("Firestore Players Collection Response:");
  Serial.println(payload);

  playerCount = 0;
  StaticJsonDocument<8192> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (!error && doc.containsKey("documents")) {
    JsonArray docs = doc["documents"].as<JsonArray>();
    for (JsonObject d : docs) {
      if (d.containsKey("fields") && d["fields"].containsKey("username")) {
        String name = d["fields"]["username"]["stringValue"].as<String>();
        playerNames.push_back(name);
        playerCount++;

        // Extract document ID from "name" field
        String docPath = d["name"].as<String>();  // e.g. ".../users/ABC123"
        int lastSlash = docPath.lastIndexOf('/');
        String docId = docPath.substring(lastSlash + 1);
        playerDocIds.push_back(docId);
      }
    }
  } else {
    Serial.print("deserializeJson() failed or no documents: ");
    Serial.println(error.c_str());
  }

  maxToShow = min(4, (int)playerNames.size());

  Serial.println("Player names:");
  for (int i = 0; i < playerCount; i++) {
    Serial.println(playerNames[i]);
  }
}

void uploadCodeBreakerSession(const String &userDocId, int stars, int score) {
  String url = "https://firestore.googleapis.com/v1/projects/";
  url += projectId;
  url += "/databases/(default)/documents/users/";
  url += userDocId;  // Use the Firestore document ID, not the display name!
  url += "/games/code_breaker_game/sessions?key=";
  url += apiKey;

  DynamicJsonDocument doc(256);
  doc["fields"]["coins"]["integerValue"] = stars;
  doc["fields"]["score"]["integerValue"] = score * 10;

  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(payload);
  if (httpCode == 200) {
    Serial.println("Session uploaded successfully!");
  } else {
    Serial.print("Failed to upload session. HTTP code: ");
    Serial.println(httpCode);
    Serial.println(http.getString());
  }
  http.end();
}

void uploadVisualMemorySession(const String &userDocId, int coins, int score) {
  String url = "https://firestore.googleapis.com/v1/projects/";
  url += projectId;
  url += "/databases/(default)/documents/users/";
  url += userDocId;
  url += "/games/visual_memory_challenge/sessions?key=";  // <-- updated path
  url += apiKey;

  DynamicJsonDocument doc(256);
  doc["fields"]["coins"]["integerValue"] = coins;
  doc["fields"]["score"]["integerValue"] = score * 10;

  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(payload);
  if (httpCode == 200) {
    Serial.println("Visual Memory Challenge session uploaded successfully!");
  } else {
    Serial.print("Failed to upload Visual Memory Challenge session. HTTP code: ");
    Serial.println(httpCode);
    Serial.println(http.getString());
  }
  http.end();
}
int fetchHighScore(const String &userDocId, const String &gameName) {
  String url = "https://firestore.googleapis.com/v1/projects/";
  url += projectId;
  url += "/databases/(default)/documents/users/";
  url += userDocId;
  url += "/games/";
  url += gameName;
  url += "?key=";
  url += apiKey;

  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  int highScore = 0;
  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(512);
    deserializeJson(doc, payload);
    if (doc["fields"]["high_score"]["integerValue"]) {
      highScore = doc["fields"]["high_score"]["integerValue"].as<int>();
    }
  }
  http.end();
  return highScore;
}
void updateHighScore(const String &userDocId, const String &gameName, int newHighScore) {
  String url = "https://firestore.googleapis.com/v1/projects/";
  url += projectId;
  url += "/databases/(default)/documents/users/";
  url += userDocId;
  url += "/games/";
  url += gameName;
  url += "?key=";
  url += apiKey;

  DynamicJsonDocument doc(128);
  doc["fields"]["high_score"]["integerValue"] = newHighScore * 20;

  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.PATCH(payload);  // PATCH to update only the field
  if (httpCode == 200) {
    Serial.println("High score updated!");
  } else {
    Serial.print("Failed to update high score. HTTP code: ");
    Serial.println(httpCode);
    Serial.println(http.getString());
  }
  http.end();
}

void uploadLedReactionSession(const String &userDocId, int coins, int score) {
  String url = "https://firestore.googleapis.com/v1/projects/";
  url += projectId;
  url += "/databases/(default)/documents/users/";
  url += userDocId;
  url += "/games/LED_reaction_game/sessions?key=";
  url += apiKey;

  DynamicJsonDocument doc(256);
  doc["fields"]["coins"]["integerValue"] = coins;
  doc["fields"]["score"]["integerValue"] = score;

  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(payload);
  if (httpCode == 200) {
    Serial.println("LED Reaction Game session uploaded successfully!");
  } else {
    Serial.print("Failed to upload LED Reaction Game session. HTTP code: ");
    Serial.println(httpCode);
    Serial.println(http.getString());
  }
  http.end();
}




void uploadColorWordSession(const String &userDocId, int coins, int score) {
  String url = "https://firestore.googleapis.com/v1/projects/";
  url += projectId;
  url += "/databases/(default)/documents/users/";
  url += userDocId;
  url += "/games/color_word_game/sessions?key=";
  url += apiKey;

  DynamicJsonDocument doc(256);
  doc["fields"]["coins"]["integerValue"] = coins;
  doc["fields"]["score"]["integerValue"] = score;

  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(payload);
  if (httpCode == 200) {
    Serial.println("Color Word Game session uploaded successfully!");
  } else {
    Serial.print("Failed to upload Color Word Game session. HTTP code: ");
    Serial.println(httpCode);
    Serial.println(http.getString());
  }
  http.end();
}



void showPlayerMenu(int selected = -1) {
  inputIndex = 0;
  inputBuffer[0] = inputBuffer[1] = inputBuffer[2] = inputBuffer[3] = '\0';

  display.fillScreen(WHITE);

  // --- Title ---
  display.setTextColor(BLACK);
  display.setTextSize(2);
  const char *title = "Select player:";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 32);
  display.print(title);

  // --- Button dimensions ---
  int btnWidth = 180;
  int btnHeight = 48;
  int btnX = (SCREEN_WIDTH - btnWidth) / 2;
  int btnYStart = 80;
  int btnSpacing = 16;
  int radius = 14;

  // --- Button colors ---
  uint16_t btnColors[] = {0x3A99, 0xFFE0, 0xC618, 0xF800, 0x07E0}; // blue, yellow, grey, red, green

  int shown = 0;
  for (int i = 0; i < maxToShow; i++) {
    int btnY = btnYStart + i * (btnHeight + btnSpacing);
    uint16_t color = btnColors[i % 5];

    // Highlight selected player (if any)
    if (i == selected) {
      display.drawRoundRect(btnX-3, btnY-3, btnWidth+6, btnHeight+6, radius+3, BLACK);
    }

    display.fillRoundRect(btnX, btnY, btnWidth, btnHeight, radius, color);

    // Center player name in button
    display.setTextColor(BLACK);
    display.setTextSize(2);
    String name = playerNames[playerDisplayOffset + i];
    int16_t nx1, ny1;
    uint16_t nw, nh;
    display.getTextBounds(name.c_str(), 0, 0, &nx1, &ny1, &nw, &nh);
    int nameX = btnX + (btnWidth - nw) / 2;
    int nameY = btnY + (btnHeight - nh) / 2;
    display.setCursor(nameX, nameY);
    display.print(name);
    ++shown;
  }

  // --- Logout at bottom right ---
  const char *logoutText = "# logout";
  int16_t lx1, ly1;
  uint16_t lw, lh;
  display.setTextSize(1);
  display.setTextColor(DARKGREY);
  display.getTextBounds(logoutText, 0, 0, &lx1, &ly1, &lw, &lh);
  display.setCursor(SCREEN_WIDTH - lw, SCREEN_HEIGHT - 10);
  display.print(logoutText);
}

// Show player selection menu with fetched names
/*void showPlayerMenu() {
  inputIndex = 0;
  inputBuffer[0] = inputBuffer[1] = inputBuffer[2] = inputBuffer[3] = '\0';

  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);
  display.setCursor(20, 60);
  display.print("Select player:");
  int y = 100;
  int shown = 0;
  for (int i = 0; i < maxToShow; i++) {
    display.setCursor(20, y);
    display.print(String(i + 1) + ") " + playerNames[playerDisplayOffset + i]);
    y += 30;
    ++shown;
  }

  // --- Add this for # logout at bottom right ---
  const char *logoutText = "# logout";
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(1);
  display.setTextColor(DARKGREY);
  display.getTextBounds(logoutText, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(SCREEN_WIDTH - w, SCREEN_HEIGHT - 10);
  display.print(logoutText);
}*/

// Show loading screen with a circle and "loading" text
// This function can be called while fetching data or performing long operations
void showLoadingScreen() {
  display.fillScreen(WHITE);
  // Draw a circle in the center
  int centerX = SCREEN_WIDTH / 2;
  int centerY = SCREEN_HEIGHT / 2 - 30;
  int radius = 40;
  display.drawCircle(centerX, centerY, radius, BLACK);
  // Optionally, fill the circle
  // display.fillCircle(centerX, centerY, radius, LIGHTGREY);

  // Draw "loading" text under the circle
  display.setTextColor(BLACK);
  display.setTextSize(2);
  int16_t x1, y1;
  uint16_t w, h;
  const char *loadingText = "loading";
  display.getTextBounds(loadingText, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(centerX - w / 2, centerY + radius + 20);
  display.print(loadingText);
}

// Show the color on the rings of NeoPixel LEDs
void showColorOnRings(uint8_t colorIndex) {
  // Turn off all LEDs first
  for (uint8_t i = 0; i < WS2812_NUM_LEDS; i++) {
    leds.setPixelColor(i, 0);
  }

  uint32_t color = 0;
  if (colorIndex == 0) {  // Red
    color = leds.Color(255, 0, 0);
  } else if (colorIndex == 1) {  // Blue
    color = leds.Color(0, 0, 255);
  } else if (colorIndex == 2) {  // Green
    color = leds.Color(0, 255, 0);
  }

  // Light up the corresponding ring
  uint8_t ringStart = colorIndex * LEDS_PER_RING;
  for (uint8_t i = 0; i < LEDS_PER_RING; i++) {
    leds.setPixelColor(ringStart + i, color);
  }
  leds.show();
}

// Turn off all rings of NeoPixel LEDs
void turnOffAllRings() {
  for (uint8_t i = 0; i < WS2812_NUM_LEDS; i++) {
    leds.setPixelColor(i, 0);
  }
  leds.show();
}

// Show the number of tries remaining for the current game
void showTriesRemaining(int triesRemaining) {
  int y = 220;                                      // Just below the "Last try" line (which is at y=150)
  display.fillRect(0, y, SCREEN_WIDTH, 30, WHITE);  // Clear previous message
  display.setTextColor(DARKGREY);
  display.setTextSize(2);
  display.setCursor(20, y);
  display.print(triesRemaining);
  display.print(" tries remaining");
}

// Show stars and score centered on the display
void showCenteredStarsAndScore(int stars) {
  display.fillScreen(WHITE);  // Clear everything

  // Prepare stars string
  String starsStr = "";
  for (int i = 0; i < stars; i++) {
    starsStr += "* ";
  }

  // Center stars
  display.setTextSize(3);
  display.setTextColor(GREEN);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(starsStr.c_str(), 0, 0, &x1, &y1, &w, &h);
  int starsX = (SCREEN_WIDTH - w) / 2;
  int starsY = (SCREEN_HEIGHT / 2) - 40;
  display.setCursor(starsX, starsY);
  display.print(starsStr);

  // Center score
  String scoreStr = "Score: " + String(stars * 2);
  display.setTextSize(2);
  display.setTextColor(BLACK);
  display.getTextBounds(scoreStr.c_str(), 0, 0, &x1, &y1, &w, &h);
  int scoreX = (SCREEN_WIDTH - w) / 2;
  int scoreY = starsY + 50;
  display.setCursor(scoreX, scoreY);
  display.print(scoreStr);
}

// Show the title for the LED reaction game
void showLedReactionTitle() {
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);

  const char *title = "Led Reaction";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - w) / 2;
  int y = 100;  // Adjust vertically if you want

  display.setCursor(x, y);
  display.print(title);
  delay(2000);

  display.fillScreen(WHITE);
  showBottomHints();
}

// Show the LED reaction color based on the index
void showLedReactionColor(int colorIdx) {
  // Show on screen
  display.fillScreen(colorValues[colorIdx]);
  display.setTextSize(3);
  display.setTextColor(WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(colorNames[colorIdx], 0, 0, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - w) / 2;
  int y = (SCREEN_HEIGHT - 3 * 8) / 2;
  display.setCursor(x, y);
  display.print(colorNames[colorIdx]);

  // Show on LED ring
  showColorOnRings(colorIdx);
}

// Function to determine the number of stars based on coins
int ledReactionStars(int coins) {
  if (coins >= 14)
    return 5;
  if (coins >= 10)
    return 4;
  if (coins >= 7)
    return 3;
  if (coins >= 4)
    return 2;
  if (coins >= 1)
    return 1;
  return 0;
}

void showEnterSecretPrompt(uint8_t player) {
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);
  display.setCursor(20, 80);
  display.print("Player ");
  display.print(player);
  display.print(": Enter secret");
  display.setCursor(20, 120);
  display.print("number (3 digits)");
}

void showMaskedInputProgress(const char *inputBuffer, byte inputIndex) {
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
      display.print("*");  // Masked
    } else {
      display.print("-");
    }
    if (i < 2)
      display.print(" ");
  }
}

// Show the last feedback for a player in code breaker multiplayer
void showLastFeedback(uint8_t player, const char *lastGuess, int lastExact, int lastPartial) {
  display.fillScreen(WHITE);
  display.setTextSize(2);
  display.setTextColor(BLACK);
  display.setCursor(20, 60);
  display.print("Player ");
  display.print(player);
  display.print(" Last: ");
  display.print(lastGuess);
  display.setCursor(20, 100);
  display.setTextColor(GREEN);
  display.print("Exact: ");
  display.print(lastExact);
  display.setTextColor(RED);
  display.setCursor(20, 140);
  display.print("Partial: ");
  display.print(lastPartial);
}

void setup() {
  // Initialize NeoPixel LEDs
  leds.begin();
  leds.show();  // Initialize all LEDs to 'off'

  Serial.begin(9600);
  display.begin();
  display.setRotation(0);
  randomSeed(analogRead(0));

  // Show loading screen while connecting to Wi-Fi
  showLoadingScreen();

  // Connect to Wi-Fi
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);

#ifdef WOKWI_SIMULATION
  // Simple WiFi connection for Wokwi simulation
  Serial.println("Connecting to Wokwi simulation WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#else
  // WPA2-Enterprise connection for real hardware
  Serial.println("Connecting to WPA2-Enterprise WiFi...");
  esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)WIFI_USERNAME, strlen(WIFI_USERNAME));
  esp_wifi_sta_wpa2_ent_set_username((uint8_t *)WIFI_USERNAME, strlen(WIFI_USERNAME));
  esp_wifi_sta_wpa2_ent_set_password((uint8_t *)WIFI_PASSWORD, strlen(WIFI_PASSWORD));
  esp_wifi_sta_wpa2_ent_enable();
  WiFi.begin(WIFI_SSID);
#endif

  Serial.print("Connecting to Wi-Fi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    Serial.print(".");
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi");
  }

  // showLoadingScreen();
  fetchPlayersFromFirestore();
  showModeSelect();

  currentState = MODE_SELECT;

  pinMode(RED_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BLUE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(GREEN_BUTTON_PIN, INPUT_PULLUP);
}
void showRivalryScreen(byte player1, byte player2)
{
  display.fillScreen(WHITE);

  // --- Colors ---
  uint16_t p1Color = 0x3A99; // Blue
  uint16_t p2Color = 0xFD20; // Orange

  // --- Player 1 box and icon ---
  int boxW = 90, boxH = 90;
  int boxY = (SCREEN_HEIGHT - boxH) / 2;
  int box1X = 24;
  display.fillRoundRect(box1X, boxY, boxW, boxH, 18, p1Color);
  int icon1X = box1X + boxW/2, icon1Y = boxY + 30;
  display.fillCircle(icon1X, icon1Y, 18, WHITE); // Head
  display.fillRoundRect(icon1X - 18, icon1Y + 12, 36, 18, 9, WHITE); // Shoulders

  // Player 1 name, centered in box
  display.setTextSize(1);
  display.setTextColor(WHITE);
  String name1 = (player1 >= 1 && player1 <= playerCount) ? playerNames[player1-1] : "Player 1";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(name1.c_str(), 0, 0, &x1, &y1, &w, &h);
  display.setCursor(box1X + (boxW - w)/2, boxY + boxH - 22);
  display.print(name1);

  // --- Player 2 box and icon ---
  int box2X = SCREEN_WIDTH - boxW - 24;
  display.fillRoundRect(box2X, boxY, boxW, boxH, 18, p2Color);
  int icon2X = box2X + boxW/2, icon2Y = boxY + 30;
  display.fillCircle(icon2X, icon2Y, 18, WHITE); // Head
  display.fillRoundRect(icon2X - 18, icon2Y + 12, 36, 18, 9, WHITE); // Shoulders

  // Player 2 name, centered in box
  String name2 = (player2 >= 1 && player2 <= playerCount) ? playerNames[player2-1] : "Player 2";
  display.getTextBounds(name2.c_str(), 0, 0, &x1, &y1, &w, &h);
  display.setCursor(box2X + (boxW - w)/2, boxY + boxH - 22);
  display.print(name2);

  // --- VS in the center ---
  display.setTextSize(4);
  display.setTextColor(DARKGREY);
  const char* vsText = "VS";
  display.getTextBounds(vsText, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w)/2, boxY + (boxH - h)/2 + 8);
  display.print(vsText);

  delay(1200); // Show for 1.2 seconds
}

void loop() {
  char key = keypad.getKey();
  switch (currentState) {
    case MODE_SELECT:
      {
        if (key == '1') {
          showPlayerMenu();
          currentState = PLAYER_SELECT;
          offlineMode = false;
          delay(150);
          while (keypad.getKey() != NO_KEY) {
          }
        } else if (key == '2') {
          showMultiplayerPlayerSelect1();
          currentState = PLAYER1_SELECT;
          offlineMode = false;
          delay(150);
          while (keypad.getKey() != NO_KEY) {
          }
        } else if (key=='3'){
    offlineMode = true;
    showMenu();
    currentState = MENU; // or whatever your game select state is
  }
        break;
      }

    case PLAYER1_SELECT:
      {
        if (key == '9') {
          if (playerDisplayOffset + 4 < playerNames.size()) {
            playerDisplayOffset += 4;
            showMultiplayerPlayerSelect1();
          }
          break;
        }
        if (key == '#') {
          playerDisplayOffset = 0;  // <-- Reset the offset!
          showModeSelect();
          currentState = MODE_SELECT;
          return;
        }

        if (key && key >= '1' && key <= '0' + maxToShow) {
          int selection = key - '1';  // 0-based index for button pressed (0,1,2,3)
          int actualIndex = playerDisplayOffset + selection;
          if (actualIndex < playerNames.size()) {
            multiplayerPlayer1 = actualIndex;                  // 0-based index for player
            playerDisplayOffset = 0;                           // Reset for next selection
            showMultiplayerPlayerSelect2(multiplayerPlayer1);  // Pass 0-based index to exclude
            currentState = PLAYER2_SELECT;
            delay(150);
            while (keypad.getKey() != NO_KEY) {
            }
          }
        }
        break;
      }

    case PLAYER2_SELECT:
      {
        if (key == '9') {
          if (playerDisplayOffset + 4 < playerNames.size()) {
            playerDisplayOffset += 4;
            showMultiplayerPlayerSelect2(multiplayerPlayer1);
          }
          break;
        }
        if (key == '#') {
          playerDisplayOffset = 0;  // <-- Reset the offset!
          showModeSelect();
          currentState = MODE_SELECT;
          return;  // or break; if not in a function
        }

        if (key && key >= '1' && key <= '0' + maxToShow) {
          int option = key - '1';            // 0-based option (0,1,2,3)
          int exclude = multiplayerPlayer1;  // 0-based index to skip
          int chosenIdx = -1;
          int count = 0;
          // Loop through the currently displayed players
          for (int i = playerDisplayOffset; i < playerNames.size() && count < maxToShow; i++) {
            if (i == exclude)
              continue;  // skip the chosen player
            if (count == option) {
              chosenIdx = i;
              break;
            }
            count++;
          }
          if (chosenIdx != -1) {
            multiplayerPlayer2 = chosenIdx;
        showRivalryScreen(multiplayerPlayer1+1, multiplayerPlayer2+1);
            showMultiplayerMenu();
            currentState = MULTI_MENU;
            delay(150);
            while (keypad.getKey() != NO_KEY) {
            }
          }
        }
        break;
      }

    case MULTI_MENU:
      {
        if (key == '#') {
          playerDisplayOffset = 0;  // <-- Reset the offset!
          showModeSelect();
          currentState = MODE_SELECT;
          return;  // or break; if not in a function
        }

        if (key == '1') {
          // Reset multiplayer Code Breaker variables
          memset(player1Secret, 0, sizeof(player1Secret));
          memset(player2Secret, 0, sizeof(player2Secret));
          memset(player1Guess, 0, sizeof(player1Guess));
          memset(player2Guess, 0, sizeof(player2Guess));
          player1Tries = 0;
          player2Tries = 0;
          player1Guessed = false;
          player2Guessed = false;
          codeBreakerMultiplayerTurn = false;
          cbMultiInputIndex = 0;

          currentState = CODE_BREAKER_MULTI_SECRET1;
          break;
        }

        // If any other key is pressed, show "Not implemented"
        if (key && key != NO_KEY) {
          display.fillScreen(WHITE);
          display.setTextColor(RED);
          display.setTextSize(2);
          display.setCursor(20, 120);
          display.print("Not implemented");
          delay(1200);
          showMultiplayerMenu();  // Redraw the menu
        }
        break;
      }

    case CODE_BREAKER_MULTI_SECRET1:
      {
        // Show prompt only once when entering the state
        static bool promptShown1 = false;
        if (!promptShown1) {
          showEnterSecretPrompt(1);
          showMaskedInputProgress(player1Secret, cbMultiInputIndex);
          promptShown1 = true;
        }

        if (key && key >= '0' && key <= '9' && cbMultiInputIndex < 3) {
          player1Secret[cbMultiInputIndex++] = key;
          player1Secret[cbMultiInputIndex] = '\0';
          showMaskedInputProgress(player1Secret, cbMultiInputIndex);  // Update input line only
        }

        if (cbMultiInputIndex == 3) {
          delay(500);
          display.fillRect(0, 190, SCREEN_WIDTH, 30, WHITE);  // Clear input line
          delay(300);
          cbMultiInputIndex = 0;
          promptShown1 = false;  // Reset for next time
          currentState = CODE_BREAKER_MULTI_SECRET2;
        }
        break;
      }

    case CODE_BREAKER_MULTI_SECRET2:
      {
        static bool promptShown2 = false;
        if (!promptShown2) {
          showEnterSecretPrompt(2);
          showMaskedInputProgress(player2Secret, cbMultiInputIndex);
          promptShown2 = true;
        }

        if (key && key >= '0' && key <= '9' && cbMultiInputIndex < 3) {
          player2Secret[cbMultiInputIndex++] = key;
          player2Secret[cbMultiInputIndex] = '\0';
          showMaskedInputProgress(player2Secret, cbMultiInputIndex);
        }

        if (cbMultiInputIndex == 3) {
          delay(500);
          display.fillRect(0, 190, SCREEN_WIDTH, 30, WHITE);
          delay(300);
          codeBreakerMultiplayerTurn = false;
          cbMultiInputIndex = 0;
          promptShown2 = false;  // Reset for next time
          // Now move to guessing phase...
          currentState = CODE_BREAKER_MULTI_TURN_P1;
        }
        break;
      }

    case CODE_BREAKER_MULTI_TURN_P1:
      {
        static bool feedbackShown = false;
        if (!feedbackShown) {
          if (player1Tries > 0) {
            showLastFeedback(1, player1LastGuess, player1LastExact, player1LastPartial);
            delay(2000);
          }
          showGuessScreen(1, player1Tries);
          showInputProgress(player1Guess, 0);
          feedbackShown = true;
        }
        if (key && key >= '0' && key <= '9' && cbMultiInputIndex < 3) {
          player1Guess[cbMultiInputIndex++] = key;
          showInputProgress(player1Guess, cbMultiInputIndex);
        }
        if (cbMultiInputIndex == 3) {
          player1Guess[3] = '\0';
          int exact = 0, partial = 0;
          cbMultiCountMatches(player1Guess, player2Secret, exact, partial);
          player1Tries++;

          // Store feedback for next turn
          strcpy(player1LastGuess, player1Guess);
          player1LastExact = exact;
          player1LastPartial = partial;

          // Show feedback
          display.fillScreen(WHITE);
          display.setTextSize(2);
          display.setTextColor(BLACK);
          display.setCursor(20, 60);
          display.print("Player 1 Guess: ");
          display.print(player1Guess);
          display.setCursor(20, 100);
          display.setTextColor(GREEN);
          display.print("Exact: ");
          display.print(exact);
          display.setTextColor(RED);
          display.setCursor(20, 140);
          display.print("Partial: ");
          display.print(partial);
          delay(1500);

          if (exact == 3) {
            display.fillScreen(WHITE);
            display.setTextColor(GREEN);
            display.setTextSize(2);
            display.setCursor(20, 100);
            display.print("Player 1 Wins!");
            display.setCursor(20, 140);
            display.setTextSize(1);
            display.setTextColor(BLACK);
            display.print("Tries: ");
            display.print(player1Tries);
            delay(2500);
            showMenu();
            currentState = MENU;
            feedbackShown = false;
            cbMultiInputIndex = 0;
            break;
          }
          // Next player's turn
          cbMultiInputIndex = 0;
          memset(player1Guess, 0, sizeof(player1Guess));
          feedbackShown = false;
          currentState = CODE_BREAKER_MULTI_TURN_P2;  // Switch to Player 2
        }
        break;
      }

    case CODE_BREAKER_MULTI_TURN_P2:
      {
        static bool feedbackShown = false;
        if (!feedbackShown) {
          if (player2Tries > 0) {
            showLastFeedback(2, player2LastGuess, player2LastExact, player2LastPartial);
            delay(2000);
          }
          showGuessScreen(2, player2Tries);
          showInputProgress(player2Guess, 0);
          feedbackShown = true;
        }
        if (key && key >= '0' && key <= '9' && cbMultiInputIndex < 3) {
          player2Guess[cbMultiInputIndex++] = key;
          showInputProgress(player2Guess, cbMultiInputIndex);
        }
        if (cbMultiInputIndex == 3) {
          player2Guess[3] = '\0';
          int exact = 0, partial = 0;
          cbMultiCountMatches(player2Guess, player1Secret, exact, partial);
          player2Tries++;

          // Store feedback for next turn
          strcpy(player2LastGuess, player2Guess);
          player2LastExact = exact;
          player2LastPartial = partial;

          // Show feedback
          display.fillScreen(WHITE);
          display.setTextSize(2);
          display.setTextColor(BLACK);
          display.setCursor(20, 60);
          display.print("Player 2 Guess: ");
          display.print(player2Guess);
          display.setCursor(20, 100);
          display.setTextColor(GREEN);
          display.print("Exact: ");
          display.print(exact);
          display.setTextColor(RED);
          display.setCursor(20, 140);
          display.print("Partial: ");
          display.print(partial);
          delay(1500);

          if (exact == 3) {
            display.fillScreen(WHITE);
            display.setTextColor(GREEN);
            display.setTextSize(2);
            display.setCursor(20, 100);
            display.print("Player 2 Wins!");
            display.setCursor(20, 140);
            display.setTextSize(1);
            display.setTextColor(BLACK);
            display.print("Tries: ");
            display.print(player2Tries);
            delay(2500);
            showMenu();
            currentState = MENU;
            feedbackShown = false;
            cbMultiInputIndex = 0;
            break;
          }
          // Next player's turn
          cbMultiInputIndex = 0;
          memset(player2Guess, 0, sizeof(player2Guess));
          feedbackShown = false;
          currentState = CODE_BREAKER_MULTI_TURN_P1;  // Switch to Player 1
        }
        break;
      }

    case PLAYER_SELECT:
      {
        if (key == '9') {
          if (playerDisplayOffset + 4 < playerNames.size()) {
            playerDisplayOffset += 4;
            // Redraw the selection menu with new offset
            showPlayerMenu();
          }
          break;
        }
        if (key == '#') {
          playerDisplayOffset = 0;  // <-- Reset the offset!
          showModeSelect();
          currentState = MODE_SELECT;
          return;
        }

        if (key && key >= '1' && key <= '4' && (key - '0') <= maxToShow) {
          int selection = key - '1';  // 0-based index for key pressed (0 for '1', 1 for '2', etc.)
          int actualIndex = playerDisplayOffset + selection;
          if (actualIndex < playerNames.size()) {
            currentPlayer = actualIndex;        // 1-based index if you want, or just use actualIndex
            showPlayerSelected(currentPlayer+1);  // Pass actualIndex+1 or actualIndex as needed
            showMenu();
            currentState = MENU;
            playerDisplayOffset = 0;
          }
        }
        break;
      }

    case MENU:

      {
        if (key) {
          if (key == '#') {
            playerDisplayOffset = 0;  // <-- Reset the offset!
            showModeSelect();
            currentState = MODE_SELECT;
            codeBreakerWrongTries = 0;
            visualMemoryWrongTries = 0;
            break;
          }
          Serial.print("Key pressed: ");
          Serial.println(key);

          //  Code Breaker
          if (key == '1') {
            generateNewRandomNumber();
            inputIndex = 0;
            codeBreakerWrongTries = 0;  // Reset here the number of tries
            showInputProgress(inputBuffer, inputIndex);
            currentState = CODE_BREAKER_DIFFICULTY_SELECT;
            showCodeBreakerDifficultyMenu();
            if (key == '*') {
              display.fillRect(0, 220, SCREEN_WIDTH, 30, WHITE);
              showMenu();
              currentState = MENU;
              codeBreakerWrongTries = 0;
              visualMemoryWrongTries = 0;
              break;
            }
            if (key == '#') {
              playerDisplayOffset = 0;  // <-- Reset the offset!
              showModeSelect();
              currentState = MODE_SELECT;
              codeBreakerWrongTries = 0;
              visualMemoryWrongTries = 0;
              break;
            }
          } else if (key == '2') {
            currentState = VISUAL_MEMORY_DIFFICULTY_SELECT;
            showVisualMemoryDifficultyMenu();
          }
          // --- Color-Word Challenge menu handler ---
          else if (key == '3') {
            showColorWordDifficultySelect();
            currentState = COLOR_WORD_DIFFICULTY_SELECT;
          } else if (key == '4') {
            showLedReactionDifficultySelect();
            currentState = LED_REACTION_DIFFICULTY_SELECT;
          }

          else {
            showMenuMessage("Please choose 1, 2, 3 or 4");
          }
        }
        break;
      }

    case VISUAL_MEMORY_DIFFICULTY_SELECT:
      {
        if (key) {
          if (key == '*') {
            display.fillRect(0, 220, SCREEN_WIDTH, 30, WHITE);
            showMenu();
            currentState = MENU;
            codeBreakerWrongTries = 0;
            visualMemoryWrongTries = 0;
            break;
          }
          if (key == '#') {
            playerDisplayOffset = 0;  // <-- Reset the offset!
            showModeSelect();
            currentState = MODE_SELECT;
            codeBreakerWrongTries = 0;
            visualMemoryWrongTries = 0;
            break;
          }
          switch (key) {
            case '1':  // Easy
              colorSequenceLength = 5;
              break;
            case '2':  // Medium
              colorSequenceLength = 8;
              break;
            case '3':  // Hard
              colorSequenceLength = 10;
              break;
            default:
              // Ignore other keys
              return;
          }
          visualMemoryWrongTries = 0;  // Reset tries

          // Now start the Visual Memory game as before:
          generateRandomColorSequence(colorSequence, colorSequenceLength);
          for (uint8_t i = 0; i < colorSequenceLength; i++) {
            showColorOnDisplay(colorSequence[i]);
            showColorOnRings(colorSequence[i]);
            delay(2000);
            turnOffAllRings();
            if (i < colorSequenceLength - 1) {
              display.fillScreen(WHITE);
              delay(1000);
            }
            turnOffAllRings();
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
        }
        break;
      }

    case VISUAL_MEMORY_INPUT:
      {
        if (key == '*') {
          display.fillRect(0, 220, SCREEN_WIDTH, 30, WHITE);
          showMenu();
          currentState = MENU;
          codeBreakerWrongTries = 0;
          visualMemoryWrongTries = 0;
          break;
        }
        if (key == '#') {
          playerDisplayOffset = 0;  // <-- Reset the offset!
          showModeSelect();
          currentState = MODE_SELECT;
          codeBreakerWrongTries = 0;
          visualMemoryWrongTries = 0;
          break;
        }

        // --- Handle button presses for visual memory game ---
        int buttonPins[3] = { RED_BUTTON_PIN, BLUE_BUTTON_PIN, GREEN_BUTTON_PIN };
        const char *buttonNames[3] = { "Red button pressed", "Blue button pressed", "Green button pressed" };
        for (int i = 0; i < 3; i++) {
          int reading = digitalRead(buttonPins[i]);
          if (lastButtonState[i] == HIGH && reading == LOW && (millis() - lastDebounceTime[i]) > debounceDelay) {
            lastDebounceTime[i] = millis();
            Serial.println(buttonNames[i]);
            if (i == colorSequence[currentStep]) {
              currentStep++;
              if (currentStep == colorSequenceLength) {
                int stars = 10 - visualMemoryWrongTries;
                showCenteredStarsAndScore(stars);
                uploadVisualMemorySession(playerDocIds[currentPlayer], stars, stars * 2);
                String gameName = "visual_memory_challenge";
                int currentHighScore = fetchHighScore(playerDocIds[currentPlayer], gameName);
                if (stars * 20 > currentHighScore) {
                  updateHighScore(playerDocIds[currentPlayer], gameName, stars);
                }             // Show only stars and score, centered
                delay(2000);  // Show for 2 seconds
                if (!offlineMode) {
                display.fillScreen(WHITE);
                delay(500);
int currentHighScore2 = fetchHighScore(playerDocIds[currentPlayer], gameName);

// Top Centered Label
String scoreStr = "HighScore: " + String(currentHighScore2);
display.setTextSize(2);
display.setTextColor(BLACK);
int labelWidth = scoreStr.length() * 12;
int labelX = (240 - labelWidth) / 2;
int labelY = 10;
display.setCursor(labelX, labelY);
display.print(scoreStr);

// Centered Score Value
String scoreValue = String(currentHighScore2);
display.setTextSize(3);
int scoreWidth = scoreValue.length() * 18;
int scoreHeight = 24;
int scoreX = (240 - scoreWidth) / 2;
int scoreY = (320 - scoreHeight) / 2;
display.setCursor(scoreX, scoreY);
display.print(scoreValue);

delay(2000);
              }
                showMenu();   // Return to games menu
                currentState = MENU;
                visualMemoryWrongTries = 0;  // Reset tries
                break;
              }
            } else {
              visualMemoryWrongTries++;  // Increment on wrong try
              // Check if the number of wrong tries exceeds the maximum allowed
              if (visualMemoryWrongTries >= maxWrongTries_VM) {
                display.fillScreen(WHITE);
                display.setTextColor(RED);
                display.setTextSize(2);
                display.setCursor(20, 120);
                display.print("Out of tries!");
                uploadVisualMemorySession(playerDocIds[currentPlayer], 0, 0);

                delay(2000);
                display.fillRect(0, 220, SCREEN_WIDTH, 30, WHITE);
                showMenu();
                currentState = MENU;
                visualMemoryWrongTries = 0;  // Reset for next game
                break;
              }

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
              showTriesRemaining(maxWrongTries_VM - visualMemoryWrongTries);
              currentStep = 0;
            }
          }
          lastButtonState[i] = reading;
        }
        break;
      }

    case LED_REACTION_DIFFICULTY_SELECT:
      {
        if (key == '*') {
          showMenu();
          currentState = MENU;
          colorWordWrongTries = 0;
          colorWordCurrentStep = 0;
          break;
        }
        if (key == '#') {
          playerDisplayOffset = 0;  // <-- Reset the offset!
          showModeSelect();
          currentState = MODE_SELECT;
          colorWordWrongTries = 0;
          colorWordCurrentStep = 0;
          break;
        }
        if (key == '1') {
          ledReactionDifficulty = LED_EASY;
          ledReactionStepDuration = ledReactionDurations[LED_EASY];
          // Prepare game
          ledReactionCorrect = 0;
          ledReactionStartTime = millis();
          ledReactionActive = true;
          ledReactionWaiting = false;
          showNextLedReactionColor();
          currentState = LED_REACTION;
        } else if (key == '2') {
          ledReactionDifficulty = LED_MEDIUM;
          ledReactionStepDuration = ledReactionDurations[LED_MEDIUM];
          // Prepare game
          ledReactionCorrect = 0;
          ledReactionStartTime = millis();
          ledReactionActive = true;
          ledReactionWaiting = false;
          showNextLedReactionColor();
          currentState = LED_REACTION;
        } else if (key == '3') {
          ledReactionDifficulty = LED_HARD;
          ledReactionStepDuration = ledReactionDurations[LED_HARD];
          // Prepare game
          ledReactionCorrect = 0;
          ledReactionStartTime = millis();
          ledReactionActive = true;
          ledReactionWaiting = false;
          showNextLedReactionColor();
          currentState = LED_REACTION;
        }
        break;
      }

    case COLOR_WORD_DIFFICULTY_SELECT:
      {
        if (key == '*') {
          showMenu();
          currentState = MENU;
          colorWordWrongTries = 0;
          colorWordCurrentStep = 0;
          break;
        }
        if (key == '#') {
          playerDisplayOffset = 0;  // <-- Reset the offset!
          showModeSelect();
          currentState = MODE_SELECT;
          colorWordWrongTries = 0;
          colorWordCurrentStep = 0;
          break;
        }
        if (key == '1') {
          colorWordStepDuration = colorWordDurations[EASY];
          colorWordDifficulty = EASY;
          // Prepare game
          generateColorWordChallengeSequences(cwcSeq1, cwcSeq2, COLOR_WORD_CHALLENGE_LENGTH);
          colorWordCurrentStep = 0;
          colorWordWrongTries = 0;
          colorWordStepStartTime = 0;
          showColorWordChallengeStep(0);
          currentState = COLOR_WORD_CHALLENGE;
        } else if (key == '2') {
          colorWordStepDuration = colorWordDurations[MEDIUM];
          colorWordDifficulty = MEDIUM;
          // Prepare game
          generateColorWordChallengeSequences(cwcSeq1, cwcSeq2, COLOR_WORD_CHALLENGE_LENGTH);
          colorWordCurrentStep = 0;
          colorWordWrongTries = 0;
          colorWordStepStartTime = 0;
          showColorWordChallengeStep(0);
          currentState = COLOR_WORD_CHALLENGE;
        } else if (key == '3') {
          colorWordStepDuration = colorWordDurations[HARD];
          colorWordDifficulty = HARD;
          // Prepare game
          generateColorWordChallengeSequences(cwcSeq1, cwcSeq2, COLOR_WORD_CHALLENGE_LENGTH);
          colorWordCurrentStep = 0;
          colorWordWrongTries = 0;
          colorWordStepStartTime = 0;
          showColorWordChallengeStep(0);
          currentState = COLOR_WORD_CHALLENGE;
        }
        break;
      }

    case COLOR_WORD_CHALLENGE:
      {
        unsigned long now = millis();
        if (colorWordStepStartTime == 0) {
          colorWordStepStartTime = now;
        }
        // Allow menu/logout
        if (key == '*') {
          showMenu();
          currentState = MENU;
          colorWordWrongTries = 0;
          colorWordCurrentStep = 0;
          break;
        }
        if (key == '#') {
          playerDisplayOffset = 0;  // <-- Reset the offset!
          showModeSelect();
          currentState = MODE_SELECT;
          colorWordWrongTries = 0;
          colorWordCurrentStep = 0;
          break;
        }

        // Start timer for each new word
        if (colorWordStepStartTime == 0) {
          colorWordStepStartTime = now;
        }

        // Handle timeout: if 7 seconds pass, count as wrong and advance
        if (now - colorWordStepStartTime >= colorWordStepDuration) {
          colorWordWrongTries++;
          colorWordCurrentStep++;
          colorWordStepStartTime = 0;  // reset for next word

          if (colorWordCurrentStep == COLOR_WORD_CHALLENGE_LENGTH) {
            int stars = colorWordMaxTries - colorWordWrongTries;
            showCenteredStarsAndScore(stars);
            uploadColorWordSession(playerDocIds[currentPlayer], stars, stars * 2);
            String gameName = "color_word_game";
            int currentHighScore = fetchHighScore(playerDocIds[currentPlayer], gameName);
            if (stars * 20 > currentHighScore) {
              updateHighScore(playerDocIds[currentPlayer], gameName, stars);
            }

            delay(2000);
if (!offlineMode) {
display.fillScreen(WHITE);
                delay(500);
int currentHighScore2 = fetchHighScore(playerDocIds[currentPlayer], gameName);

// Top Centered Label
String scoreStr = "HighScore: " + String(currentHighScore2);
display.setTextSize(2);
display.setTextColor(BLACK);
int labelWidth = scoreStr.length() * 12;
int labelX = (240 - labelWidth) / 2;
int labelY = 10;
display.setCursor(labelX, labelY);
display.print(scoreStr);

// Centered Score Value
String scoreValue = String(currentHighScore2);
display.setTextSize(3);
int scoreWidth = scoreValue.length() * 18;
int scoreHeight = 24;
int scoreX = (240 - scoreWidth) / 2;
int scoreY = (320 - scoreHeight) / 2;
display.setCursor(scoreX, scoreY);
display.print(scoreValue);

delay(2000);
          }


            showMenu();
            currentState = MENU;
            colorWordWrongTries = 0;
            colorWordCurrentStep = 0;
            break;
          } else {
            showColorWordChallengeStep(colorWordCurrentStep);
          }
        }

        // Handle button presses
        int buttonPins[3] = { RED_BUTTON_PIN, BLUE_BUTTON_PIN, GREEN_BUTTON_PIN };
        for (int i = 0; i < 3; i++) {
          int reading = digitalRead(buttonPins[i]);
          if (lastButtonState[i] == HIGH && reading == LOW && (now - lastDebounceTime[i]) > debounceDelay) {
            lastDebounceTime[i] = now;

            if (i == cwcSeq1[colorWordCurrentStep]) {
              // Correct: advance and award star (no need to increment wrongTries)
              colorWordCurrentStep++;
            } else {
              // Wrong: advance, count as wrong, no retry
              colorWordWrongTries++;
              colorWordCurrentStep++;
            }
            colorWordStepStartTime = 0;  // reset timer for next word

            if (colorWordCurrentStep == COLOR_WORD_CHALLENGE_LENGTH) {
              int stars = colorWordMaxTries - colorWordWrongTries;
              showCenteredStarsAndScore(stars);
              uploadColorWordSession(playerDocIds[currentPlayer], stars, stars * 2);
              String gameName = "color_word_game";
              int currentHighScore = fetchHighScore(playerDocIds[currentPlayer], gameName);
              if (stars * 20 > currentHighScore) {
                updateHighScore(playerDocIds[currentPlayer], gameName, stars);
              }
              delay(2000);
if (!offlineMode) {
display.fillScreen(WHITE);
                delay(500);
int currentHighScore2 = fetchHighScore(playerDocIds[currentPlayer], gameName);

// Top Centered Label
String scoreStr = "HighScore: " + String(currentHighScore2);
display.setTextSize(2);
display.setTextColor(BLACK);
int labelWidth = scoreStr.length() * 12;
int labelX = (240 - labelWidth) / 2;
int labelY = 10;
display.setCursor(labelX, labelY);
display.print(scoreStr);

// Centered Score Value
String scoreValue = String(currentHighScore2);
display.setTextSize(3);
int scoreWidth = scoreValue.length() * 18;
int scoreHeight = 24;
int scoreX = (240 - scoreWidth) / 2;
int scoreY = (320 - scoreHeight) / 2;
display.setCursor(scoreX, scoreY);
display.print(scoreValue);

delay(2000);
            }



              showMenu();
              currentState = MENU;
              colorWordWrongTries = 0;
              colorWordCurrentStep = 0;
              break;
            } else {
              showColorWordChallengeStep(colorWordCurrentStep);
            }
          }
          lastButtonState[i] = reading;
        }
        break;
      }

    case LED_REACTION:
      {
        static unsigned long ledStepStartTime = 0;

        if (key == '*') {
          turnOffAllRings();
          showMenu();
          currentState = MENU;
          ledReactionActive = false;
          ledReactionWaiting = false;
          break;
        }
        if (key == '#') {
          playerDisplayOffset = 0;  // <-- Reset the offset!
          turnOffAllRings();
          showModeSelect();
          currentState = MODE_SELECT;
          ledReactionActive = false;
          ledReactionWaiting = false;
          break;
        }

        if (!ledReactionActive)
          break;

        unsigned long now = millis();

        // ----- Handle the white screen delay -----
        if (ledReactionWaiting) {
          if (now - ledReactionDelayStart >= ledReactionDelayDuration) {
            // End of white screen, show next color
            display.fillScreen(WHITE);  // Optional: clear before next color
            showNextLedReactionColor();
            ledStepStartTime = now;
            ledReactionWaiting = false;
          }
          break;  // Don't process button/color logic during white screen
        }

        // ----- Start step timer and show new color if needed -----
        if (ledStepStartTime == 0) {
          showNextLedReactionColor();
          ledStepStartTime = now;
        }

        // ----- Check for button press -----
        int buttonPins[3] = { RED_BUTTON_PIN, BLUE_BUTTON_PIN, GREEN_BUTTON_PIN };
        for (int i = 0; i < 3; i++) {
          int reading = digitalRead(buttonPins[i]);
          if (lastButtonState[i] == HIGH && reading == LOW && (now - lastDebounceTime[i]) > debounceDelay) {
            lastDebounceTime[i] = now;
            if (i == ledReactionCurrentColor) {
              ledReactionCorrect++;  // Correct button
            }
            // Move to white screen before next color
            turnOffAllRings();
            display.fillScreen(WHITE);
            ledReactionWaiting = true;
            ledReactionDelayStart = millis();
            ledStepStartTime = 0;  // Reset for next round
            break;
          }
          lastButtonState[i] = reading;
        }

        // ----- Timeout: move to white screen if time exceeded -----
        if (!ledReactionWaiting && (now - ledStepStartTime >= ledReactionStepDuration)) {
          turnOffAllRings();
          display.fillScreen(WHITE);
          ledReactionWaiting = true;
          ledReactionDelayStart = millis();
          ledStepStartTime = 0;  // Reset for next round
        }

        // ----- End game after 20 seconds -----
        if (now - ledReactionStartTime >= ledReactionGameDuration) {
          ledReactionActive = false;
          turnOffAllRings();
          showLedReactionScore(ledReactionCorrect);  // Show score
          uploadLedReactionSession(playerDocIds[currentPlayer], ledReactionCorrect, ledReactionCorrect * 2);
          String gameName = "LED_reaction_game";
          int currentHighScore = fetchHighScore(playerDocIds[currentPlayer], gameName);
          if (ledReactionCorrect * 2 > currentHighScore) {
            updateHighScore(playerDocIds[currentPlayer], gameName, ledReactionCorrect * 2);
          }
          showMenu();
          currentState = MENU;
          ledReactionWaiting = false;
        }
        break;
      }

    case CODE_BREAKER_DIFFICULTY_SELECT:
      {
        if (key) {
          if (key == '*') {
            display.fillRect(0, 220, SCREEN_WIDTH, 30, WHITE);
            showMenu();
            currentState = MENU;
            codeBreakerWrongTries = 0;
            visualMemoryWrongTries = 0;
            break;
          }
          if (key == '#') {
            playerDisplayOffset = 0;  // <-- Reset the offset!
            showModeSelect();
            currentState = MODE_SELECT;
            codeBreakerWrongTries = 0;
            visualMemoryWrongTries = 0;
            break;
          }
          switch (key) {
            case '1':  // Easy
              codeBreakerWrongTries = 0;
              // Set max tries for easy
              maxWrongTries = 12;
              currentState = CODE_BREAKER;
              showCodeBreakerTitle();  // Show the title screen
              generateNewRandomNumber();
              break;
            case '2':  // Medium
              codeBreakerWrongTries = 0;
              // Set max tries for medium
              maxWrongTries = 8;
              currentState = CODE_BREAKER;
              showCodeBreakerTitle();  // Show the title screen
              generateNewRandomNumber();
              break;
            case '3':  // Hard
              codeBreakerWrongTries = 0;
              // Set max tries for hard
              maxWrongTries = 5;
              currentState = CODE_BREAKER;
              showCodeBreakerTitle();  // Show the title screen
              generateNewRandomNumber();
              break;
            default:
              // Ignore other keys
              break;
          }
        }
        break;
      }

    case CODE_BREAKER:
      {
        if (key) {
          if (key == '*') {
            showMenu();
            currentState = MENU;
            codeBreakerWrongTries = 0;
            visualMemoryWrongTries = 0;
            inputIndex = 0;
            int y = 190;
            display.fillRect(0, y, SCREEN_WIDTH, 30, WHITE);
            break;
          }
          if (key == '#') {
            playerDisplayOffset = 0;  // <-- Reset the offset!
            showModeSelect();
            currentState = MODE_SELECT;
            codeBreakerWrongTries = 0;
            visualMemoryWrongTries = 0;
            break;
          }
          if (inputIndex < 3 && key >= '0' && key <= '9') {
            inputBuffer[inputIndex++] = key;
            showInputProgress(inputBuffer, inputIndex);
          }
          if (inputIndex == 3) {
            inputBuffer[3] = '\0';
            if (strcmp(inputBuffer, randomNumberStr) == 0) {
              int stars = maxWrongTries - codeBreakerWrongTries;
              showCenteredStarsAndScore(stars);  // Show only stars and score, centered
              uploadCodeBreakerSession(playerDocIds[currentPlayer], stars, stars * 2);
              String gameName = "code_breaker_game";
              int currentHighScore = fetchHighScore(playerDocIds[currentPlayer], gameName);
              if (stars * 20 >   currentHighScore) {
                updateHighScore(playerDocIds[currentPlayer], gameName, stars);
              }
              delay(2000);  // Show for 2 seconds

if (!offlineMode) {
display.fillScreen(WHITE);
                delay(500);
int currentHighScore2 = fetchHighScore(playerDocIds[currentPlayer], gameName);

// Top Centered Label
String scoreStr = "HighScore: " + String(currentHighScore2);
display.setTextSize(2);
display.setTextColor(BLACK);
int labelWidth = scoreStr.length() * 12;
int labelX = (240 - labelWidth) / 2;
int labelY = 10;
display.setCursor(labelX, labelY);
display.print(scoreStr);

// Centered Score Value
String scoreValue = String(currentHighScore2);
display.setTextSize(3);
int scoreWidth = scoreValue.length() * 18;
int scoreHeight = 24;
int scoreX = (240 - scoreWidth) / 2;
int scoreY = (320 - scoreHeight) / 2;
display.setCursor(scoreX, scoreY);
display.print(scoreValue);

delay(2000);
}



              showMenu();   // Return to games menu
              currentState = MENU;
              codeBreakerWrongTries = 0;  // Reset tries
              inputIndex = 0;
              break;
            }

            else {
              int exact = 0, partial = 0;
              countMatches(inputBuffer, randomNumberStr, exact, partial);

              showCodeBreakerResult(exact, partial);
              showLastTry(inputBuffer);
              showTriesRemaining(maxWrongTries - codeBreakerWrongTries - 1);

              codeBreakerWrongTries++;  // Increment on wrong try

              if (codeBreakerWrongTries >= maxWrongTries) {
                display.fillScreen(WHITE);
                display.setTextColor(RED);
                display.setTextSize(2);
                display.setCursor(20, 120);
                display.print("Out of tries!");
                uploadCodeBreakerSession(playerDocIds[currentPlayer], 0, 0);
                delay(2000);

                // Clear the input progress line (where "_ _ _" is shown)
                int y = 190;
                display.fillRect(0, y, SCREEN_WIDTH, 30, WHITE);

                showMenu();
                currentState = MENU;
                codeBreakerWrongTries = 0;  // Reset for next game
                inputIndex = 0;
                break;
              }

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
}
