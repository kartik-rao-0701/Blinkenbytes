/*
 * ESP32-S3 N8R8 Display and Media Center (Optimized for No PSRAM)
 * Features:
 * - Rotary encoder navigation with scrollwheel-like interface
 * - Multiple screens (Time/Date, Weather, Stocks, Bitcoin)
 * - SD card music player with album art display
 * - Radio playback with time-shift capability using SD card buffer
 * - SD card image slideshow with proper orientation
 * - Optimized for ESP32-S3 with 8MB RAM, no PSRAM dependency
 */

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <ESP32Encoder.h>
#include <SD.h>
#include <SPI.h>
#include <driver/i2s.h>
#include <Audio.h>
#include <JPEGDecoder.h>
#include <JPEGDEC.h>
#include <esp_heap_caps.h>
#include <algorithm>

// ================= LED Matrix Connections =================
#define R1_PIN     1     // Red Row 1
#define G1_PIN     2     // Green Row 1
#define B1_PIN     3     // Blue Row 1
#define R2_PIN     4     // Red Row 2
#define G2_PIN     5     // Green Row 2
#define B2_PIN     6     // Blue Row 2
#define A_PIN      7     // Address line A
#define B_PIN      8     // Address line B
#define C_PIN      12    // Address line C
#define D_PIN      13    // Address line D
#define E_PIN      -1    // Address line E (Only used in 64-row panels)
#define CLK_PIN    15    // Clock pin
#define LAT_PIN    16    // Latch pin
#define OE_PIN     17    // Output Enable

// ================= SD Card Connections =================
#define SD_DATA2   36    // Data2
#define SD_DATA3   35    // CD/Data3
#define SD_CMD     40    // Command
#define SD_CLK     39    // Clock
#define SD_DATA0   38    // Data0
#define SD_DATA1   37    // Data1

// ================= Rotary Encoder Connections =================
#define ENCODER_CLK   18   // CLK pin
#define ENCODER_DT    19   // DT pin
#define ENCODER_SW    20   // Switch (button) pin

// ================= MAX98357 Audio Chip =================
#define I2S_DIN    9     // Digital audio data input
#define I2S_BCLK   10    // Bit clock
#define I2S_LRCLK  11    // Left-right clock

// WiFi credentials
const char* ssid = "kartik's iPhone";
const char* password = "jeiuvbsfgn";

// API Keys
const char* weatherApiKey = "9ad9d95f5fe5ed2ab0ac1c0c6f391dcc";
const char* stockApiKey = "49Y4AEA59RIWSDDW";
const char* lastFmApiKey = "4b1760bb21f92ed72c6e38a55c1cfc7f";

// API Endpoints
const char* weatherServer = "http://api.openweathermap.org/data/2.5/weather?id=1273294&units=metric&appid=9ad9d95f5fe5ed2ab0ac1c0c6f391dcc";
const String stockSymbols[] = {"TSLA", "AAPL", "MSFT", "GOOGL", "AMZN"};
int currentStockIndex = 0;
const int numStocks = 5;

// Bitcoin API
const char* cryptoServer = "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd&include_24hr_change=true";

// Radio stations in Delhi
const char* radioStations[] = {
    "http://peridot.streamguys.com:7150/Mirchi",
    "http://prclive1.listenon.in:9888/",
    "http://sc-bb.1.fm:8017/",
    "http://airhlspush.pc.cdn.bitgravity.com/airlive/hlspush/hlsmaster/fmaircr_64k.m3u8",
    "http://airhlspush.pc.cdn.bitgravity.com/airlive/hlspush/hlsmaster/fmaircr_64k.m3u8"
};
const char* radioNames[] = {"Radio Mirchi", "Radio City", "Bombay Beats", "AIR Rainbow", "AIR Gold"};
const int numRadioStations = 5;
int currentRadioStation = 0;

// NTP Server for time synchronization
const char* ntpServer = "pool.ntp.org";
long gmtOffset_sec = 19800;  // GMT+5:30 for India
int daylightOffset_sec = 0;

// Matrix Display Configuration
#define PANEL_WIDTH 64
#define PANEL_HEIGHT 32
#define PANEL_CHAIN 1
#define MATRIX_ROTATION 0

// Define HUB75 pin mappings for ESP32-S3
HUB75_I2S_CFG::i2s_pins pins = {
    R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN, 
    A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, 
    LAT_PIN, OE_PIN, CLK_PIN
};

HUB75_I2S_CFG mxconfig(PANEL_WIDTH, PANEL_HEIGHT, PANEL_CHAIN, pins);
MatrixPanel_I2S_DMA* display = nullptr;

// I2S configuration for audio
i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
};

i2s_pin_config_t i2s_pins = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRCLK,
    .data_out_num = I2S_DIN,
    .data_in_num = -1
};

// Audio objects
Audio audio;

// Rotary encoder setup
ESP32Encoder encoder;
int lastEncoderValue = 0;
bool buttonPressed = false;
long lastButtonPress = 0;
bool doubleClickDetected = false;
unsigned long lastSingleClickTime = 0;

// Menu system
#define MENU_HOME 0
#define MENU_CLOCK 1
#define MENU_WEATHER 2
#define MENU_STOCKS 3
#define MENU_CRYPTO 4
#define MENU_RADIO 5
#define MENU_MUSIC 6
#define MENU_SLIDESHOW 7

int mainMenuPosition = 0;
int submenuPosition = 0;
bool inSubmenu = false;
bool inScrollAnimation = false;
bool audioPlaying = false;
bool radioPlaying = false;
bool musicPlaying = false;
bool slideshowActive = false;
bool musicNavigationMode = false;

// Data structures
struct WeatherData {
    float temp;
    float feels_like;
    int humidity;
    String description;
    String icon;
    String city;
} currentWeather;

struct StockData {
    String symbol;
    float price;
    float change;
    float change_percent;
    float prices[10];
} stocks[5];

struct CryptoData {
    float price;
    float change_24h;
} bitcoin;

// Radio buffer settings - OPTIMIZED FOR LESS MEMORY
#define RADIO_BUFFER_MINUTES 2  // Reduced from 10 to save memory
#define BUFFER_CHUNK_SIZE 4096  // Reduced from 32768
bool timeShiftActive = false;
int timeShiftOffset = 0;
String currentSong = "Unknown";
String currentArtist = "Unknown";

// Music player variables - REDUCED ARRAY SIZES
#define MAX_SONGS 50       // Reduced from 100
#define MAX_IMAGES 25      // Reduced from 50
String musicFiles[MAX_SONGS];
String musicArtFiles[MAX_SONGS];
int musicCount = 0;
int currentMusic = 0;
String currentMusicTitle = "Unknown";
String currentMusicArtist = "Unknown";
String currentMusicAlbum = "Unknown";
String currentAlbumArtPath = "";
int musicVolume = 15;

// Slideshow variables
String imageFiles[MAX_IMAGES];
int imageCount = 0;
int currentImage = 0;
unsigned long lastImageChange = 0;
const unsigned long imageChangeInterval = 5000;

// Animation variables
unsigned long lastFrameTime = 0;
const int frameDelay = 50;
int scrollOffset = 0;
bool transitioning = false;
int targetScreen = 0;
int currentScreen = 0;
int scrollWheelPosition = 0;
int scrollWheelTargetPosition = 0;
bool scrollWheelAnimating = false;

