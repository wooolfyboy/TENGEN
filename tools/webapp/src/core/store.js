// ---- latest stick data + smoothing for a calm picture ----
// Looks over precision: small signal jitter (+-2-3 dB) is hidden, big changes flow (T.steady).
// The stick already sends calm values; this is a second layer so motion between packets is soft.
const S = T.S = {
  st: null, ble: null, bleT: 0, wifi: null, wifiT: 0, snd: null, sndT: 0, sub: null, subT: 0, pc: null, pcT: 0, sky: null,
  bleHist: [],                                         // device count, one point per second
  dev: new Map(),                                      // id -> device (rs = value to draw)
  aps: new Map(),                                      // bssid -> network (rs = value to draw)
  bands: new Float32Array(32), caps: new Float32Array(32), level: -90, peak: -90, hz: 0, energy: 0,
  wave: new Float32Array(64), lvHist: [], target: new Float32Array(32),
  subWf: [], subHist: [],                              // Sub-GHz waterfall (spectrum rows), up to 400 rows
  pcHist: [], cores: [],                               // CPU per second; cores (smoothed)
  events: [],                                          // event feed: {t, tag, text, alert}
};
// ---- HQ alerts: settings live on the stick (Settings > Alerts, /api/settings); this is a copy ----
// Defaults match the firmware (older firmware without that page uses them).
S.CFG_DEF = { al_near: -65, al_new: 1, al_trk: 1, al_newfor: 30, al_warm: 20, al_sens: 5, al_warn: 60, al_alarm: 80,
  al_cble: 1, al_crf: 1, al_csnd: 1, al_cprb: 1, al_blink: 1, al_plate: 1 };
S.cfg = Object.assign({}, S.CFG_DEF);
S.cfgItems = null;                                     // Alerts page items as the stick sent them (for the HQ panel)
S.cfgFromItem = it => {                                 // item from /api/settings -> number in S.cfg
  if (!it || !(it.k in S.CFG_DEF)) return;
  let v = it.v;
  if (it.type === 'enum') { const i = (it.opts || []).findIndex(o => String(o).toLowerCase() === String(v).toLowerCase()); v = i >= 0 ? i : typeof v === 'number' ? v : S.CFG_DEF[it.k]; }
  else if (it.type === 'bool') v = v === true || v === 1 || v === '1' || v === 'on' ? 1 : 0;
  else v = +v;
  if (!isNaN(v)) S.cfg[it.k] = v;
  if (S.cfgItems) { const j = S.cfgItems.findIndex(x => x.k === it.k); if (j >= 0) S.cfgItems[j] = it; }
};
S.cfgFromSettings = d => {
  const pg = d && d.pages && d.pages.find(p => p.id === 'alert');
  if (!pg) return false;
  S.cfgItems = pg.items.map(x => Object.assign({}, x));
  pg.items.forEach(S.cfgFromItem);
  return true;
};
T.loadCfg = () => {
  if (document.hidden) return Promise.resolve();
  return fetch(T.apiBase() + '/api/settings', { cache: 'no-store' }).then(r => r.json()).then(S.cfgFromSettings).catch(() => {});
};
setTimeout(T.loadCfg, 1500); setInterval(T.loadCfg, 15000);
// "new" and "tracker" by settings: HQ and the event feed judge alike
S.isNew = (e, now) => {
  const c = S.cfg; if (e.first || !c.al_new) return false;
  if (now - e.born > c.al_newfor * 1000) return false;
  return c.al_new === 2 || (e.rs != null ? e.rs : e.r) > c.al_near;
};
S.isTrk = e => {
  const c = S.cfg; if (e.type !== 'tracker' || !c.al_trk) return false;
  return c.al_trk === 2 || (e.rs != null ? e.rs : e.r) > c.al_near;
};
// Air activity per source (each 0..1), only those enabled in settings:
//   BLE: new devices per minute (5/min = max), 433: packets per minute (20/min),
//   sound: level -60...-20 dB, IR/NFC: presses and cards per minute (3/min).
//   Result = mean of enabled sources x sensitivity/5.
S.act = { ble: [], rf: [], prb: [] };
const actPush = (k, now, n = 1) => { const a = S.act[k]; for (let i = 0; i < Math.min(n, 30); i++) a.push(now); while (a.length && now - a[0] > 60000) a.shift(); };
S.activity = () => {
  const now = performance.now(), c = S.cfg, cnt = k => { const a = S.act[k]; while (a.length && now - a[0] > 60000) a.shift(); return a.length; };
  const parts = {
    ble: T.clamp(cnt('ble') / 5), rf: T.clamp(cnt('rf') / 20),
    snd: S.snd && S.snd.ok ? T.clamp((S.level + 60) / 40) : 0, prb: T.clamp(cnt('prb') / 3),
  };
  const en = { ble: !!c.al_cble, rf: !!c.al_crf, snd: !!c.al_csnd, prb: !!c.al_cprb };
  let sum = 0, n = 0; for (const k in parts) if (en[k]) { sum += parts[k]; n++; }
  return { v: n ? T.clamp(sum / n * c.al_sens / 5) : 0, parts, en };
};
// level label by the thresholds from settings
S.actState = v => {
  const c = S.cfg, warn = c.al_warn / 100, alarm = c.al_alarm / 100;
  return v >= alarm ? 3 : v >= warn ? 2 : v >= warn / 2 ? 1 : 0;   // 0 quiet, 1 active, 2 warning, 3 alarm
};
S.event = (tag, text, alert) => { S.events.unshift({ t: Date.now(), tag, text, alert: !!alert }); if (S.events.length > 40) S.events.pop(); };

