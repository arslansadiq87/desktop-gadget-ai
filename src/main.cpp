#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <JPEGDEC.h>
#include <MD5Builder.h>
#include <Preferences.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef PROJECT_INITIAL_WIFI_SSID
#define PROJECT_INITIAL_WIFI_SSID ""
#endif

#ifndef PROJECT_INITIAL_WIFI_PASSWORD
#define PROJECT_INITIAL_WIFI_PASSWORD ""
#endif

#ifndef PROJECT_CCTV_DVR_HOST
#define PROJECT_CCTV_DVR_HOST ""
#endif

#ifndef PROJECT_CCTV_DVR_PORT
#define PROJECT_CCTV_DVR_PORT 443
#endif

#ifndef PROJECT_CCTV_DVR_USER
#define PROJECT_CCTV_DVR_USER ""
#endif

#ifndef PROJECT_CCTV_DVR_PASSWORD
#define PROJECT_CCTV_DVR_PASSWORD ""
#endif

#ifndef PROJECT_WEATHER_CITY
#define PROJECT_WEATHER_CITY "London"
#endif

#ifndef PROJECT_WEATHER_LATITUDE
#define PROJECT_WEATHER_LATITUDE 51.5072f
#endif

#ifndef PROJECT_WEATHER_LONGITUDE
#define PROJECT_WEATHER_LONGITUDE -0.1276f
#endif

constexpr int TFT_MOSI = 4;
constexpr int TFT_SCLK = 3;
constexpr int TFT_CS = 7;
constexpr int TFT_DC = 6;
constexpr int TFT_RST = 5;
constexpr int TFT_BL = 2;

constexpr int TFT_WIDTH = 240;
constexpr int TFT_HEIGHT = 320;
constexpr int DISPLAY_WIDTH = 320;
constexpr int DISPLAY_HEIGHT = 240;
constexpr int CLOCK_FACE_CANVAS_SIZE = 160;
constexpr int CLOCK_FACE_CANVAS_X = 24;
constexpr int CLOCK_FACE_CANVAS_Y = 40;
constexpr int CLOCK_FACE_CENTER_X = 104;
constexpr int CLOCK_FACE_CENTER_Y = 120;

constexpr int BUTTON_COUNT = 4;
constexpr int BUTTON_PINS[BUTTON_COUNT] = {8, 9, 10, 11};
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;

constexpr int BUZZER_PIN = 12;
constexpr int BUZZER_FREQUENCY_HZ = 2000;
constexpr int BUZZER_BEEP_MS = 80;

constexpr int BATTERY_ADC_PIN = 1;
constexpr bool BATTERY_ADC_ENABLED = false;
constexpr float BATTERY_DIVIDER_RATIO = 2.0f;
constexpr int BATTERY_EMPTY_MV = 3000;
constexpr int BATTERY_FULL_MV = 4200;
constexpr int BATTERY_SAMPLE_COUNT = 8;

const char *INITIAL_WIFI_SSID = PROJECT_INITIAL_WIFI_SSID;
const char *INITIAL_WIFI_PASSWORD = PROJECT_INITIAL_WIFI_PASSWORD;
const char *WIFI_PREF_NAMESPACE = "wifi";
const char *WIFI_PREF_SSID_KEY = "ssid";
const char *WIFI_PREF_PASSWORD_KEY = "password";
const char *DEVICE_PREF_NAMESPACE = "device";
const char *DEVICE_PREF_BUTTON_BUZZER_KEY = "buttonBuzzer";
constexpr int MAX_SCANNED_NETWORKS = 12;
constexpr int MAX_NUMERIC_PASSWORD_DIGITS = 16;
constexpr uint32_t BUTTON_DOUBLE_CLICK_MS = 450;
constexpr uint32_t BUTTON_LONG_PRESS_MS = 850;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr int WIFI_CONNECT_ATTEMPTS = 2;

const char *CCTV_DVR_HOST = PROJECT_CCTV_DVR_HOST;
constexpr uint16_t CCTV_DVR_PORT = PROJECT_CCTV_DVR_PORT;
const char *CCTV_DVR_USER = PROJECT_CCTV_DVR_USER;
const char *CCTV_DVR_PASSWORD = PROJECT_CCTV_DVR_PASSWORD;
const char *CCTV_SNAPSHOT_PATH = "/cgi-bin/snapshot.cgi?channel=";
constexpr int CCTV_CHANNEL_COUNT = 8;
constexpr size_t CCTV_MAX_JPEG_BYTES = 512 * 1024;
constexpr uint32_t CCTV_FRAME_INTERVAL_MS = 1200;
constexpr uint32_t CCTV_HTTP_TIMEOUT_MS = 5000;
constexpr int CCTV_VIEW_X = 0;
constexpr int CCTV_VIEW_Y = 34;
constexpr int CCTV_VIEW_W = DISPLAY_WIDTH;
constexpr int CCTV_VIEW_H = 174;

const char *WEATHER_HOST = "api.open-meteo.com";
const char *WEATHER_CITY = PROJECT_WEATHER_CITY;
constexpr float WEATHER_LATITUDE = PROJECT_WEATHER_LATITUDE;
constexpr float WEATHER_LONGITUDE = PROJECT_WEATHER_LONGITUDE;
constexpr uint16_t WEATHER_PORT = 80;
constexpr uint32_t WEATHER_HTTP_TIMEOUT_MS = 8000;
constexpr uint32_t WEATHER_REFRESH_INTERVAL_MS = 10UL * 60UL * 1000UL;

SPIClass displaySpi(FSPI);
Adafruit_ST7789 display(&displaySpi, TFT_CS, TFT_DC, TFT_RST);
JPEGDEC jpeg;
WiFiClientSecure cctvSecureClient;
WiFiClient weatherClient;
GFXcanvas16 clockFaceCanvas(CLOCK_FACE_CANVAS_SIZE, CLOCK_FACE_CANVAS_SIZE);
Preferences wifiPreferences;
Preferences devicePreferences;

enum class ScreenMode {
    Menu,
    Page,
};

enum class SettingsView {
    Main,
    WiFiList,
    PasswordEntry,
    ConnectResult,
};

struct ButtonState {
    bool pressed;
    bool lastReading;
    uint32_t lastChangeMs;
    uint32_t pressStartMs;
};

ButtonState buttons[BUTTON_COUNT] = {};
ScreenMode screenMode = ScreenMode::Menu;
int selectedMenuIndex = 0;
uint32_t batteryMillivolts = 0;
int batteryPercent = 0;
bool batteryAdcAvailable = false;
time_t bootEpochSeconds = 0;
uint32_t bootClockMillis = 0;
bool clockStaticDrawn = false;
bool clockHandsDrawn = false;
tm previousClockHandsTime = {};
int currentCctvChannel = 1;
uint32_t lastCctvFrameMs = 0;
uint8_t *cctvFrameBuffer = nullptr;
int currentCctvFrameX = CCTV_VIEW_X;
int currentCctvFrameY = CCTV_VIEW_Y;
uint32_t lastWeatherUpdateMs = 0;
bool weatherDataValid = false;
SettingsView settingsView = SettingsView::Main;
int selectedSettingsIndex = 0;
String savedWifiSsid;
String savedWifiPassword;
String scannedWifiSsids[MAX_SCANNED_NETWORKS];
int32_t scannedWifiRssi[MAX_SCANNED_NETWORKS] = {};
int scannedWifiCount = 0;
int selectedWifiIndex = 0;
String pendingWifiSsid;
String numericWifiPassword;
int currentPasswordDigit = 0;
uint32_t lastPasswordBackMs = 0;
String settingsResultLine1;
String settingsResultLine2;

struct WeatherData {
    float temperature;
    float apparentTemperature;
    float windSpeed;
    int humidity;
    int weatherCode;
    char time[24];
};

WeatherData weatherData = {};

const char *MENU_ITEMS[] = {
    "CCTV",
    "Settings",
    "Weather",
    "Clock",
};
constexpr int MENU_ITEM_COUNT = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);
constexpr int CCTV_MENU_INDEX = 0;
constexpr int SETTINGS_MENU_INDEX = 1;
constexpr int WEATHER_MENU_INDEX = 2;
constexpr int CLOCK_MENU_INDEX = 3;
constexpr int SETTINGS_ITEM_COUNT = 2;
constexpr int SETTINGS_WIFI_INDEX = 0;
constexpr int SETTINGS_BUTTON_BUZZER_INDEX = 1;

bool buttonBuzzerEnabled = true;

String configuredWifiSsid();

void beepBuzzer() {
    if (!buttonBuzzerEnabled) {
        return;
    }

    tone(BUZZER_PIN, BUZZER_FREQUENCY_HZ, BUZZER_BEEP_MS);
}

uint16_t batteryColor(int percent) {
    if (percent <= 20) {
        return ST77XX_RED;
    }

    if (percent <= 50) {
        return ST77XX_YELLOW;
    }

    return ST77XX_GREEN;
}

int wifiSignalBars() {
    if (WiFi.status() != WL_CONNECTED) {
        return 0;
    }

    const int32_t rssi = WiFi.RSSI();
    if (rssi >= -55) {
        return 4;
    }
    if (rssi >= -67) {
        return 3;
    }
    if (rssi >= -75) {
        return 2;
    }
    return 1;
}

void drawWifiBarsIcon(int x, int y) {
    const int bars = wifiSignalBars();
    const uint16_t active = display.color565(0, 220, 255);
    const uint16_t inactive = display.color565(46, 52, 64);

    for (int i = 0; i < 4; ++i) {
        const int barHeight = 4 + i * 3;
        const int barX = x + i * 5;
        const int barY = y + 14 - barHeight;
        display.fillRect(barX, barY, 3, barHeight, i < bars ? active : inactive);
    }
}

void drawBatteryIcon() {
    const int x = 270;
    const int y = 8;
    const int w = 34;
    const int h = 16;
    const int fillWidth = map(batteryPercent, 0, 100, 0, w - 4);

    display.fillRect(210, 0, 110, 30, ST77XX_BLACK);
    drawWifiBarsIcon(214, 9);

    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(1);
    display.setCursor(236, 12);
    display.printf("%3d%%", batteryPercent);

    display.drawRect(x, y, w, h, ST77XX_WHITE);
    display.drawRect(x + w, y + 4, 4, 8, ST77XX_WHITE);
    display.fillRect(x + 2, y + 2, w - 4, h - 4, ST77XX_BLACK);

    if (fillWidth > 0) {
        display.fillRect(x + 2, y + 2, fillWidth, h - 4, batteryColor(batteryPercent));
    }
}

void drawFooter() {
    display.fillRect(0, 208, DISPLAY_WIDTH, 32, ST77XX_BLACK);
    display.drawFastHLine(0, 208, DISPLAY_WIDTH, display.color565(46, 52, 64));

    const char *menuLabels[BUTTON_COUNT] = {"B1 OK", "B2 Back", "B3 Prev", "B4 Next"};
    const char *pageLabels[BUTTON_COUNT] = {"B1 OK", "B2 Back", "B3 Prev", "B4 Next"};
    const char *cctvLabels[BUTTON_COUNT] = {"B1 Shot", "B2 Back", "B3 Ch-", "B4 Ch+"};
    const char *weatherLabels[BUTTON_COUNT] = {"B1 Sync", "B2 Back", "", ""};
    const char *settingsMainLabels[BUTTON_COUNT] = {"B1 Sel", "B2 Back", "B3 Up", "B4 Down"};
    const char *wifiListLabels[BUTTON_COUNT] = {"B1 Sel", "B2 Back", "B3 Up", "B4 Down"};
    const char *wifiPasswordLabels[BUTTON_COUNT] = {"B1 Next", "B2 Del", "B3 -", "B4 +"};
    const char *wifiResultLabels[BUTTON_COUNT] = {"B1 Scan", "B2 Back", "", ""};
    const char **labels = screenMode == ScreenMode::Menu ? menuLabels : pageLabels;

    if (screenMode == ScreenMode::Page && selectedMenuIndex == CCTV_MENU_INDEX) {
        labels = cctvLabels;
    } else if (screenMode == ScreenMode::Page && selectedMenuIndex == WEATHER_MENU_INDEX) {
        labels = weatherLabels;
    } else if (screenMode == ScreenMode::Page && selectedMenuIndex == SETTINGS_MENU_INDEX) {
        if (settingsView == SettingsView::Main) {
            labels = settingsMainLabels;
        } else if (settingsView == SettingsView::WiFiList) {
            labels = wifiListLabels;
        } else if (settingsView == SettingsView::PasswordEntry) {
            labels = wifiPasswordLabels;
        } else {
            labels = wifiResultLabels;
        }
    }

    for (int i = 0; i < BUTTON_COUNT; ++i) {
        const int x = 8 + i * 78;
        const uint16_t fillColor = buttons[i].pressed ? display.color565(128, 203, 196) : display.color565(18, 23, 34);
        const uint16_t textColor = buttons[i].pressed ? ST77XX_BLACK : ST77XX_WHITE;

        display.fillRoundRect(x, 215, 68, 18, 8, fillColor);
        display.drawRoundRect(x, 215, 68, 18, 8, display.color565(82, 92, 112));
        display.setTextColor(textColor);
        display.setTextSize(1);
        display.setCursor(x + 8, 221);
        display.print(labels[i]);
    }
}

