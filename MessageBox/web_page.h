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
h2{font-size:16px;margin:0 0 10px}h3{font-size:14px;margin:14px 0 0;color:var(--mute);font-weight:600}
label{display:block;font-size:13px;color:var(--mute);margin:10px 0 4px}
label.ck{display:flex;gap:8px;align-items:center;color:var(--ink);font-size:14px}label.ck input{width:auto}
input,select,textarea{width:100%;padding:9px 10px;border:1px solid var(--line);border-radius:9px;background:var(--field);color:var(--ink);font:inherit}
textarea{min-height:70px;resize:vertical}input[type=color]{padding:2px;height:40px}
.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}.row3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px}
@media(max-width:520px){.row{grid-template-columns:1fr}.row3{grid-template-columns:1fr 1fr}}
button{font:inherit;border:1px solid var(--line);background:var(--field);color:var(--ink);padding:8px 12px;border-radius:9px;cursor:pointer;margin:6px 4px 0 0}
button.pri{background:var(--acc);border-color:var(--acc);color:#fff}button.bad{color:var(--bad)}
button:active{transform:translateY(1px)}
.kv{display:grid;grid-template-columns:auto 1fr;gap:3px 14px;font-size:14px}.kv b{font-weight:500;color:var(--mute)}
.pill{display:inline-block;padding:1px 9px;border-radius:99px;font-size:12px;border:1px solid var(--line)}
.ok{color:var(--ok)}.bad{color:var(--bad)}
.nets{max-height:180px;overflow:auto;border:1px solid var(--line);border-radius:9px;margin-top:6px;display:none}
.nets div{padding:7px 10px;border-bottom:1px solid var(--line);cursor:pointer;display:flex;justify-content:space-between}
.enc{display:flex;gap:16px;align-items:center;font-size:14px}.dial{width:64px;height:64px;border-radius:50%;border:3px solid var(--line);position:relative}
.dial i{position:absolute;left:27px;top:4px;width:4px;height:24px;background:var(--acc);border-radius:2px;transform-origin:2px 26px}
.dot{width:14px;height:14px;border-radius:50%;background:var(--line);display:inline-block;vertical-align:middle}.dot.on{background:var(--acc)}
pre{background:var(--field);border:1px solid var(--line);border-radius:9px;padding:8px;font-size:12px;max-height:200px;overflow:auto;white-space:pre-wrap;margin:6px 0 0}
.hint{font-size:12px;color:var(--mute);margin-top:6px}
.msg{border-top:1px solid var(--line);padding:8px 0;display:flex;gap:10px;align-items:flex-start}.msg div{flex:1;min-width:0}
.msg p{margin:2px 0 0;font-size:13px;color:var(--mute);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.save{position:sticky;bottom:10px;z-index:2;text-align:right}.save button{box-shadow:0 4px 14px rgba(0,0,0,.18)}
#toast{position:fixed;left:50%;bottom:64px;transform:translateX(-50%);background:var(--ink);color:var(--bg);padding:8px 14px;border-radius:9px;font-size:14px;opacity:0;transition:.3s;pointer-events:none;z-index:3}
</style></head><body>
<header><h1>Message<span>Box</span></h1><div class="sub" id="sub">Loading...</div></header>
<main>

<section><h2>Status</h2><div class="kv" id="kv"></div></section>

<section><h2>1. Home Wi-Fi</h2>
<label>Network name</label><input id="ssid" class="s" autocomplete="off" oninput="$('wopen').checked=false;$('wpass').disabled=false">
<button id="scanButton" onclick="scan()">Scan for networks</button><div class="nets" id="nets"></div>
<label>Password</label><input id="wpass" type="password" placeholder="(unchanged)">
<label><input id="wopen" type="checkbox" onchange="$('wpass').disabled=this.checked"> No password (open network)</label>
<div class="hint">The device joins this network to read the Gmail inbox. 2.4 GHz networks only.</div>
</section>

<section><h2>2. Gmail inbox</h2>
<label>Gmail address the device reads</label><input id="guser" class="s" type="email" autocomplete="off" placeholder="our.messagebox@gmail.com">
<label>App password (16 letters)</label><input id="gpass" type="password" placeholder="(unchanged)">
<label>Only show mail from (comma separated, empty = anyone)</label><input id="allow" class="s" placeholder="friend@example.com, me@example.com">
<div class="row"><div><label>Check every (seconds)</label><input id="poll" class="s" type="number" min="20" max="3600"></div>
<div><label>Time zone (POSIX)</label><input id="tz" class="s"></div></div>
<div class="hint">Use a separate Gmail account for the device. Turn on 2-Step Verification, then make an
app password at myaccount.google.com/apppasswords. Your normal Google password does not work here.</div>
<button onclick="post('/api/checkmail',{})">Check mail now</button>
</section>

<section><h2>3. Lights for a new message</h2>
<div class="row"><div><label>Style</label><select id="lstyle" class="s">
<option value="0">Comet (colour 1 fading to colour 2)</option><option value="1">Twin comets (colour 1 and colour 2)</option>
<option value="2">Rainbow comet</option><option value="3">Chase (every third LED)</option><option value="4">Comet + flash each lap</option></select></div>
<div><label>Brightness (5-255)</label><input id="bright" class="s" type="number" min="5" max="255"></div></div>
<div class="row"><div><label>Colour 1</label><input id="col1" class="s" type="color"></div><div><label>Colour 2</label><input id="col2" class="s" type="color"></div></div>
<div class="row3"><div><label>One lap (ms, lower = faster)</label><input id="lspeed" class="s" type="number" min="200" max="10000"></div>
<div><label>Tail length (LEDs)</label><input id="ltail" class="s" type="number" min="1" max="12"></div><div></div></div>
<label class="ck"><input id="ledson" class="s" type="checkbox"> Lights on (also on the box's settings screen)</label>
<div class="row"><div><label>While a message waits, spin for (seconds)</label><input id="spinsec" class="s" type="number" min="0" max="3600"></div>
<div><label>...every (minutes, 0 = all the time)</label><input id="spinevery" class="s" type="number" min="0" max="1440"></div></div>
<button onclick="test('lights')">Try these lights</button>
<div class="hint">Keep brightness around 60: all 12 LEDs at full white draw about 0.7 A from USB.</div>
</section>

<section><h2>4. Haptics</h2>
<label class="ck"><input id="hknob" class="s" type="checkbox"> Tick when the knob is turned or pressed</label>
<div class="row"><div><label>Tick strength (0-255)</label><input id="hpow" class="s" type="number" min="0" max="255"></div>
<div><label>Tick length (ms)</label><input id="hms" class="s" type="number" min="3" max="100"></div></div>
<button onclick="test('tick')">Try ticks</button><button onclick="test('shutdown')">Try power-down buzz</button>
<h3>New message: tick tick tick tick tick TICK</h3>
<label class="ck"><input id="nvibe" class="s" type="checkbox"> Buzz when a message arrives</label>
<div class="row3"><div><label>Number of ticks</label><input id="nticks" class="s" type="number" min="1" max="12"></div>
<div><label>Time between (ms)</label><input id="ngap" class="s" type="number" min="30" max="3000"></div>
<div><label>Buzz again every (min, 0 = off)</label><input id="nrem" class="s" type="number" min="0" max="1440"></div></div>
<div class="row"><div><label>First tick length (ms)</label><input id="nms0" class="s" type="number" min="5" max="1000"></div>
<div><label>Last TICK length (ms)</label><input id="nms1" class="s" type="number" min="5" max="1000"></div></div>
<div class="row"><div><label>First tick strength (0-255)</label><input id="npow0" class="s" type="number" min="0" max="255"></div>
<div><label>Last TICK strength (0-255)</label><input id="npow1" class="s" type="number" min="0" max="255"></div></div>
<button onclick="test('notify')">Try the new-message buzz</button>
<div class="hint">Each tick is a bit longer and stronger than the one before, so the last one stands out.</div>
</section>

<section><h2>5. Bedtime</h2>
<label class="ck"><input id="bed" class="s" type="checkbox"> Quiet time: no lights or buzz for new messages</label>
<div class="row"><div><label>From</label><input id="bedfrom" class="s" type="time" step="1800"></div>
<div><label>Until</label><input id="bedto" class="s" type="time" step="1800"></div></div>
<div class="hint">Messages still arrive and show on the screen. The box needs the internet time for this, so it starts working once it's online. You can also change it on the box: hold the knob for 20 seconds.</div>
</section>

<section><h2>6. Nicknames</h2>
<div class="hint" style="margin-top:0">Show a name of your choice instead of the one in the email, on the envelope cards and while reading.</div>
<div id="nickrows"></div>
<input id="nicks" class="s" type="hidden">
<button onclick="addNick('','')">Add a nickname</button>
<div id="nicksugg" class="hint"></div>
</section>

<section><h2>7. Screen and knob</h2>
<div class="row"><div><label>Screen orientation</label><select id="rot" class="s"><option value="0">Portrait</option><option value="1">Landscape</option><option value="2">Portrait, flipped</option><option value="3">Landscape, flipped</option></select></div>
</div>
<div class="hint">Fast updates while reading. Full refreshes happen at startup, when clearing the screen, and for setup, orientation changes or a requested full redraw. There is no refresh-count timer.</div>
<label class="ck"><input id="krev" class="s" type="checkbox"> Reverse the knob (if clockwise scrolls up)</label>
<label class="ck"><input id="epdwire" class="s" type="checkbox"> Screen BUSY and RESET are wired (BUSY to GPIO4, RES to GPIO8)</label>
<div class="hint">Untick to run the screen with neither wire: tie the screen's RES to 3V3 and leave BUSY unconnected. Each update then takes a little longer (fixed waits). Restarts the box.</div>
<div class="row"><div><label>Screen SPI speed</label><select id="epdspi" class="s"><option value="1">1 MHz (tested)</option><option value="4">4 MHz (faster refresh)</option><option value="10">10 MHz (fastest; short wires)</option></select></div><div></div></div>
<div class="row"><div><label>Device name (for messagebox.local)</label><input id="name" class="s"></div>
<div><label>Setup Wi-Fi password (8+ characters)</label><input id="appass" type="password" placeholder="(unchanged)"></div></div>
<div class="hint">Changing Wi-Fi, the name, the time zone or the orientation restarts the box. Everything else applies straight away.</div>
</section>

<div class="save"><button class="pri" onclick="save()">Save settings</button></div>

<section><h2>Messages</h2><div id="msgs" class="hint">Loading...</div>
<button class="bad" onclick="if(confirm('Delete every message on the box?'))post('/api/msgdel',{id:'all'}).then(loadMsgs)">Delete all</button></section>

<section><h2>Test the hardware</h2>
<label>Pretend a message arrived (the real flow: lights, buzz, envelope)</label>
<div class="row"><input id="ffrom" value="Test"><input id="fsub" value="Thinking of you"></div>
<input id="faddr" type="email" placeholder="Reply address (optional: put your own to try a quick reply)" style="margin-top:8px">
<textarea id="fbody">This is a pretend message, so you can test the screen, lights and buzz before Gmail is set up. Turn the knob clockwise to scroll down, keep turning for the quick replies, press to send one or to finish. Hold the knob for 2 seconds to delete.</textarea>
<button class="pri" onclick="post('/api/message',{from:v('ffrom'),subject:v('fsub'),body:v('fbody'),addr:v('faddr')}).then(loadMsgs)">Send to the box</button>
<label>Rotary encoder (turn it and press it)</label>
<div class="enc"><div class="dial"><i id="dial"></i></div><div>Position: <b id="encpos">0</b><br>Button: <span class="dot" id="btn"></span> <span id="presses"></span></div></div>
<label>LED ring</label>
<div><button onclick="test('chase')">Test each LED</button><button onclick="test('solid')">Colour 1</button><button onclick="test('ledsoff')">Off</button></div>
<label>E-paper screens (back to normal after 12 s)</label>
<div><button onclick="test('arrive')">Hummingbird</button><button onclick="test('envelope',{count:3})">Envelope</button><button onclick="test('react')">Quick replies</button><button onclick="test('nomsg')">No new messages</button><button onclick="test('happy')">Happy envelope</button><button onclick="test('pattern')">Test pattern</button></div>
<div class="row" style="margin-top:6px"><input id="etext" value="Hello from MessageBox!"><button onclick="test('epd',{text:v('etext')})" style="margin:0">Show text</button></div>
</section>

<section><h2>Log</h2><pre id="log"></pre></section>

<section><h2>Firmware update</h2>
<label>Public GitHub firmware download link</label>
<input id="fwurl" type="url" placeholder="https://github.com/owner/repo/releases/download/version/MessageBox-main-ota.bin">
<button id="fwinstall" class="pri" onclick="installFirmware()" disabled>Install and restart</button>
<progress id="fwprogress" max="100" value="0" style="width:100%;margin-top:10px"></progress>
<div id="fwstate" class="hint">Connect home Wi-Fi to install an update.</div>
<div class="hint">Paste a public GitHub release or raw download link to the ESP32-C3 OTA .bin. An OTA update keeps Wi-Fi settings and saved messages.</div>
</section>

<section><h2>Maintenance</h2>
<button onclick="post('/api/leavesetup',{})">Leave setup mode</button>
<button onclick="if(confirm('Restart the device?'))post('/api/reboot',{})">Restart</button>
<button class="bad" onclick="if(confirm('Erase every setting and message, including Wi-Fi and Gmail?'))post('/api/factory',{})">Factory reset</button>
<div class="hint">On the device: hold the knob down for 20 seconds (the ring fills up) for its settings screen. "Change Wi-Fi" there opens the setup Wi-Fi again.</div>
</section>
</main><div id="toast"></div>
<script>
const $=id=>document.getElementById(id),v=id=>$(id).value;
function toast(t){const e=$('toast');e.textContent=t;e.style.opacity=1;clearTimeout(e.t);e.t=setTimeout(()=>e.style.opacity=0,2800)}
function post(url,data){return fetch(url,{method:'POST',body:new URLSearchParams(data)}).then(r=>r.text()).catch(()=>'No answer from the device').then(t=>{toast(t);return t})}
function form(){const d={};document.querySelectorAll('.s').forEach(e=>d[e.id]=e.type=='checkbox'?(e.checked?'1':'0'):e.value);return d}
function test(what,extra){post('/api/test',Object.assign(form(),{what},extra||{}))}
function addNick(addr,name){
  const d=document.createElement('div');d.className='row nick';d.style.marginTop='6px';
  d.innerHTML='<input type="email" placeholder="their@email.com"><div style="display:flex;gap:6px"><input placeholder="Nickname"><button class="bad" style="margin:0" title="Remove">&times;</button></div>';
  const [a,n]=d.querySelectorAll('input');a.value=addr;n.value=name;
  d.querySelector('button').onclick=()=>{d.remove();suggest()};
  $('nickrows').appendChild(d);if(!addr)a.focus();suggest()
}
function nickText(){return[...document.querySelectorAll('.nick')].map(d=>{const[a,n]=d.querySelectorAll('input');return a.value.trim()&&n.value.trim()?a.value.trim().toLowerCase()+'='+n.value.trim().replace(/[\r\n=]/g,' '):''}).filter(Boolean).join('\n')}
function fillNicks(t){$('nickrows').innerHTML='';(t||'').split('\n').forEach(l=>{const i=l.indexOf('=');if(i>0)addNick(l.slice(0,i),l.slice(i+1))})}
let senders=[];
function suggest(){
  const have=new Set([...document.querySelectorAll('.nick input[type=email]')].map(e=>e.value.trim().toLowerCase()));
  const s=senders.filter(a=>!have.has(a));const b=$('nicksugg');b.innerHTML=s.length?'Senders without one: ':'';
  s.forEach(a=>{const x=document.createElement('button');x.textContent=a;x.onclick=()=>addNick(a,'');b.appendChild(x)})
}
async function save(){
  if(!loaded){toast('Wait for settings to load');return}
  $('nicks').value=nickText();
  const d=form();for(const k of['wpass','gpass','appass'])d[k]=v(k);
  d.wopen=$('wopen').checked?'1':'0';
  try {
    const r=await fetch('/api/settings',{method:'POST',body:new URLSearchParams(d)});
    toast(await r.text());
    if(r.ok){loaded=false;for(const k of['wpass','gpass','appass'])$(k).value=''}
  } catch { toast('No answer from the device') }
}
let loaded=false,firmwareToken='';
function installFirmware(){const url=v('fwurl').trim();if(!url){toast('Paste a GitHub application firmware link first');return}post('/api/update',{url,token:firmwareToken})}
function fill(s){
  document.querySelectorAll('.s').forEach(e=>{if(!(e.id in s))return;if(e.type=='checkbox')e.checked=s[e.id];else e.value=s[e.id]});
  $('wopen').checked=!!s.ssid&&!s.hasWifiPass;
  $('wpass').disabled=$('wopen').checked;
  $('wpass').placeholder=s.hasWifiPass?'(leave empty to keep for saved network)':'(none saved)';
  $('gpass').placeholder=s.hasGmailPass?'(saved - leave empty to keep)':'(none saved)';
  fillNicks(s.nicks);
}
function scan(){
  const n=$('nets'),b=$('scanButton');b.disabled=true;n.style.display='block';n.textContent='Scanning...';
  const poll=()=>fetch('/api/scan').then(r=>r.json()).then(j=>{
    if(j.busy){setTimeout(poll,1200);return}
    b.disabled=false;
    if(j.error){n.textContent=j.error;return}
    n.textContent=j.nets.length?'':'No networks found';
    j.nets.forEach(x=>{const d=document.createElement('div');d.innerHTML='<span></span><span class="sub">'+x.rssi+' dBm'+(x.open?' open':'')+'</span>';
      d.firstChild.textContent=x.ssid;d.onclick=()=>{
        if(v('ssid')!==x.ssid)$('wpass').value='';
        $('ssid').value=x.ssid;$('wopen').checked=x.open;$('wpass').disabled=x.open;
        n.style.display='none';if(!x.open)$('wpass').focus()
      };n.appendChild(d)})
  }).catch(()=>{b.disabled=false;n.textContent='Wi-Fi scan unavailable. Try again shortly.'});poll()
}
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
function loadMsgs(){fetch('/api/messages').then(r=>r.json()).then(list=>{
  const box=$('msgs');if(!list.length){box.textContent='No messages on the box.';return}
  senders=[...new Set(list.map(m=>(m.addr||'').toLowerCase()).filter(Boolean))];suggest();
  box.className='';box.innerHTML=list.map(m=>'<div class="msg"><div><span class="pill">'+m.state+'</span> <b>'+esc(m.subject||'(no subject)')+'</b> from '+esc(m.from)+
    ' <span class="sub">'+esc(m.when)+'</span>'+(m.reaction?' <span class="pill">replied: '+esc(m.reaction)+(m.reply&&m.reply!='sent'?' ('+m.reply+')':'')+'</span>':'')+'<p>'+esc(m.body)+'</p></div><button class="bad" onclick="post(\'/api/msgdel\',{id:'+m.id+'}).then(loadMsgs)">Delete</button></div>').join('')
}).catch(()=>{})}
let lastCounts='';
function tick(){fetch('/api/status').then(r=>r.json()).then(s=>{
  if(!loaded){fill(s.settings);loaded=true}
  $('sub').textContent=s.setup?'Setup mode - Wi-Fi '+s.apSsid+' is on':'Online at '+s.ip;
  const wifi=s.staOk?'<span class="ok">connected</span> to '+esc(s.staSsid)+' ('+s.rssi+' dBm), '+s.ip:(s.settings.ssid?'<span class="bad">not connected</span> ('+esc(s.staState)+')':'not set up yet');
  const mail=s.mail.busy?'checking...':(s.mail.ok===null?'not checked yet':(s.mail.ok?'<span class="ok">OK</span>':'<span class="bad">'+esc(s.mail.error)+'</span>')+(s.mail.ago>=0?', '+s.mail.ago+' s ago':''));
  $('kv').innerHTML='<b>Wi-Fi</b><span>'+wifi+'</span><b>Gmail</b><span>'+mail+'</span><b>Box</b><span>'+esc(s.ui)+'</span><b>Messages</b><span>'+s.newCount+' new, '+s.savedCount+' saved</span><b>Screen</b><span>last '+(s.lastRefreshFull?'full':'fast')+' refresh '+s.refreshMs+' ms ('+s.fullRefreshes+' full, '+s.partialRefreshes+' fast)</span><b>Time</b><span>'+esc(s.time)+'</span><b>Device</b><span>'+esc(s.firmware)+'; up '+s.uptime+' s, '+s.heap+' bytes free</span>';
  $('encpos').textContent=s.enc;$('dial').style.transform='rotate('+(s.enc*30)+'deg)';
  $('btn').className='dot'+(s.btn?' on':'');$('presses').textContent=s.presses+' press(es)';
  $('log').textContent=s.log;
  firmwareToken=s.update.token;
  $('fwinstall').disabled=!s.staOk||s.update.busy||s.update.restarting;
  const percent=s.update.total?Math.floor(s.update.written*100/s.update.total):0;
  $('fwprogress').value=percent;
  $('fwstate').textContent=s.update.message+(s.update.total?' ('+percent+'%)':'');
  const c=s.newCount+'/'+s.savedCount;if(c!=lastCounts){lastCounts=c;loadMsgs()}
}).catch(()=>{$('sub').textContent='Device not answering...'}).finally(()=>setTimeout(tick,1000))}
tick();
</script></body></html>)rawliteral";
