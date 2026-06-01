#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>

#include "secrets.h"

#define API_URL           "https://bt.app2.rushicpatel.com/ttdata"
#define NTP_SERVER        "pool.ntp.org"
#define HKT_OFFSET_SEC    28800   // UTC+8
#define FETCH_INTERVAL_MS 30000

#define SCREEN_W  320
#define SCREEN_H  240
#define ROW_H      16
#define HEADER_H   30
#define MAX_STOPS   8
#define MAX_ETAS    3  // ETAs shown per stop

TFT_eSPI tft = TFT_eSPI();

struct EtaEntry {
    char route[8];
    time_t utc;
};

struct StopData {
    char name[40];
    EtaEntry etas[MAX_ETAS];
    int etaCount;
};

StopData stops[MAX_STOPS];
int stopCount = 0;
volatile bool dataReady  = false;
volatile bool needRedraw = false;
SemaphoreHandle_t dataMutex = nullptr;

// Parse "2026-06-01T06:33:30.578000+08:00" as HKT → return UTC time_t
time_t parseHktToUtc(const char* s) {
    int yr, mo, dy, hr, mn, sc;
    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &yr, &mo, &dy, &hr, &mn, &sc) != 6) return 0;
    struct tm t = {};
    t.tm_year = yr - 1900;
    t.tm_mon  = mo - 1;
    t.tm_mday = dy;
    t.tm_hour = hr;
    t.tm_min  = mn;
    t.tm_sec  = sc;
    // mktime treats tm as local time; configTime(0,0,...) makes local = UTC
    // Input is HKT (UTC+8), so subtract offset to get UTC
    return mktime(&t) - HKT_OFFSET_SEC;
}

bool fetchData() {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, API_URL);
    http.setTimeout(10000);

    int code = http.GET();
    if (code != 200) {
        http.end();
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    if (err) return false;

    const char* hidden[] = { "OPPOSITE GG", "WHT", "KT MTR" };

    // Parse into a local buffer — no lock held during the slow HTTP/JSON work
    StopData newStops[MAX_STOPS];
    int newCount = 0;
    for (JsonArray group : doc.as<JsonArray>()) {
        for (JsonObject stop : group) {
            if (newCount >= MAX_STOPS) break;
            const char* name = stop["stop_name"] | "?";
            bool skip = false;
            for (const char* h : hidden)
                if (strstr(name, h)) { skip = true; break; }
            if (skip) continue;
            StopData& sd = newStops[newCount++];
            strlcpy(sd.name, name, sizeof(sd.name));
            EtaEntry allEtas[12];
            int allCount = 0;
            for (JsonObject eta : stop["etas"].as<JsonArray>()) {
                if (allCount >= 12) break;
                EtaEntry& e = allEtas[allCount++];
                strlcpy(e.route, eta["_Eta__route_name"] | "?", sizeof(e.route));
                const char* etaStr = eta["projected_eta"].isNull()
                    ? eta["eta"].as<const char*>()
                    : eta["projected_eta"].as<const char*>();
                e.utc = parseHktToUtc(etaStr);
            }
            for (int i = 1; i < allCount; i++) {
                EtaEntry key = allEtas[i];
                int j = i - 1;
                while (j >= 0 && allEtas[j].utc > key.utc) { allEtas[j+1] = allEtas[j]; j--; }
                allEtas[j+1] = key;
            }
            sd.etaCount = min(allCount, MAX_ETAS);
            for (int i = 0; i < sd.etaCount; i++) sd.etas[i] = allEtas[i];
        }
    }

    // Swap into the display buffer atomically
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    memcpy(stops, newStops, sizeof(StopData) * newCount);
    stopCount = newCount;
    xSemaphoreGive(dataMutex);
    return true;
}

void fetchTask(void*) {
    while (true) {
        if (fetchData()) {
            dataReady  = true;
            needRedraw = true;
        }
        vTaskDelay(pdMS_TO_TICKS(FETCH_INTERVAL_MS));
    }
}

void drawBusIcon(int x, int y) {
    uint16_t busRed   = tft.color565(220, 30, 30);
    uint16_t winBlue  = tft.color565(100, 190, 230);
    uint16_t darkGrey = tft.color565(60, 60, 60);
    uint16_t midGrey  = tft.color565(160, 160, 160);

    // Body
    tft.fillRoundRect(x, y+2, 42, 17, 2, busRed);
    // Windshield
    tft.fillRect(x+2, y+4, 8, 10, winBlue);
    // Windows
    tft.fillRect(x+12, y+4, 6, 6, winBlue);
    tft.fillRect(x+20, y+4, 6, 6, winBlue);
    tft.fillRect(x+28, y+4, 6, 6, winBlue);
    // Rear door gap
    tft.fillRect(x+36, y+9, 4, 10, TFT_BLACK);
    // Wheel arches
    tft.fillRect(x+2,  y+16, 10, 3, TFT_BLACK);
    tft.fillRect(x+30, y+16, 10, 3, TFT_BLACK);
    // Wheels
    tft.fillCircle(x+7,  y+22, 5, darkGrey);
    tft.fillCircle(x+35, y+22, 5, darkGrey);
    // Hubcaps
    tft.fillCircle(x+7,  y+22, 2, midGrey);
    tft.fillCircle(x+35, y+22, 2, midGrey);
    // Undercarriage strip
    tft.fillRect(x+17, y+18, 13, 2, darkGrey);
}

