#include <Arduino_GFX_Library.h>
#include <Keypad.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include "esp_wpa2.h" // Only needed for WPA2-Enterprise

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
const char *colorNames[] = {"Red", "Blue", "Green"};
uint16_t colorValues[] = {RED, BLUE, GREEN}; // TFT color values

// --- Color-Word Challenge definitions ---
#define COLOR_WORD_CHALLENGE_LENGTH 5
uint8_t cwcSeq1[COLOR_WORD_CHALLENGE_LENGTH];
uint8_t cwcSeq2[COLOR_WORD_CHALLENGE_LENGTH];

// --- Color-Word Challenge game variables ---
int colorWordWrongTries = 0;
uint8_t colorWordCurrentStep = 0;
const int colorWordMaxTries = 5; // same as sequence length

// --- Led Reaction game variables ---
unsigned long ledReactionStartTime = 0;
const unsigned long ledReactionGameDuration = 20000; // 20 seconds in ms
int ledReactionCorrect = 0;
int ledReactionCurrentColor = 0;
bool ledReactionActive = false;
bool ledReactionWaiting = false; // Are we in the 1s delay between rounds?
unsigned long ledReactionDelayStart = 0;
const unsigned long ledReactionDelayDuration = 1000; // 1 second between rounds

// Multiplayer game variables
byte multiplayerPlayer1 = 0; // 1-based index
byte multiplayerPlayer2 = 0; // 1-based index

// --- Code Breaker multiplayer game variables ---
// --- Secret numbers for each player (3 digits + null terminator) ---
char player1Secret[4] = ""; // Player 1's secret number (as a string)
char player2Secret[4] = ""; // Player 2's secret number (as a string)

// --- Current guess buffers for each player ---
char player1Guess[4] = ""; // Player 1's current guess (as a string)
char player2Guess[4] = ""; // Player 2's current guess (as a string)

bool cbSecretNeedsRedraw = true;

// --- Try counters (optional, for stats) ---
uint8_t player1Tries = 0; // Number of guesses Player 1 has made
uint8_t player2Tries = 0; // Number of guesses Player 2 has made

// --- Whose turn is it? ---
bool codeBreakerMultiplayerTurn = false; // false = Player 1's turn, true = Player 2's turn

// --- Input buffer index for entering numbers ---
uint8_t cbMultiInputIndex = 0; // For multiplayer code breaker digit entry

// --- (Optional) Game over flags ---
bool player1Guessed = false;
bool player2Guessed = false;

// Keypad definitions
const byte ROWS = 4;
const byte COLS = 3;

char keys[ROWS][COLS] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'}};

byte rowPins[ROWS] = {13, 12, 27, 26};
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

enum State
{
  MODE_SELECT,
  PLAYER1_SELECT,
  PLAYER2_SELECT,
  PLAYER_SELECT,
  MULTI_MENU,
  CODE_BREAKER_MULTI_SECRET1,
  CODE_BREAKER_MULTI_SECRET2,
  CODE_BREAKER_MULTI_TURN,
  MENU,
  CODE_BREAKER,
  VISUAL_MEMORY,
  VISUAL_MEMORY_INPUT,
  VISUAL_MEMORY_RESULT,
  COLOR_WORD_CHALLENGE,
  COLOR_WORD_CHALLENGE_INPUT,
  LED_REACTION
};
State currentState = PLAYER_SELECT;

// WiFi credentials (for WPA2-Enterprise)
#define WIFI_SSID "eduroam"
#define WIFI_USERNAME "omarm@campus.technion.ac.il"
#define WIFI_PASSWORD "JonSnow04112001#"

// Firestore info
const char *projectId = "iot-rehab-game";
const char *apiKey = "AIzaSyBXmChDOo054ZKjiiCma295qYqMzf7kjZs";
const char *collection = "Players";

// Players info
#define MAX_PLAYERS 3

String playerNames[MAX_PLAYERS] = {"Omar Mahamid", "Mahmod Mahamid", "Mohamad Zbedat"};
// int playerCount = 0;
int playerCount = MAX_PLAYERS;

// Game variables for number of tries
int codeBreakerWrongTries = 0;
int visualMemoryWrongTries = 0;
const int maxWrongTries = 5;

// --- Color-Word Challenge helpers ---
void generateColorWordChallengeSequences(uint8_t seq1[], uint8_t seq2[], uint8_t length)
{
  for (uint8_t i = 0; i < length; i++)
  {
    seq1[i] = random(0, 3);
    uint8_t color2;
    do
    {
      color2 = random(0, 3);
    } while (color2 == seq1[i]);
    seq2[i] = color2;
  }
}

