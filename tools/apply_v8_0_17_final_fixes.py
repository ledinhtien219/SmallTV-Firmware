from pathlib import Path
import re

SOURCE = Path("ESP8266_SmallTV_Pro_V8_0_15_Full_Audit_Fix.ino")
text = SOURCE.read_text(encoding="utf-8")


def sub_once(pattern: str, replacement: str, *, flags: int = 0, required: bool = True) -> None:
    global text
    updated, count = re.subn(pattern, replacement, text, count=1, flags=flags)
    if count:
        text = updated
    elif required:
        raise RuntimeError(f"Required patch target not found: {pattern[:140]!r}")


# Version and stable frame interval. Accept reruns without modifying twice.
text = text.replace('static const char* FW_VERSION = "8.0.16";',
                    'static const char* FW_VERSION = "8.0.17";')
text = text.replace('const unsigned long MOCHI_FRAME_INTERVAL_MS = 50UL;',
                    'const unsigned long MOCHI_FRAME_INTERVAL_MS = 80UL;')

# Track manual edits immediately so the five-second status refresh cannot restore
# the previous city or submit old coordinates.
if "typedLocationChanged" not in text:
    sub_once(
        r"const \$=id=>document\.getElementById\(id\);let weatherDirty=false;let locationValid=false;",
        "const $=id=>document.getElementById(id);let weatherDirty=false;"
        "let locationValid=false;let typedLocationChanged=false;",
    )

text = text.replace(
    "resolve:weatherDirty?1:0,utc:$('utc').value,",
    "resolve:(weatherDirty||typedLocationChanged)?1:0,utc:$('utc').value,",
)

if "typedLocationChanged=false" not in text:
    sub_once(
        r"weatherDirty=false;locationValid=true;\s*toast\('Đã lưu vị trí mới'\);",
        "weatherDirty=false;locationValid=true;typedLocationChanged=false;\n"
        "   toast('Đã lưu vị trí mới');",
    )

if "Địa điểm đã thay đổi — bấm Lưu cấu hình" not in text:
    sub_once(
        r"const savedTab=localStorage\.getItem\('smalltvTab'\)\|\|'home';",
        "$('city').addEventListener('input',()=>{weatherDirty=true;locationValid=false;"
        "typedLocationChanged=true;$('locationResult').textContent='Địa điểm đã thay đổi — bấm Lưu cấu hình'});\n"
        "const savedTab=localStorage.getItem('smalltvTab')||'home';",
    )

# Bypass browser cache and unregister any old PWA worker.
text = text.replace(
    "async function req(url,opt={}){const r=await fetch(url,opt);",
    "async function req(url,opt={}){opt.cache='no-store';const r=await fetch(url,opt);",
)
sub_once(
    r"if\('serviceWorker' in navigator\)\{\s*navigator\.serviceWorker\.(?:register\('/sw\.js'\)|getRegistrations\(\)\.then\(list=>list\.forEach\(r=>r\.unregister\(\)\)\))\.catch\(\(\)=>\{\}\)\s*\}",
    "if('serviceWorker' in navigator){\n navigator.serviceWorker.getRegistrations().then(list=>list.forEach(r=>r.unregister())).catch(()=>{})\n}",
    required=False,
)

# Manual selection already resets the frame in V8.0.16. Do not add duplicate
# assignments; only ensure the renderer draws frame zero after rebuilding shell.
sub_once(
    r"if \(!mochiScreenReady \|\| lastRenderedMochiExpression != expression\) \{\s*"
    r"drawMochiStaticShell\(expression\);\s*\}\s*mochiAnimationFrame\+\+;",
    "if (!mochiScreenReady || lastRenderedMochiExpression != expression) {\n"
    "    drawMochiStaticShell(expression);\n"
    "    mochiAnimationFrame = 0;\n"
    "    drawMochiFaceFrame(expression, 0);\n"
    "    return;\n"
    "  }\n\n  mochiAnimationFrame++;",
    flags=re.S,
    required=False,
)

# Avoid long synchronous network calls while Mochi is visible.
text = text.replace("if (weatherRefreshRequested) {",
                    "if (weatherRefreshRequested && currentPage != 2) {")
text = text.replace(
    "if (now - lastWeatherUpdate >= (unsigned long)cfg.weatherIntervalMinutes * 60000UL) {",
    "if (currentPage != 2 && now - lastWeatherUpdate >= (unsigned long)cfg.weatherIntervalMinutes * 60000UL) {",
)
text = text.replace(
    "if (now - lastAirQualityUpdate >= (unsigned long)cfg.aqiIntervalMinutes * 60000UL) {",
    "if (currentPage != 2 && now - lastAirQualityUpdate >= (unsigned long)cfg.aqiIntervalMinutes * 60000UL) {",
)
text = text.replace(
    "if (lastCryptoUpdate == 0 ||\n      now - lastCryptoUpdate >= (unsigned long)cfg.cryptoIntervalSeconds * 1000UL) {",
    "if (currentPage != 2 && (lastCryptoUpdate == 0 ||\n"
    "      now - lastCryptoUpdate >= (unsigned long)cfg.cryptoIntervalSeconds * 1000UL)) {",
)

# Final structural checks. These verify behavior, not fragile whitespace.
required_markers = (
    'FW_VERSION = "8.0.17"',
    'MOCHI_FRAME_INTERVAL_MS = 80UL',
    'typedLocationChanged=true',
    'resolve:(weatherDirty||typedLocationChanged)?1:0',
    "opt.cache='no-store'",
    'weatherRefreshRequested && currentPage != 2',
    'currentPage != 2 && (lastCryptoUpdate == 0',
    'HTTP_GET, handleMochiSelection',
)
missing = [marker for marker in required_markers if marker not in text]
if missing:
    raise RuntimeError("V8.0.17 verification failed: " + ", ".join(missing))

SOURCE.write_text(text, encoding="utf-8")
print("Applied and verified SmallTV V8.0.17 final fixes")