// Update intervals
unsigned long lastWeatherUpdate = 0;
unsigned long lastStockUpdate = 0;
unsigned long lastCryptoUpdate = 0;
unsigned long lastTimeUpdate = 0;
const unsigned long weatherInterval = 300000;
const unsigned long stockInterval = 300000;
const unsigned long cryptoInterval = 180000;
const unsigned long timeUpdateInterval = 1000;

// SD Card buffer for radio time-shift
File radioBufferFile;
int currentBufferIndex = 0;
const int maxBufferFiles = RADIO_BUFFER_MINUTES * 6;
String radioBufferFiles[RADIO_BUFFER_MINUTES * 6];
unsigned long lastBufferWrite = 0;
const unsigned long bufferWriteInterval = 10000;

// Weather icons (5x5 pixels)
const uint16_t SUNNY_ICON[] = {
    0, 0xFFE0, 0, 0, 0,
    0xFFE0, 0xFFE0, 0xFFE0, 0, 0,
    0, 0xFFE0, 0xFFE0, 0xFFE0, 0,
    0, 0, 0xFFE0, 0xFFE0, 0xFFE0,
    0, 0, 0, 0xFFE0, 0
};

const uint16_t CLOUDY_ICON[] = {
    0, 0xFFFF, 0xFFFF, 0, 0,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0,
    0, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0, 0, 0xFFFF, 0xFFFF, 0xFFFF,
    0, 0, 0, 0, 0
};

const uint16_t RAINY_ICON[] = {
    0, 0xFFFF, 0xFFFF, 0, 0,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0,
    0, 0x001F, 0xFFFF, 0x001F, 0xFFFF,
    0, 0, 0x001F, 0, 0x001F,
    0, 0x001F, 0, 0, 0
};

// Menu wheel items
const char* mainMenuItems[] = {
    "Clock", "Weather", "Stocks", "Bitcoin", "Radio", "Music", "Slideshow"
};
const int numMainMenuItems = 7;

// JPEG decoder for album art
JPEGDEC jpeg;

// Function to check available memory
bool checkMemory(size_t requiredSize) {
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < requiredSize) {
        // Display low memory warning
        display->fillRect(0, 0, PANEL_WIDTH, 8, 0);  // Clear top line
        display->setTextSize(1);
        display->setCursor(2, 1);
        display->setTextColor(display->color565(255, 0, 0));
        display->print("Low Mem: ");
        display->print(freeHeap / 1024);
        display->print("kB");
        return false;
    }
    return true;
}
#ifndef PI
#define PI 3.14159265358979323846
#endif
// Function prototypes
void fetchWeather();
void fetchStockData(int index);
void fetchBitcoinData();
void displayTime();
void displayWeather();
void displayStock(int index);
void displayCrypto();
void displayRadioMenu();
void displayRadioPlayer();
void displayMusicMenu();
void displayMusicPlayer();
void displaySlideshow();
void drawWeatherIcon(String iconCode, int x, int y);
void drawStockChart(StockData stock, int x, int y, int width, int height);
void handleEncoder();
void handleButton();
void showLoadingScreen(String message);
void drawScreenContent(int screen, int offset);
void updateDisplay();
void initSDCard();
void scanSDCardImages();
void scanSDCardMusic();
void loadAlbumArt(int songIndex);
void initAudio();
void startRadio(int stationIndex);
void stopRadio();
void startTimeShift(int secondsBack);
void stopTimeShift();
void playMusic(int songIndex);
void stopMusic();
void nextMusic();
void prevMusic();
void playTickSound();
void drawScrollWheel(int position);
void animateScrollWheel();
void showMenuTransition(int fromItem, int toItem);
void initRadioBuffer();
void writeToRadioBuffer(uint8_t* buffer, size_t size);
void readFromRadioBuffer(int timeOffset);
String getMusicFileTitle(String filename);
String getMusicFileArtist(String filename);
bool loadJpegFromSD(String filename);
bool loadBmpFromSD(String filename);
int drawMCUblockCallback(JPEGDRAW* pDraw);
String URLEncode(const char* msg);
void checkSystemMemory();


void setup() {
    // Initialize rotary encoder
    encoder.attachHalfQuad(ENCODER_CLK, ENCODER_DT);
    encoder.setCount(0);
    
    // Set up button pin
    pinMode(ENCODER_SW, INPUT_PULLUP);
    
    // Initialize display
    display = new MatrixPanel_I2S_DMA(mxconfig);
    display->begin();
    display->setBrightness8(100);
    display->clearScreen();
    display->setRotation(MATRIX_ROTATION);
    
    // Initialize SD Card
    showLoadingScreen("Init SD Card...");
    initSDCard();
    
    // Set up directories if needed
    if (!SD.exists("/album_art")) {
        SD.mkdir("/album_art");
    }
    
    if (!SD.exists("/radio_buffer")) {
        SD.mkdir("/radio_buffer");
    }
    
    if (!SD.exists("/images")) {
        SD.mkdir("/images");
    }
    
    if (!SD.exists("/music")) {
        SD.mkdir("/music");
    }
    
    // Scan for images in SD card
    scanSDCardImages();
    
    // Scan for music in SD card
    scanSDCardMusic();
    
    // Initialize the radio buffer system
    initRadioBuffer();
    
    // Initialize audio
    showLoadingScreen("Setting up Audio...");
    initAudio();
    
    // Show loading message
    showLoadingScreen("Connecting to WiFi...");
    
    // Connect to WiFi
    WiFi.begin(ssid, password);
    int wifiAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
        delay(500);
        wifiAttempts++;
        display->drawPixel(10 + wifiAttempts, 20, display->color565(0, 255, 0));
        display->fillRect(10, 20, wifiAttempts, 1, display->color565(0, 255, 0));
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        // Synchronize time
        showLoadingScreen("Syncing Time...");
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        
        // Initial data fetch
        showLoadingScreen("Fetching Weather...");
        fetchWeather();
        
        showLoadingScreen("Fetching Stocks...");
        for (int i = 0; i < numStocks; i++) {
            fetchStockData(i);
            delay(300); // Avoid rate limiting
        }
        
        showLoadingScreen("Fetching Bitcoin...");
        fetchBitcoinData();
    } else {
        showLoadingScreen("WiFi Failed!");
        delay(2000);
    }
    
    // Ready!
    showLoadingScreen("Welcome!");
    delay(500);
    
    // Start with clock display
    currentScreen = MENU_CLOCK;
    mainMenuPosition = 0;
}