void drawTopBar(const char *title) {
    display.fillRect(0, 0, DISPLAY_WIDTH, 32, display.color565(10, 14, 22));
    display.drawFastHLine(0, 31, DISPLAY_WIDTH, display.color565(46, 52, 64));
    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(1);
    display.setCursor(10, 12);
    display.print(title);
    drawBatteryIcon();
}

void drawLauncherBackground() {
    display.fillScreen(display.color565(10, 14, 22));

    for (int y = 32; y < 208; y += 8) {
        const uint8_t shade = 18 + ((y - 32) / 8) * 2;
        display.fillRect(0, y, DISPLAY_WIDTH, 8, display.color565(8, shade, 34));
    }

    display.drawFastHLine(0, 207, DISPLAY_WIDTH, display.color565(46, 52, 64));
}

void drawCctvGlyph(int cx, int cy, uint16_t color) {
    const uint16_t shadow = display.color565(18, 23, 34);

    display.drawFastHLine(cx - 2, cy - 22, 26, color);
    display.drawFastVLine(cx + 24, cy - 22, 13, color);
    display.drawLine(cx + 24, cy - 9, cx + 14, cy - 3, color);

    display.fillRoundRect(cx - 24, cy - 9, 39, 21, 4, color);
    display.fillTriangle(cx + 12, cy - 9, cx + 25, cy - 4, cx + 12, cy + 12, color);
    display.drawRoundRect(cx - 24, cy - 9, 39, 21, 4, shadow);
    display.fillCircle(cx - 12, cy + 1, 6, shadow);
    display.drawCircle(cx - 12, cy + 1, 3, ST77XX_WHITE);

    display.drawLine(cx - 8, cy + 12, cx - 20, cy + 22, color);
    display.drawLine(cx + 4, cy + 12, cx + 16, cy + 22, color);
    display.drawFastHLine(cx - 24, cy + 22, 44, color);
}

void drawSettingsGlyph(int cx, int cy, uint16_t color) {
    display.fillCircle(cx, cy, 18, color);
    display.fillCircle(cx, cy, 8, ST77XX_BLACK);
    display.drawCircle(cx, cy, 4, ST77XX_WHITE);

    for (int i = 0; i < 8; ++i) {
        const float angle = i * PI / 4.0f;
        const int x1 = cx + cos(angle) * 17;
        const int y1 = cy + sin(angle) * 17;
        const int x2 = cx + cos(angle) * 24;
        const int y2 = cy + sin(angle) * 24;
        display.drawLine(x1, y1, x2, y2, color);
    }
}

void drawWeatherGlyph(int cx, int cy, uint16_t color) {
    display.fillCircle(cx - 10, cy - 8, 13, ST77XX_YELLOW);
    display.fillCircle(cx - 2, cy + 6, 13, color);
    display.fillCircle(cx + 14, cy + 4, 16, color);
    display.fillRoundRect(cx - 18, cy + 4, 48, 18, 8, color);
}

void drawClockGlyph(int cx, int cy, uint16_t color) {
    display.drawCircle(cx, cy, 20, color);
    display.drawCircle(cx, cy, 16, display.color565(128, 203, 196));
    display.drawLine(cx, cy, cx, cy - 11, color);
    display.drawLine(cx, cy, cx + 9, cy + 6, color);
    display.fillCircle(cx, cy, 3, ST77XX_WHITE);
    display.drawPixel(cx, cy - 14, ST77XX_WHITE);
    display.drawPixel(cx + 14, cy, ST77XX_WHITE);
    display.drawPixel(cx, cy + 14, ST77XX_WHITE);
    display.drawPixel(cx - 14, cy, ST77XX_WHITE);
}

void drawAppIcon(int index, int centerX, int iconY, bool selected) {
    const int focusX = centerX - 42;
    const int focusY = iconY - 36;
    const uint16_t labelColor = selected ? ST77XX_WHITE : display.color565(210, 218, 232);
    const uint16_t iconBgColors[MENU_ITEM_COUNT] = {
        display.color565(41, 121, 255),
        display.color565(124, 77, 255),
        display.color565(0, 172, 193),
        display.color565(0, 200, 120),
    };

    if (selected) {
        display.drawRoundRect(focusX, focusY, 84, 76, 12, ST77XX_WHITE);
        display.drawRoundRect(focusX + 2, focusY + 2, 80, 72, 10, display.color565(128, 203, 196));
    }

    display.fillRoundRect(centerX - 24, iconY - 24, 48, 48, 12, iconBgColors[index]);

    if (index == CCTV_MENU_INDEX) {
        drawCctvGlyph(centerX, iconY, ST77XX_WHITE);
    } else if (index == 1) {
        drawSettingsGlyph(centerX, iconY, ST77XX_WHITE);
    } else if (index == 2) {
        drawWeatherGlyph(centerX, iconY, ST77XX_WHITE);
    } else {
        drawClockGlyph(centerX, iconY, ST77XX_WHITE);
    }

    const int textX = centerX - (strlen(MENU_ITEMS[index]) * 6) / 2;
    display.setTextColor(labelColor);
    display.setTextSize(1);
    display.setCursor(textX, iconY + 31);
    display.print(MENU_ITEMS[index]);
}

int compileMonthNumber(const char *month) {
    const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *match = strstr(months, month);
    return match == nullptr ? 0 : ((match - months) / 3);
}

void setupClockBase() {
    char monthText[4] = {};
    int day = 1;
    int year = 2026;
    int hour = 0;
    int minute = 0;
    int second = 0;

    sscanf(__DATE__, "%3s %d %d", monthText, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);

    tm buildTime = {};
    buildTime.tm_year = year - 1900;
    buildTime.tm_mon = compileMonthNumber(monthText);
    buildTime.tm_mday = day;
    buildTime.tm_hour = hour;
    buildTime.tm_min = minute;
    buildTime.tm_sec = second;
    buildTime.tm_isdst = -1;

    bootEpochSeconds = mktime(&buildTime);
    bootClockMillis = millis();
}

time_t currentClockEpoch() {
    return bootEpochSeconds + ((millis() - bootClockMillis) / 1000);
}

void drawWatchSubdial(int cx, int cy, int value, int maxValue, const char *label, uint16_t accent, bool drawHand = true) {
    const uint16_t ring = display.color565(80, 92, 104);
    const uint16_t face = display.color565(9, 15, 20);

    display.fillCircle(cx, cy, 18, face);
    display.drawCircle(cx, cy, 18, ring);
    display.drawCircle(cx, cy, 14, display.color565(30, 38, 46));

    for (int i = 0; i < maxValue; i += max(1, maxValue / 6)) {
        const float angle = (i * TWO_PI / maxValue) - HALF_PI;
        const int x1 = cx + cos(angle) * 12;
        const int y1 = cy + sin(angle) * 12;
        const int x2 = cx + cos(angle) * 15;
        const int y2 = cy + sin(angle) * 15;
        display.drawLine(x1, y1, x2, y2, ring);
    }

    if (drawHand) {
        const float handAngle = (value * TWO_PI / maxValue) - HALF_PI;
        display.drawLine(cx, cy, cx + cos(handAngle) * 12, cy + sin(handAngle) * 12, accent);
        display.fillCircle(cx, cy, 2, ST77XX_WHITE);
    }

    display.setTextColor(display.color565(150, 160, 172));
    display.setTextSize(1);
    display.setCursor(cx - (strlen(label) * 3), cy + 23);
    display.print(label);
}

void drawClockStaticFace(const tm &now) {
    const int cx = CLOCK_FACE_CENTER_X;
    const int cy = CLOCK_FACE_CENTER_Y;
    const uint16_t steel = display.color565(176, 190, 204);
    const uint16_t darkSteel = display.color565(56, 66, 76);
    const uint16_t dial = display.color565(7, 12, 18);
    const uint16_t dial2 = display.color565(14, 22, 30);
    const uint16_t gold = display.color565(220, 176, 72);
    const uint16_t red = display.color565(230, 56, 64);

    display.fillCircle(cx, cy, 78, display.color565(4, 7, 10));
    display.drawCircle(cx, cy, 78, steel);
    display.drawCircle(cx, cy, 76, display.color565(92, 108, 122));
    display.drawCircle(cx, cy, 72, darkSteel);
    display.fillCircle(cx, cy, 68, dial);

    for (int r = 65; r > 42; r -= 6) {
        display.drawCircle(cx, cy, r, r % 12 == 5 ? dial2 : display.color565(10, 18, 25));
    }

    for (int i = 0; i < 60; ++i) {
        const float angle = (i - 15) * PI / 30.0f;
        const bool hourMark = i % 5 == 0;
        const int outer = hourMark ? 64 : 63;
        const int inner = hourMark ? 52 : 58;
        const uint16_t tickColor = hourMark ? steel : display.color565(58, 70, 82);
        const int x1 = cx + cos(angle) * inner;
        const int y1 = cy + sin(angle) * inner;
        const int x2 = cx + cos(angle) * outer;
        const int y2 = cy + sin(angle) * outer;
        display.drawLine(x1, y1, x2, y2, tickColor);
        if (hourMark) {
            display.drawLine(x1 + 1, y1, x2 + 1, y2, tickColor);
        }
    }

    display.setTextColor(gold);
    display.setTextSize(1);
    display.setCursor(cx - 15, cy - 46);
    display.print("NOVA");
    display.setTextColor(display.color565(140, 152, 164));
    display.setCursor(cx - 24, cy - 34);
    display.print("SAPPHIRE");

    drawWatchSubdial(cx - 34, cy + 24, now.tm_sec, 60, "SEC", red, false);
    drawWatchSubdial(cx + 34, cy + 24, now.tm_min, 60, "MIN", gold, false);

    display.fillRoundRect(cx + 35, cy - 10, 35, 20, 3, display.color565(232, 236, 230));
    display.drawRoundRect(cx + 35, cy - 10, 35, 20, 3, gold);
    display.setTextColor(ST77XX_BLACK);
    display.setTextSize(2);
    display.setCursor(cx + 41, cy - 5);
    display.printf("%02d", now.tm_mday);
}

