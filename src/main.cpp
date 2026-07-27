#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include "time.h"
#include "sprite_frames.h"
#include "display_pm.h"
#include "offline_ind.h"
#include "settings_ui.h"
#include "globals.h"
#include "capture.h"
#include "mode_sprite.h"
#include "mode_clock.h"
#include "mode_system.h"

// --- CYD PIN CONFIGURATION ---
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

TFT_eSPI tft = TFT_eSPI();
TFT_eSPI* canvas = &tft;
SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
Preferences prefs;

int currentMode = 0;
unsigned long modeTimer = 0;
bool modeChanged = true;

uint8_t spriteFrame = 0;
uint8_t spriteAnim = 0;
unsigned long lastSpriteFrame = 0;
bool dynamicSprite = true;
uint8_t displayRotation = 0;
bool displayInverted = true;

int usageSession = 0;
int usageWeekly = 0;
int usageSR = 0;
int usageWR = 0;
int claudeWaiting = 0;
unsigned long lastUsageFetch = 0;

String daemonUrl;
String daemonToken;

bool captureRecording = false;
bool captureFrameReady = false;

unsigned long animNow() { return capture::now(); }

inline int mapTouchX(int raw) {
  return (displayRotation == 2) ? map(raw, 3900, 300, 0, 240) : map(raw, 300, 3900, 0, 240);
}
inline int mapTouchY(int raw) {
  return (displayRotation == 2) ? map(raw, 3900, 300, 0, 320) : map(raw, 300, 3900, 0, 320);
}

// Simple HTTP server for capture control
#include <WebServer.h>
WebServer captureServer(8789);

static void handleCaptureStart() {
  String sink = captureServer.arg("sink");
  if (sink.length() == 0) {
    // Derive from daemon URL (same machine that runs the daemon)
    String host = daemonUrl;
    if (host.startsWith("http://")) host = host.substring(7);
    int colonIdx = host.indexOf(':');
    if (colonIdx > 0) host = host.substring(0, colonIdx);
    int slashIdx = host.indexOf('/');
    if (slashIdx > 0) host = host.substring(0, slashIdx);
    sink = "http://" + host + ":8788";
  }
  if (capture::init()) {
    capture::startRecording(sink);
    captureServer.send(200, "text/plain", "recording to " + sink);
  } else {
    captureServer.send(500, "text/plain", "sprite alloc failed");
  }
}

static void handleCaptureStop() {
  capture::stopRecording();
  captureServer.send(200, "text/plain", "stopped, frames=" + String(capture::frameNum));
}

static void handleCaptureStatus() {
  String json = "{\"recording\":" + String(capture::isActive() ? "true" : "false");
  json += ",\"frames\":" + String(capture::frameNum);
  json += ",\"heap\":" + String(ESP.getFreeHeap());
  json += ",\"sink\":\"" + capture::sinkUrl + "\"";
  json += ",\"ready\":" + String(captureFrameReady ? "true" : "false") + "}";
  captureServer.send(200, "application/json", json);
}

const uint8_t banner_ohmy[BANNER_H][BANNER_W] PROGMEM = {
  {1,1,1,0,1,0,1,0,0,0,0,0,1,0,1,0,1,0,1},
  {1,0,1,0,1,0,1,0,0,0,0,0,1,1,1,0,1,0,1},
  {1,0,1,0,1,1,1,0,0,0,0,0,1,1,1,0,0,1,0},
  {1,0,1,0,1,0,1,0,0,0,0,0,1,0,1,0,0,1,0},
  {1,1,1,0,1,0,1,0,0,0,0,0,1,0,1,0,0,1,0},
};
const uint8_t banner_clawd[BANNER_H][BANNER_W] PROGMEM = {
  {1,1,1,0,1,0,0,0,1,1,1,0,1,0,1,0,1,1,0},
  {1,0,0,0,1,0,0,0,1,0,1,0,1,0,1,0,1,0,1},
  {1,0,0,0,1,0,0,0,1,1,1,0,1,1,1,0,1,0,1},
  {1,0,0,0,1,0,0,0,1,0,1,0,1,1,1,0,1,0,1},
  {1,1,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,1,0},
};

