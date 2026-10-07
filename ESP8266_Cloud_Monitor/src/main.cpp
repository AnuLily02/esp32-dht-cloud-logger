#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>          // Dynamic Captive Portal Manager
#include "DHT.h"
#include "AdafruitIO_WiFi.h"
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// ================= CONFIGURATION =================
#define IO_USERNAME     "YOUR_ADAFRUIT_USERNAME"
#define IO_KEY          "YOUR_ADAFRUIT_KEY"

#define BOT_TOKEN       "YOUR_TELEGRAM_BOT_TOKEN"
#define CHAT_ID         "YOUR_TELEGRAM_CHAT_ID"

#define DHTPIN          D2       // Connected to DHT DATA pin
#define DHTTYPE         DHT11
#define BUILTIN_LED_PIN LED_BUILTIN 

#define SPIKE_THRESHOLD 2.0      // Alert trigger if temp rises >= 2.0°C between checks
// =================================================

DHT dht(DHTPIN, DHTTYPE);

// Pass empty strings for Wi-Fi since WiFiManager handles connections dynamically
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, "", "");

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

float previousTemp = -999.0;
unsigned long lastCloudUpdate = 0;
const unsigned long UPDATE_INTERVAL = 10000; // 10-second logging interval

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP8266 System Starting ===");

  pinMode(BUILTIN_LED_PIN, OUTPUT);
  digitalWrite(BUILTIN_LED_PIN, HIGH); // OFF (Active LOW on NodeMCU)

  dht.begin();

  // 1. WIFIMANAGER SETUP
  WiFiManager wm;
  wm.resetSettings();
  wm.setConfigPortalTimeout(180); // 3 minutes timeout

  Serial.println("Connecting to Wi-Fi...");
  bool res = wm.autoConnect("NodeMCU-Setup", "password123");

  if (!res) {
    Serial.println("Failed to connect or portal timed out. Restarting...");
    ESP.restart();
  }

  Serial.println("Wi-Fi Connected Successfully!");
  Serial.print("Local IP Address: ");
  Serial.println(WiFi.localIP());

  // 2. TIME SYNC FOR TELEGRAM SSL VALIDATION
  Serial.print("Syncing time via NTP...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  int retries = 0;
  while (now < 24 * 3600 && retries < 20) {
    Serial.print(".");
    delay(500);
    now = time(nullptr);
    retries++;
  }

  if (now > 24 * 3600) {
    Serial.println("\nTime Synchronized!");
  } else {
    Serial.println("\nTime sync timeout, proceeding anyway...");
  }

  secured_client.setInsecure(); // Disable SSL certificate checks for Telegram

  // 3. CONNECT TO ADAFRUIT IO
  Serial.print("Connecting to Adafruit IO...");
  io.connect();

  while (io.status() < AIO_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nConnected to Adafruit IO!");

  // 4. TELEGRAM STARTUP NOTIFICATION
  Serial.println("Sending Telegram startup notification...");
  bool sent = bot.sendMessage(CHAT_ID, "⚠️ ESP8266 Temperature Monitor Connected and Online!", "");
  
  if (sent) {
    Serial.println("Telegram message SENT successfully!");
  } else {
    Serial.println("Telegram message FAILED to send. Check Bot Token or Chat ID.");
  }
}

void loop() {
  io.run(); // Keeps connection alive with Adafruit IO

  if (millis() - lastCloudUpdate > UPDATE_INTERVAL) {
    lastCloudUpdate = millis();

    float currentHumidity = dht.readHumidity();
    float currentTemp = dht.readTemperature();

    if (isnan(currentHumidity) || isnan(currentTemp)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }

    Serial.printf("Temp: %.1f °C | Humidity: %.1f %%\n", currentTemp, currentHumidity);

    // 1. Upload readings to Adafruit IO
    AdafruitIO_Feed *tempFeed = io.feed("temperature");
    AdafruitIO_Feed *humFeed  = io.feed("humidity");
    tempFeed->save(currentTemp);
    humFeed->save(currentHumidity);

    // 2. Check for Rapid Temperature Spike
    if (previousTemp != -999.0) {
      float tempDifference = currentTemp - previousTemp;

      if (tempDifference >= SPIKE_THRESHOLD) {
        digitalWrite(BUILTIN_LED_PIN, LOW); // Turn ON LED
        
        String alertMsg = "🚨 RAPID TEMPERATURE SPIKE DETECTED!\n";
        alertMsg += "Previous: " + String(previousTemp, 1) + " °C\n";
        alertMsg += "Current: " + String(currentTemp, 1) + " °C\n";
        alertMsg += "Increase: +" + String(tempDifference, 1) + " °C";

        Serial.println("Sending Telegram Alert...");
        bot.sendMessage(CHAT_ID, alertMsg, "");
      } else {
        digitalWrite(BUILTIN_LED_PIN, HIGH); // Turn OFF LED
      }
    }

    previousTemp = currentTemp;
  }
}