void drawClockHands(const tm &now) {
    const int cx = CLOCK_FACE_CENTER_X;
    const int cy = CLOCK_FACE_CENTER_Y;
    const uint16_t steel = display.color565(176, 190, 204);
    const uint16_t gold = display.color565(220, 176, 72);
    const uint16_t red = display.color565(230, 56, 64);

    const float secondAngle = (now.tm_sec - 15) * PI / 30.0f;
    const float minuteAngle = (now.tm_min - 15) * PI / 30.0f;
    const float hourAngle = (((now.tm_hour % 12) * 5) + (now.tm_min / 12) - 15) * PI / 30.0f;

    display.drawLine(cx, cy, cx + cos(hourAngle) * 34, cy + sin(hourAngle) * 34, gold);
    display.drawLine(cx + 1, cy, cx + cos(hourAngle) * 34 + 1, cy + sin(hourAngle) * 34, gold);
    display.drawLine(cx, cy, cx + cos(minuteAngle) * 50, cy + sin(minuteAngle) * 50, ST77XX_WHITE);
    display.drawLine(cx + 1, cy, cx + cos(minuteAngle) * 50 + 1, cy + sin(minuteAngle) * 50, ST77XX_WHITE);
    display.drawLine(cx, cy, cx + cos(secondAngle) * 60, cy + sin(secondAngle) * 60, red);
    display.drawLine(cx, cy, cx - cos(secondAngle) * 14, cy - sin(secondAngle) * 14, red);

    display.fillCircle(cx, cy, 7, gold);
    display.drawCircle(cx, cy, 9, steel);
    display.fillCircle(cx, cy, 3, ST77XX_BLACK);
}

void drawClockSubdialHands(const tm &now) {
    const int cx = CLOCK_FACE_CENTER_X;
    const int cy = CLOCK_FACE_CENTER_Y;
    const uint16_t gold = display.color565(220, 176, 72);
    const uint16_t red = display.color565(230, 56, 64);

    drawWatchSubdial(cx - 34, cy + 24, now.tm_sec, 60, "SEC", red);
    drawWatchSubdial(cx + 34, cy + 24, now.tm_min, 60, "MIN", gold);
}

void drawCanvasSubdialStatic(int cx, int cy, const char *label) {
    const uint16_t ring = display.color565(80, 92, 104);
    const uint16_t face = display.color565(9, 15, 20);

    clockFaceCanvas.fillCircle(cx, cy, 18, face);
    clockFaceCanvas.drawCircle(cx, cy, 18, ring);
    clockFaceCanvas.drawCircle(cx, cy, 14, display.color565(30, 38, 46));

    for (int i = 0; i < 60; i += 10) {
        const float angle = (i * TWO_PI / 60.0f) - HALF_PI;
        const int x1 = cx + cos(angle) * 12;
        const int y1 = cy + sin(angle) * 12;
        const int x2 = cx + cos(angle) * 15;
        const int y2 = cy + sin(angle) * 15;
        clockFaceCanvas.drawLine(x1, y1, x2, y2, ring);
    }

    clockFaceCanvas.setTextColor(display.color565(150, 160, 172));
    clockFaceCanvas.setTextSize(1);
    clockFaceCanvas.setCursor(cx - (strlen(label) * 3), cy + 23);
    clockFaceCanvas.print(label);
}

void buildClockFaceCanvas(const tm &now) {
    const int cx = CLOCK_FACE_CANVAS_SIZE / 2;
    const int cy = CLOCK_FACE_CANVAS_SIZE / 2;
    const uint16_t steel = display.color565(176, 190, 204);
    const uint16_t darkSteel = display.color565(56, 66, 76);
    const uint16_t dial = display.color565(7, 12, 18);
    const uint16_t dial2 = display.color565(14, 22, 30);
    const uint16_t gold = display.color565(220, 176, 72);

    for (int row = 0; row < CLOCK_FACE_CANVAS_SIZE; ++row) {
        const int screenY = CLOCK_FACE_CANVAS_Y + row;
        const uint8_t shade = 8 + max(0, (screenY - 36) / 6);
        clockFaceCanvas.drawFastHLine(0, row, CLOCK_FACE_CANVAS_SIZE,
                                      display.color565(shade, shade + 3, shade + 8));
    }
    clockFaceCanvas.fillCircle(cx, cy, 78, display.color565(4, 7, 10));
    clockFaceCanvas.drawCircle(cx, cy, 78, steel);
    clockFaceCanvas.drawCircle(cx, cy, 76, display.color565(92, 108, 122));
    clockFaceCanvas.drawCircle(cx, cy, 72, darkSteel);
    clockFaceCanvas.fillCircle(cx, cy, 68, dial);

    for (int r = 65; r > 42; r -= 6) {
        clockFaceCanvas.drawCircle(cx, cy, r, r % 12 == 5 ? dial2 : display.color565(10, 18, 25));
    }

    for (int i = 0; i < 60; ++i) {
        const float angle = (i - 15) * PI / 30.0f;
        const bool hourMark = i % 5 == 0;
        const int outer = hourMark ? 64 : 63;
        const int inner = hourMark ? 52 : 58;
        const uint16_t tickColor = hourMark ? steel : display.color565(58, 70, 82);
        const int x1 = cx + cos(angle) * inner;
        const int y1 = cy + sin(angle) * inner;
        const int x2 = cx + cos(angle) * outer;
        const int y2 = cy + sin(angle) * outer;
        clockFaceCanvas.drawLine(x1, y1, x2, y2, tickColor);
        if (hourMark) {
            clockFaceCanvas.drawLine(x1 + 1, y1, x2 + 1, y2, tickColor);
        }
    }

    clockFaceCanvas.setTextColor(gold);
    clockFaceCanvas.setTextSize(1);
    clockFaceCanvas.setCursor(cx - 15, cy - 46);
    clockFaceCanvas.print("NOVA");
    clockFaceCanvas.setTextColor(display.color565(140, 152, 164));
    clockFaceCanvas.setCursor(cx - 24, cy - 34);
    clockFaceCanvas.print("SAPPHIRE");

    drawCanvasSubdialStatic(cx - 34, cy + 24, "SEC");
    drawCanvasSubdialStatic(cx + 34, cy + 24, "MIN");

    clockFaceCanvas.fillRoundRect(cx + 35, cy - 10, 35, 20, 3, display.color565(232, 236, 230));
    clockFaceCanvas.drawRoundRect(cx + 35, cy - 10, 35, 20, 3, gold);
    clockFaceCanvas.setTextColor(ST77XX_BLACK);
    clockFaceCanvas.setTextSize(2);
    clockFaceCanvas.setCursor(cx + 41, cy - 5);
    clockFaceCanvas.printf("%02d", now.tm_mday);
}

void pushClockFaceCanvasRegion(int x, int y, int w, int h) {
    const int clippedX = constrain(x, CLOCK_FACE_CANVAS_X, CLOCK_FACE_CANVAS_X + CLOCK_FACE_CANVAS_SIZE - 1);
    const int clippedY = constrain(y, CLOCK_FACE_CANVAS_Y, CLOCK_FACE_CANVAS_Y + CLOCK_FACE_CANVAS_SIZE - 1);
    const int clippedX2 = constrain(x + w, CLOCK_FACE_CANVAS_X, CLOCK_FACE_CANVAS_X + CLOCK_FACE_CANVAS_SIZE);
    const int clippedY2 = constrain(y + h, CLOCK_FACE_CANVAS_Y, CLOCK_FACE_CANVAS_Y + CLOCK_FACE_CANVAS_SIZE);
    const int drawW = clippedX2 - clippedX;
    const int drawH = clippedY2 - clippedY;

    if (drawW <= 0 || drawH <= 0) {
        return;
    }

    const uint16_t *buffer = clockFaceCanvas.getBuffer();
    const int localX = clippedX - CLOCK_FACE_CANVAS_X;
    const int localY = clippedY - CLOCK_FACE_CANVAS_Y;

    display.startWrite();
    for (int row = 0; row < drawH; ++row) {
        display.setAddrWindow(clippedX, clippedY + row, drawW, 1);
        display.writePixels(const_cast<uint16_t *>(buffer + ((localY + row) * CLOCK_FACE_CANVAS_SIZE) + localX),
                            drawW, true, false);
    }
    display.endWrite();
}

void clockHandBounds(const tm &now, int &x, int &y, int &w, int &h) {
    const int cx = CLOCK_FACE_CENTER_X;
    const int cy = CLOCK_FACE_CENTER_Y;
    const float secondAngle = (now.tm_sec - 15) * PI / 30.0f;
    const float minuteAngle = (now.tm_min - 15) * PI / 30.0f;
    const float hourAngle = (((now.tm_hour % 12) * 5) + (now.tm_min / 12) - 15) * PI / 30.0f;
    const int xs[] = {
        cx,
        static_cast<int>(cx + cos(hourAngle) * 34),
        static_cast<int>(cx + cos(minuteAngle) * 50),
        static_cast<int>(cx + cos(secondAngle) * 60),
        static_cast<int>(cx - cos(secondAngle) * 14),
    };
    const int ys[] = {
        cy,
        static_cast<int>(cy + sin(hourAngle) * 34),
        static_cast<int>(cy + sin(minuteAngle) * 50),
        static_cast<int>(cy + sin(secondAngle) * 60),
        static_cast<int>(cy - sin(secondAngle) * 14),
    };

    int minX = xs[0];
    int maxX = xs[0];
    int minY = ys[0];
    int maxY = ys[0];

    for (int i = 1; i < 5; ++i) {
        minX = min(minX, xs[i]);
        maxX = max(maxX, xs[i]);
        minY = min(minY, ys[i]);
        maxY = max(maxY, ys[i]);
    }

    const int pad = 12;
    x = minX - pad;
    y = minY - pad;
    w = (maxX - minX) + pad * 2;
    h = (maxY - minY) + pad * 2;
}

void restoreClockHandRegions(const tm &now) {
    int currentX = 0;
    int currentY = 0;
    int currentW = 0;
    int currentH = 0;
    clockHandBounds(now, currentX, currentY, currentW, currentH);
    pushClockFaceCanvasRegion(currentX, currentY, currentW, currentH);

    if (clockHandsDrawn) {
        int previousX = 0;
        int previousY = 0;
        int previousW = 0;
        int previousH = 0;
        clockHandBounds(previousClockHandsTime, previousX, previousY, previousW, previousH);
        pushClockFaceCanvasRegion(previousX, previousY, previousW, previousH);
    }
}

void drawClockDynamicContent(const tm &now, bool redrawDatePanels) {
    const uint16_t panel = display.color565(12, 18, 25);
    const uint16_t line = display.color565(42, 52, 62);
    const uint16_t gold = display.color565(220, 176, 72);
    const uint16_t cyan = display.color565(0, 220, 255);

    const char *weekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    const char *months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                            "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

    display.fillRoundRect(203, 48, 106, 40, 5, panel);
    display.drawRoundRect(203, 48, 106, 40, 5, line);
    display.setTextColor(cyan);
    display.setTextSize(3);
    display.setCursor(211, 58);
    display.printf("%02d:%02d", now.tm_hour, now.tm_min);
    display.setTextColor(gold);
    display.setTextSize(1);
    display.setCursor(284, 76);
    display.printf("%02d", now.tm_sec);

    if (redrawDatePanels) {
        display.fillRoundRect(203, 98, 106, 44, 5, panel);
        display.drawRoundRect(203, 98, 106, 44, 5, line);
        display.setTextColor(ST77XX_WHITE);
        display.setTextSize(2);
        display.setCursor(213, 108);
        display.printf("%s", weekdays[now.tm_wday]);
        display.setTextColor(gold);
        display.setTextSize(1);
        display.setCursor(213, 130);
        display.printf("%02d %s %04d", now.tm_mday, months[now.tm_mon], now.tm_year + 1900);

        display.fillRoundRect(203, 152, 106, 40, 5, panel);
        display.drawRoundRect(203, 152, 106, 40, 5, line);
        display.setTextColor(display.color565(150, 160, 172));
        display.setTextSize(1);
        display.setCursor(213, 162);
        display.print("CALIBRE");
        display.setTextColor(display.color565(0, 230, 150));
        display.setCursor(213, 178);
        display.print("AUTO QUARTZ");
    }
}

void drawClockStaticPage(const tm &now) {
    const uint16_t bg = display.color565(5, 8, 12);

    display.fillRect(0, 34, DISPLAY_WIDTH, 174, bg);

    for (int y = 36; y < 208; y += 6) {
        const uint8_t shade = 8 + ((y - 36) / 6);
        display.drawFastHLine(0, y, DISPLAY_WIDTH, display.color565(shade, shade + 3, shade + 8));
    }

    buildClockFaceCanvas(now);
    display.drawRGBBitmap(CLOCK_FACE_CANVAS_X, CLOCK_FACE_CANVAS_Y,
                          clockFaceCanvas.getBuffer(), CLOCK_FACE_CANVAS_SIZE, CLOCK_FACE_CANVAS_SIZE);
    drawClockSubdialHands(now);
    drawClockDynamicContent(now, true);
    clockStaticDrawn = true;
    clockHandsDrawn = false;
}