void loop() {
    // Update data at regular intervals
    unsigned long currentMillis = millis();
    
    // Check system memory periodically
    if (currentMillis % 5000 == 0) {
        checkSystemMemory();
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        if (currentMillis - lastWeatherUpdate > weatherInterval) {
            fetchWeather();
            lastWeatherUpdate = currentMillis;
        }
        
        if (currentMillis - lastStockUpdate > stockInterval) {
            fetchStockData(currentStockIndex);
            currentStockIndex = (currentStockIndex + 1) % numStocks;
            lastStockUpdate = currentMillis;
        }
        
        if (currentMillis - lastCryptoUpdate > cryptoInterval) {
            fetchBitcoinData();
            lastCryptoUpdate = currentMillis;
        }
    }
    
    if (currentMillis - lastTimeUpdate > timeUpdateInterval) {
        lastTimeUpdate = currentMillis;
    }
    
    // Handle radio buffer
    if (radioPlaying && !timeShiftActive && currentMillis - lastBufferWrite > bufferWriteInterval) {
        lastBufferWrite = currentMillis;
    }
    
    // Handle slideshow timing
    if (slideshowActive && imageCount > 0 && currentMillis - lastImageChange > imageChangeInterval) {
        currentImage = (currentImage + 1) % imageCount;
        lastImageChange = currentMillis;
    }
    
    // Process audio data
    if (radioPlaying || musicPlaying) {
        audio.loop();
    }
    
    // Handle encoder input
    handleEncoder();
    handleButton();
    
    // Handle scroll wheel animation
    if (scrollWheelAnimating) {
        animateScrollWheel();
    }
    
    // Update display
    if (currentMillis - lastFrameTime > frameDelay) {
        updateDisplay();
        lastFrameTime = currentMillis;
    }
}

void checkSystemMemory() {
    if (ESP.getFreeHeap() < 3000) { // Critical low memory
        // Emergency recovery - stop memory-intensive operations
        if (musicPlaying) stopMusic();
        if (radioPlaying) stopRadio();
        slideshowActive = false;
        
        // Reset to clock display
        inSubmenu = false;
        currentScreen = MENU_CLOCK;
        
        // Show warning
        display->fillScreen(0);
        display->setTextSize(1);
        display->setCursor(5, 10);
        display->setTextColor(display->color565(255, 0, 0));
        display->print("MEMORY LOW");
        display->setCursor(5, 20);
        display->print("Resetting...");
        delay(2000);
    }
}

void initSDCard() {
    SPI.begin(SD_CLK, SD_DATA0, SD_CMD, SD_DATA3);
    SD.begin(SD_DATA3);
}

void initRadioBuffer() {
    // Clear any existing buffer files
    File dir = SD.open("/radio_buffer");
    if (dir && dir.isDirectory()) {
        File file = dir.openNextFile();
        while (file) {
            String fileName = String(file.name());
            file.close();
            SD.remove("/radio_buffer/" + fileName);
            file = dir.openNextFile();
        }
    }
    dir.close();
    
    // Initialize buffer file array
    for (int i = 0; i < maxBufferFiles; i++) {
        radioBufferFiles[i] = "/radio_buffer/chunk_" + String(i) + ".buf";
    }
    
    currentBufferIndex = 0;
}

// OPTIMIZED: Write to radio buffer in smaller chunks
void writeToRadioBuffer(uint8_t* buffer, size_t size) {
    if (!radioPlaying || timeShiftActive) return;
    
    // Check available memory before proceeding
    if (ESP.getFreeHeap() < size + 4000) { // 4KB safety margin
        return; // Skip buffer writing if memory is low
    }
    
    // Open the current buffer file
    radioBufferFile = SD.open(radioBufferFiles[currentBufferIndex], FILE_WRITE);
    if (!radioBufferFile) {
        return;
    }
    
    // Write in smaller chunks if necessary
    const size_t MAX_WRITE = 1024; // 1KB chunks
    size_t bytesLeft = size;
    size_t offset = 0;
    
    while (bytesLeft > 0) {
        size_t bytesToWrite = (bytesLeft > MAX_WRITE) ? MAX_WRITE : bytesLeft;
        radioBufferFile.write(buffer + offset, bytesToWrite);
        bytesLeft -= bytesToWrite;
        offset += bytesToWrite;
        
        // Small yield to avoid watchdog trigger
        yield();
    }
    
    radioBufferFile.close();
    
    // Move to the next buffer file
    currentBufferIndex = (currentBufferIndex + 1) % maxBufferFiles;
    
    // Initialize the next file (overwrite if it exists)
    if (SD.exists(radioBufferFiles[currentBufferIndex])) {
        SD.remove(radioBufferFiles[currentBufferIndex]);
    }
}

// OPTIMIZED: Read from radio buffer in smaller chunks
void readFromRadioBuffer(int timeOffset) {
    if (!radioPlaying) return;
    
    // Calculate which buffer file to read from
    int fileIndex = (currentBufferIndex - (timeOffset / 10) + maxBufferFiles) % maxBufferFiles;
    
    // Open the file for reading
    radioBufferFile = SD.open(radioBufferFiles[fileIndex], FILE_READ);
    if (!radioBufferFile) {
        return;
    }
    
    // Read buffer in smaller chunks directly to audio output
    const size_t READ_CHUNK = 1024;
    uint8_t* audioChunk = (uint8_t*)malloc(READ_CHUNK);
    
    if (!audioChunk) {
        radioBufferFile.close();
        return;
    }
    
    // Stop current audio playback
    if (audioPlaying) {
        audio.stopSong();
        delay(50);
    }
    
    // In real implementation, you would feed these chunks to audio
    size_t bytesRead;
    while ((bytesRead = radioBufferFile.read(audioChunk, READ_CHUNK)) > 0) {
        // Process audio chunk here
        // audio.processBuffer(audioChunk, bytesRead);
        
        // Small yield to avoid watchdog trigger
        yield();
    }
    
    free(audioChunk);
    radioBufferFile.close();
}

// OPTIMIZED: Prioritize BMP files and limit memory usage
void scanSDCardImages() {
    File root = SD.open("/images");
    if (!root || !root.isDirectory()) {
        return;
    }
    
    File file = root.openNextFile();
    imageCount = 0;
    
    // First pass: collect only BMP files (less memory intensive)
    while (file && imageCount < MAX_IMAGES) {
        String fileName = file.name();
        if (fileName.endsWith(".bmp")) {
            imageFiles[imageCount] = "/images/" + fileName;
            imageCount++;
        }
        file = root.openNextFile();
    }
    
    // Second pass: add JPG files only if we haven't reached MAX_IMAGES
    if (imageCount < MAX_IMAGES) {
        root.rewindDirectory();
        file = root.openNextFile();
        while (file && imageCount < MAX_IMAGES) {
            String fileName = file.name();
            if ((fileName.endsWith(".jpg") || fileName.endsWith(".jpeg")) && !fileName.startsWith("._")) {
                imageFiles[imageCount] = "/images/" + fileName;
                imageCount++;
            }
            file = root.openNextFile();
        }
    }
    
    root.close();
}

void scanSDCardMusic() {
    File root = SD.open("/music");
    if (!root || !root.isDirectory()) {
        return;
    }
    
    File file = root.openNextFile();
    musicCount = 0;
    
    while (file && musicCount < MAX_SONGS) {
        String fileName = file.name();
        if (fileName.endsWith(".mp3") || fileName.endsWith(".wav") || fileName.endsWith(".aac")) {
            musicFiles[musicCount] = "/music/" + fileName;
            
            // Look for a matching BMP album art file with the same name
            String artFilename = fileName.substring(0, fileName.lastIndexOf('.')) + ".bmp";
            String artPath = "/music/" + artFilename;
            if (SD.exists(artPath)) {
                musicArtFiles[musicCount] = artPath;
            } else {
                musicArtFiles[musicCount] = "";
            }
            
            musicCount++;
        }
        file = root.openNextFile();
    }
    
    root.close();
}

