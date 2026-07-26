
/*
  ESP8266 SmallTV Pro V8.0.15 - Full Audit Fix
  ESP-12S + ST7789 240x240

  Confirmed LCD pins:
    CS   = GPIO15
    DC   = GPIO2
    RST  = GPIO0
    SCLK = GPIO14
    MOSI = GPIO13

  Raw SPI only. No Adafruit_GFX / TFT_eSPI required.
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ESP8266HTTPUpdateServer.h>
#include <Updater.h>
#include <EEPROM.h>
#include <SPI.h>
#include <time.h>

static const char* FW_VERSION = "8.0.15";
static const char* FW_BUILD = __DATE__ " " __TIME__;

// ---------------- LCD ----------------
static const uint8_t TFT_CS  = 15;
static const uint8_t TFT_DC  = 2;
static const uint8_t TFT_RST = 0;

#define RGB565(r,g,b) (uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3))
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define CYAN    0x07FF
#define YELLOW  0xFFE0
#define ORANGE  0xFD20
#define GREY    0x8410

const uint16_t COL_BG      = RGB565(7, 12, 18);
const uint16_t COL_PANEL   = RGB565(13, 23, 32);
const uint16_t COL_CARD    = RGB565(20, 32, 43);
const uint16_t COL_CARD2   = RGB565(24, 39, 52);
const uint16_t COL_LINE    = RGB565(46, 68, 84);
const uint16_t COL_MUTED   = RGB565(130, 153, 166);
const uint16_t COL_ACCENT  = RGB565(59, 210, 232);
const uint16_t COL_TEMP    = RGB565(255, 185, 74);
const uint16_t COL_GOOD    = RGB565(91, 220, 145);
const uint16_t COL_SHADOW  = RGB565(2, 5, 9);
const uint16_t COL_DIGIT   = RGB565(244, 249, 252);

// ---------------- Settings ----------------
struct Settings {
  uint32_t magic;
  char ssid[33];
  char password[65];
  char city[24];
  char latitude[16];
  char longitude[16];
  int16_t utcOffsetMinutes;
  uint8_t autoPage;
  uint8_t pageIntervalSeconds;
  uint8_t colonBlink;
  uint8_t use12Hour;
  uint16_t clockColor;
  uint16_t dateColor;
  uint8_t weatherIntervalMinutes;
  uint8_t aqiIntervalMinutes;
  // Appended for backward-compatible UTF-8 location storage.
  char locationName[64];
  // Crypto settings, appended to preserve older EEPROM layouts.
  char cryptoSymbols[32];   // Example: BTC,ETH,DOGE
  uint16_t cryptoIntervalSeconds;
};

const uint32_t SETTINGS_MAGIC = 0x53563531;
Settings cfg;
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

// Explicit forward declarations.
// Arduino normally generates prototypes automatically, but large raw HTML
// strings and lambdas can prevent that mechanism from seeing these functions.
String otaUpdatePage();
void handleGeocodeRequest();
void sendJsonOk();
void startWebServer();
void drawOtaStartScreen();
void drawOtaProgress(size_t current, size_t total);
void drawOtaSuccessScreen();
void drawOtaErrorScreen(int errorCode);

bool apMode = false;
bool ntpOk = false;
bool weatherOk = false;

float currentTemp = 0;
float apparentTemp = 0;
float minTemp = 0;
float maxTemp = 0;
float windSpeed = 0;
float windDirection = 0;
float precipitation = 0;
float pressure = 0;
float cloudCover = 0;
int humidity = 0;
int weatherCode = 0;
int aqi = -1;
String weatherName = "SYNC";

struct CryptoQuote {
  String symbol;
  float usd;
  float change24h;
  bool valid;
};
CryptoQuote cryptoQuotes[3];
unsigned long lastCryptoUpdate = 0;
unsigned long lastCryptoSuccessAt = 0;

float forecastMax[7] = {0};
float forecastMin[7] = {0};
int forecastCode[7] = {0};
int forecastRainChance[7] = {0};
bool forecastValid = false;

unsigned long lastMochiAnimation = 0;
uint8_t mochiAnimationFrame = 0;
bool mochiScreenReady = false;
uint8_t lastRenderedMochiExpression = 255;
bool lastMochiBlink = false;

bool cryptoScreenReady = false;
String lastCryptoRenderedSymbol[3];
String lastCryptoRenderedPrice[3];
String lastCryptoRenderedChange[3];
unsigned long lastCryptoClockDraw = 0;

unsigned long lastClockUpdate = 0;
unsigned long lastFooterCheck = 0;
unsigned long lastWeatherUpdate = 0;
unsigned long lastAirQualityUpdate = 0;
unsigned long lastWifiRetry = 0;
unsigned long lastPageChange = 0;
unsigned long lastColonToggle = 0;
unsigned long setupApStartedAt = 0;
unsigned long lastWeatherSuccessAt = 0;
unsigned long lastAqiSuccessAt = 0;
bool setupApActive = false;
bool waitingScreenShown = false;
const unsigned long SETUP_AP_TIMEOUT_MS = 10UL * 60UL * 1000UL;

uint8_t currentPage = 0;
uint8_t mochiExpression = 0; // 0=AUTO, 1=VUI, 2=BUON, 3=NGU, 4=NGAC NHIEN, 5=GIAN, 6=YEU
bool colonVisible = true;
bool forcePageRedraw = true;

bool otaInProgress = false;
uint8_t otaLastPercent = 255;
unsigned long otaLastDrawAt = 0;

// Cached UI values: only changed areas are redrawn.
int lastHourTens = -1;
int lastHourOnes = -1;
int lastMinuteTens = -1;
int lastMinuteOnes = -1;
int lastDay = -1;
int lastMonth = -1;
int lastWeekday = -1;
String lastFooterState = "";
String lastDateText = "";
int lastFooterHumidity = -999;
int lastFooterAqi = -999;
int lastFooterWind = -999;

// ---------------- 5x7 font ----------------
const uint8_t font5x7[][5] PROGMEM = {
{0,0,0,0,0},{0,0,95,0,0},{0,7,0,7,0},{20,127,20,127,20},{36,42,127,42,18},
{35,19,8,100,98},{54,73,85,34,80},{0,5,3,0,0},{0,28,34,65,0},{0,65,34,28,0},
{20,8,62,8,20},{8,8,62,8,8},{0,80,48,0,0},{8,8,8,8,8},{0,96,96,0,0},
{32,16,8,4,2},{62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},
{33,65,69,75,49},{24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},
{1,113,9,5,3},{54,73,73,73,54},{6,73,73,41,30},{0,54,54,0,0},
{0,86,54,0,0},{8,20,34,65,0},{20,20,20,20,20},{0,65,34,20,8},
{2,1,81,9,6},{50,73,121,65,62},{126,17,17,17,126},{127,73,73,73,54},
{62,65,65,65,34},{127,65,65,34,28},{127,73,73,73,65},{127,9,9,9,1},
{62,65,73,73,122},{127,8,8,8,127},{0,65,127,65,0},{32,64,65,63,1},
{127,8,20,34,65},{127,64,64,64,64},{127,2,12,2,127},{127,4,8,16,127},
{62,65,65,65,62},{127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},
{70,73,73,73,49},{1,1,127,1,1},{63,64,64,64,63},{31,32,64,32,31},
{63,64,56,64,63},{99,20,8,20,99},{7,8,112,8,7},{97,81,73,69,67}
};

// ---------------- ST7789 raw SPI ----------------
inline void lcdSelect(bool active) {
  digitalWrite(TFT_CS, active ? LOW : HIGH);
}

void lcdCommand(uint8_t v) {
  digitalWrite(TFT_DC, LOW);
  lcdSelect(true);
  SPI.transfer(v);
  lcdSelect(false);
}

void lcdData8(uint8_t v) {
  digitalWrite(TFT_DC, HIGH);
  lcdSelect(true);
  SPI.transfer(v);
  lcdSelect(false);
}

void lcdData16(uint16_t v) {
  digitalWrite(TFT_DC, HIGH);
  lcdSelect(true);
  SPI.transfer(v >> 8);
  SPI.transfer(v & 0xFF);
  lcdSelect(false);
}

void lcdReset() {
  digitalWrite(TFT_RST, HIGH); delay(20);
  digitalWrite(TFT_RST, LOW);  delay(80);
  digitalWrite(TFT_RST, HIGH); delay(180);
}

void lcdInit() {
  lcdReset();
  lcdCommand(0x01); delay(150);
  lcdCommand(0x11); delay(150);
  lcdCommand(0x3A); lcdData8(0x55);
  lcdCommand(0x36); lcdData8(0x00);
  lcdCommand(0x21);
  lcdCommand(0x13);
  lcdCommand(0x29);
  delay(120);
}

void lcdWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  lcdCommand(0x2A); lcdData16(x0); lcdData16(x1);
  lcdCommand(0x2B); lcdData16(y0); lcdData16(y1);
  lcdCommand(0x2C);
}

void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (w <= 0 || h <= 0) return;
  if (x < 0 || y < 0 || x + w > 240 || y + h > 240) return;

  lcdWindow(x, y, x + w - 1, y + h - 1);

  const uint8_t hi = color >> 8;
  const uint8_t lo = color & 0xFF;

  digitalWrite(TFT_DC, HIGH);
  lcdSelect(true);

  uint32_t remaining = (uint32_t)w * (uint32_t)h;
  uint16_t watchdogCounter = 0;

  while (remaining--) {
    SPI.transfer(hi);
    SPI.transfer(lo);

    // ESP8266 software watchdog needs CPU time during large screen fills.
    // Feeding every 512 pixels keeps rendering stable without visible gaps.
    if (++watchdogCounter >= 512) {
      watchdogCounter = 0;
      yield();
    }
  }

  lcdSelect(false);
}

void fillScreen(uint16_t color) {
  fillRect(0, 0, 240, 240, color);
}

void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  fillRect(x, y, w, 1, color);
  fillRect(x, y + h - 1, w, 1, color);
  fillRect(x, y, 1, h, color);
  fillRect(x + w - 1, y, 1, h, color);
}

void fillCircle(int16_t cx, int16_t cy, int16_t radius, uint16_t color) {
  for (int16_t y = -radius; y <= radius; y++) {
    int16_t xx = (int16_t)sqrt((float)radius * radius - (float)y * y);
    fillRect(cx - xx, cy + y, xx * 2 + 1, 1, color);
    if ((y & 3) == 0) yield();
  }
}

void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                   int16_t radius, uint16_t color) {
  fillRect(x + radius, y, w - 2 * radius, h, color);
  fillRect(x, y + radius, w, h - 2 * radius, color);
  fillCircle(x + radius, y + radius, radius, color);
  fillCircle(x + w - radius - 1, y + radius, radius, color);
  fillCircle(x + radius, y + h - radius - 1, radius, color);
  fillCircle(x + w - radius - 1, y + h - radius - 1, radius, color);
}

void drawHGradientLine(int16_t x, int16_t y, int16_t w,
                       uint16_t left, uint16_t right) {
  uint8_t lr = (left >> 11) << 3;
  uint8_t lg = ((left >> 5) & 0x3F) << 2;
  uint8_t lb = (left & 0x1F) << 3;
  uint8_t rr = (right >> 11) << 3;
  uint8_t rg = ((right >> 5) & 0x3F) << 2;
  uint8_t rb = (right & 0x1F) << 3;

  for (int16_t i = 0; i < w; i++) {
    uint8_t r = lr + ((int32_t)(rr - lr) * i) / max(1, w - 1);
    uint8_t g = lg + ((int32_t)(rg - lg) * i) / max(1, w - 1);
    uint8_t b = lb + ((int32_t)(rb - lb) * i) / max(1, w - 1);
    fillRect(x + i, y, 1, 1, RGB565(r, g, b));
    if ((i & 31) == 0) yield();
  }
}

void drawDegree(int16_t x, int16_t y, uint16_t color, uint16_t bg) {
  fillRect(x, y, 7, 7, bg);
  drawRect(x + 1, y + 1, 5, 5, color);
  fillRect(x + 2, y + 2, 3, 3, bg);
}

void drawChar(int16_t x, int16_t y, char ch, uint16_t fg, uint16_t bg, uint8_t scale) {
  if (ch >= 'a' && ch <= 'z') ch -= 32;
  if (ch < 32 || ch > 90) ch = ' ';

  uint8_t idx = ch - 32;

  for (uint8_t col = 0; col < 5; col++) {
    uint8_t bits = pgm_read_byte(&font5x7[idx][col]);

    for (uint8_t row = 0; row < 7; row++) {
      fillRect(x + col * scale, y + row * scale, scale, scale,
               (bits & (1 << row)) ? fg : bg);
    }
  }

  fillRect(x + 5 * scale, y, scale, 7 * scale, bg);
}

void drawText(int16_t x, int16_t y, String s, uint16_t fg, uint16_t bg, uint8_t scale) {
  s.toUpperCase();
  for (size_t i = 0; i < s.length(); i++) {
    drawChar(x, y, s[i], fg, bg, scale);
    x += 6 * scale;
    if ((i & 3) == 3) yield();
  }
}

String twoDigits(int v) {
  return v < 10 ? "0" + String(v) : String(v);
}

// ---------------- Premium digital clock ----------------
const uint8_t SEGMENTS[10] = {
  0b1111110, 0b0110000, 0b1101101, 0b1111001, 0b0110011,
  0b1011011, 0b1011111, 0b1110000, 0b1111111, 0b1111011
};

void drawSegmentH(int x, int y, int len, int thick, uint16_t color) {
  // Beveled ends make the digits look less like plain rectangles.
  fillRect(x + thick / 2, y, len - thick, thick, color);
  for (int i = 0; i < thick / 2; i++) {
    fillRect(x + i, y + thick / 2 - i, 1, 2 * i + 1, color);
    fillRect(x + len - 1 - i, y + thick / 2 - i, 1, 2 * i + 1, color);
  }
}

void drawSegmentV(int x, int y, int len, int thick, uint16_t color) {
  fillRect(x, y + thick / 2, thick, len - thick, color);
  for (int i = 0; i < thick / 2; i++) {
    fillRect(x + thick / 2 - i, y + i, 2 * i + 1, 1, color);
    fillRect(x + thick / 2 - i, y + len - 1 - i, 2 * i + 1, 1, color);
  }
}

void clearBigDigit(int x, int y) {
  fillRect(x, y, 36, 68, COL_PANEL);
}

void drawBigDigit(int x, int y, int digit, uint16_t color) {
  const int W = 36;
  const int H = 68;
  const int T = 6;
  uint8_t mask = SEGMENTS[digit];

  if (mask & 0b1000000) drawSegmentH(x,         y,                  W, T, color);
  if (mask & 0b0100000) drawSegmentV(x + W - T, y + 2,              H / 2 - 2, T, color);
  if (mask & 0b0010000) drawSegmentV(x + W - T, y + H / 2,          H / 2 - 2, T, color);
  if (mask & 0b0001000) drawSegmentH(x,         y + H - T,          W, T, color);
  if (mask & 0b0000100) drawSegmentV(x,         y + H / 2,          H / 2 - 2, T, color);
  if (mask & 0b0000010) drawSegmentV(x,         y + 2,              H / 2 - 2, T, color);
  if (mask & 0b0000001) drawSegmentH(x,         y + H / 2 - T / 2, W, T, color);
}

void updateBigDigit(int x, int y, int value, int &cachedValue) {
  if (value == cachedValue) return;
  clearBigDigit(x, y);
  drawBigDigit(x, y, value, cfg.clockColor);
  cachedValue = value;
}

void drawColonStatic() {
  fillRect(102, 60, 12, 80, COL_PANEL);
  fillCircle(108, 84, 3, COL_ACCENT);
  fillCircle(108, 116, 3, COL_ACCENT);
}

// ---------------- Weather icon ----------------
String weatherTextForCode(int code) {
  // Open-Meteo WMO weather codes, displayed in Vietnamese without accents
  // for compatibility with the compact 5x7 LCD font.
  if (code == 0) return "TROI QUANG";
  if (code == 1) return "GAN NHU QUANG";
  if (code == 2) return "MAY RAI RAC";
  if (code == 3) return "NHIEU MAY";
  if (code == 45 || code == 48) return "SUONG MU";
  if (code >= 51 && code <= 55) return "MUA PHUN";
  if (code == 56 || code == 57) return "MUA PHUN LANH";
  if (code >= 61 && code <= 65) return "MUA";
  if (code == 66 || code == 67) return "MUA LANH";
  if (code >= 71 && code <= 77) return "TUYET";
  if (code >= 80 && code <= 82) return "MUA RAO";
  if (code == 85 || code == 86) return "MUA TUYET";
  if (code == 95) return "DONG";
  if (code == 96 || code == 99) return "DONG MUA DA";
  return "THOI TIET";
}

void drawSunIcon(int x, int y) {
  fillCircle(x, y, 7, COL_TEMP);
  fillRect(x - 2, y - 16, 4, 5, COL_TEMP);
  fillRect(x - 2, y + 11, 4, 5, COL_TEMP);
  fillRect(x - 16, y - 2, 5, 4, COL_TEMP);
  fillRect(x + 11, y - 2, 5, 4, COL_TEMP);
  fillRect(x - 12, y - 12, 4, 4, COL_TEMP);
  fillRect(x + 8, y - 12, 4, 4, COL_TEMP);
  fillRect(x - 12, y + 8, 4, 4, COL_TEMP);
  fillRect(x + 8, y + 8, 4, 4, COL_TEMP);
}

void drawCloudIcon(int x, int y, uint16_t color) {
  fillCircle(x - 8, y, 8, color);
  fillCircle(x + 2, y - 5, 11, color);
  fillCircle(x + 12, y + 1, 7, color);
  fillRect(x - 15, y, 31, 9, color);
}

void drawWeatherIcon(int code, int x, int y) {
  fillRect(x - 24, y - 21, 48, 43, COL_CARD);

  if (code == 0) {
    drawSunIcon(x, y);
  } else if (code <= 3) {
    drawSunIcon(x - 8, y - 7);
    drawCloudIcon(x + 4, y + 5, WHITE);
  } else if (code == 45 || code == 48) {
    drawCloudIcon(x, y - 6, RGB565(190, 207, 214));
    fillRect(x - 18, y + 9, 36, 2, COL_MUTED);
    fillRect(x - 13, y + 15, 26, 2, COL_MUTED);
  } else if (code >= 95) {
    drawCloudIcon(x, y - 7, RGB565(185, 198, 208));
    drawSegmentV(x - 2, y + 5, 13, 4, COL_TEMP);
    fillRect(x - 6, y + 12, 7, 4, COL_TEMP);
  } else {
    drawCloudIcon(x, y - 7, RGB565(222, 238, 244));
    fillRect(x - 12, y + 8, 2, 9, COL_ACCENT);
    fillRect(x - 1, y + 8, 2, 9, COL_ACCENT);
    fillRect(x + 10, y + 8, 2, 9, COL_ACCENT);
  }
}

// ---------------- Settings storage ----------------
void loadDefaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = SETTINGS_MAGIC;
  strcpy(cfg.city, "HANOI");
  strcpy(cfg.locationName, "HANOI");
  strcpy(cfg.latitude, "21.0285");
  strcpy(cfg.longitude, "105.8542");
  cfg.utcOffsetMinutes = 420;
  cfg.autoPage = 1;
  cfg.pageIntervalSeconds = 12;
  cfg.colonBlink = 1;
  cfg.use12Hour = 0;
  cfg.clockColor = COL_DIGIT;
  cfg.dateColor = COL_ACCENT;
  cfg.weatherIntervalMinutes = 5;
  cfg.aqiIntervalMinutes = 15;
  strlcpy(cfg.cryptoSymbols, "BTC,ETH,DOGE", sizeof(cfg.cryptoSymbols));
  cfg.cryptoIntervalSeconds = 30;
}

bool isValidUtf8CString(const char* value, size_t capacity) {
  if (!value || capacity == 0) return false;
  size_t length = strnlen(value, capacity);
  if (length == 0 || length >= capacity) return false;

  const uint8_t* data = reinterpret_cast<const uint8_t*>(value);
  for (size_t i = 0; i < length;) {
    uint8_t c = data[i];
    size_t continuation = 0;
    if (c <= 0x7F) continuation = 0;
    else if (c >= 0xC2 && c <= 0xDF) continuation = 1;
    else if (c >= 0xE0 && c <= 0xEF) continuation = 2;
    else if (c >= 0xF0 && c <= 0xF4) continuation = 3;
    else return false;

    if (i + continuation >= length) return false;
    for (size_t j = 1; j <= continuation; j++) {
      if ((data[i + j] & 0xC0) != 0x80) return false;
    }
    i += continuation + 1;
  }
  return true;
}

void copyUtf8Safe(char* destination, size_t capacity, const String& source) {
  if (!destination || capacity == 0) return;
  destination[0] = '\0';

  size_t sourceLength = source.length();
  size_t sourceIndex = 0;
  size_t destinationIndex = 0;
  while (sourceIndex < sourceLength && destinationIndex + 1 < capacity) {
    uint8_t first = static_cast<uint8_t>(source[sourceIndex]);
    size_t codePointBytes = 1;
    if (first >= 0xC2 && first <= 0xDF) codePointBytes = 2;
    else if (first >= 0xE0 && first <= 0xEF) codePointBytes = 3;
    else if (first >= 0xF0 && first <= 0xF4) codePointBytes = 4;
    else if (first >= 0x80) { sourceIndex++; continue; }

    if (sourceIndex + codePointBytes > sourceLength ||
        destinationIndex + codePointBytes >= capacity) break;

    bool valid = true;
    for (size_t j = 1; j < codePointBytes; j++) {
      if ((static_cast<uint8_t>(source[sourceIndex + j]) & 0xC0) != 0x80) {
        valid = false;
        break;
      }
    }
    if (!valid) { sourceIndex++; continue; }

    for (size_t j = 0; j < codePointBytes; j++) {
      destination[destinationIndex++] = source[sourceIndex++];
    }
  }
  destination[destinationIndex] = '\0';
}

String storedLocationName() {
  if (isValidUtf8CString(cfg.locationName, sizeof(cfg.locationName))) {
    return String(cfg.locationName);
  }
  return String(cfg.city);
}

void loadSettings() {
  EEPROM.begin(sizeof(Settings));
  EEPROM.get(0, cfg);

  if (cfg.magic != SETTINGS_MAGIC) {
    loadDefaults();
    EEPROM.put(0, cfg);
    EEPROM.commit();
  } else {
    bool changed = false;
    if (!isValidUtf8CString(cfg.locationName, sizeof(cfg.locationName))) {
      copyUtf8Safe(cfg.locationName, sizeof(cfg.locationName), String(cfg.city));
      changed = true;
    }
    if (cfg.weatherIntervalMinutes < 1 || cfg.weatherIntervalMinutes > 60) {
      cfg.weatherIntervalMinutes = 5;
      changed = true;
    }
    if (cfg.aqiIntervalMinutes < 1 || cfg.aqiIntervalMinutes > 60) {
      cfg.aqiIntervalMinutes = 15;
      changed = true;
    }
    if (strnlen(cfg.cryptoSymbols, sizeof(cfg.cryptoSymbols)) == 0 ||
        strnlen(cfg.cryptoSymbols, sizeof(cfg.cryptoSymbols)) >= sizeof(cfg.cryptoSymbols)) {
      strlcpy(cfg.cryptoSymbols, "BTC,ETH,DOGE", sizeof(cfg.cryptoSymbols));
      changed = true;
    }
    if (cfg.cryptoIntervalSeconds < 15 || cfg.cryptoIntervalSeconds > 300) {
      cfg.cryptoIntervalSeconds = 30;
      changed = true;
    }
    if (changed) {
      EEPROM.put(0, cfg);
      EEPROM.commit();
    }
  }
}

void saveSettings() {
  cfg.magic = SETTINGS_MAGIC;
  EEPROM.put(0, cfg);
  EEPROM.commit();
}

// ---------------- UI ----------------
void drawBaseUI() {
  fillScreen(COL_BG);

  drawRect(2, 2, 236, 236, COL_SHADOW);
  drawRect(4, 4, 232, 232, COL_LINE);
  drawRect(5, 5, 230, 230, RGB565(10, 18, 25));

  // Compact header.
  fillRoundRect(9, 9, 222, 42, 7, COL_CARD);
  drawHGradientLine(15, 50, 210, COL_ACCENT, RGB565(18, 80, 102));

  // Main clock and calendar area.
  fillRoundRect(9, 56, 222, 108, 8, COL_PANEL);

  // Metrics.
  fillRoundRect(9, 169, 70, 36, 7, COL_CARD);
  fillRoundRect(85, 169, 70, 36, 7, COL_CARD2);
  fillRoundRect(161, 169, 70, 36, 7, COL_CARD);

  // Footer.
  fillRoundRect(9, 210, 222, 22, 7, COL_CARD);
}

void drawHeader() {
  fillRoundRect(9, 9, 222, 42, 7, COL_CARD);

  drawText(15, 13, String(cfg.city), WHITE, COL_CARD, 2);

  String condition = weatherName;
  if (condition.length() > 16) condition = condition.substring(0, 16);
  drawText(15, 35, condition, COL_MUTED, COL_CARD, 1);

  drawWeatherIcon(weatherCode, 171, 29);

  int tempInt = (int)round(currentTemp);
  String tempText = String(tempInt);
  int tempX = tempInt >= 10 ? 193 : 202;
  drawText(tempX, 14, tempText, COL_TEMP, COL_CARD, 2);
  drawDegree(219, 13, COL_TEMP, COL_CARD);
  drawText(218, 32, "C", COL_TEMP, COL_CARD, 1);
}

void drawClock(bool forceRedraw = false) {
  struct tm tmNow;
  time_t now = time(nullptr);

  if (now > 1577836800) {
    localtime_r(&now, &tmNow);
    ntpOk = true;
  } else {
    memset(&tmNow, 0, sizeof(tmNow));
    tmNow.tm_hour = 12;
    tmNow.tm_min = 0;
    tmNow.tm_mday = 1;
    tmNow.tm_mon = 0;
    tmNow.tm_year = 126;
  }

  if (forceRedraw) {
    fillRoundRect(9, 56, 222, 108, 8, COL_PANEL);
    lastHourTens = lastHourOnes = -1;
    lastMinuteTens = lastMinuteOnes = -1;
    lastDay = lastMonth = -1;
    lastDateText = "";
  }

  // Cụm đồng hồ chiếm x=28..212, tâm chính xác tại x=120.
  const int clockY = 59;
  const int hourTensX = 28;
  const int hourOnesX = 69;
  const int minuteTensX = 135;
  const int minuteOnesX = 176;
  const int colonCenterX = 120;

  int displayHour = tmNow.tm_hour;
  if (cfg.use12Hour) {
    displayHour %= 12;
    if (displayHour == 0) displayHour = 12;
  }

  updateBigDigit(hourTensX, clockY, displayHour / 10, lastHourTens);
  updateBigDigit(hourOnesX, clockY, displayHour % 10, lastHourOnes);

  fillRect(105, clockY, 30, 68, COL_PANEL);
  if (!cfg.colonBlink || colonVisible) {
    fillCircle(colonCenterX, 80, 3, COL_ACCENT);
    fillCircle(colonCenterX, 106, 3, COL_ACCENT);
  }

  updateBigDigit(minuteTensX, clockY, tmNow.tm_min / 10, lastMinuteTens);
  updateBigDigit(minuteOnesX, clockY, tmNow.tm_min % 10, lastMinuteOnes);

  if (tmNow.tm_mday != lastDay ||
      tmNow.tm_mon != lastMonth ||
      forceRedraw) {

    // Bỏ dòng thứ, chỉ giữ ngày lớn màu xanh cyan.
    fillRoundRect(18, 132, 204, 28, 6, COL_CARD);

    String dateLine = twoDigits(tmNow.tm_mday) + "/" +
                      twoDigits(tmNow.tm_mon + 1) + "/" +
                      String(tmNow.tm_year + 1900);

    const int dateScale = 2;
    const int dateWidth = (int)dateLine.length() * 6 * dateScale;
    const int dateX = (240 - dateWidth) / 2;

    drawText(dateX, 139, dateLine, cfg.dateColor, COL_CARD, dateScale);

    lastDay = tmNow.tm_mday;
    lastMonth = tmNow.tm_mon;
    lastDateText = dateLine;
  }
}

void drawMetricCard(int x, String title, int value,
                    uint16_t valueColor, uint16_t cardColor) {
  fillRoundRect(x, 169, 70, 36, 7, cardColor);
  drawText(x + 8, 173, title, COL_MUTED, cardColor, 1);

  String number = String(value);
  drawText(x + 8, 187, number, valueColor, cardColor, 2);

  int degreeX = x + 8 + number.length() * 12 + 1;
  drawDegree(degreeX, 186, valueColor, cardColor);
  drawText(degreeX + 8, 188, "C", valueColor, cardColor, 1);
}

void drawWeatherPanel() {
  drawMetricCard(9, "THAP", (int)round(minTemp), COL_ACCENT, COL_CARD);

  fillRoundRect(85, 169, 70, 36, 7, COL_CARD2);
  drawText(93, 173, "AM", COL_MUTED, COL_CARD2, 1);
  drawText(93, 187, String(humidity) + "%", WHITE, COL_CARD2, 2);

  drawMetricCard(161, "CAO", (int)round(maxTemp), COL_TEMP, COL_CARD);
}

void drawWifiBars(int x, int baseline, uint16_t activeColor, uint16_t inactiveColor) {
  int bars = 0;

  if (WiFi.status() == WL_CONNECTED) {
    int rssi = WiFi.RSSI();
    bars = 1;
    if (rssi > -80) bars = 2;
    if (rssi > -67) bars = 3;
    if (rssi > -55) bars = 4;
  }

  for (int i = 0; i < 4; i++) {
    int h = 3 + i * 3;
    fillRect(x + i * 4, baseline - h, 3, h,
             i < bars ? activeColor : inactiveColor);
  }
}

void drawFooter(bool forceRedraw = false) {
  String state = wifiStateText();
  int roundedWind = (int)round(windSpeed);

  if (!forceRedraw &&
      humidity == lastFooterHumidity &&
      aqi == lastFooterAqi &&
      roundedWind == lastFooterWind &&
      state == lastFooterState) {
    return;
  }

  fillRoundRect(9, 210, 222, 22, 7, COL_CARD);

  String aqiText = aqi >= 0 ? String(aqi) : "--";
  uint16_t aqiColor = COL_MUTED;
  if (aqi >= 0 && aqi <= 50) aqiColor = COL_GOOD;
  else if (aqi <= 100) aqiColor = YELLOW;
  else if (aqi <= 150) aqiColor = ORANGE;
  else if (aqi > 150) aqiColor = RED;

  fillCircle(17, 221, 3, aqiColor);
  drawText(24, 217, "AQI " + aqiText, WHITE, COL_CARD, 1);

  fillCircle(89, 221, 3, COL_ACCENT);
  drawText(96, 217, String(roundedWind) + " KMH", WHITE, COL_CARD, 1);

  if (WiFi.status() == WL_CONNECTED) {
    drawWifiBars(177, 228, COL_GOOD, COL_LINE);
    drawText(195, 217, String(WiFi.RSSI()), COL_GOOD, COL_CARD, 1);
  } else if (setupApActive || apMode) {
    drawText(174, 217, "AP", COL_TEMP, COL_CARD, 1);
    drawWifiBars(210, 228, COL_TEMP, COL_LINE);
  } else {
    drawText(174, 217, "OFF", RED, COL_CARD, 1);
    drawWifiBars(210, 228, RED, COL_LINE);
  }

  lastFooterHumidity = humidity;
  lastFooterAqi = aqi;
  lastFooterWind = roundedWind;
  lastFooterState = state;
}
String aqiLevelText(int value) {
  if (value < 0) return "CHUA CO";
  if (value <= 50) return "TOT";
  if (value <= 100) return "TRUNG BINH";
  if (value <= 150) return "KEM";
  if (value <= 200) return "XAU";
  return "RAT XAU";
}

String windDirectionText(float degrees) {
  const char* dirs[] = {"B", "DB", "D", "DN", "N", "TN", "T", "TB"};
  int index = (int)((degrees + 22.5f) / 45.0f) & 7;
  return String(dirs[index]);
}

void drawDetailRow(int y, String label, String value, uint16_t valueColor) {
  fillRoundRect(13, y, 214, 25, 5, COL_CARD);
  drawText(20, y + 9, label, COL_MUTED, COL_CARD, 1);
  int valueWidth = value.length() * 6;
  drawText(220 - valueWidth, y + 9, value, valueColor, COL_CARD, 1);
}

String lastUpdatedText() {
  if (lastWeatherSuccessAt == 0) return "CHUA CO";

  time_t currentTime = time(nullptr);
  if (currentTime <= 1577836800) {
    unsigned long elapsedMinutes = (millis() - lastWeatherSuccessAt) / 60000UL;
    return String(elapsedMinutes) + " PHUT";
  }

  unsigned long elapsedSeconds = (millis() - lastWeatherSuccessAt) / 1000UL;
  time_t updateTime = currentTime - elapsedSeconds;
  struct tm updateTm;
  localtime_r(&updateTime, &updateTm);

  char buffer[6];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", updateTm.tm_hour, updateTm.tm_min);
  return String(buffer);
}

void drawWeatherDetailPage() {
  fillScreen(COL_BG);
  drawRect(4, 4, 232, 232, COL_LINE);

  fillRoundRect(8, 8, 224, 38, 7, COL_CARD);
  drawText(14, 18, "DU BAO 7 NGAY", COL_ACCENT, COL_CARD, 2);

  time_t now = time(nullptr);
  if (now > 1577836800) {
    struct tm localTm;
    localtime_r(&now, &localTm);
    char clockText[6];
    snprintf(clockText, sizeof(clockText), "%02d:%02d", localTm.tm_hour, localTm.tm_min);
    drawText(190, 23, String(clockText), WHITE, COL_CARD, 1);
  }

  if (!forecastValid) {
    drawCenteredText(105, "DANG TAI DU BAO", COL_MUTED, COL_BG, 1);
    drawCenteredText(130, "VUI LONG CHO...", COL_ACCENT, COL_BG, 1);
    return;
  }

  const int colW = 32;
  for (uint8_t i = 0; i < 7; i++) {
    int x = 8 + i * colW;
    uint16_t bg = (i == 0) ? COL_CARD2 : COL_BG;
    if (i == 0) fillRoundRect(x, 51, 31, 172, 5, bg);

    String day = forecastDayName(i);
    int dayX = x + (31 - (int)day.length() * 6) / 2;
    drawText(dayX, 57, day, i == 0 ? COL_TEMP : COL_ACCENT, bg, 1);

    drawTinyWeatherIcon(forecastCode[i], x + 15, 91);

    String hi = String((int)round(forecastMax[i]));
    String lo = String((int)round(forecastMin[i]));
    String rain = String(forecastRainChance[i]) + "%";

    drawText(x + (31 - (int)hi.length() * 6) / 2, 125, hi, RGB565(255, 139, 167), bg, 1);
    drawText(x + (31 - (int)lo.length() * 6) / 2, 153, lo, RGB565(70, 154, 255), bg, 1);
    drawText(x + max(0, (31 - (int)rain.length() * 6) / 2), 186, rain, RGB565(35, 123, 255), bg, 1);
  }

  drawText(12, 213, "MAX", RGB565(255, 139, 167), COL_BG, 1);
  drawText(53, 213, "MIN", RGB565(70, 154, 255), COL_BG, 1);
  drawText(94, 213, "MUA", RGB565(35, 123, 255), COL_BG, 1);
  String city = storedLocationName();
  if (city.length() > 17) city = city.substring(0, 17);
  int cityWidth = city.length() * 6;
  drawText(max(130, 228 - cityWidth), 213, city, COL_MUTED, COL_BG, 1);
}


uint8_t resolvedMochiExpression() {
  if (mochiExpression != 0) return mochiExpression;

  time_t now = time(nullptr);
  if (now > 1577836800) {
    struct tm localTm;
    localtime_r(&now, &localTm);
    if (localTm.tm_hour >= 22 || localTm.tm_hour < 6) return 3;
  }
  if (aqi > 120) return 5;
  if (weatherCode >= 51 && weatherCode <= 99) return 2;
  if (weatherCode == 0 || weatherCode == 1) return 1;
  return 4;
}

String mochiExpressionName(uint8_t value) {
  switch (value) {
    case 1: return "VUI";
    case 2: return "BUON";
    case 3: return "DANG NGU";
    case 4: return "NGAC NHIEN";
    case 5: return "GIAN";
    case 6: return "YEU THICH";
    default: return "TU DONG";
  }
}

void drawMochiEye(int x, int y, uint8_t expression) {
  if (expression == 3) {
    fillRect(x - 10, y, 20, 3, COL_LINE);
    fillRect(x - 7, y + 3, 14, 2, COL_LINE);
  } else if (expression == 5) {
    fillRect(x - 10, y - 5, 20, 4, COL_LINE);
    fillCircle(x, y + 4, 5, COL_LINE);
  } else if (expression == 4) {
    drawRect(x - 7, y - 7, 14, 14, COL_LINE);
    fillCircle(x, y, 3, COL_LINE);
  } else {
    fillCircle(x, y, 7, COL_LINE);
    fillCircle(x - 2, y - 2, 2, WHITE);
  }
}

void drawMochiStaticShell(uint8_t expression) {
  const uint16_t body = RGB565(245, 244, 239);

  fillScreen(COL_BG);
  drawRect(4, 4, 232, 232, COL_LINE);

  fillRoundRect(9, 9, 222, 38, 7, COL_CARD);
  drawText(15, 19, "MOCHI LIVE", WHITE, COL_CARD, 2);

  String mode = mochiExpression == 0 ? "AUTO" : mochiExpressionName(expression);
  int modeWidth = mode.length() * 6;
  fillRect(150, 15, 75, 24, COL_CARD);
  drawText(220 - modeWidth, 24, mode, COL_ACCENT, COL_CARD, 1);

  // Mochi body is drawn once. Animation only redraws small face/effect regions.
  fillRoundRect(43, 69, 160, 133, 38, COL_SHADOW);
  fillRoundRect(39, 63, 160, 133, 38, body);
  fillCircle(65, 81, 25, body);
  fillCircle(174, 81, 25, body);

  fillCircle(67, 149, 10, RGB565(255, 158, 177));
  fillCircle(171, 149, 10, RGB565(255, 158, 177));

  String caption = mochiExpressionName(expression);
  int captionWidth = caption.length() * 12;
  fillRect(8, 209, 224, 25, COL_BG);
  drawText(max(8, (240 - captionWidth) / 2), 214, caption, COL_TEMP, COL_BG, 2);

  mochiScreenReady = true;
  lastRenderedMochiExpression = expression;
  lastMochiBlink = false;
}

void drawMochiFaceFrame(uint8_t expression, uint8_t frame) {
  const uint16_t body = RGB565(245, 244, 239);
  const int eyeY = 120;
  const int mouthY = 158;
  bool blink = (frame % 30 == 27 || frame % 30 == 28);

  // Clear only dynamic regions. This avoids full-screen flashing.
  fillRect(72, 103, 96, 30, body);   // eyes and eyebrows
  fillRect(99, 148, 42, 30, body);   // mouth
  fillRect(145, 126, 18, 42, body);  // tear area
  fillRect(16, 61, 24, 70, COL_BG);  // left sparkle
  fillRect(199, 55, 27, 82, COL_BG); // right sparkle
  fillRect(146, 48, 50, 48, COL_BG); // floating Z / hearts
  fillRect(44, 48, 28, 35, COL_BG);  // left heart

  if (blink && expression != 3 && expression != 4) {
    fillRoundRect(77, eyeY, 20, 3, 1, COL_LINE);
    fillRoundRect(141, eyeY, 20, 3, 1, COL_LINE);
  } else {
    drawMochiEye(87, eyeY, expression);
    drawMochiEye(151, eyeY, expression);
  }

  if (expression == 1 || expression == 6) {
    fillRect(105, mouthY - 7, 28, 3, COL_LINE);
    fillRect(109, mouthY - 4, 20, 3, COL_LINE);
    fillRect(114, mouthY - 1, 10, 3, COL_LINE);
  } else if (expression == 2) {
    fillRect(110, mouthY + 2, 18, 3, COL_LINE);
    fillRect(106, mouthY - 1, 4, 3, COL_LINE);
    fillRect(128, mouthY - 1, 4, 3, COL_LINE);
    int tearY = 132 + (frame % 8) * 4;
    if (tearY > 160) tearY = 132;
    fillCircle(153, tearY, 3, RGB565(68, 162, 255));
  } else if (expression == 3) {
    drawText(108, mouthY - 7, "Z", COL_ACCENT, body, 2);
    int phase = frame % 12;
    drawText(151 + phase, 86 - phase * 2, "Z", COL_ACCENT, COL_BG, 1);
  } else if (expression == 4) {
    fillCircle(119, mouthY, 9, COL_LINE);
    fillCircle(119, mouthY - 1, 4, body);
  } else if (expression == 5) {
    fillRect(107, mouthY + 1, 24, 4, COL_LINE);
    fillRect(76, eyeY - 10, 22, 3, COL_LINE);
    fillRect(140, eyeY - 10, 22, 3, COL_LINE);
  }

  if (expression == 6) {
    int phase = frame % 10;
    int heartY = 72 - phase * 2;
    fillCircle(54, heartY, 5, RGB565(255, 88, 130));
    fillCircle(61, heartY, 5, RGB565(255, 88, 130));
    fillRect(52, heartY, 12, 7, RGB565(255, 88, 130));
    fillCircle(183, heartY + 12, 4, RGB565(255, 88, 130));
    fillCircle(189, heartY + 12, 4, RGB565(255, 88, 130));
    fillRect(181, heartY + 12, 10, 6, RGB565(255, 88, 130));
  }

  if (expression == 1) {
    uint8_t phase = frame % 16;
    if (phase < 8) {
      drawText(25, 90, "*", COL_TEMP, COL_BG, 2);
      drawText(202, 75, "*", COL_ACCENT, COL_BG, 2);
    } else {
      drawText(23, 78, "*", COL_ACCENT, COL_BG, 1);
      drawText(205, 95, "*", COL_TEMP, COL_BG, 1);
    }
  }

  lastMochiBlink = blink;
}

void drawMochiPage() {
  uint8_t expression = resolvedMochiExpression();

  // A full draw occurs only when entering the page or changing expression.
  if (!mochiScreenReady || lastRenderedMochiExpression != expression) {
    drawMochiStaticShell(expression);
  }

  drawMochiFaceFrame(expression, mochiAnimationFrame);
}

void animateMochiFrame() {
  if (currentPage != 2) return;

  uint8_t expression = resolvedMochiExpression();
  if (!mochiScreenReady || lastRenderedMochiExpression != expression) {
    drawMochiStaticShell(expression);
  }

  mochiAnimationFrame++;
  drawMochiFaceFrame(expression, mochiAnimationFrame);
}


String cryptoIdForSymbol(String symbol) {
  symbol.trim(); symbol.toUpperCase();
  if (symbol == "BTC") return "bitcoin";
  if (symbol == "ETH") return "ethereum";
  if (symbol == "DOGE") return "dogecoin";
  if (symbol == "BNB") return "binancecoin";
  if (symbol == "SOL") return "solana";
  if (symbol == "ADA") return "cardano";
  if (symbol == "XRP") return "ripple";
  if (symbol == "LTC") return "litecoin";
  if (symbol == "DOT") return "polkadot";
  if (symbol == "AVAX") return "avalanche-2";
  if (symbol == "TRX") return "tron";
  if (symbol == "LINK") return "chainlink";
  if (symbol == "MATIC" || symbol == "POL") return "matic-network";
  if (symbol == "SHIB") return "shiba-inu";
  if (symbol == "TON") return "the-open-network";
  return "";
}

String cryptoSymbolAt(uint8_t index) {
  String all = String(cfg.cryptoSymbols);
  int start = 0;
  for (uint8_t i = 0; i <= index; i++) {
    int comma = all.indexOf(',', start);
    String part = comma < 0 ? all.substring(start) : all.substring(start, comma);
    part.trim(); part.toUpperCase();
    if (i == index) return part;
    if (comma < 0) break;
    start = comma + 1;
  }
  return "";
}

String compactUsd(float value) {
  if (value >= 1000.0f) {
    char b[18];
    snprintf(b, sizeof(b), "$%.0f", value);
    return String(b);
  }
  if (value >= 1.0f) return "$" + String(value, 2);
  if (value >= 0.01f) return "$" + String(value, 4);
  return "$" + String(value, 6);
}

void drawCryptoStaticShell() {
  fillScreen(COL_BG);
  drawRect(4, 4, 232, 232, COL_LINE);
  fillRoundRect(8, 8, 224, 38, 7, COL_CARD);
  drawText(18, 20, "CRYPTO LIVE", COL_ACCENT, COL_CARD, 2);
  drawText(183, 22, "USD", COL_MUTED, COL_CARD, 1);

  for (uint8_t i = 0; i < 3; i++) {
    int y = 55 + i * 55;
    uint16_t bg = i == 0 ? COL_CARD2 : COL_CARD;
    fillRoundRect(10, y, 220, 47, 7, bg);
    lastCryptoRenderedSymbol[i] = "";
    lastCryptoRenderedPrice[i] = "";
    lastCryptoRenderedChange[i] = "";
  }

  fillRect(8, 214, 224, 20, COL_BG);
  drawText(14, 224, "LIVE", COL_GOOD, COL_BG, 1);
  cryptoScreenReady = true;
}

void drawCryptoRowValues(uint8_t i, bool force = false) {
  if (i >= 3) return;

  int y = 55 + i * 55;
  uint16_t bg = i == 0 ? COL_CARD2 : COL_CARD;
  String symbol = cryptoQuotes[i].symbol.length() ? cryptoQuotes[i].symbol : cryptoSymbolAt(i);
  if (!symbol.length()) symbol = "---";

  String price = cryptoQuotes[i].valid ? compactUsd(cryptoQuotes[i].usd) : "--";
  String change = cryptoQuotes[i].valid
                    ? ((cryptoQuotes[i].change24h >= 0 ? "+" : "") +
                       String(cryptoQuotes[i].change24h, 2) + "%")
                    : "DANG TAI";

  if (force || symbol != lastCryptoRenderedSymbol[i]) {
    fillRect(17, y + 7, 75, 30, bg);
    drawText(20, y + 10, symbol,
             i == 0 ? COL_TEMP : COL_ACCENT, bg, 2);
    lastCryptoRenderedSymbol[i] = symbol;
  }

  if (force || price != lastCryptoRenderedPrice[i]) {
    fillRect(88, y + 5, 134, 24, bg);
    int priceX = max(88, 220 - (int)price.length() * 12);
    drawText(priceX, y + 8, price, WHITE, bg, 2);
    lastCryptoRenderedPrice[i] = price;
  }

  if (force || change != lastCryptoRenderedChange[i]) {
    fillRect(148, y + 29, 74, 15, bg);
    uint16_t c = COL_MUTED;
    if (cryptoQuotes[i].valid) {
      c = cryptoQuotes[i].change24h >= 0 ? COL_GOOD : RED;
    }
    int changeX = max(148, 220 - (int)change.length() * 6);
    drawText(changeX, y + 31, change, c, bg, 1);
    lastCryptoRenderedChange[i] = change;
  }
}

void drawCryptoFooter(bool force = false) {
  if (!cryptoScreenReady) return;
  if (!force && millis() - lastCryptoClockDraw < 1000UL) return;
  lastCryptoClockDraw = millis();

  fillRect(49, 218, 181, 16, COL_BG);

  String intervalText = String(cfg.cryptoIntervalSeconds) + "S";
  drawText(50, 224, intervalText, COL_MUTED, COL_BG, 1);

  if (lastCryptoSuccessAt > 0) {
    unsigned long age = (millis() - lastCryptoSuccessAt) / 1000UL;
    String ageText = age < 60 ? String(age) + "S AGO" : String(age / 60) + "M AGO";
    int x = max(125, 228 - (int)ageText.length() * 6);
    drawText(x, 224, ageText, age <= cfg.cryptoIntervalSeconds * 2 ? COL_GOOD : COL_TEMP, COL_BG, 1);
  } else {
    drawText(153, 224, "CHO DU LIEU", COL_TEMP, COL_BG, 1);
  }
}

void updateCryptoScreen(bool force = false) {
  if (currentPage != 3) return;
  if (!cryptoScreenReady) drawCryptoStaticShell();
  for (uint8_t i = 0; i < 3; i++) drawCryptoRowValues(i, force);
  drawCryptoFooter(force);
}

void drawCryptoPage() {
  drawCryptoStaticShell();
  updateCryptoScreen(true);
}

bool fetchCryptoPrices() {
  if (WiFi.status() != WL_CONNECTED) return false;

  String ids = "";
  for (uint8_t i = 0; i < 3; i++) {
    String symbol = cryptoSymbolAt(i);
    cryptoQuotes[i].symbol = symbol;
    String id = cryptoIdForSymbol(symbol);
    if (id.length()) {
      if (ids.length()) ids += ",";
      ids += id;
    }
  }
  if (!ids.length()) return false;

  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://api.coingecko.com/api/v3/simple/price?ids=" + ids +
               "&vs_currencies=usd&include_24hr_change=true";
  if (!http.begin(client, url)) return false;
  http.setTimeout(15000);
  http.addHeader("User-Agent", "SmallTV-ESP8266/8.0.10");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("Crypto HTTP error %d\n", code);
    http.end();
    return false;
  }

  String json = http.getString();
  bool any = false;
  for (uint8_t i = 0; i < 3; i++) {
    String id = cryptoIdForSymbol(cryptoQuotes[i].symbol);
    if (!id.length()) continue;
    int obj = objectStart(json, id);
    float usd = 0, change = 0;
    if (obj >= 0 && extractNumberFrom(json, "usd", usd, obj)) {
      extractNumberFrom(json, "usd_24h_change", change, obj);
      cryptoQuotes[i].usd = usd;
      cryptoQuotes[i].change24h = change;
      cryptoQuotes[i].valid = true;
      any = true;
    }
  }
  http.end();
  lastCryptoUpdate = millis();
  if (any) lastCryptoSuccessAt = lastCryptoUpdate;
  if (currentPage == 3) {
    updateCryptoScreen(false);
    forcePageRedraw = false;
  }
  return any;
}

void drawPage(bool forceRedraw = false) {
  if (!forceRedraw && !forcePageRedraw) return;

  if (currentPage != 2) mochiScreenReady = false;
  if (currentPage != 3) cryptoScreenReady = false;

  if (currentPage == 0) {
    drawBaseUI();
    yield();
    drawHeader();
    yield();
    drawClock(true);
    yield();
    drawWeatherPanel();
    yield();
    drawFooter(true);
  } else if (currentPage == 1) {
    drawWeatherDetailPage();
  } else if (currentPage == 2) {
    drawMochiPage();
  } else {
    drawCryptoPage();
  }

  forcePageRedraw = false;
  yield();
}

void nextPage() {
  currentPage = (currentPage + 1) % 4;
  mochiScreenReady = false;
  cryptoScreenReady = false;
  forcePageRedraw = true;
  lastPageChange = millis();
}

void previousPage() {
  currentPage = currentPage == 0 ? 3 : currentPage - 1;
  mochiScreenReady = false;
  cryptoScreenReady = false;
  forcePageRedraw = true;
  lastPageChange = millis();
}


void redrawClockPageAfterStyleChange() {
  if (currentPage != 0) {
    forcePageRedraw = true;
    return;
  }

  lastHourTens = -1;
  lastHourOnes = -1;
  lastMinuteTens = -1;
  lastMinuteOnes = -1;
  lastDateText = "";

  drawBaseUI();
  yield();
  drawHeader();
  yield();
  drawClock(true);
  yield();
  drawWeatherPanel();
  yield();
  drawFooter(true);
  yield();
}

void drawAll() {
  waitingScreenShown = false;
  drawPage(true);
}

// ---------------- Weather JSON helper ----------------
/*
  Open-Meteo includes keys such as "temperature_2m" in current_units,
  current, daily_units and daily. Searching from the beginning can therefore
  hit the unit string ("°C") instead of the numeric value. These helpers parse
  only inside the requested JSON object.
*/
int objectStart(const String& json, const String& objectName) {
  int p = json.indexOf("\"" + objectName + "\"");
  if (p < 0) return -1;
  return json.indexOf('{', p);
}

