from pathlib import Path
import re
import runpy

SOURCE = Path("ESP8266_SmallTV_Pro_V8_0_15_Full_Audit_Fix.ino")
text = SOURCE.read_text(encoding="utf-8")

final_markers = (
    'String solarDate = twoDigits(tmNow.tm_mday)',
    'String lunarDate = lunarDateLabel(tmNow);',
    'lunarDate.replace("AL- ", "");',
    'drawText(61, 132, "DL"',
    'drawText(161, 132, "AL"',
)

# Already materialized: only normalize the version and finish successfully.
if all(marker in text for marker in final_markers):
    text, count = re.subn(
        r'static const char\* FW_VERSION = "8\.0\.\d+";',
        'static const char* FW_VERSION = "8.0.22";',
        text,
        count=1,
    )
    if count != 1:
        raise RuntimeError("FW_VERSION declaration not found")
    SOURCE.write_text(text, encoding="utf-8")
    print("V8.0.22 split calendar already present")
    raise SystemExit(0)

# Materialize all earlier fixes exactly once.
runpy.run_path("tools/apply_v8_0_21_weather_icon_cleanup.py", run_name="__main__")
text = SOURCE.read_text(encoding="utf-8")

text, count = re.subn(
    r'static const char\* FW_VERSION = "8\.0\.\d+";',
    'static const char* FW_VERSION = "8.0.22";',
    text,
    count=1,
)
if count != 1:
    raise RuntimeError("FW_VERSION declaration not found")

old_calendar = '''    // Two compact calendar lines: solar date and Vietnamese lunar date.
    fillRoundRect(18, 130, 204, 32, 6, COL_CARD);

    String dateLine = twoDigits(tmNow.tm_mday) + "/" +
                      twoDigits(tmNow.tm_mon + 1) + "/" +
                      String(tmNow.tm_year + 1900);
    String lunarLine = lunarDateLabel(tmNow);

    const int dateWidth = (int)dateLine.length() * 6;
    const int lunarWidth = (int)lunarLine.length() * 6;
    drawText((240 - dateWidth) / 2, 133, dateLine, cfg.dateColor, COL_CARD, 1);
    drawText((240 - lunarWidth) / 2, 148, lunarLine, COL_TEMP, COL_CARD, 1);
'''

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

if old_calendar not in text:
    raise RuntimeError("Expected V8.0.20 calendar block not found")
text = text.replace(old_calendar, new_calendar, 1)

missing = [marker for marker in final_markers if marker not in text]
if missing:
    raise RuntimeError("V8.0.22 verification failed: " + ", ".join(missing))

SOURCE.write_text(text, encoding="utf-8")
print("Applied deterministic V8.0.22 split calendar")
