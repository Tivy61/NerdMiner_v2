#include "displayDriver.h"

#ifdef NERDMINER_S3_ILI9341

#include <TFT_eSPI.h>
#include <math.h>
#include "media/myFonts.h"
#include "media/Free_Fonts.h"
#include "version.h"
#include "monitor.h"
#include "OpenFontRender.h"
#include "rotation.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <xpt2046.h>

#define WIDTH 320
#define HEIGHT 240
#define PADX 12

// Cette dalle ILI9341 s'affiche en couleurs inversees par defaut (fond clair) ->
// on force l'inversion pour retrouver un vrai fond noir. Passer a false si un
// jour le panneau montre l'inverse.
#define ILI9341_INVERT true

OpenFontRender render;
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite background = TFT_eSprite(&tft);

SPIClass hSPI(HSPI);
XPT2046 touch(hSPI, ETOUCH_CS, TOUCH_IRQ);

extern monitor_data mMonitor;
extern DisplayDriver *currentDisplayDriver;
extern String getTime(void);   // "HH:MM" (monitor.cpp)
void esp32S3ILI9341_AlternateRotation(void);

// Bascule automatique entre les ecrans toutes les 10 s (voir
// esp32S3ILI9341_DoLedStuff). Un appui bouton BOOT ou un contact tactile
// force l'ecran voulu et relance le minuteur de 10 s a partir de la.
#define SCREEN_AUTO_CYCLE_MS 10000UL

// Zones tactiles : coins = navigation directe, centre = rafraichissement des
// cours. Tactile HS materiellement pour l'instant (XPT2046 non connecte) ->
// code laisse dormant, il refonctionnera des que le cablage sera repare.
// Pas de reset WiFi par tactile (signal parasite capable de rester fige) :
// le bouton BOOT physique (5s) reste le moyen fiable de reset.
#define TOUCH_CORNER_SIZE 70
#define SCREEN_INDEX_MINER  0
#define SCREEN_INDEX_CLOCK  1
#define SCREEN_INDEX_GLOBAL 2
#define SCREEN_INDEX_PRICES 3
#define SCREEN_INDEX_CHART  4

bool touchWasDown = false;

// Palette "orange Bitcoin / sombre epure". color565() est de l'arithmetique
// pure mais on l'initialise dans _Init() pour rester lisible.
static uint16_t COL_BG, COL_ACCENT, COL_LABEL, COL_VALUE, COL_UP, COL_DOWN, COL_DIM, COL_PANEL;

// --------------------------------------------------------------------------
//  Donnees marche : prix spot (BTC / ETH / HYPE)
// --------------------------------------------------------------------------
struct prices_data {
  float btc = 0, eth = 0, hype = 0;
  float btc_chg = 0, eth_chg = 0, hype_chg = 0;
  bool valid = false;
};

prices_data lastPrices;
unsigned long lastPricesFetch = 0;
#define PRICES_UPDATE_MS (90UL * 1000UL)

void fetchPrices() {
  if (WiFi.status() != WL_CONNECTED) return;
  unsigned long now = millis();
  if (lastPrices.valid && (now - lastPricesFetch < PRICES_UPDATE_MS)) return;

  HTTPClient http;
  http.setTimeout(6000);
  http.begin("https://api.coingecko.com/api/v3/simple/price?ids=bitcoin,ethereum,hyperliquid&vs_currencies=usd&include_24hr_change=true");
  http.addHeader("User-Agent", "NerdMiner-ESP32");
  http.addHeader("Accept", "application/json");
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(512);
    if (!deserializeJson(doc, payload)) {
      lastPrices.btc = doc["bitcoin"]["usd"] | lastPrices.btc;
      lastPrices.btc_chg = doc["bitcoin"]["usd_24h_change"] | lastPrices.btc_chg;
      lastPrices.eth = doc["ethereum"]["usd"] | lastPrices.eth;
      lastPrices.eth_chg = doc["ethereum"]["usd_24h_change"] | lastPrices.eth_chg;
      lastPrices.hype = doc["hyperliquid"]["usd"] | lastPrices.hype;
      lastPrices.hype_chg = doc["hyperliquid"]["usd_24h_change"] | lastPrices.hype_chg;
      lastPrices.valid = true;
      lastPricesFetch = now;
    }
  }
  http.end();
}