bool extractNumberFrom(const String& json, const String& key,
                       float& value, int startAt) {
  if (startAt < 0) return false;

  int p = json.indexOf("\"" + key + "\"", startAt);
  if (p < 0) return false;

  p = json.indexOf(':', p);
  if (p < 0) return false;
  p++;

  while (p < (int)json.length() &&
         (json[p] == ' '  ||
          json[p] == '\n' ||
          json[p] == '\r' ||
          json[p] == '\t' ||
          json[p] == '[')) {
    p++;
  }

  int e = p;
  while (e < (int)json.length() &&
         (isDigit(json[e]) ||
          json[e] == '-' ||
          json[e] == '+' ||
          json[e] == '.')) {
    e++;
  }

  if (e == p) return false;

  value = json.substring(p, e).toFloat();
  return true;
}

bool extractStringFrom(const String& json, const String& key,
                       String& value, int startAt) {
  if (startAt < 0) return false;
  int p = json.indexOf("\"" + key + "\"", startAt);
  if (p < 0) return false;
  p = json.indexOf(':', p);
  if (p < 0) return false;
  p++;
  while (p < (int)json.length() && isspace((unsigned char)json[p])) p++;
  if (p >= (int)json.length() || json[p] != '\"') return false;
  p++;

  value = "";
  bool escaped = false;
  while (p < (int)json.length()) {
    char c = json[p++];
    if (escaped) {
      if (c == 'n') value += '\n';
      else if (c == 'r') value += '\r';
      else if (c == 't') value += '\t';
      else value += c;
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '\"') {
      return true;
    } else {
      value += c;
    }
  }
  return false;
}

