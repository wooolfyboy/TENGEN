// ---- HQ 天眼司令室 (Evangelion style): everything the stick hears on one screen ----
// Fixed own style (orange/red on black) - independent of the UI theme.
// Center: 'radio horizon': BLE dots, Wi-Fi diamonds, 433 sensors triangles; closer to center = stronger.
// Edges: counters, weather, sound, remotes/cards, log.
(() => {
// HQ palette, one per theme: DENKO - orange 'NERV', KURENAI - black / white / red
const PAL = {
  denko: { bg: '#050302', or: '#ff8c1a', or2: '#ffb347', dim: '#5a2e08', dk: '#2a1405', red: '#ff2a1f',
    grn: '#69ff6e', cy: '#5fe3ff', txt: '#ffcf99', wht: '#fff4e6', orRGB: '255,140,26', stripe: '#ff8c1a', plateTxt: '#000', skew: 0, mid: '#b35f10', cyRGB: '95,227,255', or2RGB: '255,179,71', redDk: '#7a120c', plateBg: 'rgba(40,0,0,.88)' },
  kurenai: { bg: '#0b0b0b', or: '#f4f1ea', or2: '#f4f1ea', dim: '#3a3a3a', dk: '#1e1e1e', red: '#e5262f',
    grn: '#ffffff', cy: '#bdb9b0', txt: '#d9d6cf', wht: '#ffffff', orRGB: '244,241,234', stripe: '#e5262f', plateTxt: '#0b0b0b', skew: 0.18, mid: '#8a8a8a', cyRGB: '244,241,234', or2RGB: '244,241,234', redDk: '#5a0e12', plateBg: 'rgba(11,11,11,.94)' },
};
const E = Object.assign({}, PAL.denko);
const K = () => T.theme === 'kurenai';
const orA = a => `rgba(${E.orRGB},${a})`;
// fonts are fixed: HQ looks the same in any theme
const RAJ = "'Exo 2','Arial Narrow',sans-serif", ANTON = "Oswald,'Arial Narrow',sans-serif", SPM = "'JetBrains Mono',ui-monospace,monospace";   // names are historical: DENKO / KURENAI headings, text
const JP = () => T.SEAL_FONT, FT = () => K() ? ANTON : RAJ, FM = () => K() ? SPM : T.FM, FC = () => T.FC;
// Oswald has one weight for all - no need for 'bold'
const font = (g, px, f, w = '') => { g.font = `${f === ANTON ? '' : w}${px}px ${f}`; };
const tx = (g, s, x, y, px, col, f = FM(), al = 'left', w = '') => { font(g, px, f, w); g.fillStyle = col; g.textAlign = al; g.textBaseline = 'alphabetic'; g.fillText(s, x, y); };
// 'bold' Japanese: the system print font has one weight, thickness is added by stroke
const jp = (g, s, x, y, px, col, al = 'left', bold = 0.06) => {
  font(g, px, JP()); g.textAlign = al; g.textBaseline = 'alphabetic';
  g.lineJoin = 'round'; g.lineWidth = px * bold; g.strokeStyle = col; g.strokeText(s, x, y);
  g.fillStyle = col; g.fillText(s, x, y);
};
const pad2 = n => String(n).padStart(2, '0');
const hms = d => pad2(d.getHours()) + ':' + pad2(d.getMinutes()) + ':' + pad2(d.getSeconds());
// Moon age (days) - offline, good enough for a label
const moonAge = () => { const syn = 29.530588853, ref = Date.UTC(2000, 0, 6, 18, 14) / 864e5; let a = (Date.now() / 864e5 - ref) % syn; if (a < 0) a += syn; return a; };
// 'caution' striped tape
const stripes = (g, x, y, w, h, col, step) => {
  g.save(); g.beginPath(); g.rect(x, y, w, h); g.clip();
  g.fillStyle = K() ? E.bg : '#000'; g.fillRect(x, y, w, h); g.fillStyle = col;
  for (let k = -h; k < w + h; k += step * 2) { g.beginPath(); g.moveTo(x + k, y + h); g.lineTo(x + k + step, y + h); g.lineTo(x + k + step + h, y); g.lineTo(x + k + h, y); g.closePath(); g.fill(); }
  g.restore();
};
// panel: frame + orange header (EN left, kanji right) + cut corner
const panel = (g, x, y, w, h, u, en, kj, right, hot) => {
  const hh = u * 1.15, col = hot ? E.red : E.or, sk = E.skew * hh;
  g.fillStyle = orA(0.045); g.fillRect(x, y, w, h);
  g.strokeStyle = col; g.lineWidth = Math.max(1, u * 0.07); g.strokeRect(x + 0.5, y + 0.5, w - 1, h - 1);
  g.fillStyle = col;
  if (sk) { g.beginPath(); g.moveTo(x, y); g.lineTo(x + w, y); g.lineTo(x + w - sk, y + hh); g.lineTo(x, y + hh); g.closePath(); g.fill(); }   // KURENAI: slanted plate
  else g.fillRect(x, y, w, hh);
  const pt = hot && K() ? '#fff' : E.plateTxt;
  tx(g, en, x + u * 0.5, y + hh * 0.78, u * 0.8, pt, FT(), 'left', '600 ');
  let rx = x + w - u * 0.5 - sk;
  if (kj) { jp(g, kj, rx, y + hh * 0.8, u * 0.78, pt, 'right', 0.03); font(g, u * 0.78, JP()); rx -= g.measureText(kj).width + u * 0.6; }
  if (right) tx(g, right, rx, y + hh * 0.78, u * 0.72, pt, FT(), 'right', '600 ');
  g.beginPath(); g.moveTo(x + w, y + h - u); g.lineTo(x + w, y + h); g.lineTo(x + w - u, y + h); g.closePath(); g.fillStyle = col; g.fill();
  return y + hh;
};
// blinking can be turned off (Settings > Alerts > Blinking) - then everything glows steady
const blink = (t, hz = 1) => (S.cfg && !S.cfg.al_blink) || (t * hz) % 1 < 0.6;

T.views.push({
  id: 'hq', title: 'HQ', subs: ['Command', 'Holo 3D', 'Autopilot'], sub: 0, want: 'ble,wifi,snd,sens,wx,pc,ir,nfc',
  mount(main) {
    this.root = T.el('div', 'view'); const st = T.el('div', 'stage'); this.root.appendChild(st); this.cv = T.canvas(st); main.appendChild(this.root);
    this.sweep = 0; this.wf = []; this.wfPk = -1; this.lvl = 0; this.alertT = -99; this.alertTxt = null;
    this.prevIr = 0; this.prevNfc = 0; this.flashIr = -99; this.flashNfc = -99; this.scan = null; this.seenAt = new Map();
    if (T.hqHolo) T.hqHolo.init(this);
    this.cv.cv.addEventListener('pointerup', e => { if (this.sub !== 0) return; const h = T.hits.at(e.offsetX, e.offsetY); if (h && h.go) T.app.go(h.go); });
    this.alUI();
  },
  onSub(i) {
    if (i === 2) {
      if (T.auto) T.auto.start();
    } else if (T.auto && T.auto.on && !T.auto._switching) {
      T.auto.stop();
    }
  },
  unmount() { this.cv.dispose(); this.root.remove(); },

  // ---- ⚙ alert settings right in HQ (same items as Settings > Alerts on the stick and tablet) ----
  alUI() {
    const esc = x => String(x == null ? '' : x).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
    const btn = T.el('button', 'hq-gear', '⚙ ' + T.tr('ALERTS') + ' · <b class="kj">警報設定</b>');
    const back = T.el('div', 'hq-al-back'); back.hidden = true;
    const pan = T.el('div', 'hq-al'); back.appendChild(pan);
    this.root.appendChild(btn); this.root.appendChild(back);
    this.alBtn = btn; this.alBack = back;
    this.onLang = () => { btn.innerHTML = '⚙ ' + T.tr('ALERTS') + ' · <b class="kj">警報設定</b>'; if (!back.hidden && this.alRender) this.alRender(); };
    const api = path => fetch(T.apiBase() + path, { cache: 'no-store' }).then(r => r.json());
    const row = it => {
      let ctl;
      if (it.type === 'range') ctl = `<div class="st"><button data-d="-1">−</button><output>${esc(it.v)}${it.unit && it.unit !== '%' ? ' ' : ''}${esc(it.unit || '')}</output><button data-d="1">+</button></div>`;
      else if (it.type === 'enum') ctl = `<div class="ch">${(it.opts || []).map((o, i) => `<button data-o="${i}" class="${String(o).toLowerCase() === String(it.v).toLowerCase() ? 'on' : ''}">${esc(T.tr(String(o)))}</button>`).join('')}</div>`;
      else if (it.type === 'bool') { const on = +it.v === 1 || it.v === true; ctl = `<button class="tg ${on ? 'on' : ''}">${T.tr(on ? 'ON' : 'OFF')}</button>`; }
      else ctl = `<span>${esc(it.v)}</span>`;
      return `<div class="r" data-k="${esc(it.k)}"><div class="n"><b>${esc(T.tr(it.name))}</b><i>${esc(T.tr(it.hint || ''))}</i></div>${ctl}</div>`;
    };
    const send = (k, v) => api(`/api/set?k=${encodeURIComponent(k)}&v=${encodeURIComponent(v)}`).then(r => {
      if (!r || !r.ok) throw new Error((r && r.err) || 'refused');
      S.cfgFromItem(r.item);
      if (k === 'al_warn' || k === 'al_alarm') return T.loadCfg();   // the stick may have adjusted the paired threshold
    });
    const render = () => {
      const items = S.cfgItems;
      pan.innerHTML = `<div class="h"><span>// ${T.tr('ALERTS')} · <b class="kj">警報設定</b></span><button class="x" aria-label="${T.tr('close')}">✕</button></div>` +
        (items ? items.map(row).join('') : `<div class="r"><div class="n"><b>${T.tr('NO ALERT SETTINGS ON THE DEVICE')}</b><i>${T.tr('update TENGEN firmware (1.8.2+) · defaults are used')}</i></div></div>`) +
        `<div class="f"><span class="msg"></span><button class="rst">${T.tr('RESET TO DEFAULTS')}</button></div>`;
      pan.querySelector('.x').onclick = close;
      const msg = t => { const m = pan.querySelector('.msg'); if (m) m.textContent = T.tr(t); };
      pan.querySelectorAll('.r[data-k]').forEach(el => {
        const it = items.find(x => x.k === el.dataset.k); if (!it) return;
        const go = v => send(it.k, v).then(() => { render(); msg('saved'); }).catch(e => msg(T.tr('not saved') + ' · ' + T.tr(e.message || 'no link')));
        el.querySelectorAll('[data-d]').forEach(b => b.onclick = () => go(Math.max(it.min, Math.min(it.max, +it.v + (+b.dataset.d) * (it.step || 1)))));
        el.querySelectorAll('[data-o]').forEach(b => b.onclick = () => go(+b.dataset.o));
        const tg = el.querySelector('.tg'); if (tg) tg.onclick = () => go(+it.v === 1 || it.v === true ? 0 : 1);
      });
      pan.querySelector('.rst').onclick = () => {
        const keys = Object.keys(S.CFG_DEF).filter(k => k !== 'al_alarm' && k !== 'al_warn');
        // thresholds in order, so the stick does not adjust them under each other
        const seq = [['al_alarm', 100], ['al_warn', S.CFG_DEF.al_warn], ['al_alarm', S.CFG_DEF.al_alarm], ...keys.map(k => [k, S.CFG_DEF[k]])];
        msg('resetting…');
        seq.reduce((p, [k, v]) => p.then(() => send(k, v)), Promise.resolve()).then(() => T.loadCfg()).then(() => { render(); msg('defaults restored'); })
          .catch(e => msg(T.tr('reset failed') + ' · ' + T.tr(e.message || 'no link')));
      };
    };
    this.alRender = render;
    const open = () => { back.hidden = false; render(); T.loadCfg().then(() => { if (!back.hidden) render(); }); };
    const close = () => { back.hidden = true; };
    btn.onclick = () => (back.hidden ? open() : close());
    back.onclick = e => { if (e.target === back) close(); };
  },

  frame(dt, t) {
    const th = K() ? 'kurenai' : 'denko';
    if (this.th !== th) { this.th = th; Object.assign(E, PAL[th]); }
    // top strip for subtabs + ⚙ ALERTS button (collapses to 0 in cinema-hide mode)
    const off = document.body.classList.contains('cinema-hide') ? 0 : 40, o = this.cv;
    if (off) { o.g.fillStyle = this.sub === 1 ? T.C.bg : E.bg; o.g.fillRect(0, 0, o.w, off); }
    o.g.save(); o.g.translate(0, off);
    try {
      if (this.sub === 1 && T.hqHolo) T.hqHolo.frame(dt, t, o.g, o.w, o.h - off);
      else this.frame3(dt, t, o.g, o.w, o.h - off);
    } finally { o.g.restore(); }
    for (const q of T.hits.list) q.y += off;
  },
  frame3(dt, t, g, w, h) {
    T.hits.clear();
    const now = performance.now(), u = Math.min(w / 96, h / 60), m = u * 0.8;
    this.sweep = (this.sweep + dt * 1.6) % T.TAU;
    this.track(now, t); this.wfTick(dt); this.actTick(dt, t);
    g.fillStyle = E.bg; g.fillRect(0, 0, w, h);

    // ---------- layout ----------
    const topH = u * 5.2, logH = u * 2.4, botH = u * 6;
    const y1 = m + topH + m, yLog = h - m - logH, yBot = yLog - m - botH;
    const portrait = h > w * 1.05;
    let L, C, R;
    if (!portrait) {
      const sw = Math.min(u * 22, w * 0.23);
      L = { x: m, y: y1, w: sw, h: yBot - m - y1 };
      R = { x: w - m - sw, y: y1, w: sw, h: L.h };
      C = { x: L.x + sw + m, y: y1, w: R.x - m - (L.x + sw + m), h: L.h };
    } else {
      const ch = (yBot - m - y1) * 0.5;
      C = { x: m, y: y1, w: w - 2 * m, h: ch };
      const sw = (w - 3 * m) / 2;
      L = { x: m, y: y1 + ch + m, w: sw, h: yBot - m - (y1 + ch + m) };
      R = { x: m * 2 + sw, y: L.y, w: sw, h: L.h };
    }

    this.top(g, m, m, w - 2 * m, topH, u, t);
    // left column: BLE / Wi-Fi / 433
    const lh = (L.h - 2 * m);
    this.blePanel(g, L.x, L.y, L.w, lh * 0.38, u, t);
    this.wifiPanel(g, L.x, L.y + lh * 0.38 + m, L.w, lh * 0.28, u);
    this.sensPanel(g, L.x, L.y + lh * 0.66 + 2 * m, L.w, lh * 0.34, u, t);
    // center: radar
    this.radar(g, C.x, C.y, C.w, C.h, u, t, now);
    // right column: weather / sound / remotes and cards
    this.wxPanel(g, R.x, R.y, R.w, lh * 0.33, u);
    this.audioPanel(g, R.x, R.y + lh * 0.33 + m, R.w, lh * 0.22, u);
    this.probePanel(g, R.x, R.y + lh * 0.55 + 2 * m, R.w, lh * 0.45, u, t, now);
    // bottom: 433 stream + activity scale
    const bw = (w - 3 * m) * 0.58;
    this.wfPanel(g, m, yBot, bw, botH, u, dt);
    this.lvlPanel(g, m * 2 + bw, yBot, w - 3 * m - bw, botH, u, dt, t);
    this.log(g, m, yLog, w - 2 * m, logH, u, t);
    // scanlines (pattern is fixed 4x3; create once)
    if (!this.scan) {
      const c = document.createElement('canvas'); c.width = 4; c.height = 3; const q = c.getContext('2d');
      q.fillStyle = 'rgba(0,0,0,.28)'; q.fillRect(0, 0, 4, 1); this.scan = g.createPattern(c, 'repeat');
    }
    if (T.Q.scan && !K()) { g.fillStyle = this.scan; g.fillRect(0, 0, w, h); }   // KURENAI - flat, no scanlines
  },

  // events: new near device, remote, card
  track(now, t) {
    const I = S.ir, N = S.nfc;
    if (I && I.t !== this.prevIr) { this.prevIr = I.t; if (I.t) this.flashIr = t; }
    if (N && N.t !== this.prevNfc) { this.prevNfc = N.t; if (N.t) this.flashNfc = t; }
    if (S.devInit) for (const e of S.dev.values()) {
      if (!e.alive || this.seenAt.has(e.id)) continue;
      this.seenAt.set(e.id, now);
      if (S.isNew(e, now) || S.isTrk(e)) { this.alertT = t; this.alertTxt = { n: e.label || e.name || e.type || 'device', r: e.r, tr: e.type === 'tracker' }; }
    }
    if (this.seenAt.size > 400) this.seenAt.clear();
  },

  top(g, x, y, w, h, u, t) {
    stripes(g, x, y, u * 2.6, h, E.stripe, u * 0.7);
    // title
    let cx = x + u * 3.4;
    const tw0 = (() => { font(g, h * 0.72, JP()); return g.measureText('天眼司令室').width; })();
    const bw = tw0 + u * 13.5;
    g.strokeStyle = E.or; g.lineWidth = u * 0.12; g.strokeRect(cx, y, bw, h);
    jp(g, '天眼司令室', cx + u * 0.7, y + h * 0.8, h * 0.72, E.or, 'left', 0.05);
    tx(g, 'TENGEN HQ', cx + tw0 + u * 1.6, y + h * 0.45, u * 1.5, E.or2, FT(), 'left', '600 ');
    tx(g, 'PASSIVE MONITOR · ' + (T.VERSION || ''), cx + tw0 + u * 1.6, y + h * 0.75, u * 0.7, E.or);
    cx += bw + u;
    // clock on the right
    const d = new Date(), cw = u * 13;
    const kx = x + w - cw;
    g.strokeStyle = E.or; g.strokeRect(kx, y, cw, h);
    tx(g, hms(d), kx + cw - u * 0.8, y + h * 0.62, h * 0.55, E.or2, FT(), 'right', '600 ');
    tx(g, d.getFullYear() + '.' + pad2(d.getMonth() + 1) + '.' + pad2(d.getDate()) + ' · 月齢 ' + moonAge().toFixed(0), kx + cw - u * 0.8, y + h * 0.88, u * 0.72, E.or, FM(), 'right');
    // status
    const sx = cx, sw = kx - u - cx;
    if (sw > u * 18) {
      g.strokeStyle = E.or; g.lineWidth = u * 0.07; g.strokeRect(sx, y, sw, h);
      const live = T.Stream.mode === 'live' || T.Stream.mode === 'usb', poll = T.Stream.mode === 'poll' || T.Stream.mode === 'usbwait', s = S.st || {};
      const r = s.radio || '', srcs = ['b', 'w', 's', 'm', 'p', 'i', 'n'].filter(k => r.includes(k)).length;
      const cols = [
        ['STATUS 状態', live ? '受信中' : poll ? '低速' : '断線', live ? E.grn : poll ? E.or2 : E.red, true],
        ['SOURCES 系統', srcs + ' / 7 ONLINE', E.or2, false],
        ['DEVICE 機', s.bat >= 0 ? 'BAT ' + s.bat + '%' + (s.usb ? ' ⚡' : '') : 'BAT —', s.bat >= 0 && s.bat <= 15 ? E.red : E.or2, false],
      ];
      const cw2 = sw / cols.length;
      cols.forEach(([k, v, col, j], i) => {
        const px = sx + cw2 * i + u;
        tx(g, k, px, y + h * 0.33, u * 0.7, E.or);
        if (j) { jp(g, v, px, y + h * 0.82, h * 0.42, col, 'left', 0.04); if (live && blink(t)) { font(g, h * 0.42, JP()); g.fillStyle = col; g.beginPath(); g.arc(px + g.measureText(v).width + u * 1.1, y + h * 0.66, u * 0.45, 0, T.TAU); g.fill(); } }
        else tx(g, v, px, y + h * 0.8, h * 0.4, col, FT(), 'left', '600 ');
      });
    }
  },

  blePanel(g, x, y, w, h, u, t) {
    const y0 = panel(g, x, y, w, h, u, 'BLE · 近距離', '機器');
    T.hits.rect(x, y, w, h, { go: 'ble' });
    const devs = [...S.dev.values()].filter(e => e.alive).sort((a, b) => b.rs - a.rs);
    const n = S.ble ? S.ble.n : devs.length, now = performance.now();
    const fresh = devs.filter(e => !e.first && now - e.born < 300000).length;
    tx(g, S.ble ? String(n) : '—', x + u * 0.7, y0 + u * 4.2, u * 4.6, E.or2, FT(), 'left', '600 ');
    font(g, u * 4.6, FT(), '600 '); const nx = x + u * 1.4 + g.measureText(S.ble ? String(n) : '—').width;
    tx(g, 'DEVICES', nx, y0 + u * 2.8, u * 0.7, E.or);
    tx(g, '+' + fresh + ' NEW / 5 MIN', nx, y0 + u * 3.9, u * 0.7, fresh ? E.grn : E.dim);
    // honeycomb: a cell per device, color = strength, new ones red
    const top = y0 + u * 5.2, s = u * 0.62, dx = s * 1.75, dy = s * 1.52;
    const cols = Math.max(1, Math.floor((w - u * 1.2) / dx)), rows = Math.max(1, Math.floor((y + h - top - u * 0.4) / dy));
    let i = 0;
    for (let r = 0; r < rows; r++) for (let c = 0; c < cols; c++, i++) {
      const cx = x + u * 0.6 + s + c * dx + (r % 2) * dx / 2, cy = top + s + r * dy;
      if (cx + s > x + w - u * 0.3) continue;
      const e = devs[i];
      g.beginPath();
      for (let k = 0; k < 6; k++) { const a = Math.PI / 3 * k + Math.PI / 6; g.lineTo(cx + s * Math.cos(a), cy + s * Math.sin(a)); }
      g.closePath();
      if (e) {
        const p = T.pct(e.rs), nw = S.isNew(e, now), trk = S.isTrk(e);
        g.fillStyle = nw || trk ? (blink(t, 2) || !nw ? E.red : E.dk) : p > 0.55 ? E.or : p > 0.25 ? E.mid : E.dim;
        g.globalAlpha = 0.35 + 0.65 * e.h; g.fill(); g.globalAlpha = 1;
      }
      g.strokeStyle = orA(.3); g.lineWidth = 1; g.stroke();
    }
  },

  wifiPanel(g, x, y, w, h, u) {
    const y0 = panel(g, x, y, w, h, u, 'WI-FI · 2.4 GHz', '網');
    T.hits.rect(x, y, w, h, { go: 'wifi' });
    const best = this._wifiBest || (this._wifiBest = new Float32Array(14));
    best.fill(-100);
    for (const a of S.aps.values()) if (a.alive) { const c = a.c | 0; if (c >= 1 && c <= 13) best[c] = Math.max(best[c], a.rs); }
    const bx = x + u * 0.6, bw = (w - u * 1.2) / 13, bb = y + h - u * 1.4, bh = bb - y0 - u * 0.6, segs = 6, sh = bh / segs;
    for (let c = 1; c <= 13; c++) {
      const lit = Math.round(T.pct(best[c]) * segs), xx = bx + (c - 1) * bw;
      for (let k = 0; k < segs; k++) { g.fillStyle = k < lit ? (k >= 4 ? E.red : E.or) : E.dk; g.fillRect(xx + bw * 0.14, bb - (k + 1) * sh + sh * 0.12, bw * 0.72, sh * 0.76); }
      tx(g, String(c), xx + bw / 2, bb + u * 0.95, u * 0.62, E.or, FM(), 'center');
    }
  },

  sensPanel(g, x, y, w, h, u, t) {
    const y0 = panel(g, x, y, w, h, u, '433 MHz · SENSORS', '感知');
    T.hits.rect(x, y, w, h, { go: 'sub' });
    const d = S.sens, list = (d && d.sensors) || [];
    let ry = y0 + u * 1.3;
    tx(g, 'ID', x + u * 0.6, ry, u * 0.62, E.or); tx(g, 'TEMP · HUM', x + w * 0.62, ry, u * 0.62, E.or, FM(), 'right'); tx(g, 'SEEN', x + w - u * 0.6, ry, u * 0.62, E.or, FM(), 'right');
    ry += u * 1.45;
    if (!list.length) { tx(g, d ? 'LISTENING · NO SENSORS YET' : 'NO DATA', x + u * 0.6, ry, u * 0.7, E.dim); }
    for (const s of list) {
      if (ry > y + h - u * 0.5) break;
      const rem = s.kind === 'remote', id = rem ? 'REMOTE ⌁ #' + String(s.id).split('-').pop().toUpperCase() : (s.p + '·' + s.ch).toUpperCase();
      const col = rem ? E.cy : E.txt, fresh = s.ago < 3;
      font(g, u * 0.72, FM()); tx(g, T.fit(g, id, w * 0.4), x + u * 0.6, ry, u * 0.72, col);
      if (!rem && s.t != null) tx(g, (s.t > 0 ? '+' : '') + s.t.toFixed(1) + '°' + (s.h != null ? ' ' + s.h + '%' : ''), x + w * 0.62, ry, u * 0.9, E.or2, FT(), 'right', '600 ');
      else tx(g, rem ? 'BTN ' + (s.btn ?? '—') : '—', x + w * 0.62, ry, u * 0.72, E.dim, FM(), 'right');
      tx(g, fresh && blink(t, 2) ? 'NOW' : T.ago(s.ago), x + w - u * 0.6, ry, u * 0.72, fresh ? E.grn : E.or, FM(), 'right');
      if (s.bat === 0) tx(g, 'BAT', x + w * 0.66, ry, u * 0.6, E.red);
      ry += u * 1.35;
    }
  },

  radar(g, x, y, w, h, u, t, now) {
    const nB = [...S.dev.values()].filter(e => e.alive).length, nA = [...S.aps.values()].filter(a => a.alive).length, nR = ((S.sens && S.sens.sensors) || []).length;
    const y0 = panel(g, x, y, w, h, u, 'RADIO HORIZON · 電波地平', '', 'RANGE ≈ RSSI · ' + nB + ' BLE · ' + nA + ' AP · ' + nR + ' RF');
    const cx = x + w / 2, cy = y0 + (y + h - y0) / 2, R = Math.min(w * 0.46, (y + h - y0) * 0.44);
    if (!(R > 8)) return;                                // window not laid out yet (rotation/resize)
    const gr = g.createRadialGradient(cx, cy, 0, cx, cy, R * 1.25); gr.addColorStop(0, orA(.10)); gr.addColorStop(1, orA(0));
    g.fillStyle = gr; g.fillRect(x + 1, y0, w - 2, y + h - y0 - 1);
    jp(g, '索敵', x + u * 0.8, y0 + u * 3.2, u * 2.8, E.or, 'left', 0.05);
    tx(g, 'SCANNING ALL BANDS', x + u * 0.9, y0 + u * 4.3, u * 0.7, E.or);
    // rings, rays, scale
    g.lineWidth = 1;
    for (let i = 1; i <= 5; i++) {
      g.strokeStyle = orA(i === 5 ? 0.8 : 0.3); g.setLineDash(i % 2 ? [] : [u * 0.2, u * 0.3]);
      g.beginPath(); g.arc(cx, cy, R * i / 5, 0, T.TAU); g.stroke();
    }
    g.setLineDash([]);
    g.strokeStyle = orA(.16);
    for (let a = 0; a < 12; a++) { const q = a * Math.PI / 6; g.beginPath(); g.moveTo(cx, cy); g.lineTo(cx + R * Math.cos(q), cy + R * Math.sin(q)); g.stroke(); }
    g.strokeStyle = E.or;
    for (let a = 0; a < 72; a++) { const q = a * Math.PI / 36, l = a % 6 ? u * 0.35 : u * 0.8; g.beginPath(); g.moveTo(cx + (R + u * 0.4) * Math.cos(q), cy + (R + u * 0.4) * Math.sin(q)); g.lineTo(cx + (R + u * 0.4 + l) * Math.cos(q), cy + (R + u * 0.4 + l) * Math.sin(q)); g.stroke(); }
    ['-45', '-60', '-75', '-90'].forEach((s, i) => tx(g, s + ' dBm', cx + u * 0.3, cy - R * (i + 1) / 5 - u * 0.4, u * 0.6, E.or));
    // beam
    const sw = this.sweep, N = 22;
    for (let i = 0; i < N; i++) {
      const a1 = sw - i * 0.035, a2 = a1 - 0.036;
      g.fillStyle = orA((0.26 * (1 - i / N)).toFixed(3));
      g.beginPath(); g.moveTo(cx, cy); g.arc(cx, cy, R, a2, a1); g.closePath(); g.fill();
    }
    g.strokeStyle = E.or2; g.lineWidth = u * 0.12; g.beginPath(); g.moveTo(cx, cy); g.lineTo(cx + R * Math.cos(sw), cy + R * Math.sin(sw)); g.stroke();
    // dots: radius by strength, angle fixed from id; brighter when the beam passed
    const lit = a => { let d = (sw - a) % T.TAU; if (d < 0) d += T.TAU; return 1 - Math.min(1, d / 2.6); };
    const pos = (id, rs) => { const a = (T.hash(id) % 3600) / 3600 * T.TAU; const rr = R * (0.08 + 0.92 * (1 - T.pct(rs))); return [a, cx + rr * Math.cos(a), cy + rr * Math.sin(a)]; };
    const labels = [];
    for (const a of S.aps.values()) {
      if (!a.alive && a.h < 0.05) continue;
      const [q, px, py] = pos('ap' + a.k, a.rs), s = u * 0.4, k = 0.45 + 0.55 * lit(q);
      g.globalAlpha = a.h * k; g.fillStyle = E.cy; g.beginPath(); g.moveTo(px, py - s); g.lineTo(px + s, py); g.lineTo(px, py + s); g.lineTo(px - s, py); g.closePath(); g.fill();
    }
    for (const s of ((S.sens && S.sens.sensors) || [])) {
      if (s.rssi == null) continue;
      const [q, px, py] = pos('rf' + s.id, s.rssi), z = u * 0.5, k = 0.5 + 0.5 * lit(q);
      g.globalAlpha = k; g.fillStyle = E.wht; g.beginPath(); g.moveTo(px, py - z); g.lineTo(px + z * 0.9, py + z * 0.7); g.lineTo(px - z * 0.9, py + z * 0.7); g.closePath(); g.fill();
      if (s.t != null) labels.push([px, py, (s.t > 0 ? '+' : '') + s.t.toFixed(1) + '°', E.wht]);
    }
    for (const e of S.dev.values()) {
      if (e.h < 0.05) continue;
      const [q, px, py] = pos(e.id, e.rs), nw = S.isNew(e, now), k = 0.4 + 0.6 * lit(q);
      const col = e.label ? E.grn : nw || S.isTrk(e) ? E.red : E.or2, r = u * (nw ? 0.42 : 0.3);
      g.globalAlpha = e.h * (nw ? 1 : k); g.fillStyle = col; g.beginPath(); g.arc(px, py, r, 0, T.TAU); g.fill();
      if (nw) {
        g.globalAlpha = blink(t, 2) ? 1 : 0.3; g.strokeStyle = col; g.lineWidth = u * 0.1; g.beginPath(); g.arc(px, py, u * 1.1, 0, T.TAU); g.stroke();
        g.globalAlpha = 0.8; g.setLineDash([u * 0.5, u * 2]); g.strokeRect(px - u * 1.6, py - u * 1.6, u * 3.2, u * 3.2); g.setLineDash([]);
      }
      if (e.label || e.name || nw) labels.push([px, py, e.label || e.name || e.type || 'new', col]);
      T.hits.circle(px, py, u * 1.2, { go: 'ble' });
    }
    g.globalAlpha = 1;
    // labels without overlaps: skip occupied rectangles
    const boxes = [];
    for (const [px, py, s, col] of labels) {
      if (boxes.length >= 14) break;
      g.font = `500 ${u * 0.72}px ${FC()}`; const str = T.fit(g, s, u * 9), bw = g.measureText(str).width;
      const b = [px + u * 0.7, py - u * 0.55, bw, u * 1.0];
      if (boxes.some(q => b[0] < q[0] + q[2] && q[0] < b[0] + b[2] && b[1] < q[1] + q[3] && q[1] < b[1] + b[3])) continue;
      boxes.push(b); tx(g, str, b[0], py + u * 0.25, u * 0.72, col, FC(), 'left', '500 ');
    }
    // center - the eye
    g.fillStyle = K() ? E.bg : '#000'; g.beginPath(); g.arc(cx, cy, u * 0.9, 0, T.TAU); g.fill();
    g.strokeStyle = E.or; g.lineWidth = u * 0.15; g.stroke();
    jp(g, '眼', cx, cy + u * 0.4, u * 1.1, E.or, 'center', 0.02);
    // legend
    const ly = y + h - u * 0.8; let lx = x + u * 0.8;
    for (const [c, s] of [[E.grn, 'SAVED 自'], [E.or2, 'KNOWN 既'], [E.red, 'NEW 新'], [E.cy, '◆ WI-FI AP'], [E.wht, '▲ 433 RF']]) {
      g.fillStyle = c; g.beginPath(); g.arc(lx + u * 0.25, ly - u * 0.25, u * 0.22, 0, T.TAU); g.fill();
      tx(g, s, lx + u * 0.7, ly, u * 0.62, E.or); font(g, u * 0.62, FM()); lx += u * 1.6 + g.measureText(s).width;
    }
    // warning 接近警告
    const age = t - this.alertT;
    if (age < 20 && this.alertTxt && S.cfg.al_plate) {
      const bw = Math.min(u * 15, w * 0.36), bh = u * 7.6, bx = x + w - bw - u * 0.8, by = y0 + u * 0.8;
      g.fillStyle = E.plateBg; g.fillRect(bx, by, bw, bh);
      stripes(g, bx, by, bw, u * 0.55, E.red, u * 0.5); stripes(g, bx, by + bh - u * 0.55, bw, u * 0.55, E.red, u * 0.5);
      g.strokeStyle = E.red; g.lineWidth = u * 0.1; g.strokeRect(bx, by, bw, bh);
      if (age < 4 ? blink(t, 2) : true) jp(g, '接近警告', bx + u * 0.7, by + u * 3.3, u * 2.4, E.red, 'left', 0.05);
      tx(g, this.alertTxt.tr ? 'TRACKER · NEARBY' : 'PROXIMITY · NEW DEVICE', bx + u * 0.7, by + u * 4.5, u * 0.8, E.red, FT(), 'left', '600 ');
      g.font = `500 ${u * 0.75}px ${FC()}`;
      tx(g, T.fit(g, String(this.alertTxt.n), bw - u * 1.4) , bx + u * 0.7, by + u * 5.7, u * 0.75, E.txt, FC(), 'left', '500 ');
      tx(g, this.alertTxt.r + ' dBm · ' + Math.round(age) + 's AGO', bx + u * 0.7, by + u * 6.7, u * 0.7, E.txt);
    }
  },

  wxPanel(g, x, y, w, h, u) {
    const y0 = panel(g, x, y, w, h, u, 'OUTSIDE · 外気', '天気');
    T.hits.rect(x, y, w, h, { go: 'gal' });
    const d = S.wx || {}, has = d.temp != null;
    const big = has ? (d.temp > 0 ? '+' : '') + Number(d.temp).toFixed(1) : '—';
    tx(g, big, x + u * 0.8, y0 + u * 4.6, u * 4.6, E.or2, FT(), 'left', '600 ');
    if (has) { font(g, u * 4.6, FT(), '600 '); tx(g, '°C', x + u * 1.0 + g.measureText(big).width, y0 + u * 4.6, u * 2, E.or2, FT(), 'left', '600 '); }
    const W = S.wxs, calm = W.storm > 0.5 ? ['STORM', '嵐', E.red] : W.rain > 0.4 ? ['RAIN', '雨', E.or2] : W.wind > 0.4 ? ['WIND', '風', E.or2] : W.clouds > 0.5 ? ['CLOUDY', '曇', E.or2] : ['CALM', '静', E.grn];
    const rows = [['HUMIDITY', d.hum != null ? d.hum + '%' : '—', E.txt], ['SOURCE', has ? '433 · ' + (d.tsrc || '?') : 'NO OUTDOOR SENSOR', has ? E.txt : E.dim], ['RADIO WEATHER', calm[0] + ' · ' + calm[1], calm[2]]];
    let ry = y0 + u * 6.2;
    for (const [k, v, c] of rows) { if (ry > y + h - u * 0.4) break; tx(g, k, x + u * 0.8, ry, u * 0.7, E.txt); tx(g, v, x + w - u * 0.8, ry, u * 0.7, c, FC(), 'right'); ry += u * 1.25; }
  },

  audioPanel(g, x, y, w, h, u) {
    const sn = S.snd && S.snd.ok;
    const y0 = panel(g, x, y, w, h, u, 'AUDIO · 音', '', sn ? Math.round(S.level) + ' dB' : 'MIC OFF');
    T.hits.rect(x, y, w, h, { go: 'snd' });
    const top = y0 + u * 0.4, hh = y + h - top - u * 0.4, mid = top + hh / 2;
    g.strokeStyle = orA(.15); g.lineWidth = 1;
    for (let i = 0; i <= 8; i++) { const xx = x + w * i / 8; g.beginPath(); g.moveTo(xx, top); g.lineTo(xx, top + hh); g.stroke(); }
    g.beginPath(); g.moveTo(x, mid); g.lineTo(x + w, mid); g.stroke();
    g.strokeStyle = E.grn; g.lineWidth = u * 0.12; g.beginPath();
    for (let i = 0; i < 64; i++) { const xx = x + u * 0.3 + (w - u * 0.6) * i / 63, yy = mid - (sn ? S.wave[i] : 0) * hh * 0.45; i ? g.lineTo(xx, yy) : g.moveTo(xx, yy); }
    g.stroke();
  },

  probePanel(g, x, y, w, h, u, t, now) {
    const y0 = panel(g, x, y, w, h, u, 'PROBES · 探針', '解析');
    const bx = x + u * 0.6, bw = w - u * 1.2, bh = (y + h - y0 - u * 2.6) / 2;
    // IR
    const I = S.ir && S.ir.last, fI = T.clamp(1 - (t - this.flashIr) / 1.2);
    let by = y0 + u * 0.6;
    g.fillStyle = `rgba(${E.cyRGB},${0.25 * fI})`; g.fillRect(bx, by, bw, bh);
    g.strokeStyle = E.cy; g.lineWidth = u * (0.06 + 0.1 * fI); g.strokeRect(bx, by, bw, bh);
    jp(g, '赤外線', bx + u * 0.5, by + u * 1.2, u * 0.75, E.cy, 'left', 0.02);
    tx(g, 'IR' + (I ? ' · ' + T.ago((now - S.ir.t) / 1000) + ' AGO' : ' · LISTENING'), bx + u * 3.2, by + u * 1.2, u * 0.65, E.cy);
    if (I) {
      g.font = `600 ${u * 1.5}px ${FT()}`; tx(g, T.fit(g, (I.brand || I.p).toUpperCase(), bw - u), bx + u * 0.5, by + u * 2.9, u * 1.5, E.cy, FT(), 'left', '600 ');
      tx(g, I.p + ' · ' + T.hexs(I.addr, 4) + ' · ' + T.hexs(I.cmd) + (I.bits ? ' · ' + I.bits + 'b' : '') + (S.ir.hist[0] && S.ir.hist[0].x > 1 ? ' · ×' + S.ir.hist[0].x : ''), bx + u * 0.5, by + u * 4, u * 0.62, E.txt);
      const raw = I.raw || [];
      if (raw.length > 2 && bh > u * 5.2) {
        let tot = 0; for (const v of raw) tot += v;
        let xx = bx + u * 0.5; const ww = bw - u, yh = by + u * 4.6, yl = Math.min(by + bh - u * 0.4, yh + u * 1.1);
        g.strokeStyle = E.cy; g.lineWidth = u * 0.08; g.beginPath(); g.moveTo(xx, yl);
        raw.forEach((v, i) => { const nx = xx + ww * v / tot, yy = i % 2 ? yl : yh; g.lineTo(xx, yy); g.lineTo(nx, yy); xx = nx; });
        g.stroke();
      }
    } else tx(g, 'POINT A REMOTE AT TENGEN', bx + u * 0.5, by + u * 2.8, u * 0.7, E.dim);
    T.hits.rect(bx, by, bw, bh, { go: 'probe' });
    // NFC
    by += bh + u * 0.6;
    const N = S.nfc && S.nfc.last, fN = T.clamp(1 - (t - this.flashNfc) / 1.2);
    g.fillStyle = `rgba(${E.or2RGB},${0.25 * fN})`; g.fillRect(bx, by, bw, bh);
    g.strokeStyle = E.or2; g.lineWidth = u * (0.06 + 0.1 * fN); g.strokeRect(bx, by, bw, bh);
    jp(g, '近接', bx + u * 0.5, by + u * 1.2, u * 0.75, E.or2, 'left', 0.02);
    tx(g, 'NFC' + (N ? ' · ' + T.ago((now - S.nfc.t) / 1000) + ' AGO' : S.nfc && S.nfc.err ? ' · NO READER' : ' · WAITING'), bx + u * 2.4, by + u * 1.2, u * 0.65, E.or2);
    if (N) {
      g.font = `600 ${u * 1.5}px ${FT()}`; tx(g, T.fit(g, String(N.fam || 'CARD').toUpperCase(), bw - u), bx + u * 0.5, by + u * 2.9, u * 1.5, E.or2, FT(), 'left', '600 ');
      g.font = `500 ${u * 0.62}px ${FC()}`; tx(g, T.fit(g, (N.hint ? N.hint + ' · ' : '') + 'UID ' + (N.uid || '—') + (N.sak ? ' · SAK ' + N.sak : ''), bw - u), bx + u * 0.5, by + u * 4, u * 0.62, E.txt, FC(), 'left', '500 ');
    } else tx(g, 'TOUCH A CARD TO TENGEN', bx + u * 0.5, by + u * 2.8, u * 0.7, E.dim);
    T.hits.rect(bx, by, bw, bh, { go: 'probe' });
    // PC
    const p = S.pc, py = y + h - u * 0.7;
    tx(g, 'PC 機', bx, py, u * 0.65, E.txt);
    const ps = p && p.live ? 'CPU ' + Math.round(p.cpu || 0) + '%' + (p.ram != null ? ' · RAM ' + Math.round(p.ram) + '%' : '') + (p.trk ? ' · ♪ ' + (p.trk.play ? 'PLAY' : 'PAUSE') : '') : 'OFFLINE';
    tx(g, ps, bx + bw, py, u * 0.65, p && p.live ? E.txt : E.dim, FM(), 'right');
  },

  // 433 stream: each new packet is a pulse strip, the tape moves down
  wfPanel(g, x, y, w, h, u, dt) {
    const d = S.sens, last = d && d.last;
    const y0 = panel(g, x, y, w, h, u, '433.92 MHz · PULSE FLOW', '滝', d ? (d.pk || 0) + ' PKTS' : 'OFF');
    T.hits.rect(x, y, w, h, { go: 'sub' });
    const top = y0 + u * 0.3, H = y + h - top - u * 0.3, rowH = H / 5;
    g.fillStyle = '#0d0602'; g.fillRect(x + 1, top, w - 2, H);
    this.wf.forEach((r, i) => {
      if (i >= 5) return;
      r.a = Math.max(0.25, r.a - dt * 0.05);
      const yy = top + i * rowH; let tot = 0; for (const v of r.pt) tot += v;
      const col = r.cls === 'sensor' ? E.or : r.cls === 'remote' ? E.cy : r.cls === 'noise' ? E.dim : E.red;
      let xx = x + u * 6; const ww = w - u * 6.6;
      g.globalAlpha = r.a; g.fillStyle = col;
      r.pt.forEach((v, k) => { const nw = ww * v / tot; if (k % 2 === 0) g.fillRect(xx, yy + rowH * 0.2, Math.max(1, nw), rowH * 0.6); xx += nw; });
      g.globalAlpha = 1;
      g.font = `${u * 0.6}px ${FM()}`; tx(g, T.fit(g, String(r.p || r.cls || '?').toUpperCase(), u * 5.2), x + u * 0.5, yy + rowH * 0.72, u * 0.6, col);
    });
    if (!this.wf.length) tx(g, d ? 'WAITING FOR 433 MHz PACKETS' : 'SENSORS STREAM OFF', x + w / 2, top + H / 2 + u * 0.3, u * 0.8, E.dim, FM(), 'center');
  },

  // new 433 packets go to the tape (shared by both styles)
  wfTick(dt) {
    const d = S.sens, last = d && d.last;
    if (d && d.pk !== this.wfPk && last && last.pt) {
      if (this.wfPk >= 0) this.wf.unshift({ pt: last.pt, cls: last.cls, p: last.p, a: 1 });
      this.wfPk = d.pk; if (this.wf.length > 5) this.wf.pop();
    }
  },
  // airwaves activity: new BLE per minute, 433 packets, loudness, remotes/cards
  actTick(dt, t) {
    const A = S.activity(), k = T.ease(dt, 1.5);
    this.lvl += (A.v - this.lvl) * k;
    this.parts = this.parts || { ble: 0, rf: 0, snd: 0, prb: 0 };
    for (const q in A.parts) this.parts[q] += (A.parts[q] - this.parts[q]) * k;
    this.partsEn = A.en;
  },
  // mini scales of activity sources: shows WHY the level is what it is (disabled ones are dim)
  minis(g, x, y, w, h, u, cOn, cOff, cTxt) {
    const L = [['ble', 'BLE'], ['rf', '433'], ['snd', 'SND'], ['prb', 'IR·NFC']], cw = w / 4;
    L.forEach(([k, n], i) => {
      const en = this.partsEn ? this.partsEn[k] : true, v = (this.parts || {})[k] || 0, xx = x + i * cw;
      tx(g, n + (en ? '' : ' ×'), xx, y + u * 0.62, u * 0.55, en ? cTxt : cOff);
      g.fillStyle = cOff; g.fillRect(xx, y + h - u * 0.45, cw - u * 0.5, u * 0.3);
      if (en) { g.fillStyle = cOn; g.fillRect(xx, y + h - u * 0.45, (cw - u * 0.5) * T.clamp(v), u * 0.3); }
    });
  },
  lvlPanel(g, x, y, w, h, u, dt, t) {
    const v = this.lvl, lv = S.actState(v), hot = lv >= 2;
    const st = [['QUIET', '平穏', E.grn], ['ACTIVE', '活発', E.or2], ['ATTENTION', '注意', E.red], ['ALARM', '警報', E.red]][lv];
    const y0 = panel(g, x, y, w, h, u, 'ACTIVITY LEVEL · 活動度', '', st[0] + ' · ' + Math.round(v * 100) + '%', hot);
    jp(g, st[1], x + w - u * 0.6, y + h - u * 0.6, u * 1.6, st[2], 'right', 0.04);
    const n = 30, bx = x + u * 0.6, bw = w - u * 5.5, gap = u * 0.15, sw = (bw - gap * (n - 1)) / n, top = y0 + u * 0.5, bh = Math.max(u * 0.8, (y + h - top) * 0.5);
    const lit = Math.round(v * n), warm = Math.round(S.cfg.al_warn / 100 * n);
    for (let i = 0; i < n; i++) {
      const on = i < lit, red = i >= warm;
      g.fillStyle = on ? (red ? (blink(t, 1.5) ? E.red : E.redDk) : E.or) : E.dk;
      g.fillRect(bx + i * (sw + gap), top, sw, bh);
    }
    // 'attention' and 'alarm' threshold marks
    for (const [p, c] of [[S.cfg.al_warn, E.or2], [S.cfg.al_alarm, E.red]]) { const xx = bx + bw * p / 100; g.fillStyle = c; g.fillRect(xx - 1, top - u * 0.25, 2, bh + u * 0.5); }
    this.minis(g, bx, top + bh + u * 0.2, bw, y + h - (top + bh + u * 0.2) - u * 0.2, u, E.or, E.dk, E.txt);
  },
  log(g, x, y, w, h, u, t) {
    g.fillStyle = E.or; g.fillRect(x, y, w, u * 0.12);
    const my = y + h * 0.72;
    stripes(g, x, y + u * 0.5, u * 2, h - u * 0.7, E.stripe, u * 0.45);
    jp(g, '記録', x + u * 2.6, my, u * 1.2, E.or, 'left', 0.03);
    let lx = x + u * 5.6;
    g.save(); g.beginPath(); g.rect(lx, y, w - (lx - x), h); g.clip();
    const tagCol = { BLE: E.red, IR: E.cy, NFC: E.or2, '433': E.grn, SUB: E.red, PC: E.txt, PWR: E.red };
    if (!S.events.length) tx(g, 'QUIET SO FAR', lx, my, u * 0.8, E.dim);
    for (const ev of S.events.slice(0, 8)) {
      const s1 = hms(new Date(ev.t)) + ' ', s2 = ev.tag + ' ', s3 = String(ev.text);
      font(g, u * 0.78, FC(), '500 ');
      tx(g, s1, lx, my, u * 0.78, E.txt, FC(), 'left', '500 '); lx += g.measureText(s1).width;
      tx(g, s2, lx, my, u * 0.78, ev.alert ? E.red : tagCol[ev.tag] || E.or, FC(), 'left', '500 '); lx += g.measureText(s2).width;
      tx(g, s3, lx, my, u * 0.78, E.txt, FC(), 'left', '500 '); lx += g.measureText(s3).width + u * 0.8;
      tx(g, '▍', lx, my, u * 0.78, E.dim); lx += u * 1.4;
      if (lx > x + w) break;
    }
    if (blink(t)) tx(g, '_', lx, my, u * 0.78, E.or);
    g.restore();
  },
});
})();
