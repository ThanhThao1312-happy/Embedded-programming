#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Firebase_ESP_Client.h>

// Insert your RTDB URL
#define FIREBASE_HOST "https://firedetection-ab06f-default-rtdb.firebaseio.com/"    
// Insert Firebase Database secret
#define FIREBASE_AUTH "AIzaSyCw85h4ynNMqUsULKi06ic399BL4yC6ffg"

// Insert your network credentials
#define WIFI_SSID "huywifi"                        
#define WIFI_PASSWORD "12345678"  

// Define Firebase Data object
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

bool pumpStatus = false;  // status of the pump received from Firebase as a boolean
const int relayPin = D1; // Number of GPIO that is connected to Relay
String path = "Pump"; // path to Pump on Firebase Database 

void setup() 
{
  // Initialize serial communication
  Serial.begin(115200);
  
  // Set the relay pin as OUTPUT
  pinMode(relayPin, OUTPUT);
  // Initialize relay state as OFF
  digitalWrite(relayPin, LOW);

  // Connect to Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected to Wi-Fi with IP: ");
  Serial.println(WiFi.localIP());

  // Configure Firebase
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;

  // Initialize Firebase
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Serial.println("Connected to Firebase");
}

void loop() 
{
  if (Firebase.RTDB.getBool(&fbdo, path)) {
    pumpStatus = fbdo.boolData();
    Serial.print("Pump status from Firebase: ");
    Serial.println(pumpStatus);
    if (pumpStatus) {
      digitalWrite(relayPin, HIGH);
    } 
    else {
      digitalWrite(relayPin, LOW);
    }
  } 
  else {
    Serial.print("Failed to get pump status: ");
    Serial.println(fbdo.errorReason());
  }

  delay(1000); 
}