// --------------------------------------------------------------------------
//  Donnees marche : historique BTC sur 3 jours (pour le graphique)
// --------------------------------------------------------------------------
#define BTC_HIST_MAX 100
static float btcHist[BTC_HIST_MAX];
static int   btcHistCount = 0;
static bool  btcHistValid = false;
static unsigned long btcHistFetch = 0;
#define BTC_HIST_UPDATE_MS (30UL * 60UL * 1000UL)
#define BTC_HIST_RETRY_MS  (60UL * 1000UL)

void fetchBtcHistory() {
  if (WiFi.status() != WL_CONNECTED) return;
  unsigned long now = millis();
  if (btcHistValid && (now - btcHistFetch < BTC_HIST_UPDATE_MS)) return;
  if (!btcHistValid && btcHistFetch != 0 && (now - btcHistFetch < BTC_HIST_RETRY_MS)) return;

  HTTPClient http;
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin("https://api.coingecko.com/api/v3/coins/bitcoin/market_chart?vs_currency=usd&days=3");
  http.addHeader("User-Agent", "NerdMiner-ESP32");
  http.addHeader("Accept", "application/json");
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    StaticJsonDocument<48> filter;
    filter["prices"] = true;
    DynamicJsonDocument doc(12288);
    if (!deserializeJson(doc, payload, DeserializationOption::Filter(filter))) {
      JsonArray arr = doc["prices"].as<JsonArray>();
      int n = arr.size();
      if (n > 1) {
        int stride = (n + BTC_HIST_MAX - 1) / BTC_HIST_MAX;
        if (stride < 1) stride = 1;
        int k = 0;
        for (int i = 0; i < n && k < BTC_HIST_MAX; i += stride)
          btcHist[k++] = (float)arr[i][1].as<double>();
        if (k < BTC_HIST_MAX && stride > 1)
          btcHist[k++] = (float)arr[n - 1][1].as<double>();  // garder le dernier point
        btcHistCount = k;
        btcHistValid = (k >= 2);
      }
    }
  }
  btcHistFetch = now;
  http.end();
}

// --------------------------------------------------------------------------
//  Helpers de rendu
// --------------------------------------------------------------------------
static String fmtUsdShort(float v) {
  char b[16];
  if (v >= 1000.0f) snprintf(b, sizeof(b), "$%.1fk", v / 1000.0f);
  else              snprintf(b, sizeof(b), "$%.0f", v);
  return String(b);
}

// Petite jauge WiFi 4 barres (x = bord gauche, y = haut ; bloc ~18x13).
static void drawWifi(int x, int y) {
  int bars = 0;
  if (WiFi.status() == WL_CONNECTED) {
    long r = WiFi.RSSI();
    if      (r >= -55) bars = 4;
    else if (r >= -65) bars = 3;
    else if (r >= -73) bars = 2;
    else               bars = 1;
  }
  for (int i = 0; i < 4; i++) {
    int bh = 3 + i * 3;                       // 3, 6, 9, 12
    background.fillRect(x + i * 5, y + 12 - bh, 3, bh, (i < bars) ? COL_ACCENT : COL_DIM);
  }
  if (bars == 0) {                            // croix si hors ligne
    background.drawLine(x, y + 1, x + 16, y + 12, COL_DOWN);
    background.drawLine(x, y + 12, x + 16, y + 1, COL_DOWN);
  }
}

