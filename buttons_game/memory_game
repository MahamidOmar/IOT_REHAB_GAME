#include <Adafruit_NeoPixel.h>

#define BUTTON_RED   18
#define BUTTON_BLUE  19
#define BUTTON_GREEN 21
#define LED_PIN      2       // NeoPixel data pin
#define NUMPIXELS    8       // Number of NeoPixels in your strip

const int SEQUENCE_LENGTH = 6;
int colorSequence[SEQUENCE_LENGTH];
int currentStep = 0;
bool gameActive = true;

int lastButtonState[3] = {HIGH, HIGH, HIGH};
const unsigned long debounceDelay = 300;
unsigned long lastDebounceTime[3] = {0, 0, 0};

Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

void showColor(int colorIndex, int duration_ms) {
  uint32_t color;
  switch (colorIndex) {
    case 0: color = pixels.Color(150, 0, 0);   break; // Red
    case 1: color = pixels.Color(0, 0, 150);   break; // Blue
    case 2: color = pixels.Color(0, 150, 0);   break; // Green
    default: color = pixels.Color(0, 0, 0);    break; // Off
  }
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, color);
  }
  pixels.show();
  delay(duration_ms);
  // Turn off after showing
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, 0);
  }
  pixels.show();
}

void setup() {
  Serial.begin(9600);
  delay(2000);

  pinMode(BUTTON_RED, INPUT_PULLUP);
  pinMode(BUTTON_BLUE, INPUT_PULLUP);
  pinMode(BUTTON_GREEN, INPUT_PULLUP);

  pixels.begin();
  pixels.clear();
  pixels.show();

  randomSeed(analogRead(0));

  for (int i = 0; i < SEQUENCE_LENGTH; i++) {
    int randNum = random(0, 10000);
    colorSequence[i] = randNum % 3;
  }

  Serial.print("Generated color sequence: ");
  for (int i = 0; i < SEQUENCE_LENGTH; i++) {
    switch (colorSequence[i]) {
      case 0: Serial.print("Red "); break;
      case 1: Serial.print("Blue "); break;
      case 2: Serial.print("Green "); break;
    }
  }
  Serial.println();

  // Show the sequence on the NeoPixel strip
  for (int i = 0; i < SEQUENCE_LENGTH; i++) {
    switch (colorSequence[i]) {
      case 0: Serial.println("Showing RED"); break;
      case 1: Serial.println("Showing BLUE"); break;
      case 2: Serial.println("Showing GREEN"); break;
    }
    showColor(colorSequence[i], 3000); // Show color for 3 seconds
    delay(1000);                       // 1 second pause between colors
  }
}

void loop() {
  if (!gameActive) return;

  int buttonPins[3] = {BUTTON_RED, BUTTON_BLUE, BUTTON_GREEN};
  const char* buttonNames[3] = {"Red Button Pressed", "Blue Button Pressed", "Green Button Pressed"};

  for (int i = 0; i < 3; i++) {
    int reading = digitalRead(buttonPins[i]);
    if (lastButtonState[i] == HIGH && reading == LOW && (millis() - lastDebounceTime[i]) > debounceDelay) {
      lastDebounceTime[i] = millis();
      Serial.println(buttonNames[i]);
      if (i == colorSequence[currentStep]) {
        currentStep++;
        if (currentStep == SEQUENCE_LENGTH) {
          Serial.println("Success");
          gameActive = false;
        }
      } else {
        Serial.println("Wrong sequence try again");
        currentStep = 0;
      }
    }
    lastButtonState[i] = reading;
  }
}
