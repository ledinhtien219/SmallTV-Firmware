from pathlib import Path

SOURCE = Path("ESP8266_SmallTV_Pro_V8_0_15_Full_Audit_Fix.ino")
text = SOURCE.read_text(encoding="utf-8")


def replace_once(old: str, new: str) -> None:
    global text
    if old not in text:
        raise RuntimeError(f"Expected V8.0.16 fragment not found: {old[:140]!r}")
    text = text.replace(old, new, 1)


# Make the produced binary identify itself correctly.
replace_once('static const char* FW_VERSION = "8.0.16";',
             'static const char* FW_VERSION = "8.0.17";')

# A 20 FPS schedule overloaded the raw-SPI renderer whenever network work ran.
# 80 ms gives a stable 12.5 FPS while the V8.0.16 renderer only redraws effects.
replace_once("const unsigned long MOCHI_FRAME_INTERVAL_MS = 50UL;",
             "const unsigned long MOCHI_FRAME_INTERVAL_MS = 80UL;")

# The original page refreshed every five seconds. Typing a new city did not mark
# the form dirty, so refresh() could restore the old city and save old coordinates.
replace_once(
    "const $=id=>document.getElementById(id);let weatherDirty=false;let locationValid=false;",
    "const $=id=>document.getElementById(id);let weatherDirty=false;"
    "let locationValid=false;let typedLocationChanged=false;"
)
replace_once(
    "resolve:weatherDirty?1:0,utc:$('utc').value,",
    "resolve:(weatherDirty||typedLocationChanged)?1:0,utc:$('utc').value,"
)
replace_once(
    "weatherDirty=false;locationValid=true;\n   toast('Đã lưu vị trí mới');",
    "weatherDirty=false;locationValid=true;typedLocationChanged=false;\n"
    "   toast('Đã lưu vị trí mới');"
)
replace_once(
    "const savedTab=localStorage.getItem('smalltvTab')||'home';",
    "$('city').addEventListener('input',()=>{weatherDirty=true;locationValid=false;"
    "typedLocationChanged=true;$('locationResult').textContent='Địa điểm đã thay đổi — bấm Lưu cấu hình'});\n"
    "const savedTab=localStorage.getItem('smalltvTab')||'home';"
)

# Remove any old installed PWA worker and force API requests to bypass cache.
replace_once(
    "async function req(url,opt={}){const r=await fetch(url,opt);",
    "async function req(url,opt={}){opt.cache='no-store';const r=await fetch(url,opt);"
)
replace_once(
    "if('serviceWorker' in navigator){\n navigator.serviceWorker.register('/sw.js').catch(()=>{})\n}",
    "if('serviceWorker' in navigator){\n navigator.serviceWorker.getRegistrations().then(list=>list.forEach(r=>r.unregister())).catch(()=>{})\n}"
)

# A manual Mochi selection must draw frame zero immediately. Without this,
# entering the page with a nonzero animation counter could leave eyes/mouth blank.
replace_once(
    "    lastMochiBlink = false;\n    forcePageRedraw = true;",
    "    lastMochiBlink = false;\n    mochiAnimationFrame = 0;\n    forcePageRedraw = true;"
)
replace_once(
    "  if (!mochiScreenReady || lastRenderedMochiExpression != expression) {\n    drawMochiStaticShell(expression);\n  }\n\n  mochiAnimationFrame++;",
    "  if (!mochiScreenReady || lastRenderedMochiExpression != expression) {\n"
    "    drawMochiStaticShell(expression);\n"
    "    mochiAnimationFrame = 0;\n"
    "    drawMochiFaceFrame(expression, 0);\n"
    "    return;\n"
    "  }\n\n  mochiAnimationFrame++;"
)

# Synchronous HTTP calls can block the loop for seconds. Defer weather, AQI and
# crypto polling while Mochi is visible so animation timing remains consistent.
replace_once(
    "  if (weatherRefreshRequested) {",
    "  if (weatherRefreshRequested && currentPage != 2) {"
)
replace_once(
    "  if (now - lastWeatherUpdate >= (unsigned long)cfg.weatherIntervalMinutes * 60000UL) {",
    "  if (currentPage != 2 && now - lastWeatherUpdate >= (unsigned long)cfg.weatherIntervalMinutes * 60000UL) {"
)
replace_once(
    "  if (now - lastAirQualityUpdate >= (unsigned long)cfg.aqiIntervalMinutes * 60000UL) {",
    "  if (currentPage != 2 && now - lastAirQualityUpdate >= (unsigned long)cfg.aqiIntervalMinutes * 60000UL) {"
)
replace_once(
    "  if (lastCryptoUpdate == 0 ||\n      now - lastCryptoUpdate >= (unsigned long)cfg.cryptoIntervalSeconds * 1000UL) {",
    "  if (currentPage != 2 && (lastCryptoUpdate == 0 ||\n"
    "      now - lastCryptoUpdate >= (unsigned long)cfg.cryptoIntervalSeconds * 1000UL)) {"
)

for required in (
    'FW_VERSION = "8.0.17"',
    "typedLocationChanged=true",
    "opt.cache='no-store'",
    "weatherRefreshRequested && currentPage != 2",
    "currentPage != 2 && (lastCryptoUpdate == 0",
    "MOCHI_FRAME_INTERVAL_MS = 80UL",
):
    if required not in text:
        raise RuntimeError(f"V8.0.17 verification failed: {required}")

SOURCE.write_text(text, encoding="utf-8")
print("Applied and verified SmallTV V8.0.17 final fixes")