static void drawHeader(const char *title, const String &timeStr) {
  background.fillSprite(COL_BG);
  background.setTextDatum(TL_DATUM);
  background.setFreeFont(FSSB9);
  background.setTextColor(COL_ACCENT, COL_BG);
  background.drawString(title, PADX, 6);

  int rightEdge = WIDTH - PADX;
  background.setFreeFont(FSS9);
  if (timeStr.length()) {
    background.setTextColor(COL_LABEL, COL_BG);
    background.setTextDatum(TR_DATUM);
    background.drawString(timeStr, rightEdge, 8);
    rightEdge -= background.textWidth(timeStr) + 8;
    background.setTextDatum(TL_DATUM);
  }
  drawWifi(rightEdge - 18, 6);

  background.drawFastHLine(PADX, 26, WIDTH - 2 * PADX, COL_DIM);
}

// Ligne "label a gauche (petit gris) / valeur a droite (gras)".
static void drawRow(int y, const char *label, const String &value, uint16_t vcolor) {
  background.setTextDatum(TL_DATUM);
  background.setFreeFont(FSS9);
  background.setTextColor(COL_LABEL, COL_BG);
  background.drawString(label, PADX, y + 3);
  background.setFreeFont(FSSB12);
  background.setTextColor(vcolor, COL_BG);
  background.setTextDatum(TR_DATUM);
  background.drawString(value, WIDTH - PADX, y);
  background.setTextDatum(TL_DATUM);
}

// Ligne de l'ecran Marches : ticker / prix / variation avec triangle.
static void priceRow(int y, const char *tk, uint16_t tkcol, float price, float chg, bool cents) {
  background.setTextDatum(TL_DATUM);
  background.setFreeFont(FSSB12);
  background.setTextColor(tkcol, COL_BG);
  background.drawString(tk, PADX, y + 4);
  int valx = PADX + background.textWidth(tk) + 16;   // toujours un espace apres le ticker

  char b[24];
  if (cents) snprintf(b, sizeof(b), "$%.2f", price);
  else       snprintf(b, sizeof(b), "$%.0f", price);
  background.setFreeFont(FSSB18);
  background.setTextColor(COL_VALUE, COL_BG);
  background.drawString(b, valx, y);

  uint16_t c = chg >= 0 ? COL_UP : COL_DOWN;
  snprintf(b, sizeof(b), "%.1f%%", fabsf(chg));
  background.setFreeFont(FSS9);
  background.setTextColor(c, COL_BG);
  background.setTextDatum(TR_DATUM);
  background.drawString(b, WIDTH - PADX, y + 6);
  int tw = background.textWidth(b);
  int tx = WIDTH - PADX - tw - 11;
  if (chg >= 0) background.fillTriangle(tx, y + 17, tx + 9, y + 17, tx + 4, y + 6, c);
  else          background.fillTriangle(tx, y + 7,  tx + 9, y + 7,  tx + 4, y + 18, c);
  background.setTextDatum(TL_DATUM);
}

// --------------------------------------------------------------------------
//  Init
// --------------------------------------------------------------------------
void esp32S3ILI9341_Init(void)
{
  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  tft.invertDisplay(ILI9341_INVERT);
  background.createSprite(WIDTH, HEIGHT);
  background.setSwapBytes(true);
  render.setDrawer(background);
  render.setLineSpaceRatio(0.9);

  if (render.loadFont(DigitalNumbers, sizeof(DigitalNumbers)))
    Serial.println("Initialise error");

  COL_BG     = TFT_BLACK;
  COL_ACCENT = tft.color565(247, 147, 26);   // #F7931A orange Bitcoin
  COL_LABEL  = tft.color565(150, 158, 170);
  COL_VALUE  = TFT_WHITE;
  COL_UP     = tft.color565(45, 210, 120);
  COL_DOWN   = tft.color565(235, 80, 80);
  COL_DIM    = tft.color565(70, 74, 84);
  COL_PANEL  = tft.color565(32, 33, 38);

  hSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI);
  // begin() attend la resolution native (portrait) du panneau, PAS la resolution
  // apres rotation : setRotation() se charge d'echanger largeur/hauteur.
  touch.begin(HEIGHT, WIDTH);
  touch.setRotation(tft.getRotation());

  // La lib xpt2046 ne configure jamais la broche IRQ : on force un pull-up pour
  // qu'elle soit franche au repos (evite les faux contacts quand le XPT2046
  // n'est pas connecte).
  pinMode(TOUCH_IRQ, INPUT_PULLUP);
}

