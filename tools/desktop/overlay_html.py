# -*- coding: utf-8 -*-
"""HTML pages for the popups and the widget (small always-on-top windows).
Unified TENGEN Zen + subordinate Evangelion design system via shared CSS/Canvas tokens:
  DENKO   — obsidian glass + amber/teal HQ instrumentation
  KURENAI — matte black + warm ivory & scarlet hanko seals

Python pushes data in via evaluate_js; the pages call back through window.pywebview.api:
  popups:  P.add(notice), P.style('denko'|'kurenai') -> api.popup_size(h), api.popup_click()
  widget:  W.update(snapshot), W.style(...), W.lang('en'|'ru') -> api.widget_open()
"""

COMMON_CSS = """
*{box-sizing:border-box;margin:0;padding:0}
html,body{background:transparent;overflow:hidden;user-select:none;-webkit-user-select:none;cursor:default}
body{
  --bg:#06080c;--panel:#0d1c24;--acc:#ff9429;--acc2:#3ee6ff;--hot:#fff5e8;--lbl:#d8b892;--dim:#7d6247;--line:#2e1d0d;--alert:#ff3324;
  font:13px/1.25 'Exo 2','Segoe UI Variable Display','Segoe UI',system-ui,sans-serif;color:var(--hot);
}
body.kurenai{
  --bg:#0b0b0b;--panel:#161616;--acc:#e5262f;--acc2:#f4f1ea;--hot:#f4f1ea;--lbl:#bdb9b0;--dim:#7e7c77;--line:#2d2d2d;--alert:#ff3b30;
}
.jp{font-family:'Yu Gothic UI','Yu Gothic','Meiryo','Noto Sans CJK JP','IPAGothic',sans-serif;font-weight:700}
.mono{font-family:'JetBrains Mono','Cascadia Mono','Consolas','DejaVu Sans Mono',monospace;font-variant-numeric:tabular-nums}
"""

