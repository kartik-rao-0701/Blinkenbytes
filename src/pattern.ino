#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <FastLED.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <stdio.h>

// ================= LED Matrix Connections =================
#define R1_PIN     4     // Red Row 1
#define G1_PIN     5     // Green Row 1
#define B1_PIN     6     // Blue Row 1
#define R2_PIN     7     // Red Row 2
#define G2_PIN     15    // Green Row 2
#define B2_PIN     16    // Blue Row 2
#define A_PIN      17    // Address line A
#define B_PIN      18    // Address line B
#define C_PIN      8     // Address line C
#define D_PIN      3     // Address line D
#define E_PIN      -1    // Address line E (Not used)
#define CLK_PIN    19    // Clock pin
#define LAT_PIN    20    // Latch pin
#define OE_PIN     2     // Output Enable

// ===== Panel Configuration =====
#define PANEL_WIDTH     64
#define PANEL_HEIGHT    32
#define PANEL_CHAIN     1  // Number of panels chained
#define MATRIX_ROTATION 0

// Define PANELS_NUMBER (missing definition)
#define PANELS_NUMBER 1

// Define dimensions for plasma effect
#define PANE_WIDTH (PANEL_WIDTH * PANELS_NUMBER)
#define PANE_HEIGHT PANEL_HEIGHT

// Define HUB75 pin mappings for ESP32
HUB75_I2S_CFG::i2s_pins pins = {
    R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN, 
    A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, 
    LAT_PIN, OE_PIN, CLK_PIN
};

// Create matrix configuration with pins
HUB75_I2S_CFG mxconfig(PANEL_WIDTH, PANEL_HEIGHT, PANEL_CHAIN, pins);

// placeholder for the matrix object
MatrixPanel_I2S_DMA *dma_display = nullptr;

uint16_t time_counter = 0, cycles = 0, fps = 0;
unsigned long fps_timer;

CRGB currentColor;
CRGBPalette16 palettes[] = {HeatColors_p, LavaColors_p, RainbowColors_p, RainbowStripeColors_p, CloudColors_p};
CRGBPalette16 currentPalette = palettes[0];

CRGB ColorFromCurrentPalette(uint8_t index = 0, uint8_t brightness = 255, TBlendType blendType = LINEARBLEND) {
  return ColorFromPalette(currentPalette, index, brightness, blendType);
}

void setup() {
  Serial.begin(115200);
  
  Serial.println(F("*****************************************************"));
  Serial.println(F("*        ESP32-HUB75-MatrixPanel-I2S-DMA DEMO       *"));
  Serial.println(F("*****************************************************"));

  // Additional configuration to improve display quality
  mxconfig.clkphase = false;
  mxconfig.driver = HUB75_I2S_CFG::FM6126A;
  mxconfig.latch_blanking = 4;
  mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_10M;

  // Create and initialize the matrix object
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);

  // let's adjust default brightness to about 75%
  dma_display->setBrightness8(255);    // range is 0-255, 0 - 0%, 255 - 100%

  // Allocate memory and start DMA display
  if(!dma_display->begin())
      Serial.println("****** !KABOOM! I2S memory allocation failed ***********");
 
  // well, hope we are OK, let's draw some colors first :)
  Serial.println("Fill screen: RED");
  dma_display->fillScreenRGB888(255, 0, 0);
  delay(1000);

  Serial.println("Fill screen: GREEN");
  dma_display->fillScreenRGB888(0, 255, 0);
  delay(1000);

  Serial.println("Fill screen: BLUE");
  dma_display->fillScreenRGB888(0, 0, 255);
  delay(1000);

  Serial.println("Fill screen: Neutral White");
  dma_display->fillScreenRGB888(64, 64, 64);
  delay(1000);

  Serial.println("Fill screen: black");
  dma_display->fillScreenRGB888(0, 0, 0);
  delay(1000);

  // Set current FastLED palette
  currentPalette = RainbowColors_p;
  Serial.println("Starting plasma effect...");
  fps_timer = millis();
}

void loop() {
  for (int x = 0; x < PANE_WIDTH; x++) {
    for (int y = 0; y < PANE_HEIGHT; y++) {
      int16_t v = 128;
      uint8_t wibble = sin8(time_counter);
      v += sin16(x * wibble * 3 + time_counter);
      v += cos16(y * (128 - wibble) + time_counter);
      v += sin16(y * x * cos8(-time_counter) / 8);

      currentColor = ColorFromPalette(currentPalette, (v >> 8)); //, brightness, currentBlendType);
      dma_display->drawPixelRGB888(x, y, currentColor.r, currentColor.g, currentColor.b);
    }
  }

  ++time_counter;
  ++cycles;
  ++fps;

  if (cycles >= 1024) {
    time_counter = 0;
    cycles = 0;
    currentPalette = palettes[random(0, sizeof(palettes) / sizeof(palettes[0]))];
  }

  // print FPS rate every 5 seconds
  // Note: this is NOT a matrix refresh rate, it's the number of data frames being drawn to the DMA buffer per second
  if (fps_timer + 5000 < millis()) {
    Serial.printf_P(PSTR("Effect fps: %d\n"), fps / 5);
    fps_timer = millis();
    fps = 0;
  }
}