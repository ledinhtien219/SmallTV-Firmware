from pathlib import Path
import re
import runpy

SOURCE = Path("ESP8266_SmallTV_Pro_V8_0_15_Full_Audit_Fix.ino")

# Materialize V8.0.22 first. The upstream patch is idempotent.
runpy.run_path("tools/apply_v8_0_22_split_calendar.py", run_name="__main__")
text = SOURCE.read_text(encoding="utf-8")

old_open = '''String mobileAppPage() {
  String h;
  h.reserve(22000);
  h += F(R"HTML(
'''
new_open = '''const char* mobileAppPage() {
  // Keep the large dashboard in flash. A 20+ KB dynamic String requires one
  // contiguous heap block and can make route "/" return a blank page on ESP8266.
  static const char h[] PROGMEM = R"HTML(
'''

if old_open in text:
    text = text.replace(old_open, new_open, 1)
elif 'const char* mobileAppPage() {' not in text:
    raise RuntimeError("mobileAppPage opening block not found")

old_close = ''')HTML");
  return h;
}

void handleGeocodeRequest() {
'''
new_close = ''')HTML";
  return h;
}

void handleGeocodeRequest() {
'''
if old_close in text:
    text = text.replace(old_close, new_close, 1)
elif ')HTML";\n  return h;\n}\n\nvoid handleGeocodeRequest() {' not in text:
    raise RuntimeError("mobileAppPage closing block not found")

old_route = '    server.send(200, "text/html; charset=utf-8", mobileAppPage());'
new_route = '    server.send_P(200, PSTR("text/html; charset=utf-8"), mobileAppPage());'
if old_route in text:
    text = text.replace(old_route, new_route, 1)
elif new_route not in text:
    raise RuntimeError("dashboard route not found")

text = text.replace(
    "ESP8266 SmallTV Pro V8.0.15 - Full Audit Fix",
    "ESP8266 SmallTV Pro V8.0.23 - Dashboard Flash Fix",
    1,
)
text, count = re.subn(
    r'static const char\* FW_VERSION = "8\.0\.\d+";',
    'static const char* FW_VERSION = "8.0.23";',
    text,
    count=1,
)
if count != 1:
    raise RuntimeError("FW_VERSION declaration not found")

# Static audit for the historical raw-string/linker corruption.
if text.count('R"HTML(') != text.count(')HTML"'):
    raise RuntimeError("unbalanced raw HTML strings")
checks = {
    'const char* mobileAppPage()': 1,
    'void handleGeocodeRequest() {': 1,
    'void sendJsonOk() {': 1,
    'String otaUpdatePage() {': 1,
    'server.send_P(200, PSTR("text/html; charset=utf-8"), mobileAppPage());': 1,
}
for marker, expected in checks.items():
    actual = text.count(marker)
    if actual != expected:
        raise RuntimeError(f"invalid marker count for {marker!r}: {actual}")
if 'String mobileAppPage() {' in text:
    raise RuntimeError("dynamic dashboard String still present")

SOURCE.write_text(text, encoding="utf-8")
print(f"Applied V8.0.23 dashboard flash fix ({len(text)} bytes)")
