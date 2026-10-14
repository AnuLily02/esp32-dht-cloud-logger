#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include "DHT.h"
#include "AdafruitIO_WiFi.h"
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// 1. Load private credentials from include/config.h
#include "config.h"

// 2. Hardware Pin Setup & Sensitivity
#define DHTPIN          D2       // Connect DHT DATA pin to D2
#define DHTTYPE         DHT11
#define BUILTIN_LED_PIN LED_BUILTIN 

#define SPIKE_THRESHOLD 1.5      // Sends alert if temp rises >= 1.5°C between 10-sec checks

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

  // --- FORCE WI-FI FLASH RESET (Clears stuck session memory) ---
  Serial.println("Resetting ESP8266 Wi-Fi memory...");
  WiFi.persistent(false);
  WiFi.disconnect(true);
  delay(2000);
  // -----------------------------------------------------------

  // Connect to Wi-Fi Network
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  // Auto-restart if stuck connecting for over 15 seconds
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n>>> Wi-Fi Connection Timeout! Restarting ESP8266...");
    ESP.restart();
  }

  Serial.println("\nWi-Fi Connected Successfully!");
  Serial.print("Local IP Address: ");
  Serial.println(WiFi.localIP());

  // Disable SSL Certificate validation for Telegram API
  secured_client.setInsecure();
  secured_client.setTimeout(15000); // 15-second timeout for Telegram requests

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
  bool startupSent = bot.sendMessage(CHAT_ID, "⚠️ ESP8266 Temperature Monitor Connected & Online!", "");
  
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

    // Check for Temperature Spike OR High Temperature (>= 35°C)
    if (previousTemp != -999.0) {
      float tempDifference = currentTemp - previousTemp;

      if (tempDifference >= SPIKE_THRESHOLD || currentTemp >= 35.0) {
        digitalWrite(BUILTIN_LED_PIN, LOW); // Turn ON onboard LED indicator
        
        String alertMsg = "🚨 TEMPERATURE ALERT DETECTED!\n";
        alertMsg += "Current Temp: " + String(currentTemp, 1) + " °C\n";
        alertMsg += "Humidity: " + String(currentHumidity, 1) + " %\n";
        if (tempDifference >= SPIKE_THRESHOLD) {
          alertMsg += "Spike Increase: +" + String(tempDifference, 1) + " °C";
        }

        Serial.println("Attempting to send Telegram Alert...");
        
        // Re-arm SSL settings right before sending to prevent connection drops
        secured_client.setInsecure();
        bool alertSent = bot.sendMessage(CHAT_ID, alertMsg, "");
        
        if (alertSent) {
          Serial.println(">>> SUCCESS: Telegram Spike Alert SENT!");
        } else {
          Serial.println(">>> ERROR: Telegram Spike Alert FAILED to send!");
        }
      } else {
        digitalWrite(BUILTIN_LED_PIN, HIGH); // Turn OFF onboard LED indicator
      }
    }

    previousTemp = currentTemp;
  }
}