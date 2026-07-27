from pathlib import Path
import re
import runpy

runpy.run_path("tools/apply_v8_0_20_clock_lunar_and_form_lock.py", run_name="__main__")

SOURCE = Path("ESP8266_SmallTV_Pro_V8_0_15_Full_Audit_Fix.ino")
text = SOURCE.read_text(encoding="utf-8")

text = re.sub(
    r'static const char\* FW_VERSION = "8\.0\.\d+";',
    'static const char* FW_VERSION = "8.0.21";',
    text,
    count=1,
)

# Remove the decorative three-dot activity pulse. It was visually confused with
# rain beside the weather icon and also caused unnecessary periodic LCD writes.
text = text.replace("\n  drawPageActivityPulse();\n", "\n")

start = text.find("void drawWeatherIcon(int code, int x, int y) {")
end = text.find("\n}\n\n// ---------------- Settings storage ----------------", start)
if start < 0 or end < 0:
    raise RuntimeError("drawWeatherIcon block not found")

new_icon = r'''void drawRainDrops(int x, int y, bool showers) {
  uint16_t rain = RGB565(58, 153, 255);
  if (showers) {
    fillRect(x - 13, y, 3, 8, rain);
    fillRect(x - 1, y + 3, 3, 8, rain);
    fillRect(x + 11, y, 3, 8, rain);
  } else {
    fillRect(x - 12, y, 2, 7, rain);
    fillRect(x - 1, y + 2, 2, 7, rain);
    fillRect(x + 10, y, 2, 7, rain);
  }
}

void drawWeatherIcon(int code, int x, int y) {
  // Clear only the icon cell. Every weather state is static and unambiguous.
  fillRect(x - 25, y - 22, 50, 46, COL_CARD);

  const uint16_t cloudWhite = RGB565(225, 236, 243);
  const uint16_t cloudGrey = RGB565(169, 188, 201);
  const uint16_t fogGrey = RGB565(126, 151, 168);

  if (code == 0) {
    drawSunIcon(x, y);
    return;
  }

  if (code == 1 || code == 2) {
    // Partly cloudy: sun remains visible behind a compact bright cloud.
    drawSunIcon(x - 9, y - 7);
    drawCloudIcon(x + 5, y + 5, cloudWhite);
    return;
  }

  if (code == 3) {
    // Overcast: cloud only. Never show blue rain marks for plain cloud cover.
    drawCloudIcon(x, y, cloudGrey);
    return;
  }

  if (code == 45 || code == 48) {
    drawCloudIcon(x, y - 6, cloudGrey);
    fillRoundRect(x - 18, y + 8, 36, 2, 1, fogGrey);
    fillRoundRect(x - 13, y + 14, 28, 2, 1, fogGrey);
    return;
  }

  if (code >= 95) {
    drawCloudIcon(x, y - 8, cloudGrey);
    uint16_t bolt = RGB565(255, 191, 55);
    fillRect(x - 2, y + 2, 7, 5, bolt);
    fillRect(x - 6, y + 7, 7, 5, bolt);
    fillRect(x - 1, y + 12, 4, 7, bolt);
    return;
  }

  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    drawCloudIcon(x, y - 8, cloudWhite);
    drawRainDrops(x, y + 7, code >= 80);
    return;
  }

  if (code >= 71 && code <= 77) {
    drawCloudIcon(x, y - 7, cloudWhite);
    uint16_t snow = RGB565(183, 221, 255);
    drawText(x - 14, y + 8, "*", snow, COL_CARD, 1);
    drawText(x + 6, y + 11, "*", snow, COL_CARD, 1);
    return;
  }

  drawCloudIcon(x, y, cloudGrey);
}'''

text = text[:start] + new_icon + text[end + 2:]

required = (
    'FW_VERSION = "8.0.21"',
    'void drawRainDrops(',
    'Overcast: cloud only',
    'drawPageActivityPulse();',
)
if required[0] not in text or required[1] not in text or required[2] not in text:
    raise RuntimeError("V8.0.21 weather icon verification failed")
if required[3] in text:
    raise RuntimeError("Activity pulse call still present")

SOURCE.write_text(text, encoding="utf-8")
print("Applied V8.0.21 clean weather icons and removed activity dots")
