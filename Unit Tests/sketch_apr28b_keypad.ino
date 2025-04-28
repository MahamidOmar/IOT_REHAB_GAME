#include <Keypad.h>

// Define the number of rows and columns on your keypad
const byte ROWS = 4; // Four rows
const byte COLS = 3; // Three columns

// Define the keymap (change if your keypad has a different layout)
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};

// Connect keypad ROW0, ROW1, ROW2 and ROW3 to these Arduino pins:
byte rowPins[ROWS] = {18, 21, 22, 23}; // Adjust if using different pins

// Connect keypad COL0, COL1 and COL2 to these Arduino pins:
byte colPins[COLS] = {5, 4, 19}; // Adjust if using different pins

// Create the Keypad object
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

void setup() {
  Serial.begin(9600);
  Serial.println("Keypad Test Ready. Press any key...");
}

void loop() {
  char key = keypad.getKey();
  if (key) {
    Serial.print("You pressed: ");
    Serial.println(key);
  }
}
