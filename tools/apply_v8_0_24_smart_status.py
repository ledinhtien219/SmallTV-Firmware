from pathlib import Path
import re
import runpy

SOURCE = Path("ESP8266_SmallTV_Pro_V8_0_15_Full_Audit_Fix.ino")

runpy.run_path("tools/apply_v8_0_23_web_flash_fix.py", run_name="__main__")
text = SOURCE.read_text(encoding="utf-8")

text = text.replace(
    '    json += "\\\"mochi_expression\\\":" + String(mochiExpression) + ",";',
    '    json += "\\\"mochi_expression\\\":" + String(mochiExpression) + ",";\n'
    '    json += "\\\"mochi_resolved\\\":" + String(resolvedMochiExpression()) + ",";',
    1,
)
text = text.replace(
    '    json += "\\\"weather\\\":\\\"" + jsonEscape(weatherName) + "\\\",";',
    '    json += "\\\"weather\\\":\\\"" + jsonEscape(weatherName) + "\\\",";\n'
    '    json += "\\\"weather_code\\\":" + String(weatherCode) + ",";',
    1,
)

old_mochi = """  if (aqi > 120) return 5;
  if (weatherCode >= 51 && weatherCode <= 99) return 2;
  if (weatherCode == 0 || weatherCode == 1) return 1;
  return 4;
"""
new_mochi = """  if (WiFi.status() != WL_CONNECTED) return 4;
  if (aqi >= 151) return 5;
  if (aqi >= 101) return 2;
  if (weatherCode >= 95) return 4;
  if ((weatherCode >= 51 && weatherCode <= 67) ||
      (weatherCode >= 80 && weatherCode <= 82)) return 2;
  if (currentTemp >= 35.0f) return 5;
  if (currentTemp <= 12.0f) return 3;
  if (weatherCode == 0 || weatherCode == 1) return 1;
  return 4;
"""
if old_mochi not in text:
    raise RuntimeError("AUTO Mochi block not found")
text = text.replace(old_mochi, new_mochi, 1)

old_icon = '<div class="weatherEmoji">🌤️</div>'
new_icon = '<div class="weatherEmoji" id="weatherIcon" title="Biểu tượng thời tiết">🌤️</div>'
if old_icon not in text:
    raise RuntimeError("weather icon HTML not found")
text = text.replace(old_icon, new_icon, 1)

css = r'''
body.night{--bg:#02070d;--card:#0b1620;--line:#1e3546;--muted:#8ea4b5}
body.night .topbar,body.night .card{box-shadow:0 18px 44px rgba(0,0,0,.42)}
.systemStrip{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px;margin-top:13px}
.systemPill{background:#0d1a25;border:1px solid #294355;border-radius:13px;padding:11px}
.systemPill span{display:block;color:var(--muted);font-size:11px}
.systemPill b{display:block;margin-top:4px;font-size:14px;overflow-wrap:anywhere}
.alertBox{display:none;margin-top:13px;border-radius:14px;padding:13px 14px;font-weight:750;line-height:1.45}
.alertBox.show{display:block}
.alertBox.info{background:#10243a;border:1px solid #285c8d;color:#b9dcff}
.alertBox.warn{background:#392b12;border:1px solid #8a6520;color:#ffe0a3}
.alertBox.danger{background:#3a171c;border:1px solid #91414b;color:#ffc2c8}
@media(max-width:720px){.systemStrip{grid-template-columns:repeat(2,minmax(0,1fr))}}
'''
if '</style>' not in text:
    raise RuntimeError("style closing tag not found")
text = text.replace('</style>', css + '\n</style>', 1)

overview_anchor = '''<div class="grid four"><div class="stat metric"><span>Độ ẩm</span><b id="hum" class="accent">--%</b></div><div class="stat metric"><span>Tốc độ gió</span><b id="wind" class="accent">--</b></div><div class="stat metric"><span>Chất lượng khí</span><b id="aqi" class="good">--</b></div><div class="stat metric"><span>Trang đang hiện</span><b id="page">--</b></div></div>'''
overview_extra = overview_anchor + '''
<div id="weatherAlert" class="alertBox"></div>
<div class="systemStrip">
  <div class="systemPill"><span>Chế độ giao diện</span><b id="nightState">--</b></div>
  <div class="systemPill"><span>Heap trống</span><b id="heapQuick">--</b></div>
  <div class="systemPill"><span>Firmware</span><b id="fwQuick">--</b></div>
  <div class="systemPill"><span>Mochi đang phản ứng</span><b id="mochiQuick">--</b></div>
</div>'''
if overview_anchor not in text:
    raise RuntimeError("overview metrics block not found")
text = text.replace(overview_anchor, overview_extra, 1)

