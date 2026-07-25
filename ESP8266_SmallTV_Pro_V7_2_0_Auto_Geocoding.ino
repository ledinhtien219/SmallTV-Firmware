
/*
  ESP8266 SmallTV Pro V7.2.0 Compile Fix
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
#include <ESP8266HTTPUpdateServer.h>
#include <EEPROM.h>
#include <SPI.h>
#include <time.h>

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
};

const uint32_t SETTINGS_MAGIC = 0x53563531;
Settings cfg;
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

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
bool colonVisible = true;
bool forcePageRedraw = true;

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
    if (cfg.weatherIntervalMinutes < 1 || cfg.weatherIntervalMinutes > 60) {
      cfg.weatherIntervalMinutes = 5;
      changed = true;
    }
    if (cfg.aqiIntervalMinutes < 1 || cfg.aqiIntervalMinutes > 60) {
      cfg.aqiIntervalMinutes = 15;
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

  fillRoundRect(9, 9, 222, 42, 7, COL_CARD);
  drawText(15, 13, "THOI TIET", WHITE, COL_CARD, 2);
  drawText(15, 35, String(cfg.city), COL_ACCENT, COL_CARD, 1);
  drawWeatherIcon(weatherCode, 204, 29);

  drawDetailRow(58,  "NHIET DO",       String(currentTemp, 1) + " C", COL_TEMP);
  drawDetailRow(87,  "CAM GIAC", String(apparentTemp, 1) + " C", WHITE);
  drawDetailRow(116, "DO AM",   String(humidity) + " %", COL_ACCENT);
  drawDetailRow(145, "GIO",       String(windSpeed, 1) + " " + windDirectionText(windDirection), WHITE);
  drawDetailRow(174, "AP SUAT",   String((int)round(pressure)) + " HPA", COL_GOOD);

  fillRoundRect(13, 203, 214, 25, 5, COL_CARD);
  drawText(20, 212, "AQI " + String(aqi) + " " + aqiLevelText(aqi), aqi > 100 ? ORANGE : COL_GOOD, COL_CARD, 1);
  String updateText = "CAP NHAT " + lastUpdatedText();
  int updateWidth = updateText.length() * 6;
  drawText(220 - updateWidth, 212, updateText, COL_MUTED, COL_CARD, 1);
}
void drawPage(bool forceRedraw = false) {
  if (!forceRedraw && !forcePageRedraw) return;

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
  } else {
    drawWeatherDetailPage();
  }

  forcePageRedraw = false;
  yield();
}

void nextPage() {
  currentPage = (currentPage + 1) % 2;
  forcePageRedraw = true;
  lastPageChange = millis();
}

void previousPage() {
  currentPage = currentPage == 0 ? 1 : 0;
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

bool geocodeLocation(const String& query, String& resolvedCity,
                     float& latitude, float& longitude) {
  if (WiFi.status() != WL_CONNECTED || query.length() < 2) return false;

  WiFiClient client;
  HTTPClient http;
  String url = "http://geocoding-api.open-meteo.com/v1/search?name=" +
               urlEncode(query) + "&count=1&language=vi&format=json";

  if (!http.begin(client, url)) return false;
  http.setTimeout(12000);
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

bool fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClient client;
  HTTPClient http;

  String url =
    "http://api.open-meteo.com/v1/forecast?latitude=" + String(cfg.latitude) +
    "&longitude=" + String(cfg.longitude) +
    "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,wind_direction_10m,precipitation,surface_pressure,cloud_cover" +
    "&daily=temperature_2m_max,temperature_2m_min" +
    "&forecast_days=1&timezone=auto";

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
  h.reserve(14500);
  h += F(R"HTML(
<!doctype html><html lang="vi"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#111820"><link rel="manifest" href="/manifest.json">
<title>SmallTV Control</title>
<style>
:root{--bg:#0b1117;--panel:#131d26;--card:#192630;--line:#2b4050;--text:#f4f8fb;--muted:#91a8b8;--accent:#3bd2e8;--ok:#5bdc91;--warn:#ffb94a}
*{box-sizing:border-box}body{margin:0;background:linear-gradient(160deg,#071019,#111b25);color:var(--text);font-family:system-ui,-apple-system,Segoe UI,sans-serif}
.app{max-width:760px;margin:auto;min-height:100vh;padding:env(safe-area-inset-top) 12px 30px}
.top{position:sticky;top:0;z-index:5;background:rgba(11,17,23,.92);backdrop-filter:blur(12px);padding:14px 4px 10px;border-bottom:1px solid var(--line)}
.brand{display:flex;justify-content:space-between;align-items:center}.brand h1{font-size:21px;margin:0}.dot{width:10px;height:10px;border-radius:50%;background:var(--ok);box-shadow:0 0 12px var(--ok)}
.tabs{display:flex;overflow:auto;gap:8px;margin-top:12px}.tabs button{white-space:nowrap}
button,.btn{border:0;border-radius:11px;padding:11px 15px;background:#243746;color:var(--text);font-weight:700}
button.primary{background:#287ce1}.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:10px}.card{background:rgba(25,38,48,.94);border:1px solid var(--line);border-radius:16px;padding:16px;margin-top:12px;box-shadow:0 10px 24px rgba(0,0,0,.18)}
.hero{display:grid;grid-template-columns:1fr auto;align-items:center}.temp{font-size:48px;font-weight:800}.muted{color:var(--muted);font-size:13px}.metric b{display:block;font-size:21px;margin-top:4px}
section{display:none}section.active{display:block}label{display:block;color:#c9d5dc;font-size:13px;margin:12px 0 5px}
input,select{width:100%;border:1px solid #3a5262;border-radius:10px;padding:12px;background:#0e171e;color:#fff;font-size:15px}
input[type=checkbox]{width:auto;transform:scale(1.25);margin-right:9px}input[type=color]{height:48px;padding:4px}
.toggle{display:flex;align-items:center;margin:14px 0}.actions{display:flex;gap:9px;flex-wrap:wrap;margin-top:14px}
.toast{position:fixed;left:50%;bottom:24px;transform:translateX(-50%);background:#e9f7ee;color:#173d24;padding:10px 18px;border-radius:20px;display:none;z-index:20}
@media(min-width:620px){.grid{grid-template-columns:repeat(4,1fr)}}
</style></head><body><div class="app">
<div class="top"><div class="brand"><div><h1>SmallTV Control</h1><div id="netline" class="muted">Đang kiểm tra Wi-Fi...</div></div><span id="online" class="dot"></span></div>
<div class="tabs">
<button onclick="tab('home')">Dashboard</button><button onclick="tab('display')">Display</button>
<button onclick="tab('weather')">Weather</button><button onclick="tab('network')">Network</button>
<button onclick="tab('system')">System</button></div></div>

<section id="home" class="active">
<div class="card hero"><div><div class="muted" id="cityView">Đang tải...</div><div class="temp"><span id="temp">--</span>°C</div><div id="desc">--</div></div><div style="font-size:54px">🌤️</div></div>
<div class="grid">
<div class="card metric"><span class="muted">Độ ẩm</span><b id="hum">--%</b></div>
<div class="card metric"><span class="muted">Gió</span><b id="wind">--</b></div>
<div class="card metric"><span class="muted">AQI</span><b id="aqi">--</b></div>
<div class="card metric"><span class="muted">Trang</span><b id="page">--</b></div>
</div>
<div class="card"><b>Wi-Fi hiện tại</b><div class="grid">
<div class="metric"><span class="muted">Trạng thái</span><b id="wifiState">--</b></div>
<div class="metric"><span class="muted">SSID</span><b id="ssidView">--</b></div>
<div class="metric"><span class="muted">IP</span><b id="ipView">--</b></div>
<div class="metric"><span class="muted">RSSI</span><b id="rssiView">--</b></div>
</div></div>
<div class="card"><b>Chuyển trang nhanh</b><div class="actions">
<button class="primary" onclick="go('/api/page?value=0')">Clock</button>
<button class="primary" onclick="go('/api/page?value=1')">Weather</button>
<button onclick="go('/api/next')">Trang kế</button></div></div>
</section>

<section id="display"><div class="card"><h3>Hiển thị</h3>
<label>Màu đồng hồ</label><input id="clockColor" type="color" oninput="$('clockHex').textContent=this.value.toUpperCase()">
<div class="muted">Giá trị: <span id="clockHex">--</span></div>
<label>Màu ngày</label><input id="dateColor" type="color" oninput="$('dateHex').textContent=this.value.toUpperCase()">
<div class="muted">Giá trị: <span id="dateHex">--</span></div>
<div class="toggle"><input id="autoPage" type="checkbox"><span>Tự lật trang</span></div>
<label>Chu kỳ lật trang</label><select id="interval"><option>5</option><option>10</option><option>12</option><option>15</option><option>30</option><option>60</option></select>
<div class="toggle"><input id="colonBlink" type="checkbox"><span>Nhấp nháy dấu hai chấm</span></div>
<div class="toggle"><input id="use12Hour" type="checkbox"><span>Định dạng 12 giờ</span></div>
<div class="actions"><button class="primary" onclick="saveDisplay()">Lưu Display</button></div>
</div></section>

<section id="weather"><div class="card"><h3>Thời tiết</h3>
<label>Nhập địa điểm</label><div class="grid"><div><input id="city" placeholder="Ví dụ: Hà Nội, Đà Nẵng, Quận 1"></div><div class="actions" style="margin-top:0"><button type="button" onclick="findLocation()">Tự tìm tọa độ</button></div></div>
<div class="grid"><div><label>Latitude (tự động)</label><input id="lat" readonly></div><div><label>Longitude (tự động)</label><input id="lon" readonly></div></div>
<p class="muted">Nhập tên tỉnh/thành phố hoặc khu vực rồi bấm Tự tìm tọa độ. Khi lưu, hệ thống cũng tự tìm lại nếu cần.</p>
<label>UTC offset (phút)</label><input id="utc" type="number">
<div class="grid"><div><label>Cập nhật thời tiết (1–60 phút)</label><input id="weatherInterval" type="number" min="1" max="60"></div><div><label>Cập nhật AQI (1–60 phút)</label><input id="aqiInterval" type="number" min="1" max="60"></div></div>
<div class="card" style="margin-top:14px"><b>Trạng thái cập nhật</b>
<div class="grid"><div class="metric"><span class="muted">Thời tiết lần cuối</span><b id="weatherLast">Chưa có</b><span class="muted">Lần tiếp theo: <span id="weatherNext">--</span></span></div>
<div class="metric"><span class="muted">AQI lần cuối</span><b id="aqiLast">Chưa có</b><span class="muted">Lần tiếp theo: <span id="aqiNext">--</span></span></div></div></div>
<div class="actions"><button class="primary" onclick="saveWeather()">Lưu cấu hình</button><button onclick="fetchNow()">Cập nhật ngay</button></div>
</div></section>

<section id="network"><div class="card"><h3>Wi-Fi</h3>
<label>SSID</label><input id="ssid"><label>Mật khẩu</label><input id="password" type="password" placeholder="Để trống nếu không đổi">
<div class="actions"><button class="primary" onclick="saveNetwork()">Lưu và khởi động lại</button></div>
<p class="muted">Khi chưa kết nối Wi-Fi, dùng AP SmallTV-Setup / 12345678.</p></div></section>

<section id="system"><div class="card"><h3>Hệ thống</h3>
<div class="grid">
<div class="metric"><span class="muted">IP</span><b id="ip">--</b></div>
<div class="metric"><span class="muted">RSSI</span><b id="rssi">--</b></div>
<div class="metric"><span class="muted">Heap</span><b id="heap">--</b></div>
<div class="metric"><span class="muted">Uptime</span><b id="uptime">--</b></div>
</div><div class="actions"><button onclick="refresh()">Làm mới</button><button onclick="enableSetupAp()">Bật Setup AP 10 phút</button><button onclick="location.href='/update'">OTA Firmware</button><button onclick="reboot()">Khởi động lại</button></div>
</div></section>
</div><div id="toast" class="toast"></div>
<script>
const $=id=>document.getElementById(id);
function tab(id){document.querySelectorAll('section').forEach(x=>x.classList.remove('active'));$(id).classList.add('active')}
function toast(t){$('toast').textContent=t;$('toast').style.display='block';setTimeout(()=>$('toast').style.display='none',1800)}
async function req(url,opt={}){const r=await fetch(url,opt);if(!r.ok)throw Error(await r.text());return r.json()}
async function go(url){await req(url);await refresh();toast('Đã chuyển trang')}
async function refresh(){
 try{
  const s=await req('/api/status');
  $('online').style.background='#5bdc91';$('cityView').textContent=s.city;$('temp').textContent=s.temperature.toFixed(1);
  $('desc').textContent=s.weather;$('hum').textContent=s.humidity+'%';$('wind').textContent=s.wind_speed.toFixed(1)+' km/h';
  $('aqi').textContent=s.aqi;$('page').textContent=s.page===0?'Clock':'Weather';
  $('city').value=s.city;$('lat').value=s.latitude;$('lon').value=s.longitude;$('utc').value=s.utc_offset;
  $('weatherInterval').value=s.weather_interval;$('aqiInterval').value=s.aqi_interval;
  $('weatherLast').textContent=s.weather_last;$('weatherNext').textContent=s.weather_next;
  $('aqiLast').textContent=s.aqi_last;$('aqiNext').textContent=s.aqi_next;
  $('autoPage').checked=s.auto_page;$('interval').value=s.page_interval;$('colonBlink').checked=s.colon_blink;
  $('use12Hour').checked=s.use_12_hour;$('clockColor').value=s.clock_color;$('dateColor').value=s.date_color;
  $('clockHex').textContent=s.clock_color.toUpperCase();$('dateHex').textContent=s.date_color.toUpperCase();
  $('ssid').value=s.saved_ssid;$('ip').textContent=s.ip;$('rssi').textContent=s.rssi+' dBm';$('heap').textContent=Math.round(s.free_heap/1024)+' KB';
  $('uptime').textContent=Math.floor(s.uptime/60)+' phút';
  $('wifiState').textContent=s.wifi_state;
  $('ssidView').textContent=s.connected_ssid || s.ap_ssid || '--';
  $('ipView').textContent=s.ip;
  $('rssiView').textContent=s.ap_mode ? 'AP' : s.rssi+' dBm';
  $('netline').textContent=s.ap_mode
    ? ('AP '+s.ap_ssid+' · 192.168.4.1')
    : (s.wifi_state+' · '+(s.connected_ssid||s.saved_ssid)+' · LAN '+s.ip+' · Setup 192.168.4.1');
 }catch(e){$('online').style.background='#ff5b5b'}
}
async function post(url,data){return req(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)})}
async function saveDisplay(){
 const payload={
  autoPage:$('autoPage').checked?1:0,
  interval:$('interval').value,
  colonBlink:$('colonBlink').checked?1:0,
  use12Hour:$('use12Hour').checked?1:0,
  clockColor:$('clockColor').value.toUpperCase(),
  dateColor:$('dateColor').value.toUpperCase()
 };
 const result=await post('/api/display',payload);
 if(result.clock_color)$('clockColor').value=result.clock_color;
 if(result.date_color)$('dateColor').value=result.date_color;
 toast('Đã lưu màu hiển thị');
 await refresh();
}
async function findLocation(showToast=true){
 const q=$('city').value.trim();
 if(q.length<2){toast('Hãy nhập tên địa điểm');return false}
 if(showToast)toast('Đang tìm tọa độ...');
 try{
  const r=await post('/api/geocode',{q});
  $('city').value=r.city;$('lat').value=Number(r.latitude).toFixed(5);$('lon').value=Number(r.longitude).toFixed(5);
  if(showToast)toast('Đã tìm thấy '+r.city);
  return true;
 }catch(e){toast('Không tìm thấy địa điểm hoặc chưa có Internet');return false}
}
async function saveWeather(){
 if(!await findLocation(false))return;
 await post('/api/weather',{city:$('city').value,lat:$('lat').value,lon:$('lon').value,utc:$('utc').value,weatherInterval:$('weatherInterval').value,aqiInterval:$('aqiInterval').value});
 toast('Đã lưu vị trí và cấu hình thời tiết');refresh();
}
async function fetchNow(){toast('Đang cập nhật...');const r=await post('/api/weather/fetch',{});toast(r.ok?'Đã cập nhật thời tiết và AQI':'Cập nhật chưa hoàn tất');await refresh()}
async function saveNetwork(){await post('/api/network',{ssid:$('ssid').value,password:$('password').value});toast('Đã lưu, thiết bị sẽ restart')}
async function enableSetupAp(){await post('/api/setup-ap',{});toast('SmallTV-Setup đã bật 10 phút');await refresh()}
async function reboot(){if(confirm('Khởi động lại SmallTV?')){await post('/api/reboot',{});toast('Đang restart')}}
refresh();setInterval(refresh,5000);
if('serviceWorker' in navigator)navigator.serviceWorker.register('/sw.js').catch(()=>{});
</script></body></html>
)HTML");
  return h;
}

void sendJsonOk() {
  server.send(200, "application/json", "{\"ok\":true}");
}

void startWebServer() {
  server.on("/", HTTP_GET, []() {
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
    currentPage = server.arg("value").toInt() == 1 ? 1 : 0;
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

  server.on("/api/geocode", HTTP_POST, []() {
    String query = server.arg("q");
    query.trim();
    String resolvedCity;
    float latitude = 0.0f;
    float longitude = 0.0f;
    if (!geocodeLocation(query, resolvedCity, latitude, longitude)) {
      server.send(404, "application/json", "{\"ok\":false,\"error\":\"location_not_found\"}");
      return;
    }
    String json = "{\"ok\":true,";
    json += "\"city\":\"" + jsonEscape(resolvedCity) + "\",";
    json += "\"latitude\":" + String(latitude, 5) + ",";
    json += "\"longitude\":" + String(longitude, 5) + "}";
    server.send(200, "application/json", json);
  });

  server.on("/api/weather", HTTP_POST, []() {
    strlcpy(cfg.city, server.arg("city").c_str(), sizeof(cfg.city));
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
    json += "\"page\":" + String(currentPage) + ",";
    json += "\"city\":\"" + jsonEscape(String(cfg.city)) + "\",";
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

  httpUpdater.setup(&server, "/update");
  server.begin();
}


void drawCenteredText(int y, String text, uint16_t color, uint16_t bg, int scale) {
  int width = text.length() * 6 * scale;
  int x = (240 - width) / 2;
  if (x < 4) x = 4;
  drawText(x, y, text, color, bg, scale);
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

  yield();
}