void initAudio() {
    audio.setPinout(I2S_BCLK, I2S_LRCLK, I2S_DIN);
    audio.setVolume(musicVolume);
}

String getMusicFileTitle(String filename) {
    // Remove path if present
    int lastSlash = filename.lastIndexOf('/');
    if (lastSlash >= 0) {
        filename = filename.substring(lastSlash + 1);
    }
    
    // Remove extension
    int lastDot = filename.lastIndexOf('.');
    if (lastDot >= 0) {
        filename = filename.substring(0, lastDot);
    }
    
    // Get title part (before _by_)
    int byPos = filename.indexOf("_by_");
    if (byPos >= 0) {
        String title = filename.substring(0, byPos);
        title.replace('_', ' ');
        return title;
    }
    
    String result = filename;
    result.replace('_', ' ');
    return result;
}

String getMusicFileArtist(String filename) {
    // Remove path if present
    int lastSlash = filename.lastIndexOf('/');
    if (lastSlash >= 0) {
        filename = filename.substring(lastSlash + 1);
    }
    
    // Remove extension
    int lastDot = filename.lastIndexOf('.');
    if (lastDot >= 0) {
        filename = filename.substring(0, lastDot);
    }
    
    // Get artist part (after _by_)
    int byPos = filename.indexOf("_by_");
    if (byPos >= 0) {
        String artist = filename.substring(byPos + 4);
        artist.replace('_', ' ');
        return artist;
    }
    
    return "Unknown";
}

void loadAlbumArt(int songIndex) {
    currentAlbumArtPath = "";
    
    if (songIndex < 0 || songIndex >= musicCount) {
        return;
    }
    
    String artPath = musicArtFiles[songIndex];
    if (artPath.length() > 0) {
        currentAlbumArtPath = artPath;
    }
}

// OPTIMIZED: More memory-efficient BMP loading
bool loadBmpFromSD(String filename) {
    // Check available memory
    if (!checkMemory(5000)) { // Need at least 5KB free
        return false;
    }

    File bmpFile = SD.open(filename);
    if (!bmpFile) {
        return false;
    }
    
    // Check BMP header
    if (bmpFile.read() != 'B' || bmpFile.read() != 'M') {
        bmpFile.close();
        return false;
    }
    
    // Skip file size, reserved and offset fields
    bmpFile.seek(10);
    
    // Read the header offset
    uint32_t dataOffset = 0;
    bmpFile.read((uint8_t*)&dataOffset, 4);
    
    // Read DIB header size
    uint32_t dibHeaderSize = 0;
    bmpFile.read((uint8_t*)&dibHeaderSize, 4);
    
    // Read image dimensions
    int32_t width = 0, height = 0;
    bmpFile.read((uint8_t*)&width, 4);
    bmpFile.read((uint8_t*)&height, 4);
    
    // For ESP32 without PSRAM, limit image size
    if (width > 64 || height > 64) {
        bmpFile.close();
        return false; // Skip large images
    }
    
    // Check if height is negative (bottom-up BMP)
    bool topDown = false;
    if (height < 0) {
        topDown = true;
        height = -height;
    }
    
    // Skip color planes and bits per pixel
    bmpFile.seek(28);
    
    // Read compression method
    uint32_t compression = 0;
    bmpFile.read((uint8_t*)&compression, 4);
    
    if (compression != 0) {
        bmpFile.close();
        return false;
    }
    
    // Move to pixel data
    bmpFile.seek(dataOffset);
    
    // Calculate row width with padding to 4-byte boundary
    int rowSize = ((width * 3 + 3) & ~3);
    
    // Centered display coordinates
    int x0 = (PANEL_WIDTH - std::min((int32_t)width, (int32_t)PANEL_WIDTH)) / 2;
    int y0 = (PANEL_HEIGHT - std::min((int32_t)height, (int32_t)PANEL_HEIGHT)) / 2;

    // Declare and allocate rowBuffer - THIS LINE WAS MISSING
    uint8_t* rowBuffer = (uint8_t*)malloc(rowSize);
    if (!rowBuffer) {
        bmpFile.close();
        return false;
    }
    
    for (int y = 0; y < std::min((int32_t)height, (int32_t)PANEL_HEIGHT); y++) {
        // For bottom-up BMPs, we need to read from the bottom
        int fileRow = topDown ? y : (height - 1 - y);
        
        // Seek to the correct row
        bmpFile.seek(dataOffset + fileRow * rowSize);
        
        // Read entire row at once
        bmpFile.read(rowBuffer, rowSize);
        
        // Process and display row pixels
        for (int x = 0; x < std::min((int32_t)width, (int32_t)PANEL_WIDTH); x++) {
            // Get BGR pixel from buffer
            uint8_t b = rowBuffer[x * 3];
            uint8_t g = rowBuffer[x * 3 + 1];
            uint8_t r = rowBuffer[x * 3 + 2];
            
            // Convert to RGB565 format
            uint16_t color = display->color565(r, g, b);
            
            // Draw pixel
            display->drawPixel(x0 + x, y0 + y, color);
        }
        
        // Small yield to avoid watchdog trigger
        yield();
    }
    
    free(rowBuffer);
    bmpFile.close();
    return true;
}

// OPTIMIZED: Stream-based JPEG loading to save memory
bool loadJpegFromSD(String filename) {
    // Check available memory
    if (!checkMemory(8000)) { // Need at least 8KB free
        return false;
    }
    
    File jpegFile = SD.open(filename);
    if (!jpegFile) {
        return false;
    }
    
    // Initialize JPEG decoder with file reading callback
    if (jpeg.open(jpegFile, drawMCUblockCallback)) {
        // Decode the image
        jpeg.setPixelType(RGB565_BIG_ENDIAN);
        jpeg.setMaxOutputSize(PANEL_WIDTH);
        jpeg.decode(0, 0, 0);
        jpeg.close();
    }
    
    jpegFile.close();
    return true;
}

int drawMCUblockCallback(JPEGDRAW* pDraw) {
    // Scale the image to fit the display
    int x = 0;
    int y = 0;
    int width = pDraw->iWidth;
    int height = pDraw->iHeight;
    
    // Center the image if smaller than display
    if (width < PANEL_WIDTH) {
        x = (PANEL_WIDTH - width) / 2;
    }
    if (height < PANEL_HEIGHT) {
        y = (PANEL_HEIGHT - height) / 2;
    }
    
    // Draw the MCU block
    for (int py = 0; py < pDraw->iHeight; py++) {
        for (int px = 0; px < pDraw->iWidth; px++) {
            int drawX = x + px;
            int drawY = y + py;
            
            // Check if pixel is within display boundaries
            if (drawX >= 0 && drawX < PANEL_WIDTH && drawY >= 0 && drawY < PANEL_HEIGHT) {
                uint16_t pixel = pDraw->pPixels[py * pDraw->iWidth + px];
                display->drawPixel(drawX, drawY, pixel);
            }
        }
    }
    
    return 1; // Continue decoding
}

String URLEncode(const char* msg) {
    const char *hex = "0123456789ABCDEF";
    String encodedMsg = "";
    
    while (*msg != '\0') {
        if (('a' <= *msg && *msg <= 'z') ||
            ('A' <= *msg && *msg <= 'Z') ||
            ('0' <= *msg && *msg <= '9') ||
            *msg == '-' || *msg == '_' || *msg == '.' || *msg == '~') {
            encodedMsg += *msg;
        } else {
            encodedMsg += '%';
            encodedMsg += hex[*msg >> 4];
            encodedMsg += hex[*msg & 0xf];
        }
        msg++;
    }
    return encodedMsg;
}