void updateClockHandsAndTime(bool forceStatic) {
    const time_t epoch = currentClockEpoch();
    tm now = {};
    localtime_r(&epoch, &now);

    if (forceStatic || !clockStaticDrawn) {
        drawClockStaticPage(now);
    }

    restoreClockHandRegions(now);
    drawClockHands(now);
    drawClockSubdialHands(now);
    drawClockDynamicContent(now, false);
    previousClockHandsTime = now;
    clockHandsDrawn = true;
}

void drawClockPageContent() {
    updateClockHandsAndTime(true);
}

String clippedText(const String &text, int maxChars) {
    if (text.length() <= static_cast<unsigned int>(maxChars)) {
        return text;
    }

    return text.substring(0, maxChars - 1) + "~";
}

void drawSettingsContentArea() {
    display.fillRect(0, 34, DISPLAY_WIDTH, 174, display.color565(10, 14, 22));
}

void drawSettingsStatus(const char *line1, const char *line2, uint16_t color) {
    drawLauncherBackground();
    drawTopBar("WiFi Settings");
    drawSettingsContentArea();

    display.setTextColor(color);
    display.setTextSize(2);
    display.setCursor(18, 78);
    display.print(line1);

    display.setTextColor(display.color565(210, 218, 232));
    display.setTextSize(1);
    display.setCursor(18, 116);
    display.print(line2);

    drawFooter();
}

void drawSettingsMain() {
    settingsView = SettingsView::Main;
    drawLauncherBackground();
    drawTopBar("Settings");
    drawSettingsContentArea();

    const char *itemLabels[SETTINGS_ITEM_COUNT] = {
        "WiFi",
        "Button Buzzer Sound",
    };

    for (int i = 0; i < SETTINGS_ITEM_COUNT; ++i) {
        const int y = 58 + i * 48;
        const bool selected = i == selectedSettingsIndex;
        const uint16_t fill = selected ? display.color565(18, 86, 122) : display.color565(18, 23, 34);
        const uint16_t border = selected ? display.color565(0, 220, 255) : display.color565(46, 52, 64);

        display.fillRoundRect(10, y, 300, 36, 5, fill);
        display.drawRoundRect(10, y, 300, 36, 5, border);

        display.setTextColor(selected ? ST77XX_WHITE : display.color565(210, 218, 232));
        display.setTextSize(1);
        display.setCursor(20, y + 9);
        display.print(itemLabels[i]);

        display.setTextColor(display.color565(128, 203, 196));
        display.setCursor(20, y + 22);
        if (i == SETTINGS_WIFI_INDEX) {
            if (WiFi.status() == WL_CONNECTED) {
                display.print(clippedText(WiFi.SSID(), 28));
            } else if (configuredWifiSsid().length() > 0) {
                display.print(clippedText(configuredWifiSsid(), 28));
            } else {
                display.print("Not configured");
            }
        } else {
            display.print(buttonBuzzerEnabled ? "Enabled" : "Disabled");
        }

        if (i == SETTINGS_BUTTON_BUZZER_INDEX) {
            display.setTextColor(buttonBuzzerEnabled ? display.color565(0, 230, 150) : ST77XX_YELLOW);
            display.setCursor(252, y + 15);
            display.print(buttonBuzzerEnabled ? "ON" : "OFF");
        }
    }

    drawFooter();
}

void drawSettingsWifiList() {
    drawLauncherBackground();
    drawTopBar("WiFi Settings");
    drawSettingsContentArea();

    display.setTextSize(1);
    if (WiFi.status() == WL_CONNECTED) {
        display.setTextColor(display.color565(0, 230, 150));
        display.setCursor(12, 42);
        display.print("Connected: ");
        display.print(clippedText(WiFi.SSID(), 28));
    } else {
        display.setTextColor(display.color565(210, 218, 232));
        display.setCursor(12, 42);
        display.print("Not connected");
    }

    if (scannedWifiCount <= 0) {
        display.setTextColor(ST77XX_YELLOW);
        display.setTextSize(2);
        display.setCursor(54, 96);
        display.print("No WiFi found");
        display.setTextSize(1);
        display.setTextColor(display.color565(210, 218, 232));
        display.setCursor(80, 134);
        display.print("Press OK to scan again");
        drawFooter();
        return;
    }

    int firstIndex = selectedWifiIndex - 1;
    firstIndex = constrain(firstIndex, 0, max(0, scannedWifiCount - 4));

    for (int row = 0; row < 4; ++row) {
        const int networkIndex = firstIndex + row;
        if (networkIndex >= scannedWifiCount) {
            break;
        }

        const int y = 64 + row * 34;
        const bool selected = networkIndex == selectedWifiIndex;
        const uint16_t fill = selected ? display.color565(18, 86, 122) : display.color565(18, 23, 34);
        const uint16_t border = selected ? display.color565(0, 220, 255) : display.color565(46, 52, 64);

        display.fillRoundRect(10, y, 300, 28, 5, fill);
        display.drawRoundRect(10, y, 300, 28, 5, border);
        display.setTextColor(selected ? ST77XX_WHITE : display.color565(210, 218, 232));
        display.setTextSize(1);
        display.setCursor(20, y + 10);
        display.print(clippedText(scannedWifiSsids[networkIndex], 28));
        display.setCursor(254, y + 10);
        display.printf("%ld", static_cast<long>(scannedWifiRssi[networkIndex]));
    }

    drawFooter();
}

void scanWifiNetworks() {
    settingsView = SettingsView::WiFiList;
    drawSettingsStatus("Scanning", "Searching for nearby WiFi", display.color565(0, 220, 255));

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(false, false);
    delay(150);
    const int found = WiFi.scanNetworks(false, true);
    scannedWifiCount = 0;

    for (int i = 0; i < found && scannedWifiCount < MAX_SCANNED_NETWORKS; ++i) {
        const String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) {
            continue;
        }

        scannedWifiSsids[scannedWifiCount] = ssid;
        scannedWifiRssi[scannedWifiCount] = WiFi.RSSI(i);
        ++scannedWifiCount;
    }

    WiFi.scanDelete();
    selectedWifiIndex = 0;
    drawSettingsWifiList();
}

void drawWifiPasswordEntry() {
    drawLauncherBackground();
    drawTopBar("WiFi Password");
    drawSettingsContentArea();

    display.setTextColor(display.color565(210, 218, 232));
    display.setTextSize(1);
    display.setCursor(14, 46);
    display.print(clippedText(pendingWifiSsid, 32));

    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(2);
    display.setCursor(18, 82);
    display.print(numericWifiPassword);

    const int cursorX = 18 + numericWifiPassword.length() * 12;
    display.fillRoundRect(cursorX - 2, 78, 16, 24, 4, display.color565(0, 220, 255));
    display.setTextColor(ST77XX_BLACK);
    display.setCursor(cursorX + 2, 82);
    display.print(currentPasswordDigit);

    display.setTextColor(display.color565(210, 218, 232));
    display.setTextSize(1);
    display.setCursor(18, 128);
    display.printf("%d/%d digits", numericWifiPassword.length(), MAX_NUMERIC_PASSWORD_DIGITS);
    display.setCursor(18, 152);
    display.print("Hold OK connects");
    display.setCursor(18, 170);
    display.print("Double Back exits");

    drawFooter();
}

void drawSettingsResult() {
    settingsView = SettingsView::ConnectResult;
    drawSettingsStatus(settingsResultLine1.c_str(), settingsResultLine2.c_str(),
                       WiFi.status() == WL_CONNECTED ? display.color565(0, 230, 150) : ST77XX_RED);
}

void saveWifiCredentials(const String &ssid, const String &password) {
    if (!wifiPreferences.begin(WIFI_PREF_NAMESPACE, false)) {
        Serial.println("WiFi preferences save failed: could not open NVS namespace");
        return;
    }

    wifiPreferences.putString(WIFI_PREF_SSID_KEY, ssid);
    wifiPreferences.putString(WIFI_PREF_PASSWORD_KEY, password);
    wifiPreferences.end();

    savedWifiSsid = ssid;
    savedWifiPassword = password;
}

const char *wifiStatusText(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS:
            return "Idle";
        case WL_NO_SSID_AVAIL:
            return "SSID not found";
        case WL_SCAN_COMPLETED:
            return "Scan done";
        case WL_CONNECTED:
            return "Connected";
        case WL_CONNECT_FAILED:
            return "Auth failed";
        case WL_CONNECTION_LOST:
            return "Signal lost";
        case WL_DISCONNECTED:
            return "Disconnected";
        default:
            return "Unknown";
    }
}

wl_status_t connectWifiBlocking(const String &ssid, const String &password) {
    wl_status_t status = WL_DISCONNECTED;

    for (int attempt = 1; attempt <= WIFI_CONNECT_ATTEMPTS; ++attempt) {
        Serial.printf("WiFi connect attempt %d/%d: ssid=%s password_len=%u\n",
                      attempt, WIFI_CONNECT_ATTEMPTS, ssid.c_str(), password.length());

        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.disconnect(false, true);
        delay(500);
        WiFi.begin(ssid.c_str(), password.c_str());

        const uint32_t startedAt = millis();
        while (millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
            status = WiFi.status();
            if (status == WL_CONNECTED) {
                Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
                return status;
            }

            delay(250);
        }

        status = WiFi.status();
        Serial.printf("WiFi attempt %d failed: status=%d (%s)\n",
                      attempt, static_cast<int>(status), wifiStatusText(status));
    }

    return status;
}

void connectToPendingWifiAndSave() {
    if (numericWifiPassword.length() < 8) {
        settingsResultLine1 = "Password too short";
        settingsResultLine2 = "WiFi needs 8+ digits";
        drawSettingsResult();
        delay(1400);
        drawWifiPasswordEntry();
        return;
    }

    drawSettingsStatus("Connecting", pendingWifiSsid.c_str(), display.color565(0, 220, 255));

    const wl_status_t status = connectWifiBlocking(pendingWifiSsid, numericWifiPassword);

    if (status == WL_CONNECTED) {
        saveWifiCredentials(pendingWifiSsid, numericWifiPassword);
        settingsResultLine1 = "Connected";
        settingsResultLine2 = WiFi.localIP().toString();
    } else {
        settingsResultLine1 = "Connect failed";
        settingsResultLine2 = String(wifiStatusText(status)) + " (" + String(static_cast<int>(status)) + ")";
        WiFi.disconnect(false, false);
        drawSettingsResult();
        delay(2200);
        scanWifiNetworks();
        return;
    }

    drawSettingsResult();
}

void beginWifiPasswordEntry() {
    if (scannedWifiCount <= 0) {
        scanWifiNetworks();
        return;
    }

    settingsView = SettingsView::PasswordEntry;
    pendingWifiSsid = scannedWifiSsids[selectedWifiIndex];
    numericWifiPassword = "";
    currentPasswordDigit = 0;
    lastPasswordBackMs = 0;
    drawWifiPasswordEntry();
}

void drawSettingsPage() {
    selectedSettingsIndex = 0;
    drawSettingsMain();
}

void drawCctvHeader() {
    char title[24] = {};
    snprintf(title, sizeof(title), "CCTV CH%d", currentCctvChannel);
    drawTopBar(title);
}

void drawCctvStatus(const char *line1, const char *line2, uint16_t color) {
    display.fillRect(CCTV_VIEW_X, CCTV_VIEW_Y, CCTV_VIEW_W, CCTV_VIEW_H, display.color565(5, 8, 14));

    display.drawRoundRect(96, 58, 128, 86, 8, display.color565(46, 68, 92));
    drawCctvGlyph(160, 96, color);

    display.setTextColor(color);
    display.setTextSize(1);
    display.setCursor(12, 158);
    display.print(line1);

    display.setTextColor(display.color565(210, 218, 232));
    display.setCursor(12, 176);
    display.print(line2);
}

