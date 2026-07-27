from pathlib import Path
import re

SOURCE = Path("ESP8266_SmallTV_Pro_V8_0_15_Full_Audit_Fix.ino")
text = SOURCE.read_text(encoding="utf-8")


def replace_once(old: str, new: str) -> None:
    global text
    if old in text:
        text = text.replace(old, new, 1)
        return
    if new in text:
        return
    raise RuntimeError(f"Expected source fragment not found: {old[:120]!r}")


# Version and a faster but still conservative LCD SPI clock.
text = re.sub(r'static const char\* FW_VERSION = "8\.0\.\d+";',
              'static const char* FW_VERSION = "8.0.18";', text, count=1)
text = text.replace('SPI.setFrequency(10000000);', 'SPI.setFrequency(16000000);')

replace_once(
    'void drawOtaErrorScreen(int errorCode);\n',
    'void drawOtaErrorScreen(int errorCode);\n'
    'void animatePageTransition();\n'
    'void drawPageActivityPulse();\n'
)

replace_once(
    'bool forcePageRedraw = true;\n',
    'bool forcePageRedraw = true;\n'
    'uint8_t lastRenderedPage = 255;\n'
    'unsigned long lastPageActivityFrame = 0;\n'
    'uint8_t pageActivityFrame = 0;\n'
)

marker = 'void drawPage(bool forceRedraw = false) {'
if 'void animatePageTransition() {' not in text:
    transition_code = r'''void animatePageTransition() {
  // A short scan-line wipe hides full-screen redraws without requiring a
  // 115 KB framebuffer, which is not practical on ESP8266.
  const uint8_t bands = 12;
  const int bandHeight = 10;
  for (uint8_t step = 0; step < bands; step++) {
    int topY = step * bandHeight;
    int bottomY = 240 - (step + 1) * bandHeight;
    fillRect(0, topY, 240, bandHeight, COL_BG);
    if (bottomY != topY) fillRect(0, bottomY, 240, bandHeight, COL_BG);
    delay(2);
    yield();
  }
}

void drawPageActivityPulse() {
  if (currentPage == 2 || otaInProgress || lastRenderedPage != currentPage) return;
  unsigned long now = millis();
  if (now - lastPageActivityFrame < 180UL) return;
  lastPageActivityFrame = now;
  pageActivityFrame = (pageActivityFrame + 1) % 3;

  // All non-Mochi pages share the dark top card in this area. Redrawing only
  // three tiny dots gives subtle motion and costs very little SPI bandwidth.
  const int y = 42;
  fillRect(205, 38, 25, 9, COL_CARD);
  for (uint8_t i = 0; i < 3; i++) {
    fillCircle(209 + i * 8, y, 2,
               i == pageActivityFrame ? COL_ACCENT : COL_LINE);
  }
}

'''
    text = text.replace(marker, transition_code + marker, 1)

# Transition only when the actual page changes, never during ordinary data refresh.
replace_once(
    '''void drawPage(bool forceRedraw = false) {
  if (!forceRedraw && !forcePageRedraw) return;

  if (currentPage != 2) mochiScreenReady = false;''',
    '''void drawPage(bool forceRedraw = false) {
  if (!forceRedraw && !forcePageRedraw) return;

  bool pageChanged = lastRenderedPage != currentPage;
  if (pageChanged && lastRenderedPage != 255) animatePageTransition();

  if (currentPage != 2) mochiScreenReady = false;'''
)

replace_once(
    '''  forcePageRedraw = false;
  yield();
}

void nextPage()''',
    '''  lastRenderedPage = currentPage;
  pageActivityFrame = 0;
  lastPageActivityFrame = millis();
  forcePageRedraw = false;
  yield();
}

void nextPage()'''
)

# Let the transition finish before any blocking HTTP refresh starts.
replace_once(
    '''  if (forcePageRedraw) {
    drawPage(true);
  }

  if (weatherRefreshRequested && currentPage != 2) {''',
    '''  if (forcePageRedraw) {
    drawPage(true);
  }

  drawPageActivityPulse();

  if (weatherRefreshRequested && currentPage != 2 &&
      now - lastPageChange > 600UL) {'''
)

# Scheduled network work also waits briefly after switching pages.
text = text.replace(
    'if (currentPage != 2 && now - lastWeatherUpdate >=',
    'if (currentPage != 2 && now - lastPageChange > 600UL && now - lastWeatherUpdate >=',
    1,
)
text = text.replace(
    'if (currentPage != 2 && now - lastAirQualityUpdate >=',
    'if (currentPage != 2 && now - lastPageChange > 600UL && now - lastAirQualityUpdate >=',
    1,
)
text = text.replace(
    'if (currentPage != 2 && (lastCryptoUpdate == 0 ||',
    'if (currentPage != 2 && now - lastPageChange > 600UL && (lastCryptoUpdate == 0 ||',
    1,
)

required = (
    'FW_VERSION = "8.0.18"',
    'SPI.setFrequency(16000000)',
    'void animatePageTransition()',
    'lastRenderedPage = currentPage',
    'drawPageActivityPulse();',
    'now - lastPageChange > 600UL',
)
for token in required:
    if token not in text:
        raise RuntimeError(f"V8.0.18 verification failed: {token}")

SOURCE.write_text(text, encoding="utf-8")
print("Applied and verified SmallTV V8.0.18 smooth page rendering")
