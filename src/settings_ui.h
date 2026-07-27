// src/settings_ui.h
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "display_pm.h"

extern bool dynamicSprite;
extern uint8_t displayRotation;
extern bool displayInverted;
extern TFT_eSPI tft;

namespace settings_ui {

static bool          needsFullRedraw   = true;
static bool          rowsDirty         = true;
static unsigned long resetHoldStartMs  = 0;
static bool          resetActive       = false;
static int           resetFilledPx     = 0;
static bool          dirty             = false; // unsaved changes

// Pending values (edited but not yet saved)
static uint8_t  pBri, pQhStart, pQhEnd, pQhMode, pCycSec, pRotation;
static bool     pDynSprite, pInverted;

// Layout constants.
static constexpr int HEADER_Y_TOP    = 2;
static constexpr int SUBHEADER_Y     = 20;
static constexpr int ROW0_Y          = 35;
static constexpr int ROW_H           = 28;
static constexpr int LABEL_X         = 15;
static constexpr int VALUE_X         = 225;
static constexpr int RULE_INDENT     = 5;
static constexpr int NUM_ROWS        = 10;

inline int rowY(int idx) { return ROW0_Y + idx * ROW_H; }

inline int rowFromY(int y) {
  if (y < ROW0_Y) return -1;
  int idx = (y - ROW0_Y) / ROW_H;
  if (idx >= NUM_ROWS) return -1;
  return idx;
}

inline String briLabel() { return String(pBri) + "%"; }
inline const char* qhmLabel() {
  switch (pQhMode) { case 0: return "OFF"; case 1: return "DIM"; default: return "SLEEP"; }
}
inline String cycLabel() {
  if (pCycSec == 0) return "OFF";
  return "< " + String(pCycSec) + "s >";
}
inline const char* sprModeLabel() { return pDynSprite ? "DYNAMIC" : "FREE"; }
inline const char* orientLabel() { return pRotation == 0 ? "NORMAL" : "FLIPPED"; }
inline const char* invertLabel() { return pInverted ? "ON" : "OFF"; }

inline String qhRangeStr() {
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:00 -> %02d:00", display_pm::qhStart, display_pm::qhEnd);
  return String(buf);
}

inline void drawRow(TFT_eSPI& tft, int idx, const char* label, const String& value, bool highlighted) {
  int y = rowY(idx);
  uint16_t bg = highlighted ? TFT_ORANGE : TFT_BLACK;
  uint16_t labelFg = highlighted ? TFT_BLACK : TFT_DARKGREY;
  uint16_t valueFg = highlighted ? TFT_BLACK : TFT_ORANGE;
  tft.fillRect(0, y, 240, ROW_H - 1, bg);
  tft.setTextSize(1);
  tft.setTextColor(labelFg, bg);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(label, LABEL_X, y + 8, 1);
  tft.setTextColor(valueFg, bg);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(value, VALUE_X, y + 8, 1);
  tft.drawFastHLine(RULE_INDENT, y + ROW_H - 1, 240 - 2 * RULE_INDENT, TFT_DARKGREY);
}

inline void flashRow(TFT_eSPI& tft, int idx, const char* label, const String& value) {
  drawRow(tft, idx, label, value, true);
  delay(120);
}

inline void save();

inline void handleTap(TFT_eSPI& tft, int touchY, int touchX) {
  int row = rowFromY(touchY);
  if (row < 0) return;

  if (row != 8) { resetActive = false; resetFilledPx = 0; }

  switch (row) {
    case 0: case 1: case 2: case 4:
      break; // handled by hold-drag slider
    case 3:
      pQhMode = (pQhMode + 1) % 3;
      dirty = true;
      flashRow(tft, 3, "QUIET MODE", String(qhmLabel()));
      break;
    case 5:
      pDynSprite = !pDynSprite;
      dirty = true;
      flashRow(tft, 5, "SPRITE MODE", String(sprModeLabel()));
      break;
    case 6:
      pRotation = (pRotation == 0) ? 2 : 0;
      dirty = true;
      flashRow(tft, 6, "ORIENTATION", String(orientLabel()));
      break;
    case 7:
      pInverted = !pInverted;
      dirty = true;
      flashRow(tft, 7, "INVERT COLORS", String(invertLabel()));
      break;
    case 8:
      break; // hold-only reset
    case 9:
      if (dirty) { flashRow(tft, 9, "SAVE", "SAVED!"); save(); }
      break;
  }
  needsFullRedraw = true;
  rowsDirty = true;
}

static constexpr unsigned long RESET_HOLD_MS = 3000UL;

// Slider state
static int   sliderRow    = -1;
static int   sliderStartX = -1;
static int   sliderLastVal = 0;
static unsigned long sliderHoldStart = 0;
static bool  sliderEngaged = false;
static constexpr unsigned long SLIDER_HOLD_MS = 500UL;

inline int sliderMaxForRow(int row) {
  if (row == 0) return 100;  // percentage
  if (row == 1 || row == 2) return 23;
  if (row == 4) return 250;
  return 0;
}

inline int sliderValForRow(int row) {
  if (row == 0) return pBri;
  if (row == 1) return pQhStart;
  if (row == 2) return pQhEnd;
  if (row == 4) return pCycSec;
  return 0;
}

inline void sliderSetVal(int row, int val) {
  if (row == 0) {
    if (val < 10) val = 10;
    pBri = val;
    display_pm::previewActive = true;
    display_pm::targetDuty = (val * 255) / 100;
    dirty = true;
  }
  else if (row == 1) { pQhStart = val; dirty = true; }
  else if (row == 2) { pQhEnd = val; dirty = true; }
  else if (row == 4) { pCycSec = val; dirty = true; }
}

inline int sliderStep(int row) { return (row == 4) ? 5 : 1; }

inline void drawSliderBar(TFT_eSPI& tft, int row) {
  int y = rowY(row);
  int maxVal = sliderMaxForRow(row);
  int val = sliderValForRow(row);
  int filled = (maxVal > 0) ? (val * 240) / maxVal : 0;
  tft.fillRect(0, y, 240, ROW_H - 1, TFT_BLACK);
  tft.fillRect(0, y, filled, ROW_H - 1, TFT_ORANGE);
  tft.setTextSize(1);
  tft.setTextColor(TFT_BLACK, TFT_ORANGE);
  tft.setTextDatum(TL_DATUM);
  const char* rowName = (row == 0) ? "BRIGHTNESS" : (row == 1) ? "QUIET START" : (row == 2) ? "QUIET END" : "AUTO-CYCLE";
  tft.drawString(rowName, LABEL_X, y + 8, 1);
  tft.setTextDatum(TR_DATUM);
  String label;
  if (row == 0) { label = String(val) + "%"; }
  else if (row == 4) label = (val == 0) ? "OFF" : String(val) + "s";
  else label = String(val) + ":00";
  tft.drawString(label, VALUE_X, y + 8, 1);
  tft.drawFastHLine(RULE_INDENT, y + ROW_H - 1, 240 - 2 * RULE_INDENT, TFT_DARKGREY);
  tft.setTextDatum(TL_DATUM);
}

inline void handleHoldTick(TFT_eSPI& tft, int touchY, int touchX, unsigned long elapsedMs) {
  // Once slider is engaged, lock to that row
  if (sliderEngaged && sliderRow >= 0) {
    int delta = touchX - sliderStartX;
    int maxVal = sliderMaxForRow(sliderRow);
    int step = sliderStep(sliderRow);
    int valDelta = (delta * maxVal) / 200;
    valDelta = (valDelta / step) * step;
    int newVal = sliderLastVal + valDelta;
    if (sliderRow == 1 || sliderRow == 2) { newVal = ((newVal % 24) + 24) % 24; }
    else if (sliderRow == 0) { if (newVal < 10) newVal = 10; if (newVal > 100) newVal = 100; }
    else { if (newVal < 0) newVal = 0; if (newVal > 250) newVal = 250; }
    if (newVal != sliderValForRow(sliderRow)) {
      sliderSetVal(sliderRow, newVal);
      drawSliderBar(tft, sliderRow);
    }
    return;
  }

  if (resetActive) {
    unsigned long held = millis() - resetHoldStartMs;
    if (held > RESET_HOLD_MS) held = RESET_HOLD_MS;
    int filled = (int)((held * 240UL) / RESET_HOLD_MS);
    if (filled != resetFilledPx) {
      resetFilledPx = filled;
      int y = rowY(7);
      tft.fillRect(0, y, 240, ROW_H - 1, TFT_BLACK);
      tft.fillRect(0, y, resetFilledPx, ROW_H - 1, TFT_ORANGE);
      tft.setTextSize(1);
      tft.setTextColor(TFT_BLACK, TFT_ORANGE);
      tft.setTextDatum(TL_DATUM);
      tft.drawString("RESET", LABEL_X, y + 8, 1);
      tft.setTextDatum(TR_DATUM);
      tft.drawString("hold...", VALUE_X, y + 8, 1);
      tft.drawFastHLine(RULE_INDENT, y + ROW_H - 1, 240 - 2 * RULE_INDENT, TFT_DARKGREY);
    }
    return;
  }

  // Waiting for 2s hold threshold
  int row = rowFromY(touchY);
  if (row == 0 || row == 1 || row == 2 || row == 4) {
    if (sliderRow != row) {
      sliderRow = row;
      sliderHoldStart = millis();
      sliderEngaged = false;
    } else if (!sliderEngaged && (millis() - sliderHoldStart >= SLIDER_HOLD_MS)) {
      sliderEngaged = true;
      sliderStartX = touchX;
      sliderLastVal = sliderValForRow(row);
      drawSliderBar(tft, row);
    }
  } else if (row == 8) {
    resetActive = true;
    resetHoldStartMs = millis() - elapsedMs;
    sliderRow = -1;
  } else {
    sliderRow = -1;
  }
}

inline bool consumeResetIfTriggered() {
  if (resetActive && (millis() - resetHoldStartMs) >= RESET_HOLD_MS) {
    resetActive = false;
    resetFilledPx = 0;
    return true;
  }
  return false;
}

inline void cancelHold() {
  if (resetActive) {
    resetActive = false;
    resetFilledPx = 0;
    needsFullRedraw = true;
    rowsDirty = true;
  }
  if (sliderRow >= 0) {
    sliderRow = -1;
    sliderEngaged = false;
    needsFullRedraw = true;
    rowsDirty = true;
  }
}

inline void render(TFT_eSPI& tft, bool fullRedraw);

inline void enter() {
  needsFullRedraw = true;
  rowsDirty = true;
  resetActive = false;
  resetFilledPx = 0;
  dirty = false;
  pBri = display_pm::briPct;
  pQhStart = display_pm::qhStart;
  pQhEnd = display_pm::qhEnd;
  pQhMode = display_pm::qhMode;
  pCycSec = display_pm::cycSec;
  pDynSprite = dynamicSprite;
  pRotation = displayRotation;
  pInverted = displayInverted;
}

inline void exit() {
  resetActive = false;
  sliderRow = -1;
  sliderEngaged = false;
  if (dirty) {
    display_pm::previewActive = false;
    dirty = false;
  }
}

inline void save() {
  display_pm::setBrightness(pBri);
  display_pm::setQuietHours(pQhStart, pQhEnd, pQhMode);
  display_pm::setCycle(pCycSec);
  dynamicSprite = pDynSprite;
  displayRotation = pRotation;
  displayInverted = pInverted;
  Preferences p; p.begin("ohmyclawd", false);
  p.putBool("dyn_spr", pDynSprite);
  p.putUChar("rot", displayRotation);
  p.putBool("inverted", displayInverted);
  p.end();
  tft.setRotation(displayRotation);
  tft.invertDisplay(displayInverted);
  display_pm::previewActive = false;
  dirty = false;
}

inline void render(TFT_eSPI& tft, bool fullRedraw) {
  if (fullRedraw || needsFullRedraw) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("SETTINGS", 120, HEADER_Y_TOP + 10, 1);
    tft.setTextSize(1);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("hold to adjust *", 120, SUBHEADER_Y + 4, 1);
    tft.setTextDatum(TL_DATUM);
    needsFullRedraw = false;
  }