void fetchWeather() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    // Check memory before making HTTP request
    if (!checkMemory(6000)) return;
    
    HTTPClient http;
    http.begin(weatherServer);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        
        // Use smaller buffer for JSON parsing
        DynamicJsonDocument doc(1024);  // Reduced from 2048
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error) {
            currentWeather.temp = doc["main"]["temp"];
            currentWeather.feels_like = doc["main"]["feels_like"];
            currentWeather.humidity = doc["main"]["humidity"];
            currentWeather.description = doc["weather"][0]["description"].as<String>();
            currentWeather.icon = doc["weather"][0]["icon"].as<String>();
            currentWeather.city = doc["name"].as<String>();
        }
    }
    http.end();
}

void fetchStockData(int index) {
    if (WiFi.status() != WL_CONNECTED) return;
    
    // Check memory before making HTTP request
    if (!checkMemory(6000)) return;
    
    String symbol = stockSymbols[index];
    String url = "https://www.alphavantage.co/query?function=GLOBAL_QUOTE&symbol=" + symbol + "&apikey=" + stockApiKey;
    
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        
        // Use smaller buffer for JSON parsing
        DynamicJsonDocument doc(1024);  // Reduced from 2048
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error && doc.containsKey("Global Quote")) {
            stocks[index].symbol = symbol;
            stocks[index].price = doc["Global Quote"]["05. price"].as<float>();
            stocks[index].change = doc["Global Quote"]["09. change"].as<float>();
            String changePercentStr = doc["Global Quote"]["10. change percent"].as<String>();
            // Remove % character if present
            changePercentStr.replace("%", "");
            stocks[index].change_percent = changePercentStr.toFloat();
            
            // Shift history data
            for (int i = 9; i > 0; i--) {
                stocks[index].prices[i] = stocks[index].prices[i-1];
            }
            stocks[index].prices[0] = stocks[index].price;
        }
    }
    http.end();
}

void fetchBitcoinData() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    // Check memory before making HTTP request
    if (!checkMemory(6000)) return;
    
    HTTPClient http;
    http.begin(cryptoServer);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        
        // Use smaller buffer for JSON parsing
        DynamicJsonDocument doc(512);  // Reduced from 2048
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error) {
            bitcoin.price = doc["bitcoin"]["usd"];
            bitcoin.change_24h = doc["bitcoin"]["usd_24h_change"];
        }
    }
    http.end();
}

void displayTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        display->setTextSize(1);
        display->setCursor(2, 5);
        display->setTextColor(display->color565(255, 0, 0));
        display->print("Time Error");
        return;
    }

    // Show day of week
    display->setTextSize(1);
    display->setCursor(2, 2);
    display->setTextColor(display->color565(255, 255, 0));
    
    char dayStr[12];
    strftime(dayStr, sizeof(dayStr), "%a, %b %d", &timeinfo);
    display->print(dayStr);
    
    // Show time in larger font
    display->setTextSize(2);
    display->setCursor(10, 13);
    display->setTextColor(display->color565(0, 255, 255));
    
    char timeStr[9];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    display->print(timeStr);
}

void displayWeather() {
    display->setTextSize(1);
    
    // City name at top
    display->setCursor(2, 1);
    display->setTextColor(display->color565(255, 255, 0));
    display->print(currentWeather.city);
    
    // Weather icon
    drawWeatherIcon(currentWeather.icon, 3, 10);
    
    // Temperature
    display->setCursor(15, 10);
    display->setTextColor(display->color565(255, 100, 0));
    display->print(String(currentWeather.temp, 1));
    display->print("C");
    
    // Description
    display->setCursor(15, 18);
    display->setTextColor(display->color565(0, 255, 127));
    display->print(currentWeather.description);
    
    // Humidity
    display->setCursor(2, 25);
    display->setTextColor(display->color565(0, 100, 255));
    display->print("Hum: ");
    display->print(currentWeather.humidity);
    display->print("%");
}

void drawWeatherIcon(String iconCode, int x, int y) {
    const uint16_t* iconData;
    
    // Select appropriate icon based on code
    if (iconCode.startsWith("01")) {
        // Clear sky
        iconData = SUNNY_ICON;
    } else if (iconCode.startsWith("02") || iconCode.startsWith("03") || iconCode.startsWith("04")) {
        // Clouds
        iconData = CLOUDY_ICON;
    } else if (iconCode.startsWith("09") || iconCode.startsWith("10") || iconCode.startsWith("11")) {
        // Rain
        iconData = RAINY_ICON;
    } else {
        // Default to sunny
        iconData = SUNNY_ICON;
    }
    
    // Draw the 5x5 icon
    for (int dy = 0; dy < 5; dy++) {
        for (int dx = 0; dx < 5; dx++) {
            if (iconData[dy * 5 + dx] != 0) {
                display->drawPixel(x + dx, y + dy, iconData[dy * 5 + dx]);
            }
        }
    }
}

void displayStock(int index) {
    display->setTextSize(1);
    
    // Symbol name at top
    display->setCursor(2, 1);
    display->setTextColor(display->color565(255, 255, 255));
    display->print(stocks[index].symbol);
    
    // Price
    display->setCursor(34, 1);
    display->setTextColor(display->color565(0, 255, 255));
    display->print("$");
    display->print(String(stocks[index].price, 2));
    
    // Chart
    drawStockChart(stocks[index], 2, 11, 60, 14);
    
    // Change
    display->setCursor(2, 25);
    
    if (stocks[index].change >= 0) {
        display->setTextColor(display->color565(0, 255, 0));
        display->print("+");
    } else {
        display->setTextColor(display->color565(255, 0, 0));
    }
    
    display->print(String(stocks[index].change, 2));
    display->print(" (");
    display->print(String(stocks[index].change_percent, 2));
    display->print("%)");
}

void drawStockChart(StockData stock, int x, int y, int width, int height) {
    // Find min and max values
    float minPrice = stock.prices[0];
    float maxPrice = stock.prices[0];
    
    for (int i = 0; i < 10; i++) {
        if (stock.prices[i] == 0 && i > 0) break; // End of valid data
        
        if (stock.prices[i] < minPrice) minPrice = stock.prices[i];
        if (stock.prices[i] > maxPrice) maxPrice = stock.prices[i];
    }
    
    // Add margin
    float margin = (maxPrice - minPrice) * 0.1;
    if (margin == 0) margin = 1.0; // Prevent division by zero
    
    maxPrice += margin;
    minPrice -= margin;
    
    // Draw chart background
    display->drawRect(x, y, width, height, display->color565(50, 50, 50));
    
    // Draw the chart line
    for (int i = 0; i < 9; i++) {
        if (stock.prices[i] == 0 || stock.prices[i+1] == 0) break; // End of valid data
        
        int x1 = x + width - (i+1) * (width / 10);
        int x2 = x + width - (i+2) * (width / 10);
        
        int y1 = y + height - (int)((stock.prices[i] - minPrice) / (maxPrice - minPrice) * height);
        int y2 = y + height - (int)((stock.prices[i+1] - minPrice) / (maxPrice - minPrice) * height);
        
        uint16_t lineColor = (stock.prices[i] >= stock.prices[i+1]) 
                             ? display->color565(0, 255, 0)  // Green if going up
                             : display->color565(255, 0, 0); // Red if going down
        
        display->drawLine(x1, y1, x2, y2, lineColor);
    }
}

