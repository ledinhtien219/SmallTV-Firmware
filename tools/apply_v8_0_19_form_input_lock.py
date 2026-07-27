from pathlib import Path
import re

SOURCE = Path("ESP8266_SmallTV_Pro_V8_0_15_Full_Audit_Fix.ino")
text = SOURCE.read_text(encoding="utf-8")

text = re.sub(
    r'static const char\* FW_VERSION = "8\.0\.\d+";',
    'static const char* FW_VERSION = "8.0.19";',
    text,
    count=1,
)

REQ = "async function req(url,opt={}){opt.cache='no-store';const r=await fetch(url,opt);if(!r.ok)throw Error(await r.text());return r.json()}"
HELPERS = """const dirtyFields=new Set();
function fieldIsEditing(id){const el=$(id);return !!el&&(document.activeElement===el||dirtyFields.has(id))}
function syncValue(id,value){const el=$(id);if(el&&!fieldIsEditing(id))el.value=value??''}
function syncChecked(id,value){const el=$(id);if(el&&!fieldIsEditing(id))el.checked=!!value}
function clearDirty(...ids){ids.forEach(id=>dirtyFields.delete(id))}
document.addEventListener('input',e=>{if(e.target&&e.target.id&&(e.target.matches('input')||e.target.matches('select')))dirtyFields.add(e.target.id)});
document.addEventListener('change',e=>{if(e.target&&e.target.id&&(e.target.matches('input')||e.target.matches('select')))dirtyFields.add(e.target.id)});"""

if "const dirtyFields=new Set()" not in text:
    if REQ not in text:
        raise RuntimeError("request helper anchor not found")
    text = text.replace(REQ, REQ + "\n" + HELPERS, 1)

# Replace only the assignments that polling must not overwrite. These small
# substitutions are resilient to unrelated formatting/content changes.
value_fields = {
    "city": "s.city",
    "lat": "s.latitude",
    "lon": "s.longitude",
    "utc": "s.utc_offset",
    "weatherInterval": "s.weather_interval",
    "aqiInterval": "s.aqi_interval",
    "interval": "s.page_interval",
    "clockColor": "s.clock_color",
    "dateColor": "s.date_color",
    "ssid": "s.saved_ssid",
}
for field, value in value_fields.items():
    text = text.replace(f"$('" + field + f"').value={value}", f"syncValue('{field}',{value})")

checked_fields = {
    "autoPage": "s.auto_page",
    "colonBlink": "s.colon_blink",
    "use12Hour": "s.use_12_hour",
}
for field, value in checked_fields.items():
    text = text.replace(f"$('" + field + f"').checked={value}", f"syncChecked('{field}',{value})")

text = text.replace(
    "$('cryptoSymbols').value=s.crypto_symbols||'BTC,ETH,DOGE';$('cryptoInterval').value=s.crypto_interval||30;",
    "syncValue('cryptoSymbols',s.crypto_symbols||'BTC,ETH,DOGE');syncValue('cryptoInterval',s.crypto_interval||30);",
)

# Clear dirty flags only after a successful save.
if "clearDirty('autoPage'" not in text:
    text = text.replace(
        "toast('Đã lưu cài đặt hiển thị');await refresh()}",
        "clearDirty('autoPage','interval','colonBlink','use12Hour','clockColor','dateColor');toast('Đã lưu cài đặt hiển thị');await refresh()}",
        1,
    )
if "typedLocationChanged=false;locationValid=true;clearDirty(" not in text:
    text = text.replace(
        "weatherDirty=false;locationValid=true;\n   toast('Đã lưu vị trí mới');",
        "weatherDirty=false;typedLocationChanged=false;locationValid=true;clearDirty('city','lat','lon','utc','weatherInterval','aqiInterval');\n   toast('Đã lưu vị trí mới');",
        1,
    )
if "clearDirty('cryptoSymbols','cryptoInterval')" not in text:
    text = text.replace(
        "$('cryptoSymbols').value=symbols;\n  toast('Đã lưu danh sách coin');",
        "$('cryptoSymbols').value=symbols;clearDirty('cryptoSymbols','cryptoInterval');\n  toast('Đã lưu danh sách coin');",
        1,
    )
if "clearDirty('ssid','password')" not in text:
    text = text.replace(
        "await post('/api/network',{ssid:ssid,password:$('password').value});\n  toast('Đã lưu, thiết bị sẽ khởi động lại')",
        "await post('/api/network',{ssid:ssid,password:$('password').value});clearDirty('ssid','password');\n  toast('Đã lưu, thiết bị sẽ khởi động lại')",
        1,
    )

required = (
    'FW_VERSION = "8.0.19"',
    'const dirtyFields=new Set()',
    "syncValue('ssid',s.saved_ssid)",
    "syncValue('cryptoSymbols'",
    "syncChecked('autoPage'",
    "typedLocationChanged=false",
)
missing = [token for token in required if token not in text]
if missing:
    raise RuntimeError("V8.0.19 verification failed: " + ", ".join(missing))

SOURCE.write_text(text, encoding="utf-8")
print("Applied resilient V8.0.19 form input lock fix")