String urlEncode(const String& input) {
  const char hex[] = "0123456789ABCDEF";
  String output;
  output.reserve(input.length() * 3);
  for (size_t i = 0; i < input.length(); i++) {
    uint8_t c = (uint8_t)input[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      output += (char)c;
    } else {
      output += '%';
      output += hex[c >> 4];
      output += hex[c & 0x0F];
    }
  }
  return output;
}

String normalizeLocationQuery(String query) {
  query.trim();
  String key = query;
  key.toLowerCase();
  key.replace(" ", "");
  key.replace("-", "");
  key.replace("_", "");

  // Common Vietnamese place names typed without spaces/accents.
  if (key == "phutho") return "Phu Tho";
  if (key == "viettri") return "Viet Tri";
  if (key == "hanoi") return "Ha Noi";
  if (key == "hochiminh" || key == "tphcm" || key == "saigon") return "Ho Chi Minh City";
  if (key == "danang") return "Da Nang";
  if (key == "haiphong") return "Hai Phong";
  if (key == "cantho") return "Can Tho";
  if (key == "hanam") return "Ha Nam";
  if (key == "bacninh") return "Bac Ninh";
  if (key == "bacgiang") return "Bac Giang";
  if (key == "ninhbinh") return "Ninh Binh";
  if (key == "namdinh") return "Nam Dinh";
  if (key == "thaibinh") return "Thai Binh";
  if (key == "haiduong") return "Hai Duong";
  if (key == "hungyen") return "Hung Yen";
  if (key == "vinhphuc") return "Vinh Phuc";
  if (key == "thainguyen") return "Thai Nguyen";
  if (key == "tuyenquang") return "Tuyen Quang";
  if (key == "laocai") return "Lao Cai";
  if (key == "yenbai") return "Yen Bai";
  if (key == "hoabinh") return "Hoa Binh";
  if (key == "sonla") return "Son La";
  if (key == "dienbien") return "Dien Bien";
  if (key == "laichau") return "Lai Chau";
  if (key == "caobang") return "Cao Bang";
  if (key == "backan") return "Bac Kan";
  if (key == "langson") return "Lang Son";
  if (key == "quangninh") return "Quang Ninh";
  if (key == "thanhhoa") return "Thanh Hoa";
  if (key == "nghean") return "Nghe An";
  if (key == "hatinh") return "Ha Tinh";
  if (key == "quangbinh") return "Quang Binh";
  if (key == "quangtri") return "Quang Tri";
  if (key == "thuathienhue" || key == "hue") return "Hue";
  if (key == "quangnam") return "Quang Nam";
  if (key == "quangngai") return "Quang Ngai";
  if (key == "binhdinh") return "Binh Dinh";
  if (key == "phuyen") return "Phu Yen";
  if (key == "khanhhoa" || key == "nhatrang") return "Khanh Hoa";
  if (key == "ninhthuan") return "Ninh Thuan";
  if (key == "binhthuan") return "Binh Thuan";
  if (key == "kontum") return "Kon Tum";
  if (key == "gialai") return "Gia Lai";
  if (key == "daklak") return "Dak Lak";
  if (key == "daknong") return "Dak Nong";
  if (key == "lamdong" || key == "dalat") return "Lam Dong";
  if (key == "binhphuoc") return "Binh Phuoc";
  if (key == "tayninh") return "Tay Ninh";
  if (key == "binhduong") return "Binh Duong";
  if (key == "dongnai") return "Dong Nai";
  if (key == "bariavungtau" || key == "vungtau") return "Ba Ria Vung Tau";
  if (key == "longan") return "Long An";
  if (key == "tiengiang") return "Tien Giang";
  if (key == "bentre") return "Ben Tre";
  if (key == "travinh") return "Tra Vinh";
  if (key == "vinhlong") return "Vinh Long";
  if (key == "dongthap") return "Dong Thap";
  if (key == "angiang") return "An Giang";
  if (key == "kiengiang" || key == "phuquoc") return "Kien Giang";
  if (key == "haugiang") return "Hau Giang";
  if (key == "soctrang") return "Soc Trang";
  if (key == "baclieu") return "Bac Lieu";
  if (key == "camau") return "Ca Mau";
  return query;
}

