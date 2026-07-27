from pathlib import Path
import re
import runpy

SOURCE = Path("ESP8266_SmallTV_Pro_V8_0_15_Full_Audit_Fix.ino")

runpy.run_path("tools/apply_v8_0_24_smart_status.py", run_name="__main__")
text = SOURCE.read_text(encoding="utf-8")

# 20 FPS is smooth enough for the ST7789 while leaving time for Wi-Fi/web work.
text, count = re.subn(
    r"const unsigned long MOCHI_FRAME_INTERVAL_MS = \d+UL;",
    "const unsigned long MOCHI_FRAME_INTERVAL_MS = 50UL;",
    text,
    count=1,
)
if count != 1:
    raise RuntimeError("Mochi frame interval not found")

state_anchor = "bool lastMochiBlink = false;\n"
state_extra = """bool lastMochiBlink = false;
int8_t lastMochiEyeOffset = 99;
uint8_t lastMochiMouthPhase = 255;
"""
if state_anchor not in text:
    raise RuntimeError("Mochi state anchor not found")
text = text.replace(state_anchor, state_extra, 1)

start = text.index("void drawMochiFaceFrame(uint8_t expression, uint8_t frame) {")
end = text.index("\nvoid drawMochiPage() {", start)
new_function = r'''void drawMochiFaceFrame(uint8_t expression, uint8_t frame) {
  const uint16_t body = RGB565(245, 244, 239);
  const uint16_t blush = RGB565(255, 155, 170);
  const int eyeY = 120;
  const int mouthY = 158;

  // Natural timing: short double blink roughly every 4.5 seconds.
  uint8_t blinkPhase = frame % 92;
  bool blink = (blinkPhase == 78 || blinkPhase == 79 ||
                blinkPhase == 83 || blinkPhase == 84);

  // Eyes gently look left, centre and right without moving every frame.
  uint8_t gazePhase = (frame / 18) % 8;
  int8_t eyeOffset = 0;
  if (gazePhase == 2 || gazePhase == 3) eyeOffset = -3;
  else if (gazePhase == 6) eyeOffset = 3;

  bool firstFrame = (frame == 0);
  bool eyesChanged = firstFrame || blink != lastMochiBlink ||
                     eyeOffset != lastMochiEyeOffset;
  if (eyesChanged) {
    fillRect(70, 102, 100, 34, body);
    if (blink && expression != 3 && expression != 4) {
      fillRoundRect(77, eyeY, 20, 3, 1, COL_LINE);
      fillRoundRect(141, eyeY, 20, 3, 1, COL_LINE);
    } else {
      drawMochiEye(87 + eyeOffset, eyeY, expression);
      drawMochiEye(151 + eyeOffset, eyeY, expression);
    }
  }

  // Animate the mouth at a slow 4-step cadence instead of redrawing constantly.
  uint8_t mouthPhase = (frame / 5) % 4;
  if (firstFrame || mouthPhase != lastMochiMouthPhase) {
    fillRect(98, 145, 44, 36, body);
    if (expression == 1 || expression == 6) {
      int lift = (mouthPhase == 1 || mouthPhase == 2) ? 1 : 0;
      fillRect(105, mouthY - 7 - lift, 28, 3, COL_LINE);
      fillRect(109, mouthY - 4, 20, 3, COL_LINE);
      fillRect(114, mouthY - 1 + lift, 10, 3, COL_LINE);
      if (mouthPhase == 2) fillRect(116, mouthY + 3, 6, 2, RGB565(235, 95, 120));
    } else if (expression == 2) {
      int droop = (mouthPhase == 2) ? 1 : 0;
      fillRect(110, mouthY + 2 + droop, 18, 3, COL_LINE);
      fillRect(106, mouthY - 1 + droop, 4, 3, COL_LINE);
      fillRect(128, mouthY - 1 + droop, 4, 3, COL_LINE);
    } else if (expression == 3) {
      drawText(108, mouthY - 7, "Z", COL_ACCENT, body, 2);
    } else if (expression == 4) {
      int r = (mouthPhase == 1 || mouthPhase == 2) ? 10 : 8;
      fillCircle(119, mouthY, r, COL_LINE);
      fillCircle(119, mouthY - 1, r / 2, body);
    } else if (expression == 5) {
      fillRect(107, mouthY + ((mouthPhase == 2) ? 2 : 1), 24, 4, COL_LINE);
      fillRect(76, eyeY - 10, 22, 3, COL_LINE);
      fillRect(140, eyeY - 10, 22, 3, COL_LINE);
    }
  }

  // Soft cheek pulse gives the character a breathing/alive feeling.
  if (expression == 1 || expression == 6) {
    uint8_t cheekR = ((frame / 8) % 4 == 1) ? 6 : 5;
    fillRect(61, 137, 20, 16, body);
    fillRect(158, 137, 20, 16, body);
    fillCircle(71, 145, cheekR, blush);
    fillCircle(168, 145, cheekR, blush);
  }

  // Expression-specific particles are confined to small dirty rectangles.
  if (expression == 2) {
    fillRect(145, 126, 18, 42, body);
    uint8_t tearPhase = frame % 36;
    fillCircle(153, 130 + tearPhase, 3, RGB565(68, 162, 255));
  } else if (expression == 3) {
    fillRect(146, 48, 50, 48, COL_BG);
    int phase = frame % 40;
    drawText(151 + phase / 4, 88 - phase, "Z", COL_ACCENT, COL_BG, 1);
  } else if (expression == 6) {
    fillRect(42, 45, 34, 48, COL_BG);
    fillRect(171, 50, 34, 52, COL_BG);
    int phase = frame % 34;
    int heartY = 82 - phase;
    fillCircle(54, heartY, 5, RGB565(255, 88, 130));
    fillCircle(61, heartY, 5, RGB565(255, 88, 130));
    fillRect(52, heartY, 12, 7, RGB565(255, 88, 130));
    fillCircle(183, heartY + 12, 4, RGB565(255, 88, 130));
    fillCircle(189, heartY + 12, 4, RGB565(255, 88, 130));
    fillRect(181, heartY + 12, 10, 6, RGB565(255, 88, 130));
  } else if (expression == 1) {
    fillRect(16, 58, 26, 76, COL_BG);
    fillRect(198, 52, 29, 88, COL_BG);
    uint8_t phase = frame % 40;
    if (phase < 20) {
      drawText(25, 88, "*", COL_TEMP, COL_BG, 2);
      drawText(202, 74, "*", COL_ACCENT, COL_BG, 2);
    } else {
      drawText(23, 77, "*", COL_ACCENT, COL_BG, 1);
      drawText(205, 96, "*", COL_TEMP, COL_BG, 1);
    }
  }

  lastMochiBlink = blink;
  lastMochiEyeOffset = eyeOffset;
  lastMochiMouthPhase = mouthPhase;
}
'''
text = text[:start] + new_function + text[end:]

# Reset all animation caches whenever a new Mochi page/expression is selected.
text = text.replace(
    "    lastMochiBlink = false;\n    forcePageRedraw = true;",
    "    lastMochiBlink = false;\n    lastMochiEyeOffset = 99;\n    lastMochiMouthPhase = 255;\n    forcePageRedraw = true;",
    1,
)

text, count = re.subn(
    r'static const char\* FW_VERSION = "8\.0\.\d+";',
    'static const char* FW_VERSION = "8.0.25";',
    text,
    count=1,
)
if count != 1:
    raise RuntimeError("FW_VERSION not found")

checks = [
    'static const char* FW_VERSION = "8.0.25";',
    'MOCHI_FRAME_INTERVAL_MS = 50UL',
    'lastMochiEyeOffset',
    'lastMochiMouthPhase',
    'Natural timing: short double blink',
    'Soft cheek pulse',
]
missing = [item for item in checks if item not in text]
if missing:
    raise RuntimeError("V8.0.25 verification failed: " + ", ".join(missing))

SOURCE.write_text(text, encoding="utf-8")
print(f"Applied V8.0.25 animated Mochi upgrade ({len(text)} bytes)")