void esp32S3ILI9341_AlternateScreenState(void)
{
  // Retroeclairage cable en direct (pas de PWM) : rien a faire ici
}

void esp32S3ILI9341_AlternateRotation(void)
{
  tft.setRotation(flipRotation(tft.getRotation()));
  touch.setRotation(tft.getRotation());
}

// --------------------------------------------------------------------------
//  Ecran MINAGE
// --------------------------------------------------------------------------
void esp32S3ILI9341_MinerScreen(unsigned long mElapsed)
{
  mining_data d = getMiningData(mElapsed);

  Serial.printf(">>> Completed %s share(s), %s Khashes, avg. hashrate %s KH/s\n",
                d.completedShares.c_str(), d.totalKHashes.c_str(), d.currentHashRate.c_str());

  drawHeader("MINAGE", d.currentTime);

  background.setTextDatum(TL_DATUM);
  background.setFreeFont(FSS9);
  background.setTextColor(COL_LABEL, COL_BG);
  background.drawString("HASHRATE", PADX, 36);

  background.setFreeFont(FSSB24);
  background.setTextColor(COL_VALUE, COL_BG);
  background.drawString(d.currentHashRate, PADX, 52);
  int hw = background.textWidth(d.currentHashRate);
  background.setFreeFont(FSS9);
  background.setTextColor(COL_LABEL, COL_BG);
  background.drawString("kH/s", PADX + hw + 8, 72);

  background.drawFastHLine(PADX, 96, WIDTH - 2 * PADX, COL_DIM);

  drawRow(108, "SHARES VALIDES",  d.completedShares, COL_VALUE);
  drawRow(142, "MEILLEURE DIFF",  d.bestDiff,        COL_ACCENT);
  drawRow(176, "TEMPS DE MINAGE", d.timeMining,      COL_VALUE);

  background.pushSprite(0, 0);
}

// --------------------------------------------------------------------------
//  Ecran HORLOGE
// --------------------------------------------------------------------------
void esp32S3ILI9341_ClockScreen(unsigned long mElapsed)
{
  clock_data   d  = getClockData(mElapsed);
  clock_data_t td = getClockData_t(mElapsed);

  drawHeader("HORLOGE", "");

  char t[12];
  snprintf(t, sizeof(t), "%02lu:%02lu:%02lu", td.currentHours, td.currentMinutes, td.currentSeconds);
  render.setFontSize(46);
  render.cdrawString(t, WIDTH / 2, 44, COL_ACCENT, COL_BG);

  background.setFreeFont(FSS12);
  background.setTextColor(COL_LABEL, COL_BG);
  background.setTextDatum(MC_DATUM);
  background.drawString(d.currentDate, WIDTH / 2, 116);
  background.setTextDatum(TL_DATUM);

  background.drawFastHLine(PADX, 138, WIDTH - 2 * PADX, COL_DIM);
  drawRow(150, "PRIX BTC",         d.btcPrice,    COL_ACCENT);
  drawRow(186, "HAUTEUR DE BLOC",  d.blockHeight, COL_VALUE);

  background.pushSprite(0, 0);
}