void showColorWordChallengeStep(uint8_t index)
{
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
void showBottomHints()
{
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

void showGuessScreen(uint8_t player, uint8_t tries)
{
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

void cbMultiCountMatches(const char *guess, const char *secret, int &exact, int &partial)
{
  exact = 0;
  partial = 0;
  bool guessUsed[3] = {false, false, false};
  bool secretUsed[3] = {false, false, false};

  // Exact matches
  for (int i = 0; i < 3; i++)
  {
    if (guess[i] == secret[i])
    {
      exact++;
      guessUsed[i] = true;
      secretUsed[i] = true;
    }
  }
  // Partial matches
  for (int i = 0; i < 3; i++)
  {
    if (guessUsed[i])
      continue;
    for (int j = 0; j < 3; j++)
    {
      if (secretUsed[j])
        continue;
      if (guess[i] == secret[j])
      {
        partial++;
        secretUsed[j] = true;
        break;
      }
    }
  }
}

void showSecretEntry(uint8_t player, const char *buffer, uint8_t bufLen)
{
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
  for (uint8_t i = 0; i < 3; i++)
  {
    if (i < bufLen)
    {
      display.print("*");
    }
    else
    {
      display.print("-");
    }
    if (i < 2)
      display.print(" ");
  }
}

// Show the mode selection screen
void showModeSelect()
{
  display.fillScreen(WHITE);

  // --- Title ---
  display.setTextColor(BLACK);
  display.setTextSize(2); // Keep the title moderate
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
  uint16_t singleColor = 0x3A99; // Soft blue
  display.fillRoundRect(btnX, btnY1, btnWidth, btnHeight, radius, singleColor);
  display.drawRoundRect(btnX, btnY1, btnWidth, btnHeight, radius, 0x001F); // Blue border

  // Single player icon (head and shoulders)
  int iconX = btnX + 28;
  int iconY = btnY1 + btnHeight / 2 - 4;
  display.fillCircle(iconX, iconY, 10, WHITE);                     // Head
  display.fillRoundRect(iconX - 10, iconY + 10, 20, 10, 5, WHITE); // Shoulders/body

  // Label (smaller font)
  display.setTextSize(1); // Make label smaller
  display.setTextColor(WHITE);
  display.setCursor(btnX + 55, btnY1 + btnHeight / 2 - 6);
  display.print("Single Player");

  // --- Multiplayer Button (Green) ---
  uint16_t multiColor = 0x07E0; // Green
  display.fillRoundRect(btnX, btnY2, btnWidth, btnHeight, radius, multiColor);
  display.drawRoundRect(btnX, btnY2, btnWidth, btnHeight, radius, 0x03E0); // Dark green border

  // Multiplayer icon (two heads)
  int iconX1 = btnX + 23, iconY1 = btnY2 + btnHeight / 2 - 3;
  int iconX2 = btnX + 38, iconY2 = btnY2 + btnHeight / 2 + 2;
  display.fillCircle(iconX1, iconY1, 8, WHITE);                   // Head 1
  display.fillCircle(iconX2, iconY2, 8, WHITE);                   // Head 2
  display.fillRoundRect(iconX1 - 8, iconY1 + 8, 16, 8, 4, WHITE); // Body 1
  display.fillRoundRect(iconX2 - 8, iconY2 + 8, 16, 8, 4, WHITE); // Body 2

  // Label (smaller font)
  display.setTextSize(1); // Make label smaller
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
}

// Show the multiplayer player selection screen, first for player 1 then for player 2
void showMultiplayerPlayerSelect1()
{
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);
  display.setCursor(20, 60);
  display.print("Choose Player 1:");
  int y = 100;
  for (int i = 0; i < playerCount; i++)
  {
    display.setCursor(20, y);
    display.print(String(i + 1) + ") " + playerNames[i]);
    y += 30;
  }
}

void showMultiplayerPlayerSelect2(byte excludeIndex)
{
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);
  display.setCursor(20, 60);
  display.print("Choose Player 2:");
  int y = 100;
  int optionNum = 1;
  for (int i = 0; i < playerCount; i++)
  {
    if (i == excludeIndex)
      continue; // skip the chosen player
    display.setCursor(20, y);
    display.print(String(optionNum) + ") " + playerNames[i]);
    y += 30;
    optionNum++;
  }
}

void showPlayerSelected(byte player)
{
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);
  display.setCursor(20, 100);
  display.print("Welcome back :)");
  display.setCursor(20, 140);
  // player is 1-based, array is 0-based
  if (player >= 1 && player <= playerCount)
  {
    display.print(playerNames[player - 1]);
  }
  else
  {
    display.print("Unknown Player");
  }
  delay(1200);
}

