#include "displayDriver.h"

#ifdef NERDMINER_S3_ILI9341

// ---------------------------------------------------------------------------
//  Driver ILI9341 S3 - version minimale : un seul ecran de minage, fond noir,
//  aucune requete reseau, aucun tactile, aucune bascule.
// ---------------------------------------------------------------------------

#include <TFT_eSPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include "media/Free_Fonts.h"
#include "media/myFonts.h"
#include "OpenFontRender.h"
#include "monitor.h"
#include "version.h"

extern String getTime(void);   // "HH:MM" local NTP-cache (monitor.cpp)
extern String getDate(void);   // "DD/MM/YYYY" local (monitor.cpp), pas de reseau

#define WIDTH  320
#define HEIGHT 240

// Cette dalle affiche les couleurs inversees par defaut (fond clair) : on
// force l'inversion pour un vrai fond noir. Mettre a false si un jour la dalle
// montre l'inverse.
#define ILI9341_INVERT true

// [ETAPE A] bascule auto entre ecrans (aucun reseau ici)
#define SCREEN_AUTO_CYCLE_MS 10000UL

// Reinit auto de la dalle (voir initPanel) : la dalle s'est deja bloquee entre
// ~5 min et plusieurs heures apres boot selon les essais. 3 min borne le temps
// d'ecran noir max sans etre gênant (un flash noir ~150 ms, invisible en usage).
#define PANEL_REINIT_MS (3UL * 60UL * 1000UL)

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);
OpenFontRender render;   // [ETAPE B] police LCD pour la grosse horloge

static uint16_t C_BG, C_LABEL, C_VALUE, C_ACCENT;

// Sequence d'init/reset de la dalle uniquement (pas le sprite, pas les polices).
// Rejouee au demarrage ET periodiquement (voir DoLedStuff) car ce panneau se
// bloque parfois dans un etat interne fige apres un moment de fonctionnement
// (signal SPI marginal) ; seul un vrai reset+reinit ILI9341 le resynchronise -
// on l'a constate empiriquement (un reset ESP32 relance l'ecran a chaque fois).
static void initPanel(void)
{
  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  tft.invertDisplay(ILI9341_INVERT);
  tft.fillScreen(TFT_BLACK);
}

void esp32S3ILI9341_Init(void)
{
  initPanel();

  spr.createSprite(WIDTH, HEIGHT);
  spr.setSwapBytes(true);

  // [ETAPE B] OpenFontRender pour la grosse horloge LCD
  render.setDrawer(spr);
  render.setLineSpaceRatio(0.9);
  if (render.loadFont(DigitalNumbers, sizeof(DigitalNumbers)))
    Serial.println("Initialise error");

  C_BG     = TFT_BLACK;
  C_LABEL  = tft.color565(150, 158, 170);
  C_VALUE  = TFT_WHITE;
  C_ACCENT = tft.color565(247, 147, 26);   // orange Bitcoin
}

void esp32S3ILI9341_AlternateScreenState(void)
{
  // Retroeclairage cable en direct (pas de PWM) : rien a faire.
}

void esp32S3ILI9341_AlternateRotation(void)
{
  tft.setRotation(tft.getRotation() == 1 ? 3 : 1);
}

static void drawLine(int y, const char *label, const String &value, uint16_t vcol)
{
  spr.setTextDatum(TL_DATUM);
  spr.setFreeFont(FSS9);
  spr.setTextColor(C_LABEL, C_BG);
  spr.drawString(label, 12, y);
  spr.setFreeFont(FSSB18);
  spr.setTextColor(vcol, C_BG);
  spr.drawString(value, 12, y + 16);
}

// Jauge WiFi 4 barres (x = bord gauche, y = haut ; bloc ~18x13).
static void drawWifi(int x, int y)
{
  int bars = 0;
  if (WiFi.status() == WL_CONNECTED) {
    long r = WiFi.RSSI();
    if      (r >= -55) bars = 4;
    else if (r >= -65) bars = 3;
    else if (r >= -73) bars = 2;
    else               bars = 1;
  }
  for (int i = 0; i < 4; i++) {
    int bh = 3 + i * 3;
    spr.fillRect(x + i * 5, y + 12 - bh, 3, bh, (i < bars) ? C_ACCENT : C_LABEL);
  }
  if (bars == 0) {                       // croix rouge si hors ligne
    spr.drawLine(x, y + 1, x + 16, y + 12, TFT_RED);
    spr.drawLine(x, y + 12, x + 16, y + 1, TFT_RED);
  }
}

// Bandeau commun : titre orange a gauche, heure + jauge WiFi a droite, filet.
static void drawHeader(const char *title, const String &timeStr)
{
  spr.fillSprite(C_BG);
  spr.setTextDatum(TL_DATUM);
  spr.setFreeFont(FSSB9);
  spr.setTextColor(C_ACCENT, C_BG);
  spr.drawString(title, 12, 8);

  int rightEdge = WIDTH - 12;
  spr.setFreeFont(FSS9);
  if (timeStr.length()) {
    spr.setTextColor(C_LABEL, C_BG);
    spr.setTextDatum(TR_DATUM);
    spr.drawString(timeStr, rightEdge, 8);
    rightEdge -= spr.textWidth(timeStr) + 8;
    spr.setTextDatum(TL_DATUM);
  }
  drawWifi(rightEdge - 18, 8);

  spr.drawFastHLine(12, 28, WIDTH - 24, C_LABEL);
}