  if (rowsDirty || fullRedraw) {
    char buf[16];
    drawRow(tft, 0, "BRIGHTNESS *", briLabel(), false);
    snprintf(buf, sizeof(buf), "%02d:00", pQhStart);
    drawRow(tft, 1, "QUIET START *", String(buf), false);
    snprintf(buf, sizeof(buf), "%02d:00", pQhEnd);
    drawRow(tft, 2, "QUIET END *", String(buf), false);
    drawRow(tft, 3, "QUIET MODE", String(qhmLabel()), false);
    drawRow(tft, 4, "AUTO-CYCLE *", cycLabel(), false);
    drawRow(tft, 5, "SPRITE MODE", String(sprModeLabel()), false);
    drawRow(tft, 6, "ORIENTATION", String(orientLabel()), false);
    drawRow(tft, 7, "INVERT COLORS", String(invertLabel()), false);

    if (resetActive) {
      int y = rowY(8);
      tft.fillRect(0, y, 240, ROW_H - 1, TFT_BLACK);
      tft.fillRect(0, y, resetFilledPx, ROW_H - 1, TFT_ORANGE);
      tft.setTextSize(1);
      tft.setTextColor(TFT_BLACK, TFT_ORANGE);
      tft.setTextDatum(TL_DATUM);
      tft.drawString("RESET", LABEL_X, y + 8, 1);
      tft.setTextDatum(TR_DATUM);
      tft.drawString("hold...", VALUE_X, y + 8, 1);
      tft.drawFastHLine(RULE_INDENT, y + ROW_H - 1, 240 - 2 * RULE_INDENT, TFT_DARKGREY);
    } else {
      drawRow(tft, 8, "RESET", "hold 3s", false);
    }

    // SAVE button — highlighted orange when dirty
    int sy = rowY(9);
    if (dirty) {
      tft.fillRect(0, sy, 240, ROW_H - 1, TFT_ORANGE);
      tft.setTextSize(1); tft.setTextColor(TFT_BLACK, TFT_ORANGE);
      tft.setTextDatum(MC_DATUM);
      tft.drawString("SAVE", 120, sy + ROW_H / 2 - 1, 1);
      tft.setTextDatum(TL_DATUM);
    } else {
      tft.fillRect(0, sy, 240, ROW_H - 1, TFT_BLACK);
      tft.setTextSize(1); tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
      tft.setTextDatum(MC_DATUM);
      tft.drawString("SAVE", 120, sy + ROW_H / 2 - 1, 1);
      tft.setTextDatum(TL_DATUM);
    }

    rowsDirty = false;
  }
}

} // namespace settings_ui