bool initialWifiCredentialsConfigured() {
    return strlen(INITIAL_WIFI_SSID) > 0 &&
           strcmp(INITIAL_WIFI_SSID, "YOUR_WIFI_SSID") != 0;
}

String configuredWifiSsid() {
    if (savedWifiSsid.length() > 0) {
        return savedWifiSsid;
    }

    return initialWifiCredentialsConfigured() ? String(INITIAL_WIFI_SSID) : String("");
}

String configuredWifiPassword() {
    if (savedWifiSsid.length() > 0) {
        return savedWifiPassword;
    }

    return initialWifiCredentialsConfigured() ? String(INITIAL_WIFI_PASSWORD) : String("");
}

bool wifiCredentialsConfigured() {
    return configuredWifiSsid().length() > 0;
}

bool cctvSettingsConfigured() {
    return strlen(CCTV_DVR_HOST) > 0 &&
           strcmp(CCTV_DVR_HOST, "YOUR_DVR_HOST_OR_IP") != 0 &&
           strlen(CCTV_DVR_USER) > 0 &&
           strcmp(CCTV_DVR_USER, "YOUR_DVR_USER") != 0 &&
           strcmp(CCTV_DVR_PASSWORD, "YOUR_DVR_PASSWORD") != 0;
}

bool ensureWifiConnected() {
    const String ssid = configuredWifiSsid();
    const String password = configuredWifiPassword();

    if (!wifiCredentialsConfigured()) {
        drawCctvStatus("WiFi is not configured", "Open Settings and select WiFi", ST77XX_YELLOW);
        drawFooter();
        return false;
    }

    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    drawCctvStatus("Connecting to WiFi", ssid.c_str(), display.color565(0, 220, 255));
    drawFooter();

    const uint32_t startedAt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 10000) {
        delay(250);
    }

    if (WiFi.status() != WL_CONNECTED) {
        drawCctvStatus("WiFi connection failed", "Check SSID/password and range", ST77XX_RED);
        drawFooter();
        return false;
    }

    Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

bool ensureCctvBuffer() {
    if (cctvFrameBuffer != nullptr) {
        return true;
    }

    if (psramFound()) {
        cctvFrameBuffer = static_cast<uint8_t *>(ps_malloc(CCTV_MAX_JPEG_BYTES));
    }

    if (cctvFrameBuffer == nullptr) {
        cctvFrameBuffer = static_cast<uint8_t *>(malloc(CCTV_MAX_JPEG_BYTES));
    }

    if (cctvFrameBuffer == nullptr) {
        drawCctvStatus("Frame buffer allocation failed", "Lower snapshot resolution on DVR", ST77XX_RED);
        drawFooter();
        return false;
    }

    return true;
}

String cctvSnapshotUri() {
    return String(CCTV_SNAPSHOT_PATH) + String(currentCctvChannel);
}

String md5Hex(const String &input) {
    MD5Builder md5;
    md5.begin();
    md5.add(reinterpret_cast<const uint8_t *>(input.c_str()), input.length());
    md5.calculate();
    return md5.toString();
}

String randomHex(size_t length) {
    static const char hex[] = "0123456789abcdef";
    String out;
    out.reserve(length);

    for (size_t i = 0; i < length; ++i) {
        out += hex[esp_random() & 0x0F];
    }

    return out;
}

String extractHeaderValue(const String &headers, const String &name) {
    const String needle = name + ":";
    const int start = headers.indexOf(needle);

    if (start < 0) {
        return "";
    }

    const int valueStart = start + needle.length();
    const int lineEnd = headers.indexOf("\r\n", valueStart);
    String value = headers.substring(valueStart, lineEnd < 0 ? headers.length() : lineEnd);
    value.trim();
    return value;
}

String extractDigestField(const String &header, const char *field) {
    const String key = String(field) + "=\"";
    const int start = header.indexOf(key);

    if (start < 0) {
        return "";
    }

    const int valueStart = start + key.length();
    const int valueEnd = header.indexOf('"', valueStart);

    if (valueEnd < 0) {
        return "";
    }

    return header.substring(valueStart, valueEnd);
}

bool readCctvHttpHeaders(String &headers) {
    headers = "";
    const uint32_t startMs = millis();

    while (millis() - startMs < CCTV_HTTP_TIMEOUT_MS) {
        while (cctvSecureClient.available()) {
            headers += static_cast<char>(cctvSecureClient.read());
            if (headers.endsWith("\r\n\r\n")) {
                return true;
            }
        }

        if (!cctvSecureClient.connected() && !cctvSecureClient.available()) {
            break;
        }

        delay(1);
    }

    return false;
}

int parseCctvStatusCode(const String &headers) {
    const int lineEnd = headers.indexOf("\r\n");

    if (lineEnd < 0) {
        return -1;
    }

    const String statusLine = headers.substring(0, lineEnd);
    const int firstSpace = statusLine.indexOf(' ');

    if (firstSpace < 0 || firstSpace + 4 > statusLine.length()) {
        return -1;
    }

    return statusLine.substring(firstSpace + 1, firstSpace + 4).toInt();
}

String buildCctvDigestAuthHeader(const String &challenge, const String &uri) {
    const String realm = extractDigestField(challenge, "realm");
    const String nonce = extractDigestField(challenge, "nonce");
    const String qopField = extractDigestField(challenge, "qop");
    const String opaque = extractDigestField(challenge, "opaque");
    const String qop = qopField.indexOf("auth") >= 0 ? "auth" : qopField;
    const String nc = "00000001";
    const String cnonce = randomHex(16);

    if (realm.isEmpty() || nonce.isEmpty()) {
        return "";
    }

    const String ha1 = md5Hex(String(CCTV_DVR_USER) + ":" + realm + ":" + CCTV_DVR_PASSWORD);
    const String ha2 = md5Hex(String("GET:") + uri);
    const String response = qop.isEmpty()
                                ? md5Hex(ha1 + ":" + nonce + ":" + ha2)
                                : md5Hex(ha1 + ":" + nonce + ":" + nc + ":" + cnonce + ":" + qop + ":" + ha2);

    String auth = "Digest username=\"" + String(CCTV_DVR_USER) + "\"";
    auth += ", realm=\"" + realm + "\"";
    auth += ", nonce=\"" + nonce + "\"";
    auth += ", uri=\"" + uri + "\"";
    auth += ", algorithm=MD5";
    auth += ", response=\"" + response + "\"";

    if (!opaque.isEmpty()) {
        auth += ", opaque=\"" + opaque + "\"";
    }

    if (!qop.isEmpty()) {
        auth += ", qop=" + qop;
        auth += ", nc=" + nc;
        auth += ", cnonce=\"" + cnonce + "\"";
    }

    return auth;
}

bool sendCctvSnapshotRequest(const String &uri, const String &authHeader, String &headers) {
    cctvSecureClient.stop();
    cctvSecureClient.setInsecure();
    cctvSecureClient.setTimeout(CCTV_HTTP_TIMEOUT_MS / 1000);

    if (!cctvSecureClient.connect(CCTV_DVR_HOST, CCTV_DVR_PORT)) {
        drawCctvStatus("HTTPS connect failed", CCTV_DVR_HOST, ST77XX_RED);
        drawFooter();
        return false;
    }

    String request = String("GET ") + uri + " HTTP/1.1\r\n" +
                     "Host: " + CCTV_DVR_HOST + "\r\n" +
                     "User-Agent: ESP32-CCTV-Snapshot/1.0\r\n" +
                     "Connection: close\r\n" +
                     "Accept: image/jpeg,*/*\r\n";

    if (!authHeader.isEmpty()) {
        request += "Authorization: " + authHeader + "\r\n";
    }

    request += "\r\n";
    cctvSecureClient.print(request);

    if (!readCctvHttpHeaders(headers)) {
        drawCctvStatus("DVR header timeout", "No HTTP response headers", ST77XX_RED);
        drawFooter();
        cctvSecureClient.stop();
        return false;
    }

    return true;
}

bool fetchCctvSnapshot(size_t &frameSize) {
    frameSize = 0;

    if (!cctvSettingsConfigured()) {
        drawCctvStatus("CCTV is not configured", "Create include/secrets.h", ST77XX_YELLOW);
        drawFooter();
        return false;
    }

    if (!ensureWifiConnected() || !ensureCctvBuffer()) {
        return false;
    }

    const String uri = cctvSnapshotUri();
    String headers;

    if (!sendCctvSnapshotRequest(uri, "", headers)) {
        return false;
    }

    int statusCode = parseCctvStatusCode(headers);

    if (statusCode == 401) {
        const String challenge = extractHeaderValue(headers, "WWW-Authenticate");
        const String authHeader = buildCctvDigestAuthHeader(challenge, uri);
        cctvSecureClient.stop();

        if (authHeader.isEmpty()) {
            drawCctvStatus("Digest auth failed", "Could not parse challenge", ST77XX_RED);
            drawFooter();
            return false;
        }

        if (!sendCctvSnapshotRequest(uri, authHeader, headers)) {
            return false;
        }

        statusCode = parseCctvStatusCode(headers);
    }

    if (statusCode != 200) {
        char status[40] = {};
        snprintf(status, sizeof(status), "DVR HTTP error: %d", statusCode);
        drawCctvStatus(status, "Check HTTPS Digest settings", ST77XX_RED);
        drawFooter();
        cctvSecureClient.stop();
        return false;
    }

    const int contentLength = extractHeaderValue(headers, "Content-Length").toInt();

    if (contentLength > static_cast<int>(CCTV_MAX_JPEG_BYTES)) {
        drawCctvStatus("Snapshot is too large", "Lower DVR snapshot resolution", ST77XX_RED);
        drawFooter();
        cctvSecureClient.stop();
        return false;
    }

    WiFiClient *stream = &cctvSecureClient;
    uint32_t lastReadAt = millis();

    while ((cctvSecureClient.connected() || stream->available()) &&
           (contentLength <= 0 || frameSize < static_cast<size_t>(contentLength))) {
        const size_t available = stream->available();

        if (available > 0) {
            const size_t room = CCTV_MAX_JPEG_BYTES - frameSize;
            const size_t chunk = available < room ? available : room;
            const int bytesRead = stream->readBytes(cctvFrameBuffer + frameSize, chunk);

            if (bytesRead <= 0) {
                break;
            }

            frameSize += static_cast<size_t>(bytesRead);
            lastReadAt = millis();

            if (frameSize >= CCTV_MAX_JPEG_BYTES) {
                break;
            }
        } else if (millis() - lastReadAt > CCTV_HTTP_TIMEOUT_MS) {
            break;
        } else {
            delay(1);
        }
    }

    cctvSecureClient.stop();

    if (contentLength > 0 && frameSize != static_cast<size_t>(contentLength)) {
        drawCctvStatus("Snapshot read timeout", "Incomplete JPEG body", ST77XX_RED);
        drawFooter();
        return false;
    }

    if (frameSize < 4 || cctvFrameBuffer[0] != 0xFF || cctvFrameBuffer[1] != 0xD8) {
        drawCctvStatus("DVR did not return JPEG", "Check snapshot CGI response", ST77XX_RED);
        drawFooter();
        return false;
    }

    Serial.printf("CCTV CH%d snapshot: %u bytes\n", currentCctvChannel, static_cast<unsigned>(frameSize));
    return true;
}

