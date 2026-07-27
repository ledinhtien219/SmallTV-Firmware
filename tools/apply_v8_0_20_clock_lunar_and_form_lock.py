from pathlib import Path
import re
import runpy

# First apply the complete V8.0.19 web form protection.
runpy.run_path("tools/apply_v8_0_19_form_input_lock.py", run_name="__main__")

SOURCE = Path("ESP8266_SmallTV_Pro_V8_0_15_Full_Audit_Fix.ino")
text = SOURCE.read_text(encoding="utf-8")


def replace_once(old: str, new: str) -> None:
    global text
    if new in text:
        return
    if old not in text:
        raise RuntimeError(f"Expected fragment not found: {old[:160]!r}")
    text = text.replace(old, new, 1)


text = re.sub(
    r'static const char\* FW_VERSION = "8\.0\.\d+";',
    'static const char* FW_VERSION = "8.0.20";',
    text,
    count=1,
)

replace_once(
'''void updateBigDigit(int x, int y, int value, int &cachedValue) {
  if (value == cachedValue) return;
  clearBigDigit(x, y);
  drawBigDigit(x, y, value, cfg.clockColor);
  cachedValue = value;
}
''',
'''void updateBigDigit(int x, int y, int value, int &cachedValue) {
  if (value == cachedValue) return;
  clearBigDigit(x, y);
  // A negative value means a deliberately blank leading position.
  if (value >= 0 && value <= 9) drawBigDigit(x, y, value, cfg.clockColor);
  cachedValue = value;
}
''')

lunar_code = r'''
long lunarJulianDay(int day, int month, int year) {
  int a = (14 - month) / 12;
  int y = year + 4800 - a;
  int m = month + 12 * a - 3;
  long jd = day + (153L * m + 2) / 5 + 365L * y + y / 4 - y / 100 + y / 400 - 32045L;
  if (jd < 2299161L) jd = day + (153L * m + 2) / 5 + 365L * y + y / 4 - 32083L;
  return jd;
}

long lunarNewMoonDay(int k, double timeZoneHours) {
  const double PI_D = 3.14159265358979323846;
  double T = k / 1236.85;
  double T2 = T * T;
  double T3 = T2 * T;
  double dr = PI_D / 180.0;
  double jd1 = 2415020.75933 + 29.53058868 * k + 0.0001178 * T2 - 0.000000155 * T3;
  jd1 += 0.00033 * sin((166.56 + 132.87 * T - 0.009173 * T2) * dr);
  double M = 359.2242 + 29.10535608 * k - 0.0000333 * T2 - 0.00000347 * T3;
  double mpr = 306.0253 + 385.81691806 * k + 0.0107306 * T2 + 0.00001236 * T3;
  double F = 21.2964 + 390.67050646 * k - 0.0016528 * T2 - 0.00000239 * T3;
  double c1 = (0.1734 - 0.000393 * T) * sin(M * dr) + 0.0021 * sin(2 * M * dr);
  c1 -= 0.4068 * sin(mpr * dr) + 0.0161 * sin(2 * mpr * dr);
  c1 -= 0.0004 * sin(3 * mpr * dr);
  c1 += 0.0104 * sin(2 * F * dr) - 0.0051 * sin((M + mpr) * dr);
  c1 -= 0.0074 * sin((M - mpr) * dr) + 0.0004 * sin((2 * F + M) * dr);
  c1 -= 0.0004 * sin((2 * F - M) * dr) - 0.0006 * sin((2 * F + mpr) * dr);
  c1 += 0.0010 * sin((2 * F - mpr) * dr) + 0.0005 * sin((2 * mpr + M) * dr);
  double deltaT = T < -11
      ? 0.001 + 0.000839 * T + 0.0002261 * T2 - 0.00000845 * T3 - 0.000000081 * T * T3
      : -0.000278 + 0.000265 * T + 0.000262 * T2;
  return (long)floor(jd1 + c1 - deltaT + 0.5 + timeZoneHours / 24.0);
}

int lunarSunLongitude(long jdn, double timeZoneHours) {
  const double PI_D = 3.14159265358979323846;
  double T = (jdn - 2451545.5 - timeZoneHours / 24.0) / 36525.0;
  double T2 = T * T;
  double dr = PI_D / 180.0;
  double M = 357.52910 + 35999.05030 * T - 0.0001559 * T2 - 0.00000048 * T * T2;
  double L0 = 280.46645 + 36000.76983 * T + 0.0003032 * T2;
  double dl = (1.914600 - 0.004817 * T - 0.000014 * T2) * sin(dr * M);
  dl += (0.019993 - 0.000101 * T) * sin(2 * dr * M) + 0.000290 * sin(3 * dr * M);
  double L = (L0 + dl) * dr;
  L -= PI_D * 2.0 * floor(L / (PI_D * 2.0));
  return (int)floor(L / PI_D * 6.0);
}

long lunarMonth11(int year, double timeZoneHours) {
  long off = lunarJulianDay(31, 12, year) - 2415021L;
  int k = (int)floor(off / 29.530588853);
  long nm = lunarNewMoonDay(k, timeZoneHours);
  if (lunarSunLongitude(nm, timeZoneHours) >= 9) nm = lunarNewMoonDay(k - 1, timeZoneHours);
  return nm;
}

int lunarLeapMonthOffset(long a11, double timeZoneHours) {
  int k = (int)floor((a11 - 2415021.076998695) / 29.530588853 + 0.5);
  int last = 0;
  int i = 1;
  int arc = lunarSunLongitude(lunarNewMoonDay(k + i, timeZoneHours), timeZoneHours);
  do {
    last = arc;
    i++;
    arc = lunarSunLongitude(lunarNewMoonDay(k + i, timeZoneHours), timeZoneHours);
  } while (arc != last && i < 14);
  return i - 1;
}

void solarToLunar(int day, int month, int year, double timeZoneHours,
                  int &lunarDay, int &lunarMonth, int &lunarYear) {
  long dayNumber = lunarJulianDay(day, month, year);
  int k = (int)floor((dayNumber - 2415021.076998695) / 29.530588853);
  long monthStart = lunarNewMoonDay(k + 1, timeZoneHours);
  if (monthStart > dayNumber) monthStart = lunarNewMoonDay(k, timeZoneHours);
  long a11 = lunarMonth11(year, timeZoneHours);
  long b11 = a11;
  lunarYear = year;
  if (a11 >= monthStart) {
    lunarYear = year;
    a11 = lunarMonth11(year - 1, timeZoneHours);
  } else {
    lunarYear = year + 1;
    b11 = lunarMonth11(year + 1, timeZoneHours);
  }
  lunarDay = (int)(dayNumber - monthStart + 1);
  int diff = (int)floor((monthStart - a11) / 29.0);
  lunarMonth = diff + 11;
  if (b11 - a11 > 365) {
    int leapDiff = lunarLeapMonthOffset(a11, timeZoneHours);
    if (diff >= leapDiff) lunarMonth = diff + 10;
  }
  if (lunarMonth > 12) lunarMonth -= 12;
  if (lunarMonth >= 11 && diff < 4) lunarYear -= 1;
}

String lunarDateLabel(const struct tm &tmNow) {
  int lunarDay = 1;
  int lunarMonth = 1;
  int lunarYear = tmNow.tm_year + 1900;
  solarToLunar(tmNow.tm_mday, tmNow.tm_mon + 1, tmNow.tm_year + 1900,
               cfg.utcOffsetMinutes / 60.0, lunarDay, lunarMonth, lunarYear);
  return "AL- " + twoDigits(lunarDay) + "/" + twoDigits(lunarMonth);
}

'''

