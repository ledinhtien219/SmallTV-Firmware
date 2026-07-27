from pathlib import Path
import re
import runpy

SOURCE = Path("ESP8266_SmallTV_Pro_V8_0_15_Full_Audit_Fix.ino")

# Always build on the complete previous release.
runpy.run_path("tools/apply_v8_0_25_animated_mochi.py", run_name="__main__")
text = SOURCE.read_text(encoding="utf-8")

# Do not repaint the whole clock/crypto/Mochi screen after an API refresh.
# The Weather page still needs a redraw because all seven forecast cards may change.
helper_anchor = "void loop() {\n"
helper = r'''void refreshVisibleWeatherUi() {
  if (currentPage == 0) {
    // Clock page: only its weather/AQI footer changes.
    drawFooter(true);
  } else if (currentPage == 1) {
    // Forecast page contains weather data across the full content area.
    forcePageRedraw = true;
  }
}

void loop() {
'''
if helper_anchor not in text:
    raise RuntimeError("loop anchor not found")
text = text.replace(helper_anchor, helper, 1)

old_combined = '''    fetchWeather();
    fetchAirQuality();
    forcePageRedraw = true;
'''
new_combined = '''    fetchWeather();
    fetchAirQuality();
    refreshVisibleWeatherUi();
'''
if old_combined not in text:
    raise RuntimeError("combined weather refresh block not found")
text = text.replace(old_combined, new_combined, 1)

# Replace the two periodic single-API redraws.
text, weather_count = re.subn(
    r'(lastWeatherUpdate = now;\n\s*fetchWeather\(\);\n)\s*forcePageRedraw = true;',
    r'\1    refreshVisibleWeatherUi();',
    text,
    count=1,
)
text, aqi_count = re.subn(
    r'(lastAirQualityUpdate = now;\n\s*fetchAirQuality\(\);\n)\s*forcePageRedraw = true;',
    r'\1    refreshVisibleWeatherUi();',
    text,
    count=1,
)
if weather_count != 1 or aqi_count != 1:
    raise RuntimeError(f"periodic refresh blocks not found: weather={weather_count}, aqi={aqi_count}")

# Print an unambiguous boot identity so serial logs prove which image is running.
serial_anchor = "  Serial.begin(115200);\n  delay(300);\n"
serial_new = '''  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.print("SmallTV firmware V");
  Serial.println(FW_VERSION);
  Serial.print("Build: ");
  Serial.println(FW_BUILD);
  Serial.print("Flash size: ");
  Serial.println(ESP.getFlashChipRealSize());
  Serial.print("Sketch size/free OTA: ");
  Serial.print(ESP.getSketchSize());
  Serial.print(" / ");
  Serial.println(ESP.getFreeSketchSpace());
'''
if serial_anchor not in text:
    raise RuntimeError("setup serial anchor not found")
text = text.replace(serial_anchor, serial_new, 1)

# Prevent browser/proxy caching from showing an old firmware version after OTA.
status_anchor = '  server.on("/api/status", HTTP_GET, []() {\n'
status_new = '''  server.on("/api/status", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
'''
if status_anchor not in text:
    raise RuntimeError("status route anchor not found")
text = text.replace(status_anchor, status_new, 1)

# Version bump.
text, version_count = re.subn(
    r'static const char\* FW_VERSION = "8\.0\.\d+";',
    'static const char* FW_VERSION = "8.0.26";',
    text,
    count=1,
)
if version_count != 1:
    raise RuntimeError("FW_VERSION not found")

checks = [
    'static const char* FW_VERSION = "8.0.26";',
    'void refreshVisibleWeatherUi()',
    'drawFooter(true);',
    'SmallTV firmware V',
    'ESP.getFreeSketchSpace()',
    'Cache-Control", "no-store, no-cache, must-revalidate, max-age=0',
]
missing = [item for item in checks if item not in text]
if missing:
    raise RuntimeError("V8.0.26 verification failed: " + ", ".join(missing))

SOURCE.write_text(text, encoding="utf-8")
print(f"Applied V8.0.26 OTA diagnostics and partial weather refresh ({len(text)} bytes)")