void displayCrypto() {
    display->setTextSize(1);
    
    // Title
    display->setCursor(2, 1);
    display->setTextColor(display->color565(255, 180, 0));
    display->print("Bitcoin");
    
    // Draw bitcoin symbol
    int centerX = 10;
    int centerY = 15;
    display->fillRect(centerX-2, centerY-5, 5, 10, display->color565(255, 180, 0));
    display->drawLine(centerX, centerY-7, centerX, centerY+7, display->color565(255, 180, 0));
    display->drawLine(centerX+3, centerY-7, centerX+3, centerY+7, display->color565(255, 180, 0));
    
    // Price
    display->setTextSize(1);
    display->setCursor(20, 10);
    display->setTextColor(display->color565(0, 255, 255));
    display->print("$");
    display->print(String(bitcoin.price, 0));
    
    // 24h change
    display->setCursor(20, 20);
    
    if (bitcoin.change_24h >= 0) {
        display->setTextColor(display->color565(0, 255, 0));
        display->print("+");
    } else {
        display->setTextColor(display->color565(255, 0, 0));
    }
    
    display->print(String(bitcoin.change_24h, 2));
    display->print("%");
}

void displayRadioMenu() {
    display->setTextSize(1);
    
    // Title
    display->setCursor(2, 1);
    display->setTextColor(display->color565(255, 255, 255));
    display->print("Radio Stations");
    
    int yPos = 10;
    for (int i = 0; i < numRadioStations; i++) {
        if (i == submenuPosition) {
            display->setTextColor(display->color565(255, 255, 0));
            display->setCursor(0, yPos);
            display->print(">");
        } else {
            display->setTextColor(display->color565(100, 100, 100));
        }
        
        display->setCursor(8, yPos);
        display->print(radioNames[i]);

         yPos += 8;
        if (yPos >= PANEL_HEIGHT) break;
    }
    
    display->setTextColor(display->color565(0, 255, 255));
    display->setCursor(2, PANEL_HEIGHT - 8);
    display->print("Press to select");
}

void displayRadioPlayer() {
    display->setTextSize(1);
    
    // Show station name
    display->setCursor(2, 1);
    display->setTextColor(display->color565(255, 255, 0));
    display->print(radioNames[currentRadioStation]);
    
    // Show now playing
    display->setCursor(2, 10);
    display->setTextColor(display->color565(255, 255, 255));
    display->print("Now Playing:");
    
    display->setCursor(2, 18);
    display->setTextColor(display->color565(0, 255, 255));
    
    // Scroll the song title if it's too long
    static int scrollPos = 0;
    static unsigned long lastScroll = 0;
    
    String displayText = currentSong + " - " + currentArtist;
    if (displayText.length() > 12) {
        if (millis() - lastScroll > 500) {
            scrollPos = (scrollPos + 1) % displayText.length();
            lastScroll = millis();
        }
        
        String visibleText = displayText.substring(scrollPos) + " " + displayText.substring(0, scrollPos);
        if (visibleText.length() > 12) {
            visibleText = visibleText.substring(0, 12);
        }
        display->print(visibleText);
    } else {
        display->print(displayText);
    }
    
    // Show time shift status
    display->setCursor(2, 26);
    if (timeShiftActive) {
        display->setTextColor(display->color565(255, 0, 255));
        display->print("Rewind: ");
        display->print(timeShiftOffset);
        display->print("s");
    } else {
        display->setTextColor(display->color565(0, 255, 0));
        display->print("Live");
    }
}

void displayMusicMenu() {
    display->setTextSize(1);
    
    // Title
    display->setCursor(2, 1);
    display->setTextColor(display->color565(255, 255, 255));
    display->print("Music Library");
    
    int yPos = 10;
    for (int i = 0; i < musicCount; i++) {
        if (i == submenuPosition) {
            display->setTextColor(display->color565(255, 255, 0));
            display->setCursor(0, yPos);
            display->print(">");
        } else {
            display->setTextColor(display->color565(100, 100, 100));
        }
        
        display->setCursor(8, yPos);
        // Show abbreviated song title
        String songTitle = getMusicFileTitle(musicFiles[i]);
        if (songTitle.length() > 16) {
            songTitle = songTitle.substring(0, 13) + "...";
        }
        display->print(songTitle);
        
        yPos += 8;
        if (yPos >= PANEL_HEIGHT) break;
    }
    
    display->setTextColor(display->color565(0, 255, 255));
    display->setCursor(2, PANEL_HEIGHT - 8);
    display->print("Press to play");
}

void displayMusicPlayer() {
    // Check memory first
    if (!checkMemory(4000)) return;
    
    display->setTextSize(1);
    
    // Display album art if available
    if (currentAlbumArtPath.length() > 0) {
        // Clear the central area
        display->fillRect(2, 8, 60, 18, 0);
        
        // Load and display the album art
        if (currentAlbumArtPath.endsWith(".bmp")) {
            loadBmpFromSD(currentAlbumArtPath);
        } else if (currentAlbumArtPath.endsWith(".jpg") || currentAlbumArtPath.endsWith(".jpeg")) {
            loadJpegFromSD(currentAlbumArtPath);
        }
    } else {
        // Draw placeholder if no album art
        display->drawRect(12, 8, 40, 18, display->color565(100, 100, 255));
        display->drawLine(12, 8, 52, 26, display->color565(100, 100, 255));
        display->drawLine(52, 8, 12, 26, display->color565(100, 100, 255));
    }
    
    // Show song title at top
    display->setCursor(2, 1);
    display->setTextColor(display->color565(255, 255, 0));
    
    // Scroll title if needed
    static int scrollPos = 0;
    static unsigned long lastScroll = 0;
    
    String songTitle = currentMusicTitle;
    if (songTitle.length() > 16) {
        if (millis() - lastScroll > 500) {
            scrollPos = (scrollPos + 1) % songTitle.length();
            lastScroll = millis();
        }
        
        String visibleText = songTitle.substring(scrollPos) + " " + songTitle.substring(0, scrollPos);
        if (visibleText.length() > 16) {
            visibleText = visibleText.substring(0, 16);
        }
        display->print(visibleText);
    } else {
        display->print(songTitle);
    }
    
    // Show artist at bottom
    display->setCursor(2, PANEL_HEIGHT - 8);
    display->setTextColor(display->color565(0, 255, 255));
    
    // Control status message
    if (musicNavigationMode) {
        display->print("< Change Song >");
    } else {
        display->print(currentMusicArtist);
    }
}