// Show single player games menu
void showMenu()
{
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
  y += 30;
  display.setCursor(20, y);
  display.print("3) Color Word");
  y += 30;
  display.setCursor(20, y);
  display.print("4) Led Reaction");
  showBottomHints();
}

// Show multiplayer games menu
void showMultiplayerMenu()
{
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);
  display.setCursor(20, 60);
  display.print("Multiplayer Games:");
  int y = 110;
  display.setCursor(20, y);
  display.print("1) Code Breaker");
}

void showMenuMessage(const char *msg)
{
  int msgHeight = 40;
  display.fillRect(0, SCREEN_HEIGHT - msgHeight, SCREEN_WIDTH, msgHeight, WHITE);
  display.setTextColor(BLUE);
  display.setTextSize(2);
  display.setCursor(20, SCREEN_HEIGHT - msgHeight + 10);
  display.print(msg);
}

void showCodeBreakerTitle()
{
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);
  display.setCursor(30, 100);
  display.print("Code breaker game");
  delay(2000);
  display.fillScreen(WHITE);
  showBottomHints();
}

void showColorWordTitle()
{
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);

  const char *title = "Color Word";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - w) / 2;
  int y = 100; // You can adjust this value for vertical position

  display.setCursor(x, y);
  display.print(title);
  delay(2000);

  display.fillScreen(WHITE);
  showBottomHints();
}