POPUP_HTML = """<!doctype html><html><head><meta charset="utf-8"><style>%CSS%
#stack{display:flex;flex-direction:column;gap:8px;padding:6px}
.card{
  position:relative;height:94px;overflow:hidden;cursor:pointer;
  background:var(--bg);color:var(--hot);
  clip-path:polygon(0 0,calc(100% - 12px) 0,100% 12px,100% 100%,12px 100%,0 calc(100% - 12px));
  box-shadow:inset 0 0 0 1.5px var(--acc);
  animation:in .34s cubic-bezier(.18,.88,.28,1.08);
}
.card::before{
  content:'';position:absolute;top:0;right:0;width:13px;height:13px;
  background:linear-gradient(225deg,var(--acc) 50%,transparent 52%);pointer-events:none;
}
.card::after{
  content:'';position:absolute;bottom:0;left:0;width:13px;height:13px;
  background:linear-gradient(45deg,var(--acc) 50%,transparent 52%);pointer-events:none;
}
.card.out{animation:out .26s cubic-bezier(.4,0,1,1) forwards}
@keyframes in{from{transform:translateX(108%);opacity:0}to{transform:none;opacity:1}}
@keyframes out{to{transform:translateX(108%);opacity:0}}
.side{
  position:absolute;left:9px;top:9px;bottom:11px;width:40px;
  display:flex;align-items:center;justify-content:center;
  border:1px solid var(--acc);background:rgba(255,148,41,.10);
  clip-path:polygon(0 0,calc(100% - 6px) 0,100% 6px,100% 100%,6px 100%,0 calc(100% - 6px));
}
.side b{display:block;color:var(--acc);font-size:15px;line-height:1.08;text-align:center;letter-spacing:0}
body.kurenai .card{box-shadow:inset 0 0 0 2px var(--hot)}
body.kurenai .card::before{background:linear-gradient(225deg,var(--hot) 50%,transparent 52%)}
body.kurenai .card::after{background:linear-gradient(45deg,var(--hot) 50%,transparent 52%)}
body.kurenai .side{background:var(--hot);border:0;clip-path:none;transform:skewX(-8deg)}
body.kurenai .side b{color:var(--acc);transform:skewX(8deg);font-weight:900}
.kj{position:absolute;left:60px;top:9px;font-size:28px;line-height:1;letter-spacing:2px;color:var(--hot)}
.tag{position:absolute;right:14px;top:10px;font-size:10px;letter-spacing:1.6px;color:var(--dim);text-transform:uppercase}
.tt{position:absolute;left:60px;top:43px;right:12px;font-weight:700;font-size:12.5px;letter-spacing:1.8px;text-transform:uppercase;color:var(--acc);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
body.kurenai .tt{font-family:Oswald,'Arial Narrow',sans-serif;font-weight:500;font-size:14px;letter-spacing:1.2px}
.tx{position:absolute;left:60px;top:63px;right:12px;font-size:12.5px;color:var(--lbl);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.tm{position:absolute;left:0;bottom:0;height:2.5px;width:100%;background:var(--acc);transform-origin:left;animation:tm linear forwards}
@keyframes tm{from{transform:scaleX(1)}to{transform:scaleX(0)}}
.card.alert{box-shadow:inset 0 0 0 1.8px var(--alert);background:#140303}
.card.alert::before{background:linear-gradient(225deg,var(--alert) 50%,transparent 52%)}
.card.alert::after{background:linear-gradient(45deg,var(--alert) 50%,transparent 52%)}
.card.alert .side{background:var(--alert);border-color:var(--alert)}
.card.alert .side b{color:#090202}
.card.alert .kj,.card.alert .tt,.card.alert .tag{color:var(--alert)}
.card.alert .tm{background:var(--alert)}
.card.alert .kj{animation:bl .9s steps(1) 3}
@keyframes bl{50%{opacity:.28}}
</style></head><body class="denko"><div id="stack"></div><script>
var MAX=3,DUR=4500,S=document.getElementById('stack');
function api(){return window.pywebview&&window.pywebview.api}
function report(){clearTimeout(window.__rt);window.__rt=setTimeout(function(){var a=api();var h=S.children.length?S.getBoundingClientRect().height+4:0;if(a&&a.popup_size)a.popup_size(Math.ceil(h))},10)}
var SEAL={new:'接近',tracker:'追跡',ir:'赤外',nfc:'近接',sensor:'感知',temp:'外気',battery:'電池',link:'通信'};
window.P={
 style:function(s){document.body.className=s||'denko'},
 dur:function(s){DUR=Math.max(1500,s*1000)},
 add:function(n){
  while(S.children.length>=MAX){S.removeChild(S.firstChild)}
  var sev=n.sev||'info',c=document.createElement('div');c.className='card '+sev;
  var sl=(SEAL[n.kind]||'天眼').split('').join('<br>');
  c.innerHTML='<div class="side"><b class="jp">'+sl+'</b></div><div class="kj jp"></div><div class="tag mono"></div><div class="tt"></div><div class="tx mono"></div><div class="tm"></div>';
  c.querySelector('.kj').textContent=n.kj||'天眼';
  c.querySelector('.tag').textContent=sev==='alert'?'// ALERT':'// TENGEN';
  c.querySelector('.tt').textContent=n.title||'';
  c.querySelector('.tx').textContent=n.text||'';
  c.querySelector('.tm').style.animationDuration=DUR+'ms';
  c.onclick=function(){var a=api();if(a&&a.popup_click)a.popup_click()};
  S.appendChild(c);report();
  setTimeout(function(){c.classList.add('out');setTimeout(function(){if(c.parentNode)S.removeChild(c);report()},280)},DUR);
 }};
</script></body></html>""".replace("%CSS%", COMMON_CSS)


