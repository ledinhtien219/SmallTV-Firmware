from pathlib import Path

SOURCE = Path("ESP8266_SmallTV_Pro_V8_0_15_Full_Audit_Fix.ino")
text = SOURCE.read_text(encoding="utf-8")


def replace_once(old: str, new: str) -> None:
    global text
    if old not in text:
        raise RuntimeError(f"Expected source fragment not found: {old[:120]!r}")
    text = text.replace(old, new, 1)


replace_once('static const char* FW_VERSION = "8.0.15";',
             'static const char* FW_VERSION = "8.0.16";')
replace_once("bool weatherOk = false;\n",
             "bool weatherOk = false;\nbool weatherRefreshRequested = false;\n")
replace_once("unsigned long lastMochiAnimation = 0;\n",
             "unsigned long lastMochiAnimation = 0;\nconst unsigned long MOCHI_FRAME_INTERVAL_MS = 50UL;\n")

replace_once(
    ".info{margin-top:13px;",
    ".mochiBtn.selected{outline:2px solid #55d9f0;background:#18394a;"
    "box-shadow:0 0 0 3px rgba(85,217,240,.14)}.info{margin-top:13px;",
)

for expression in range(7):
    replace_once(
        f'<button class="mochiBtn" onclick="setMochi({expression})">',
        f'<button type="button" class="mochiBtn" data-expression="{expression}" onclick="setMochi({expression},this)">',
    )
replace_once(
    '<button class="primary mochiBtn" onclick="showMochi()">',
    '<button type="button" class="primary mochiBtn" onclick="showMochi()">',
)

replace_once(
    "$('page').textContent=s.page===0?'Đồng hồ':(s.page===1?'Thời tiết':(s.page===2?'Mochi':'Crypto'));",
    "$('page').textContent=s.page===0?'Đồng hồ':(s.page===1?'Thời tiết':(s.page===2?'Mochi':'Crypto'));"
    "markMochiSelection(s.mochi_expression||0);",
)

replace_once(
'''async function setMochi(expression){
 await post('/api/mochi',{expression});
 toast('Đã đổi biểu cảm Mochi');
 await go('/api/page?value=2')
}
''',
'''function markMochiSelection(expression){
 document.querySelectorAll('.mochiBtn[data-expression]').forEach(btn=>{
  btn.classList.toggle('selected',Number(btn.dataset.expression)===Number(expression))
 })
}
async function setMochi(expression,button){
 markMochiSelection(expression);
 if(button)button.blur();
 try{
  const r=await req('/api/mochi?expression='+encodeURIComponent(expression));
  markMochiSelection(r.expression);
  $('page').textContent='Mochi';
  toast('Đã chọn biểu cảm Mochi')
 }catch(e){
  markMochiSelection(-1);
  toast('Không thể chọn biểu cảm Mochi')
 }
}
''')

replace_once(
'''async function saveWeather(){
 if(!locationValid){
  if(!await findLocation(false))return
 }
 await post('/api/weather',{
  city:$('city').value,
  lat:$('lat').value,
  lon:$('lon').value,
  utc:$('utc').value,
  weatherInterval:$('weatherInterval').value,
  aqiInterval:$('aqiInterval').value
 });
 weatherDirty=false;locationValid=true;
 toast('Đã lưu vị trí và cấu hình thời tiết');
 await refresh()
}
''',
'''async function saveWeather(){
 const city=$('city').value.trim();
 if(city.length<2){toast('Hãy nhập tên địa điểm');return}
 toast('Đang xác định và lưu vị trí...');
 try{
  const result=await post('/api/weather',{
   city:city,lat:$('lat').value,lon:$('lon').value,
   resolve:weatherDirty?1:0,utc:$('utc').value,
   weatherInterval:$('weatherInterval').value,
   aqiInterval:$('aqiInterval').value
  });
  $('city').value=result.city||city;
  $('lat').value=Number(result.latitude).toFixed(5);
  $('lon').value=Number(result.longitude).toFixed(5);
  $('locationResult').textContent='✓ Đã lưu: '+$('city').value+' ('+$('lat').value+', '+$('lon').value+')';
  weatherDirty=false;locationValid=true;
  toast('Đã lưu vị trí mới');
  await refresh()
 }catch(e){
  $('locationResult').textContent='Không thể xác định vị trí. Kiểm tra Internet hoặc thử tên tỉnh/thành phố gần nhất.';
  toast('Không lưu được vị trí mới')
 }
}
''')