bool geocodeLocation(const String& query, String& resolvedCity,
                     float& latitude, float& longitude) {
  if (query.length() < 2) return false;

  // Fast, deterministic aliases for the main locations normally entered
  // without Vietnamese accents. These work even when the geocoding service
  // is temporarily unavailable.
  String key = query;
  key.trim();
  key.toUpperCase();
  key.replace(" ", "");
  key.replace("-", "");
  key.replace("_", "");

  if (key == "HANOI" || key == "HANOI") {
    resolvedCity = "HANOI"; latitude = 21.02850f; longitude = 105.85420f; return true;
  }
  if (key == "PHUTHO") {
    resolvedCity = "PHUTHO"; latitude = 21.32270f; longitude = 105.40190f; return true;
  }
  if (key == "VIETTRI") {
    resolvedCity = "VIETTRI"; latitude = 21.32270f; longitude = 105.40190f; return true;
  }
  if (key == "HCM" || key == "TPHCM" || key == "SAIGON" || key == "HOCHIMINH") {
    resolvedCity = "HCM"; latitude = 10.82310f; longitude = 106.62970f; return true;
  }
  if (key == "DANANG") {
    resolvedCity = "DANANG"; latitude = 16.05440f; longitude = 108.20220f; return true;
  }
  if (key == "HAIPHONG") {
    resolvedCity = "HAIPHONG"; latitude = 20.84490f; longitude = 106.68810f; return true;
  }
  if (key == "CANTHO") {
    resolvedCity = "CANTHO"; latitude = 10.04520f; longitude = 105.74690f; return true;
  }

  if (WiFi.status() != WL_CONNECTED) return false;

  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://geocoding-api.open-meteo.com/v1/search?name=" +
               urlEncode(normalizeLocationQuery(query)) + "&count=5&language=vi&format=json&countryCode=VN";

  if (!http.begin(client, url)) return false;
  http.setTimeout(15000);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Geocoding HTTP error %d\n", httpCode);
    http.end();
    return false;
  }

  String json = http.getString();
  int resultsPos = json.indexOf("\"results\"");
  int firstResult = resultsPos >= 0 ? json.indexOf('{', resultsPos) : -1;
  String name, admin1;
  bool ok = firstResult >= 0 &&
            extractStringFrom(json, "name", name, firstResult) &&
            extractNumberFrom(json, "latitude", latitude, firstResult) &&
            extractNumberFrom(json, "longitude", longitude, firstResult);
  if (ok) {
    extractStringFrom(json, "admin1", admin1, firstResult);
    resolvedCity = name;
    if (admin1.length() && admin1 != name) resolvedCity += ", " + admin1;
  }
  http.end();
  return ok;
}


bool extractFloatArray7(const String& json, const String& key, float values[7], int startAt) {
  int keyPos = json.indexOf("\"" + key + "\"", max(0, startAt));
  if (keyPos < 0) return false;
  int open = json.indexOf('[', keyPos);
  int close = json.indexOf(']', open);
  if (open < 0 || close < 0) return false;
  int pos = open + 1;
  for (uint8_t i = 0; i < 7; i++) {
    while (pos < close && (json[pos] == ' ' || json[pos] == ',' || json[pos] == '\n' || json[pos] == '\r')) pos++;
    if (pos >= close) return false;
    int end = pos;
    while (end < close && json[end] != ',') end++;
    String token = json.substring(pos, end);
    token.trim();
    if (!token.length() || token == "null") return false;
    values[i] = token.toFloat();
    pos = end + 1;
  }
  return true;
}

bool extractIntArray7(const String& json, const String& key, int values[7], int startAt) {
  float temp[7];
  if (!extractFloatArray7(json, key, temp, startAt)) return false;
  for (uint8_t i = 0; i < 7; i++) values[i] = (int)round(temp[i]);
  return true;
}

String forecastDayName(uint8_t offset) {
  static const char* names[] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};
  time_t now = time(nullptr);
  if (now <= 1577836800) return String("D") + String(offset + 1);
  struct tm localTm;
  localtime_r(&now, &localTm);
  return String(names[(localTm.tm_wday + offset) % 7]);
}

void drawTinyWeatherIcon(int code, int cx, int cy) {
  uint16_t blue = RGB565(83, 166, 255);
  uint16_t rain = RGB565(40, 132, 255);
  uint16_t sun = RGB565(255, 199, 50);
  if (code <= 1) {
    fillCircle(cx, cy, 6, sun);
    fillRect(cx - 1, cy - 10, 2, 3, sun);
    fillRect(cx - 1, cy + 8, 2, 3, sun);
    fillRect(cx - 10, cy - 1, 3, 2, sun);
    fillRect(cx + 8, cy - 1, 3, 2, sun);
    return;
  }
  fillCircle(cx - 5, cy, 5, blue);
  fillCircle(cx + 1, cy - 3, 7, blue);
  fillCircle(cx + 7, cy + 1, 5, blue);
  fillRect(cx - 9, cy, 20, 6, blue);
  if (code >= 51) {
    fillRect(cx - 6, cy + 9, 2, 5, rain);
    fillRect(cx, cy + 9, 2, 5, rain);
    fillRect(cx + 6, cy + 9, 2, 5, rain);
  }
}

bool fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClient client;
  HTTPClient http;

  String url =
    "http://api.open-meteo.com/v1/forecast?latitude=" + String(cfg.latitude) +
    "&longitude=" + String(cfg.longitude) +
    "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,wind_direction_10m,precipitation,surface_pressure,cloud_cover" +
    "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max" +
    "&forecast_days=7&timezone=auto";

  Serial.println(url);

  if (!http.begin(client, url)) {
    Serial.println("HTTP begin failed");
    return false;
  }

  http.setTimeout(12000);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String json = http.getString();

    float t, feels, hum, code, minT, maxT, wind, windDir, rain, press, clouds;

    int currentPos = objectStart(json, "current");
    int dailyPos = objectStart(json, "daily");

    bool ok =
      extractNumberFrom(json, "temperature_2m", t, currentPos) &&
      extractNumberFrom(json, "relative_humidity_2m", hum, currentPos) &&
      extractNumberFrom(json, "apparent_temperature", feels, currentPos) &&
      extractNumberFrom(json, "weather_code", code, currentPos) &&
      extractNumberFrom(json, "wind_speed_10m", wind, currentPos) &&
      extractNumberFrom(json, "wind_direction_10m", windDir, currentPos) &&
      extractNumberFrom(json, "precipitation", rain, currentPos) &&
      extractNumberFrom(json, "surface_pressure", press, currentPos) &&
      extractNumberFrom(json, "cloud_cover", clouds, currentPos) &&
      extractNumberFrom(json, "temperature_2m_max", maxT, dailyPos) &&
      extractNumberFrom(json, "temperature_2m_min", minT, dailyPos);

    float dailyMax[7], dailyMin[7];
    int dailyCode[7], dailyRain[7];
    bool forecastOk =
      extractFloatArray7(json, "temperature_2m_max", dailyMax, dailyPos) &&
      extractFloatArray7(json, "temperature_2m_min", dailyMin, dailyPos) &&
      extractIntArray7(json, "weather_code", dailyCode, dailyPos) &&
      extractIntArray7(json, "precipitation_probability_max", dailyRain, dailyPos);

    if (ok) {
      currentTemp = t;
      apparentTemp = feels;
      humidity = (int)round(hum);
      weatherCode = (int)round(code);
      minTemp = minT;
      maxTemp = maxT;
      windSpeed = wind;
      windDirection = windDir;
      precipitation = rain;
      pressure = press;
      cloudCover = clouds;
      weatherName = weatherTextForCode(weatherCode);
      if (forecastOk) {
        for (uint8_t i = 0; i < 7; i++) {
          forecastMax[i] = dailyMax[i];
          forecastMin[i] = dailyMin[i];
          forecastCode[i] = dailyCode[i];
          forecastRainChance[i] = constrain(dailyRain[i], 0, 100);
        }
        forecastValid = true;
      }
      weatherOk = true;
      lastWeatherSuccessAt = millis();
      lastWeatherUpdate = lastWeatherSuccessAt;

      Serial.printf("Weather %.1fC, humidity %d%%, wind %.1f km/h, rain %.1f mm, code %d\n",
                    currentTemp, humidity, windSpeed, precipitation, weatherCode);

      // The main loop/final boot redraw will display the new values.
      // Avoid a large nested redraw while HTTPClient is still unwinding.
      yield();
      http.end();
      return true;
    } else {
      Serial.println("Weather parse failed");
    }
  } else {
    Serial.printf("Weather HTTP error %d\n", httpCode);
  }

  http.end();
  return false;
}


bool fetchAirQuality() {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClient client;
  HTTPClient http;

  String url =
    "http://air-quality-api.open-meteo.com/v1/air-quality?latitude=" +
    String(cfg.latitude) +
    "&longitude=" + String(cfg.longitude) +
    "&current=us_aqi&timezone=auto";

  Serial.println(url);

  if (!http.begin(client, url)) {
    Serial.println("AQI HTTP begin failed");
    return false;
  }

  http.setTimeout(12000);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String json = http.getString();
    int currentPos = objectStart(json, "current");
    float value = 0;

    if (extractNumberFrom(json, "us_aqi", value, currentPos)) {
      aqi = (int)round(value);
      lastAqiSuccessAt = millis();
      lastAirQualityUpdate = lastAqiSuccessAt;
      Serial.printf("AQI %d\n", aqi);
      yield();
      http.end();
      return true;
    } else {
      Serial.println("AQI parse failed");
    }
  } else {
    Serial.printf("AQI HTTP error %d\n", httpCode);
  }

  http.end();
  return false;
}


uint16_t htmlHexTo565(String value, uint16_t fallback) {
  value.trim();
  value.replace("%23", "#");
  if (value.startsWith("#")) value.remove(0, 1);
  value.toUpperCase();

  if (value.length() != 6) return fallback;

  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    bool valid = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
    if (!valid) return fallback;
  }

  unsigned long rgb = strtoul(value.c_str(), nullptr, 16);
  uint8_t r = (rgb >> 16) & 0xFF;
  uint8_t g = (rgb >> 8) & 0xFF;
  uint8_t b = rgb & 0xFF;
  return RGB565(r, g, b);
}

