// BusSampler — cycles through 4 UI style previews every 5 seconds
// Uses hardcoded mock data — no WiFi needed

#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

struct MockEta  { const char* route; int mins; };
struct MockStop { const char* name;  MockEta etas[3]; };

static const MockStop STOPS[] = {
    {"CEMETERY",      {{"970", 8},  {"970X", 12}, {"970",  25}}},
    {"GG -> KELLETT", {{"970", 3},  {"A10",  15}, {"970X", 22}}},
};
constexpr int STOP_COUNT = 2;
constexpr int NUM_MODES  = 4;

#define SW  320
#define SH  240
#define RH   16  // standard row height
#define HH   30  // header height

int curMode = 0;
unsigned long lastSwitch = 0;

// ── Helpers ─────────────────────────────────────────────────────────────────

uint16_t routeCol(const char* r) {
    if (!strcmp(r, "970"))  return tft.color565( 50, 100, 220);
    if (!strcmp(r, "970X")) return tft.color565(220, 110,   0);
    if (!strcmp(r, "973"))  return tft.color565( 30, 170,  70);
    if (!strcmp(r, "A10"))  return tft.color565(160,  50, 220);
    return tft.color565(80, 80, 80);
}

void etaStr(char* buf, int sz, int mins) {
    if (mins == 0) strlcpy(buf, "Now", sz);
    else           snprintf(buf, sz, "%d min", mins);
}

void drawBusIcon(int x, int y) {
    uint16_t red = tft.color565(220,  30,  30);
    uint16_t blu = tft.color565(100, 190, 230);
    uint16_t dk  = tft.color565( 60,  60,  60);
    uint16_t md  = tft.color565(160, 160, 160);
    tft.fillRoundRect(x,    y+2,  42, 17, 2, red);
    tft.fillRect(x+2,  y+4,  8, 10, blu);
    tft.fillRect(x+12, y+4,  6,  6, blu);
    tft.fillRect(x+20, y+4,  6,  6, blu);
    tft.fillRect(x+28, y+4,  6,  6, blu);
    tft.fillRect(x+36, y+9,  4, 10, TFT_BLACK);
    tft.fillRect(x+2,  y+16, 10,  3, TFT_BLACK);
    tft.fillRect(x+30, y+16, 10,  3, TFT_BLACK);
    tft.fillCircle(x+7,  y+22, 5, dk);
    tft.fillCircle(x+35, y+22, 5, dk);
    tft.fillCircle(x+7,  y+22, 2, md);
    tft.fillCircle(x+35, y+22, 2, md);
    tft.fillRect(x+17, y+18, 13, 2, dk);
}

void drawStdHeader(uint16_t bg = TFT_BLACK) {
    tft.fillRect(0, 0, SW, HH, bg);
    drawBusIcon(2, 2);
    tft.setTextColor(TFT_CYAN, bg);
    tft.drawString("HK Bus", 50, 7, 2);
    tft.drawRightString("14:23:45", SW - 4, 7, 2);
    tft.drawFastHLine(0, HH, SW, TFT_DARKGREY);
}

void modeLabel(int m, uint16_t bg = TFT_BLACK) {
    const char* lbl[] = {"A: Cards", "B: Badges", "C: Big clock", "D: Bars"};
    tft.fillRect(0, SH - 12, SW, 12, bg);
    tft.setTextColor(tft.color565(55, 55, 55), bg);
    tft.drawRightString(lbl[m], SW - 3, SH - 11, 1);
}

// ── Mode A: Dark rounded-card layout ────────────────────────────────────────
void drawModeA() {
    uint16_t bg     = tft.color565(12, 12, 24);
    uint16_t card   = tft.color565(28, 28, 52);
    uint16_t edge   = tft.color565(55, 55, 105);
    uint16_t namebg = tft.color565(42, 42, 80);

    tft.fillScreen(bg);
    drawStdHeader(bg);

    int y = HH + 8;
    for (int i = 0; i < STOP_COUNT; i++) {
        const int cx = 6, cw = SW - 12, ch = 20 + 3*RH + 4;
        tft.fillRoundRect(cx, y, cw, ch, 4, card);
        tft.drawRoundRect(cx, y, cw, ch, 4, edge);
        tft.fillRect(cx+1, y+1, cw-2, 19, namebg);  // flat name band inside card
        tft.setTextColor(TFT_YELLOW, namebg);
        tft.drawString(STOPS[i].name, cx+8, y+3, 2);
        int ey = y + 22;
        for (int j = 0; j < 3; j++) {
            int mins = STOPS[i].etas[j].mins;
            tft.setTextColor(TFT_WHITE, card);
            tft.drawString(STOPS[i].etas[j].route, cx+10, ey, 2);
            char buf[12]; etaStr(buf, sizeof(buf), mins);
            tft.setTextColor(mins <= 3 ? TFT_GREEN : TFT_WHITE, card);
            tft.drawRightString(buf, cx+cw-8, ey, 2);
            ey += RH;
        }
        y += ch + 10;
    }
    modeLabel(0, bg);
}