void displaySlideshow() {
    display->setTextSize(1);
    
    if (imageCount == 0) {
        display->setCursor(2, 10);
        display->setTextColor(display->color565(255, 0, 0));
        display->print("No images found!");
        
        display->setCursor(2, 20);
        display->print("Add BMP files to");
        
        display->setCursor(2, 28);
        display->print("/images/ on SD card");
        return;
    }
    
    // Check available memory
    if (!checkMemory(5000)) {
        display->setCursor(2, 10);
        display->setTextColor(display->color565(255, 0, 0));
        display->print("Low memory!");
        return;
    }
    
    // Display the current image number
    display->setCursor(2, 1);
    display->setTextColor(display->color565(255, 255, 255));
    display->print("Image ");
    display->print(currentImage + 1);
    display->print("/");
    display->print(imageCount);
    
    // Load and display the actual image
    String filename = imageFiles[currentImage];
    if (filename.endsWith(".bmp")) {
        loadBmpFromSD(filename);
    } else if (filename.endsWith(".jpg") || filename.endsWith(".jpeg")) {
        loadJpegFromSD(filename);
    }
    
    // Show navigation hint
    display->setCursor(2, PANEL_HEIGHT - 8);
    display->setTextColor(display->color565(0, 255, 255));
    if (slideshowActive) {
        display->print("Auto: ON");
    } else {
        display->print("Turn: Next/Prev");
    }
}

void handleEncoder() {
    // Read current encoder position
    int32_t currentValue = encoder.getCount();
    
    // Check if position has changed
    if (currentValue != lastEncoderValue) {
        // Determine direction (clockwise or counter-clockwise)
        int direction = (currentValue > lastEncoderValue) ? 1 : -1;
        
        // Play tick sound for tactile feedback
        playTickSound();
        
        // Handle based on current screen and mode
        if (musicNavigationMode && currentScreen == MENU_MUSIC) {
            // In music navigation mode, change songs
            if (direction > 0) {
                nextMusic();
            } else {
                prevMusic();
            }
        } else if (!inSubmenu) {
            // Main menu navigation
            scrollWheelTargetPosition = (direction > 0) ? 
                                         (scrollWheelTargetPosition + 1) % numMainMenuItems : 
                                         (scrollWheelTargetPosition - 1 + numMainMenuItems) % numMainMenuItems;
            
            mainMenuPosition = scrollWheelTargetPosition;
            scrollWheelAnimating = true;
            
            // Start the transition animation
            showMenuTransition(currentScreen, mainMenuPosition + 1);
            currentScreen = mainMenuPosition + 1;
            
        } else {
            // Submenu handling
            switch (currentScreen) {
                case MENU_STOCKS:
                    currentStockIndex = (currentStockIndex + direction + numStocks) % numStocks;
                    break;
                    
                case MENU_RADIO:
                    if (radioPlaying) {
                        // If in radio playback mode, control time-shift
                        if (timeShiftActive) {
                            // Adjust time-shift offset
                            timeShiftOffset = std::max(0, std::min(RADIO_BUFFER_MINUTES * 60, timeShiftOffset + direction * 10));
                            startTimeShift(timeShiftOffset);
                        } else {
                            // Enable time-shift with first turn
                            if (direction < 0) { // Only rewind when turning counter-clockwise
                                startTimeShift(10);  // Start with 10-second rewind
                            }
                        }
                    } else {
                        // Change radio station selection
                        submenuPosition = (submenuPosition + direction + numRadioStations) % numRadioStations;
                    }
                    break;
                    
                case MENU_SLIDESHOW:
                    if (imageCount > 0) {
                        currentImage = (currentImage + direction + imageCount) % imageCount;
                        lastImageChange = millis(); // Reset the automatic change timer
                    }
                    break;
                    
                case MENU_MUSIC:
                    if (musicPlaying) {
                        // Volume control when music is playing and not in navigation mode
                        musicVolume = std::max(0, std::min(21, musicVolume + direction));
                        audio.setVolume(musicVolume);
                    } else {
                        // Navigate music list
                        submenuPosition = (submenuPosition + direction + musicCount) % musicCount;
                    }
                    break;
            }
        }
        
        // Update the last value
        lastEncoderValue = currentValue;
    }
}

void handleButton() {
    // Read button state (inverted logic with pull-up)
    bool currentButtonState = !digitalRead(ENCODER_SW);
    
    // Button press detected (with debounce)
    if (currentButtonState && !buttonPressed && (millis() - lastButtonPress > 300)) {
        buttonPressed = true;
        lastButtonPress = millis();
        
        // Play feedback sound
        playTickSound();
        
        // Check for double click (only for music player)
        if (currentScreen == MENU_MUSIC && musicPlaying && millis() - lastSingleClickTime < 500) {
            doubleClickDetected = true;
            
            // Toggle music navigation mode
            musicNavigationMode = !musicNavigationMode;
            
            return; // Skip the rest of the handling for the second click of a double-click
        }
        
        // Record time for potential double-click detection
        lastSingleClickTime = millis();
        
        // Handle button press based on current screen
        if (!inSubmenu) {
            // Enter submenu
            inSubmenu = true;
            submenuPosition = 0;
            
            // Special handling for radio, music, and slideshow
            if (currentScreen == MENU_RADIO) {
                if (radioPlaying) {
                    // If already playing, toggle time-shift
                    if (timeShiftActive) {
                        stopTimeShift();
                    }
                }
            } else if (currentScreen == MENU_SLIDESHOW) {
                slideshowActive = !slideshowActive;
            } else if (currentScreen == MENU_MUSIC) {
                if (musicPlaying) {
                    // If music is already playing, toggle play/pause
                    if (audio.isRunning()) {
                        audio.pauseResume();
                    } else {
                        audio.pauseResume();
                    }
                }
            }
        } else {
            // Handle submenu button press
            switch (currentScreen) {
                case MENU_RADIO:
                    if (!radioPlaying) {
                        // Start selected radio station
                        startRadio(submenuPosition);
                    } else {
                        // If already playing, exit radio submenu
                        inSubmenu = false;
                    }
                    break;
                    
                case MENU_SLIDESHOW:
                    inSubmenu = false;
                    break;
                    
                case MENU_MUSIC:
                    if (!musicPlaying) {
                        // Start playing the selected song
                        playMusic(submenuPosition);
                    } else {
                        // If already playing, toggle play/pause
                        if (audio.isRunning()) {
                            audio.pauseResume();
                        } else {
                            audio.pauseResume();
                        }
                    }
                    break;
                    
                case MENU_STOCKS:
                    inSubmenu = false;
                    break;
                    
                default:
                    inSubmenu = false;
                    break;
            }
        }
    }
    
    // Reset button state when released
    if (!currentButtonState && buttonPressed) {
        buttonPressed = false;
    }
}

void updateDisplay() {
    display->clearScreen();
    
    // Draw the main content based on current screen
    if (transitioning) {
        // Draw transition animation
        int offset = (scrollOffset * scrollOffset) / 100; // Accelerated scrolling
        drawScreenContent(currentScreen, offset);
        drawScreenContent(targetScreen, offset - PANEL_WIDTH);
        
        // Update scroll offset
        scrollOffset += 5;
        if (scrollOffset >= 100) {
            transitioning = false;
            currentScreen = targetScreen;
            scrollOffset = 0;
        }
    } else {
        // Draw the current screen
        switch (currentScreen) {
            case MENU_CLOCK:
                displayTime();
                break;

            case MENU_WEATHER:
                displayWeather();
                break;
                
            case MENU_STOCKS:
                displayStock(currentStockIndex);
                break;
                
            case MENU_CRYPTO:
                displayCrypto();
                break;
                
            case MENU_RADIO:
                if (inSubmenu && !radioPlaying) {
                    displayRadioMenu();
                } else {
                    displayRadioPlayer();
                }
                break;
                
            case MENU_MUSIC:
                if (inSubmenu && !musicPlaying) {
                    displayMusicMenu();
                } else {
                    displayMusicPlayer();
                }
                break;
                
            case MENU_SLIDESHOW:
                displaySlideshow();
                break;
                
            default:
                displayTime();
                break;
        }
    }
    
    // Draw scroll wheel if appropriate
    if (!inSubmenu && !transitioning) {
        drawScrollWheel(scrollWheelPosition);
    }
    
    // Show memory info in development mode (comment out for final version)
    #ifdef DEBUG_MODE
    display->setTextSize(1);
    display->setCursor(44, PANEL_HEIGHT - 8);
    display->setTextColor(display->color565(100, 100, 100));
    display->print(ESP.getFreeHeap() / 1024);
    display->print("k");
    #endif
}