int drawCctvJpegBlock(JPEGDRAW *pDraw) {
    if (pDraw == nullptr || pDraw->pPixels == nullptr) {
        return 0;
    }

    const int blockX = currentCctvFrameX + pDraw->x;
    const int blockY = currentCctvFrameY + pDraw->y;
    const int blockW = pDraw->iWidthUsed;
    const int blockH = pDraw->iHeight;

    if (blockW <= 0 || blockH <= 0) {
        return 0;
    }

    int srcOffsetX = 0;
    int drawX = blockX;
    int drawW = blockW;

    if (drawX < CCTV_VIEW_X) {
        srcOffsetX = CCTV_VIEW_X - drawX;
        drawW -= srcOffsetX;
        drawX = CCTV_VIEW_X;
    }

    if (drawX + drawW > CCTV_VIEW_X + CCTV_VIEW_W) {
        drawW = CCTV_VIEW_X + CCTV_VIEW_W - drawX;
    }

    int srcOffsetY = 0;
    int drawY = blockY;
    int drawH = blockH;

    if (drawY < CCTV_VIEW_Y) {
        srcOffsetY = CCTV_VIEW_Y - drawY;
        drawH -= srcOffsetY;
        drawY = CCTV_VIEW_Y;
    }

    if (drawY + drawH > CCTV_VIEW_Y + CCTV_VIEW_H) {
        drawH = CCTV_VIEW_Y + CCTV_VIEW_H - drawY;
    }

    if (drawW <= 0 || drawH <= 0) {
        return 1;
    }

    display.startWrite();
    for (int row = 0; row < drawH; ++row) {
        const uint16_t *pixels = pDraw->pPixels + ((srcOffsetY + row) * pDraw->iWidth) + srcOffsetX;
        display.setAddrWindow(drawX, drawY + row, drawW, 1);
        display.writePixels(const_cast<uint16_t *>(pixels), drawW, true, true);
    }
    display.endWrite();

    return 1;
}

int chooseJpegScale(int sourceWidth, int sourceHeight, int &scaledWidth, int &scaledHeight) {
    const int divisors[] = {8, 4, 2, 1};
    const int options[] = {JPEG_SCALE_EIGHTH, JPEG_SCALE_QUARTER, JPEG_SCALE_HALF, 0};
    int divisor = 1;
    int option = 0;

    for (int i = 0; i < 4; ++i) {
        const int candidateDivisor = divisors[i];
        const int candidateWidth = max(1, sourceWidth / candidateDivisor);
        const int candidateHeight = max(1, sourceHeight / candidateDivisor);

        if (candidateWidth >= CCTV_VIEW_W && candidateHeight >= CCTV_VIEW_H) {
            divisor = candidateDivisor;
            option = options[i];
            break;
        }
    }

    scaledWidth = max(1, sourceWidth / divisor);
    scaledHeight = max(1, sourceHeight / divisor);
    return option;
}

bool decodeCctvSnapshot(size_t frameSize) {
    if (!jpeg.openRAM(cctvFrameBuffer, static_cast<int>(frameSize), drawCctvJpegBlock)) {
        drawCctvStatus("Could not decode JPEG", "Unsupported snapshot format", ST77XX_RED);
        drawFooter();
        return false;
    }

    int scaledWidth = 0;
    int scaledHeight = 0;
    const int option = chooseJpegScale(jpeg.getWidth(), jpeg.getHeight(), scaledWidth, scaledHeight);
    currentCctvFrameX = CCTV_VIEW_X + ((CCTV_VIEW_W - scaledWidth) / 2);
    currentCctvFrameY = CCTV_VIEW_Y + ((CCTV_VIEW_H - scaledHeight) / 2);

    display.fillRect(CCTV_VIEW_X, CCTV_VIEW_Y, CCTV_VIEW_W, CCTV_VIEW_H, ST77XX_BLACK);
    jpeg.setPixelType(RGB565_BIG_ENDIAN);
    const bool decoded = jpeg.decode(0, 0, option) == 1;
    jpeg.close();

    if (!decoded) {
        drawCctvStatus("JPEG decode failed", "Try lower DVR snapshot quality", ST77XX_RED);
        drawFooter();
        return false;
    }

    return true;
}

void refreshCctvFrame(bool showLoading) {
    drawCctvHeader();

    if (!cctvSettingsConfigured()) {
        drawCctvStatus("CCTV is not configured", "Create include/secrets.h", ST77XX_YELLOW);
        lastCctvFrameMs = millis();
        drawFooter();
        return;
    }

    if (showLoading) {
        char loading[32] = {};
        snprintf(loading, sizeof(loading), "Loading CCTV CH%d", currentCctvChannel);
        drawCctvStatus(loading, CCTV_DVR_HOST, display.color565(0, 220, 255));
        drawFooter();
    }

    size_t frameSize = 0;
    if (fetchCctvSnapshot(frameSize)) {
        decodeCctvSnapshot(frameSize);
    }

    lastCctvFrameMs = millis();

    drawFooter();
}

void drawCctvPage() {
    drawLauncherBackground();
    refreshCctvFrame(true);
}

const char *weatherCodeText(int code) {
    if (code == 0) {
        return "Clear";
    }
    if (code == 1 || code == 2) {
        return "Partly cloudy";
    }
    if (code == 3) {
        return "Cloudy";
    }
    if (code == 45 || code == 48) {
        return "Fog";
    }
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
        return "Rain";
    }
    if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
        return "Snow";
    }
    if (code >= 95) {
        return "Thunder";
    }
    return "Weather";
}

uint16_t weatherAccentColor(int code) {
    if (code == 0 || code == 1) {
        return ST77XX_YELLOW;
    }
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82) || code >= 95) {
        return display.color565(0, 220, 255);
    }
    return display.color565(128, 203, 196);
}

void drawWeatherIcon(int cx, int cy, int code, uint16_t color) {
    if (code == 0 || code == 1) {
        display.fillCircle(cx, cy, 20, ST77XX_YELLOW);
        for (int i = 0; i < 8; ++i) {
            const float angle = i * PI / 4.0f;
            display.drawLine(cx + cos(angle) * 26, cy + sin(angle) * 26,
                             cx + cos(angle) * 33, cy + sin(angle) * 33, ST77XX_YELLOW);
        }
        return;
    }

    display.fillCircle(cx - 16, cy, 16, color);
    display.fillCircle(cx + 2, cy - 8, 20, color);
    display.fillCircle(cx + 22, cy + 2, 17, color);
    display.fillRoundRect(cx - 28, cy, 62, 22, 8, color);

    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82) || code >= 95) {
        const uint16_t rain = display.color565(0, 220, 255);
        display.drawLine(cx - 18, cy + 32, cx - 24, cy + 42, rain);
        display.drawLine(cx, cy + 32, cx - 6, cy + 42, rain);
        display.drawLine(cx + 18, cy + 32, cx + 12, cy + 42, rain);
    }
}

void drawWeatherStatus(const char *line1, const char *line2, uint16_t color) {
    drawLauncherBackground();
    drawTopBar("Weather");
    display.fillRect(0, 34, DISPLAY_WIDTH, 174, display.color565(10, 14, 22));
    drawWeatherIcon(160, 96, 3, color);

    display.setTextColor(color);
    display.setTextSize(2);
    display.setCursor(18, 150);
    display.print(line1);

    display.setTextColor(display.color565(210, 218, 232));
    display.setTextSize(1);
    display.setCursor(18, 178);
    display.print(line2);
    drawFooter();
}

bool readWeatherHeaders(String &headers) {
    headers = "";
    const uint32_t startMs = millis();

    while (millis() - startMs < WEATHER_HTTP_TIMEOUT_MS) {
        while (weatherClient.available()) {
            headers += static_cast<char>(weatherClient.read());
            if (headers.endsWith("\r\n\r\n")) {
                return true;
            }
        }

        if (!weatherClient.connected() && !weatherClient.available()) {
            break;
        }

        delay(1);
    }

    return false;
}

bool readWeatherBody(String &body, int contentLength) {
    body = "";
    body.reserve(contentLength > 0 ? min(contentLength, 4096) : 2048);
    const uint32_t startMs = millis();

    while ((weatherClient.connected() || weatherClient.available()) &&
           millis() - startMs < WEATHER_HTTP_TIMEOUT_MS) {
        while (weatherClient.available()) {
            body += static_cast<char>(weatherClient.read());
            if (contentLength > 0 && body.length() >= static_cast<unsigned int>(contentLength)) {
                return true;
            }
        }
        delay(1);
    }

    return body.length() > 0;
}

bool readWeatherChunkedBody(String &body) {
    body = "";
    body.reserve(2048);
    const uint32_t startMs = millis();

    while (millis() - startMs < WEATHER_HTTP_TIMEOUT_MS) {
        String sizeLine = weatherClient.readStringUntil('\n');
        sizeLine.trim();

        if (sizeLine.length() == 0) {
            if (!weatherClient.connected() && !weatherClient.available()) {
                break;
            }
            delay(1);
            continue;
        }

        const int chunkSize = static_cast<int>(strtol(sizeLine.c_str(), nullptr, 16));
        if (chunkSize <= 0) {
            return body.length() > 0;
        }

        int readCount = 0;
        while (readCount < chunkSize && millis() - startMs < WEATHER_HTTP_TIMEOUT_MS) {
            if (weatherClient.available()) {
                body += static_cast<char>(weatherClient.read());
                ++readCount;
            } else {
                delay(1);
            }
        }

        if (weatherClient.available()) {
            weatherClient.read();
        }
        if (weatherClient.available()) {
            weatherClient.read();
        }
    }

    return body.length() > 0;
}

float extractJsonFloat(const String &json, const char *key, float fallback) {
    const String needle = String("\"") + key + "\":";
    const int start = json.indexOf(needle);
    if (start < 0) {
        return fallback;
    }

    const int valueStart = start + needle.length();
    return json.substring(valueStart).toFloat();
}

int extractJsonInt(const String &json, const char *key, int fallback) {
    const String needle = String("\"") + key + "\":";
    const int start = json.indexOf(needle);
    if (start < 0) {
        return fallback;
    }

    const int valueStart = start + needle.length();
    return json.substring(valueStart).toInt();
}

bool extractJsonString(const String &json, const char *key, char *out, size_t outSize) {
    const String needle = String("\"") + key + "\":\"";
    const int start = json.indexOf(needle);
    if (start < 0 || outSize == 0) {
        return false;
    }

    const int valueStart = start + needle.length();
    const int valueEnd = json.indexOf('"', valueStart);
    if (valueEnd < 0) {
        return false;
    }

    const String value = json.substring(valueStart, valueEnd);
    strlcpy(out, value.c_str(), outSize);
    return true;
}

int findJsonObjectStart(const String &json, const char *objectKey) {
    const String needle = String("\"") + objectKey + "\":{";
    const int start = json.indexOf(needle);
    return start < 0 ? -1 : start + needle.length();
}

float extractJsonFloatFrom(const String &json, int startAt, const char *key, float fallback) {
    if (startAt < 0) {
        return fallback;
    }

    const String needle = String("\"") + key + "\":";
    const int start = json.indexOf(needle, startAt);
    if (start < 0) {
        return fallback;
    }

    const int valueStart = start + needle.length();
    return json.substring(valueStart).toFloat();
}

int extractJsonIntFrom(const String &json, int startAt, const char *key, int fallback) {
    if (startAt < 0) {
        return fallback;
    }

    const String needle = String("\"") + key + "\":";
    const int start = json.indexOf(needle, startAt);
    if (start < 0) {
        return fallback;
    }

    const int valueStart = start + needle.length();
    return json.substring(valueStart).toInt();
}

bool extractJsonStringFrom(const String &json, int startAt, const char *key, char *out, size_t outSize) {
    if (startAt < 0 || outSize == 0) {
        return false;
    }

    const String needle = String("\"") + key + "\":\"";
    const int start = json.indexOf(needle, startAt);
    if (start < 0) {
        return false;
    }

    const int valueStart = start + needle.length();
    const int valueEnd = json.indexOf('"', valueStart);
    if (valueEnd < 0) {
        return false;
    }

    const String value = json.substring(valueStart, valueEnd);
    strlcpy(out, value.c_str(), outSize);
    return true;
}