// --------------------------------------------------------------------------
//  Ecran RESEAU
// --------------------------------------------------------------------------
void esp32S3ILI9341_GlobalScreen(unsigned long mElapsed)
{
  coin_data d = getCoinData(mElapsed);

  drawHeader("RESEAU", d.currentTime);

  drawRow(48,  "DIFFICULTE",      d.netwrokDifficulty, COL_VALUE);
  drawRow(90,  "HASHRATE RESEAU", d.globalHashRate,    COL_VALUE);
  drawRow(132, "FRAIS ~30 MIN",   d.halfHourFee,       COL_VALUE);

  background.drawFastHLine(PADX, 162, WIDTH - 2 * PADX, COL_DIM);
  background.setFreeFont(FSS9);
  background.setTextColor(COL_LABEL, COL_BG);
  background.setTextDatum(TL_DATUM);
  background.drawString("HALVING", PADX, 172);
  if (d.progressPercent > 0.0f) {
    char p[12];
    snprintf(p, sizeof(p), "%.1f%%", d.progressPercent);
    background.setTextColor(COL_ACCENT, COL_BG);
    background.setTextDatum(TR_DATUM);
    background.drawString(p, WIDTH - PADX, 172);
    background.setTextDatum(TL_DATUM);
    int bx = PADX, bw = WIDTH - 2 * PADX, by = 192, bh = 14;
    background.drawRect(bx, by, bw, bh, COL_DIM);
    background.fillRect(bx + 1, by + 1, (int)((bw - 2) * d.progressPercent / 100.0f), bh - 2, COL_ACCENT);
  }

  background.pushSprite(0, 0);
}

// --------------------------------------------------------------------------
//  Ecran MARCHES
// --------------------------------------------------------------------------
void esp32S3ILI9341_PricesScreen(unsigned long mElapsed)
{
  fetchPrices();

  drawHeader("MARCHES", getTime());

  if (!lastPrices.valid) {
    background.setFreeFont(FSS12);
    background.setTextColor(COL_LABEL, COL_BG);
    background.setTextDatum(MC_DATUM);
    background.drawString("Chargement des cours...", WIDTH / 2, HEIGHT / 2);
    background.setTextDatum(TL_DATUM);
    background.pushSprite(0, 0);
    return;
  }

  priceRow(44,  "BTC",  COL_ACCENT, lastPrices.btc,  lastPrices.btc_chg,  false);
  background.drawFastHLine(PADX, 88, WIDTH - 2 * PADX, COL_DIM);
  priceRow(102, "ETH",  COL_VALUE,  lastPrices.eth,  lastPrices.eth_chg,  true);
  background.drawFastHLine(PADX, 146, WIDTH - 2 * PADX, COL_DIM);
  priceRow(160, "HYPE", COL_VALUE,  lastPrices.hype, lastPrices.hype_chg, true);

  unsigned long age = (millis() - lastPricesFetch) / 1000;
  char f[28];
  snprintf(f, sizeof(f), "MAJ il y a %lus", age);
  background.setFreeFont(FSS9);
  background.setTextColor(COL_DIM, COL_BG);
  background.setTextDatum(BR_DATUM);
  background.drawString(f, WIDTH - PADX, HEIGHT - 4);
  background.setTextDatum(TL_DATUM);

  background.pushSprite(0, 0);
}

