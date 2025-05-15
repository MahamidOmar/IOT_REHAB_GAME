#include <WiFi.h>
#include <Wire.h>
#include "display.h"
#include "credentials.h"
#include "SD_functions.h"
#include "fire_store.h"

extern Adafruit_SSD1306 display;
bool connected = false;

void setup() {
  // Initialize OLED display (rest of your display initialization code)
  Serial.begin(115200);
  initDisplay();
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Initializing...");
  display.display();
  initSd();
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Loading Credentials...");
  display.display();

  getWifiData(); // Load WiFi credentials from SD card

  if (ssid == nullptr || password == nullptr) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Failed to load");
    display.println("credentials");
    display.display();
    while (true);
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Initializing Wi-Fi...");
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connecting to Wi-Fi...");
  display.display();

  int retryCount = 0;
  const int maxRetries = 20;
  while (WiFi.status() != WL_CONNECTED && retryCount < maxRetries) {
    delay(500);
    retryCount++;
    display.print(".");
    display.display();
  }

  display.clearDisplay();

  if (WiFi.status() == WL_CONNECTED) {
    display.println("Wi-Fi connected!");
    display.print("IP: ");
    display.println(WiFi.localIP());
    connected = true;
  } else {
    display.println("Failed to connect.");
    display.println("Check credentials.");
  }
  display.display();

  establishFireBaseConnection(); // Establish Firebase connection
  addTestUserToFirestore();      // Add the test user to Firestore
}

void loop() {
  // Nothing needed in the loop for this simple test
}