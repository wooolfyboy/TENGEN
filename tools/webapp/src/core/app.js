// ---- app: tabs, subtabs, frame loop, fullscreen ----
const App = T.app = { cur: null };
const nav = document.getElementById('nav'), subs = document.getElementById('subs'), main = document.getElementById('main');
const elBat = document.getElementById('bat'), elRadios = document.getElementById('radios'), elClock = document.getElementById('clock'), elFps = document.getElementById('fps'), elUpd = document.getElementById('upd'), elSet = document.getElementById('setBtn');
App.status = () => {
  const s = S.st; if (!s) return;
  elBat.textContent = s.bat >= 0 ? 'BAT ' + s.bat + '%' + (s.usb ? ' ⚡' : '') : '';
  const r = s.radio || '', names = [['b', 'BLE'], ['w', 'WIFI'], ['s', 'SUB'], ['m', 'MIC'], ['p', 'PC'], ['i', 'IR'], ['n', 'NFC']];
  elRadios.innerHTML = names.map(([k, n]) => `<i class="${r.includes(k) ? 'on' : ''}">${n}</i>`).join('');
};

const updClock = () => { const d = new Date(); elClock.textContent = String(d.getHours()).padStart(2, '0') + ':' + String(d.getMinutes()).padStart(2, '0'); };
updClock(); setInterval(updClock, 10000);
const TOOL_ITEMS = [
  { id: 'sub', label: '433 MHz' },
  { id: 'snd', label: 'Sound' },
  { id: 'probe', sub: 0, label: 'IR' },
  { id: 'probe', sub: 1, label: 'NFC' },
];
App.renderSubs = () => {
  const v = App.cur; subs.innerHTML = '';
  const isTool = v.group === 'tools';
  const hasSubs = isTool || !!(v.subs && v.subs.length);
  document.body.classList.toggle('hassubs', hasSubs);
  if (isTool) {
    TOOL_ITEMS.forEach(it => {
      const on = v.id === it.id && (it.sub == null || v.sub === it.sub);
      const b = T.el('button', on ? 'on' : '', T.tr(it.label));
      b.onclick = () => { T.auto && T.auto.onUserNav(); App.go(it.id, it.sub); };
      subs.appendChild(b);
    });
    if (v.id !== 'probe' && v.subs && v.subs.length) {
      subs.appendChild(T.el('span', 'sub-sep'));
      v.subs.forEach((n, i) => {
        const b = T.el('button', i === v.sub ? 'on' : '', T.tr(n));
        b.onclick = () => { v.sub = i; v.onSub && v.onSub(i); App.renderSubs(); App.fresh(); try { localStorage.setItem('sub_' + v.id, i); } catch (e) {} App.rewant(); };
        subs.appendChild(b);
      });
    }
    return;
  }
  (v.subs || []).forEach((n, i) => {
    const b = T.el('button', i === v.sub ? 'on' : '', T.tr(n));
    b.onclick = () => { v.sub = i; v.onSub && v.onSub(i); App.renderSubs(); App.fresh(); try { localStorage.setItem('sub_' + v.id, i); } catch (e) {} App.rewant(); };
    subs.appendChild(b);
  });
};
// Sections stay in memory: leaving hides the scene instead of destroying it, so returning is instant
// with everything already accumulated. Keep the last KEEP, free the oldest.
const KEEP = 3, alive = [];
App.go = (id, sub) => {
  if (id === 'ov') id = 'hq';
  if (id === 'tools') {
    try { id = localStorage.getItem('last_tool') || 'sub'; } catch (e) { id = 'sub'; }
  }
  const v = T.views.find(x => x.id === id) || T.views[0];
  if (App.cur === v) {
    if (sub != null && v.subs && v.sub !== sub) {
      v.sub = Math.min(sub, v.subs.length - 1);
      v.onSub && v.onSub(v.sub);
      App.renderSubs(); App.fresh(); App.rewant();
    }
    return;
  }
  if (App.cur && App.cur.root) App.cur.root.hidden = true;
  App.cur = v;
  const i = alive.indexOf(v); if (i >= 0) alive.splice(i, 1);
  alive.push(v);
  while (alive.length > KEEP) { const old = alive.shift(); old.unmount(); old.root = null; }
  if (sub != null && v.subs) {
    v.sub = Math.min(sub, v.subs.length - 1);
  } else if (i < 0) {                                  // new (or unloaded): restore the saved tab
    try { const s = localStorage.getItem('sub_' + v.id); if (s != null && v.subs) v.sub = Math.min(+s, v.subs.length - 1); } catch (e) {}
  }
  if (!(T.auto && T.auto._switching)) {
    try {
      localStorage.setItem('view', v.id);
      if (v.group === 'tools') localStorage.setItem('last_tool', v.id);
    } catch (e) {}
  }
  const navId = v.group === 'tools' ? 'tools' : v.id;
  [...nav.children].forEach(b => b.classList.toggle('on', b.dataset.id === navId));
  if (elSet) elSet.classList.toggle('on', v.id === 'set');
  const w = document.getElementById('wipe'); w.classList.remove('go'); void w.offsetWidth; w.classList.add('go');
  App.renderSubs();
  App.fresh();                                         // LITE applies only to the section that struggled; a new section starts full
  if (v.root) { v.root.hidden = false; v.cv && v.cv.fit(); } else v.mount(main);
  if (sub != null && v.onSub) v.onSub(v.sub);
  App.rewant();
};
// stream subscription: a view may change it live (want is a string or function)
App.rewant = () => {
  const v = App.cur; if (!v) return;
  const want = (typeof v.want === 'function' ? v.want() : v.want) || 'st';   // gallery needs nothing from the stick, only status
  if (want !== T.Stream.want) T.Stream.connect(want);
};
let toolsBtnAdded = false;
T.views.forEach(v => {
  if (v.id === 'set') return;
  if (v.group === 'tools') {
    if (toolsBtnAdded) return;
    toolsBtnAdded = true;
    const b = T.el('button', '', T.tr('Tools')); b.dataset.id = 'tools';
    b.onclick = () => { T.auto && T.auto.onUserNav(); App.go('tools'); };
    nav.appendChild(b);
    return;
  }
  const b = T.el('button', '', T.tr(v.title)); b.dataset.id = v.id;
  b.onclick = () => { T.auto && T.auto.onUserNav(); App.go(v.id); };
  nav.appendChild(b);
});
if (elSet) {
  elSet.title = T.tr('Settings');
  elSet.onclick = () => { T.auto && T.auto.onUserNav(); App.go('set'); };
}
// language change (as on the device): menu, tabs, link label and the open section redraw their labels
T.onLang.push(() => {
  for (const b of nav.children) {
    if (b.dataset.id === 'tools') { b.textContent = T.tr('Tools'); continue; }
    const v = T.views.find(x => x.id === b.dataset.id); if (v) b.textContent = T.tr(v.title);
  }
  if (elSet) elSet.title = T.tr('Settings');
  if (App.cur) App.renderSubs();
  if (T.Stream && T.Stream.mode) T.Stream.setLink(T.Stream.mode);
  for (const v of T.views) if (v.root && v.onLang) v.onLang();
});
document.getElementById('upd').onclick = () => { const UP = window.Capacitor && window.Capacitor.Plugins && window.Capacitor.Plugins.TengenUpdate; if (UP) UP.check().catch(() => {}); };
document.getElementById('fs').onclick = () => {
  const d = document.documentElement;
  if (document.fullscreenElement) document.exitFullscreen(); else (d.requestFullscreen || d.webkitRequestFullscreen).call(d);
};
// ---- stable mode: at most 60 fps, image ~1920 on the long side ----
// Glow is the only heavy effect: if fps stays under 45 for 3 s it turns off (a safety net, rarely hit).
const FPS_CAP = 60, STEP = 1000 / FPS_CAP;
// ---- adaptive quality: if the tablet falls behind, first lower the resolution (100% -> 60% in 10% steps,
// the GPU stretches it, barely visible), and only if 60% is still too slow turn glow off (LITE). Resolution recovers when calm.
// Applies to the current section/tab; a change resets to 100%. The first 2.5 s after a change are not counted.
T.RS = 1;
App.graceT = 0;
let slow = 0, good = 0, raiseT = 0, ceil = 1;
const setRS = v => { v = Math.round(v * 10) / 10; if (v === T.RS) return; T.RS = v; for (const o of T._cvs) o.fit(); App.graceT = performance.now() + 1500; };
App.fresh = () => { T.Q.bloom = 1; T.Q.scan = 1; slow = good = 0; ceil = 1; setRS(1); App.graceT = performance.now() + 2500; };
App.adapt = fps => {
  if (performance.now() < App.graceT) return;
  slow = fps < 50 ? slow + 1 : 0; good = fps >= 58 ? good + 1 : 0;
  if (slow >= 2 && T.RS > 0.6) { if (performance.now() - raiseT < 15000) ceil = Math.min(ceil, T.RS - 0.1); setRS(T.RS - 0.1); slow = 0; }
  else if (slow >= 3 && T.Q.bloom) { T.Q.bloom = 0; T.Q.scan = 0; slow = 0; }
  else if (good >= 8 && T.RS < ceil - 0.01) { setRS(T.RS + 0.1); raiseT = performance.now(); good = 0; }
};
const SHOT = new URLSearchParams(location.search).has('shot');
// diagnostics (fps, app update status) only with ?debug or Settings > Actions > Show FPS
T.debug = new URLSearchParams(location.search).has('debug');
const applyFps = on => { T.showFps = !!on; elFps.style.display = on && !SHOT ? '' : 'none'; };
T.setShowFps = on => { try { localStorage.setItem('showFps', on ? '1' : '0'); } catch (e) {} applyFps(on); };
let fpsPref = false; try { fpsPref = localStorage.getItem('showFps') === '1'; } catch (e) {}
applyFps(T.debug || fpsPref);
if (!T.debug) elUpd.style.display = 'none';            // for README images: all effects, no fps counter
document.addEventListener('visibilitychange', () => { if (!document.hidden) { last = next = performance.now(); App.graceT = last + 1500; } });
let last = performance.now(), next = last, fpsN = 0, fpsT = 0;
const loop = now => {
  requestAnimationFrame(loop);
  if (document.hidden) { last = next = now; return; }  // pause rendering when minimized or screen locked
  if (now < next - 1) return;                          // 120/144 Hz screen: skip extra frames
  next = Math.max(next + STEP, now - STEP);            // fell behind: do not catch up in a burst
  const dt = Math.min(0.1, (now - last) / 1000); last = now;
  S.tick(dt);
  if (T.auto) T.auto.tick(dt, now);
  if (App.cur && !App.hold) App.cur.frame(dt, now / 1000);   // the scene waits during the splash (data keeps flowing)
  fpsN++; fpsT += dt;
  if (fpsT >= 1) {
    const fps = Math.round(fpsN / fpsT); T.fps = fps;
    if (!SHOT) App.adapt(fps);
    if (T.showFps) elFps.textContent = fps + ' FPS' + (T.RS < 1 ? ' · ' + Math.round(T.RS * 100) + '%' : '') + (T.Q.bloom ? '' : ' · LITE');
    // app: page update state from the stick (diagnostics); tap = check now
    const UP = T.debug && window.Capacitor && window.Capacitor.Plugins && window.Capacitor.Plugins.TengenUpdate;
    if (UP) UP.status().then(r => { T.updStatus = r.status; if (elUpd) elUpd.textContent = 'UPD ' + r.status; }).catch(() => {});
    fpsN = 0; fpsT = 0;
  }
  if (T.Stream.mode === 'live' && now - T.Stream.lastMsg > 6000) T.Stream.setLink('off');
  if (T.Stream.mode === 'usb' && now - T.Stream.lastMsg > 6000) T.Stream.setLink('usbwait');
};
// start (boot.js): boot bar (link, section radio, data, scene), then the splash, then the section
let first = 'hq'; try { first = localStorage.getItem('view') || 'hq'; } catch (e) {}
if (first === 'ov') first = 'hq';
if (T.boot) T.boot(first); else App.go(first);
requestAnimationFrame(loop);