marker = "// ---------------- Premium digital clock ----------------\n"
if "String lunarDateLabel(" not in text:
    text = text.replace(marker, lunar_code + marker, 1)

replace_once(
'''  updateBigDigit(hourTensX, clockY, displayHour / 10, lastHourTens);
  updateBigDigit(hourOnesX, clockY, displayHour % 10, lastHourOnes);
''',
'''  // In 24-hour mode, 01:xx..09:xx are displayed as 1:xx..9:xx.
  int leadingHourDigit = (!cfg.use12Hour && displayHour < 10) ? -1 : displayHour / 10;
  updateBigDigit(hourTensX, clockY, leadingHourDigit, lastHourTens);
  updateBigDigit(hourOnesX, clockY, displayHour % 10, lastHourOnes);
''')

replace_once(
'''    // Bỏ dòng thứ, chỉ giữ ngày lớn màu xanh cyan.
    fillRoundRect(18, 132, 204, 28, 6, COL_CARD);

    String dateLine = twoDigits(tmNow.tm_mday) + "/" +
                      twoDigits(tmNow.tm_mon + 1) + "/" +
                      String(tmNow.tm_year + 1900);

    const int dateScale = 2;
    const int dateWidth = (int)dateLine.length() * 6 * dateScale;
    const int dateX = (240 - dateWidth) / 2;

    drawText(dateX, 139, dateLine, cfg.dateColor, COL_CARD, dateScale);
''',
'''    // Two compact calendar lines: solar date and Vietnamese lunar date.
    fillRoundRect(18, 130, 204, 32, 6, COL_CARD);

    String dateLine = twoDigits(tmNow.tm_mday) + "/" +
                      twoDigits(tmNow.tm_mon + 1) + "/" +
                      String(tmNow.tm_year + 1900);
    String lunarLine = lunarDateLabel(tmNow);

    const int dateWidth = (int)dateLine.length() * 6;
    const int lunarWidth = (int)lunarLine.length() * 6;
    drawText((240 - dateWidth) / 2, 133, dateLine, cfg.dateColor, COL_CARD, 1);
    drawText((240 - lunarWidth) / 2, 148, lunarLine, COL_TEMP, COL_CARD, 1);
''')

required = (
    'FW_VERSION = "8.0.20"',
    'const dirtyFields=new Set()',
    'String lunarDateLabel(',
    'return "AL- " + twoDigits(lunarDay)',
    'leadingHourDigit = (!cfg.use12Hour && displayHour < 10) ? -1',
    'if (value >= 0 && value <= 9)',
)
for token in required:
    if token not in text:
        raise RuntimeError(f"V8.0.20 verification failed: {token}")

SOURCE.write_text(text, encoding="utf-8")
print("Applied V8.0.20 form lock, compact 24-hour clock, and lunar date")
