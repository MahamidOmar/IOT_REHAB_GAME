#include "fire_store.h"
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include "display.h" // Assuming your display.h is correctly set up
#include "SD_functions.h"

extern Adafruit_SSD1306 display; // Assuming your display object is declared in your main .ino

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
String projectId;
String dataBaseUrl;

void establishFireBaseConnection() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connecting to");
  display.println("Firebase...");
  display.display();

  // config.api_key = API_KEY;
  // // config.project_id = FIREBASE_PROJECT_ID; // 'project_id' is not a member in older library versions

  // auth.user.email = USER_EMAIL;
  // auth.user.password = USER_PASSWORD;

  // config.token_status_callback = tokenStatusCallback;
  readFireBaseCredentials(fbdo,auth,config,projectId,dataBaseUrl);
  config.token_status_callback = tokenStatusCallback;

  Firebase.begin(&config, &auth);

  if (Firebase.ready()) {
    Serial.println("Firebase connection established.");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Firebase Conn");
    display.println("Success!");
    display.display();
    delay(2000); // Keep the success message for a short time
  } else {
    Serial.println("Firebase connection failed!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Firebase Conn");
    display.println("Failed!");
    display.display();
    while (true); // Stop execution if Firebase connection fails
  }
}

void addTestUserToFirestore() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Adding User...");
  display.display();

  String collectionPath = "users";
  String documentPath = collectionPath + "/9999999";
  String jsonData = "{}";

  Serial.println("Adding test user to Firestore...");

  // Added the "databaseId" parameter, which is typically "(default)" for Firestore
  if (Firebase.Firestore.createDocument(&fbdo, projectId, "(default)", documentPath, jsonData)) {
    Serial.println("Test user added successfully!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("User Add");
    display.println("Success!");
    display.display();
    delay(2000); // Keep the success message for a short time
  } else {
    Serial.print("Error adding test user: ");
    Serial.println(fbdo.errorReason());
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("User Add");
    display.println("Failed!");
    display.println(fbdo.errorReason());
    display.display();
    while (true); // Stop execution if adding user fails
  }
}