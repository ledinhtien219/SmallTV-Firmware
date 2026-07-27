from pathlib import Path
import re

SOURCE = Path("ESP8266_SmallTV_Pro_V8_0_15_Full_Audit_Fix.ino")
text = SOURCE.read_text(encoding="utf-8")


def replace_once(old: str, new: str) -> None:
    global text
    if new in text:
        return
    if old not in text:
        raise RuntimeError(f"Expected fragment not found: {old[:140]!r}")
    text = text.replace(old, new, 1)


text = re.sub(
    r'static const char\* FW_VERSION = "8\.0\.\d+";',
    'static const char* FW_VERSION = "8.0.19";',
    text,
    count=1,
)

replace_once(
    "async function req(url,opt={}){opt.cache='no-store';const r=await fetch(url,opt);if(!r.ok)throw Error(await r.text());return r.json()}\n",
    "async function req(url,opt={}){opt.cache='no-store';const r=await fetch(url,opt);if(!r.ok)throw Error(await r.text());return r.json()}\n"
    "const dirtyFields=new Set();\n"
    "function fieldIsEditing(id){const el=$(id);return !!el&&(document.activeElement===el||dirtyFields.has(id))}\n"
    "function syncValue(id,value){const el=$(id);if(el&&!fieldIsEditing(id))el.value=value??''}\n"
    "function syncChecked(id,value){const el=$(id);if(el&&!fieldIsEditing(id))el.checked=!!value}\n"
    "function clearDirty(...ids){ids.forEach(id=>dirtyFields.delete(id))}\n"
    "document.addEventListener('input',e=>{if(e.target&&e.target.id&&(e.target.matches('input')||e.target.matches('select')))dirtyFields.add(e.target.id)});\n"
    "document.addEventListener('change',e=>{if(e.target&&e.target.id&&(e.target.matches('input')||e.target.matches('select')))dirtyFields.add(e.target.id)});\n"
)

old_refresh = "async function refresh(){try{const s=await req('/api/status');$('online').style.background='#20c88a';$('cityView').textContent=s.city;$('temp').textContent=s.temperature.toFixed(1);$('desc').textContent=s.weather;$('hum').textContent=s.humidity+'%';$('wind').textContent=s.wind_speed.toFixed(1)+' km/h';$('aqi').textContent=s.aqi;$('page').textContent=s.page===0?'Đồng hồ':(s.page===1?'Thời tiết':(s.page===2?'Mochi':'Crypto'));markMochiSelection(s.mochi_expression||0);if(!weatherDirty){$('city').value=s.city;$('lat').value=s.latitude;$('lon').value=s.longitude;$('utc').value=s.utc_offset;locationValid=true}$('weatherInterval').value=s.weather_interval;$('aqiInterval').value=s.aqi_interval;$('weatherLast').textContent=s.weather_last;$('weatherNext').textContent=s.weather_next;$('aqiLast').textContent=s.aqi_last;$('aqiNext').textContent=s.aqi_next;$('autoPage').checked=s.auto_page;$('interval').value=s.page_interval;$('colonBlink').checked=s.colon_blink;$('use12Hour').checked=s.use_12_hour;$('clockColor').value=s.clock_color;$('dateColor').value=s.date_color;$('clockHex').textContent=s.clock_color.toUpperCase();$('dateHex').textContent=s.date_color.toUpperCase();$('ssid').value=s.saved_ssid;$('ip').textContent=s.ip;$('rssi').textContent=s.rssi+' dBm';$('heap').textContent=Math.round(s.free_heap/1024)+' KB';$('firmware').textContent=s.firmware||'--';$('build').textContent=s.build||'--';$('uptime').textContent=Math.floor(s.uptime/60)+' phút';$('wifiState').textContent=s.wifi_state;$('ssidView').textContent=s.connected_ssid||s.ap_ssid||'--';$('ipView').textContent=s.ip;$('rssiView').textContent=s.ap_mode?'AP':s.rssi+' dBm';$('netline').textContent=s.ap_mode?('AP '+s.ap_ssid+' · 192.168.4.1'):(s.wifi_state+' · '+(s.connected_ssid||s.saved_ssid)+' · '+s.ip);"
new_refresh = "async function refresh(){try{const s=await req('/api/status');$('online').style.background='#20c88a';$('cityView').textContent=s.city;$('temp').textContent=s.temperature.toFixed(1);$('desc').textContent=s.weather;$('hum').textContent=s.humidity+'%';$('wind').textContent=s.wind_speed.toFixed(1)+' km/h';$('aqi').textContent=s.aqi;$('page').textContent=s.page===0?'Đồng hồ':(s.page===1?'Thời tiết':(s.page===2?'Mochi':'Crypto'));markMochiSelection(s.mochi_expression||0);if(!weatherDirty){syncValue('city',s.city);syncValue('lat',s.latitude);syncValue('lon',s.longitude);syncValue('utc',s.utc_offset);locationValid=true}syncValue('weatherInterval',s.weather_interval);syncValue('aqiInterval',s.aqi_interval);$('weatherLast').textContent=s.weather_last;$('weatherNext').textContent=s.weather_next;$('aqiLast').textContent=s.aqi_last;$('aqiNext').textContent=s.aqi_next;syncChecked('autoPage',s.auto_page);syncValue('interval',s.page_interval);syncChecked('colonBlink',s.colon_blink);syncChecked('use12Hour',s.use_12_hour);syncValue('clockColor',s.clock_color);syncValue('dateColor',s.date_color);$('clockHex').textContent=s.clock_color.toUpperCase();$('dateHex').textContent=s.date_color.toUpperCase();syncValue('ssid',s.saved_ssid);$('ip').textContent=s.ip;$('rssi').textContent=s.rssi+' dBm';$('heap').textContent=Math.round(s.free_heap/1024)+' KB';$('firmware').textContent=s.firmware||'--';$('build').textContent=s.build||'--';$('uptime').textContent=Math.floor(s.uptime/60)+' phút';$('wifiState').textContent=s.wifi_state;$('ssidView').textContent=s.connected_ssid||s.ap_ssid||'--';$('ipView').textContent=s.ip;$('rssiView').textContent=s.ap_mode?'AP':s.rssi+' dBm';$('netline').textContent=s.ap_mode?('AP '+s.ap_ssid+' · 192.168.4.1'):(s.wifi_state+' · '+(s.connected_ssid||s.saved_ssid)+' · '+s.ip);"
replace_once(old_refresh, new_refresh)

