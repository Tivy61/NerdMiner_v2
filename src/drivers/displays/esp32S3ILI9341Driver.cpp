#include "displayDriver.h"

#ifdef NERDMINER_S3_ILI9341

#include <TFT_eSPI.h>
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

OpenFontRender render;
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite background = TFT_eSprite(&tft);

SPIClass hSPI(HSPI);
XPT2046 touch(hSPI, ETOUCH_CS, TOUCH_IRQ);

extern monitor_data mMonitor;
extern DisplayDriver *currentDisplayDriver;
void esp32S3ILI9341_AlternateRotation(void);

// Zones tactiles : coins = navigation directe, centre = rafraichissement des
// cours (si on est deja sur l'ecran Prices). Pas de cycle automatique : les
// ecrans restent fixes tant qu'on ne touche pas l'ecran.
// Pas de reset WiFi par tactile : le signal tactile s'est avere capable de
// rester bloque plusieurs secondes sur une valeur figee (signal parasite),
// ce qui rendait un appui long dangereux pour une action destructrice comme
// celle-la. Le bouton BOOT physique (5s) reste le moyen fiable de reset.
#define TOUCH_CORNER_SIZE 70
#define SCREEN_INDEX_MINER  0
#define SCREEN_INDEX_CLOCK  1
#define SCREEN_INDEX_GLOBAL 2
#define SCREEN_INDEX_PRICES 3

bool touchWasDown = false;

#define BACK_COLOR TFT_BLACK
#define VALUE_COLOR TFT_GREEN
#define KEY_COLOR TFT_WHITE

struct prices_data {
  float btc = 0, eth = 0, hype = 0;
  float btc_chg = 0, eth_chg = 0, hype_chg = 0;
  bool valid = false;
};

prices_data lastPrices;
unsigned long lastPricesFetch = 0;
#define PRICES_UPDATE_MS (60UL * 1000UL)

