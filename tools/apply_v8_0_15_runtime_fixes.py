from pathlib import Path

SOURCE = Path("ESP8266_SmallTV_Pro_V8_0_15_Full_Audit_Fix.ino")
text = SOURCE.read_text(encoding="utf-8")


def replace_once(old: str, new: str) -> None:
    global text
    if old not in text:
        raise RuntimeError(f"Expected source fragment not found: {old[:80]!r}")
    text = text.replace(old, new, 1)


replace_once(
    "bool weatherOk = false;\n",
    "bool weatherOk = false;\nbool weatherRefreshRequested = false;\n",
)
replace_once(
    "unsigned long lastMochiAnimation = 0;\n",
    "unsigned long lastMochiAnimation = 0;\n"
    "const unsigned long MOCHI_FRAME_INTERVAL_MS = 80UL;\n",
)

replace_once(
    "async function setMochi(expression){\n"
    " await post('/api/mochi',{expression});\n"
    " toast('Đã đổi biểu cảm Mochi');\n"
    " await go('/api/page?value=2')\n"
    "}\n",
    "function markMochiSelection(expression){\n"
    " document.querySelectorAll('.mochiBtn[data-expression]').forEach(btn=>{\n"
    "  btn.classList.toggle('selected',Number(btn.dataset.expression)===Number(expression))\n"
    " })\n"
    "}\n"
    "async function setMochi(expression){\n"
    " try{\n"
    "  const r=await post('/api/mochi',{expression:String(expression)});\n"
    "  markMochiSelection(r.expression);\n"
    "  toast('Đã đổi biểu cảm Mochi');\n"
    "  await refresh()\n"
    " }catch(e){toast('Không thể đổi biểu cảm Mochi')}\n"
    "}\n",
)

replace_once(
    "$('page').textContent=s.page===0?'Đồng hồ':(s.page===1?'Thời tiết':(s.page===2?'Mochi':'Crypto'));",
    "$('page').textContent=s.page===0?'Đồng hồ':(s.page===1?'Thời tiết':(s.page===2?'Mochi':'Crypto'));"
    "markMochiSelection(s.mochi_expression||0);",
)

for expression in range(7):
    replace_once(
        f'class="mochiBtn" onclick="setMochi({expression})"',
        f'class="mochiBtn" data-expression="{expression}" onclick="setMochi({expression})"',
    )

replace_once(
    ".info{margin-top:13px;",
    ".mochiBtn.selected{outline:2px solid #55d9f0;background:#18394a;"
    "box-shadow:0 0 0 3px rgba(85,217,240,.14)}.info{margin-top:13px;",
)

replace_once(
    "  server.on(\"/api/mochi\", HTTP_POST, []() {\n"
    "    mochiExpression = constrain(server.arg(\"expression\").toInt(), 0, 6);\n"
    "    currentPage = 2;\n"
    "    forcePageRedraw = true;\n"
    "    lastPageChange = millis();\n"
    "    String json = \"{\\\"ok\\\":true,\\\"expression\\\":\" + String(mochiExpression) + \"}\";\n"
    "    server.send(200, \"application/json\", json);\n"
    "  });\n",
    "  server.on(\"/api/mochi\", HTTP_POST, []() {\n"
    "    if (!server.hasArg(\"expression\")) {\n"
    "      server.send(400, \"application/json; charset=utf-8\",\n"
    "                  \"{\\\"ok\\\":false,\\\"error\\\":\\\"missing_expression\\\"}\");\n"
    "      return;\n"
    "    }\n"
    "    int requestedExpression = server.arg(\"expression\").toInt();\n"
    "    if (requestedExpression < 0 || requestedExpression > 6) {\n"
    "      server.send(400, \"application/json; charset=utf-8\",\n"
    "                  \"{\\\"ok\\\":false,\\\"error\\\":\\\"invalid_expression\\\"}\");\n"
    "      return;\n"
    "    }\n"
    "    mochiExpression = (uint8_t)requestedExpression;\n"
    "    currentPage = 2;\n"
    "    mochiAnimationFrame = 0;\n"
    "    lastMochiAnimation = millis();\n"
    "    mochiScreenReady = false;\n"
    "    lastRenderedMochiExpression = 255;\n"
    "    forcePageRedraw = true;\n"
    "    lastPageChange = millis();\n"
    "    String json = \"{\\\"ok\\\":true,\\\"expression\\\":\" + String(mochiExpression) + \"}\";\n"
    "    server.send(200, \"application/json; charset=utf-8\", json);\n"
    "  });\n",
)

replace_once(
    "    saveSettings();\n"
    "    configTime(cfg.utcOffsetMinutes * 60, 0, \"pool.ntp.org\", \"time.nist.gov\");\n"
    "    if (!apMode) {\n"
    "      fetchWeather();\n"
    "      fetchAirQuality();\n"
    "    }\n"
    "    forcePageRedraw = true;\n"
    "    sendJsonOk();\n",
    "    saveSettings();\n"
    "    configTime(cfg.utcOffsetMinutes * 60, 0, \"pool.ntp.org\", \"time.nist.gov\");\n"
    "    // Reply immediately; refresh network data from loop afterwards.\n"
    "    weatherRefreshRequested = !apMode;\n"
    "    forcePageRedraw = true;\n"
    "    sendJsonOk();\n",
)

replace_once(
    "  // Clear only dynamic regions. This avoids full-screen flashing.\n"
    "  fillRect(72, 103, 96, 30, body);   // eyes and eyebrows\n"
    "  fillRect(99, 148, 42, 30, body);   // mouth\n"
    "  fillRect(145, 126, 18, 42, body);  // tear area\n"
    "  fillRect(16, 61, 24, 70, COL_BG);  // left sparkle\n"
    "  fillRect(199, 55, 27, 82, COL_BG); // right sparkle\n"
    "  fillRect(146, 48, 50, 48, COL_BG); // floating Z / hearts\n"
    "  fillRect(44, 48, 28, 35, COL_BG);  // left heart\n",
    "  // Clear only regions used by the active expression to reduce SPI work.\n"
    "  fillRect(72, 103, 96, 30, body);\n"
    "  fillRect(99, 148, 42, 30, body);\n"
    "  if (expression == 2) fillRect(145, 126, 18, 42, body);\n"
    "  if (expression == 1) {\n"
    "    fillRect(16, 61, 24, 70, COL_BG);\n"
    "    fillRect(199, 55, 27, 82, COL_BG);\n"
    "  }\n"
    "  if (expression == 3 || expression == 6) {\n"
    "    fillRect(146, 48, 50, 48, COL_BG);\n"
    "    fillRect(44, 48, 28, 35, COL_BG);\n"
    "  }\n",
)

replace_once("int tearY = 132 + (frame % 8) * 4;", "int tearY = 132 + (frame % 15) * 2;")
replace_once(
    "int phase = frame % 12;\n    drawText(151 + phase, 86 - phase * 2, \"Z\"",
    "int phase = frame % 18;\n    drawText(151 + phase / 2, 86 - phase, \"Z\"",
)
replace_once(
    "int phase = frame % 10;\n    int heartY = 72 - phase * 2;",
    "int phase = frame % 18;\n    int heartY = 72 - phase;",
)
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

SOURCE.write_text(text, encoding="utf-8")
print(f"Applied runtime fixes to {SOURCE}")