void drawPageIndicator() {
  const int total = 4, size = 6, gap = 4, y = NAV_Y + 6;
  int w = total * size + (total - 1) * gap;
  int x = (240 - w) / 2;
  for (int i = 0; i < total; i++) {
    int bx = x + i * (size + gap);
    if (i == currentMode) canvas->fillRect(bx, y, size, size, TFT_ORANGE);
    else canvas->drawRect(bx, y, size, size, TFT_ORANGE);
  }
  canvas->setTextColor(TFT_ORANGE, TFT_BLACK);
  canvas->setTextSize(1);
  canvas->drawCentreString("<", NAV_LEFT_X + NAV_BTN_W / 2, NAV_Y + 2, 2);
  canvas->drawCentreString(">", NAV_RIGHT_X + NAV_BTN_W / 2, NAV_Y + 2, 2);
}

bool handleNavTap(int tapX, int tapY) {
  if (tapY < NAV_Y || tapY > NAV_Y + NAV_H) return false;
  if (tapX >= NAV_LEFT_X && tapX < NAV_LEFT_X + NAV_BTN_W) {
    currentMode = (currentMode + 3) % 4;
    modeChanged = true; modeTimer = millis(); canvas->fillScreen(TFT_BLACK);
    return true;
  }
  if (tapX >= NAV_RIGHT_X && tapX < NAV_RIGHT_X + NAV_BTN_W) {
    currentMode = (currentMode + 1) % 4;
    modeChanged = true; modeTimer = millis(); canvas->fillScreen(TFT_BLACK);
    return true;
  }
  return false;
}

void fetchUsage();
void checkOTA();

void setup() {
  Serial.begin(115200);
  prefs.begin("ohmyclawd", false);
  daemonUrl = prefs.getString("url", "http://ohmyclawd.local:8787");
  daemonToken = prefs.getString("token", "");
  String tzStr = prefs.getString("tz", "UTC-8");
  dynamicSprite = prefs.getBool("dyn_spr", true);
  displayRotation = prefs.getUChar("rot", 0);
  displayInverted = prefs.getBool("inverted", true);
  if (displayRotation != 0 && displayRotation != 2) displayRotation = 0;
  prefs.end();

  tft.init();
  tft.invertDisplay(displayInverted);
  display_pm::init(prefs);
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);

  tft.setRotation(displayRotation);
  ts.setRotation(0);

  tft.fillScreen(TFT_BLACK);
  const int cell = 8, px = 7;
  int xOff = (240 - BANNER_W * cell) / 2;
  int yOff = 30;
  for (int r = 0; r < BANNER_H; r++)
    for (int c = 0; c < BANNER_W; c++)
      if (pgm_read_byte(&banner_ohmy[r][c])) tft.fillRect(xOff + c * cell, yOff + r * cell, px, px, TFT_ORANGE);
  yOff += BANNER_H * cell + 4;
  for (int r = 0; r < BANNER_H; r++)
    for (int c = 0; c < BANNER_W; c++)
      if (pgm_read_byte(&banner_clawd[r][c])) tft.fillRect(xOff + c * cell, yOff + r * cell, px, px, TFT_ORANGE);

  tft.setTextColor(TFT_DARKGREY);
  tft.setTextSize(1); tft.drawCentreString("Connect to AP:", 120, 160, 1);
  tft.setTextColor(TFT_ORANGE);
  tft.setTextSize(2); tft.drawCentreString("OhMyClawd", 120, 180, 1);
  tft.setTextColor(TFT_DARKGREY);
  tft.setTextSize(1); tft.drawCentreString("then open 192.168.4.1", 120, 210, 1);

  WiFiManager wm;
  WiFiManagerParameter daemonParam("daemon_url", "Daemon URL", daemonUrl.c_str(), 80);
  WiFiManagerParameter tzParam("timezone", "Timezone (POSIX)", tzStr.c_str(), 40);
  WiFiManagerParameter tokenParam("daemon_token", "Daemon Token (optional)", daemonToken.c_str(), 40);
  wm.addParameter(&daemonParam);
  wm.addParameter(&tzParam);
  wm.addParameter(&tokenParam);
  wm.setConfigPortalTimeout(300);
  if (!wm.autoConnect("OhMyClawd")) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED);
    tft.setTextSize(3); tft.drawCentreString("WIFI FAILED", 120, 150, 1);
    tft.setTextSize(2); tft.drawCentreString("Restarting...", 120, 190, 1);
    delay(3000);
    ESP.restart();
  }

  String newUrl = String(daemonParam.getValue());
  String newTz = String(tzParam.getValue());
  String newToken = String(tokenParam.getValue());
  if ((newUrl.length() > 0 && newUrl != daemonUrl) || (newTz.length() > 0 && newTz != tzStr) || (newToken != daemonToken)) {
    if (newUrl.length() > 0) daemonUrl = newUrl;
    if (newTz.length() > 0) tzStr = newTz;
    daemonToken = newToken;
    prefs.begin("ohmyclawd", false);
    prefs.putString("url", daemonUrl);
    prefs.putString("tz", tzStr);
    prefs.putString("token", daemonToken);
    prefs.end();
  }

  tft.fillScreen(TFT_ORANGE); tft.setTextColor(TFT_BLACK);
  tft.setTextSize(3); tft.drawCentreString("CONNECTED!", 120, 160, 1);
  configTime(0, 0, "pool.ntp.org");
  setenv("TZ", tzStr.c_str(), 1);
  tzset();
  delay(1000);
  checkOTA();
  tft.fillScreen(TFT_BLACK);
  modeTimer = millis();

  // Capture control server
  captureServer.on("/capture/start", HTTP_POST, handleCaptureStart);
  captureServer.on("/capture/stop", HTTP_POST, handleCaptureStop);
  captureServer.on("/capture/status", HTTP_GET, handleCaptureStatus);
  captureServer.begin();
  Serial.printf("[capture] control server on :8789\n");
}