void fetchPrices() {
  if (WiFi.status() != WL_CONNECTED) return;
  unsigned long now = millis();
  if (lastPrices.valid && (now - lastPricesFetch < PRICES_UPDATE_MS)) return;

  HTTPClient http;
  http.begin("https://api.coingecko.com/api/v3/simple/price?ids=bitcoin,ethereum,hyperliquid&vs_currencies=usd&include_24hr_change=true");
  http.setTimeout(5000);
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

void esp32S3ILI9341_Init(void)
{
  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  background.createSprite(WIDTH, HEIGHT);
  background.setSwapBytes(true);
  render.setDrawer(background);
  render.setLineSpaceRatio(0.9);

  if (render.loadFont(DigitalNumbers, sizeof(DigitalNumbers)))
  {
    Serial.println("Initialise error");
  }

  hSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI);
  // begin() attend la resolution native (portrait) du panneau, PAS la resolution
  // apres rotation : setRotation() se charge lui-meme d'echanger largeur/hauteur.
  touch.begin(HEIGHT, WIDTH);
  touch.setRotation(tft.getRotation());
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

void esp32S3ILI9341_MinerScreen(unsigned long mElapsed)
{
  mining_data data = getMiningData(mElapsed);

  Serial.printf(">>> Completed %s share(s), %s Khashes, avg. hashrate %s KH/s\n",
                data.completedShares.c_str(), data.totalKHashes.c_str(), data.currentHashRate.c_str());

  background.fillSprite(BACK_COLOR);
  int32_t x = 8, y = 6;
  background.setTextSize(1);
  background.setTextFont(FONT2);
  background.setTextColor(KEY_COLOR, BACK_COLOR);
  render.setFontSize(18);

  background.drawString("Heure", x, y); y += 16;
  render.drawString(data.currentTime.c_str(), x, y, VALUE_COLOR); y += 28;

  background.drawString("Hashrate", x, y); y += 16;
  render.drawString(data.currentHashRate.c_str(), x, y, VALUE_COLOR); y += 28;

  background.drawString("Shares valides", x, y); y += 16;
  render.drawString(data.completedShares.c_str(), x, y, VALUE_COLOR); y += 28;

  background.drawString("Meilleure diff.", x, y); y += 16;
  render.drawString(data.bestDiff.c_str(), x, y, VALUE_COLOR); y += 28;

  background.drawString("Temps de minage", x, y); y += 16;
  render.drawString(data.timeMining.c_str(), x, y, VALUE_COLOR);

  background.pushSprite(0, 0);
}
void esp32S3ILI9341_ClockScreen(unsigned long mElapsed)
{
  clock_data data = getClockData(mElapsed);

  background.fillSprite(BACK_COLOR);
  int32_t x = 8, y = 8;
  background.setTextSize(1);
  background.setTextFont(FONT2);
  background.setTextColor(KEY_COLOR, BACK_COLOR);
  render.setFontSize(20);

  background.drawString("Date", x, y); y += 22;
  render.drawString(data.currentDate.c_str(), x, y, VALUE_COLOR); y += 30;
  background.drawString("Heure", x, y); y += 22;
  render.drawString(data.currentTime.c_str(), x, y, VALUE_COLOR); y += 34;

  background.drawString("Cours BTC", x, y); y += 22;
  render.drawString(data.btcPrice.c_str(), x, y, VALUE_COLOR); y += 34;

  background.drawString("Hauteur de bloc", x, y); y += 22;
  render.drawString(data.blockHeight.c_str(), x, y, VALUE_COLOR);

  background.pushSprite(0, 0);
}

void esp32S3ILI9341_GlobalScreen(unsigned long mElapsed)
{
  coin_data data = getCoinData(mElapsed);

  background.fillSprite(BACK_COLOR);
  int32_t x = 8, y = 8;
  background.setTextSize(1);
  background.setTextFont(FONT2);
  background.setTextColor(KEY_COLOR, BACK_COLOR);
  render.setFontSize(18);

  background.drawString("Difficulte reseau", x, y); y += 22;
  render.drawString(data.netwrokDifficulty.c_str(), x, y, VALUE_COLOR); y += 30;

  background.drawString("Hashrate reseau", x, y); y += 22;
  render.drawString(data.globalHashRate.c_str(), x, y, VALUE_COLOR); y += 30;

  background.drawString("Blocs avant halving", x, y); y += 22;
  render.drawString(data.remainingBlocks.c_str(), x, y, VALUE_COLOR); y += 30;

  background.drawString("Frais (30 min)", x, y); y += 22;
  render.drawString(data.halfHourFee.c_str(), x, y, VALUE_COLOR);

  background.pushSprite(0, 0);
}

void esp32S3ILI9341_PricesScreen(unsigned long mElapsed)
{
  fetchPrices();

  background.fillSprite(BACK_COLOR);
  int32_t x = 8, y = 6;
  background.setTextSize(1);
  background.setTextFont(FONT2);
  background.setTextColor(KEY_COLOR, BACK_COLOR);

  if (!lastPrices.valid) {
    background.setTextSize(2);
    background.drawString("Chargement cours...", x, 100);
    background.pushSprite(0, 0);
    return;
  }

  char buf[24];

  background.drawString("BTC", x, y); y += 16;
  render.setFontSize(20);
  snprintf(buf, sizeof(buf), "$%.2f", lastPrices.btc);
  render.drawString(buf, x, y, VALUE_COLOR);
  render.setFontSize(14);
  snprintf(buf, sizeof(buf), "%+.1f%%", lastPrices.btc_chg);
  render.drawString(buf, x + 170, y, lastPrices.btc_chg >= 0 ? TFT_GREEN : TFT_RED);
  y += 28;

  background.drawString("ETH", x, y); y += 16;
  render.setFontSize(20);
  snprintf(buf, sizeof(buf), "$%.2f", lastPrices.eth);
  render.drawString(buf, x, y, VALUE_COLOR);
  render.setFontSize(14);
  snprintf(buf, sizeof(buf), "%+.1f%%", lastPrices.eth_chg);
  render.drawString(buf, x + 170, y, lastPrices.eth_chg >= 0 ? TFT_GREEN : TFT_RED);
  y += 28;

  background.drawString("HYPE", x, y); y += 16;
  render.setFontSize(20);
  snprintf(buf, sizeof(buf), "$%.2f", lastPrices.hype);
  render.drawString(buf, x, y, VALUE_COLOR);
  render.setFontSize(14);
  snprintf(buf, sizeof(buf), "%+.1f%%", lastPrices.hype_chg);
  render.drawString(buf, x + 170, y, lastPrices.hype_chg >= 0 ? TFT_GREEN : TFT_RED);

  background.pushSprite(0, 0);
}

void esp32S3ILI9341_LoadingScreen(void)
{
  background.fillSprite(BACK_COLOR);
  background.setTextSize(2);
  background.setTextFont(FONT2);
  background.setTextColor(TFT_GREEN, BACK_COLOR);
  background.drawString("MyPicsou Miner", 20, 90);
  background.setTextSize(1);
  background.setTextColor(TFT_WHITE, BACK_COLOR);
  background.drawString(CURRENT_VERSION, 20, 130);
  background.pushSprite(0, 0);
}

void esp32S3ILI9341_SetupScreen(void)
{
  background.fillSprite(BACK_COLOR);
  background.setTextSize(2);
  background.setTextFont(FONT2);
  background.setTextColor(TFT_YELLOW, BACK_COLOR);
  background.drawString("Config WiFi requise", 10, 90);
  background.setTextSize(1);
  background.setTextColor(TFT_WHITE, BACK_COLOR);
  background.drawString("Rejoindre: NerdMinerAP", 10, 130);
  background.pushSprite(0, 0);
}

void esp32S3ILI9341_AnimateCurrentScreen(unsigned long frame) {}

void esp32S3ILI9341_HandleTouch(void)
{
  if (!touch.pressed()) {
    touchWasDown = false;
    return;
  }

  if (touchWasDown) return; // deja traite au premier contact, rien de plus tant que le doigt reste pose

  uint16_t x = touch.X();
  uint16_t y = touch.Y();
  Serial.printf(">>> Touch detecte (brut) : x=%u y=%u (raw x=%u y=%u)\n", x, y, touch.RawX(), touch.RawY());

  // Front montant : premiere detection de ce contact, on determine la zone
  // et on declenche l'action une seule fois, meme si le doigt reste pose.
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
      return; // rien a faire au centre en dehors de l'ecran Prices
    }
  } else {
    return; // touche hors zone active : rien a faire
  }

  Serial.printf(">>> Zone tactile reconnue : ecran actif -> %d\n", currentDisplayDriver->current_cyclic_screen);
}

void esp32S3ILI9341_DoLedStuff(unsigned long frame)
{
  esp32S3ILI9341_HandleTouch();
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