// --------------------------------------------------------------------------
//  Ecran BTC - 3 JOURS (graphique)
// --------------------------------------------------------------------------
void esp32S3ILI9341_BtcChartScreen(unsigned long mElapsed)
{
  fetchBtcHistory();

  drawHeader("BTC - 3 JOURS", getTime());

  if (!btcHistValid || btcHistCount < 2) {
    background.setFreeFont(FSS12);
    background.setTextColor(COL_LABEL, COL_BG);
    background.setTextDatum(MC_DATUM);
    background.drawString(WiFi.status() == WL_CONNECTED ? "Chargement historique..." : "Hors ligne",
                          WIDTH / 2, HEIGHT / 2);
    background.setTextDatum(TL_DATUM);
    background.pushSprite(0, 0);
    return;
  }

  float mn = btcHist[0], mx = btcHist[0];
  for (int i = 1; i < btcHistCount; i++) {
    if (btcHist[i] < mn) mn = btcHist[i];
    if (btcHist[i] > mx) mx = btcHist[i];
  }
  float span = mx - mn;
  if (span < 1.0f) span = 1.0f;
  float first = btcHist[0];
  float last  = btcHist[btcHistCount - 1];
  float chg   = (last - first) / first * 100.0f;

  // Dernier prix + variation 3 j sous le bandeau
  background.setFreeFont(FSSB18);
  background.setTextColor(COL_VALUE, COL_BG);
  background.setTextDatum(TL_DATUM);
  background.drawString(fmtUsdShort(last), PADX, 34);

  uint16_t c = chg >= 0 ? COL_UP : COL_DOWN;
  char b[20];
  snprintf(b, sizeof(b), "%+.1f%% / 3j", chg);
  background.setFreeFont(FSS9);
  background.setTextColor(c, COL_BG);
  background.setTextDatum(TR_DATUM);
  background.drawString(b, WIDTH - PADX, 40);
  background.setTextDatum(TL_DATUM);

  // Cadre + grille
  const int gx0 = PADX, gx1 = WIDTH - PADX;
  const int gy0 = 70,   gy1 = 220;
  background.drawRect(gx0, gy0, gx1 - gx0, gy1 - gy0, COL_DIM);
  for (int i = 1; i < 4; i++)
    background.drawFastHLine(gx0 + 1, gy0 + (gy1 - gy0) * i / 4, gx1 - gx0 - 2, COL_PANEL);

  // Courbe (trait 2 px)
  int ppx = 0, ppy = 0;
  for (int i = 0; i < btcHistCount; i++) {
    int px = gx0 + (int)((long)i * (gx1 - gx0) / (btcHistCount - 1));
    int py = gy1 - (int)((btcHist[i] - mn) * (gy1 - gy0) / span);
    if (i > 0) {
      background.drawLine(ppx, ppy, px, py, COL_ACCENT);
      background.drawLine(ppx, ppy + 1, px, py + 1, COL_ACCENT);
    }
    ppx = px;
    ppy = py;
  }

  // Bornes min / max
  background.setFreeFont(FSS9);
  background.setTextColor(COL_LABEL, COL_BG);
  background.setTextDatum(TL_DATUM);
  background.drawString(fmtUsdShort(mx), gx0 + 4, gy0 + 3);
  background.setTextDatum(BL_DATUM);
  background.drawString(fmtUsdShort(mn), gx0 + 4, gy1 - 3);
  background.setTextDatum(TL_DATUM);

  background.pushSprite(0, 0);
}

// --------------------------------------------------------------------------
//  Ecrans explicites
// --------------------------------------------------------------------------
void esp32S3ILI9341_LoadingScreen(void)
{
  background.fillSprite(TFT_BLACK);
  background.setTextDatum(MC_DATUM);
  background.setFreeFont(FSSB18);
  background.setTextColor(tft.color565(247, 147, 26), TFT_BLACK);
  background.drawString("MyPicsou Miner", WIDTH / 2, HEIGHT / 2 - 14);
  background.setFreeFont(FSS9);
  background.setTextColor(tft.color565(150, 158, 170), TFT_BLACK);
  background.drawString(CURRENT_VERSION, WIDTH / 2, HEIGHT / 2 + 16);
  background.setTextDatum(TL_DATUM);
  background.pushSprite(0, 0);
}

void esp32S3ILI9341_SetupScreen(void)
{
  background.fillSprite(TFT_BLACK);
  background.setTextDatum(MC_DATUM);
  background.setFreeFont(FSSB12);
  background.setTextColor(tft.color565(247, 147, 26), TFT_BLACK);
  background.drawString("Configuration WiFi requise", WIDTH / 2, HEIGHT / 2 - 16);
  background.setFreeFont(FSS9);
  background.setTextColor(TFT_WHITE, TFT_BLACK);
  background.drawString("Rejoindre le WiFi : NerdMinerAP", WIDTH / 2, HEIGHT / 2 + 12);
  background.setTextDatum(TL_DATUM);
  background.pushSprite(0, 0);
}

void esp32S3ILI9341_AnimateCurrentScreen(unsigned long frame) {}