String rgb565ToHtml(uint16_t color) {
  uint8_t r = ((color >> 11) & 0x1F) * 255 / 31;
  uint8_t g = ((color >> 5) & 0x3F) * 255 / 63;
  uint8_t b = (color & 0x1F) * 255 / 31;
  char buffer[8];
  snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", r, g, b);
  return String(buffer);
}

String jsonEscape(const String& input) {
  String output;
  output.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    if (c == '"' || c == '\\') output += '\\';
    if (c == '\n') output += "\\n";
    else if (c != '\r') output += c;
  }
  return output;
}

String boolJson(bool value) {
  return value ? "true" : "false";
}


String wifiStateText() {
  if (WiFi.status() == WL_CONNECTED) return "CONNECTED";
  if (setupApActive || apMode) return "SETUP AP";
  return "OFFLINE";
}

bool internetLikelyAvailable() {
  return WiFi.status() == WL_CONNECTED;
}

String activeIpText() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  if (setupApActive || apMode) return WiFi.softAPIP().toString();
  return "0.0.0.0";
}

String timeForMillis(unsigned long eventMillis) {
  if (eventMillis == 0) return "Chưa có";
  time_t now = time(nullptr);
  if (now <= 1577836800) return String((millis() - eventMillis) / 60000UL) + " phút trước";
  unsigned long elapsedSeconds = (millis() - eventMillis) / 1000UL;
  time_t eventTime = now - elapsedSeconds;
  struct tm tmEvent;
  localtime_r(&eventTime, &tmEvent);
  char buffer[6];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", tmEvent.tm_hour, tmEvent.tm_min);
  return String(buffer);
}

String nextUpdateText(unsigned long lastAttemptMillis, uint8_t intervalMinutes) {
  if (WiFi.status() != WL_CONNECTED) return "Đang chờ mạng";
  unsigned long intervalMs = (unsigned long)intervalMinutes * 60000UL;
  unsigned long dueMillis = lastAttemptMillis + intervalMs;
  long remainingMs = (long)(dueMillis - millis());
  if (lastAttemptMillis == 0 || remainingMs <= 0) return "Sắp cập nhật";
  time_t now = time(nullptr);
  if (now <= 1577836800) return String((remainingMs + 59999L) / 60000L) + " phút nữa";
  time_t dueTime = now + remainingMs / 1000L;
  struct tm tmDue;
  localtime_r(&dueTime, &tmDue);
  char buffer[6];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", tmDue.tm_hour, tmDue.tm_min);
  return String(buffer);
}

