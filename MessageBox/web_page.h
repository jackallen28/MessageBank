// The settings and hardware-test page, served at http://192.168.4.1 (setup Wi-Fi) or at the
// device's address on your home network (http://messagebox.local).
#pragma once
static const char WEB_PAGE[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MessageBox</title>
<style>
:root{--bg:#f4f1ee;--card:#fff;--ink:#1d1b1a;--mute:#6f6a66;--line:#e2ddd8;--acc:#d6336c;--ok:#2b8a3e;--bad:#c92a2a;--field:#faf8f6}
@media (prefers-color-scheme:dark){:root{--bg:#161413;--card:#211e1c;--ink:#f1ece8;--mute:#a49d97;--line:#38332f;--acc:#f06595;--ok:#69db7c;--bad:#ff8787;--field:#1a1816}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{padding:18px 16px 6px;max-width:760px;margin:auto}h1{margin:0;font-size:22px}h1 span{color:var(--acc)}
.sub{color:var(--mute);font-size:13px}
main{max-width:760px;margin:auto;padding:8px 16px 40px;display:grid;gap:14px}
section{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px 16px}
h2{font-size:16px;margin:0 0 10px}label{display:block;font-size:13px;color:var(--mute);margin:10px 0 4px}
input,select,textarea{width:100%;padding:9px 10px;border:1px solid var(--line);border-radius:9px;background:var(--field);color:var(--ink);font:inherit}
textarea{min-height:70px;resize:vertical}input[type=color]{padding:2px;height:40px}
.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}@media(max-width:520px){.row{grid-template-columns:1fr}}
button{font:inherit;border:1px solid var(--line);background:var(--field);color:var(--ink);padding:8px 12px;border-radius:9px;cursor:pointer;margin:4px 4px 0 0}
button.pri{background:var(--acc);border-color:var(--acc);color:#fff}button.bad{color:var(--bad)}
button:active{transform:translateY(1px)}
.kv{display:grid;grid-template-columns:auto 1fr;gap:3px 14px;font-size:14px}.kv b{font-weight:500;color:var(--mute)}
.pill{display:inline-block;padding:1px 9px;border-radius:99px;font-size:12px;border:1px solid var(--line)}
.ok{color:var(--ok)}.bad{color:var(--bad)}
.nets{max-height:180px;overflow:auto;border:1px solid var(--line);border-radius:9px;margin-top:6px;display:none}
.nets div{padding:7px 10px;border-bottom:1px solid var(--line);cursor:pointer;display:flex;justify-content:space-between}
.nets div:hover{background:var(--field)}
.enc{display:flex;gap:16px;align-items:center;font-size:14px}.dial{width:64px;height:64px;border-radius:50%;border:3px solid var(--line);position:relative}
.dial i{position:absolute;left:29px;top:4px;width:4px;height:24px;background:var(--acc);border-radius:2px;transform-origin:2px 26px}
.dot{width:14px;height:14px;border-radius:50%;background:var(--line);display:inline-block;vertical-align:middle}.dot.on{background:var(--acc)}
pre{background:var(--field);border:1px solid var(--line);border-radius:9px;padding:8px;font-size:12px;max-height:200px;overflow:auto;white-space:pre-wrap;margin:6px 0 0}
.hint{font-size:12px;color:var(--mute);margin-top:6px}
#toast{position:fixed;left:50%;bottom:18px;transform:translateX(-50%);background:var(--ink);color:var(--bg);padding:8px 14px;border-radius:9px;font-size:14px;opacity:0;transition:.3s;pointer-events:none}
</style></head><body>
<header><h1>Message<span>Box</span></h1><div class="sub" id="sub">Loading...</div></header>
<main>

<section><h2>Status</h2><div class="kv" id="kv"></div></section>

<section><h2>1. Home Wi-Fi</h2>
<label>Network name</label><input id="ssid" autocomplete="off">
<button onclick="scan()">Scan for networks</button><div class="nets" id="nets"></div>
<label>Password</label><input id="wpass" type="password" placeholder="(unchanged)">
<div class="hint">The device joins this network to read the Gmail inbox. 2.4 GHz networks only.</div>
</section>

<section><h2>2. Gmail inbox</h2>
<label>Gmail address the device reads</label><input id="guser" type="email" autocomplete="off" placeholder="our.messagebox@gmail.com">
<label>App password (16 letters)</label><input id="gpass" type="password" placeholder="(unchanged)">
<label>Only show mail from (comma separated, empty = anyone)</label><input id="allow" placeholder="friend@example.com, me@example.com">
<div class="row"><div><label>Check every (seconds)</label><input id="poll" type="number" min="20" max="3600"></div>
<div><label>Time zone (POSIX)</label><input id="tz"></div></div>
<div class="hint">Use a separate Gmail account for the device. Turn on 2-Step Verification, then make an
app password at myaccount.google.com/apppasswords. Your normal Google password does not work here.</div>
<button onclick="api('/api/checkmail',{}).then(t=>toast(t))">Check mail now</button>
</section>

<section><h2>3. Device</h2>
<div class="row"><div><label>Notification colour</label><input id="color" type="color"></div>
<div><label>LED brightness (max 255)</label><input id="bright" type="number" min="5" max="255"></div></div>
<div class="row"><div><label>Vibration strength (0-255)</label><input id="vibe" type="number" min="0" max="255"></div>
<div><label>Screen orientation</label><select id="rot"><option value="0">Portrait</option><option value="1">Landscape</option><option value="2">Portrait, flipped</option><option value="3">Landscape, flipped</option></select></div></div>
<div class="row"><div><label>Device name (for messagebox.local)</label><input id="name"></div>
<div><label>Setup Wi-Fi password (8+ characters)</label><input id="appass" type="password" placeholder="(unchanged)"></div></div>
<p><button class="pri" onclick="save()">Save settings and restart</button></p>
<div class="hint">Passwords are never shown again. Leave a password box empty to keep the saved one.</div>
</section>

<section><h2>Test the hardware</h2>
<label>LED ring</label>
<input id="tcol" type="color" value="#ff3366" style="max-width:120px">
<div><button onclick="led('solid')">Solid</button><button onclick="led('breathe')">Breathe</button><button onclick="led('spin')">Spin</button>
<button onclick="led('rainbow')">Rainbow</button><button onclick="led('chase')">Test each LED</button><button onclick="led('off')">Off</button></div>
<label>Vibration motor</label>
<div><button onclick="vib('short')">Short</button><button onclick="vib('long')">Long</button><button onclick="vib('heart')">Heartbeat</button><button onclick="vib('ramp')">Ramp (PWM)</button></div>
<label>E-paper (a full refresh takes about 3 seconds)</label>
<input id="etext" value="Hello from MessageBox!">
<div><button onclick="api('/api/test',{what:'epd',text:v('etext')}).then(toast)">Show text</button><button onclick="api('/api/test',{what:'epdpattern'}).then(toast)">Test pattern</button><button onclick="api('/api/test',{what:'epdclear'}).then(toast)">Clear</button></div>
<label>Rotary encoder (turn it and press it)</label>
<div class="enc"><div class="dial"><i id="dial"></i></div><div>Position: <b id="encpos">0</b><br>Button: <span class="dot" id="btn"></span> <span id="presses"></span></div></div>
<label>Pretend a message arrived</label>
<div class="row"><input id="ffrom" value="Test"><input id="fsub" value="Thinking of you"></div>
<textarea id="fbody">This is a pretend message, so you can test the screen, lights and buzz before Gmail is set up.</textarea>
<button onclick="api('/api/message',{from:v('ffrom'),subject:v('fsub'),body:v('fbody')}).then(toast)">Show as a new message</button>
</section>

<section><h2>Log</h2><pre id="log"></pre></section>

<section><h2>Maintenance</h2>
<button onclick="api('/api/leavesetup',{}).then(toast)">Leave setup mode</button>
<button onclick="if(confirm('Restart the device?'))api('/api/reboot',{}).then(toast)">Restart</button>
<button class="bad" onclick="if(confirm('Erase every setting, including Wi-Fi and Gmail?'))api('/api/factory',{}).then(toast)">Factory reset</button>
<div class="hint">On the device: hold the knob down for 5 seconds to open the setup Wi-Fi again.</div>
</section>
</main><div id="toast"></div>
<script>
const $=id=>document.getElementById(id),v=id=>$(id).value;
function toast(t){const e=$('toast');e.textContent=t;e.style.opacity=1;clearTimeout(e.t);e.t=setTimeout(()=>e.style.opacity=0,2600)}
function api(url,data){return fetch(url,{method:'POST',body:new URLSearchParams(data)}).then(r=>r.text()).catch(()=>'No answer from the device')}
function led(mode){api('/api/test',{what:'led',mode,color:v('tcol')}).then(toast)}
function vib(p){api('/api/test',{what:'vibe',pattern:p}).then(toast)}
let loaded=false;
function fill(s){
  for(const[k,id]of[['ssid','ssid'],['gmailUser','guser'],['allowFrom','allow'],['pollSec','poll'],['tz','tz'],['color','color'],['brightness','bright'],['vibe','vibe'],['rotation','rot'],['name','name']])$(id).value=s[k];
  $('wpass').placeholder=s.hasWifiPass?'(saved - leave empty to keep)':'(none saved)';
  $('gpass').placeholder=s.hasGmailPass?'(saved - leave empty to keep)':'(none saved)';
}
function save(){
  const d={ssid:v('ssid'),wpass:v('wpass'),guser:v('guser'),gpass:v('gpass'),allow:v('allow'),poll:v('poll'),tz:v('tz'),color:v('color'),bright:v('bright'),vibe:v('vibe'),rot:v('rot'),name:v('name'),appass:v('appass')};
  api('/api/settings',d).then(t=>{toast(t);loaded=false})
}
function scan(){
  const n=$('nets');n.style.display='block';n.innerHTML='<div>Scanning...</div>';
  const poll=()=>fetch('/api/scan').then(r=>r.json()).then(j=>{
    if(j.busy){setTimeout(poll,1200);return}
    n.innerHTML='';if(!j.nets.length)n.innerHTML='<div>No networks found</div>';
    j.nets.forEach(x=>{const d=document.createElement('div');d.innerHTML='<span></span><span class="sub">'+x.rssi+' dBm'+(x.open?' open':'')+'</span>';
      d.firstChild.textContent=x.ssid;d.onclick=()=>{$('ssid').value=x.ssid;n.style.display='none';$('wpass').focus()};n.appendChild(d)})
  }).catch(()=>setTimeout(poll,1500));poll()
}
function esc(s){return String(s).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]))}
function tick(){fetch('/api/status').then(r=>r.json()).then(s=>{
  if(!loaded){fill(s.settings);loaded=true}
  $('sub').textContent=s.setup?'Setup mode - Wi-Fi '+s.apSsid+' is on':'Online at '+s.ip;
  const wifi=s.staOk?'<span class="ok">connected</span> to '+esc(s.staSsid)+' ('+s.rssi+' dBm), '+s.ip:(s.settings.ssid?'<span class="bad">not connected</span> ('+esc(s.staState)+')':'not set up yet');
  const mail=s.mail.ok===null?'not checked yet':(s.mail.ok?'<span class="ok">OK</span>':'<span class="bad">'+esc(s.mail.error)+'</span>')+(s.mail.ago>=0?', '+s.mail.ago+' s ago':'');
  $('kv').innerHTML='<b>Wi-Fi</b><span>'+wifi+'</span><b>Gmail</b><span>'+mail+'</span><b>Messages</b><span>'+s.msgCount+' kept'+(s.unread?', <span class="pill">unread</span>':'')+'</span><b>Time</b><span>'+esc(s.time)+'</span><b>Device</b><span>up '+s.uptime+' s, '+s.heap+' bytes free</span>';
  $('encpos').textContent=s.enc;$('dial').style.transform='rotate('+(s.enc*18)+'deg)';
  $('btn').className='dot'+(s.btn?' on':'');$('presses').textContent=s.presses+' press(es)';
  $('log').textContent=s.log;
}).catch(()=>{$('sub').textContent='Device not answering...'}).finally(()=>setTimeout(tick,1000))}
tick();
</script></body></html>)rawliteral";