replace_once(
'''  server.on("/api/mochi", HTTP_POST, []() {
    mochiExpression = constrain(server.arg("expression").toInt(), 0, 6);
    currentPage = 2;
    forcePageRedraw = true;
    lastPageChange = millis();
    String json = "{\\"ok\\":true,\\"expression\\":" + String(mochiExpression) + "}";
    server.send(200, "application/json", json);
  });
''',
'''  auto handleMochiSelection = []() {
    if (!server.hasArg("expression")) {
      server.send(400, "application/json; charset=utf-8", "{\\"ok\\":false,\\"error\\":\\"missing_expression\\"}");
      return;
    }
    int requested = server.arg("expression").toInt();
    if (requested < 0 || requested > 6) {
      server.send(400, "application/json; charset=utf-8", "{\\"ok\\":false,\\"error\\":\\"invalid_expression\\"}");
      return;
    }
    mochiExpression = (uint8_t)requested;
    currentPage = 2;
    mochiAnimationFrame = 0;
    lastMochiAnimation = millis();
    mochiScreenReady = false;
    lastRenderedMochiExpression = 255;
    lastMochiBlink = false;
    forcePageRedraw = true;
    lastPageChange = millis();
    String json = "{\\"ok\\":true,\\"expression\\":" + String(mochiExpression) + "}";
    server.send(200, "application/json; charset=utf-8", json);
  };
  server.on("/api/mochi", HTTP_GET, handleMochiSelection);
  server.on("/api/mochi", HTTP_POST, handleMochiSelection);
''')

old_weather = '''  server.on("/api/weather", HTTP_POST, []() {
    String submittedLocation = server.arg("city");
    submittedLocation.trim();
    copyUtf8Safe(cfg.locationName, sizeof(cfg.locationName), submittedLocation);
    // Keep the legacy short field for LCD/backward compatibility only.
    copyUtf8Safe(cfg.city, sizeof(cfg.city), submittedLocation);
    strlcpy(cfg.latitude, server.arg("lat").c_str(), sizeof(cfg.latitude));
    strlcpy(cfg.longitude, server.arg("lon").c_str(), sizeof(cfg.longitude));
    cfg.utcOffsetMinutes = server.arg("utc").toInt();
    cfg.weatherIntervalMinutes = constrain(server.arg("weatherInterval").toInt(), 1, 60);
    cfg.aqiIntervalMinutes = constrain(server.arg("aqiInterval").toInt(), 1, 60);
    saveSettings();
    configTime(cfg.utcOffsetMinutes * 60, 0, "pool.ntp.org", "time.nist.gov");
    if (!apMode) {
      fetchWeather();
      fetchAirQuality();
    }
    forcePageRedraw = true;
    sendJsonOk();
  });
'''
new_weather = '''  server.on("/api/weather", HTTP_POST, []() {
    String submittedLocation = server.arg("city");
    submittedLocation.trim();
    if (submittedLocation.length() < 2) {
      server.send(400, "application/json; charset=utf-8", "{\\"ok\\":false,\\"error\\":\\"missing_location\\"}");
      return;
    }
    String resolvedLocation = submittedLocation;
    float latitude = server.arg("lat").toFloat();
    float longitude = server.arg("lon").toFloat();
    bool mustResolve = server.arg("resolve").toInt() != 0 ||
                       latitude < -90.0f || latitude > 90.0f ||
                       longitude < -180.0f || longitude > 180.0f ||
                       (fabs(latitude) < 0.0001f && fabs(longitude) < 0.0001f);
    if (mustResolve && !geocodeLocation(submittedLocation, resolvedLocation, latitude, longitude)) {
      server.send(422, "application/json; charset=utf-8", "{\\"ok\\":false,\\"error\\":\\"location_not_found\\"}");
      return;
    }
    copyUtf8Safe(cfg.locationName, sizeof(cfg.locationName), resolvedLocation);
    copyUtf8Safe(cfg.city, sizeof(cfg.city), resolvedLocation);
    String latitudeText = String(latitude, 5);
    String longitudeText = String(longitude, 5);
    strlcpy(cfg.latitude, latitudeText.c_str(), sizeof(cfg.latitude));
    strlcpy(cfg.longitude, longitudeText.c_str(), sizeof(cfg.longitude));
    cfg.utcOffsetMinutes = constrain(server.arg("utc").toInt(), -720, 840);
    cfg.weatherIntervalMinutes = constrain(server.arg("weatherInterval").toInt(), 1, 60);
    cfg.aqiIntervalMinutes = constrain(server.arg("aqiInterval").toInt(), 1, 60);
    saveSettings();
    configTime(cfg.utcOffsetMinutes * 60, 0, "pool.ntp.org", "time.nist.gov");
    weatherRefreshRequested = !apMode;
    forcePageRedraw = true;
    String json = "{\\"ok\\":true,";
    json += "\\"city\\":\\"" + jsonEscape(resolvedLocation) + "\\",";
    json += "\\"latitude\\":" + latitudeText + ",";
    json += "\\"longitude\\":" + longitudeText + "}";
    server.send(200, "application/json; charset=utf-8", json);
  });
'''
replace_once(old_weather, new_weather)

