#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include "DHT.h"
#include "AdafruitIO_WiFi.h"
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// Load private credentials from include/config.h
#include "config.h"

// Hardware Pins & Settings
#define DHTPIN          D2       // Connected to DHT DATA pin
#define DHTTYPE         DHT11
#define BUILTIN_LED_PIN LED_BUILTIN 

#define SPIKE_THRESHOLD 1.5      // Alert trigger if temp rises >= 1.5°C in 10 seconds

// Objects
DHT dht(DHTPIN, DHTTYPE);
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

float previousTemp = -999.0;
unsigned long lastCloudUpdate = 0;
const unsigned long UPDATE_INTERVAL = 10000; // 10-second check loop

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP8266 Temperature Monitor Starting ===");

  pinMode(BUILTIN_LED_PIN, OUTPUT);
  digitalWrite(BUILTIN_LED_PIN, HIGH); // Turn off LED initially

  dht.begin();

  // 1. Connect to Wi-Fi
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

  // 2. Configure SSL Client (Insecure mode bypasses cert checks)
  secured_client.setInsecure();
  secured_client.setTimeout(15000); // 15-second timeout for Telegram requests

  // 3. Connect to Adafruit IO
  Serial.print("Connecting to Adafruit IO...");
  io.connect();

  while (io.status() < AIO_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to Adafruit IO!");

  // 4. Send Startup Telegram Notification
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

  if (millis() - lastCloudUpdate > UPDATE_INTERVAL) {
    lastCloudUpdate = millis();

    float currentHumidity = dht.readHumidity();
    float currentTemp = dht.readTemperature();

    if (isnan(currentHumidity) || isnan(currentTemp)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }

    Serial.printf("Temp: %.1f °C | Humidity: %.1f %%\n", currentTemp, currentHumidity);

    // Upload feeds to Adafruit IO
    AdafruitIO_Feed *tempFeed = io.feed("temperature");
    AdafruitIO_Feed *humFeed  = io.feed("humidity");
    tempFeed->save(currentTemp);
    humFeed->save(currentHumidity);

    // Check for Temperature Spike OR High Temperature (>= 35°C)
    if (previousTemp != -999.0) {
      float tempDifference = currentTemp - previousTemp;

      if (tempDifference >= SPIKE_THRESHOLD || currentTemp >= 35.0) {
        digitalWrite(BUILTIN_LED_PIN, LOW); // Turn ON onboard LED
        
        String alertMsg = "🚨 TEMPERATURE ALERT DETECTED!\n";
        alertMsg += "Current Temp: " + String(currentTemp, 1) + " °C\n";
        alertMsg += "Humidity: " + String(currentHumidity, 1) + " %\n";
        if (tempDifference >= SPIKE_THRESHOLD) {
          alertMsg += "Spike Increase: +" + String(tempDifference, 1) + " °C";
        }

        Serial.println("Attempting to send Telegram Alert...");
        
        // Re-apply SSL settings right before sending to prevent connection drops
        secured_client.setInsecure();
        bool alertSent = bot.sendMessage(CHAT_ID, alertMsg, "");
        
        if (alertSent) {
          Serial.println(">>> SUCCESS: Telegram Spike Alert SENT!");
        } else {
          Serial.println(">>> ERROR: Telegram Spike Alert FAILED to send!");
        }
      } else {
        digitalWrite(BUILTIN_LED_PIN, HIGH); // Turn OFF onboard LED
      }
    }

    previousTemp = currentTemp;
  }
}