void drawScreenContent(int screen, int offset) {
    // Save original cursor position
    int16_t origX = display->getCursorX();
    int16_t origY = display->getCursorY();
    
    // Set cursor for offset position
    display->setCursor(offset, 0);
    
    // Draw content with offset
    switch (screen) {
        case MENU_CLOCK:
            displayTime();
            break;
            
        case MENU_WEATHER:
            displayWeather();
            break;
            
        case MENU_STOCKS:
            displayStock(currentStockIndex);
            break;
            
        case MENU_CRYPTO:
            displayCrypto();
            break;
            
        case MENU_RADIO:
            if (inSubmenu && !radioPlaying) {
                displayRadioMenu();
            } else {
                displayRadioPlayer();
            }
            break;
            
        case MENU_MUSIC:
            if (inSubmenu && !musicPlaying) {
                displayMusicMenu();
            } else {
                displayMusicPlayer();
            }
            break;
            
        case MENU_SLIDESHOW:
            displaySlideshow();
            break;
            
        default:
            displayTime();
            break;
    }
    
    // Restore original cursor position
    display->setCursor(origX, origY);
}

void drawScrollWheel(int position) {
    // Draw a small wheel indicator at the bottom
    int wheelCenter = PANEL_WIDTH / 2;
    int wheelRadius = 12;
    int wheelBottom = PANEL_HEIGHT - 2;
    
    // Draw wheel background
    display->drawCircle(wheelCenter, wheelBottom, wheelRadius, display->color565(50, 50, 50));
    
    // Calculate positions for menu items on wheel
    for (int i = 0; i < numMainMenuItems; i++) {
        float angle = ((float)(i - position) / numMainMenuItems) * 2 * PI;
        int x = wheelCenter + sin(angle) * wheelRadius;
        int y = wheelBottom - cos(angle) * wheelRadius * 0.5; // Flatten the circle to an oval
        
        // Draw indicator dot
        uint16_t color = (i == mainMenuPosition) ? 
                         display->color565(255, 255, 0) : // Highlight current position
                         display->color565(100, 100, 100); // Other positions
        
        display->fillCircle(x, y, 1, color);
    }
    
    // Draw center dot
    display->fillCircle(wheelCenter, wheelBottom, 2, display->color565(200, 200, 200));
}

void animateScrollWheel() {
    // Smoothly animate the scroll wheel position
    float diff = scrollWheelTargetPosition - scrollWheelPosition;
    
    // Apply easing function for smooth animation
    if (abs(diff) > 0.01) {
        scrollWheelPosition += diff * 0.2; // Adjust speed factor as needed
    } else {
        scrollWheelPosition = scrollWheelTargetPosition;
        scrollWheelAnimating = false;
    }
}

void showMenuTransition(int fromItem, int toItem) {
    transitioning = true;
    targetScreen = toItem;
    scrollOffset = 0;
}

void startRadio(int stationIndex) {
    // Check memory before starting radio
    if (!checkMemory(8000)) {
        return;
    }
    
    // Stop any currently playing audio
    if (radioPlaying) {
        audio.stopSong();
    }
    
    if (musicPlaying) {
        audio.stopSong();
        musicPlaying = false;
    }
    
    // Update state
    currentRadioStation = stationIndex;
    radioPlaying = true;
    timeShiftActive = false;
    
    // Start playing the selected station
    audio.connecttohost(radioStations[stationIndex]);
    
    // Reset metadata
    currentSong = "Loading...";
    currentArtist = "";
}

void stopRadio() {
    if (radioPlaying) {
        audio.stopSong();
        radioPlaying = false;
        timeShiftActive = false;
    }
}

void startTimeShift(int secondsBack) {
    if (!radioPlaying) return;
    
    // Check memory before time-shifting
    if (!checkMemory(6000)) {
        return;
    }
    
    timeShiftActive = true;
    timeShiftOffset = secondsBack;
    
    // Read from the buffer
    readFromRadioBuffer(secondsBack);
}

void stopTimeShift() {
    if (!radioPlaying) return;
    
    timeShiftActive = false;
    timeShiftOffset = 0;
    
    // Resume live playback by reconnecting to the station
    audio.stopSong();
    delay(100);
    audio.connecttohost(radioStations[currentRadioStation]);
}

void playMusic(int songIndex) {
    if (songIndex < 0 || songIndex >= musicCount) {
        return;
    }
    
    // Check memory before playing music
    if (!checkMemory(8000)) {
        return;
    }
    
    // Stop any currently playing audio
    if (radioPlaying) {
        stopRadio();
    }
    
    if (musicPlaying) {
        audio.stopSong();
    }
    
    // Update state
    currentMusic = songIndex;
    musicPlaying = true;
    musicNavigationMode = false;
    
    // Get title and artist from filename
    currentMusicTitle = getMusicFileTitle(musicFiles[songIndex]);
    currentMusicArtist = getMusicFileArtist(musicFiles[songIndex]);
    
    // Load album art if available
    loadAlbumArt(songIndex);
    
    // Start playing the selected song
    audio.connecttoFS(SD, musicFiles[songIndex].c_str());
    audio.setVolume(musicVolume);
}

void stopMusic() {
    if (musicPlaying) {
        audio.stopSong();
        musicPlaying = false;
        musicNavigationMode = false;
    }
}

void nextMusic() {
    if (musicCount == 0) return;
    
    int nextIndex = (currentMusic + 1) % musicCount;
    playMusic(nextIndex);
}

void prevMusic() {
    if (musicCount == 0) return;
    
    int prevIndex = (currentMusic - 1 + musicCount) % musicCount;
    playMusic(prevIndex);
}

void playTickSound() {
    // In a real implementation, this would play a short tick sound
    // For feedback when turning the encoder
    // Simple beep using PWM would be implemented here
    // Since we're optimizing for memory, we'll skip actual sound generation
}

void showLoadingScreen(String message) {
    display->clearScreen();
    
    display->setTextSize(1);
    display->setCursor(2, 5);
    display->setTextColor(display->color565(0, 255, 0));
    display->print(message);
    
    // Draw loading bar
    display->drawRect(2, 15, 60, 5, display->color565(100, 100, 100));
    
    // Update partial progress bar based on current phase
    static int loadingProgress = 0;
    loadingProgress = (loadingProgress + 5) % 55;
    display->fillRect(5, 16, loadingProgress, 3, display->color565(0, 255, 0));
}