bool fetchWeatherData() {
    if (!wifiCredentialsConfigured()) {
        drawWeatherStatus("WiFi needed", "Open Settings and connect WiFi", ST77XX_YELLOW);
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        const wl_status_t status = connectWifiBlocking(configuredWifiSsid(), configuredWifiPassword());
        if (status != WL_CONNECTED) {
            drawWeatherStatus("WiFi failed", wifiStatusText(status), ST77XX_RED);
            return false;
        }
    }

    weatherClient.stop();
    weatherClient.setTimeout(WEATHER_HTTP_TIMEOUT_MS / 1000);

    if (!weatherClient.connect(WEATHER_HOST, WEATHER_PORT)) {
        Serial.println("Weather HTTP connect failed");
        drawWeatherStatus("Weather failed", "HTTP connect failed", ST77XX_RED);
        return false;
    }

    char path[220] = {};
    snprintf(path, sizeof(path),
             "/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m&timezone=auto",
             WEATHER_LATITUDE, WEATHER_LONGITUDE);

    String request = String("GET ") + path + " HTTP/1.1\r\n" +
                     "Host: " + WEATHER_HOST + "\r\n" +
                     "User-Agent: ESP32-Weather-Dashboard/1.0\r\n" +
                     "Connection: close\r\n" +
                     "Accept: application/json\r\n\r\n";
    Serial.printf("Weather request: http://%s%s\n", WEATHER_HOST, path);
    weatherClient.print(request);

    String headers;
    if (!readWeatherHeaders(headers)) {
        weatherClient.stop();
        Serial.println("Weather header timeout");
        drawWeatherStatus("Weather failed", "Header timeout", ST77XX_RED);
        return false;
    }

    const int statusCode = parseCctvStatusCode(headers);
    Serial.printf("Weather HTTP status: %d\n", statusCode);
    if (statusCode != 200) {
        weatherClient.stop();
        char status[32] = {};
        snprintf(status, sizeof(status), "HTTP %d", statusCode);
        drawWeatherStatus("Weather failed", status, ST77XX_RED);
        return false;
    }

    String body;
    const int contentLength = extractHeaderValue(headers, "Content-Length").toInt();
    const bool chunked = extractHeaderValue(headers, "Transfer-Encoding").indexOf("chunked") >= 0;
    const bool bodyOk = chunked ? readWeatherChunkedBody(body) : readWeatherBody(body, contentLength);

    if (!bodyOk) {
        weatherClient.stop();
        Serial.println("Weather body read failed");
        drawWeatherStatus("Weather failed", "No JSON body", ST77XX_RED);
        return false;
    }
    weatherClient.stop();
    Serial.printf("Weather JSON bytes: %u\n", body.length());

    const int currentObjectStart = findJsonObjectStart(body, "current");
    weatherData.temperature = extractJsonFloatFrom(body, currentObjectStart, "temperature_2m", NAN);
    weatherData.apparentTemperature = extractJsonFloatFrom(body, currentObjectStart, "apparent_temperature", NAN);
    weatherData.windSpeed = extractJsonFloatFrom(body, currentObjectStart, "wind_speed_10m", NAN);
    weatherData.humidity = extractJsonIntFrom(body, currentObjectStart, "relative_humidity_2m", -1);
    weatherData.weatherCode = extractJsonIntFrom(body, currentObjectStart, "weather_code", -1);
    extractJsonStringFrom(body, currentObjectStart, "time", weatherData.time, sizeof(weatherData.time));
    Serial.printf("Weather parsed: temp=%.1f apparent=%.1f humidity=%d wind=%.1f code=%d\n",
                  weatherData.temperature, weatherData.apparentTemperature, weatherData.humidity,
                  weatherData.windSpeed, weatherData.weatherCode);

    if (isnan(weatherData.temperature) || weatherData.humidity < 0 || weatherData.weatherCode < 0) {
        Serial.println("Weather JSON parse failed");
        Serial.println(body.substring(0, 240));
        drawWeatherStatus("Weather failed", "JSON parse failed", ST77XX_RED);
        return false;
    }

    weatherDataValid = true;
    lastWeatherUpdateMs = millis();
    return true;
}

void drawWeatherMetric(int x, int y, const char *label, const char *value, uint16_t color) {
    display.fillRoundRect(x, y, 92, 38, 5, display.color565(18, 23, 34));
    display.drawRoundRect(x, y, 92, 38, 5, display.color565(46, 52, 64));
    display.setTextColor(display.color565(180, 190, 205));
    display.setTextSize(1);
    display.setCursor(x + 8, y + 7);
    display.print(label);
    display.setTextColor(color);
    display.setCursor(x + 8, y + 22);
    display.print(value);
}

void drawWeatherDashboard() {
    drawLauncherBackground();
    drawTopBar("Weather");
    display.fillRect(0, 34, DISPLAY_WIDTH, 174, display.color565(8, 14, 22));

    const uint16_t accent = weatherAccentColor(weatherData.weatherCode);
    drawWeatherIcon(58, 92, weatherData.weatherCode, accent);

    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(4);
    display.setCursor(118, 58);
    display.printf("%.0f", weatherData.temperature);
    display.setTextSize(2);
    display.setCursor(190, 64);
    display.print("C");

    display.setTextColor(accent);
    display.setTextSize(1);
    display.setCursor(120, 108);
    display.print(weatherCodeText(weatherData.weatherCode));

    display.setTextColor(display.color565(210, 218, 232));
    display.setCursor(120, 126);
    display.print(WEATHER_CITY);

    char feels[18] = {};
    char humid[18] = {};
    char wind[18] = {};
    snprintf(feels, sizeof(feels), "%.0f C", weatherData.apparentTemperature);
    snprintf(humid, sizeof(humid), "%d%%", weatherData.humidity);
    snprintf(wind, sizeof(wind), "%.0f km/h", weatherData.windSpeed);

    drawWeatherMetric(10, 158, "FEELS", feels, ST77XX_WHITE);
    drawWeatherMetric(114, 158, "HUMID", humid, display.color565(0, 220, 255));
    drawWeatherMetric(218, 158, "WIND", wind, display.color565(0, 230, 150));

    drawFooter();
}

void refreshWeather(bool showLoading) {
    if (showLoading) {
        drawWeatherStatus("Updating", WEATHER_CITY, display.color565(0, 220, 255));
    }

    if (fetchWeatherData()) {
        drawWeatherDashboard();
    }
}

void drawWeatherPage() {
    if (!weatherDataValid || millis() - lastWeatherUpdateMs >= WEATHER_REFRESH_INTERVAL_MS) {
        refreshWeather(true);
    } else {
        drawWeatherDashboard();
    }
}

void drawMenu() {
    screenMode = ScreenMode::Menu;
    drawLauncherBackground();
    drawTopBar("Apps");

    display.setTextColor(display.color565(210, 218, 232));
    display.setTextSize(1);
    display.setCursor(18, 190);
    display.print("Select an app");

    drawAppIcon(0, 82, 76, selectedMenuIndex == 0);
    drawAppIcon(1, 238, 76, selectedMenuIndex == 1);
    drawAppIcon(2, 82, 158, selectedMenuIndex == 2);
    drawAppIcon(3, 238, 158, selectedMenuIndex == 3);

    drawFooter();
}

void drawPage() {
    screenMode = ScreenMode::Page;

    if (selectedMenuIndex == CCTV_MENU_INDEX) {
        drawCctvPage();
        return;
    }

    if (selectedMenuIndex == SETTINGS_MENU_INDEX) {
        drawSettingsPage();
        return;
    }

    if (selectedMenuIndex == WEATHER_MENU_INDEX) {
        drawWeatherPage();
        return;
    }

    drawLauncherBackground();
    drawTopBar(MENU_ITEMS[selectedMenuIndex]);

    if (selectedMenuIndex == CLOCK_MENU_INDEX) {
        drawClockPageContent();
        drawFooter();
        return;
    }

    display.fillRoundRect(112, 54, 96, 96, 18, display.color565(18, 23, 34));
    display.drawRoundRect(112, 54, 96, 96, 18, display.color565(82, 92, 112));
    drawAppIcon(selectedMenuIndex, 160, 102, false);

    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(2);
    display.setCursor(104, 166);
    display.print(MENU_ITEMS[selectedMenuIndex]);

    display.setTextSize(1);
    display.setTextColor(display.color565(210, 218, 232));
    display.setCursor(96, 188);
    display.print("Back returns to apps");

    drawFooter();
}

void drawBootAnimation() {
    display.setRotation(3);
    display.fillScreen(ST77XX_BLACK);
    display.setTextWrap(false);

    const uint16_t bg = display.color565(4, 8, 18);
    const uint16_t grid = display.color565(10, 34, 56);
    const uint16_t cyan = display.color565(0, 220, 255);
    const uint16_t blue = display.color565(41, 121, 255);
    const uint16_t green = display.color565(0, 230, 150);
    const uint16_t dim = display.color565(90, 110, 132);
    const char *steps[] = {"DISPLAY BUS", "INPUT MATRIX", "AUDIO CORE", "BATTERY ADC", "LAUNCHER UI"};

    display.fillScreen(bg);

    for (int x = 0; x < DISPLAY_WIDTH; x += 20) {
        display.drawFastVLine(x, 32, 150, grid);
    }
    for (int y = 32; y < 184; y += 20) {
        display.drawFastHLine(0, y, DISPLAY_WIDTH, grid);
    }

    display.setTextColor(cyan);
    display.setTextSize(2);
    display.setCursor(86, 18);
    display.print("NOVA OS");

    display.setTextColor(dim);
    display.setTextSize(1);
    display.setCursor(104, 40);
    display.print("ESP32-S3 BOOT");

    for (int progress = 0; progress <= 100; progress += 4) {
        const int sweepX = map(progress, 0, 100, 0, DISPLAY_WIDTH - 1);
        const int radius = 24 + (progress % 24);
        const int activeStep = min(progress / 21, 4);

        display.fillRect(0, 58, DISPLAY_WIDTH, 126, bg);

        for (int x = 0; x < DISPLAY_WIDTH; x += 20) {
            display.drawFastVLine(x, 58, 126, grid);
        }
        for (int y = 64; y < 184; y += 20) {
            display.drawFastHLine(0, y, DISPLAY_WIDTH, grid);
        }

        display.drawFastVLine(sweepX, 58, 126, cyan);
        display.drawFastVLine(max(0, sweepX - 3), 58, 126, display.color565(0, 90, 120));
        display.drawFastVLine(min(DISPLAY_WIDTH - 1, sweepX + 3), 58, 126, display.color565(0, 90, 120));

        display.drawCircle(160, 116, radius, blue);
        display.drawCircle(160, 116, 44, display.color565(18, 86, 122));
        display.drawCircle(160, 116, 68, display.color565(14, 56, 92));
        display.drawLine(160, 116, 160 + cos(progress * PI / 25.0f) * 58,
                         116 + sin(progress * PI / 25.0f) * 58, cyan);
        display.fillCircle(160, 116, 8, green);
        display.drawCircle(160, 116, 15, cyan);

        display.fillTriangle(36, 90, 50, 82, 50, 98, progress < 34 ? dim : cyan);
        display.fillTriangle(284, 90, 270, 82, 270, 98, progress < 67 ? dim : cyan);
        display.drawRoundRect(82, 76, 156, 78, 10, display.color565(20, 78, 112));

        display.fillRect(14, 188, 292, 16, bg);
        for (int i = 0; i < 20; ++i) {
            const int segmentX = 14 + i * 15;
            const bool active = i < map(progress, 0, 100, 0, 20);
            display.fillRoundRect(segmentX, 190, 10, 10, 2, active ? green : display.color565(24, 34, 50));
        }

        display.fillRect(72, 210, 176, 14, bg);
        display.setTextColor(ST77XX_WHITE);
        display.setTextSize(1);
        display.setCursor(88, 212);
        display.print(steps[activeStep]);
        display.setCursor(214, 212);
        display.printf("%3d%%", progress);

        delay(28);
    }

    display.fillScreen(bg);
    display.drawRoundRect(58, 70, 204, 84, 12, cyan);
    display.setTextColor(green);
    display.setTextSize(2);
    display.setCursor(88, 96);
    display.print("SYSTEM READY");
    display.setTextColor(dim);
    display.setTextSize(1);
    display.setCursor(108, 126);
    display.print("Launching apps");
    delay(450);
}

void setupButtons() {
    for (int i = 0; i < BUTTON_COUNT; ++i) {
        pinMode(BUTTON_PINS[i], INPUT_PULLUP);
        buttons[i].pressed = false;
        buttons[i].lastReading = false;
        buttons[i].lastChangeMs = millis();
        buttons[i].pressStartMs = 0;
    }
}