void generateNewRandomNumber()
{
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

void showCodeBreakerResult(int exact, int partial)
{
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

void showLastTry(const char *guess)
{
  int y = 150;
  display.fillRect(0, y, SCREEN_WIDTH, 30, WHITE);
  display.setTextColor(DARKGREY);
  display.setTextSize(2);
  display.setCursor(20, y);
  display.print("Last try: ");
  display.print(guess);
}

void showInputProgress(const char *inputBuffer, byte inputIndex)
{
  int y = 190;
  int numChars = 3;
  int charWidth = 12;
  int totalWidth = numChars * charWidth + (numChars - 1) * charWidth / 2;
  int x = (SCREEN_WIDTH - totalWidth) / 2;

  display.setTextSize(2);
  display.setTextColor(BLACK);
  display.fillRect(0, y, SCREEN_WIDTH, 30, WHITE);

  display.setCursor(x, y);
  for (int i = 0; i < 3; i++)
  {
    if (i < inputIndex)
    {
      display.print(inputBuffer[i]);
    }
    else
    {
      display.print("_");
    }
    if (i < 2)
      display.print(" ");
  }
}

void countMatches(const char *guess, const char *answer, int &exact, int &partial)
{
  exact = 0;
  partial = 0;
  bool answer_used[3] = {false, false, false};
  bool guess_used[3] = {false, false, false};
  for (int i = 0; i < 3; i++)
  {
    if (guess[i] == answer[i])
    {
      exact++;
      answer_used[i] = true;
      guess_used[i] = true;
    }
  }
  for (int i = 0; i < 3; i++)
  {
    if (guess_used[i])
      continue;
    for (int j = 0; j < 3; j++)
    {
      if (!answer_used[j] && guess[i] == answer[j])
      {
        partial++;
        answer_used[j] = true;
        break;
      }
    }
  }
}

void generateRandomColorSequence(uint8_t sequence[], uint8_t length)
{
  for (uint8_t i = 0; i < length; i++)
  {
    sequence[i] = random(0, 3);
  }
  Serial.print("Color sequence: ");
  for (uint8_t i = 0; i < length; i++)
  {
    Serial.print(colorNames[sequence[i]]);
    if (i < length - 1)
      Serial.print(", ");
  }
  Serial.println();
}

void showColorOnDisplay(uint8_t colorIndex)
{
  display.fillScreen(colorValues[colorIndex]);
  display.setTextSize(3);
  display.setTextColor(WHITE);
  display.setCursor(60, 140);
  display.print(colorNames[colorIndex]);
}

// Fetch and parse player names from Firestore
void fetchPlayersFromFirestore()
{
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

  if (!error && doc.containsKey("documents"))
  {
    JsonArray docs = doc["documents"].as<JsonArray>();
    for (JsonObject d : docs)
    {
      if (d.containsKey("fields") && d["fields"].containsKey("name"))
      {
        String name = d["fields"]["name"]["stringValue"].as<String>();
        if (playerCount < MAX_PLAYERS)
        {
          playerNames[playerCount++] = name;
        }
      }
    }
  }
  else
  {
    Serial.print("deserializeJson() failed or no documents: ");
    Serial.println(error.c_str());
  }

  Serial.println("Player names:");
  for (int i = 0; i < playerCount; i++)
  {
    Serial.println(playerNames[i]);
  }
}

// Show player selection menu with fetched names
void showPlayerMenu()
{
  inputIndex = 0;
  inputBuffer[0] = inputBuffer[1] = inputBuffer[2] = inputBuffer[3] = '\0';

  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);
  display.setCursor(20, 60);
  display.print("Select player:");
  int y = 100;
  for (int i = 0; i < playerCount; i++)
  {
    display.setCursor(20, y);
    display.print(String(i + 1) + ") " + playerNames[i]);
    y += 30;
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

// Show loading screen with a circle and "loading" text
// This function can be called while fetching data or performing long operations
void showLoadingScreen()
{
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
void showColorOnRings(uint8_t colorIndex)
{
  // Turn off all LEDs first
  for (uint8_t i = 0; i < WS2812_NUM_LEDS; i++)
  {
    leds.setPixelColor(i, 0);
  }

  uint32_t color = 0;
  if (colorIndex == 0)
  { // Red
    color = leds.Color(255, 0, 0);
  }
  else if (colorIndex == 1)
  { // Blue
    color = leds.Color(0, 0, 255);
  }
  else if (colorIndex == 2)
  { // Green
    color = leds.Color(0, 255, 0);
  }

  // Light up the corresponding ring
  uint8_t ringStart = colorIndex * LEDS_PER_RING;
  for (uint8_t i = 0; i < LEDS_PER_RING; i++)
  {
    leds.setPixelColor(ringStart + i, color);
  }
  leds.show();
}

// Turn off all rings of NeoPixel LEDs
void turnOffAllRings()
{
  for (uint8_t i = 0; i < WS2812_NUM_LEDS; i++)
  {
    leds.setPixelColor(i, 0);
  }
  leds.show();
}

// Show the number of tries remaining for the current game
void showTriesRemaining(int triesRemaining)
{
  int y = 220;                                     // Just below the "Last try" line (which is at y=150)
  display.fillRect(0, y, SCREEN_WIDTH, 30, WHITE); // Clear previous message
  display.setTextColor(DARKGREY);
  display.setTextSize(2);
  display.setCursor(20, y);
  display.print(triesRemaining);
  display.print(" tries remaining");
}

// Show stars and score centered on the display
void showCenteredStarsAndScore(int stars)
{
  display.fillScreen(WHITE); // Clear everything

  // Prepare stars string
  String starsStr = "";
  for (int i = 0; i < stars; i++)
  {
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
void showLedReactionTitle()
{
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);

  const char *title = "Led Reaction";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - w) / 2;
  int y = 100; // Adjust vertically if you want

  display.setCursor(x, y);
  display.print(title);
  delay(2000);

  display.fillScreen(WHITE);
  showBottomHints();
}

// Show the LED reaction color based on the index
void showLedReactionColor(int colorIdx)
{
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
int ledReactionStars(int coins)
{
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

void showEnterSecretPrompt(uint8_t player)
{
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

void showMaskedInputProgress(const char *inputBuffer, byte inputIndex)
{
  int y = 190;
  int numChars = 3;
  int charWidth = 12;
  int totalWidth = numChars * charWidth + (numChars - 1) * charWidth / 2;
  int x = (SCREEN_WIDTH - totalWidth) / 2;

  display.setTextSize(2);
  display.setTextColor(BLACK);
  display.fillRect(0, y, SCREEN_WIDTH, 30, WHITE);

  display.setCursor(x, y);
  for (int i = 0; i < 3; i++)
  {
    if (i < inputIndex)
    {
      display.print("*"); // Masked
    }
    else
    {
      display.print("-");
    }
    if (i < 2)
      display.print(" ");
  }
}

void setup()
{
  // Initialize NeoPixel LEDs
  leds.begin();
  leds.show(); // Initialize all LEDs to 'off'

  Serial.begin(9600);
  display.begin();
  display.setRotation(0);
  randomSeed(analogRead(0));

  // Show loading screen while connecting to Wi-Fi
  showLoadingScreen();
  showModeSelect();

  // Connect to Wi-Fi (WPA2-Enterprise)
  /*
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
  */

  // fetchPlayersFromFirestore();
  // showLoadingScreen();

  // currentState = PLAYER_SELECT;
  currentState = MODE_SELECT;

  pinMode(RED_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BLUE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(GREEN_BUTTON_PIN, INPUT_PULLUP);
}

void loop()
{
  char key = keypad.getKey();
  switch (currentState)
  {
  case MODE_SELECT:
  {
    if (key == '1')
    {
      showPlayerMenu();
      currentState = PLAYER_SELECT;
      delay(150);
      while (keypad.getKey() != NO_KEY)
      {
      }
    }
    else if (key == '2')
    {
      showMultiplayerPlayerSelect1();
      currentState = PLAYER1_SELECT;
      delay(150);
      while (keypad.getKey() != NO_KEY)
      {
      }
    }
    break;
  }

  case PLAYER1_SELECT:
  {
    if (key == '#')
    {
      showModeSelect();
      currentState = MODE_SELECT;
      // Optionally reset other variables here if needed
      return; // or break; if not in a function
    }

    if (key && key >= '1' && key <= '0' + playerCount)
    {
      multiplayerPlayer1 = key - '0';                       // 1-based index
      showMultiplayerPlayerSelect2(multiplayerPlayer1 - 1); // Pass 0-based index to exclude
      currentState = PLAYER2_SELECT;
      delay(150);
      while (keypad.getKey() != NO_KEY)
      {
      }
    }
    break;
  }

  case PLAYER2_SELECT:
  {
    if (key == '#')
    {
      showModeSelect();
      currentState = MODE_SELECT;
      // Optionally reset other variables here if needed
      return; // or break; if not in a function
    }

    if (key && key >= '1' && key <= '0' + (playerCount - 1))
    {
      // Map the key to the correct player index, skipping the excluded one
      int option = key - '1';               // 0-based option
      int exclude = multiplayerPlayer1 - 1; // 0-based index to skip
      int chosenIdx = -1;
      int count = 0;
      for (int i = 0; i < playerCount; i++)
      {
        if (i == exclude)
          continue;
        if (count == option)
        {
          chosenIdx = i;
          break;
        }
        count++;
      }
      if (chosenIdx != -1)
      {
        multiplayerPlayer2 = chosenIdx + 1; // Store as 1-based
        showMultiplayerMenu();
        currentState = MULTI_MENU;
        delay(150);
        while (keypad.getKey() != NO_KEY)
        {
        }
      }
    }
    break;
  }

  case MULTI_MENU:
  {
    if (key == '#')
    {
      showModeSelect();
      currentState = MODE_SELECT;
      // Optionally reset other variables here if needed
      return; // or break; if not in a function
    }

    if (key == '1')
    {
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
    if (key && key != NO_KEY)
    {
      display.fillScreen(WHITE);
      display.setTextColor(RED);
      display.setTextSize(2);
      display.setCursor(20, 120);
      display.print("Not implemented");
      delay(1200);
      showMultiplayerMenu(); // Redraw the menu
    }
    break;
  }

  case CODE_BREAKER_MULTI_SECRET1:
  {
    // Show prompt only once when entering the state
    static bool promptShown1 = false;
    if (!promptShown1)
    {
      showEnterSecretPrompt(1);
      showMaskedInputProgress(player1Secret, cbMultiInputIndex);
      promptShown1 = true;
    }

    if (key && key >= '0' && key <= '9' && cbMultiInputIndex < 3)
    {
      player1Secret[cbMultiInputIndex++] = key;
      player1Secret[cbMultiInputIndex] = '\0';
      showMaskedInputProgress(player1Secret, cbMultiInputIndex); // Update input line only
    }

    if (cbMultiInputIndex == 3)
    {
      delay(500);
      display.fillRect(0, 190, SCREEN_WIDTH, 30, WHITE); // Clear input line
      delay(300);
      cbMultiInputIndex = 0;
      promptShown1 = false;     // Reset for next time
      currentState = CODE_BREAKER_MULTI_SECRET2;
    }
    break;
  }

  case CODE_BREAKER_MULTI_SECRET2:
  {
    static bool promptShown2 = false;
    if (!promptShown2)
    {
      showEnterSecretPrompt(2);
      showMaskedInputProgress(player2Secret, cbMultiInputIndex);
      promptShown2 = true;
    }

    if (key && key >= '0' && key <= '9' && cbMultiInputIndex < 3)
    {
      player2Secret[cbMultiInputIndex++] = key;
      player2Secret[cbMultiInputIndex] = '\0';
      showMaskedInputProgress(player2Secret, cbMultiInputIndex);
    }

    if (cbMultiInputIndex == 3)
    {
      delay(500);
      display.fillRect(0, 190, SCREEN_WIDTH, 30, WHITE);
      delay(300);
      codeBreakerMultiplayerTurn = false;
      cbMultiInputIndex = 0;
      promptShown2 = false; // Reset for next time
      // Now move to guessing phase...
      showGuessScreen(1, player1Tries); // Or your equivalent
      currentState = CODE_BREAKER_MULTI_TURN;
    }
    break;
  }

  case CODE_BREAKER_MULTI_TURN:
  {
    if (!codeBreakerMultiplayerTurn)
    { // Player 1's turn
      if (key && key >= '0' && key <= '9' && cbMultiInputIndex < 3)
      {
        player1Guess[cbMultiInputIndex++] = key;
      }
      if (cbMultiInputIndex == 3)
      {
        player1Guess[3] = '\0';
        int exact = 0, partial = 0;
        cbMultiCountMatches(player1Guess, player2Secret, exact, partial);
        player1Tries++;

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

        if (exact == 3)
        {
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
          break;
        }
        // Next player's turn
        codeBreakerMultiplayerTurn = true;
        showGuessScreen(2, player2Tries);
        cbMultiInputIndex = 0;
      }
    }
    else
    {
      if (key && key >= '0' && key <= '9' && cbMultiInputIndex < 3)
      {
        player2Guess[cbMultiInputIndex++] = key;
      }
      if (cbMultiInputIndex == 3)
      {
        player2Guess[3] = '\0';
        int exact = 0, partial = 0;
        cbMultiCountMatches(player2Guess, player1Secret, exact, partial);
        player2Tries++;

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

        if (exact == 3)
        {
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
          break;
        }
        // Next player's turn
        codeBreakerMultiplayerTurn = false;
        showGuessScreen(1, player1Tries);
        inputIndex = 0;
      }
    }
    // ... Player 2's turn handled below ...
    break;
  }

  case PLAYER_SELECT:
  {
    if (key == '#')
    {
      showModeSelect();
      currentState = MODE_SELECT;
      // Optionally reset other variables here if needed
      return; // or break; if not in a function
    }

    if (key && key >= '1' && key <= '4' && key - '0' <= playerCount)
    {
      currentPlayer = key - '0';
      showPlayerSelected(currentPlayer);
      showMenu();
      currentState = MENU;
    }
    break;
  }

  case MENU:
  {
    if (key)
    {
      if (key == '#')
      {
        showModeSelect();
        currentState = MODE_SELECT;
        codeBreakerWrongTries = 0;
        visualMemoryWrongTries = 0;
        break;
      }
      Serial.print("Key pressed: ");
      Serial.println(key);

      //  Code Breaker
      if (key == '1')
      {
        showCodeBreakerTitle();
        generateNewRandomNumber();
        inputIndex = 0;
        codeBreakerWrongTries = 0; // Reset here the number of tries
        showInputProgress(inputBuffer, inputIndex);
        currentState = CODE_BREAKER;
      }
      else if (key == '2')
      { // Visual Memory
        display.fillScreen(WHITE);
        display.setTextColor(BLACK);
        display.setTextSize(2);
        display.setCursor(20, 100);
        display.print("Visual memory game");
        showBottomHints();
        delay(1000);

        visualMemoryWrongTries = 0; // Reset here the number of tries

        generateRandomColorSequence(colorSequence, colorSequenceLength);
        for (uint8_t i = 0; i < colorSequenceLength; i++)
        {
          showColorOnDisplay(colorSequence[i]);
          showColorOnRings(colorSequence[i]); // Show color on rings
          delay(2000);
          turnOffAllRings(); // Turn off all rings after showing
          if (i < colorSequenceLength - 1)
          {
            display.fillScreen(WHITE);
            delay(1000);
          }
          turnOffAllRings(); // Turn off all rings after showing
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
      // --- Color-Word Challenge menu handler ---
      else if (key == '3')
      {
        showColorWordTitle(); // <-- Centered title with delay
        generateColorWordChallengeSequences(cwcSeq1, cwcSeq2, COLOR_WORD_CHALLENGE_LENGTH);
        currentStep = 0;
        colorWordCurrentStep = 0;
        colorWordWrongTries = 0;
        // Show the first challenge step
        showColorWordChallengeStep(currentStep);
        currentState = COLOR_WORD_CHALLENGE;
      }
      else if (key == '4')
      {
        showLedReactionTitle();
        ledReactionCorrect = 0;
        ledReactionActive = true;
        ledReactionStartTime = millis();
        ledReactionCurrentColor = random(0, 3);
        ledReactionWaiting = false;
        showLedReactionColor(ledReactionCurrentColor);
        currentState = LED_REACTION;
      }

      else
      {
        showMenuMessage("Please choose 1, 2, 3 or 4");
      }
    }
    break;
  }

  case VISUAL_MEMORY_INPUT:
  {
    if (key == '*')
    {
      display.fillRect(0, 220, SCREEN_WIDTH, 30, WHITE);
      showMenu();
      currentState = MENU;
      codeBreakerWrongTries = 0;
      visualMemoryWrongTries = 0;
      break;
    }
    if (key == '#')
    {
      showModeSelect();
      currentState = MODE_SELECT;
      codeBreakerWrongTries = 0;
      visualMemoryWrongTries = 0;
      break;
    }

    // --- Handle button presses for visual memory game ---
    int buttonPins[3] = {RED_BUTTON_PIN, BLUE_BUTTON_PIN, GREEN_BUTTON_PIN};
    const char *buttonNames[3] = {"Red button pressed", "Blue button pressed", "Green button pressed"};
    for (int i = 0; i < 3; i++)
    {
      int reading = digitalRead(buttonPins[i]);
      if (lastButtonState[i] == HIGH && reading == LOW && (millis() - lastDebounceTime[i]) > debounceDelay)
      {
        lastDebounceTime[i] = millis();
        Serial.println(buttonNames[i]);
        if (i == colorSequence[currentStep])
        {
          currentStep++;
          if (currentStep == colorSequenceLength)
          {
            int stars = maxWrongTries - visualMemoryWrongTries;
            showCenteredStarsAndScore(stars); // Show only stars and score, centered
            delay(2000);                      // Show for 2 seconds
            showMenu();                       // Return to games menu
            currentState = MENU;
            visualMemoryWrongTries = 0; // Reset tries
            break;
          }
        }
        else
        {
          visualMemoryWrongTries++; // Increment on wrong try
          // Check if the number of wrong tries exceeds the maximum allowed
          if (visualMemoryWrongTries >= maxWrongTries)
          {
            display.fillScreen(WHITE);
            display.setTextColor(RED);
            display.setTextSize(2);
            display.setCursor(20, 120);
            display.print("Out of tries!");
            delay(2000);
            display.fillRect(0, 220, SCREEN_WIDTH, 30, WHITE);
            showMenu();
            currentState = MENU;
            visualMemoryWrongTries = 0; // Reset for next game
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
          showTriesRemaining(maxWrongTries - visualMemoryWrongTries);
          currentStep = 0;
        }
      }
      lastButtonState[i] = reading;
    }
    break;
  }

    // --- Color-Word Challenge state handler (just shows the first step for now) ---
  case COLOR_WORD_CHALLENGE:
  {
    // Allow menu/logout
    if (key == '*')
    {
      showMenu();
      currentState = MENU;
      colorWordWrongTries = 0;
      break;
    }
    if (key == '#')
    {
      showModeSelect();
      currentState = MODE_SELECT;
      colorWordWrongTries = 0;
      break;
    }

    // Button input handling
    int buttonPins[3] = {RED_BUTTON_PIN, BLUE_BUTTON_PIN, GREEN_BUTTON_PIN};
    for (int i = 0; i < 3; i++)
    {
      int reading = digitalRead(buttonPins[i]);
      if (lastButtonState[i] == HIGH && reading == LOW && (millis() - lastDebounceTime[i]) > debounceDelay)
      {
        lastDebounceTime[i] = millis();
        // The correct answer is the color in cwcSeq1[colorWordCurrentStep]
        if (i == cwcSeq1[colorWordCurrentStep])
        {
          // Correct!
          colorWordCurrentStep++;
          if (colorWordCurrentStep == COLOR_WORD_CHALLENGE_LENGTH)
          {
            // Game complete, show stars/score
            int stars = colorWordMaxTries - colorWordWrongTries;
            showCenteredStarsAndScore(stars);
            delay(2000);
            showMenu();
            currentState = MENU;
            colorWordWrongTries = 0;
            colorWordCurrentStep = 0;
            break;
          }
          else
          {
            showColorWordChallengeStep(colorWordCurrentStep);
          }
        }
        else
        {
          // Wrong!
          colorWordWrongTries++;
          // Optional: flash screen/message for wrong answer
          display.fillScreen(WHITE);
          display.setTextSize(2);
          display.setTextColor(RED);
          display.setCursor(40, 120);
          display.print("Wrong!");
          delay(1000);
          showColorWordChallengeStep(colorWordCurrentStep);
        }
      }
      lastButtonState[i] = reading;
    }
    break;
  }

  case LED_REACTION:
  {
    unsigned long now = millis();

    // End game if time is up
    if (ledReactionActive && (now - ledReactionStartTime >= ledReactionGameDuration))
    {
      ledReactionActive = false;
      turnOffAllRings();
      display.fillScreen(WHITE);
      display.setTextSize(2);
      display.setTextColor(BLACK);

      // Show coins
      const char *msg = "Coins:";
      int16_t x1, y1;
      uint16_t w, h;
      display.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
      int x = (SCREEN_WIDTH - w) / 2;
      int y = 80;
      display.setCursor(x, y);
      display.print(msg);
      String coinsStr = String(ledReactionCorrect);
      display.getTextBounds(coinsStr.c_str(), 0, 0, &x1, &y1, &w, &h);
      x = (SCREEN_WIDTH - w) / 2;
      y += 30;
      display.setCursor(x, y);
      display.print(coinsStr);

      // Show stars
      int stars = ledReactionStars(ledReactionCorrect);
      String starsStr = "Stars: ";
      for (int i = 0; i < stars; i++)
        starsStr += "* ";
      display.setTextSize(2);
      display.setTextColor(GREEN);
      display.getTextBounds(starsStr.c_str(), 0, 0, &x1, &y1, &w, &h);
      x = (SCREEN_WIDTH - w) / 2;
      y += 50;
      display.setCursor(x, y);
      display.print(starsStr);

      delay(2500);
      showMenu();
      currentState = MENU;
      break;
    }

    // Allow exit to menu or logout
    if (key == '*')
    {
      turnOffAllRings();
      showMenu();
      currentState = MENU;
      ledReactionActive = false;
      break;
    }
    if (key == '#')
    {
      turnOffAllRings();
      showModeSelect();
      currentState = MODE_SELECT;
      ledReactionActive = false;
      break;
    }

    // If waiting between rounds, check if 1s has passed
    if (ledReactionWaiting)
    {
      if (now - ledReactionDelayStart >= ledReactionDelayDuration)
      {
        // Generate and show new color
        int newColor;
        do
        {
          newColor = random(0, 3);
        } while (newColor == ledReactionCurrentColor);
        ledReactionCurrentColor = newColor;
        showLedReactionColor(ledReactionCurrentColor);
        ledReactionWaiting = false;
      }
      break; // Don't process button input during the delay
    }

    // Button input handling (only if game is active and not waiting)
    if (ledReactionActive)
    {
      int buttonPins[3] = {RED_BUTTON_PIN, BLUE_BUTTON_PIN, GREEN_BUTTON_PIN};
      for (int i = 0; i < 3; i++)
      {
        int reading = digitalRead(buttonPins[i]);
        if (lastButtonState[i] == HIGH && reading == LOW && (now - lastDebounceTime[i]) > debounceDelay)
        {
          lastDebounceTime[i] = now;
          // Check if correct
          if (i == ledReactionCurrentColor)
          {
            ledReactionCorrect++;
          }
          // Start the 1s delay before next color
          ledReactionWaiting = true;
          ledReactionDelayStart = now;
          turnOffAllRings();
          display.fillScreen(WHITE); // Optionally blank the screen during the wait
        }
        lastButtonState[i] = reading;
      }
    }
    break;
  }

  case CODE_BREAKER:
  {
    if (key)
    {
      if (key == '*')
      {
        showMenu();
        currentState = MENU;
        codeBreakerWrongTries = 0;
        visualMemoryWrongTries = 0;
        inputIndex = 0;
        int y = 190;
        display.fillRect(0, y, SCREEN_WIDTH, 30, WHITE);
        break;
      }
      if (key == '#')
      {
        showModeSelect();
        currentState = MODE_SELECT;
        codeBreakerWrongTries = 0;
        visualMemoryWrongTries = 0;
        break;
      }
      if (inputIndex < 3 && key >= '0' && key <= '9')
      {
        inputBuffer[inputIndex++] = key;
        showInputProgress(inputBuffer, inputIndex);
      }
      if (inputIndex == 3)
      {
        inputBuffer[3] = '\0';
        if (strcmp(inputBuffer, randomNumberStr) == 0)
        {
          int stars = maxWrongTries - codeBreakerWrongTries;
          showCenteredStarsAndScore(stars); // Show only stars and score, centered
          delay(2000);                      // Show for 2 seconds
          showMenu();                       // Return to games menu
          currentState = MENU;
          codeBreakerWrongTries = 0; // Reset tries
          inputIndex = 0;
          break;
        }

        else
        {
          int exact = 0, partial = 0;
          countMatches(inputBuffer, randomNumberStr, exact, partial);

          showCodeBreakerResult(exact, partial);
          showLastTry(inputBuffer);
          showTriesRemaining(maxWrongTries - codeBreakerWrongTries - 1);

          codeBreakerWrongTries++; // Increment on wrong try

          if (codeBreakerWrongTries >= maxWrongTries)
          {
            display.fillScreen(WHITE);
            display.setTextColor(RED);
            display.setTextSize(2);
            display.setCursor(20, 120);
            display.print("Out of tries!");
            delay(2000);

            // Clear the input progress line (where "_ _ _" is shown)
            int y = 190;
            display.fillRect(0, y, SCREEN_WIDTH, 30, WHITE);

            showMenu();
            currentState = MENU;
            codeBreakerWrongTries = 0; // Reset for next game
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