// ---------------- Web config / mobile app ----------------
String mobileAppPage() {
  String h;
  h.reserve(22000);
  h += F(R"HTML(
<!doctype html><html lang="vi"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0b121b"><link rel="manifest" href="/manifest.json">
<title>SmallTV Control</title>
<style>
:root{--bg:#08111a;--panel:#111d28;--card:#152431;--card2:#101c27;--line:#294153;--text:#f5f8fc;--muted:#91a9bc;--purple:#7848ff;--blue:#378cff;--green:#20c88a;--orange:#ffb64b;--danger:#ff5d69}
*{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;background:radial-gradient(circle at 50% -10%,#172a3b 0,#09131d 45%,#060d14 100%);color:var(--text);font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;min-height:100vh}
button,input,select{font:inherit}.app{max-width:980px;margin:auto;min-height:100vh;padding:14px 14px 36px}.top{position:sticky;top:0;z-index:10;margin-bottom:15px;padding:15px 16px 12px;background:rgba(10,20,30,.92);border:1px solid var(--line);border-radius:0 0 18px 18px;backdrop-filter:blur(14px);box-shadow:0 12px 35px rgba(0,0,0,.28)}
.brand{display:flex;align-items:center;justify-content:space-between;gap:15px}.brandline{display:flex;gap:12px;align-items:center}.logo{width:44px;height:44px;border-radius:14px;display:grid;place-items:center;background:linear-gradient(145deg,var(--purple),#3f86ff);box-shadow:0 0 24px rgba(108,73,255,.35);font-size:23px}.brand h1{font-size:22px;margin:0}.net{font-size:12px;color:var(--muted);margin-top:3px}.dot{width:11px;height:11px;border-radius:50%;background:var(--green);box-shadow:0 0 14px var(--green)}
.tabs{display:flex;overflow:auto;gap:8px;margin-top:14px;padding-bottom:2px;scrollbar-width:none}.tabs::-webkit-scrollbar{display:none}.tabs button{white-space:nowrap;background:#152532;border:1px solid #294153;color:#bcd0df;padding:10px 14px;border-radius:12px;font-weight:750}.tabs button.activeTab{background:linear-gradient(135deg,#6d3dff,#3f7cff);color:white;border-color:transparent;box-shadow:0 8px 22px rgba(86,66,255,.3)}
section{display:none}section.active{display:block}.pagehead{display:flex;align-items:center;gap:12px;margin:10px 4px 16px}.pageicon{font-size:24px;color:#8d68ff}.pagehead h2{font-size:22px;margin:0}.pagehead p{margin:3px 0 0;color:var(--muted);font-size:13px}.card{background:linear-gradient(145deg,rgba(22,38,51,.98),rgba(15,28,39,.98));border:1px solid var(--line);border-radius:18px;padding:17px;margin-top:13px;box-shadow:0 15px 38px rgba(0,0,0,.2)}.cardTitle{font-weight:850;font-size:16px;margin-bottom:12px;display:flex;align-items:center;gap:9px}.subcard{background:rgba(8,18,28,.48);border:1px solid #23394a;border-radius:15px;padding:14px}.hero{display:grid;grid-template-columns:1fr auto;gap:12px;align-items:center}.temp{font-size:52px;font-weight:900;letter-spacing:-2px}.weatherEmoji{font-size:62px;filter:drop-shadow(0 6px 12px rgba(0,0,0,.35))}
.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:11px}.metric{min-width:0}.metric span{color:var(--muted);font-size:12px}.metric b{display:block;font-size:20px;margin-top:5px;overflow-wrap:anywhere}.stat{background:#0e1b26;border:1px solid #243c4e;border-radius:14px;padding:14px}.accent{color:#60a0ff!important}.good{color:#39d99d!important}.warn{color:#ffc15c!important}.muted{color:var(--muted);font-size:13px;line-height:1.5}
label{display:block;color:#c9d7e2;font-size:13px;margin:13px 0 6px}input,select{width:100%;border:1px solid #365268;border-radius:12px;padding:13px 14px;background:#0b1721;color:#fff;outline:0;transition:.2s}input:focus,select:focus{border-color:#7651ff;box-shadow:0 0 0 3px rgba(118,81,255,.15)}input[readonly]{color:#37d79d;font-weight:800;background:#0c1b24}.inputrow{display:grid;grid-template-columns:1fr auto;gap:9px;align-items:end}.toggle{display:flex;align-items:center;justify-content:space-between;gap:15px;border-bottom:1px solid rgba(55,79,96,.55);padding:14px 0}.toggle:last-child{border-bottom:0}.toggle input{width:22px;height:22px;accent-color:#7045ff}.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:16px}button,.btn{border:1px solid #304b60;border-radius:12px;padding:11px 16px;background:#1c3243;color:#fff;font-weight:800;cursor:pointer;transition:.16s}button:active{transform:scale(.98)}button.primary{border:0;background:linear-gradient(135deg,#7545ff,#3d79ff);box-shadow:0 9px 22px rgba(92,67,255,.24)}button.success{border:0;background:linear-gradient(135deg,#139b73,#27c897)}button.danger{border-color:#70404a;color:#ff9aa3;background:#301b24}.wide{width:100%}
.info{margin-top:13px;padding:13px 14px;border-radius:13px;border:1px solid rgba(121,77,255,.65);border-left:4px solid #8c55ff;background:rgba(84,54,163,.09);color:#cbd6e4;font-size:13px;line-height:1.6}.locationStatus{margin-top:12px;padding:10px 12px;border-radius:11px;background:#0d1b27;border:1px solid #294155;color:#91abc0;font-size:13px}.coordHead{display:flex;align-items:center;justify-content:space-between}.badge{padding:6px 10px;border-radius:20px;font-size:12px;font-weight:800;background:rgba(27,196,137,.13);color:#3bdba1;border:1px solid rgba(27,196,137,.28)}
.mochiGrid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.mochiBtn{min-height:76px;font-size:15px;background:#102130}.mochiBtn span{display:block;font-size:28px;margin-bottom:4px}.systemActions button{flex:1 1 180px}.toast{position:fixed;left:50%;bottom:24px;transform:translateX(-50%);background:#eefaf4;color:#173d2b;padding:11px 18px;border-radius:30px;display:none;z-index:30;box-shadow:0 12px 35px rgba(0,0,0,.35);font-weight:750;max-width:90%;text-align:center}
@media(min-width:680px){.app{padding:16px 20px 45px}.grid.four{grid-template-columns:repeat(4,1fr)}.grid.three{grid-template-columns:repeat(3,1fr)}.mochiGrid{grid-template-columns:repeat(4,1fr)}}
@media(max-width:520px){.app{padding:8px 9px 28px}.top{margin-left:-2px;margin-right:-2px}.card{padding:14px}.inputrow{grid-template-columns:1fr}.inputrow button{width:100%}.temp{font-size:45px}.brand h1{font-size:19px}.logo{width:39px;height:39px}.actions button{flex:1 1 auto}.grid.mobileOne{grid-template-columns:1fr}}
</style></head><body><div class="app">
<div class="top"><div class="brand"><div class="brandline"><div class="logo">📺</div><div><h1>SmallTV Control</h1><div id="netline" class="net">Đang kiểm tra Wi-Fi...</div></div></div><span id="online" class="dot"></span></div>
<div class="tabs"><button data-tab="home" onclick="tab('home',this)">🏠 Tổng quan</button><button data-tab="display" onclick="tab('display',this)">🎨 Hiển thị</button><button data-tab="weather" onclick="tab('weather',this)">📍 Thời tiết</button><button data-tab="mochi" onclick="tab('mochi',this)">😊 Mochi</button><button data-tab="crypto" onclick="tab('crypto',this)">₿ Crypto</button><button data-tab="network" onclick="tab('network',this)">📶 Wi-Fi</button><button data-tab="system" onclick="tab('system',this)">⚙️ Hệ thống</button></div></div>

<section id="home" class="active"><div class="pagehead"><div class="pageicon">⌂</div><div><h2>TỔNG QUAN</h2><p>Trạng thái nhanh của SmallTV</p></div></div>
<div class="card hero"><div><div class="muted" id="cityView">Đang tải...</div><div class="temp"><span id="temp">--</span>°C</div><div id="desc">--</div></div><div class="weatherEmoji">🌤️</div></div>
<div class="grid four"><div class="stat metric"><span>Độ ẩm</span><b id="hum" class="accent">--%</b></div><div class="stat metric"><span>Tốc độ gió</span><b id="wind" class="accent">--</b></div><div class="stat metric"><span>Chất lượng khí</span><b id="aqi" class="good">--</b></div><div class="stat metric"><span>Trang đang hiện</span><b id="page">--</b></div></div>
<div class="card"><div class="cardTitle">📡 Kết nối hiện tại</div><div class="grid four"><div class="metric"><span>Trạng thái</span><b id="wifiState">--</b></div><div class="metric"><span>SSID</span><b id="ssidView">--</b></div><div class="metric"><span>Địa chỉ IP</span><b id="ipView">--</b></div><div class="metric"><span>Tín hiệu</span><b id="rssiView">--</b></div></div></div>
<div class="card"><div class="cardTitle">🖥️ Chuyển trang nhanh</div><div class="actions"><button class="primary" onclick="go('/api/page?value=0')">🕒 Đồng hồ</button><button class="primary" onclick="go('/api/page?value=1')">🌦️ Thời tiết</button><button class="primary" onclick="go('/api/page?value=2')">😊 Mochi</button><button class="primary" onclick="go('/api/page?value=3')">₿ Crypto</button><button onclick="go('/api/next')">Trang kế tiếp</button></div></div></section>

<section id="display"><div class="pagehead"><div class="pageicon">✦</div><div><h2>HIỂN THỊ</h2><p>Tùy chỉnh màu sắc và cách chuyển trang</p></div></div>
<div class="card"><div class="cardTitle">🎨 Màu giao diện đồng hồ</div><div class="grid mobileOne"><div><label>Màu chữ đồng hồ</label><input id="clockColor" type="color" oninput="$('clockHex').textContent=this.value.toUpperCase()"><div class="muted">Mã màu: <b id="clockHex">--</b></div></div><div><label>Màu ngày tháng</label><input id="dateColor" type="color" oninput="$('dateHex').textContent=this.value.toUpperCase()"><div class="muted">Mã màu: <b id="dateHex">--</b></div></div></div></div>
<div class="card"><div class="cardTitle">🔄 Cách trình chiếu</div><div class="toggle"><div><b>Tự động chuyển trang</b><div class="muted">Lần lượt Đồng hồ, Thời tiết, Mochi và Crypto</div></div><input id="autoPage" type="checkbox"></div><label>Chu kỳ chuyển trang</label><select id="interval"><option>5</option><option>10</option><option>12</option><option>15</option><option>30</option><option>60</option></select><div class="toggle"><div><b>Nhấp nháy dấu hai chấm</b></div><input id="colonBlink" type="checkbox"></div><div class="toggle"><div><b>Định dạng 12 giờ</b></div><input id="use12Hour" type="checkbox"></div><div class="actions"><button class="primary" onclick="saveDisplay()">💾 Lưu cài đặt hiển thị</button></div></div></section>

<section id="weather"><div class="pagehead"><div class="pageicon">⌖</div><div><h2>THỜI TIẾT</h2><p>Nhập vị trí để xem thời tiết chính xác</p></div></div>
<div class="card"><div class="cardTitle" style="color:#9a78ff">📍 VỊ TRÍ</div><label>Nhập địa điểm</label><div class="inputrow"><input id="city" placeholder="Ví dụ: Hà Nam, Hà Nội, Đà Nẵng, Phú Quốc..." oninput="weatherDirty=true;locationValid=false"><button class="primary" type="button" onclick="findLocation()">🔎 Tìm kiếm</button></div><div class="actions"><button type="button" onclick="usePhoneLocation()">📱 Dùng vị trí điện thoại</button><button type="button" onclick="findLocation()">🎯 Tự tìm tọa độ</button></div><div id="locationResult" class="locationStatus">Chưa chọn vị trí mới.</div><div class="info"><b>💡 Có thể nhập mọi địa danh trên toàn quốc:</b> tỉnh, thành phố, quận, huyện, phường, xã hoặc tên địa điểm cụ thể.<br>Ví dụ: “Hà Nam”, “TP.HCM”, “Nha Trang”, “Phú Quốc”, “Bắc Ninh”, “Thủ Đức”...</div></div>
<div class="card"><div class="coordHead"><div class="cardTitle" style="color:#39d99d;margin:0">⌖ TỌA ĐỘ ĐÃ CHỌN</div><span class="badge">✓ Vị trí hợp lệ</span></div><div class="grid mobileOne"><div><label>Latitude (vĩ độ)</label><input id="lat" readonly></div><div><label>Longitude (kinh độ)</label><input id="lon" readonly></div></div></div>
<div class="card"><div class="cardTitle" style="color:#62a0ff">⚙️ CÀI ĐẶT KHÁC</div><label>UTC offset (phút)</label><input id="utc" type="number"><div class="muted">Giờ Việt Nam: UTC+7 = 420 phút</div><div class="grid mobileOne"><div><label>Cập nhật dự báo 7 ngày (1–60 phút)</label><input id="weatherInterval" type="number" min="1" max="60"></div><div><label>Cập nhật AQI (1–60 phút)</label><input id="aqiInterval" type="number" min="1" max="60"></div></div></div>
<div class="card"><div class="cardTitle" style="color:#4e93ff">🔄 TRẠNG THÁI CẬP NHẬT</div><div class="grid mobileOne"><div class="subcard metric"><span>🌤️ Thời tiết lần cuối</span><b id="weatherLast">Chưa có</b><span>Lần tiếp theo: <b id="weatherNext" class="accent">--</b></span></div><div class="subcard metric"><span>🍃 AQI lần cuối</span><b id="aqiLast">Chưa có</b><span>Lần tiếp theo: <b id="aqiNext" class="accent">--</b></span></div></div><div class="actions"><button class="primary" onclick="saveWeather()">💾 Lưu cấu hình</button><button class="success" onclick="fetchNow()">⟳ Cập nhật ngay</button></div></div></section>

<section id="mochi"><div class="pagehead"><div class="pageicon">☺</div><div><h2>MOCHI BIỂU CẢM</h2><p>Chọn tâm trạng cho màn hình SmallTV</p></div></div><div class="card"><div class="cardTitle">😊 Chọn biểu cảm</div><div class="mochiGrid"><button class="mochiBtn" onclick="setMochi(0)"><span>✨</span>Tự động</button><button class="mochiBtn" onclick="setMochi(1)"><span>😊</span>Vui</button><button class="mochiBtn" onclick="setMochi(2)"><span>😢</span>Buồn</button><button class="mochiBtn" onclick="setMochi(3)"><span>😴</span>Ngủ</button><button class="mochiBtn" onclick="setMochi(4)"><span>😮</span>Ngạc nhiên</button><button class="mochiBtn" onclick="setMochi(5)"><span>😠</span>Giận</button><button class="mochiBtn" onclick="setMochi(6)"><span>😍</span>Yêu thích</button><button class="primary mochiBtn" onclick="showMochi()"><span>📺</span>Hiện ngay</button></div><div class="info">Ở chế độ tự động: ban đêm Mochi ngủ, trời mưa Mochi buồn và AQI xấu Mochi sẽ khó chịu.</div></div></section>


<section id="crypto"><div class="pagehead"><div class="pageicon">₿</div><div><h2>GIÁ TIỀN MÃ HÓA</h2><p>Giá được cập nhật và hiển thị trực tiếp trên màn hình SmallTV</p></div></div>
<div class="card"><div class="cardTitle">📈 Đồng tiền hiển thị</div>
<label>Nhập tối đa 3 mã coin, cách nhau bằng dấu phẩy</label>
<input id="cryptoSymbols" maxlength="31" placeholder="BTC,ETH,DOGE">
<div class="info">Hỗ trợ: BTC, ETH, DOGE, BNB, SOL, ADA, XRP, LTC, DOT, AVAX, TRX, LINK, MATIC/POL, SHIB, TON.</div>
<label>Làm mới gần realtime (15–300 giây)</label><input id="cryptoInterval" type="number" min="15" max="300" step="5">
<div class="grid three"><div class="metric"><span class="muted">Coin 1</span><b id="coin0">--</b></div><div class="metric"><span class="muted">Coin 2</span><b id="coin1">--</b></div><div class="metric"><span class="muted">Coin 3</span><b id="coin2">--</b></div></div>
<div class="actions"><button class="primary" onclick="saveCrypto()">💾 Lưu cấu hình</button><button onclick="fetchCryptoNow()">🔄 Cập nhật ngay</button><button onclick="go('/api/page?value=3')">📺 Hiện trên thiết bị</button></div>
</div></section>

<section id="network"><div class="pagehead"><div class="pageicon">⌁</div><div><h2>WI-FI</h2><p>Cấu hình mạng cho thiết bị</p></div></div><div class="card"><div class="cardTitle">📶 Mạng Wi-Fi</div><label>Tên mạng (SSID)</label><input id="ssid" placeholder="Nhập tên Wi-Fi"><label>Mật khẩu</label><input id="password" type="password" placeholder="Để trống nếu không muốn thay đổi"><div class="info">Khi không kết nối được Wi-Fi, thiết bị tự mở mạng <b>SmallTV-Setup</b>, mật khẩu <b>12345678</b>.</div><div class="actions"><button class="primary" onclick="saveNetwork()">💾 Lưu và khởi động lại</button></div></div></section>

<section id="system"><div class="pagehead"><div class="pageicon">⚙</div><div><h2>HỆ THỐNG</h2><p>Thông tin thiết bị và bảo trì</p></div></div><div class="card"><div class="cardTitle">📊 Thông tin thiết bị</div><div class="grid three"><div class="stat metric"><span>Địa chỉ IP</span><b id="ip">--</b></div><div class="stat metric"><span>Tín hiệu Wi-Fi</span><b id="rssi">--</b></div><div class="stat metric"><span>Bộ nhớ trống</span><b id="heap">--</b></div><div class="stat metric"><span>Thời gian hoạt động</span><b id="uptime">--</b></div><div class="stat metric"><span>Firmware</span><b id="firmware">--</b></div><div class="stat metric"><span>Ngày build</span><b id="build">--</b></div></div></div><div class="card"><div class="cardTitle">🛠️ Công cụ hệ thống</div><div class="actions systemActions"><button onclick="refresh()">⟳ Làm mới</button><button onclick="enableSetupAp()">📡 Bật Setup AP 10 phút</button><button class="primary" onclick="location.href='/update'">⬆ OTA Firmware</button><button class="danger" onclick="reboot()">⏻ Khởi động lại</button></div></div></section>
</div><div id="toast" class="toast"></div>
<script>
const $=id=>document.getElementById(id);let weatherDirty=false;let locationValid=false;
function tab(id,btn){document.querySelectorAll('section').forEach(x=>x.classList.remove('active'));$(id).classList.add('active');document.querySelectorAll('.tabs button').forEach(x=>x.classList.remove('activeTab'));(btn||document.querySelector('[data-tab="'+id+'"]'))?.classList.add('activeTab');localStorage.setItem('smalltvTab',id)}
function toast(t){$('toast').textContent=t;$('toast').style.display='block';setTimeout(()=>$('toast').style.display='none',2200)}
async function req(url,opt={}){const r=await fetch(url,opt);if(!r.ok)throw Error(await r.text());return r.json()}
async function go(url){await req(url);await refresh();toast('Đã chuyển trang')}
async function refresh(){try{const s=await req('/api/status');$('online').style.background='#20c88a';$('cityView').textContent=s.city;$('temp').textContent=s.temperature.toFixed(1);$('desc').textContent=s.weather;$('hum').textContent=s.humidity+'%';$('wind').textContent=s.wind_speed.toFixed(1)+' km/h';$('aqi').textContent=s.aqi;$('page').textContent=s.page===0?'Đồng hồ':(s.page===1?'Thời tiết':(s.page===2?'Mochi':'Crypto'));if(!weatherDirty){$('city').value=s.city;$('lat').value=s.latitude;$('lon').value=s.longitude;$('utc').value=s.utc_offset;locationValid=true}$('weatherInterval').value=s.weather_interval;$('aqiInterval').value=s.aqi_interval;$('weatherLast').textContent=s.weather_last;$('weatherNext').textContent=s.weather_next;$('aqiLast').textContent=s.aqi_last;$('aqiNext').textContent=s.aqi_next;$('autoPage').checked=s.auto_page;$('interval').value=s.page_interval;$('colonBlink').checked=s.colon_blink;$('use12Hour').checked=s.use_12_hour;$('clockColor').value=s.clock_color;$('dateColor').value=s.date_color;$('clockHex').textContent=s.clock_color.toUpperCase();$('dateHex').textContent=s.date_color.toUpperCase();$('ssid').value=s.saved_ssid;$('ip').textContent=s.ip;$('rssi').textContent=s.rssi+' dBm';$('heap').textContent=Math.round(s.free_heap/1024)+' KB';$('firmware').textContent=s.firmware||'--';$('build').textContent=s.build||'--';$('uptime').textContent=Math.floor(s.uptime/60)+' phút';$('wifiState').textContent=s.wifi_state;$('ssidView').textContent=s.connected_ssid||s.ap_ssid||'--';$('ipView').textContent=s.ip;$('rssiView').textContent=s.ap_mode?'AP':s.rssi+' dBm';$('netline').textContent=s.ap_mode?('AP '+s.ap_ssid+' · 192.168.4.1'):(s.wifi_state+' · '+(s.connected_ssid||s.saved_ssid)+' · '+s.ip);
if($('cryptoSymbols')){$('cryptoSymbols').value=s.crypto_symbols||'BTC,ETH,DOGE';$('cryptoInterval').value=s.crypto_interval||30;for(let i=0;i<3;i++){const q=(s.crypto||[])[i];$('coin'+i).textContent=q&&q.valid?(q.symbol+' $'+q.usd+' ('+(q.change>=0?'+':'')+q.change+'%)'):(q?q.symbol:'--')}}}catch(e){$('online').style.background='#ff5d69'}}
async function post(url,data){return req(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)})}
async function saveDisplay(){const payload={autoPage:$('autoPage').checked?1:0,interval:$('interval').value,colonBlink:$('colonBlink').checked?1:0,use12Hour:$('use12Hour').checked?1:0,clockColor:$('clockColor').value.toUpperCase(),dateColor:$('dateColor').value.toUpperCase()};const result=await post('/api/display',payload);if(result.clock_color)$('clockColor').value=result.clock_color;if(result.date_color)$('dateColor').value=result.date_color;toast('Đã lưu cài đặt hiển thị');await refresh()}
async function findLocation(showToast=true){
 const typedLocation=$('city').value.trim();
 if(typedLocation.length<2){toast('Hãy nhập tên địa điểm');return false}
 if(showToast)toast('Đang tìm tọa độ...');
 try{
  const r=await post('/api/geocode',{q:typedLocation});
  // Luôn giữ nguyên đúng nội dung người dùng đã nhập.
  $('city').value=typedLocation;
  $('lat').value=Number(r.latitude).toFixed(5);
  $('lon').value=Number(r.longitude).toFixed(5);
  $('locationResult').textContent='✓ Đã tìm thấy: '+r.city+' ('+$('lat').value+', '+$('lon').value+')';
  weatherDirty=true;locationValid=true;
  if(showToast)toast('Đã tìm thấy vị trí');
  return true;
 }catch(e){
  // Khi không tìm thấy cũng không thay đổi nội dung ô nhập.
  $('city').value=typedLocation;
  locationValid=false;
  $('locationResult').textContent='Không tìm thấy địa điểm. Hãy kiểm tra lại tên hoặc dùng vị trí điện thoại.';
  toast('Không tìm thấy địa điểm hoặc chưa có Internet');
  return false
 }
}


function usePhoneLocation(){
 if(!navigator.geolocation){toast('Trình duyệt không hỗ trợ GPS');return}
 toast('Đang lấy vị trí điện thoại...');
 navigator.geolocation.getCurrentPosition(
  p=>{
   $('lat').value=p.coords.latitude.toFixed(5);
   $('lon').value=p.coords.longitude.toFixed(5);
   if(!$('city').value.trim())$('city').value='Vị trí hiện tại';
   $('locationResult').textContent='✓ Đã lấy GPS điện thoại ('+$('lat').value+', '+$('lon').value+')';
   weatherDirty=true;locationValid=true;toast('Đã lấy vị trí hiện tại')
  },
  ()=>toast('Không lấy được GPS. Hãy cấp quyền vị trí cho trình duyệt.'),
  {enableHighAccuracy:true,timeout:12000}
 )
}

async function saveWeather(){
 if(!locationValid){
  if(!await findLocation(false))return
 }
 await post('/api/weather',{
  city:$('city').value,
  lat:$('lat').value,
  lon:$('lon').value,
  utc:$('utc').value,
  weatherInterval:$('weatherInterval').value,
  aqiInterval:$('aqiInterval').value
 });
 weatherDirty=false;locationValid=true;
 toast('Đã lưu vị trí và cấu hình thời tiết');
 await refresh()
}

async function fetchNow(){
 toast('Đang cập nhật dữ liệu...');
 try{
  const r=await post('/api/weather/fetch',{});
  toast(r.ok?'Đã cập nhật thời tiết và AQI':'Cập nhật chưa hoàn tất');
  await refresh()
 }catch(e){toast('Không thể cập nhật thời tiết')}
}

async function setMochi(expression){
 await post('/api/mochi',{expression});
 toast('Đã đổi biểu cảm Mochi');
 await go('/api/page?value=2')
}

async function showMochi(){
 await go('/api/page?value=2')
}

async function saveCrypto(){
 let symbols=$('cryptoSymbols').value.trim().toUpperCase().replace(/\s+/g,'');
 if(!symbols)symbols='BTC,ETH,DOGE';
 await post('/api/crypto',{
  symbols:symbols,
  interval:$('cryptoInterval').value
 });
 $('cryptoSymbols').value=symbols;
 toast('Đã lưu danh sách coin');
 await refresh()
}

async function fetchCryptoNow(){
 toast('Đang cập nhật giá coin...');
 try{
  const r=await post('/api/crypto/fetch',{});
  toast(r.ok?'Đã cập nhật giá coin':'Chưa lấy được giá coin');
  await refresh()
 }catch(e){toast('Không thể cập nhật giá coin')}
}

async function saveNetwork(){
 const ssid=$('ssid').value.trim();
 if(!ssid){toast('Hãy nhập tên Wi-Fi');return}
 await post('/api/network',{ssid:ssid,password:$('password').value});
 toast('Đã lưu, thiết bị sẽ khởi động lại')
}

async function enableSetupAp(){
 await post('/api/setup-ap',{});
 toast('SmallTV-Setup đã bật trong 10 phút');
 await refresh()
}

async function reboot(){
 if(!confirm('Khởi động lại SmallTV?'))return;
 await post('/api/reboot',{});
 toast('Đang khởi động lại...')
}

const savedTab=localStorage.getItem('smalltvTab')||'home';
tab(savedTab);
refresh();
setInterval(refresh,5000);
if('serviceWorker' in navigator){
 navigator.serviceWorker.register('/sw.js').catch(()=>{})
}
</script></body></html>
)HTML");
  return h;
}

void handleGeocodeRequest() {
  String query = server.arg("q");
  if (!query.length() && server.hasArg("name")) query = server.arg("name");
  query.trim();
  if (query.length() < 2) {
    server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"missing_query\"}");
    return;
  }
  String resolvedCity;
  float latitude = 0.0f;
  float longitude = 0.0f;
  if (!geocodeLocation(query, resolvedCity, latitude, longitude)) {
    server.send(404, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"location_not_found_or_api_error\"}");
    return;
  }
  String json = "{\"ok\":true,";
  json += "\"city\":\"" + jsonEscape(resolvedCity) + "\",";
  json += "\"latitude\":" + String(latitude, 5) + ",";
  json += "\"longitude\":" + String(longitude, 5) + "}";
  server.send(200, "application/json; charset=utf-8", json);
}

void sendJsonOk() {
  server.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
}


String otaUpdatePage() {
  String h;
  h.reserve(9000);
  h += F(R"HTML(
<!doctype html><html lang="vi"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0b1117"><title>SmallTV OTA Update</title>
<style>
:root{--bg:#081019;--panel:#121d27;--card:#192733;--line:#2c4354;--text:#f5f8fb;--muted:#9bb0bf;--purple:#7048e8;--green:#1fb65a;--blue:#2d7fe8;--danger:#ff5f67}
*{box-sizing:border-box}body{margin:0;min-height:100vh;background:radial-gradient(circle at top,#172737 0,#0b131c 48%,#070d13 100%);color:var(--text);font-family:system-ui,-apple-system,Segoe UI,sans-serif;padding:18px}
.wrap{max-width:760px;margin:auto}.head{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px}.head h1{font-size:22px;margin:0}.back{color:#cbd8e2;text-decoration:none;background:#1a2935;border:1px solid var(--line);padding:9px 13px;border-radius:11px;font-weight:700}
.card{background:rgba(25,39,51,.96);border:1px solid var(--line);border-radius:20px;padding:18px;margin:14px 0;box-shadow:0 18px 45px rgba(0,0,0,.28)}
.title{display:flex;gap:13px;align-items:center}.icon{width:48px;height:48px;border-radius:14px;display:grid;place-items:center;font-size:25px}.purple{background:rgba(112,72,232,.18)}.green{background:rgba(31,182,90,.16)}h2{font-size:21px;margin:0 0 3px}.sub{color:var(--muted);font-size:13px}
.drop{margin-top:16px;border:1px dashed #5b7080;border-radius:15px;padding:14px;display:grid;gap:11px;background:#101a23}.fileline{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.choose{display:inline-flex;align-items:center;justify-content:center;cursor:pointer;border-radius:11px;padding:11px 15px;font-weight:800}.choose.p{background:var(--purple)}.choose.g{background:var(--green)}input[type=file]{display:none}.filename{color:#c8d4dc;font-size:14px;overflow-wrap:anywhere;flex:1;min-width:180px}.upload{width:100%;border:0;border-radius:11px;padding:12px 15px;color:white;font-weight:800;font-size:15px;cursor:pointer}.upload.p{background:linear-gradient(135deg,#8257ef,#6134d7)}.upload.g{background:linear-gradient(135deg,#28c766,#149647)}.upload:disabled{opacity:.45;cursor:not-allowed}
.note{background:rgba(45,127,232,.12);border:1px solid rgba(89,157,238,.45);color:#cfe4ff;padding:13px 15px;border-radius:14px;font-size:14px;line-height:1.5}
.overlay{position:fixed;inset:0;background:rgba(4,9,14,.88);backdrop-filter:blur(8px);display:none;align-items:center;justify-content:center;padding:20px;z-index:99}.overlay.show{display:flex}.progressCard{width:min(430px,100%);background:#15222d;border:1px solid #355064;border-radius:22px;padding:24px;text-align:center;box-shadow:0 25px 80px rgba(0,0,0,.5)}.spinner{width:54px;height:54px;margin:0 auto 16px;border:5px solid #2c4050;border-top-color:#6f98ff;border-radius:50%;animation:spin .9s linear infinite}.check{font-size:48px;display:none}.bar{height:13px;background:#0b1218;border-radius:999px;overflow:hidden;margin:18px 0 10px}.fill{height:100%;width:0;background:linear-gradient(90deg,#6f4ee8,#2e8cf0,#22bd67);transition:width .15s}.percent{font-size:32px;font-weight:900}.status{color:var(--muted);margin-top:5px;line-height:1.5}.error{color:#ff9da3}
@keyframes spin{to{transform:rotate(360deg)}}@media(min-width:650px){.drop{grid-template-columns:1fr auto;align-items:center}.upload{width:auto;min-width:190px}.fileline{min-width:0}}
</style></head><body><div class="wrap">
<div class="head"><div><h1>SmallTV Update</h1><div class="sub">Cập nhật thiết bị qua Wi-Fi</div></div><a class="back" href="/">← Quay lại</a></div>
<div class="card"><div class="title"><div class="icon purple">⚙️</div><div><h2>Firmware</h2><div class="sub">Cập nhật chương trình điều khiển thiết bị (.bin)</div></div></div>
<form class="drop" id="fwForm"><div class="fileline"><label class="choose p" for="fwFile">↑ Chọn tệp</label><input id="fwFile" name="update" type="file" accept=".bin,application/octet-stream" required><span id="fwName" class="filename">Không có tệp nào được chọn</span></div><button id="fwBtn" class="upload p" type="submit" disabled>Update Firmware</button></form></div>
<div class="card"><div class="title"><div class="icon green">📁</div><div><h2>FileSystem</h2><div class="sub">Cập nhật giao diện web, font hoặc hình ảnh (.bin)</div></div></div>
<form class="drop" id="fsForm"><div class="fileline"><label class="choose g" for="fsFile">↑ Chọn tệp</label><input id="fsFile" name="update" type="file" accept=".bin,application/octet-stream" required><span id="fsName" class="filename">Không có tệp nào được chọn</span></div><button id="fsBtn" class="upload g" type="submit" disabled>Update FileSystem</button></form></div>
<div class="note"><b>Lưu ý:</b> Không tắt nguồn hoặc đóng trang trong khi cập nhật. Sau khi hoàn tất, thiết bị sẽ tự khởi động lại.</div>
</div>
<div id="overlay" class="overlay"><div class="progressCard"><div id="spinner" class="spinner"></div><div id="check" class="check">✅</div><div id="progressTitle"><b>Đang cập nhật...</b></div><div class="bar"><div id="fill" class="fill"></div></div><div id="percent" class="percent">0%</div><div id="status" class="status">Đang chuẩn bị gửi firmware tới SmallTV</div></div></div>
<script>
function bind(formId,fileId,nameId,btnId,label){
 const form=document.getElementById(formId),file=document.getElementById(fileId),name=document.getElementById(nameId),btn=document.getElementById(btnId);
 file.addEventListener('change',()=>{const ok=file.files.length>0;name.textContent=ok?file.files[0].name:'Không có tệp nào được chọn';btn.disabled=!ok});
 form.addEventListener('submit',e=>{e.preventDefault();if(!file.files.length)return;upload(file.files[0],label)});
}
function upload(file,label){
 const overlay=document.getElementById('overlay'),fill=document.getElementById('fill'),percent=document.getElementById('percent'),status=document.getElementById('status'),spinner=document.getElementById('spinner'),check=document.getElementById('check'),title=document.getElementById('progressTitle');
 overlay.classList.add('show');fill.style.width='0%';percent.textContent='0%';status.className='status';status.textContent='Đang tải '+label+' lên thiết bị...';spinner.style.display='block';check.style.display='none';title.innerHTML='<b>Đang cập nhật...</b>';
 const data=new FormData();data.append('update',file,file.name);const xhr=new XMLHttpRequest();xhr.open('POST','/update',true);
 xhr.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.min(99,Math.round(e.loaded/e.total*100));fill.style.width=p+'%';percent.textContent=p+'%';status.textContent='Đã gửi '+p+'% — vui lòng không tắt nguồn';}};
 xhr.onload=()=>{if(xhr.status>=200&&xhr.status<300){fill.style.width='100%';percent.textContent='100%';spinner.style.display='none';check.style.display='block';title.innerHTML='<b>Cập nhật thành công</b>';status.textContent='SmallTV đang khởi động lại. Trang chính sẽ mở lại sau vài giây.';setTimeout(()=>location.href='/',9000)}else fail('Cập nhật thất bại: '+(xhr.responseText||xhr.status));};
 xhr.onerror=()=>{if(Number(percent.textContent.replace('%',''))>=95){fill.style.width='100%';percent.textContent='100%';spinner.style.display='none';check.style.display='block';title.innerHTML='<b>Đã gửi xong firmware</b>';status.textContent='Kết nối bị ngắt do SmallTV đang khởi động lại. Trang chính sẽ mở lại sau vài giây.';setTimeout(()=>location.href='/',9000)}else fail('Mất kết nối khi đang cập nhật. Hãy kiểm tra nguồn và thử lại.');};
 function fail(msg){spinner.style.display='none';title.innerHTML='<b>Cập nhật không thành công</b>';status.className='status error';status.textContent=msg;}
 xhr.send(data);
}
bind('fwForm','fwFile','fwName','fwBtn','firmware');bind('fsForm','fsFile','fsName','fsBtn','FileSystem');
</script></body></html>
)HTML");
  return h;
}

void startWebServer() {
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    server.send(200, "text/html; charset=utf-8", mobileAppPage());
  });

  server.on("/manifest.json", HTTP_GET, []() {
    server.send(200, "application/manifest+json",
      "{\"name\":\"SmallTV Control\",\"short_name\":\"SmallTV\","
      "\"start_url\":\"/\",\"display\":\"standalone\","
      "\"background_color\":\"#0b1117\",\"theme_color\":\"#111820\"}");
  });

  server.on("/sw.js", HTTP_GET, []() {
    server.send(200, "application/javascript",
      "self.addEventListener('install',e=>self.skipWaiting());"
      "self.addEventListener('activate',e=>self.clients.claim());");
  });

  server.on("/api/page", HTTP_GET, []() {
    currentPage = constrain(server.arg("value").toInt(), 0, 3);
    mochiScreenReady = false;
    cryptoScreenReady = false;
    forcePageRedraw = true;
    lastPageChange = millis();
    server.send(200, "application/json",
                String("{\"ok\":true,\"page\":") + currentPage + "}");
  });

  server.on("/api/next", HTTP_GET, []() {
    nextPage();
    server.send(200, "application/json",
                String("{\"ok\":true,\"page\":") + currentPage + "}");
  });

  server.on("/api/prev", HTTP_GET, []() {
    previousPage();
    server.send(200, "application/json",
                String("{\"ok\":true,\"page\":") + currentPage + "}");
  });



  server.on("/api/crypto", HTTP_POST, []() {
    String symbols = server.arg("symbols");
    symbols.trim(); symbols.toUpperCase();
    symbols.replace(" ", "");
    if (!symbols.length()) symbols = "BTC,ETH,DOGE";
    uint8_t commas = 0;
    for (size_t i = 0; i < symbols.length(); i++) if (symbols[i] == ',') commas++;
    if (commas > 2) {
      int cut = symbols.indexOf(',', symbols.indexOf(',') + 1);
      cut = symbols.indexOf(',', cut + 1);
      if (cut > 0) symbols = symbols.substring(0, cut);
    }
    strlcpy(cfg.cryptoSymbols, symbols.c_str(), sizeof(cfg.cryptoSymbols));
    cfg.cryptoIntervalSeconds = constrain(server.arg("interval").toInt(), 15, 300);
    saveSettings();
    bool ok = fetchCryptoPrices();
    String json = String("{\"ok\":") + boolJson(ok) + "}";
    server.send(200, "application/json", json);
  });

  server.on("/api/crypto/fetch", HTTP_POST, []() {
    bool ok = fetchCryptoPrices();
    String json = String("{\"ok\":") + boolJson(ok) + "}";
    server.send(200, "application/json", json);
  });

  server.on("/api/mochi", HTTP_POST, []() {
    mochiExpression = constrain(server.arg("expression").toInt(), 0, 6);
    currentPage = 2;
    forcePageRedraw = true;
    lastPageChange = millis();
    String json = "{\"ok\":true,\"expression\":" + String(mochiExpression) + "}";
    server.send(200, "application/json", json);
  });

  server.on("/api/display", HTTP_POST, []() {
    cfg.autoPage = server.arg("autoPage").toInt() ? 1 : 0;
    cfg.pageIntervalSeconds = constrain(server.arg("interval").toInt(), 5, 60);
    cfg.colonBlink = server.arg("colonBlink").toInt() ? 1 : 0;
    cfg.use12Hour = server.arg("use12Hour").toInt() ? 1 : 0;

    String clockValue = server.hasArg("clockColor")
                          ? server.arg("clockColor")
                          : server.arg("clock_color");
    String dateValue = server.hasArg("dateColor")
                         ? server.arg("dateColor")
                         : server.arg("date_color");

    cfg.clockColor = htmlHexTo565(clockValue, cfg.clockColor);
    cfg.dateColor = htmlHexTo565(dateValue, cfg.dateColor);
    saveSettings();

    redrawClockPageAfterStyleChange();
    forcePageRedraw = false;

    String json = "{";
    json += "\"ok\":true,";
    json += "\"clock_color\":\"" + rgb565ToHtml(cfg.clockColor) + "\",";
    json += "\"date_color\":\"" + rgb565ToHtml(cfg.dateColor) + "\"";
    json += "}";
    server.send(200, "application/json", json);
  });

  // Web UI uses POST; GET is provided for direct browser testing.
  server.on("/api/geocode", HTTP_POST, handleGeocodeRequest);
  server.on("/api/geocode", HTTP_GET, handleGeocodeRequest);

  server.on("/api/weather", HTTP_POST, []() {
    String submittedLocation = server.arg("city");
    submittedLocation.trim();
    copyUtf8Safe(cfg.locationName, sizeof(cfg.locationName), submittedLocation);
    // Keep the legacy short field for LCD/backward compatibility only.
    copyUtf8Safe(cfg.city, sizeof(cfg.city), submittedLocation);
    strlcpy(cfg.latitude, server.arg("lat").c_str(), sizeof(cfg.latitude));
    strlcpy(cfg.longitude, server.arg("lon").c_str(), sizeof(cfg.longitude));
    cfg.utcOffsetMinutes = server.arg("utc").toInt();
    cfg.weatherIntervalMinutes = constrain(server.arg("weatherInterval").toInt(), 1, 60);
    cfg.aqiIntervalMinutes = constrain(server.arg("aqiInterval").toInt(), 1, 60);
    saveSettings();
    configTime(cfg.utcOffsetMinutes * 60, 0, "pool.ntp.org", "time.nist.gov");
    if (!apMode) {
      fetchWeather();
      fetchAirQuality();
    }
    forcePageRedraw = true;
    sendJsonOk();
  });

  server.on("/api/weather/fetch", HTTP_POST, []() {
    bool weatherDone = fetchWeather();
    bool aqiDone = fetchAirQuality();
    forcePageRedraw = true;
    String json = "{\"ok\":" + boolJson(weatherDone && aqiDone) +
                  ",\"weather_ok\":" + boolJson(weatherDone) +
                  ",\"aqi_ok\":" + boolJson(aqiDone) + "}";
    server.send(200, "application/json", json);
  });

  server.on("/api/network", HTTP_POST, []() {
    strlcpy(cfg.ssid, server.arg("ssid").c_str(), sizeof(cfg.ssid));
    if (server.arg("password").length() > 0) {
      strlcpy(cfg.password, server.arg("password").c_str(), sizeof(cfg.password));
    }
    saveSettings();
    sendJsonOk();
    delay(500);
    ESP.restart();
  });

  server.on("/api/setup-ap", HTTP_POST, []() {
    enableSetupAP("manual request");
    setupApStartedAt = millis();
    sendJsonOk();
  });

  server.on("/api/reboot", HTTP_POST, []() {
    sendJsonOk();
    delay(400);
    ESP.restart();
  });

  server.on("/api/status", HTTP_GET, []() {
    String json;
    json.reserve(850);
    json = "{";
    json += "\"firmware\":\"" + String(FW_VERSION) + "\",";
    json += "\"build\":\"" + String(FW_BUILD) + "\",";
    json += "\"page\":" + String(currentPage) + ",";
    json += "\"mochi_expression\":" + String(mochiExpression) + ",";
    json += "\"crypto_symbols\":\"" + jsonEscape(String(cfg.cryptoSymbols)) + "\",";
    json += "\"crypto_interval\":" + String(cfg.cryptoIntervalSeconds) + ",";
    json += "\"crypto\":[";
    for (uint8_t i = 0; i < 3; i++) {
      if (i) json += ",";
      json += "{\"symbol\":\"" + jsonEscape(cryptoQuotes[i].symbol) + "\",";
      json += "\"usd\":" + String(cryptoQuotes[i].usd, 6) + ",";
      json += "\"change\":" + String(cryptoQuotes[i].change24h, 2) + ",";
      json += "\"valid\":" + boolJson(cryptoQuotes[i].valid) + "}";
    }
    json += "],";
    json += "\"city\":\"" + jsonEscape(storedLocationName()) + "\",";
    json += "\"latitude\":\"" + jsonEscape(String(cfg.latitude)) + "\",";
    json += "\"longitude\":\"" + jsonEscape(String(cfg.longitude)) + "\",";
    json += "\"utc_offset\":" + String(cfg.utcOffsetMinutes) + ",";
    json += "\"weather_interval\":" + String(cfg.weatherIntervalMinutes) + ",";
    json += "\"aqi_interval\":" + String(cfg.aqiIntervalMinutes) + ",";
    json += "\"weather_last\":\"" + jsonEscape(timeForMillis(lastWeatherSuccessAt)) + "\",";
    json += "\"weather_next\":\"" + jsonEscape(nextUpdateText(lastWeatherUpdate, cfg.weatherIntervalMinutes)) + "\",";
    json += "\"aqi_last\":\"" + jsonEscape(timeForMillis(lastAqiSuccessAt)) + "\",";
    json += "\"aqi_next\":\"" + jsonEscape(nextUpdateText(lastAirQualityUpdate, cfg.aqiIntervalMinutes)) + "\",";
    json += "\"temperature\":" + String(currentTemp, 1) + ",";
    json += "\"feels_like\":" + String(apparentTemp, 1) + ",";
    json += "\"humidity\":" + String(humidity) + ",";
    json += "\"wind_speed\":" + String(windSpeed, 1) + ",";
    json += "\"wind_direction\":" + String(windDirection, 0) + ",";
    json += "\"pressure\":" + String(pressure, 0) + ",";
    json += "\"cloud_cover\":" + String(cloudCover, 0) + ",";
    json += "\"precipitation\":" + String(precipitation, 1) + ",";
    json += "\"aqi\":" + String(aqi) + ",";
    json += "\"weather\":\"" + jsonEscape(weatherName) + "\",";
    json += "\"saved_ssid\":\"" + jsonEscape(String(cfg.ssid)) + "\",";
    json += "\"connected_ssid\":\"" + jsonEscape(apMode ? String("") : WiFi.SSID()) + "\",";
    json += "\"ap_ssid\":\"SmallTV-Setup\",";
    json += "\"wifi_state\":\"" + wifiStateText() + "\",";
    json += "\"ap_mode\":" + boolJson(apMode) + ",";
    json += "\"internet\":" + boolJson(internetLikelyAvailable()) + ",";
    json += "\"gateway\":\"" + (apMode ? String("192.168.4.1") : WiFi.gatewayIP().toString()) + "\",";
    json += "\"mac\":\"" + WiFi.macAddress() + "\",";
    json += "\"auto_page\":" + boolJson(cfg.autoPage) + ",";
    json += "\"page_interval\":" + String(cfg.pageIntervalSeconds) + ",";
    json += "\"colon_blink\":" + boolJson(cfg.colonBlink) + ",";
    json += "\"use_12_hour\":" + boolJson(cfg.use12Hour) + ",";
    json += "\"clock_color\":\"" + rgb565ToHtml(cfg.clockColor) + "\",";
    json += "\"date_color\":\"" + rgb565ToHtml(cfg.dateColor) + "\",";
    json += "\"ip\":\"" + (apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\",";
    json += "\"rssi\":" + String(apMode ? 0 : WiFi.RSSI()) + ",";
    json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"uptime\":" + String(millis() / 1000UL);
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/api/network", HTTP_GET, []() {
    String json;
    json.reserve(420);
    json = "{";
    json += "\"state\":\"" + wifiStateText() + "\",";
    json += "\"ap_mode\":" + boolJson(apMode) + ",";
    json += "\"saved_ssid\":\"" + jsonEscape(String(cfg.ssid)) + "\",";
    json += "\"connected_ssid\":\"" + jsonEscape(apMode ? String("") : WiFi.SSID()) + "\",";
    json += "\"ap_ssid\":\"SmallTV-Setup\",";
    json += "\"ip\":\"" + (apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\",";
    json += "\"gateway\":\"" + (apMode ? String("192.168.4.1") : WiFi.gatewayIP().toString()) + "\",";
    json += "\"rssi\":" + String(apMode ? 0 : WiFi.RSSI()) + ",";
    json += "\"mac\":\"" + WiFi.macAddress() + "\",";
    json += "\"internet\":" + boolJson(internetLikelyAvailable());
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/status", HTTP_GET, []() {
    server.sendHeader("Location", "/api/status");
    server.send(302, "text/plain", "");
  });


  server.on("/update", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    server.send(200, "text/html; charset=utf-8", otaUpdatePage());
  });

  Update.onStart([]() {
    drawOtaStartScreen();
  });

  Update.onProgress([](size_t current, size_t total) {
    drawOtaProgress(current, total);
  });

  Update.onEnd([]() {
    drawOtaProgress(100, 100);
    drawOtaSuccessScreen();
  });

  Update.onError([](int errorCode) {
    drawOtaErrorScreen(errorCode);
  });

  httpUpdater.setup(&server, "/update");
  server.begin();
}


void drawCenteredText(int y, String text, uint16_t color, uint16_t bg, int scale) {
  int width = text.length() * 6 * scale;
  int x = (240 - width) / 2;
  if (x < 4) x = 4;
  drawText(x, y, text, color, bg, scale);
}


void drawOtaStartScreen() {
  otaInProgress = true;
  otaLastPercent = 255;
  otaLastDrawAt = 0;

  fillScreen(COL_BG);
  drawRect(4, 4, 232, 232, COL_LINE);

  fillRoundRect(10, 10, 220, 38, 7, COL_CARD);
  drawCenteredText(20, "OTA UPDATE", WHITE, COL_CARD, 2);

  fillRoundRect(14, 58, 212, 160, 8, COL_CARD);
  drawCenteredText(75, "DANG CAP NHAT", COL_ACCENT, COL_CARD, 2);
  drawCenteredText(105, "0%", WHITE, COL_CARD, 3);

  fillRoundRect(26, 151, 188, 18, 8, COL_BG);
  drawRect(26, 151, 188, 18, COL_LINE);

  drawCenteredText(185, "KHONG TAT NGUON", COL_TEMP, COL_CARD, 1);
  drawCenteredText(202, "VUI LONG CHO...", COL_MUTED, COL_CARD, 1);
  yield();
}

void drawOtaProgress(size_t current, size_t total) {
  if (!otaInProgress) drawOtaStartScreen();
  if (total == 0) return;

  size_t calculatedPercent = current >= total
                               ? (size_t)100
                               : (current / (total / (size_t)100 + (size_t)1));
  if (calculatedPercent > 100) calculatedPercent = 100;
  uint8_t percent = (uint8_t)calculatedPercent;
  unsigned long now = millis();

  // Limit LCD work during upload so the network stream remains stable.
  if (percent == otaLastPercent) return;
  if (percent < 100 && otaLastPercent != 255 &&
      percent < otaLastPercent + 2 && now - otaLastDrawAt < 250UL) return;

  otaLastPercent = percent;
  otaLastDrawAt = now;

  fillRect(70, 100, 100, 30, COL_CARD);
  String percentText = String(percent) + "%";
  drawCenteredText(105, percentText, WHITE, COL_CARD, 3);

  fillRoundRect(29, 154, 182, 12, 5, COL_BG);
  int progressWidth = (182 * percent) / 100;
  if (progressWidth > 0) {
    fillRoundRect(29, 154, progressWidth, 12, 5,
                  percent < 100 ? COL_ACCENT : COL_GOOD);
  }

  String sizeText = String(current / 1024UL) + " / " +
                    String(total / 1024UL) + " KB";
  fillRect(55, 174, 130, 10, COL_CARD);
  drawCenteredText(174, sizeText, COL_MUTED, COL_CARD, 1);
  yield();
}

void drawOtaSuccessScreen() {
  otaInProgress = false;
  fillScreen(COL_BG);
  drawRect(4, 4, 232, 232, COL_GOOD);

  fillRoundRect(12, 12, 216, 40, 8, COL_CARD);
  drawCenteredText(23, "OTA UPDATE", WHITE, COL_CARD, 2);

  fillCircle(120, 94, 28, COL_GOOD);
  fillRect(105, 94, 10, 5, WHITE);
  fillRect(111, 99, 8, 5, WHITE);
  fillRect(117, 94, 5, 10, WHITE);
  fillRect(121, 88, 5, 10, WHITE);
  fillRect(125, 82, 5, 10, WHITE);

  drawCenteredText(138, "THANH CONG", COL_GOOD, COL_BG, 2);
  drawCenteredText(174, "DANG KHOI DONG LAI", COL_MUTED, COL_BG, 1);
  drawCenteredText(198, "VUI LONG CHO...", COL_ACCENT, COL_BG, 1);
  yield();
  delay(350);
}

void drawOtaErrorScreen(int errorCode) {
  otaInProgress = false;
  fillScreen(COL_BG);
  drawRect(4, 4, 232, 232, RED);

  fillRoundRect(12, 12, 216, 40, 8, COL_CARD);
  drawCenteredText(23, "OTA UPDATE", WHITE, COL_CARD, 2);

  fillCircle(120, 92, 28, RED);
  fillRect(105, 89, 30, 6, WHITE);
  fillRect(117, 77, 6, 30, WHITE);

  drawCenteredText(136, "CAP NHAT LOI", RED, COL_BG, 2);
  drawCenteredText(168, "MA LOI " + String(errorCode), COL_TEMP, COL_BG, 1);
  drawCenteredText(194, "THU LAI TREN WEB", COL_MUTED, COL_BG, 1);
  forcePageRedraw = true;
  yield();
}

void drawBootShell() {
  fillScreen(COL_BG);
  drawRect(4, 4, 232, 232, COL_LINE);
  fillRoundRect(10, 10, 220, 34, 7, COL_CARD);
  fillRoundRect(12, 52, 216, 170, 7, COL_CARD);
}

void drawBootTitle(String title) {
  fillRoundRect(10, 10, 220, 34, 7, COL_CARD);
  drawCenteredText(18, title, WHITE, COL_CARD, 2);
}

void clearBootContent() {
  fillRoundRect(12, 52, 216, 170, 7, COL_CARD);
}

void drawProgressDots(int y, int step) {
  fillRect(100, y - 6, 44, 12, COL_CARD);
  for (int i = 0; i < 3; i++) {
    uint16_t c = i <= (step % 3) ? COL_ACCENT : COL_LINE;
    fillCircle(108 + i * 12, y, 3, c);
  }
}

void drawSetupWizardScreen() {
  waitingScreenShown = true;
  drawBootTitle("CHE DO CAI DAT");
  clearBootContent();
  drawCenteredText(62, "KET NOI DIEN THOAI", COL_MUTED, COL_CARD, 1);
  drawCenteredText(82, "SmallTV-Setup", COL_ACCENT, COL_CARD, 2);
  drawCenteredText(116, "MAT KHAU", COL_MUTED, COL_CARD, 1);
  drawCenteredText(135, "12345678", WHITE, COL_CARD, 2);
  drawCenteredText(175, "MO 192.168.4.1", COL_TEMP, COL_CARD, 1);
  drawCenteredText(202, "DANG CHO CAU HINH", COL_MUTED, COL_CARD, 1);
}

void drawConnectingScreen(String ssid, int step) {
  static bool initialized = false;
  static String lastSsid = "";

  if (!initialized || lastSsid != ssid) {
    drawBootTitle("DANG KET NOI");
    clearBootContent();
    drawCenteredText(72, ssid.length() ? ssid : String("WIFI DA LUU"), COL_ACCENT, COL_CARD, 2);
    drawCenteredText(111, "VUI LONG CHO", COL_MUTED, COL_CARD, 1);
    fillRoundRect(26, 157, 188, 14, 7, COL_BG);
    lastSsid = ssid;
    initialized = true;
  }

  drawProgressDots(135, step);
  fillRoundRect(26, 157, 188, 14, 7, COL_BG);
  int progress = constrain(step, 0, 10) * 18;
  if (progress > 0) fillRoundRect(30, 161, progress, 6, 3, COL_GOOD);
  yield();
}

void drawConnectedScreen(String ssid, String ip) {
  drawBootTitle("DA KET NOI WIFI");
  clearBootContent();
  drawCenteredText(70, ssid, COL_GOOD, COL_CARD, 2);
  drawCenteredText(110, "DIA CHI IP", COL_MUTED, COL_CARD, 1);
  drawCenteredText(130, ip, COL_ACCENT, COL_CARD, 2);
  drawCenteredText(178, "DANG KHOI DONG", WHITE, COL_CARD, 1);
}

void drawSyncScreen(bool timeOk, bool weatherDone, bool aqiDone) {
  static int previousMask = -1;
  int mask = (timeOk ? 1 : 0) | (weatherDone ? 2 : 0) | (aqiDone ? 4 : 0);

  // A mask of zero always starts a new synchronization sequence.
  // This is required after reconnecting from the Setup AP screen.
  if (mask == 0 || previousMask == -1) {
    drawBootTitle("DANG KHOI TAO");
    clearBootContent();
    drawText(70, 76, "DONG BO GIO", WHITE, COL_CARD, 1);
    drawText(70, 114, "THOI TIET", WHITE, COL_CARD, 1);
    drawText(70, 152, "CHAT LUONG KK", WHITE, COL_CARD, 1);
    drawCenteredText(200, "DANG MO DONG HO...", COL_MUTED, COL_CARD, 1);
    previousMask = -1;
  }

  if (previousMask != mask) {
    fillRect(26, 70, 34, 100, COL_CARD);
    drawText(28, 76, timeOk ? "[OK]" : "[..]", timeOk ? COL_GOOD : COL_TEMP, COL_CARD, 1);
    drawText(28, 114, weatherDone ? "[OK]" : "[..]", weatherDone ? COL_GOOD : COL_TEMP, COL_CARD, 1);
    drawText(28, 152, aqiDone ? "[OK]" : "[..]", aqiDone ? COL_GOOD : COL_TEMP, COL_CARD, 1);
    previousMask = mask;
  }

  yield();
}


bool waitForValidTime(unsigned long timeoutMs) {
  unsigned long started = millis();
  while (millis() - started < timeoutMs) {
    time_t now = time(nullptr);
    if (now > 1577836800) return true;
    server.handleClient();
    delay(100);
    yield();
  }
  return false;
}

void enableSetupAP(const char* reason) {
  if (setupApActive || apMode) return;

  WiFi.mode(WIFI_AP_STA);
  setupApActive = WiFi.softAP("SmallTV-Setup", "12345678");
  setupApStartedAt = millis();

  Serial.print("Setup AP enabled: ");
  Serial.println(reason);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  forcePageRedraw = true;
}

void disableSetupAP() {
  if (!setupApActive || apMode) return;

  WiFi.softAPdisconnect(true);
  setupApActive = false;
  WiFi.mode(WIFI_STA);
  forcePageRedraw = true;

  Serial.println("Temporary Setup AP disabled");
}

bool connectWiFi() {
  if (strlen(cfg.ssid) == 0) return false;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.ssid, cfg.password);

  Serial.print("Connecting WiFi");
  unsigned long started = millis();
  int step = 0;

  while (WiFi.status() != WL_CONNECTED &&
         millis() - started < 18000UL) {
    drawConnectingScreen(String(cfg.ssid), step++);
    delay(600);
    Serial.print(".");
    yield();
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    apMode = false;
    setupApActive = false;
    WiFi.softAPdisconnect(true);
    drawConnectedScreen(WiFi.SSID(), WiFi.localIP().toString());
    Serial.print("Connected. IP: ");
    Serial.println(WiFi.localIP());
    delay(1800);
    return true;
  }

  return false;
}

void startSetupAP() {
  apMode = true;
  WiFi.mode(WIFI_AP_STA);
  setupApActive = WiFi.softAP("SmallTV-Setup", "12345678");
  setupApStartedAt = millis();

  Serial.print("SmallTV-Setup AP: ");
  Serial.println(setupApActive ? "ON" : "FAILED");
  Serial.print("Setup AP IP: ");
  Serial.println(WiFi.softAPIP());

  startWebServer();
  drawSetupWizardScreen();
}

// ---------------- Setup / loop ----------------
void setup() {
  Serial.begin(115200);
  delay(300);

  loadSettings();

  pinMode(TFT_CS, OUTPUT);
  pinMode(TFT_DC, OUTPUT);
  pinMode(TFT_RST, OUTPUT);

  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TFT_DC, HIGH);
  digitalWrite(TFT_RST, HIGH);

  SPI.begin();
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  SPI.setFrequency(10000000);

  lcdInit();
  Serial.println("LCD init OK");
  drawBootShell();
  yield();
  delay(80);

  if (connectWiFi()) {
    apMode = false;

    configTime(cfg.utcOffsetMinutes * 60L, 0,
               "ntp.aliyun.com",
               "pool.ntp.org",
               "time.cloudflare.com");

    startWebServer();

    drawSyncScreen(false, false, false);
    bool timeOk = waitForValidTime(8000UL);
    drawSyncScreen(timeOk, false, false);

    bool weatherDone = fetchWeather();
    drawSyncScreen(timeOk, weatherDone, false);

    bool aqiDone = fetchAirQuality();
    drawSyncScreen(timeOk, weatherDone, aqiDone);
    fetchCryptoPrices();
    delay(1200);

    SPI.begin();
    SPI.setDataMode(SPI_MODE0);
    SPI.setBitOrder(MSBFIRST);
    SPI.setFrequency(10000000);
    lcdInit();

    lastPageChange = millis();
    drawAll();
    Serial.println("UI draw OK");
  } else {
    SPI.begin();
    SPI.setDataMode(SPI_MODE0);
    SPI.setBitOrder(MSBFIRST);
    SPI.setFrequency(10000000);
    lcdInit();

    startSetupAP();
    Serial.println("Setup wizard ready");
  }
}

void loop() {
  server.handleClient();
  unsigned long now = millis();

  // During OTA, the updater callbacks own the LCD.
  if (otaInProgress) {
    yield();
    return;
  }

  // No router Wi-Fi: remain on the setup waiting screen.
  // Do not continue to Clock/Weather until Wi-Fi reconnects.
  if (WiFi.status() != WL_CONNECTED) {
    if (!setupApActive && !apMode) {
      enableSetupAP("WiFi disconnected - waiting in setup mode");
    }

    if (!waitingScreenShown) {
      drawSetupWizardScreen();
    }

    if (now - lastWifiRetry >= 15000UL) {
      lastWifiRetry = now;

      if (strlen(cfg.ssid) > 0) {
        WiFi.disconnect();
        WiFi.begin(cfg.ssid, cfg.password);
      }
    }

    delay(20);
    yield();
    return;
  }

  // Wi-Fi has reconnected after the device was waiting in setup mode.
  if (waitingScreenShown || apMode) {
    apMode = false;

    drawConnectedScreen(WiFi.SSID(), WiFi.localIP().toString());
    delay(900);

    configTime(cfg.utcOffsetMinutes * 60L, 0,
               "ntp.aliyun.com",
               "pool.ntp.org",
               "time.cloudflare.com");

    drawSyncScreen(false, false, false);
    bool timeOk = waitForValidTime(5000UL);
    drawSyncScreen(timeOk, false, false);

    bool weatherDone = fetchWeather();
    drawSyncScreen(timeOk, weatherDone, false);

    bool aqiDone = fetchAirQuality();
    drawSyncScreen(timeOk, weatherDone, aqiDone);
    delay(800);

    waitingScreenShown = false;
    setupApActive = false;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);

    lastPageChange = millis();
    forcePageRedraw = true;
    drawAll();
  }

  // Temporary manually-enabled AP times out only while Wi-Fi is connected.
  if (setupApActive && !apMode &&
      now - setupApStartedAt >= SETUP_AP_TIMEOUT_MS) {
    disableSetupAP();
    drawFooter(true);
  }

  if (cfg.colonBlink && currentPage == 0 && now - lastColonToggle >= 500UL) {
    lastColonToggle = now;
    colonVisible = !colonVisible;
    fillRect(105, 59, 30, 68, COL_PANEL);
    if (colonVisible) {
      fillCircle(120, 80, 3, COL_ACCENT);
      fillCircle(120, 106, 3, COL_ACCENT);
    }
  }

  if (currentPage == 0 && now - lastClockUpdate >= 1000UL) {
    lastClockUpdate = now;
    drawClock(false);
  }

  if (currentPage == 0 && now - lastFooterCheck >= 2000UL) {
    lastFooterCheck = now;
    drawFooter(false);
  }

  if (currentPage == 2 && now - lastMochiAnimation >= 125UL) {
    lastMochiAnimation = now;
    animateMochiFrame();
    forcePageRedraw = false;
  }

  if (cfg.autoPage &&
      now - lastPageChange >= (unsigned long)cfg.pageIntervalSeconds * 1000UL) {
    nextPage();
  }

  if (forcePageRedraw) {
    drawPage(true);
  }

  if (now - lastWeatherUpdate >= (unsigned long)cfg.weatherIntervalMinutes * 60000UL) {
    lastWeatherUpdate = now;
    fetchWeather();
    forcePageRedraw = true;
  }

  if (now - lastAirQualityUpdate >= (unsigned long)cfg.aqiIntervalMinutes * 60000UL) {
    lastAirQualityUpdate = now;
    fetchAirQuality();
    forcePageRedraw = true;
  }

  if (currentPage == 3) {
    drawCryptoFooter(false);
  }

  if (lastCryptoUpdate == 0 ||
      now - lastCryptoUpdate >= (unsigned long)cfg.cryptoIntervalSeconds * 1000UL) {
    lastCryptoUpdate = now;
    fetchCryptoPrices();
  }

  yield();
}
