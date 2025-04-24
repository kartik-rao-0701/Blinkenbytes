#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <ESP32-VirtualMatrixPanel-I2S-DMA.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ===== Panel Configuration =====
#define PANEL_WIDTH     64
#define PANEL_HEIGHT    32
#define PANEL_CHAIN     1

HUB75_I2S_CFG mxconfig(
  PANEL_WIDTH,
  PANEL_HEIGHT,
  PANEL_CHAIN
);
MatrixPanel_I2S_DMA* dma_display = nullptr;

// ===== Wi-Fi & Weather API Config =====
const char* ssid = "NSUT_WIFI";
const char* password = "";
const String apiKey = "9ad9d95f5fe5ed2ab0ac1c0c6f391dcc";
const String city = "Delhi";
const String units = "metric"; // "imperial" for Fahrenheit

void setup() {
  Serial.begin(115200);

  // Set up Matrix Pins
  mxconfig.gpio.r1 = 4;
  mxconfig.gpio.g1 = 5;
  mxconfig.gpio.b1 = 6;
  mxconfig.gpio.r2 = 7;
  mxconfig.gpio.g2 = 15;
  mxconfig.gpio.b2 = 16;
  mxconfig.gpio.a = 17;
  mxconfig.gpio.b = 18;
  mxconfig.gpio.c = 8;
  mxconfig.gpio.d = 3;
  mxconfig.gpio.e = -1;
  mxconfig.gpio.lat = 20;
  mxconfig.gpio.oe = 2;
  mxconfig.gpio.clk = 19;

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->clearScreen();

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  dma_display->fillScreenRGB888(0, 0, 0);
  dma_display->setTextColor(dma_display->color565(255, 255, 0));
  dma_display->setCursor(0, 0);
  dma_display->print("WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  dma_display->fillScreenRGB888(0, 0, 0);
  dma_display->setTextColor(dma_display->color565(0, 255, 0));
  dma_display->setCursor(0, 0);
  dma_display->print("Connected");

  delay(1000);
  fetchAndDisplayWeather();
}

void loop() {
  // Refresh weather every 10 minutes
  fetchAndDisplayWeather();
  delay(10 * 60 * 1000);
}

void fetchAndDisplayWeather() {
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "&appid=" + apiKey + "&units=" + units;

  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode > 0) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);

    float temp = doc["main"]["temp"];
    const char* condition = doc["weather"][0]["main"];

    Serial.printf("Temp: %.1f, Condition: %s\n", temp, condition);

    dma_display->fillScreenRGB888(0, 0, 0); // Clear screen

    // Display temperature
    char tempStr[16];
    sprintf(tempStr, "%.1f C", temp);
    dma_display->setTextColor(dma_display->color565(0, 255, 255));
    dma_display->setCursor(0, 0);
    dma_display->print(tempStr);

    // Display condition
    dma_display->setTextColor(dma_display->color565(255, 255, 0));
    dma_display->setCursor(0, 10);
    dma_display->print(String(condition).substring(0, 10));
  } else {
    dma_display->fillScreenRGB888(0, 0, 0);
    dma_display->setTextColor(dma_display->color565(255, 0, 0));
    dma_display->setCursor(0, 0);
    dma_display->print("HTTP error");
    Serial.println("Failed to fetch weather");
  }

  http.end();
}