void loop() {
  if (ts.touched()) {
    if (display_pm::isSleeping()) {
      display_pm::wake(10000);
      while (ts.touched()) { delay(20); }
      delay(200);
      return;
    }
    TS_Point p = ts.getPoint();
    int startX = mapTouchX(p.x);
    int lastX = startX;
    unsigned long touchStart = millis();
    while (ts.touched()) {
      unsigned long elapsedSoFar = millis() - touchStart;
      if (currentMode == 3) {
        int yMapped = mapTouchY(p.y);
        int xMapped = mapTouchX(p.x);
        settings_ui::handleHoldTick(*canvas, yMapped, xMapped, elapsedSoFar);
        capture::markFrame();
        capture::flush();
        capture::endFrame();
        if (settings_ui::consumeResetIfTriggered()) {
          WiFiManager wm;
          wm.resetSettings();
          prefs.begin("ohmyclawd", false); prefs.clear(); prefs.end();
          ESP.restart();
        }
      } else {
        if (elapsedSoFar >= 5000) {
          WiFiManager wm;
          wm.resetSettings();
          prefs.begin("ohmyclawd", false); prefs.clear(); prefs.end();
          ESP.restart();
        }
      }
      p = ts.getPoint();
      lastX = mapTouchX(p.x);
      delay(20);
    }
    unsigned long elapsed = millis() - touchStart;
    int deltaX = lastX - startX;
    if (currentMode == 3) settings_ui::cancelHold();
    if (elapsed < 500 && abs(deltaX) > 40) {
      if (currentMode == 3) settings_ui::exit();
      if (deltaX > 0) { currentMode = (currentMode + 3) % 4; }
      else { currentMode = (currentMode + 1) % 4; }
      modeChanged = true; modeTimer = millis(); canvas->fillScreen(TFT_BLACK);
    } else if (elapsed < 300 && abs(deltaX) < 20) {
      int tapY = mapTouchY(p.y);
      int tapX = mapTouchX(p.x);
      if (handleNavTap(tapX, tapY)) {
        // handled
      } else if (currentMode == 3) {
        settings_ui::handleTap(tft, tapY, tapX);
      } else if (currentMode == 0) {
        if (dynamicSprite) {
          static const uint8_t waitPool[] = {3, 4};
          static const uint8_t limitPool[] = {2, 6};
          static const uint8_t heavyPool[] = {8, 7};
          static const uint8_t modPool[] = {12, 11};
          static const uint8_t lightPool[] = {0, 1, 9, 10, 5};
          const uint8_t* pool; uint8_t poolSize;
          if (claudeWaiting > 0) { pool = waitPool; poolSize = 2; }
          else if (usageSession >= 80) { pool = limitPool; poolSize = 2; }
          else if (usageSession >= 50) { pool = heavyPool; poolSize = 2; }
          else if (usageSession >= 25) { pool = modPool; poolSize = 2; }
          else { pool = lightPool; poolSize = 5; }
          uint8_t idx = 0;
          for (uint8_t i = 0; i < poolSize; i++) { if (pool[i] == spriteAnim) { idx = i; break; } }
          spriteAnim = pool[(idx + 1) % poolSize];
        } else {
          spriteAnim = (spriteAnim + 1) % SPRITE_ANIM_COUNT;
        }
        spriteFrame = 0;
        // Feedback: show sprite number briefly in status area
        canvas->fillRect(0, 35, 240, 10, TFT_BLACK);
        canvas->setTextSize(1); canvas->setTextColor(TFT_ORANGE, TFT_BLACK);
        canvas->setTextDatum(MC_DATUM);
        canvas->drawString("sprite " + String(spriteAnim + 1) + "/" + String(SPRITE_ANIM_COUNT), 120, 39, 1);
        canvas->setTextDatum(TL_DATUM);
      }
    }
    delay(200);
  }
  display_pm::tick();
  offline_ind::update();
  uint32_t cycleMs = display_pm::getCycleIntervalMs();
  if (cycleMs > 0 && currentMode != 3 && (millis() - modeTimer > cycleMs)) {
    currentMode = (currentMode + 1) % 3;
    modeChanged = true; modeTimer = millis(); canvas->fillScreen(TFT_BLACK);
  }
  if (millis() - lastUsageFetch > 30000 || lastUsageFetch == 0) { fetchUsage(); lastUsageFetch = millis(); }

  captureServer.handleClient();

  switch (currentMode) {
    case 0: runSprite(); break;
    case 1: runClock(); break;
    case 2: runSystem(); break;
    case 3:
      if (modeChanged) { settings_ui::enter(); modeChanged = false; settings_ui::render(*canvas, true); }
      else { settings_ui::render(*canvas, false); }
      offline_ind::drawGlyph(*canvas);
      capture::markFrame();
      break;
  }
  drawPageIndicator();
  capture::flush();
  capture::endFrame();
}