replace_once(
    "if($('cryptoSymbols')){$('cryptoSymbols').value=s.crypto_symbols||'BTC,ETH,DOGE';$('cryptoInterval').value=s.crypto_interval||30;",
    "if($('cryptoSymbols')){syncValue('cryptoSymbols',s.crypto_symbols||'BTC,ETH,DOGE');syncValue('cryptoInterval',s.crypto_interval||30);",
)

replace_once(
    "toast('Đã lưu cài đặt hiển thị');await refresh()}",
    "clearDirty('autoPage','interval','colonBlink','use12Hour','clockColor','dateColor');toast('Đã lưu cài đặt hiển thị');await refresh()}",
)

replace_once(
    "weatherDirty=false;locationValid=true;\n   toast('Đã lưu vị trí mới');",
    "weatherDirty=false;typedLocationChanged=false;locationValid=true;clearDirty('city','lat','lon','utc','weatherInterval','aqiInterval');\n   toast('Đã lưu vị trí mới');",
)

replace_once(
    "$('cryptoSymbols').value=symbols;\n  toast('Đã lưu danh sách coin');",
    "$('cryptoSymbols').value=symbols;clearDirty('cryptoSymbols','cryptoInterval');\n  toast('Đã lưu danh sách coin');",
)

# Keep Wi-Fi text stable while typing; the device restarts immediately after save.
replace_once(
    "await post('/api/network',{ssid:ssid,password:$('password').value});\n  toast('Đã lưu, thiết bị sẽ khởi động lại')",
    "await post('/api/network',{ssid:ssid,password:$('password').value});clearDirty('ssid','password');\n  toast('Đã lưu, thiết bị sẽ khởi động lại')",
)

required = (
    'FW_VERSION = "8.0.19"',
    'const dirtyFields=new Set()',
    "syncValue('ssid',s.saved_ssid)",
    "syncValue('cryptoSymbols'",
    "syncChecked('autoPage'",
    "typedLocationChanged=false",
)
for token in required:
    if token not in text:
        raise RuntimeError(f"V8.0.19 verification failed: {token}")

SOURCE.write_text(text, encoding="utf-8")
print("Applied V8.0.19 form input lock fix")