void esp32S3ILI9341_MinerScreen(unsigned long mElapsed)
{
  mining_data d = getMiningData(mElapsed);

  Serial.printf(">>> Completed %s share(s), %s Khashes, avg. hashrate %s KH/s\n",
                d.completedShares.c_str(), d.totalKHashes.c_str(), d.currentHashRate.c_str());

  drawHeader("NERDMINER", d.currentTime);

  drawLine(40,  "HASHRATE",        d.currentHashRate + " kH/s", C_VALUE);
  drawLine(92,  "SHARES VALIDES",  d.completedShares,           C_VALUE);
  drawLine(144, "MEILLEURE DIFF",  d.bestDiff,                  C_ACCENT);
  drawLine(196, "TEMPS DE MINAGE", d.timeMining,                C_VALUE);

  spr.pushSprite(0, 0);
}

// [ETAPE A] Ecran horloge : heure locale seulement, aucune requete reseau
// (getClockData_t = calcul local NTP-cache, ne touche pas getBTCprice ni mempool).
void esp32S3ILI9341_ClockScreen(unsigned long mElapsed)
{
  clock_data_t d = getClockData_t(mElapsed);
  char t[12];
  snprintf(t, sizeof(t), "%02lu:%02lu:%02lu", d.currentHours, d.currentMinutes, d.currentSeconds);

  drawHeader("HORLOGE", "");

  // [ETAPE B] rendu via OpenFontRender (police LCD DigitalNumbers)
  render.setFontSize(46);
  uint32_t w = render.getTextWidth(t);
  render.drawString(t, (WIDTH - (int)w) / 2, 84, C_VALUE, C_BG);

  // date DD/MM/YYYY sous l'horloge
  spr.setFreeFont(FSSB12);
  spr.setTextColor(C_LABEL, C_BG);
  spr.setTextDatum(MC_DATUM);
  spr.drawString(getDate(), WIDTH / 2, 176);
  spr.setTextDatum(TL_DATUM);

  spr.pushSprite(0, 0);
}

// [ETAPE C] Ecran reseau : getCoinData() interroge mempool.space en HTTPS
// (difficulte, hashrate reseau, halving, frais). Suspect principal du bisect :
// requete bloquante sur la tache d'affichage (pile "Monitor" 9500 o).
void esp32S3ILI9341_GlobalScreen(unsigned long mElapsed)
{
  coin_data d = getCoinData(mElapsed);

  drawHeader("RESEAU", d.currentTime);

  drawLine(40,  "DIFFICULTE RESEAU",  d.netwrokDifficulty, C_VALUE);
  drawLine(92,  "HASHRATE RESEAU",    d.globalHashRate,    C_VALUE);
  drawLine(144, "BLOCS AV. HALVING",  d.remainingBlocks,   C_ACCENT);
  drawLine(196, "FRAIS (30 MIN)",     d.halfHourFee,       C_VALUE);

  spr.pushSprite(0, 0);
}

// [ETAPE D] Ecran marches : BTC / ETH / HYPE en dollars via CoinGecko (HTTPS).
struct prices_data {
  float btc = 0, eth = 0, hype = 0;
  float btc_chg = 0, eth_chg = 0, hype_chg = 0;
  bool valid = false;
};
static prices_data lastPrices;
static unsigned long lastPricesFetch = 0;
#define PRICES_UPDATE_MS (90UL * 1000UL)

static void fetchPrices(void)
{
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
      lastPrices.btc     = doc["bitcoin"]["usd"]              | lastPrices.btc;
      lastPrices.btc_chg = doc["bitcoin"]["usd_24h_change"]   | lastPrices.btc_chg;
      lastPrices.eth     = doc["ethereum"]["usd"]             | lastPrices.eth;
      lastPrices.eth_chg = doc["ethereum"]["usd_24h_change"]  | lastPrices.eth_chg;
      lastPrices.hype    = doc["hyperliquid"]["usd"]          | lastPrices.hype;
      lastPrices.hype_chg= doc["hyperliquid"]["usd_24h_change"]| lastPrices.hype_chg;
      lastPrices.valid = true;
      lastPricesFetch = now;
    }
  }
  http.end();
}

