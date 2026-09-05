#include "displayDriver.h"

#ifdef NERDMINER_S3_ILI9341

// ---------------------------------------------------------------------------
//  Driver ILI9341 S3 - version minimale : un seul ecran de minage, fond noir,
//  aucune requete reseau, aucun tactile, aucune bascule.
// ---------------------------------------------------------------------------

#include <TFT_eSPI.h>
#include "media/Free_Fonts.h"
#include "media/myFonts.h"
#include "OpenFontRender.h"
#include "monitor.h"
#include "version.h"

#define WIDTH  320
#define HEIGHT 240

// Cette dalle affiche les couleurs inversees par defaut (fond clair) : on
// force l'inversion pour un vrai fond noir. Mettre a false si un jour la dalle
// montre l'inverse.
#define ILI9341_INVERT true

// [ETAPE A] bascule auto entre ecrans (aucun reseau ici)
#define SCREEN_AUTO_CYCLE_MS 10000UL

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);
OpenFontRender render;   // [ETAPE B] police LCD pour la grosse horloge

static uint16_t C_BG, C_LABEL, C_VALUE, C_ACCENT;

void esp32S3ILI9341_Init(void)
{
  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  tft.invertDisplay(ILI9341_INVERT);
  tft.fillScreen(TFT_BLACK);

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

void esp32S3ILI9341_MinerScreen(unsigned long mElapsed)
{
  mining_data d = getMiningData(mElapsed);

  Serial.printf(">>> Completed %s share(s), %s Khashes, avg. hashrate %s KH/s\n",
                d.completedShares.c_str(), d.totalKHashes.c_str(), d.currentHashRate.c_str());

  spr.fillSprite(C_BG);

  spr.setTextDatum(TL_DATUM);
  spr.setFreeFont(FSSB9);
  spr.setTextColor(C_ACCENT, C_BG);
  spr.drawString("NERDMINER", 12, 8);

  spr.setFreeFont(FSS9);
  spr.setTextColor(C_LABEL, C_BG);
  spr.setTextDatum(TR_DATUM);
  spr.drawString(d.currentTime, WIDTH - 12, 8);
  spr.setTextDatum(TL_DATUM);

  spr.drawFastHLine(12, 28, WIDTH - 24, C_LABEL);

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

  spr.fillSprite(C_BG);
  spr.setTextDatum(TL_DATUM);
  spr.setFreeFont(FSSB9);
  spr.setTextColor(C_ACCENT, C_BG);
  spr.drawString("HORLOGE", 12, 8);
  spr.drawFastHLine(12, 28, WIDTH - 24, C_LABEL);

  // [ETAPE B] rendu via OpenFontRender (police LCD DigitalNumbers)
  render.setFontSize(46);
  uint32_t w = render.getTextWidth(t);
  render.drawString(t, (WIDTH - (int)w) / 2, 90, C_VALUE, C_BG);

  spr.pushSprite(0, 0);
}

// [ETAPE C] Ecran reseau : getCoinData() interroge mempool.space en HTTPS
// (difficulte, hashrate reseau, halving, frais). Suspect principal du bisect :
// requete bloquante sur la tache d'affichage (pile "Monitor" 9500 o).
void esp32S3ILI9341_GlobalScreen(unsigned long mElapsed)
{
  coin_data d = getCoinData(mElapsed);

  spr.fillSprite(C_BG);
  spr.setTextDatum(TL_DATUM);
  spr.setFreeFont(FSSB9);
  spr.setTextColor(C_ACCENT, C_BG);
  spr.drawString("RESEAU", 12, 8);
  spr.setFreeFont(FSS9);
  spr.setTextColor(C_LABEL, C_BG);
  spr.setTextDatum(TR_DATUM);
  spr.drawString(d.currentTime, WIDTH - 12, 8);
  spr.setTextDatum(TL_DATUM);
  spr.drawFastHLine(12, 28, WIDTH - 24, C_LABEL);

  drawLine(40,  "DIFFICULTE RESEAU",  d.netwrokDifficulty, C_VALUE);
  drawLine(92,  "HASHRATE RESEAU",    d.globalHashRate,    C_VALUE);
  drawLine(144, "BLOCS AV. HALVING",  d.remainingBlocks,   C_ACCENT);
  drawLine(196, "FRAIS (30 MIN)",     d.halfHourFee,       C_VALUE);

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
  esp32S3ILI9341_GlobalScreen
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