// device labels ("my AirPods"): the stick stores them and sends them as field 8; the tablet keeps its own copy so a new name shows before the stick answers
S.labels = new Map();                                  // id -> {n, t}
S.setLabel = (id, n) => { S.labels.set(id, { n, t: performance.now() }); const e = S.dev.get(id); if (e) e.label = n; };
const pickLabel = (id, fromStick) => {
  const loc = S.labels.get(id);
  if (loc && (fromStick === undefined || performance.now() - loc.t < 6000)) return loc.n;   // our fresh one wins
  return fromStick || '';
};
S.ever = new Map();                                  // id -> first seen (this session)
S.onBle = d => {
  S.ble = d; S.bleT = performance.now();
  S.bleHist.push(d.n); if (S.bleHist.length > 180) S.bleHist.shift();
  const seen = new Set(), now = performance.now();
  for (const a of d.d) {
    const [id, r, type, vend, name, age, macs] = a; seen.add(id);
    const label = pickLabel(id, a.length > 7 ? a[7] : undefined);
    let e = S.dev.get(id);
    if (!e) {
      // "new" = not seen earlier this session. A device that dropped out and came back (the stick does not send everyone at once,
      // phones come and go) is not new. The first 20 s after connecting are warm-up: everything counts as already seen.
      const seenBefore = S.ever.has(id); S.ever.set(id, now);
      if (!S.warmT) S.warmT = now + S.cfg.al_warm * 1000 + 1;
      const fresh = !seenBefore && now > S.warmT;
      e = { id, st: T.steady(4, 0.8), h: 0, born: now, hist: [], first: !fresh, type, r };
      S.dev.set(id, e);
      if (fresh) { actPush('ble', now); S.event('BLE', '+ ' + (label || name || type || 'device') + '  ' + r + ' dBm', S.isTrk(e) || S.isNew(e, now)); }
    }
    e.st.set(r);
    Object.assign(e, { r, type, vend, name, label, age, macs, alive: true, seen: now });
    e.hist.push(r); if (e.hist.length > 120) e.hist.shift();
  }
  for (const [id, e] of S.dev) if (!seen.has(id) && e.alive) e.alive = false;      // no "lost" in the feed: it flickers too often
  S.devInit = true;
  if (S.ever.size > 5000) S.ever.clear();
};
S.onWifi = d => {
  S.wifi = d; S.wifiT = performance.now();
  const seen = new Set();
  for (const a of (d.aps || d.nets || [])) {
    const k = a.b || a.bssid || a.s + a.c; seen.add(k);
    let e = S.aps.get(k);
    if (!e) { e = { k, st: T.steady(3, 1.2), h: 0, hist: [] }; S.aps.set(k, e); }
    e.st.set(a.r);
    e.hist.push(a.r); if (e.hist.length > 60) e.hist.shift();
    Object.assign(e, a, { alive: true });
  }
  for (const [k, e] of S.aps) if (!seen.has(k)) e.alive = false;
};
S.onSnd = d => {
  S.snd = d; S.sndT = performance.now();
  if (!d.ok) return;
  const b = T.hex8(d.b), w = T.hex8(d.w, true);
  for (let k = 0; k < 32; k++) S.target[k] = (b[k] || 0) / 255;
  for (let i = 0; i < 64; i++) S.wave[i] = (w[i] || 0) / 127;
  S.level = d.lv; S.peak = d.pk; S.hz = d.hz; S.energy = d.e;
  S.lvHist.push(d.lv); if (S.lvHist.length > 25 * 120) S.lvHist.shift();
};
S.onSub = d => {
  const prevBurst = S.sub && S.sub.burst;
  S.sub = d; S.subT = performance.now();
  if (d.cur) { S.subWf.unshift(Int8Array.from(d.cur)); if (S.subWf.length > 400) S.subWf.pop(); }
  if (d.rssi) S.subHist = d.rssi;
  if (d.burst && !prevBurst) S.event('SUB', 'burst ' + (d.mon != null ? d.mon.toFixed(2) + ' MHz' : ''), true);
};
S.onPc = d => {
  const was = S.pc && S.pc.live;
  S.pc = d; S.pcT = performance.now();
  if (d.live && d.cpu != null) { S.pcHist.push(d.cpu); if (S.pcHist.length > 120) S.pcHist.shift(); }
  if (d.c) d.c.forEach((v, i) => { if (!S.cores[i]) S.cores[i] = T.steady(3, 0.5); S.cores[i].set(v); });
  if (was !== undefined && !!was !== !!d.live) S.event('PC', d.live ? 'PC online' : 'PC offline', !d.live);
};
S.onSat = d => { S.sky = d; };
// IR remotes (event ir, per press): last + history; a held button (rep) adds no rows
S.ir = { last: null, t: 0, hist: [], n: 0 };
S.onIr = d => {
  const I = S.ir, now = performance.now(), prev = I.hist[0];
  I.n++;
  if (d.rep && prev && prev.p === d.p && prev.addr === d.addr && prev.cmd === d.cmd) { prev.x++; prev.at = now; }
  else {
    I.hist.unshift(Object.assign({ at: now, x: 1 }, d)); if (I.hist.length > 12) I.hist.pop();
    S.event('IR', (d.brand ? d.brand + ' · ' : '') + d.p + ' ' + T.hexs(d.cmd)); actPush('prb', now);
  }
  I.last = d; I.t = now;
};
// NFC (event nfc, per tap): card, history; err = no reader
S.nfc = { last: null, t: 0, hist: [], err: '' };
S.onNfc = d => {
  const N = S.nfc, now = performance.now();
  if (d.err) { N.err = d.err; return; }
  N.err = ''; N.last = d; N.t = now;
  const prev = N.hist[0];
  if (!(prev && prev.uid === d.uid && now - prev.at < 3000)) {
    N.hist.unshift(Object.assign({ at: now }, d)); if (N.hist.length > 8) N.hist.pop();
    S.event('NFC', d.fam || 'card'); actPush('prb', now);
  } else prev.at = now;
};
// 433 sensors (event sens): table and last packet; new packet = pk grew
S.sens = null; S.sensPkT = 0;
S.onSens = d => {
  const prev = S.sens; S.sens = d; S.sensT = performance.now();
  if (!prev || d.pk !== prev.pk) S.sensPkT = S.sensT;
  if (prev && d.pk > prev.pk) actPush('rf', S.sensT, d.pk - prev.pk);
  if (prev) {
    const known = new Set((prev.sensors || []).map(x => x.id));
    for (const x of d.sensors || []) if (!known.has(x.id)) S.event('433', x.kind === 'remote' ? 'remote / bell #' + String(x.id).split('-').pop() : x.p + ' ch' + x.ch + (x.t != null ? '  ' + x.t + '°' : ''));
  }
};
// radio weather (event wx, once a second): rx = received, wxs = smoothed values for the scene
S.wxs = { clouds: 0, wind: 0, rain: 0, storm: 0, trend: 0 }; S.wxBolt = 0;
S.onWx = d => {
  const prev = S.wx; S.wx = d; S.wxT = performance.now();
  if (d.thunder && !(prev && prev.thunder)) S.wxBolt++;          // new thunder: the scene draws lightning
};
S.onSt = d => {
  const prev = S.st; S.st = d;
  if (prev && prev.bat >= 0 && d.bat >= 0 && prev.bat > 15 && d.bat <= 15) S.event('PWR', 'battery ' + d.bat + '%', true);
};
S.loc = null; S.locT = 0;
S.onLoc = d => { S.loc = d; S.locT = performance.now(); };
// every frame: anything that arrives in jumps eases smoothly
S.tick = dt => {
  if (S.wx) { const k = T.ease(dt, 1.4); for (const key in S.wxs) { const v = +S.wx[key] || 0; S.wxs[key] += (v - S.wxs[key]) * k; } }
  const k = 1 - Math.pow(0.001, dt);
  for (const e of S.dev.values()) {
    e.rs = e.st.tick(dt);
    e.h = T.lerp(e.h, e.alive ? 1 : 0, T.ease(dt, 0.6));
    if (!e.alive && e.h < 0.01) S.dev.delete(e.id);
  }
  for (const e of S.aps.values()) {
    e.rs = e.st.tick(dt);
    e.h = T.lerp(e.h, e.alive ? 1 : 0, T.ease(dt, 0.8));
    if (!e.alive && e.h < 0.01) S.aps.delete(e.k);
  }
  for (const c of S.cores) if (c) c.tick(dt);
  for (let i = 0; i < 32; i++) {
    const t = S.target[i];
    S.bands[i] = t > S.bands[i] ? T.lerp(S.bands[i], t, Math.min(1, k * 3)) : T.lerp(S.bands[i], t, k * 0.8);
    S.caps[i] = Math.max(S.bands[i], S.caps[i] - dt * 0.35);
  }
};