void setupBuzzer() {
    pinMode(BUZZER_PIN, OUTPUT);
}

void setupBatteryAdc() {
    if (!BATTERY_ADC_ENABLED) {
        batteryAdcAvailable = false;
        Serial.println("Battery ADC disabled in firmware");
        return;
    }

    if (digitalPinToAnalogChannel(BATTERY_ADC_PIN) < 0) {
        batteryAdcAvailable = false;
        Serial.printf("Battery ADC disabled: GPIO %d is not an ADC pin for this board\n", BATTERY_ADC_PIN);
        return;
    }

    batteryAdcAvailable = true;
    analogReadResolution(12);
    analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
    pinMode(BATTERY_ADC_PIN, INPUT);
}

uint32_t readBatteryMillivolts() {
    if (!batteryAdcAvailable) {
        return BATTERY_FULL_MV;
    }

    uint32_t adcMillivolts = 0;

    for (int i = 0; i < BATTERY_SAMPLE_COUNT; ++i) {
        adcMillivolts += analogReadMilliVolts(BATTERY_ADC_PIN);
    }

    const float adcAverage = static_cast<float>(adcMillivolts) / BATTERY_SAMPLE_COUNT;
    return static_cast<uint32_t>((adcAverage * BATTERY_DIVIDER_RATIO) + 0.5f);
}

int batteryPercentFromMillivolts(uint32_t batteryMillivolts) {
    const int percent = ((static_cast<int>(batteryMillivolts) - BATTERY_EMPTY_MV) * 100) /
                        (BATTERY_FULL_MV - BATTERY_EMPTY_MV);
    return constrain(percent, 0, 100);
}

void updateBatteryStatus() {
    batteryMillivolts = readBatteryMillivolts();
    batteryPercent = batteryPercentFromMillivolts(batteryMillivolts);
    drawBatteryIcon();
    Serial.printf("Battery: %lu mV, %d%%\n", batteryMillivolts, batteryPercent);
}

void saveButtonBuzzerSetting() {
    if (!devicePreferences.begin(DEVICE_PREF_NAMESPACE, false)) {
        Serial.println("Device preferences save failed: could not open NVS namespace");
        return;
    }

    devicePreferences.putBool(DEVICE_PREF_BUTTON_BUZZER_KEY, buttonBuzzerEnabled);
    devicePreferences.end();
    Serial.printf("Button buzzer sound: %s\n", buttonBuzzerEnabled ? "enabled" : "disabled");
}

void loadDeviceSettings() {
    if (!devicePreferences.begin(DEVICE_PREF_NAMESPACE, false)) {
        Serial.println("Device preferences load failed: could not open NVS namespace");
        return;
    }

    buttonBuzzerEnabled = devicePreferences.getBool(DEVICE_PREF_BUTTON_BUZZER_KEY, true);
    devicePreferences.end();
    Serial.printf("Button buzzer sound loaded: %s\n", buttonBuzzerEnabled ? "enabled" : "disabled");
}

void handleSettingsButtonPress(int index) {
    if (settingsView == SettingsView::Main) {
        if (index == 0) {
            if (selectedSettingsIndex == SETTINGS_WIFI_INDEX) {
                scanWifiNetworks();
            } else if (selectedSettingsIndex == SETTINGS_BUTTON_BUZZER_INDEX) {
                buttonBuzzerEnabled = !buttonBuzzerEnabled;
                saveButtonBuzzerSetting();
                drawSettingsMain();
            }
        } else if (index == 1) {
            drawMenu();
        } else if (index == 2) {
            selectedSettingsIndex = selectedSettingsIndex == 0 ? SETTINGS_ITEM_COUNT - 1 : selectedSettingsIndex - 1;
            drawSettingsMain();
        } else if (index == 3) {
            selectedSettingsIndex = (selectedSettingsIndex + 1) % SETTINGS_ITEM_COUNT;
            drawSettingsMain();
        }
        return;
    }

    if (settingsView == SettingsView::WiFiList) {
        if (index == 0) {
            beginWifiPasswordEntry();
        } else if (index == 1) {
            drawSettingsMain();
        } else if (index == 2 && scannedWifiCount > 0) {
            selectedWifiIndex = selectedWifiIndex == 0 ? scannedWifiCount - 1 : selectedWifiIndex - 1;
            drawSettingsWifiList();
        } else if (index == 3 && scannedWifiCount > 0) {
            selectedWifiIndex = (selectedWifiIndex + 1) % scannedWifiCount;
            drawSettingsWifiList();
        }
        return;
    }

    if (settingsView == SettingsView::PasswordEntry) {
        const uint32_t now = millis();

        if (index == 0) {
            return;
        }

        if (index == 1) {
            if (lastPasswordBackMs != 0 && now - lastPasswordBackMs <= BUTTON_DOUBLE_CLICK_MS) {
                lastPasswordBackMs = 0;
                drawSettingsMain();
                return;
            }

            if (numericWifiPassword.length() > 0) {
                const char lastDigit = numericWifiPassword[numericWifiPassword.length() - 1];
                currentPasswordDigit = constrain(lastDigit - '0', 0, 9);
                numericWifiPassword.remove(numericWifiPassword.length() - 1);
            }
            lastPasswordBackMs = now;
            drawWifiPasswordEntry();
            return;
        }

        if (index == 2) {
            currentPasswordDigit = currentPasswordDigit == 0 ? 9 : currentPasswordDigit - 1;
            drawWifiPasswordEntry();
            return;
        }

        if (index == 3) {
            currentPasswordDigit = (currentPasswordDigit + 1) % 10;
            drawWifiPasswordEntry();
        }
        return;
    }

    if (settingsView == SettingsView::ConnectResult) {
        if (index == 0) {
            scanWifiNetworks();
        } else if (index == 1) {
            drawSettingsMain();
        }
    }
}

void handleButtonPress(int index) {
    beepBuzzer();

    if (index == 0 && screenMode == ScreenMode::Menu) {
        drawPage();
        return;
    }

    if (screenMode == ScreenMode::Page && selectedMenuIndex == CCTV_MENU_INDEX) {
        if (index == 0) {
            refreshCctvFrame(true);
        } else if (index == 1) {
            drawMenu();
        } else if (index == 2) {
            currentCctvChannel = currentCctvChannel == 1 ? CCTV_CHANNEL_COUNT : currentCctvChannel - 1;
            refreshCctvFrame(true);
        } else if (index == 3) {
            currentCctvChannel = currentCctvChannel == CCTV_CHANNEL_COUNT ? 1 : currentCctvChannel + 1;
            refreshCctvFrame(true);
        }
        return;
    }

    if (screenMode == ScreenMode::Page && selectedMenuIndex == SETTINGS_MENU_INDEX) {
        handleSettingsButtonPress(index);
        return;
    }

    if (screenMode == ScreenMode::Page && selectedMenuIndex == WEATHER_MENU_INDEX) {
        if (index == 0) {
            refreshWeather(true);
        } else if (index == 1) {
            drawMenu();
        }
        return;
    }

    if (index == 1 && screenMode == ScreenMode::Page) {
        drawMenu();
        return;
    }

    if (index == 2) {
        selectedMenuIndex = (selectedMenuIndex + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT;
        if (screenMode == ScreenMode::Menu) {
            drawMenu();
        } else {
            drawPage();
        }
        return;
    }

    if (index == 3) {
        selectedMenuIndex = (selectedMenuIndex + 1) % MENU_ITEM_COUNT;
        if (screenMode == ScreenMode::Menu) {
            drawMenu();
        } else {
            drawPage();
        }
    }
}

void handleButtonRelease(int index, uint32_t pressDurationMs) {
    if (screenMode != ScreenMode::Page || selectedMenuIndex != SETTINGS_MENU_INDEX ||
        settingsView != SettingsView::PasswordEntry || index != 0) {
        return;
    }

    if (pressDurationMs >= BUTTON_LONG_PRESS_MS) {
        connectToPendingWifiAndSave();
        return;
    }

    if (numericWifiPassword.length() < MAX_NUMERIC_PASSWORD_DIGITS) {
        numericWifiPassword += static_cast<char>('0' + currentPasswordDigit);
    }
    drawWifiPasswordEntry();
}

void updateButtons() {
    const uint32_t now = millis();

    for (int i = 0; i < BUTTON_COUNT; ++i) {
        const bool reading = digitalRead(BUTTON_PINS[i]) == LOW;

        if (reading != buttons[i].lastReading) {
            buttons[i].lastReading = reading;
            buttons[i].lastChangeMs = now;
        }

        if ((now - buttons[i].lastChangeMs) >= BUTTON_DEBOUNCE_MS &&
            reading != buttons[i].pressed) {
            buttons[i].pressed = reading;
        Serial.printf("Button %d %s\n", i + 1, buttons[i].pressed ? "pressed" : "released");
            if (buttons[i].pressed) {
                buttons[i].pressStartMs = now;
                handleButtonPress(i);
            } else {
                handleButtonRelease(i, now - buttons[i].pressStartMs);
            }
            drawFooter();
        }
    }
}

void loadSavedWifiCredentials() {
    if (!wifiPreferences.begin(WIFI_PREF_NAMESPACE, false)) {
        Serial.println("WiFi preferences load failed: could not open NVS namespace");
        return;
    }

    savedWifiSsid = wifiPreferences.getString(WIFI_PREF_SSID_KEY, "");
    savedWifiPassword = wifiPreferences.getString(WIFI_PREF_PASSWORD_KEY, "");
    wifiPreferences.end();

    if (savedWifiSsid.length() > 0) {
        Serial.printf("Loaded saved WiFi SSID: %s\n", savedWifiSsid.c_str());
    } else if (initialWifiCredentialsConfigured()) {
        savedWifiSsid = INITIAL_WIFI_SSID;
        savedWifiPassword = INITIAL_WIFI_PASSWORD;
        saveWifiCredentials(savedWifiSsid, savedWifiPassword);
        Serial.printf("Seeded initial WiFi SSID from firmware: %s\n", savedWifiSsid.c_str());
    }
}

void beginSavedWifiConnection() {
    const String ssid = configuredWifiSsid();
    const String password = configuredWifiPassword();

    if (ssid.length() == 0) {
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.printf("Starting WiFi connection: %s\n", ssid.c_str());
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("ESP32-S3 N16R8 PlatformIO project");
    Serial.printf("Flash size: %u MB\n", ESP.getFlashChipSize() / (1024 * 1024));

    if (psramFound()) {
        Serial.printf("PSRAM size: %u MB\n", ESP.getPsramSize() / (1024 * 1024));
    } else {
        Serial.println("PSRAM not detected");
    }

    loadSavedWifiCredentials();
    loadDeviceSettings();

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    setupButtons();
    setupBuzzer();
    setupBatteryAdc();
    setupClockBase();

    displaySpi.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
    display.init(TFT_WIDTH, TFT_HEIGHT);
    drawBootAnimation();
    batteryMillivolts = readBatteryMillivolts();
    batteryPercent = batteryPercentFromMillivolts(batteryMillivolts);
    drawMenu();
    beginSavedWifiConnection();
}

void loop() {
    static uint32_t lastBatteryUpdate = 0;

    updateButtons();

    if (screenMode == ScreenMode::Page && selectedMenuIndex == CCTV_MENU_INDEX &&
        millis() - lastCctvFrameMs >= CCTV_FRAME_INTERVAL_MS) {
        refreshCctvFrame(false);
    }

    if (millis() - lastBatteryUpdate >= 1000) {
        lastBatteryUpdate = millis();
        updateBatteryStatus();

        if (screenMode == ScreenMode::Page && selectedMenuIndex == CLOCK_MENU_INDEX) {
            updateClockHandsAndTime(false);
        } else if (screenMode == ScreenMode::Page && selectedMenuIndex == WEATHER_MENU_INDEX &&
                   weatherDataValid && millis() - lastWeatherUpdateMs >= WEATHER_REFRESH_INTERVAL_MS) {
            refreshWeather(false);
        }
    }
}