start = text.index("void drawMochiFaceFrame(uint8_t expression, uint8_t frame) {")
end = text.index("\nvoid drawMochiPage() {", start)
text = text[:start] + r'''void drawMochiFaceFrame(uint8_t expression, uint8_t frame) {
  const uint16_t body = RGB565(245, 244, 239);
  const int eyeY = 120;
  const int mouthY = 158;
  bool blink = (frame % 60 == 54 || frame % 60 == 55 || frame % 60 == 56);
  bool firstFrame = (frame == 0);
  if (firstFrame || blink != lastMochiBlink) {
    fillRect(72, 103, 96, 30, body);
    if (blink && expression != 3 && expression != 4) {
      fillRoundRect(77, eyeY, 20, 3, 1, COL_LINE);
      fillRoundRect(141, eyeY, 20, 3, 1, COL_LINE);
    } else {
      drawMochiEye(87, eyeY, expression);
      drawMochiEye(151, eyeY, expression);
    }
  }
  if (firstFrame) {
    fillRect(99, 148, 42, 30, body);
    if (expression == 1 || expression == 6) {
      fillRect(105, mouthY - 7, 28, 3, COL_LINE);
      fillRect(109, mouthY - 4, 20, 3, COL_LINE);
      fillRect(114, mouthY - 1, 10, 3, COL_LINE);
    } else if (expression == 2) {
      fillRect(110, mouthY + 2, 18, 3, COL_LINE);
      fillRect(106, mouthY - 1, 4, 3, COL_LINE);
      fillRect(128, mouthY - 1, 4, 3, COL_LINE);
    } else if (expression == 3) {
      drawText(108, mouthY - 7, "Z", COL_ACCENT, body, 2);
    } else if (expression == 4) {
      fillCircle(119, mouthY, 9, COL_LINE);
      fillCircle(119, mouthY - 1, 4, body);
    } else if (expression == 5) {
      fillRect(107, mouthY + 1, 24, 4, COL_LINE);
      fillRect(76, eyeY - 10, 22, 3, COL_LINE);
      fillRect(140, eyeY - 10, 22, 3, COL_LINE);
    }
  }
  if (expression == 2) {
    fillRect(145, 126, 18, 42, body);
    fillCircle(153, 132 + (frame % 30), 3, RGB565(68, 162, 255));
  } else if (expression == 3) {
    fillRect(146, 48, 50, 48, COL_BG);
    int phase = frame % 36;
    drawText(151 + phase / 3, 86 - phase, "Z", COL_ACCENT, COL_BG, 1);
  } else if (expression == 6) {
    fillRect(44, 48, 28, 35, COL_BG);
    fillRect(174, 55, 28, 42, COL_BG);
    int phase = frame % 30;
    int heartY = 72 - phase;
    fillCircle(54, heartY, 5, RGB565(255, 88, 130));
    fillCircle(61, heartY, 5, RGB565(255, 88, 130));
    fillRect(52, heartY, 12, 7, RGB565(255, 88, 130));
    fillCircle(183, heartY + 12, 4, RGB565(255, 88, 130));
    fillCircle(189, heartY + 12, 4, RGB565(255, 88, 130));
    fillRect(181, heartY + 12, 10, 6, RGB565(255, 88, 130));
  } else if (expression == 1) {
    fillRect(16, 61, 24, 70, COL_BG);
    fillRect(199, 55, 27, 82, COL_BG);
    uint8_t phase = frame % 32;
    if (phase < 16) {
      drawText(25, 90, "*", COL_TEMP, COL_BG, 2);
      drawText(202, 75, "*", COL_ACCENT, COL_BG, 2);
    } else {
      drawText(23, 78, "*", COL_ACCENT, COL_BG, 1);
      drawText(205, 95, "*", COL_TEMP, COL_BG, 1);
    }
  }
  lastMochiBlink = blink;
}
''' + text[end:]

replace_once(
'''  drawMochiFaceFrame(expression, mochiAnimationFrame);
}

void animateMochiFrame()''',
'''  mochiAnimationFrame = 0;
  drawMochiFaceFrame(expression, 0);
}

void animateMochiFrame()''')
replace_once(
    "if (currentPage == 2 && now - lastMochiAnimation >= 125UL) {",
    "if (currentPage == 2 && now - lastMochiAnimation >= MOCHI_FRAME_INTERVAL_MS) {",
)
replace_once(
    "  if (now - lastWeatherUpdate >= (unsigned long)cfg.weatherIntervalMinutes * 60000UL) {\n",
    "  if (weatherRefreshRequested) {\n"
    "    weatherRefreshRequested = false;\n"
    "    lastWeatherUpdate = now;\n"
    "    lastAirQualityUpdate = now;\n"
    "    fetchWeather();\n"
    "    fetchAirQuality();\n"
    "    forcePageRedraw = true;\n"
    "  }\n\n"
    "  if (now - lastWeatherUpdate >= (unsigned long)cfg.weatherIntervalMinutes * 60000UL) {\n",
)

for required in (
    'FW_VERSION = "8.0.16"',
    'HTTP_GET, handleMochiSelection',
    'resolve:weatherDirty?1:0',
    'MOCHI_FRAME_INTERVAL_MS = 50UL',
    'weatherRefreshRequested = !apMode',
):
    if required not in text:
        raise RuntimeError(f"Patch verification failed: {required}")

SOURCE.write_text(text, encoding="utf-8")
print("Applied and verified SmallTV V8.0.16 runtime fixes")
