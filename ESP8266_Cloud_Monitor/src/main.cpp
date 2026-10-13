#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include "DHT.h"
#include "AdafruitIO_WiFi.h"
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// 1. Load private keys from include/config.h
#include "config.h"

// 2. Hardware Pin Setup & Sensitivity
#define DHTPIN          D2       // Connect DHT DATA pin to D2
#define DHTTYPE         DHT11
#define BUILTIN_LED_PIN LED_BUILTIN 

#define SPIKE_THRESHOLD 2.0      // Sends alert if temp rises >= 2.0°C between 10-sec checks

// 3. Initialize Sensor and Clients
DHT dht(DHTPIN, DHTTYPE);
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

float previousTemp = -999.0;
unsigned long lastCloudUpdate = 0;
const unsigned long UPDATE_INTERVAL = 10000; // Reads every 10 seconds

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP8266 Temperature Monitor Starting ===");

  pinMode(BUILTIN_LED_PIN, OUTPUT);
  digitalWrite(BUILTIN_LED_PIN, HIGH); // Turn off onboard LED initially

  dht.begin();

  // Connect to Wi-Fi Network
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWi-Fi Connected Successfully!");
  Serial.print("Local IP Address: ");
  Serial.println(WiFi.localIP());

  // Disable SSL Certificate validation for Telegram API
  secured_client.setInsecure();

  // Connect to Adafruit IO Cloud
  Serial.print("Connecting to Adafruit IO...");
  io.connect();

  while (io.status() < AIO_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to Adafruit IO!");

  // Send System Startup Alert via Telegram
  Serial.println("Attempting to send startup Telegram notification...");
  bool startupSent = bot.sendMessage(CHAT_ID, "⚠️ ESP8266 Temperature Monitor Connected and Online!", "");
  
  if (startupSent) {
    Serial.println(">>> SUCCESS: Startup message SENT to Telegram!");
  } else {
    Serial.println(">>> ERROR: Startup message FAILED to send to Telegram.");
  }
}

void loop() {
  // Keep Adafruit IO connection active
  io.run();

  // Execute reading task every 10 seconds
  if (millis() - lastCloudUpdate > UPDATE_INTERVAL) {
    lastCloudUpdate = millis();

    float currentHumidity = dht.readHumidity();
    float currentTemp = dht.readTemperature();

    if (isnan(currentHumidity) || isnan(currentTemp)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }

    // Print reading to Serial Monitor
    Serial.printf("Temp: %.1f °C | Humidity: %.1f %%\n", currentTemp, currentHumidity);

    // Save data feeds to Adafruit IO Cloud
    AdafruitIO_Feed *tempFeed = io.feed("temperature");
    AdafruitIO_Feed *humFeed  = io.feed("humidity");
    tempFeed->save(currentTemp);
    humFeed->save(currentHumidity);

    // Check for Rapid Temperature Spike
    if (previousTemp != -999.0) {
      float tempDifference = currentTemp - previousTemp;

      if (tempDifference >= SPIKE_THRESHOLD) {
        digitalWrite(BUILTIN_LED_PIN, LOW); // Turn ON LED indicator
        
        String alertMsg = "🚨 RAPID TEMPERATURE SPIKE DETECTED!\n";
        alertMsg += "Previous: " + String(previousTemp, 1) + " °C\n";
        alertMsg += "Current: " + String(currentTemp, 1) + " °C\n";
        alertMsg += "Increase: +" + String(tempDifference, 1) + " °C";

        Serial.println("Attempting to send Telegram Alert...");
        bool alertSent = bot.sendMessage(CHAT_ID, alertMsg, "");
        
        if (alertSent) {
          Serial.println(">>> SUCCESS: Telegram Spike Alert SENT!");
        } else {
          Serial.println(">>> ERROR: Telegram Spike Alert FAILED to send!");
        }
      } else {
        digitalWrite(BUILTIN_LED_PIN, HIGH); // Turn OFF LED indicator
      }
    }

    previousTemp = currentTemp;
  }
}