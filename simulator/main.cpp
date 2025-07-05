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
  PLAYER_SELECT,
  MENU,
  CODE_BREAKER,
  VISUAL_MEMORY,
  VISUAL_MEMORY_INPUT,
  VISUAL_MEMORY_RESULT
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
  showBottomHints();
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

  showPlayerMenu();

  currentState = PLAYER_SELECT;

  pinMode(RED_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BLUE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(GREEN_BUTTON_PIN, INPUT_PULLUP);
}

void loop()
{
  char key = keypad.getKey();
  switch (currentState)
  {
  case PLAYER_SELECT:
    if (key && key >= '1' && key <= '4' && key - '0' <= playerCount)
    {
      currentPlayer = key - '0';
      showPlayerSelected(currentPlayer);
      showMenu();
      currentState = MENU;
    }
    break;

  case MENU:
    if (key)
    {
      if (key == '#')
      {
        showPlayerMenu();
        currentState = PLAYER_SELECT;
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
      else
      {
        showMenuMessage("Please choose 1 or 2");
      }
    }
    break;

  case VISUAL_MEMORY_INPUT:
  {
    // --- Handle keypad keys for menu/logout ---
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
      showPlayerMenu();
      currentState = PLAYER_SELECT;
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

  case CODE_BREAKER:
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
        showPlayerMenu();
        currentState = PLAYER_SELECT;
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
            // showInputProgress(inputBuffer, inputIndex);
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