void fetchUsage() {
  HTTPClient http;
  http.begin(daemonUrl + "/usage");
  if (daemonToken.length() > 0) {
    http.addHeader("Authorization", "Bearer " + daemonToken);
  }
  int code = http.GET();
  if (code == 200) {
    offline_ind::recordSuccess();
    JsonDocument doc; deserializeJson(doc, http.getString());
    usageSession = doc["s"] | 0;
    usageWeekly = doc["w"] | 0;
    usageSR = doc["sr"] | 0;
    usageWR = doc["wr"] | 0;
    claudeWaiting = doc["cw"] | 0;
    if (currentMode == 0 && dynamicSprite) {
      uint8_t newAnim;
      static const uint8_t waitP[] = {3, 4};
      static const uint8_t limitP[] = {2, 6};
      static const uint8_t heavyP[] = {8, 7};
      static const uint8_t modP[] = {12, 11};
      static const uint8_t lightP[] = {0, 1, 9, 10, 5};
      if (claudeWaiting > 0) newAnim = waitP[random(2)];
      else if (usageSession >= 80) newAnim = limitP[random(2)];
      else if (usageSession >= 50) newAnim = heavyP[random(2)];
      else if (usageSession >= 25) newAnim = modP[random(2)];
      else newAnim = lightP[random(5)];
      if (newAnim != spriteAnim) { spriteAnim = newAnim; spriteFrame = 0; }
    }
  } else {
    offline_ind::recordFailure();
  }
  http.end();
}

