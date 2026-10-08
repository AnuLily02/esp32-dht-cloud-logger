#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include "DHT.h"
#include "AdafruitIO_WiFi.h"
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// ================= CONFIGURATION =================
// ⚠️ REPLACE THESE TWO LINES WITH YOUR ACTUAL HOME WI-FI CREDENTIALS
#define WIFI_SSID       "NAME"        // Must be 2.4 GHz network
#define WIFI_PASS       "Password"

#define IO_USERNAME     "Username"
#define IO_KEY          "Key"

#define BOT_TOKEN       "BOT TOKEN"
#define CHAT_ID         "CHAT ID"

#define DHTPIN          D2       // Connected to DHT DATA pin
#define DHTTYPE         DHT11
#define BUILTIN_LED_PIN LED_BUILTIN 

#define SPIKE_THRESHOLD 2.0      // Alert trigger if temp rises >= 2.0°C
// =================================================

DHT dht(DHTPIN, DHTTYPE);
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

float previousTemp = -999.0;
unsigned long lastCloudUpdate = 0;
const unsigned long UPDATE_INTERVAL = 10000;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP8266 Hardcoded WiFi Starting ===");

  pinMode(BUILTIN_LED_PIN, OUTPUT);
  digitalWrite(BUILTIN_LED_PIN, HIGH);

  dht.begin();

  // 1. Direct Wi-Fi Connection
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 30) {
    delay(500);
    Serial.print(".");
    wifiAttempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nFailed to connect to Wi-Fi! Please check SSID & Password.");
    return;
  }

  Serial.println("\nWi-Fi Connected Successfully!");
  Serial.print("Local IP Address: ");
  Serial.println(WiFi.localIP());

  // 2. Time Sync for Telegram SSL
  Serial.print("Syncing time via NTP...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  int timeRetries = 0;
  while (now < 24 * 3600 && timeRetries < 20) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    timeRetries++;
  }
  Serial.println("\nTime Sync Done!");

  secured_client.setInsecure();

  // 3. Connect to Adafruit IO
  Serial.print("Connecting to Adafruit IO...");
  io.connect();

  while (io.status() < AIO_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to Adafruit IO!");

  // 4. Send Telegram Notification
  Serial.println("Sending Telegram message...");
  bool sent = bot.sendMessage(CHAT_ID, "⚠️ ESP8266 Monitor Connected and Online!", "");
  
  if (sent) {
    Serial.println("Telegram message SENT successfully!");
  } else {
    Serial.println("Telegram message FAILED to send. Check Bot Token or Chat ID.");
  }
}

void loop() {
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

    AdafruitIO_Feed *tempFeed = io.feed("temperature");
    AdafruitIO_Feed *humFeed  = io.feed("humidity");
    tempFeed->save(currentTemp);
    humFeed->save(currentHumidity);

    if (previousTemp != -999.0) {
      float tempDifference = currentTemp - previousTemp;

      if (tempDifference >= SPIKE_THRESHOLD) {
        digitalWrite(BUILTIN_LED_PIN, LOW);
        
        String alertMsg = "🚨 RAPID TEMPERATURE SPIKE DETECTED!\n";
        alertMsg += "Previous: " + String(previousTemp, 1) + " °C\n";
        alertMsg += "Current: " + String(currentTemp, 1) + " °C\n";
        alertMsg += "Increase: +" + String(tempDifference, 1) + " °C";

        bot.sendMessage(CHAT_ID, alertMsg, "");
      } else {
        digitalWrite(BUILTIN_LED_PIN, HIGH);
      }
    }

    previousTemp = currentTemp;
  }
}