void drawHeader() {
    uint16_t bg = tft.color565(12, 12, 24);
    time_t now = time(nullptr);
    time_t hkt = now + HKT_OFFSET_SEC;
    struct tm ti;
    gmtime_r(&hkt, &ti);
    char buf[10];
    strftime(buf, sizeof(buf), "%H:%M:%S", &ti);

    tft.fillRect(0, 0, SCREEN_W, HEADER_H, bg);
    drawBusIcon(2, 2);
    tft.setTextColor(TFT_CYAN, bg);
    tft.drawString("HK Bus", 50, 7, 2);
    tft.drawRightString(buf, SCREEN_W - 4, 7, 2);
    tft.drawFastHLine(0, HEADER_H, SCREEN_W, tft.color565(55, 55, 105));
}

void drawStops() {
    uint16_t card     = tft.color565(28, 28, 52);
    uint16_t edge     = tft.color565(55, 55, 105);
    uint16_t namebg   = tft.color565(42, 42, 80);
    uint16_t bartrack = tft.color565(35, 35, 35);
    uint16_t timecol  = tft.color565(130, 130, 185);

    const int cx = 6, cw = SCREEN_W - 12;
    const int etaRowH = 22;
    const int nameH   = 18;
    const int cardH   = nameH + MAX_ETAS * etaRowH + 4;
    const int bx      = cx + 58;
    const int bw      = 140;
    const int maxMins = 30;

    time_t now = time(nullptr);
    int y = HEADER_H + 4;

    for (int i = 0; i < stopCount; i++) {
        if (y + cardH > SCREEN_H) break;
        StopData& sd = stops[i];

        tft.fillRoundRect(cx, y, cw, cardH, 4, card);
        tft.drawRoundRect(cx, y, cw, cardH, 4, edge);
        tft.fillRect(cx+1, y+1, cw-2, nameH-1, namebg);
        tft.setTextColor(TFT_YELLOW, namebg);
        tft.drawString(sd.name, cx+8, y+2, 2);

        int ey = y + nameH + 2;
        for (int j = 0; j < sd.etaCount; j++) {
            EtaEntry& e = sd.etas[j];
            int mins = max(0, (int)(e.utc - now) / 60);

            // Route name
            tft.setTextColor(TFT_WHITE, card);
            tft.drawString(e.route, cx+10, ey+3, 2);

            // Countdown bar
            tft.fillRoundRect(bx, ey+7, bw, 7, 2, bartrack);
            int fill = min(bw, (mins * bw) / maxMins);
            uint16_t bc = mins <= 3  ? TFT_GREEN
                        : mins <= 10 ? tft.color565(255, 150, 0)
                        : tft.color565(50, 100, 200);
            if (fill > 0) tft.fillRoundRect(bx, ey+7, fill, 7, 2, bc);

            // Right side: "14:31  8m" on one line, both font 2
            if (mins == 0) {
                tft.setTextColor(TFT_GREEN, card);
                tft.drawRightString("Now", cx+cw-8, ey+3, 2);
            } else {
                char minBuf[6];
                snprintf(minBuf, sizeof(minBuf), "%dm", mins);
                uint16_t urgcol = mins <= 3 ? TFT_GREEN : TFT_WHITE;
                tft.setTextColor(urgcol, card);
                tft.drawRightString(minBuf, cx+cw-8, ey+3, 2);

                // Actual time sits left of the countdown with a small gap
                int minW = tft.textWidth(minBuf, 2);
                time_t eta_hkt = e.utc + HKT_OFFSET_SEC;
                struct tm et;
                gmtime_r(&eta_hkt, &et);
                char timeBuf[6];
                strftime(timeBuf, sizeof(timeBuf), "%H:%M", &et);
                tft.setTextColor(timecol, card);
                tft.drawRightString(timeBuf, cx+cw-8 - minW - 5, ey+3, 2);
            }

            ey += etaRowH;
        }
        y += cardH + 6;
    }
}

int logY = 0;

void logLine(const char* msg, uint16_t color = TFT_WHITE) {
    Serial.println(msg);
    tft.setTextColor(color, TFT_BLACK);
    tft.drawString(msg, 4, logY, 2);
    logY += ROW_H;
}

void setup() {
    Serial.begin(115200);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    logLine("Connecting to WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 40) delay(500);

    if (WiFi.status() != WL_CONNECTED) {
        logLine("WiFi failed!", TFT_RED);
        while (true) delay(1000);
    }
    char connMsg[48];
    snprintf(connMsg, sizeof(connMsg), "WiFi: %s", WiFi.SSID().c_str());
    logLine(connMsg, TFT_GREEN);

    logLine("Syncing time (NTP)...");
    configTime(0, 0, NTP_SERVER);
    time_t now = 0;
    tries = 0;
    while (now < 1000000000UL && tries++ < 40) {
        delay(500);
        now = time(nullptr);
        if (tries % 4 == 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), "  waiting... (%ds)", tries / 2);
            tft.fillRect(4, logY, SCREEN_W - 4, ROW_H, TFT_BLACK);
            tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
            tft.drawString(buf, 4, logY, 2);
        }
    }

    if (now < 1000000000UL) {
        logLine("NTP sync failed!", TFT_RED);
        while (true) delay(1000);
    }

    tft.fillRect(4, logY, SCREEN_W - 4, ROW_H, TFT_BLACK);
    logLine("Time synced!", TFT_GREEN);
    logLine("Fetching bus data...");

    dataMutex = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(fetchTask, "fetch", 8192, NULL, 1, NULL, 0);
}

void loop() {
    if (needRedraw) {
        needRedraw = false;
        tft.fillScreen(tft.color565(12, 12, 24));
        xSemaphoreTake(dataMutex, portMAX_DELAY);
        drawStops();
        xSemaphoreGive(dataMutex);
    }
    if (dataReady) {
        drawHeader();
    }
    delay(1000);
}