void checkOTA() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1); tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawCentreString("checking for updates...", 120, 155, 1);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(client, "https://api.github.com/repos/opariffazman/ohmyclawd/releases/latest");
  http.addHeader("User-Agent", "OhMyClawd-ESP32");
  int code = http.GET();
  if (code != 200) { http.end(); return; }

  JsonDocument doc;
  deserializeJson(doc, http.getString());
  http.end();

  String tag = doc["tag_name"] | "";
  if (tag.startsWith("v")) tag = tag.substring(1);
  String current = VERSION;
  if (tag.length() == 0 || tag == current) return;

  String assetUrl = "";
  for (JsonObject asset : doc["assets"].as<JsonArray>()) {
    String name = asset["name"] | "";
    if (name == "ohmyclawd-firmware.bin") {
      assetUrl = asset["browser_download_url"].as<String>();
      break;
    }
  }
  if (assetUrl.length() == 0) return;

  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2); tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawCentreString("UPDATE", 120, 20, 1);
  tft.setTextSize(1); tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawCentreString("v" + current + " -> v" + tag, 120, 45, 1);
  tft.fillRect(30, 70, 80, 40, TFT_ORANGE);
  tft.setTextSize(2); tft.setTextColor(TFT_BLACK);
  tft.drawCentreString("YES", 70, 80, 1);
  tft.fillRect(130, 70, 80, 40, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE);
  tft.drawCentreString("NO", 170, 80, 1);

  String notes = doc["body"] | "";
  if (notes.length() > 0) {
    tft.setTextSize(1); tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawCentreString("What's new:", 120, 125, 1);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    int noteY = 140;
    int lineStart = 0, linesShown = 0;
    for (int i = 0; i <= (int)notes.length() && linesShown < 6; i++) {
      if (i == (int)notes.length() || notes[i] == '\n') {
        String line = notes.substring(lineStart, i);
        line.trim();
        if (line.length() > 0) {
          if (line.length() > 38) line = line.substring(0, 38) + "..";
          tft.drawString(line, 5, noteY, 1);
          noteY += 12;
          linesShown++;
        }
        lineStart = i + 1;
      }
    }
  }

  unsigned long timeout = millis() + 15000;
  bool accepted = false;
  while (millis() < timeout) {
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      int tx = mapTouchX(p.x);
      int ty = mapTouchY(p.y);
      if (ty >= 70 && ty <= 110) {
        if (tx >= 30 && tx <= 110) { accepted = true; break; }
        if (tx >= 130 && tx <= 210) { return; }
      }
      delay(200);
    }
    delay(50);
  }
  if (!accepted) return;

  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2); tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawCentreString("UPDATING", 120, 5, 1);
  tft.setTextSize(1); tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawCentreString("do not power off", 120, 25, 1);

  const int cell = 8, px = 7;
  const int xOff = (240 - SPRITE_W * cell) / 2;
  const int yOff = 45;
  uint16_t sleepOffset = pgm_read_word(&sprite_anim_offset[2]);
  static const uint16_t colors[] = {TFT_BLACK, TFT_ORANGE, TFT_BLACK, TFT_CYAN, TFT_DARKGREY, TFT_WHITE};
  uint8_t frameBuf[SPRITE_W * SPRITE_H];
  sprite_decode_frame(sleepOffset, frameBuf);
  for (int y = 0; y < SPRITE_H; y++)
    for (int x = 0; x < SPRITE_W; x++) {
      tft.fillRect(xOff + x * cell, yOff + y * cell, px, px, colors[frameBuf[y * SPRITE_W + x]]);
    }

  int barX = (240 - BANNER_W * 8) / 2;
  int barY = yOff + SPRITE_H * cell + 15;
  int numCells = BANNER_W;

  http.begin(client, assetUrl);
  http.addHeader("User-Agent", "OhMyClawd-ESP32");
  code = http.GET();
  if (code != 200) { http.end(); return; }

  int contentLen = http.getSize();
  WiFiClient* stream = http.getStreamPtr();
  if (!Update.begin(contentLen)) { http.end(); return; }

  uint8_t buf[1024];
  int written = 0;
  uint8_t sleepFrame = 0;
  uint8_t sleepCount = pgm_read_byte(&sprite_anim_count[2]);
  unsigned long lastFrame = 0;
  while (written < contentLen) {
    int avail = stream->available();
    if (avail) {
      int read = stream->readBytes(buf, min((int)sizeof(buf), avail));
      Update.write(buf, read);
      written += read;
      int pct = (written * 100) / contentLen;
      int filled = (pct * numCells) / 100;
      for (int i = 0; i < numCells; i++)
        tft.fillRect(barX + i * 8, barY, 7, 7, (i <= filled) ? TFT_ORANGE : TFT_DARKGREY);
      tft.fillRect(barX + numCells * 8 + 4, barY, 30, 8, TFT_BLACK);
      tft.setTextSize(1); tft.setTextColor(TFT_ORANGE, TFT_BLACK);
      tft.drawString(String(pct) + "%", barX + numCells * 8 + 4, barY, 1);
    }
    if (millis() - lastFrame > 150) {
      lastFrame = millis();
      sprite_decode_frame(sleepOffset + sleepFrame, frameBuf);
      for (int y = 0; y < SPRITE_H; y++)
        for (int x = 0; x < SPRITE_W; x++) {
          tft.fillRect(xOff + x * cell, yOff + y * cell, px, px, colors[frameBuf[y * SPRITE_W + x]]);
        }
      sleepFrame = (sleepFrame + 1) % sleepCount;
    }
    delay(1);
  }

  http.end();
  if (Update.end(true)) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2); tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.drawCentreString("DONE!", 120, 150, 1);
    tft.setTextSize(1); tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawCentreString("rebooting...", 120, 180, 1);
    delay(2000);
    ESP.restart();
  }
}