static void priceRow(int y, const char *tk, uint16_t tkcol, float price, float chg, bool cents)
{
  spr.setTextDatum(TL_DATUM);
  spr.setFreeFont(FSSB12);
  spr.setTextColor(tkcol, C_BG);
  spr.drawString(tk, 12, y + 4);
  int valx = 12 + spr.textWidth(tk) + 16;

  char b[24];
  if (cents) snprintf(b, sizeof(b), "$%.2f", price);
  else       snprintf(b, sizeof(b), "$%.0f", price);
  spr.setFreeFont(FSSB18);
  spr.setTextColor(C_VALUE, C_BG);
  spr.drawString(b, valx, y);

  uint16_t c = chg >= 0 ? tft.color565(45, 210, 120) : tft.color565(235, 80, 80);
  snprintf(b, sizeof(b), "%.1f%%", fabsf(chg));
  spr.setFreeFont(FSS9);
  spr.setTextColor(c, C_BG);
  spr.setTextDatum(TR_DATUM);
  spr.drawString(b, WIDTH - 12, y + 6);
  int tw = spr.textWidth(b);
  int tx = WIDTH - 12 - tw - 11;
  if (chg >= 0) spr.fillTriangle(tx, y + 17, tx + 9, y + 17, tx + 4, y + 6,  c);
  else          spr.fillTriangle(tx, y + 7,  tx + 9, y + 7,  tx + 4, y + 18, c);
  spr.setTextDatum(TL_DATUM);
}

void esp32S3ILI9341_PricesScreen(unsigned long mElapsed)
{
  fetchPrices();
  drawHeader("MARCHES", getTime());

  if (!lastPrices.valid) {
    spr.setFreeFont(FSS12);
    spr.setTextColor(C_LABEL, C_BG);
    spr.setTextDatum(MC_DATUM);
    spr.drawString("Chargement des cours...", WIDTH / 2, HEIGHT / 2);
    spr.setTextDatum(TL_DATUM);
    spr.pushSprite(0, 0);
    return;
  }

  priceRow(44,  "BTC",  C_ACCENT, lastPrices.btc,  lastPrices.btc_chg,  false);
  spr.drawFastHLine(12, 88, WIDTH - 24, C_LABEL);
  priceRow(102, "ETH",  C_VALUE,  lastPrices.eth,  lastPrices.eth_chg,  true);
  spr.drawFastHLine(12, 146, WIDTH - 24, C_LABEL);
  priceRow(160, "HYPE", C_VALUE,  lastPrices.hype, lastPrices.hype_chg, true);

  spr.pushSprite(0, 0);
}

void esp32S3ILI9341_LoadingScreen(void)
{
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(MC_DATUM);
  spr.setFreeFont(FSSB18);
  spr.setTextColor(tft.color565(247, 147, 26), TFT_BLACK);
  spr.drawString("NerdMiner", WIDTH / 2, HEIGHT / 2 - 12);
  spr.setFreeFont(FSS9);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString(CURRENT_VERSION, WIDTH / 2, HEIGHT / 2 + 16);
  spr.setTextDatum(TL_DATUM);
  spr.pushSprite(0, 0);
}

void esp32S3ILI9341_SetupScreen(void)
{
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(MC_DATUM);
  spr.setFreeFont(FSSB12);
  spr.setTextColor(TFT_YELLOW, TFT_BLACK);
  spr.drawString("Configuration WiFi requise", WIDTH / 2, HEIGHT / 2 - 12);
  spr.setFreeFont(FSS9);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("Rejoindre le WiFi : NerdMinerAP", WIDTH / 2, HEIGHT / 2 + 14);
  spr.setTextDatum(TL_DATUM);
  spr.pushSprite(0, 0);
}

void esp32S3ILI9341_AnimateCurrentScreen(unsigned long frame) {}

void esp32S3ILI9341_DoLedStuff(unsigned long frame)
{
  // Reinit periodique de la dalle (voir commentaire sur initPanel) : la
  // reapplique avant qu'elle ait le temps de rester bloquee des heures.
  // N'affecte ni le minage ni le WiFi (tache separee) : juste un flash noir
  // de l'ecran, ~150 ms, avant que le prochain rafraichissement le repeigne.
  static unsigned long lastPanelReinit = 0;
  unsigned long now0 = millis();
  if (now0 - lastPanelReinit >= PANEL_REINIT_MS) {
    initPanel();
    lastPanelReinit = now0;
  }

  // [ETAPE A] bascule auto toutes les SCREEN_AUTO_CYCLE_MS. Si l'ecran a change
  // entre-temps (bouton BOOT), on recale le minuteur.
  static unsigned long lastSwitch = 0;
  static int lastSeen = -1;
  unsigned long now = millis();
  int cur = currentDisplayDriver->current_cyclic_screen;

  if (cur != lastSeen) { lastSeen = cur; lastSwitch = now; return; }
  if (now - lastSwitch >= SCREEN_AUTO_CYCLE_MS) {
    int next = (cur + 1) % currentDisplayDriver->num_cyclic_screens;
    currentDisplayDriver->current_cyclic_screen = next;
    lastSeen = next;
    lastSwitch = now;
  }
}

CyclicScreenFunction esp32S3ILI9341CyclicScreens[] = {
  esp32S3ILI9341_MinerScreen,
  esp32S3ILI9341_ClockScreen,
  esp32S3ILI9341_GlobalScreen,
  esp32S3ILI9341_PricesScreen
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