helpers = r'''
function weatherIconFor(code,isNight){
 code=Number(code);
 if(code===0)return isNight?'🌙':'☀️';
 if(code===1)return isNight?'🌙':'🌤️';
 if(code===2)return isNight?'☁️':'⛅';
 if(code===3)return '☁️';
 if(code===45||code===48)return '🌫️';
 if((code>=51&&code<=57)||(code>=61&&code<=67)||(code>=80&&code<=82))return '🌧️';
 if(code>=71&&code<=77)return '🌨️';
 if(code>=85&&code<=86)return '❄️';
 if(code>=95)return '⛈️';
 return '🌤️';
}
function mochiLabel(v){
 return ({1:'Vui',2:'Buồn',3:'Buồn ngủ',4:'Ngạc nhiên',5:'Giận',6:'Yêu thích'})[Number(v)]||'Tự động';
}
function applyNightMode(){
 const h=new Date().getHours(),night=h>=19||h<6;
 document.body.classList.toggle('night',night);
 if($('nightState'))$('nightState').textContent=night?'Đêm tự động':'Ban ngày';
 return night;
}
function renderWeatherAlert(s){
 const box=$('weatherAlert');if(!box)return;
 const notes=[];let level='info';
 const code=Number(s.weather_code||0),aq=Number(s.aqi),temp=Number(s.temperature);
 if(aq>=151){notes.push('AQI xấu: nên hạn chế hoạt động ngoài trời và đóng cửa sổ.');level='danger'}
 else if(aq>=101){notes.push('AQI kém: người nhạy cảm nên giảm thời gian ngoài trời.');level='warn'}
 if(code>=95){notes.push('Có dông/sét: tránh khu vực trống và thiết bị điện ngoài trời.');level='danger'}
 else if((code>=51&&code<=67)||(code>=80&&code<=82)){notes.push('Có mưa: nhớ mang áo mưa hoặc ô.');if(level==='info')level='warn'}
 if(temp>=35){notes.push('Nhiệt độ cao: bổ sung nước và tránh nắng gắt.');if(level==='info')level='warn'}
 box.className='alertBox'+(notes.length?' show '+level:'');
 box.textContent=notes.join(' ');
}
'''
needle='async function refresh(){try{'
if needle not in text:
    raise RuntimeError("refresh function not found")
text=text.replace(needle,helpers+'\n'+needle,1)

refresh_old = '''$('online').style.background='#20c88a';$('cityView').textContent=s.city;$('temp').textContent=s.temperature.toFixed(1);$('desc').textContent=s.weather;$('hum').textContent=s.humidity+'%';$('wind').textContent=s.wind_speed.toFixed(1)+' km/h';$('aqi').textContent=s.aqi;'''
refresh_new = '''$('online').style.background='#20c88a';const night=applyNightMode();$('cityView').textContent=s.city;$('temp').textContent=s.temperature.toFixed(1);$('desc').textContent=s.weather;$('hum').textContent=s.humidity+'%';$('wind').textContent=s.wind_speed.toFixed(1)+' km/h';$('aqi').textContent=s.aqi;if($('weatherIcon')){$('weatherIcon').textContent=weatherIconFor(s.weather_code,night);$('weatherIcon').title=s.weather+' · mã '+s.weather_code}renderWeatherAlert(s);'''
if refresh_old not in text:
    raise RuntimeError("refresh weather block not found")
text=text.replace(refresh_old,refresh_new,1)

status_old = '''$('heap').textContent=Math.round(s.free_heap/1024)+' KB';$('firmware').textContent=s.firmware||'--';'''
status_new = '''$('heap').textContent=Math.round(s.free_heap/1024)+' KB';if($('heapQuick'))$('heapQuick').textContent=Math.round(s.free_heap/1024)+' KB';if($('fwQuick'))$('fwQuick').textContent=s.firmware||'--';if($('mochiQuick'))$('mochiQuick').textContent=mochiLabel(s.mochi_resolved);$('firmware').textContent=s.firmware||'--';'''
if status_old not in text:
    raise RuntimeError("system status refresh block not found")
text=text.replace(status_old,status_new,1)

text, count = re.subn(
    r'static const char\* FW_VERSION = "8\.0\.\d+";',
    'static const char* FW_VERSION = "8.0.24";',
    text,
    count=1,
)
if count != 1:
    raise RuntimeError("FW_VERSION not found")

checks = [
    'String(weatherCode)',
    'String(resolvedMochiExpression())',
    'id="weatherIcon"',
    'function weatherIconFor(',
    'function renderWeatherAlert(',
    'body.night',
    'id="weatherAlert"',
    'id="heapQuick"',
    'static const char* FW_VERSION = "8.0.24";',
]
missing=[x for x in checks if x not in text]
if missing:
    raise RuntimeError("V8.0.24 verification failed: "+", ".join(missing))
if text.count('R"HTML(') != text.count(')HTML"'):
    raise RuntimeError("unbalanced raw HTML strings")

SOURCE.write_text(text, encoding="utf-8")
print(f"Applied V8.0.24 smart status upgrade ({len(text)} bytes)")
