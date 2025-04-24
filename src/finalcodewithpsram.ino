/*
 * Enhanced ESP32-S3 N8R8 Display and Media Center
 * Features:
 * - Rotary encoder navigation with scrollwheel-like interface
 * - Multiple screens (Time/Date, Weather, Stocks, Bitcoin)
 * - Radio playback with time-shift capability using SD card buffer
 * - Bluetooth audio receiver with album art display from online API
 * - SD card image slideshow with proper orientation
 * - Optimized for ESP32-S3 with 8MB RAM and 8MB PSRAM
 * - Extensive serial monitoring for debugging
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
#include <BluetoothA2DPSink.h>
#include <Audio.h>
#include <JPEGDecoder.h>  // For album art and slideshow
#include <JPEGDEC.h>      // JPEG decoder for efficiently displaying album art
#include <esp_heap_caps.h> // For PSRAM allocation

// Debug flag - set to true to enable detailed serial output
#define DEBUG_SERIAL true

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
#define E_PIN      14    // Address line E (Only used in 64-row panels)
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
// CD (Card Detect) — Not Connected

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
const char* lastFmApiKey = "4b1760bb21f92ed72c6e38a55c1cfc7f"; // For album art

// API Endpoints
const char* weatherServer = "http://api.openweathermap.org/data/2.5/weather?id=1273294&units=metric&appid=9ad9d95f5fe5ed2ab0ac1c0c6f391dcc";
const String stockSymbols[] = {"TSLA", "AAPL", "MSFT", "GOOGL", "AMZN"};
int currentStockIndex = 0;
const int numStocks = 5;

// Bitcoin API
const char* cryptoServer = "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd&include_24hr_change=true";

// Radio stations in Delhi
const char* radioStations[] = {
    "http://peridot.streamguys.com:7150/Mirchi",   // Radio Mirchi 98.3 FM Delhi
    "http://prclive1.listenon.in:9888/",           // Radio City 91.1 FM Delhi
    "http://sc-bb.1.fm:8017/",                     // Bombay Beats Bollywood
    "http://airhlspush.pc.cdn.bitgravity.com/airlive/hlspush/hlsmaster/fmaircr_64k.m3u8", // AIR FM Rainbow Delhi
    "http://airhlspush.pc.cdn.bitgravity.com/airlive/hlspush/hlsmaster/fmaircr_64k.m3u8"  // AIR FM Gold Delhi
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
#define MATRIX_ROTATION 1  // 0=0°, 1=90°, 2=180°, 3=270°

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
    .data_in_num = I2S_INTR_GPIO_DEFAULT  // Not used
};

// Audio objects
Audio audio;
BluetoothA2DPSink a2dp_sink;

// Rotary encoder setup
ESP32Encoder encoder;
int lastEncoderValue = 0;
bool buttonPressed = false;
long lastButtonPress = 0;

// Menu system
#define MENU_HOME 0
#define MENU_CLOCK 1
#define MENU_WEATHER 2
#define MENU_STOCKS 3
#define MENU_CRYPTO 4
#define MENU_RADIO 5
#define MENU_BLUETOOTH 6
#define MENU_SLIDESHOW 7

int mainMenuPosition = 0;
int submenuPosition = 0;
bool inSubmenu = false;
bool inScrollAnimation = false;
bool audioPlaying = false;
bool radioPlaying = false;
bool bluetoothPlaying = false;
bool slideshowActive = false;

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
    float prices[10];  // Store last 10 prices for chart
} stocks[5];

struct CryptoData {
    float price;
    float change_24h;
} bitcoin;

// Radio buffer settings
#define RADIO_BUFFER_MINUTES 10
#define BUFFER_CHUNK_SIZE 32768  // 32KB chunks for SD card buffer
bool timeShiftActive = false;
int timeShiftOffset = 0;  // In seconds
String currentSong = "Unknown";
String currentArtist = "Unknown";

// Bluetooth metadata
String btDeviceName = "Unknown";
String btCurrentTrack = "Unknown";
String btCurrentArtist = "Unknown";
String btCurrentAlbum = "Unknown";
String btAlbumArtPath = "";
bool btAlbumArtDownloading = false;
uint32_t btAlbumArtLastUpdate = 0;
const uint32_t btAlbumArtUpdateInterval = 10000;  // 10 seconds

// Slideshow variables
#define MAX_IMAGES 50
String imageFiles[MAX_IMAGES];
int imageCount = 0;
int currentImage = 0;
unsigned long lastImageChange = 0;
const unsigned long imageChangeInterval = 5000;  // 5 seconds

// Animation variables
unsigned long lastFrameTime = 0;
const int frameDelay = 50; // 20fps
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
const unsigned long weatherInterval = 300000;   // 5 minutes
const unsigned long stockInterval = 300000;     // 5 minutes
const unsigned long cryptoInterval = 180000;    // 3 minutes
const unsigned long timeUpdateInterval = 1000;  // 1 second

// SD Card buffer for radio time-shift
File radioBufferFile;
int currentBufferIndex = 0;
const int maxBufferFiles = RADIO_BUFFER_MINUTES * 6; // 6 files per minute (10-second chunks)
String radioBufferFiles[RADIO_BUFFER_MINUTES * 6];
unsigned long lastBufferWrite = 0;
const unsigned long bufferWriteInterval = 10000; // Write every 10 seconds

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
    "Clock", "Weather", "Stocks", "Bitcoin", "Radio", "Bluetooth", "Slideshow"
};
const int numMainMenuItems = 7;

// JPEG decoder for album art
JPEGDEC jpeg;

// Serial debug helper macros
#define SERIAL_DEBUG(x) if(DEBUG_SERIAL){Serial.print(x);}
#define SERIAL_DEBUGLN(x) if(DEBUG_SERIAL){Serial.println(x);}
#define SERIAL_DEBUG_HEADER(x) if(DEBUG_SERIAL){Serial.print("["); Serial.print(x); Serial.print("] ");}
#define SERIAL_DEBUG_SCREEN(x) if(DEBUG_SERIAL){Serial.print("[SCREEN: "); Serial.print(x); Serial.println("]");}

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
void displayBluetoothMenu();
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
void initAudio();
void initBluetooth();
void startRadio(int stationIndex);
void stopRadio();
void startTimeShift(int secondsBack);
void stopTimeShift();
void playTickSound();
void drawScrollWheel(int position);
void animateScrollWheel();
void showMenuTransition(int fromItem, int toItem);
void initRadioBuffer();
void writeToRadioBuffer(uint8_t* buffer, size_t size);
void readFromRadioBuffer(int timeOffset);
void fetchAlbumArt(String artist, String track);
void displayAlbumArt();
bool loadJpegFromSD(String filename);
int drawMCUblockCallback(JPEGDRAW* pDraw);
String URLEncode(const char* msg);
void logHeapStatus(const char* tag);
void logCurrentScreen();

// Bluetooth callback functions
void bt_metadata_callback(uint8_t id, const uint8_t* data);
void bt_connection_state_changed(esp_a2d_connection_state_t state, void* ptr);

void setup() {
    Serial.begin(115200);
    
    SERIAL_DEBUGLN("\n\n========================================");
    SERIAL_DEBUGLN("ESP32-S3 Matrix Display and Media Center");
    SERIAL_DEBUGLN("========================================");
    
    // Initialize PSRAM if available
    if (psramInit()) {
        SERIAL_DEBUGLN("PSRAM initialized successfully");
    } else {
        SERIAL_DEBUGLN("PSRAM initialization failed");
    }
    
    logHeapStatus("Startup");
    
    // Initialize rotary encoder
    encoder.attachHalfQuad(ENCODER_CLK, ENCODER_DT);
    encoder.setCount(0);
    SERIAL_DEBUGLN("Rotary encoder initialized");
    
    // Set up button pin
    pinMode(ENCODER_SW, INPUT_PULLUP);
    
    // Initialize display
    SERIAL_DEBUG_HEADER("DISPLAY");
    SERIAL_DEBUGLN("Initializing LED matrix...");
    display = new MatrixPanel_I2S_DMA(mxconfig);
    display->begin();
    display->setBrightness8(100);  // Adjust brightness (0-255)
    display->clearScreen();
    // Set display rotation if needed
    display->setRotation(MATRIX_ROTATION);
    SERIAL_DEBUG_HEADER("DISPLAY");
    SERIAL_DEBUGLN("LED matrix initialized");
    
    // Initialize SD Card
    showLoadingScreen("Initializing SD Card...");
    initSDCard();
    
    // Set up directories for album art and radio buffer
    SERIAL_DEBUG_HEADER("SD CARD");
    
    if (!SD.exists("/album_art")) {
        SD.mkdir("/album_art");
        SERIAL_DEBUGLN("Created /album_art directory");
    }
    
    if (!SD.exists("/radio_buffer")) {
        SD.mkdir("/radio_buffer");
        SERIAL_DEBUGLN("Created /radio_buffer directory");
    }
    
    if (!SD.exists("/images")) {
        SD.mkdir("/images");
        SERIAL_DEBUGLN("Created /images directory");
    }
    
    // Scan for images in SD card
    scanSDCardImages();
    
    // Initialize the radio buffer system
    initRadioBuffer();
    
    // Initialize audio
    showLoadingScreen("Setting up Audio...");
    initAudio();
    
    // Initialize Bluetooth
    showLoadingScreen("Setting up Bluetooth...");
    initBluetooth();
    
    // Show loading message
    showLoadingScreen("Connecting to WiFi...");
    
    // Connect to WiFi
    SERIAL_DEBUG_HEADER("WIFI");
    SERIAL_DEBUG("Connecting to ");
    SERIAL_DEBUGLN(ssid);
    
    WiFi.begin(ssid, password);
    int wifiAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
        delay(500);
        SERIAL_DEBUG(".");
        wifiAttempts++;
        display->drawPixel(10 + wifiAttempts, 20, display->color565(0, 255, 0));
        display->fillRect(10, 20, wifiAttempts, 1, display->color565(0, 255, 0));
    }
    SERIAL_DEBUGLN("");
    
    if (WiFi.status() != WL_CONNECTED) {
        SERIAL_DEBUG_HEADER("WIFI");
        SERIAL_DEBUGLN("Connection failed! Continuing without network features...");
        showLoadingScreen("WiFi Failed! Continuing...");
        delay(2000);
    } else {
        SERIAL_DEBUG_HEADER("WIFI");
        SERIAL_DEBUG("Connected, IP address: ");
        SERIAL_DEBUGLN(WiFi.localIP());
        
        // Synchronize time
        showLoadingScreen("Syncing Time...");
        SERIAL_DEBUG_HEADER("TIME");
        SERIAL_DEBUGLN("Syncing with NTP server...");
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            char timeStr[64];
            strftime(timeStr, sizeof(timeStr), "%A, %B %d %Y %H:%M:%S", &timeinfo);
            SERIAL_DEBUG_HEADER("TIME");
            SERIAL_DEBUG("Current time: ");
            SERIAL_DEBUGLN(timeStr);
        }
        
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
    }
    
    // Ready!
    showLoadingScreen("Welcome!");
    SERIAL_DEBUGLN("System initialization complete!");
    delay(500);
    
    // Start with clock display
    currentScreen = MENU_CLOCK;
    mainMenuPosition = 0;
    logCurrentScreen();
}

void logHeapStatus(const char* tag) {
    if (!DEBUG_SERIAL) return;
    
    Serial.print("[MEMORY: ");
    Serial.print(tag);
    Serial.print("] Free heap: ");
    Serial.print(ESP.getFreeHeap() / 1024);
    Serial.print("KB, ");
    
    if (psramFound()) {
        Serial.print("Free PSRAM: ");
        Serial.print(ESP.getFreePsram() / 1024);
        Serial.println("KB");
    } else {
        Serial.println("No PSRAM available");
    }
}

void logCurrentScreen() {
    if (!DEBUG_SERIAL) return;
    
    Serial.print("[NAVIGATION] Current screen: ");
    switch (currentScreen) {
        case MENU_CLOCK:
            Serial.println("Clock");
            break;
        case MENU_WEATHER:
            Serial.println("Weather");
            break;
        case MENU_STOCKS:
            Serial.println("Stocks");
            break;
        case MENU_CRYPTO:
            Serial.println("Bitcoin");
            break;
        case MENU_RADIO:
            Serial.println("Radio");
            break;
        case MENU_BLUETOOTH:
            Serial.println("Bluetooth");
            break;
        case MENU_SLIDESHOW:
            Serial.println("Slideshow");
            break;
        default:
            Serial.println("Unknown");
            break;
    }
    
    Serial.print("[NAVIGATION] Menu position: ");
    Serial.print(mainMenuPosition);
    Serial.print(", In submenu: ");
    Serial.print(inSubmenu ? "Yes" : "No");
    if (inSubmenu) {
        Serial.print(", Submenu position: ");
        Serial.print(submenuPosition);
    }
    Serial.println();
}

void loop() {
    // Update data at regular intervals
    unsigned long currentMillis = millis();
    
    if (WiFi.status() == WL_CONNECTED) {
        if (currentMillis - lastWeatherUpdate > weatherInterval) {
            SERIAL_DEBUG_HEADER("WEATHER");
            SERIAL_DEBUGLN("Updating weather data...");
            fetchWeather();
            lastWeatherUpdate = currentMillis;
        }
        
        if (currentMillis - lastStockUpdate > stockInterval) {
            SERIAL_DEBUG_HEADER("STOCKS");
            SERIAL_DEBUG("Updating stock data for ");
            SERIAL_DEBUGLN(stockSymbols[currentStockIndex]);
            fetchStockData(currentStockIndex);
            currentStockIndex = (currentStockIndex + 1) % numStocks;
            lastStockUpdate = currentMillis;
        }
        
        if (currentMillis - lastCryptoUpdate > cryptoInterval) {
            SERIAL_DEBUG_HEADER("CRYPTO");
            SERIAL_DEBUGLN("Updating Bitcoin data...");
            fetchBitcoinData();
            lastCryptoUpdate = currentMillis;
        }
        
        // Check if we need to fetch album art for Bluetooth playback
        if (bluetoothPlaying && !btAlbumArtDownloading && 
            (btAlbumArtPath.length() == 0 || currentMillis - btAlbumArtLastUpdate > btAlbumArtUpdateInterval)) {
            if (btCurrentArtist != "Unknown" && btCurrentTrack != "Unknown") {
                SERIAL_DEBUG_HEADER("BLUETOOTH");
                SERIAL_DEBUG("Fetching album art for ");
                SERIAL_DEBUG(btCurrentArtist);
                SERIAL_DEBUG(" - ");
                SERIAL_DEBUGLN(btCurrentTrack);
                fetchAlbumArt(btCurrentArtist, btCurrentTrack);
            }
        }
    }
    
    if (currentMillis - lastTimeUpdate > timeUpdateInterval) {
        lastTimeUpdate = currentMillis;
    }
    
    // Handle radio buffer
    if (radioPlaying && !timeShiftActive && currentMillis - lastBufferWrite > bufferWriteInterval) {
        // Process data from the audio stream and write to the buffer
        // This is handled in the audio library callbacks
        SERIAL_DEBUG_HEADER("RADIO");
        SERIAL_DEBUG("Writing to buffer, index: ");
        SERIAL_DEBUGLN(currentBufferIndex);
        lastBufferWrite = currentMillis;
    }
    
    // Handle slideshow timing
    if (slideshowActive && imageCount > 0 && currentMillis - lastImageChange > imageChangeInterval) {
        SERIAL_DEBUG_HEADER("SLIDESHOW");
        SERIAL_DEBUG("Auto advancing to next image: ");
        SERIAL_DEBUGLN(currentImage + 1);
        currentImage = (currentImage + 1) % imageCount;
        lastImageChange = currentMillis;
    }
    
    // Process audio data
    if (radioPlaying) {
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
    
    // Periodically log memory usage (every 30 seconds)
    static unsigned long lastMemoryLog = 0;
    if (currentMillis - lastMemoryLog > 30000) {
        logHeapStatus("Periodic");
        lastMemoryLog = currentMillis;
    }
}

void initSDCard() {
    SERIAL_DEBUG_HEADER("SD CARD");
    SERIAL_DEBUGLN("Initializing SD card...");
    
    SPI.begin(SD_CLK, SD_DATA0, SD_CMD, SD_DATA3);
    
    if (!SD.begin(SD_DATA3)) {
        SERIAL_DEBUG_HEADER("SD CARD");
        SERIAL_DEBUGLN("SD Card initialization failed!");
        return;
    }
    
    SERIAL_DEBUG_HEADER("SD CARD");
    SERIAL_DEBUGLN("SD Card initialized successfully.");
    
    // Display SD card info
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        SERIAL_DEBUG_HEADER("SD CARD");
        SERIAL_DEBUGLN("No SD card attached");
        return;
    }

    SERIAL_DEBUG_HEADER("SD CARD");
    SERIAL_DEBUG("SD Card Type: ");
    if (cardType == CARD_MMC) {
        SERIAL_DEBUGLN("MMC");
    } else if (cardType == CARD_SD) {
        SERIAL_DEBUGLN("SDSC");
    } else if (cardType == CARD_SDHC) {
        SERIAL_DEBUGLN("SDHC");
    } else {
        SERIAL_DEBUGLN("UNKNOWN");
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    SERIAL_DEBUG_HEADER("SD CARD");
    SERIAL_DEBUG("SD Card Size: ");
    SERIAL_DEBUG(cardSize);
    SERIAL_DEBUGLN("MB");
    
    uint64_t usedSpace = 0;
    uint64_t totalSpace = SD.totalBytes();
    uint64_t freeSpace = SD.usedBytes();
    SERIAL_DEBUG_HEADER("SD CARD");
    SERIAL_DEBUG("Used: ");
    SERIAL_DEBUG(freeSpace / 1024);
    SERIAL_DEBUG("KB, Free: ");
    SERIAL_DEBUG((totalSpace - freeSpace) / 1024);
    SERIAL_DEBUGLN("KB");
}

void initRadioBuffer() {
    SERIAL_DEBUG_HEADER("RADIO BUFFER");
    SERIAL_DEBUGLN("Initializing radio buffer...");
    
    // Clear any existing buffer files
    File dir = SD.open("/radio_buffer");
    if (dir && dir.isDirectory()) {
        File file = dir.openNextFile();
        while (file) {
            String fileName = "/" + String(file.name());
            file.close();
            SD.remove(fileName);
            file = dir.openNextFile();
        }
    }
    dir.close();
    
    // Initialize buffer file array
    for (int i = 0; i < maxBufferFiles; i++) {
        radioBufferFiles[i] = "/radio_buffer/chunk_" + String(i) + ".buf";
    }
    
    currentBufferIndex = 0;
    SERIAL_DEBUG_HEADER("RADIO BUFFER");
    SERIAL_DEBUG("Created ");
    SERIAL_DEBUG(maxBufferFiles);
    SERIAL_DEBUG(" buffer files for ");
    SERIAL_DEBUG(RADIO_BUFFER_MINUTES);
    SERIAL_DEBUGLN(" minutes of time-shift");
}

void writeToRadioBuffer(uint8_t* buffer, size_t size) {
    if (!radioPlaying || timeShiftActive) return;
    
    // Open the current buffer file
    radioBufferFile = SD.open(radioBufferFiles[currentBufferIndex], FILE_WRITE);
    if (!radioBufferFile) {
        SERIAL_DEBUG_HEADER("RADIO BUFFER");
        SERIAL_DEBUG("Failed to open buffer file for writing: ");
        SERIAL_DEBUGLN(radioBufferFiles[currentBufferIndex]);
        return;
    }
    
    // Write the audio data
    radioBufferFile.write(buffer, size);
    radioBufferFile.close();
    
    // Move to the next buffer file
    currentBufferIndex = (currentBufferIndex + 1) % maxBufferFiles;
    
    // Initialize the next file (overwrite if it exists)
    radioBufferFile = SD.open(radioBufferFiles[currentBufferIndex], FILE_WRITE);
    if (radioBufferFile) {
        radioBufferFile.close();
    }
}

void readFromRadioBuffer(int timeOffset) {
    if (!radioPlaying) return;
    
    // Calculate which buffer file to read from
    int fileIndex = (currentBufferIndex - (timeOffset / 10) + maxBufferFiles) % maxBufferFiles;
    
    SERIAL_DEBUG_HEADER("RADIO BUFFER");
    SERIAL_DEBUG("Reading from buffer file: ");
    SERIAL_DEBUG(radioBufferFiles[fileIndex]);
    SERIAL_DEBUG(" (offset: ");
    SERIAL_DEBUG(timeOffset);
    SERIAL_DEBUGLN("s)");
    
    // Open the file for reading
    radioBufferFile = SD.open(radioBufferFiles[fileIndex], FILE_READ);
    if (!radioBufferFile) {
        SERIAL_DEBUG_HEADER("RADIO BUFFER");
        SERIAL_DEBUGLN("Failed to open buffer file for reading");
        return;
    }
    
    // Read buffer size
    size_t fileSize = radioBufferFile.size();
    SERIAL_DEBUG_HEADER("RADIO BUFFER");
    SERIAL_DEBUG("Buffer file size: ");
    SERIAL_DEBUG(fileSize);
    SERIAL_DEBUGLN(" bytes");
    
    // Allocate buffer in PSRAM if available, otherwise in heap
    uint8_t* audioBuffer;
    if (psramFound()) {
        audioBuffer = (uint8_t*)ps_malloc(fileSize);
        SERIAL_DEBUG_HEADER("RADIO BUFFER");
        SERIAL_DEBUGLN("Using PSRAM for audio buffer");
    } else {
        audioBuffer = (uint8_t*)malloc(fileSize);
        SERIAL_DEBUG_HEADER("RADIO BUFFER");
        SERIAL_DEBUGLN("Using heap for audio buffer");
    }
    
    if (!audioBuffer) {
        SERIAL_DEBUG_HEADER("RADIO BUFFER");
        SERIAL_DEBUGLN("Failed to allocate buffer");
        radioBufferFile.close();
        return;
    }
    
    // Read data
    radioBufferFile.read(audioBuffer, fileSize);
    radioBufferFile.close();
    
    // Send to audio driver
    SERIAL_DEBUG_HEADER("RADIO BUFFER");
    SERIAL_DEBUG("Playing buffered audio from ");
    SERIAL_DEBUG(timeOffset);
    SERIAL_DEBUGLN(" seconds ago");
    
    // In a real implementation, this would feed directly to the audio codec
    // For example with I2S output or through the Audio library
    if (audioPlaying) {
        audio.stopSong();
        delay(100);
    }
    
    // Feed the buffer to the audio library
    // This is simplified - actual implementation would depend on the Audio library's API
    // audio.playBuffer(audioBuffer, fileSize);
    
    // Free memory
    free(audioBuffer);
}

void scanSDCardImages() {
    SERIAL_DEBUG_HEADER("SLIDESHOW");
    SERIAL_DEBUGLN("Scanning for images...");
    
    File root = SD.open("/images");
    if (!root || !root.isDirectory()) {
        SERIAL_DEBUG_HEADER("SLIDESHOW");
        SERIAL_DEBUGLN("'/images' directory not found");
        return;
    }
    
    File file = root.openNextFile();
    imageCount = 0;
    
    while (file && imageCount < MAX_IMAGES) {
        String fileName = file.name();
        if (fileName.endsWith(".jpg") || fileName.endsWith(".jpeg") || fileName.endsWith(".bmp")) {
            imageFiles[imageCount] = "/images/" + fileName;
            imageCount++;
            SERIAL_DEBUG_HEADER("SLIDESHOW");
            SERIAL_DEBUG("Found image: ");
            SERIAL_DEBUGLN(fileName);
        }
        file = root.openNextFile();
    }
    
    SERIAL_DEBUG_HEADER("SLIDESHOW");
    SERIAL_DEBUG("Total images found: ");
    SERIAL_DEBUGLN(imageCount);
    
    root.close();
}

void initAudio() {
    SERIAL_DEBUG_HEADER("AUDIO");
    SERIAL_DEBUGLN("Initializing audio system...");
    
    // Initialize I2S
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &i2s_pins);
    i2s_set_clk(I2S_NUM_0, 44100, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    
    // Initialize Audio library
    audio.setPinout(I2S_BCLK, I2S_LRCLK, I2S_DIN);
    audio.setVolume(18); // 0...21
    
    // Set up callbacks for radio streams
    audio.setStationNameCallback([](const char* info){
        String streamInfo = String(info);
        SERIAL_DEBUG_HEADER("RADIO");
        SERIAL_DEBUG("Station info: ");
        SERIAL_DEBUGLN(streamInfo);
    });
    
    audio.setAudioDataCallback([](const uint8_t* data, uint32_t len){
        // Copy audio data to SD card buffer when radio is active
        if (radioPlaying && !timeShiftActive) {
            writeToRadioBuffer((uint8_t*)data, len);
        }
    });
    
    audio.setMetadataCallback([](const char* info){
        String streamTitle = String(info);
        SERIAL_DEBUG_HEADER("RADIO");
        SERIAL_DEBUG("Stream title: ");
        SERIAL_DEBUGLN(streamTitle);
        
        // Try to parse artist and song
        int separatorPos = streamTitle.indexOf("-");
        if (separatorPos > 0) {
            currentArtist = streamTitle.substring(0, separatorPos).trim();
            currentSong = streamTitle.substring(separatorPos + 1).trim();
        } else {
            currentSong = streamTitle;
            currentArtist = "Unknown";
        }
    });
    
    SERIAL_DEBUG_HEADER("AUDIO");
    SERIAL_DEBUGLN("Audio system initialized");
}

void initBluetooth() {
    SERIAL_DEBUG_HEADER("BLUETOOTH");
    SERIAL_DEBUGLN("Initializing Bluetooth A2DP sink...");
    
    static const i2s_config_t i2s_config_bluetooth = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
    };
    
    a2dp_sink.set_pin_config(i2s_pins);
    a2dp_sink.set_i2s_config(i2s_config_bluetooth);
    
    // Register Bluetooth metadata callback
    a2dp_sink.set_avrc_metadata_callback(bt_metadata_callback);
    a2dp_sink.set_on_connection_state_changed(bt_connection_state_changed);
    
    a2dp_sink.start("ESP32-S3 Matrix");
    
    SERIAL_DEBUG_HEADER("BLUETOOTH");
    SERIAL_DEBUGLN("Bluetooth ready, waiting for connections");
}

void bt_metadata_callback(uint8_t id, const uint8_t* data) {
    // Parse Bluetooth metadata to get track info
    switch (id) {
        case ESP_AVRC_MD_ATTR_TITLE:
            btCurrentTrack = String((char*)data);
            SERIAL_DEBUG_HEADER("BLUETOOTH");
            SERIAL_DEBUG("Track: ");
            SERIAL_DEBUGLN(btCurrentTrack);
            break;
        case ESP_AVRC_MD_ATTR_ARTIST:
            btCurrentArtist = String((char*)data);
            SERIAL_DEBUG_HEADER("BLUETOOTH");
            SERIAL_DEBUG("Artist: ");
            SERIAL_DEBUGLN(btCurrentArtist);
            break;
        case ESP_AVRC_MD_ATTR_ALBUM:
            btCurrentAlbum = String((char*)data);
            SERIAL_DEBUG_HEADER("BLUETOOTH");
            SERIAL_DEBUG("Album: ");
            SERIAL_DEBUGLN(btCurrentAlbum);
            break;
    }
    
    // When we get new track info, reset album art
    btAlbumArtPath = "";
}

void bt_connection_state_changed(esp_a2d_connection_state_t state, void* ptr) {
    if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        bluetoothPlaying = true;
        btDeviceName = String(a2dp_sink.get_last_peer_name());
        SERIAL_DEBUG_HEADER("BLUETOOTH");
        SERIAL_DEBUG("Device connected: ");
        SERIAL_DEBUGLN(btDeviceName);
    } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        bluetoothPlaying = false;
        SERIAL_DEBUG_HEADER("BLUETOOTH");
        SERIAL_DEBUGLN("Device disconnected");
        btDeviceName = "Unknown";
        btCurrentTrack = "Unknown";
        btCurrentArtist = "Unknown";
        btCurrentAlbum = "Unknown";
        btAlbumArtPath = "";
    }
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

void fetchAlbumArt(String artist, String track) {
    if (WiFi.status() != WL_CONNECTED) return;
    
    btAlbumArtDownloading = true;
    
    // Check if we already have this album art cached
    String cacheFilename = "/album_art/" + artist + "_" + track + ".jpg";
    cacheFilename.replace(" ", "_");
    
    SERIAL_DEBUG_HEADER("ALBUM ART");
    SERIAL_DEBUG("Looking for cached album art: ");
    SERIAL_DEBUGLN(cacheFilename);
    
    if (SD.exists(cacheFilename)) {
        SERIAL_DEBUG_HEADER("ALBUM ART");
        SERIAL_DEBUGLN("Found cached album art");
        btAlbumArtPath = cacheFilename;
        btAlbumArtLastUpdate = millis();
        btAlbumArtDownloading = false;
        return;
    }
    
    // Use Last.fm API to get album art
    String url = "http://ws.audioscrobbler.com/2.0/?method=track.getInfo&api_key=";
    url += lastFmApiKey;
    url += "&artist=";
    url += URLEncode(artist.c_str());
    url += "&track=";
    url += URLEncode(track.c_str());
    url += "&format=json";
    
    SERIAL_DEBUG_HEADER("ALBUM ART");
    SERIAL_DEBUG("Fetching from Last.fm: ");
    SERIAL_DEBUGLN(url);
    
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DynamicJsonDocument doc(16384);  // Larger buffer for Last.fm response
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error && doc.containsKey("track")) {
            // Extract album art URL
            if (doc["track"].containsKey("album") && 
                doc["track"]["album"].containsKey("image")) {
                
                // Get the largest image (usually the last one)
                String imageUrl = "";
                for (JsonVariant image : doc["track"]["album"]["image"].as<JsonArray>()) {
                    if (image["size"] == "large" || image["size"] == "extralarge") {
                        imageUrl = image["#text"].as<String>();
                    }
                }
                
                if (imageUrl.length() > 0) {
                    SERIAL_DEBUG_HEADER("ALBUM ART");
                    SERIAL_DEBUG("Found image URL: ");
                    SERIAL_DEBUGLN(imageUrl);
                    
                    // Download the image
                    HTTPClient imgClient;
                    imgClient.begin(imageUrl);
                    int imgCode = imgClient.GET();
                    
                    if (imgCode == HTTP_CODE_OK) {
                        // Open file for writing
                        File imgFile = SD.open(cacheFilename, FILE_WRITE);
                        if (imgFile) {
                            // Get the data
                            WiFiClient* stream = imgClient.getStreamPtr();
                            uint8_t buffer[1024];
                            size_t totalBytes = 0;
                            
                            // Read all data and write to file
                            while (imgClient.connected() && (totalBytes < imgClient.getSize())) {
                                size_t size = stream->available();
                                if (size) {
                                    int c = stream->readBytes(buffer, ((size > sizeof(buffer)) ? sizeof(buffer) : size));
                                    imgFile.write(buffer, c);
                                    totalBytes += c;
                                }
                                delay(1);
                            }
                            
                            imgFile.close();
                            SERIAL_DEBUG_HEADER("ALBUM ART");
                            SERIAL_DEBUG("Saved album art to SD card: ");
                            SERIAL_DEBUG(totalBytes);
                            SERIAL_DEBUGLN(" bytes");
                            
                            btAlbumArtPath = cacheFilename;
                        } else {
                            SERIAL_DEBUG_HEADER("ALBUM ART");
                            SERIAL_DEBUG("Failed to create file: ");
                            SERIAL_DEBUGLN(cacheFilename);
                        }
                    } else {
                        SERIAL_DEBUG_HEADER("ALBUM ART");
                        SERIAL_DEBUG("Failed to download image, HTTP code: ");
                        SERIAL_DEBUGLN(imgCode);
                    }
                    imgClient.end();
                }
            }
        } else {
            SERIAL_DEBUG_HEADER("ALBUM ART");
            SERIAL_DEBUGLN("No album info found in Last.fm response");
        }
    } else {
        SERIAL_DEBUG_HEADER("ALBUM ART");
        SERIAL_DEBUG("Last.fm API request failed, HTTP code: ");
        SERIAL_DEBUGLN(httpCode);
    }
    
    http.end();
    btAlbumArtLastUpdate = millis();
    btAlbumArtDownloading = false;
}

bool loadJpegFromSD(String filename) {
    File jpegFile = SD.open(filename);
    if (!jpegFile) {
        SERIAL_DEBUG_HEADER("JPEG");
        SERIAL_DEBUG("Failed to open file: ");
        SERIAL_DEBUGLN(filename);
        return false;
    }
    
    SERIAL_DEBUG_HEADER("JPEG");
    SERIAL_DEBUG("Loading JPEG: ");
    SERIAL_DEBUG(filename);
    SERIAL_DEBUG(" (");
    SERIAL_DEBUG(jpegFile.size());
    SERIAL_DEBUGLN(" bytes)");
    
    // Read the file into a buffer (using PSRAM if available)
    uint8_t* jpegBuffer;
    size_t jpegSize = jpegFile.size();
    
    if (psramFound()) {
        jpegBuffer = (uint8_t*)ps_malloc(jpegSize);
    } else {
        jpegBuffer = (uint8_t*)malloc(jpegSize);
    }
    
    if (!jpegBuffer) {
        SERIAL_DEBUG_HEADER("JPEG");
        SERIAL_DEBUGLN("Failed to allocate buffer for JPEG");
        jpegFile.close();
        return false;
    }
    
    // Read the full file
    jpegFile.read(jpegBuffer, jpegSize);
    jpegFile.close();
    
    // Open for the JPEG decoder
    if (jpeg.openRAM(jpegBuffer, jpegSize, drawMCUblockCallback)) {
        SERIAL_DEBUG_HEADER("JPEG");
        SERIAL_DEBUG("JPEG information: ");
        SERIAL_DEBUG(jpeg.getWidth());
        SERIAL_DEBUG("x");
        SERIAL_DEBUG(jpeg.getHeight());
        SERIAL_DEBUG(", ");
        SERIAL_DEBUG(jpeg.getNumComponents());
        SERIAL_DEBUG(" components, ");
        SERIAL_DEBUG(jpeg.getBpp());
        SERIAL_DEBUGLN(" bpp");
        
        // Decode the image
        jpeg.setPixelType(RGB565_BIG_ENDIAN);
        jpeg.setMaxOutputSize(PANEL_WIDTH);
        
        if (jpeg.decode(0, 0, 0)) {
            SERIAL_DEBUG_HEADER("JPEG");
            SERIAL_DEBUGLN("JPEG decoded successfully");
        } else {
            SERIAL_DEBUG_HEADER("JPEG");
            SERIAL_DEBUGLN("JPEG decode failed");
        }
        
        jpeg.close();
    } else {
        SERIAL_DEBUG_HEADER("JPEG");
        SERIAL_DEBUGLN("Failed to open JPEG for decoding");
    }
    
    free(jpegBuffer);
    return true;
}

// JPEG draw callback - this is where we'll actually draw to the display
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

void displayAlbumArt() {
    // Clear central area for album art
    display->fillRect(8, 8, 48, 16, 0);
    
    if (btAlbumArtPath.length() > 0) {
        // Load and display album art
        loadJpegFromSD(btAlbumArtPath);
    } else {
        // Draw placeholder if no album art
        display->drawRect(8, 8, 48, 16, display->color565(100, 100, 255));
        display->drawLine(8, 8, 56, 24, display->color565(100, 100, 255));
        display->drawLine(56, 8, 8, 24, display->color565(100, 100, 255));
    }
}

void fetchWeather() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    SERIAL_DEBUG_HEADER("WEATHER");
    SERIAL_DEBUGLN("Fetching weather data...");
    
    HTTPClient http;
    http.begin(weatherServer);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error) {
            currentWeather.temp = doc["main"]["temp"];
            currentWeather.feels_like = doc["main"]["feels_like"];
            currentWeather.humidity = doc["main"]["humidity"];
            currentWeather.description = doc["weather"][0]["description"].as<String>();
            currentWeather.icon = doc["weather"][0]["icon"].as<String>();
            currentWeather.city = doc["name"].as<String>();
            
            SERIAL_DEBUG_HEADER("WEATHER");
            SERIAL_DEBUG("Weather in ");
            SERIAL_DEBUG(currentWeather.city);
            SERIAL_DEBUG(": ");
            SERIAL_DEBUG(currentWeather.temp);
            SERIAL_DEBUG("°C, ");
            SERIAL_DEBUGLN(currentWeather.description);
        } else {
            SERIAL_DEBUG_HEADER("WEATHER");
            SERIAL_DEBUGLN("Failed to parse weather data");
        }
    } else {
        SERIAL_DEBUG_HEADER("WEATHER");
        SERIAL_DEBUG("Weather API request failed, HTTP code: ");
        SERIAL_DEBUGLN(httpCode);
    }
    http.end();
}

void fetchStockData(int index) {
    if (WiFi.status() != WL_CONNECTED) return;
    
    String symbol = stockSymbols[index];
    String url = "https://www.alphavantage.co/query?function=GLOBAL_QUOTE&symbol=" + symbol + "&apikey=" + stockApiKey;
    
    SERIAL_DEBUG_HEADER("STOCKS");
    SERIAL_DEBUG("Fetching data for ");
    SERIAL_DEBUGLN(symbol);
    
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DynamicJsonDocument doc(2048);
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
            
            SERIAL_DEBUG_HEADER("STOCKS");
            SERIAL_DEBUG(symbol);
            SERIAL_DEBUG(": $");
            SERIAL_DEBUG(stocks[index].price);
            SERIAL_DEBUG(" (");
            SERIAL_DEBUG(stocks[index].change_percent);
            SERIAL_DEBUGLN("%)");
        } else {
            SERIAL_DEBUG_HEADER("STOCKS");
            SERIAL_DEBUGLN("Failed to parse stock data");
        }
    } else {
        SERIAL_DEBUG_HEADER("STOCKS");
        SERIAL_DEBUG("Stock API request failed, HTTP code: ");
        SERIAL_DEBUGLN(httpCode);
    }
    http.end();
}

void fetchBitcoinData() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    SERIAL_DEBUG_HEADER("CRYPTO");
    SERIAL_DEBUGLN("Fetching Bitcoin data...");
    
    HTTPClient http;
    http.begin(cryptoServer);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error) {
            bitcoin.price = doc["bitcoin"]["usd"];
            bitcoin.change_24h = doc["bitcoin"]["usd_24h_change"];
            
            SERIAL_DEBUG_HEADER("CRYPTO");
            SERIAL_DEBUG("Bitcoin: $");
            SERIAL_DEBUG(bitcoin.price);
            SERIAL_DEBUG(" (24h: ");
            SERIAL_DEBUG(bitcoin.change_24h);
            SERIAL_DEBUGLN("%)");
        } else {
            SERIAL_DEBUG_HEADER("CRYPTO");
            SERIAL_DEBUGLN("Failed to parse Bitcoin data");
        }
    } else {
        SERIAL_DEBUG_HEADER("CRYPTO");
        SERIAL_DEBUG("Bitcoin API request failed, HTTP code: ");
        SERIAL_DEBUGLN(httpCode);
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
        
        SERIAL_DEBUG_HEADER("TIME");
        SERIAL_DEBUGLN("Failed to get time");
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
    
    // Log current time to serial
    static int lastSecond = -1;
    if (timeinfo.tm_sec != lastSecond) {
        lastSecond = timeinfo.tm_sec;
        
        if (DEBUG_SERIAL && timeinfo.tm_sec == 0) { // Log only at the start of a new minute
            char fullTimeStr[64];
            strftime(fullTimeStr, sizeof(fullTimeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
            SERIAL_DEBUG_HEADER("TIME");
            SERIAL_DEBUGLN(fullTimeStr);
        }
    }
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

void displayBluetoothMenu() {
    display->setTextSize(1);
    
    // Title
    display->setCursor(2, 1);
    display->setTextColor(display->color565(0, 128, 255));
    display->print("Bluetooth Audio");
    
    // Status
    display->setCursor(2, 12);
    if (bluetoothPlaying) {
        display->setTextColor(display->color565(0, 255, 0));
        display->print("Connected");
        
        // Show device name if available
        display->setCursor(2, 20);
        display->setTextColor(display->color565(255, 255, 255));
        display->print(btDeviceName);
        
        // If we have album art, display it
        if (btAlbumArtPath.length() > 0) {
            displayAlbumArt();
        }
        
        // Show track info
        if (btCurrentTrack != "Unknown") {
            // Scroll the track title if it's too long
            static int scrollPos = 0;
            static unsigned long lastScroll = 0;
            
            if (btCurrentTrack.length() > 12) {
                if (millis() - lastScroll > 500) {
                    scrollPos = (scrollPos + 1) % btCurrentTrack.length();
                    lastScroll = millis();
                }
                
                String visibleText = btCurrentTrack.substring(scrollPos) + " " + btCurrentTrack.substring(0, scrollPos);
                if (visibleText.length() > 12) {
                    visibleText = visibleText.substring(0, 12);
                }
                
                display->setCursor(2, 28);
                display->setTextColor(display->color565(255, 255, 0));
                display->print(visibleText);
            } else {
                display->setCursor(2, 28);
                display->setTextColor(display->color565(255, 255, 0));
                display->print(btCurrentTrack);
            }
        }
    } else {
        display->setTextColor(display->color565(255, 100, 100));
        display->print("Waiting for connection");
        
        display->setCursor(2, 20);
        display->setTextColor(display->color565(255, 255, 0));
        display->print("ESP32-S3 Matrix");
    }
}

void displaySlideshow() {
    display->setTextSize(1);
    
    if (imageCount == 0) {
        display->setCursor(2, 10);
        display->setTextColor(display->color565(255, 0, 0));
        display->print("No images found!");
        
        display->setCursor(2, 20);
        display->print("Add JPG files to");
        
        display->setCursor(2, 28);
        display->print("/images/ on SD card");
        return;
    }
    
    // Display the current image number
    display->setCursor(2, 1);
    display->setTextColor(display->color565(255, 255, 255));
    display->print("Image ");
    display->print(currentImage + 1);
    display->print("/");
    display->print(imageCount);
    
    // Load and display the actual image (simplified version)
    loadJpegFromSD(imageFiles[currentImage]);
    
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
        
        SERIAL_DEBUG_HEADER("ENCODER");
        SERIAL_DEBUG("Rotation detected: ");
        SERIAL_DEBUGLN(direction > 0 ? "Clockwise" : "Counter-clockwise");
        
        // Play tick sound for tactile feedback
        playTickSound();
        
        // Handle based on current screen
        if (!inSubmenu) {
            // Main menu navigation
            scrollWheelTargetPosition = (direction > 0) ? 
                                         (scrollWheelTargetPosition + 1) % numMainMenuItems : 
                                         (scrollWheelTargetPosition - 1 + numMainMenuItems) % numMainMenuItems;
            
            mainMenuPosition = scrollWheelTargetPosition;
            scrollWheelAnimating = true;
            
            SERIAL_DEBUG_HEADER("NAVIGATION");
            SERIAL_DEBUG("Navigating to menu item: ");
            SERIAL_DEBUGLN(mainMenuItems[mainMenuPosition]);
            
            // Start the transition animation
            showMenuTransition(currentScreen, mainMenuPosition + 1);
            currentScreen = mainMenuPosition + 1;
            
        } else {
            // Submenu handling
            switch (currentScreen) {
                case MENU_STOCKS:
                    currentStockIndex = (currentStockIndex + direction + numStocks) % numStocks;
                    SERIAL_DEBUG_HEADER("STOCKS");
                    SERIAL_DEBUG("Selected stock: ");
                    SERIAL_DEBUGLN(stockSymbols[currentStockIndex]);
                    break;
                    
                case MENU_RADIO:
                    if (radioPlaying) {
                        // If in radio playback mode, control time-shift
                        if (timeShiftActive) {
                            // Adjust time-shift offset
                            timeShiftOffset = max(0, min(RADIO_BUFFER_MINUTES * 60, timeShiftOffset + direction * 10));
                            SERIAL_DEBUG_HEADER("RADIO");
                            SERIAL_DEBUG("Adjusting time-shift to: ");
                            SERIAL_DEBUG(timeShiftOffset);
                            SERIAL_DEBUGLN(" seconds");
                            startTimeShift(timeShiftOffset);
                        } else {
                            // Enable time-shift with first turn
                            if (direction < 0) { // Only rewind when turning counter-clockwise
                                SERIAL_DEBUG_HEADER("RADIO");
                                SERIAL_DEBUGLN("Starting time-shift with 10-second rewind");
                                startTimeShift(10);  // Start with 10-second rewind
                            }
                        }
                    } else {
                        // Change radio station selection
                        submenuPosition = (submenuPosition + direction + numRadioStations) % numRadioStations;
                        SERIAL_DEBUG_HEADER("RADIO");
                        SERIAL_DEBUG("Selected station: ");
                        SERIAL_DEBUGLN(radioNames[submenuPosition]);
                    }
                    break;
                    
                case MENU_SLIDESHOW:
                    if (imageCount > 0) {
                        currentImage = (currentImage + direction + imageCount) % imageCount;
                        lastImageChange = millis(); // Reset the automatic change timer
                        SERIAL_DEBUG_HEADER("SLIDESHOW");
                        SERIAL_DEBUG("Selected image: ");
                        SERIAL_DEBUG(currentImage + 1);
                        SERIAL_DEBUG("/");
                        SERIAL_DEBUGLN(imageCount);
                    }
                    break;
                    
                case MENU_BLUETOOTH:
                    // Volume control when Bluetooth is active
                    if (bluetoothPlaying) {
                        int volume = a2dp_sink.get_volume();
                        volume = max(0, min(127, volume + direction * 5));
                        a2dp_sink.set_volume(volume);
                        SERIAL_DEBUG_HEADER("BLUETOOTH");
                        SERIAL_DEBUG("Volume set to: ");
                        SERIAL_DEBUG(volume);
                        SERIAL_DEBUGLN("/127");
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
        
        SERIAL_DEBUG_HEADER("BUTTON");
        SERIAL_DEBUGLN("Button pressed");
        
        // Play feedback sound
        playTickSound();
        
        // Handle button press based on current screen
        if (!inSubmenu) {
            // Enter submenu
            inSubmenu = true;
            submenuPosition = 0;
            
            SERIAL_DEBUG_HEADER("NAVIGATION");
            SERIAL_DEBUG("Entering submenu for ");
            SERIAL_DEBUGLN(mainMenuItems[mainMenuPosition]);
            
            // Special handling for radio and slideshow
            if (currentScreen == MENU_RADIO) {
                if (!radioPlaying) {
                    // First press in radio mode shows the stations
                    SERIAL_DEBUG_HEADER("RADIO");
                    SERIAL_DEBUGLN("Showing radio station list");
                } else {
                    // If already playing, toggle time-shift
                    if (timeShiftActive) {
                        SERIAL_DEBUG_HEADER("RADIO");
                        SERIAL_DEBUGLN("Stopping time-shift, returning to live");
                        stopTimeShift();
                    } else {
                        SERIAL_DEBUG_HEADER("RADIO");
                        SERIAL_DEBUGLN("Radio already playing, showing controls");
                    }
                }
            } else if (currentScreen == MENU_SLIDESHOW) {
                slideshowActive = !slideshowActive;
                SERIAL_DEBUG_HEADER("SLIDESHOW");
                SERIAL_DEBUG("Auto slideshow: ");
                SERIAL_DEBUGLN(slideshowActive ? "ON" : "OFF");
            }
        } else {
            // Handle submenu button press
            switch (currentScreen) {
                case MENU_RADIO:
                    if (!radioPlaying) {
                        // Start selected radio station
                        SERIAL_DEBUG_HEADER("RADIO");
                        SERIAL_DEBUG("Starting radio station: ");
                        SERIAL_DEBUGLN(radioNames[submenuPosition]);
                        startRadio(submenuPosition);
                    } else {
                        // If already playing, exit radio submenu
                        SERIAL_DEBUG_HEADER("RADIO");
                        SERIAL_DEBUGLN("Exiting radio submenu");
                        inSubmenu = false;
                    }
                    break;
                    
                case MENU_SLIDESHOW:
                    SERIAL_DEBUG_HEADER("SLIDESHOW");
                    SERIAL_DEBUGLN("Exiting slideshow submenu");
                    inSubmenu = false;
                    break;
                    
                case MENU_BLUETOOTH:
                    SERIAL_DEBUG_HEADER("BLUETOOTH");
                    SERIAL_DEBUGLN("Exiting Bluetooth submenu");
                    inSubmenu = false;
                    break;
                    
                case MENU_STOCKS:
                    SERIAL_DEBUG_HEADER("STOCKS");
                    SERIAL_DEBUGLN("Exiting stocks submenu");
                    inSubmenu = false;
                    break;
                    
                default:
                    SERIAL_DEBUG_HEADER("NAVIGATION");
                    SERIAL_DEBUGLN("Exiting submenu");
                    inSubmenu = false;
                    break;
            }
        }
        
        // Log the current navigation state
        logCurrentScreen();
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
                
            case MENU_BLUETOOTH:
                displayBluetoothMenu();
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
            
        case MENU_BLUETOOTH:
            displayBluetoothMenu();
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
    
    SERIAL_DEBUG_HEADER("TRANSITION");
    SERIAL_DEBUG("Transitioning from screen ");
    SERIAL_DEBUG(fromItem);
    SERIAL_DEBUG(" to ");
    SERIAL_DEBUGLN(toItem);
}

void startRadio(int stationIndex) {
    // Stop any currently playing audio
    if (radioPlaying) {
        audio.stopSong();
    }
    
    // Update state
    currentRadioStation = stationIndex;
    radioPlaying = true;
    timeShiftActive = false;
    
    SERIAL_DEBUG_HEADER("RADIO");
    SERIAL_DEBUG("Starting radio station: ");
    SERIAL_DEBUGLN(radioNames[stationIndex]);
    
    // Start playing the selected station
    audio.connecttohost(radioStations[stationIndex]);
    
    // Reset metadata
    currentSong = "Loading...";
    currentArtist = "";
}

void stopRadio() {
    if (radioPlaying) {
        SERIAL_DEBUG_HEADER("RADIO");
        SERIAL_DEBUGLN("Stopping radio playback");
        
        audio.stopSong();
        radioPlaying = false;
        timeShiftActive = false;
    }
}

void startTimeShift(int secondsBack) {
    if (!radioPlaying) return;
    
    SERIAL_DEBUG_HEADER("RADIO TIMESHIFT");
    SERIAL_DEBUG("Starting time-shift, rewinding ");
    SERIAL_DEBUG(secondsBack);
    SERIAL_DEBUGLN(" seconds");
    
    timeShiftActive = true;
    timeShiftOffset = secondsBack;
    
    // Read from the buffer
    readFromRadioBuffer(secondsBack);
}

void stopTimeShift() {
    if (!radioPlaying) return;
    
    SERIAL_DEBUG_HEADER("RADIO TIMESHIFT");
    SERIAL_DEBUGLN("Stopping time-shift, returning to live");
    
    timeShiftActive = false;
    timeShiftOffset = 0;
    
    // Resume live playback by reconnecting to the station
    audio.stopSong();
    delay(100);
    audio.connecttohost(radioStations[currentRadioStation]);
}

void playTickSound() {
    // In a real implementation, this would play a short tick sound
    // For feedback when turning the encoder
    
    // For simplicity, we'll just toggle a GPIO pin that could be
    // connected to a piezo buzzer or log to serial
    SERIAL_DEBUG_HEADER("SOUND");
    SERIAL_DEBUGLN("Tick");
}

void showLoadingScreen(String message) {
    SERIAL_DEBUG_HEADER("LOADING");
    SERIAL_DEBUGLN(message);
    
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
    