// --------------------------------------------------------------------------
//  Tactile (dormant : XPT2046 non connecte materiellement)
// --------------------------------------------------------------------------
void esp32S3ILI9341_HandleTouch(void)
{
  if (!touch.pressed()) {
    touchWasDown = false;
    return;
  }

  if (touchWasDown) return; // deja traite au premier contact

  uint16_t x = touch.X();
  uint16_t y = touch.Y();
  Serial.printf(">>> Touch detecte (brut) : x=%u y=%u (raw x=%u y=%u)\n", x, y, touch.RawX(), touch.RawY());
  touchWasDown = true;

  bool left = x < TOUCH_CORNER_SIZE;
  bool right = x > (WIDTH - TOUCH_CORNER_SIZE);
  bool top = y < TOUCH_CORNER_SIZE;
  bool bottom = y > (HEIGHT - TOUCH_CORNER_SIZE);
  bool center = x > 110 && x < 210 && y > 80 && y < 160;

  if (left && top) {
    currentDisplayDriver->current_cyclic_screen = SCREEN_INDEX_MINER;
  } else if (right && top) {
    currentDisplayDriver->current_cyclic_screen = SCREEN_INDEX_CLOCK;
  } else if (left && bottom) {
    currentDisplayDriver->current_cyclic_screen = SCREEN_INDEX_GLOBAL;
  } else if (right && bottom) {
    currentDisplayDriver->current_cyclic_screen = SCREEN_INDEX_PRICES;
    lastPricesFetch = 0;
    fetchPrices();
  } else if (center) {
    if (currentDisplayDriver->current_cyclic_screen == SCREEN_INDEX_PRICES) {
      lastPricesFetch = 0;
      fetchPrices();
    } else {
      return;
    }
  } else {
    return;
  }

  Serial.printf(">>> Zone tactile reconnue : ecran actif -> %d\n", currentDisplayDriver->current_cyclic_screen);
}

void esp32S3ILI9341_DoLedStuff(unsigned long frame)
{
  esp32S3ILI9341_HandleTouch();

  // Bascule auto toutes les SCREEN_AUTO_CYCLE_MS. Si l'ecran a change entre
  // deux passages (bouton BOOT ou tactile), on recale le minuteur pour laisser
  // le nouvel ecran affiche 10 s pleines avant l'avance suivante.
  static unsigned long lastScreenSwitch = 0;
  static int lastSeenScreen = -1;
  unsigned long now = millis();
  int cur = currentDisplayDriver->current_cyclic_screen;

  if (cur != lastSeenScreen) {
    lastSeenScreen = cur;
    lastScreenSwitch = now;
    return;
  }

  if (now - lastScreenSwitch >= SCREEN_AUTO_CYCLE_MS) {
    int next = (cur + 1) % currentDisplayDriver->num_cyclic_screens;
    currentDisplayDriver->current_cyclic_screen = next;
    lastSeenScreen = next;
    lastScreenSwitch = now;
  }
}

CyclicScreenFunction esp32S3ILI9341CyclicScreens[] = {
  esp32S3ILI9341_MinerScreen
  // Reboot-loop ~12 s constate : les autres ecrans declenchent des requetes
  // HTTP (getBTCprice / getCoinData / fetchPrices / fetchBtcHistory) sur la
  // tache d'affichage (stack 9500 o, TLS gourmand). Cycle reduit a MINAGE
  // (100% local, aucun HTTP) le temps de passer ces fetch en asynchrone.
  // , esp32S3ILI9341_ClockScreen
  // , esp32S3ILI9341_GlobalScreen
  // , esp32S3ILI9341_PricesScreen
  // , esp32S3ILI9341_BtcChartScreen
};

DisplayDriver esp32S3ILI9341Driver = {
    esp32S3ILI9341_Init,
    esp32S3ILI9341_AlternateScreenState,
    esp32S3ILI9341_AlternateRotation,
    esp32S3ILI9341_LoadingScreen,
    esp32S3ILI9341_SetupScreen,
    esp32S3ILI9341CyclicScreens,
    esp32S3ILI9341_AnimateCurrentScreen,
    esp32S3ILI9341_DoLedStuff,
    SCREENS_ARRAY_SIZE(esp32S3ILI9341CyclicScreens),
    0,
    WIDTH,
    HEIGHT};

#endif