// ── Mode B: Route colour badges ─────────────────────────────────────────────
void drawModeB() {
    uint16_t namebg = tft.color565(18, 22, 52);
    tft.fillScreen(TFT_BLACK);
    drawStdHeader();

    int y = HH + 3;
    for (int i = 0; i < STOP_COUNT; i++) {
        tft.fillRect(0, y, SW, RH+2, namebg);
        tft.setTextColor(TFT_YELLOW, namebg);
        tft.drawString(STOPS[i].name, 6, y+2, 2);
        y += RH + 3;
        for (int j = 0; j < 3; j++) {
            int mins = STOPS[i].etas[j].mins;
            tft.fillRect(0, y, SW, RH, TFT_BLACK);
            uint16_t col = routeCol(STOPS[i].etas[j].route);
            tft.fillRoundRect(12, y+1, 38, RH-2, 3, col);
            tft.setTextColor(TFT_WHITE, col);
            tft.drawCentreString(STOPS[i].etas[j].route, 31, y+3, 1);
            char buf[12]; etaStr(buf, sizeof(buf), mins);
            tft.setTextColor(mins <= 3 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
            tft.drawRightString(buf, SW-6, y+2, 2);
            y += RH + 1;
        }
        y += 5;
    }
    modeLabel(1);
}

// ── Mode C: Big clock header ─────────────────────────────────────────────────
void drawModeC() {
    uint16_t hbg    = tft.color565(8, 8, 20);
    uint16_t accent = tft.color565(0, 140, 220);
    uint16_t nbg    = tft.color565(0, 25, 52);
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, SW, 46, hbg);
    tft.setTextColor(tft.color565(80, 80, 120), hbg);
    tft.drawString("HK Bus ETA", 6, 3, 1);
    tft.setTextColor(TFT_WHITE, hbg);
    tft.drawCentreString("14:23:45", SW/2, 10, 4);  // font 4 = large
    tft.fillRect(0, 44, SW, 2, accent);

    int y = 50;
    for (int i = 0; i < STOP_COUNT; i++) {
        tft.fillRect(0, y, SW, RH, nbg);
        tft.setTextColor(TFT_CYAN, nbg);
        tft.drawString(STOPS[i].name, 6, y+2, 2);
        y += RH + 1;
        for (int j = 0; j < 3; j++) {
            int mins = STOPS[i].etas[j].mins;
            tft.fillRect(0, y, SW, RH, TFT_BLACK);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString(STOPS[i].etas[j].route, 14, y+2, 2);
            char buf[12]; etaStr(buf, sizeof(buf), mins);
            tft.setTextColor(mins <= 3 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
            tft.drawRightString(buf, SW-6, y+2, 2);
            y += RH;
        }
        y += 4;
    }
    modeLabel(2);
}

// ── Mode D: Countdown bars ───────────────────────────────────────────────────
void drawModeD() {
    tft.fillScreen(TFT_BLACK);
    drawStdHeader();

    const int bx = 58, bw = 155, maxMins = 30, rowH = RH + 5;
    int y = HH + 3;
    for (int i = 0; i < STOP_COUNT; i++) {
        tft.fillRect(0, y, SW, RH, TFT_NAVY);
        tft.setTextColor(TFT_YELLOW, TFT_NAVY);
        tft.drawString(STOPS[i].name, 4, y+1, 2);
        y += RH + 2;
        for (int j = 0; j < 3; j++) {
            int mins = STOPS[i].etas[j].mins;
            tft.fillRect(0, y, SW, rowH, TFT_BLACK);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString(STOPS[i].etas[j].route, 4, y+3, 2);
            // bar track
            tft.fillRoundRect(bx, y+4, bw, 8, 2, tft.color565(35, 35, 35));
            // bar fill (full = far away, low = arriving)
            int fill = min(bw, (mins * bw) / maxMins);
            uint16_t bc = mins <= 3  ? TFT_GREEN
                        : mins <= 10 ? tft.color565(255, 150, 0)
                        : tft.color565(50, 100, 200);
            if (fill > 0) tft.fillRoundRect(bx, y+4, fill, 8, 2, bc);
            char buf[12]; etaStr(buf, sizeof(buf), mins);
            tft.setTextColor(mins <= 3 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
            tft.drawRightString(buf, SW-4, y+3, 2);
            y += rowH;
        }
        y += 3;
    }
    modeLabel(3);
}

// ── Main ─────────────────────────────────────────────────────────────────────

void drawScreen() {
    switch (curMode) {
        case 0: drawModeA(); break;
        case 1: drawModeB(); break;
        case 2: drawModeC(); break;
        case 3: drawModeD(); break;
    }
}

void setup() {
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    drawScreen();
    lastSwitch = millis();
}

void loop() {
    if (millis() - lastSwitch >= 5000) {
        curMode = (curMode + 1) % NUM_MODES;
        drawScreen();
        lastSwitch = millis();
    }
}
