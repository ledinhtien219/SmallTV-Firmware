from pathlib import Path
import re
import runpy

runpy.run_path("tools/apply_v8_0_21_weather_icon_cleanup.py", run_name="__main__")

SOURCE = Path("ESP8266_SmallTV_Pro_V8_0_15_Full_Audit_Fix.ino")
text = SOURCE.read_text(encoding="utf-8")

text, version_count = re.subn(
    r'static const char\* FW_VERSION = "8\.0\.\d+";',
    'static const char* FW_VERSION = "8.0.22";',
    text,
    count=1,
)
if version_count != 1:
    raise RuntimeError("FW_VERSION declaration not found")

new_calendar = '''    fillRoundRect(18, 130, 204, 32, 6, COL_CARD);
    fillRect(119, 133, 2, 26, COL_LINE);

    String solarDate = twoDigits(tmNow.tm_mday) + "/" + twoDigits(tmNow.tm_mon + 1);
    String lunarDate = lunarDateLabel(tmNow);
    lunarDate.replace("AL- ", "");

    drawText(61, 132, "DL", COL_MUTED, COL_CARD, 1);
    drawText(161, 132, "AL", COL_MUTED, COL_CARD, 1);

    const int solarWidth = (int)solarDate.length() * 12;
    const int lunarWidth = (int)lunarDate.length() * 12;
    drawText(69 - solarWidth / 2, 145, solarDate, cfg.dateColor, COL_CARD, 2);
    drawText(171 - lunarWidth / 2, 145, lunarDate, COL_TEMP, COL_CARD, 2);
'''

# Idempotent: leave an already-patched source untouched.
if 'String solarDate = twoDigits(tmNow.tm_mday)' not in text:
    pattern = re.compile(
        r'(?ms)^    (?:\/\/[^\n]*\n)?'
        r'    fillRoundRect\(18,\s*1(?:30|32),\s*204,\s*(?:28|32),\s*6,\s*COL_CARD\);.*?'
        r'^    drawText\([^\n]*(?:dateLine|lunarLine)[^\n]*\);\n'
        r'(?:^    drawText\([^\n]*lunarLine[^\n]*\);\n)?'
    )
    text, replaced = pattern.subn(new_calendar, text, count=1)
    if replaced != 1:
        raise RuntimeError("Calendar drawing block not found structurally")

required = (
    'static const char* FW_VERSION = "8.0.22";',
    'String solarDate = twoDigits(tmNow.tm_mday)',
    'String lunarDate = lunarDateLabel(tmNow);',
    'lunarDate.replace("AL- ", "");',
    'drawText(61, 132, "DL"',
    'drawText(161, 132, "AL"',
    'drawText(69 - solarWidth / 2, 145, solarDate',
    'drawText(171 - lunarWidth / 2, 145, lunarDate',
)
missing = [token for token in required if token not in text]
if missing:
    raise RuntimeError("V8.0.22 verification failed: " + ", ".join(missing))

SOURCE.write_text(text, encoding="utf-8")
print("Applied V8.0.22 split solar/lunar calendar")