WIDGET_HTML = """<!doctype html><html><head><meta charset="utf-8"><style>%CSS%
body{width:100vw;height:100vh}
#w{
  position:absolute;inset:0;padding:8px 11px 9px;
  display:grid;grid-template-columns:minmax(108px,42%) 1fr;grid-template-rows:auto 1fr;gap:6px 11px;
  background:var(--bg);
  clip-path:polygon(0 0,calc(100% - 12px) 0,100% 12px,100% 100%,12px 100%,0 calc(100% - 12px));
  box-shadow:inset 0 0 0 1.5px var(--acc);
}
#w::before{content:'';position:absolute;top:0;right:0;width:13px;height:13px;background:linear-gradient(225deg,var(--acc) 50%,transparent 52%);pointer-events:none}
#w::after{content:'';position:absolute;bottom:0;left:0;width:13px;height:13px;background:linear-gradient(45deg,var(--acc) 50%,transparent 52%);pointer-events:none}
body.kurenai #w{box-shadow:inset 0 0 0 2px var(--hot)}
body.kurenai #w::before{background:linear-gradient(225deg,var(--hot) 50%,transparent 52%)}
body.kurenai #w::after{background:linear-gradient(45deg,var(--hot) 50%,transparent 52%)}
#hd{grid-column:1/3;display:flex;align-items:center;gap:7px;font-weight:700;font-size:11.5px;letter-spacing:2px;color:var(--hot);border-bottom:1px solid var(--line);padding-bottom:4px}
#tt{display:inline-flex;align-items:center;justify-content:center;padding:1px 4px;background:var(--acc);color:#06080c;font-size:11px;letter-spacing:1px;clip-path:polygon(0 0,calc(100% - 3px) 0,100% 3px,100% 100%,3px 100%,0 calc(100% - 3px))}
body.kurenai #tt{color:#f4f1ea;clip-path:none}
#hd .sp{flex:1}
#bat{font-size:10.5px;color:var(--lbl);letter-spacing:1px}
#lk{width:7px;height:7px;transform:rotate(45deg);background:var(--alert);transition:background .2s,box-shadow .2s}
#rdWrap{position:relative;width:100%;height:100%;min-height:96px;display:flex;align-items:center;justify-content:center}
#rd{display:block;width:100%;height:100%}
#rt{display:flex;flex-direction:column;justify-content:space-between;gap:3px;min-width:0;padding:1px 0}
.lv{display:flex;align-items:baseline;justify-content:space-between;gap:6px}
.lv b{font-size:16px;color:var(--acc);letter-spacing:1px}
#pc{font-size:11px;color:var(--lbl);letter-spacing:.8px;white-space:nowrap}
.bar{height:7px;display:flex;gap:2px}
.bar i{flex:1;background:var(--line);transition:background .15s}
.bar i.on{background:var(--acc)}
body.kurenai .bar i{transform:skewX(-12deg)}
body.kurenai .bar i.on{background:var(--hot)}
.bar i.hot{background:var(--alert)!important}
.parts{display:flex;gap:6px;font-size:9px;letter-spacing:.8px;color:var(--dim)}
.parts span{flex:1;min-width:0}
.parts b{display:block;font-weight:600;color:var(--lbl);font-size:8.5px}
.parts i{display:block;height:2.5px;margin-top:2px;background:var(--line);position:relative;overflow:hidden}
.parts u{display:block;height:100%;background:var(--acc);text-decoration:none}
.parts span.off{opacity:.28}
.tp{font-weight:700;font-size:24px;line-height:1;color:var(--hot);letter-spacing:.5px}
body.kurenai .tp{font-family:Oswald,'Arial Narrow',sans-serif;font-weight:500;font-size:26px}
.tp small{font-size:11px;font-weight:400;margin-left:5px;color:var(--lbl)}
.events{display:flex;flex-direction:column;gap:2px;border-top:1px solid var(--line);padding-top:3px}
.ln{font-size:10.5px;color:var(--lbl);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;letter-spacing:.3px}
.ln.fresh{color:var(--hot)}
</style></head><body class="denko"><div id="w">
<div id="hd"><span class="jp" id="tt">天眼</span><span>TENGEN</span><span class="sp"></span><span id="bat" class="mono"></span><i id="lk"></i></div>
<div id="rdWrap"><canvas id="rd"></canvas></div>
<div id="rt">
 <div class="lv"><b class="jp" id="st">平穏</b><span class="mono" id="pc">0%</span></div>
 <div class="bar" id="bar"></div>
 <div class="parts" id="parts"></div>
 <div class="tp" id="tp">—<small id="tps"></small></div>
 <div class="events">
  <div class="ln mono" id="ir">IR  —</div>
  <div class="ln mono" id="nfc">NFC —</div>
 </div>
</div></div><script>
var STYLE='denko',LANG='en',D=null,t0=Date.now(),B=document.getElementById('bar'),ripT=-9,lastCnt=0,lastIr=0,lastNfc=0;
window.LANG='en';
for(var i=0;i<20;i++)B.appendChild(document.createElement('i'));
var NAMES=['平穏','活発','注意','警報'],LV={en:['QUIET','ACTIVE','ATTN','ALARM'],ru:['ТИХО','АКТИВНО','ВНИМАНИЕ','ТРЕВОГА']},
    NOS={en:'no outdoor sensor',ru:'нет датчика'},BATW={en:'BAT ',ru:'БАТ '};
var COL={
 denko:{ring:'rgba(255,148,41,.28)',enso:'rgba(255,148,41,.68)',sw:'rgba(255,148,41,',dot:'#ffb866',neu:'#ff3324',sav:'#3ee6ff',bg:'#06080c',ok:'#69ff6e'},
 kurenai:{ring:'rgba(244,241,234,.22)',enso:'rgba(229,38,47,.82)',sw:'rgba(229,38,47,',dot:'#f4f1ea',neu:'#ff3b30',sav:'#bdb9b0',bg:'#0b0b0b',ok:'#f4f1ea'}
};
function api(){return window.pywebview&&window.pywebview.api}
document.getElementById('w').ondblclick=function(){var a=api();if(a&&a.widget_open)a.widget_open()};
function ago(t){if(!t)return'';var s=Math.max(0,Math.round(Date.now()/1000-t));return s<60?s+'s':Math.round(s/60)+'m'}
window.W={
 style:function(s){STYLE=COL[s]?s:'denko';document.body.className=STYLE},
 lang:function(l){LANG=l==='ru'?'ru':'en';window.LANG=LANG;if(D)W.update(D)},
 update:function(d){
  var nowS=(Date.now()-t0)/1000;
  var dv=(d&&d.devs)||[];
  if(dv.length>lastCnt||(d.ir&&d.ir.t&&d.ir.t!==lastIr)||(d.nfc&&d.nfc.t&&d.nfc.t!==lastNfc))ripT=nowS;
  lastCnt=dv.length;if(d.ir&&d.ir.t)lastIr=d.ir.t;if(d.nfc&&d.nfc.t)lastNfc=d.nfc.t;
  D=d;var a=d.act||{v:0,state:0,parts:{},en:{}};
  var stEl=document.getElementById('st');
  stEl.textContent=NAMES[a.state]||'平穏';
  stEl.style.color=a.state>=2?'var(--alert)':'var(--acc)';
  document.getElementById('pc').textContent=Math.round(a.v*100)+'% · '+LV[LANG][a.state];
  var n=Math.round(a.v*20),w=Math.round((a.warn||.6)*20);
  for(var i=0;i<20;i++)B.children[i].className=i<n?(i>=w?'hot':'on'):'';
  var P=document.getElementById('parts');P.innerHTML='';
  [['ble','BLE'],['rf','433'],['snd','SND'],['prb','IR·NFC']].forEach(function(k){var s=document.createElement('span');
   s.className=a.en&&a.en[k[0]]===false?'off':'';
   s.innerHTML='<b>'+k[1]+'</b><i><u style="width:'+Math.round(((a.parts||{})[k[0]]||0)*100)+'%"></u></i>';P.appendChild(s)});
  var tp=document.getElementById('tp'),tps=document.getElementById('tps');
  if(d.temp&&d.temp.t!=null){tp.firstChild.nodeValue=(d.temp.t>0?'+':'')+d.temp.t.toFixed(1)+'°';tps.textContent=(d.temp.h!=null?d.temp.h+'% ':'')+(d.temp.src||'')}
  else{tp.firstChild.nodeValue='—';tps.textContent=NOS[LANG]}
  var irEl=document.getElementById('ir'),nfEl=document.getElementById('nfc');
  irEl.textContent=d.ir?'IR   '+(d.ir.brand||d.ir.p)+' · '+ago(d.ir.t):'IR   —';
  nfEl.textContent=d.nfc?'NFC  '+d.nfc.fam+' · '+ago(d.nfc.t):'NFC  —';
  irEl.className='ln mono'+(d.ir&&Date.now()/1000-d.ir.t<8?' fresh':'');
  nfEl.className='ln mono'+(d.nfc&&Date.now()/1000-d.nfc.t<8?' fresh':'');
  document.getElementById('bat').textContent=d.bat!=null?BATW[LANG]+d.bat+'%':'';
  var lk=document.getElementById('lk'),okC=COL[STYLE].ok;
  lk.style.background=d.link?okC:'var(--alert)';
  lk.style.boxShadow=d.link?'0 0 6px '+okC:'none';
 },
 pause:function(p){PAUSED=!!p;if(!PAUSED&&!RAF){RAF=requestAnimationFrame(frame)}}
};
var cv=document.getElementById('rd'),wrap=document.getElementById('rdWrap'),g=cv.getContext('2d'),PAUSED=false,RAF=0;
function diamond(x,y,r,col,fill){
 g.beginPath();g.moveTo(x,y-r);g.lineTo(x+r,y);g.lineTo(x,y+r);g.lineTo(x-r,y);g.closePath();
 if(fill){g.fillStyle=col;g.fill()}else{g.strokeStyle=col;g.lineWidth=1.2;g.stroke()}
}
function frame(){
 RAF=0;if(PAUSED)return;RAF=requestAnimationFrame(frame);
 var box=wrap.getBoundingClientRect(),dpr=Math.min(window.devicePixelRatio||1,2.5);
 var S=Math.max(80,Math.floor(Math.min(box.width,box.height))),pw=Math.round(S*dpr);
 if(cv.width!==pw||cv.height!==pw){cv.width=pw;cv.height=pw;cv.style.width=S+'px';cv.style.height=S+'px'}
 g.setTransform(dpr,0,0,dpr,0,0);
 var C=COL[STYLE],cx=S/2,cy=S/2,R=S/2-7,t=(Date.now()-t0)/1000;
 g.clearRect(0,0,S,S);
 // Inner concentric range rings
 g.lineWidth=1;g.strokeStyle=C.ring;
 for(var i=1;i<=2;i++){g.beginPath();g.arc(cx,cy,R*i/3,0,6.2832);g.stroke()}
 // Outer incomplete Zen Enso ring + 12 azimuth ticks
 g.strokeStyle=C.enso;g.lineWidth=1.4;
 g.beginPath();g.arc(cx,cy,R,t*0.18,t*0.18+5.45);g.stroke();
 g.lineWidth=1;
 for(var m=0;m<12;m++){
  var ma=m*0.5236,r0=R+1.5,r1=R+(m%3===0?5:3);
  g.strokeStyle=m%3===0?C.enso:C.ring;
  g.beginPath();g.moveTo(cx+Math.cos(ma)*r0,cy+Math.sin(ma)*r0);g.lineTo(cx+Math.cos(ma)*r1,cy+Math.sin(ma)*r1);g.stroke();
 }
 // Calm radar sweep sector
 var sw=(t*1.25)%6.2832;
 for(var k=0;k<14;k++){
  g.fillStyle=C.sw+(0.19*(1-k/14)).toFixed(3)+')';
  g.beginPath();g.moveTo(cx,cy);g.arc(cx,cy,R-1,sw-(k+1)*0.045,sw-k*0.045);g.closePath();g.fill();
 }
 // Water ripple on new arrival / event
 var dtR=t-ripT;
 if(dtR>=0&&dtR<1.6){
  var pR=dtR/1.6;
  g.strokeStyle=C.sw+(0.65*(1-pR)).toFixed(3)+')';g.lineWidth=1.3;
  g.beginPath();g.arc(cx,cy,R*(0.15+0.85*pR),0,6.2832);g.stroke();
 }
 // Device diamond markers + Evangelion lock reticle on alerts
 if(D&&D.devs){
  var bl=!D.blink||((t*2)%1<0.62);
  D.devs.forEach(function(e){
   var p=Math.max(0,Math.min(1,(e.r+95)/50)),rr=R*(0.14+0.82*(1-p)),a=e.a*6.2832;
   var x=cx+rr*Math.cos(a),y=cy+rr*Math.sin(a),al=e.n||e.k;
   var col=e.s?C.sav:al?C.neu:C.dot;
   if(al&&!bl)g.globalAlpha=0.35;
   diamond(x,y,al?4.2:2.8,col,true);
   g.globalAlpha=1;
   if(al){
    g.strokeStyle=C.neu;g.lineWidth=1.1;
    g.beginPath();g.arc(x,y,8,t*2.2,t*2.2+4.2);g.stroke();
   }
  });
 }
 // Central Tengen Eye (眼)
 g.fillStyle=C.bg;g.beginPath();g.arc(cx,cy,8.5,0,6.2832);g.fill();
 diamond(cx,cy,6.5,C.enso,false);
 diamond(cx,cy,2.2,C.dot,true);
}
frame();
</script></body></html>""".replace("%CSS%", COMMON_CSS)
