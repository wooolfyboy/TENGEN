// ---- HQ 天眼司令室: Evangelion Central Dogma (MAGI / AT-Field) × Zen Buddhist Mandala ----
// Center: Sacred Dharma / AT-Field Radio Horizon (BLE, Wi-Fi, 433, live Ensō ripples & hexagonal locks).
// Modules: MAGI triple status deck, honeycomb BLE matrix, spectral Wi-Fi, Zen weather seal, mirrored audio, probes & pulse flow.
(() => {
const PAL = {
  denko: { bg: '#050302', or: '#ff8c1a', or2: '#ffb347', dim: '#5a2e08', dk: '#261204', red: '#ff2a1f',
    grn: '#69ff6e', cy: '#5fe3ff', txt: '#ffcf99', wht: '#fff4e6', orRGB: '255,140,26', stripe: '#ff8c1a', plateTxt: '#050302', skew: 0, mid: '#b35f10', cyRGB: '95,227,255', or2RGB: '255,179,71', redDk: '#7a120c', plateBg: 'rgba(32,2,0,.92)' },
  kurenai: { bg: '#0b0b0b', or: '#f4f1ea', or2: '#f4f1ea', dim: '#3a3a3a', dk: '#1c1c1c', red: '#e5262f',
    grn: '#ffffff', cy: '#bdb9b0', txt: '#d9d6cf', wht: '#ffffff', orRGB: '244,241,234', stripe: '#e5262f', plateTxt: '#0b0b0b', skew: 0.18, mid: '#8a8a8a', cyRGB: '244,241,234', or2RGB: '244,241,234', redDk: '#5a0e12', plateBg: 'rgba(11,11,11,.95)' },
};
const E = Object.assign({}, PAL.denko);
const K = () => T.theme === 'kurenai';
const orA = a => `rgba(${E.orRGB},${a})`;
const RAJ = "'Exo 2','Arial Narrow',sans-serif", ANTON = "Oswald,'Arial Narrow',sans-serif", SPM = "'JetBrains Mono',ui-monospace,monospace";
const JP = () => T.SEAL_FONT, FT = () => K() ? ANTON : RAJ, FM = () => K() ? SPM : T.FM, FC = () => T.FC;
const font = (g, px, f, w = '') => { g.font = `${f === ANTON ? '' : w}${px}px ${f}`; };
const tx = (g, s, x, y, px, col, f = FM(), al = 'left', w = '') => { font(g, px, f, w); g.fillStyle = col; g.textAlign = al; g.textBaseline = 'alphabetic'; g.fillText(s, x, y); };
const jp = (g, s, x, y, px, col, al = 'left') => { font(g, px, JP()); g.textAlign = al; g.textBaseline = 'alphabetic'; g.fillStyle = col; g.fillText(s, x, y); };
const pad2 = n => String(n).padStart(2, '0');
const hms = d => pad2(d.getHours()) + ':' + pad2(d.getMinutes()) + ':' + pad2(d.getSeconds());
const moonAge = () => { const syn = 29.530588853, ref = Date.UTC(2000, 0, 6, 18, 14) / 864e5; let a = (Date.now() / 864e5 - ref) % syn; if (a < 0) a += syn; return a; };

const stripes = (g, x, y, w, h, col, step) => {
  g.save(); g.beginPath(); g.rect(x, y, w, h); g.clip();
  g.fillStyle = K() ? E.bg : '#000'; g.fillRect(x, y, w, h); g.fillStyle = col;
  for (let k = -h; k < w + h; k += step * 2) { g.beginPath(); g.moveTo(x + k, y + h); g.lineTo(x + k + step, y + h); g.lineTo(x + k + step + h, y); g.lineTo(x + k + h, y); g.closePath(); g.fill(); }
  g.restore();
};

// Chamfered Evangelion × Zen module plate with NERV header strip & corner reticles
const cutBox = (g, x, y, w, h, c) => {
  g.beginPath();
  g.moveTo(x, y); g.lineTo(x + w - c, y); g.lineTo(x + w, y + c);
  g.lineTo(x + w, y + h); g.lineTo(x + c, y + h); g.lineTo(x, y + h - c);
  g.closePath();
};
const poly = (g, cx, cy, r, sides, rot = 0) => {
  g.beginPath();
  for (let i = 0; i <= sides; i++) {
    const a = rot + i * T.TAU / sides;
    i ? g.lineTo(cx + Math.cos(a) * r, cy + Math.sin(a) * r) : g.moveTo(cx + Math.cos(a) * r, cy + Math.sin(a) * r);
  }
};
const panel = (g, x, y, w, h, u, en, kj, right, hot) => {
  const hh = u * 1.2, col = hot ? E.red : E.or, cut = u * 0.75, sk = E.skew * hh;
  cutBox(g, x, y, w, h, cut);
  g.fillStyle = hot ? 'rgba(60,4,0,.28)' : orA(0.042); g.fill();
  g.strokeStyle = col; g.lineWidth = Math.max(1, u * 0.075); g.stroke();
  // Header bar clipped to chamfered top
  g.save(); cutBox(g, x, y, w, h, cut); g.clip();
  g.fillStyle = col;
  if (sk) { g.beginPath(); g.moveTo(x, y); g.lineTo(x + w, y); g.lineTo(x + w - sk, y + hh); g.lineTo(x, y + hh); g.closePath(); g.fill(); }
  else g.fillRect(x, y, w, hh);
  // Micro diagonal accent on left of header
  g.fillStyle = hot && K() ? '#0b0b0b' : E.plateTxt;
  g.fillRect(x + u * 0.28, y + hh * 0.22, u * 0.16, hh * 0.56);
  g.restore();
  const pt = hot && K() ? '#fff' : E.plateTxt;
  tx(g, en, x + u * 0.65, y + hh * 0.79, u * 0.78, pt, FT(), 'left', '600 ');
  let rx = x + w - u * 0.65 - sk;
  if (kj) { jp(g, kj, rx, y + hh * 0.81, u * 0.8, pt, 'right'); font(g, u * 0.8, JP()); rx -= g.measureText(kj).width + u * 0.55; }
  if (right) tx(g, right, rx, y + hh * 0.78, u * 0.70, pt, FT(), 'right', '600 ');
  // Evangelion corner calibration ticks inside bottom corners
  g.strokeStyle = orA(0.45); g.lineWidth = 1;
  g.beginPath();
  g.moveTo(x + w - u * 1.1, y + h - u * 0.25); g.lineTo(x + w - u * 0.25, y + h - u * 0.25); g.lineTo(x + w - u * 0.25, y + h - u * 1.1);
  g.stroke();
  return y + hh;
};

// Hanko stamp seal box (traditional Buddhist/Japanese vermilion-style seal)
const hanko = (g, x, y, sz, ch, col, filled = false) => {
  const c = sz * 0.14;
  cutBox(g, x, y, sz, sz, c);
  if (filled) { g.fillStyle = col; g.fill(); }
  else { g.strokeStyle = col; g.lineWidth = Math.max(1.2, sz * 0.06); g.stroke(); }
  jp(g, ch, x + sz / 2, y + sz * 0.76, sz * 0.66, filled ? (K() ? '#0b0b0b' : '#050302') : col, 'center');
};

const blink = (t, hz = 1) => (S.cfg && !S.cfg.al_blink) || (t * hz) % 1 < 0.6;

T.views.push({
  id: 'hq', title: 'HQ', subs: ['Command', 'Holo 3D', 'Locator 3D', 'Autopilot'], sub: 0,
  want() { return this.sub === 2 ? 'loc,ble' : 'ble,wifi,snd,sens,wx,pc,ir,nfc'; },
  mount(main) {
    this.root = T.el('div', 'view'); const st = T.el('div', 'stage'); this.root.appendChild(st); this.cv = T.canvas(st); main.appendChild(this.root);
    this.sweep = 0; this.wf = []; this.wfPk = -1; this.lvl = 0; this.alertT = -99; this.alertTxt = null;
    this.prevIr = 0; this.prevNfc = 0; this.flashIr = -99; this.flashNfc = -99; this.scan = null; this.seenAt = new Map();
    this.rips = [];
    if (T.hqHolo) T.hqHolo.init(this);
    if (T.hqLoc) T.hqLoc.init(this);
    this.cv.cv.addEventListener('pointerup', e => { if (this.sub !== 0) return; const h = T.hits.at(e.offsetX, e.offsetY); if (h && h.go) T.app.go(h.go); });
    this.alUI();
    if (this.alBtn) this.alBtn.hidden = this.sub !== 0;
  },
  onSub(i) {
    if (this.alBtn) this.alBtn.hidden = i !== 0;
    if (this.alBack && i !== 0) this.alBack.hidden = true;
    if (i === 3) {
      if (T.auto) T.auto.start();
    } else if (T.auto && T.auto.on && !T.auto._switching) {
      T.auto.stop();
    }
  },
  unmount() { this.cv.dispose(); this.root.remove(); },

  addRipple(hot = false) {
    if (this.rips.length >= 4) this.rips.shift();
    this.rips.push({ p: 0.01, hot });
  },

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
      if (k === 'al_warn' || k === 'al_alarm') return T.loadCfg();
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
    const off = document.body.classList.contains('cinema-hide') ? 0 : 40, o = this.cv;
    if (off) { o.g.fillStyle = (this.sub === 1 || this.sub === 2) ? T.C.bg : E.bg; o.g.fillRect(0, 0, o.w, off); }
    o.g.save(); o.g.translate(0, off);
    try {
      if (this.sub === 1 && T.hqHolo) T.hqHolo.frame(dt, t, o.g, o.w, o.h - off);
      else if (this.sub === 2 && T.hqLoc) T.hqLoc.frame(dt, t, o.g, o.w, o.h - off);
      else this.frame3(dt, t, o.g, o.w, o.h - off);
    } finally { o.g.restore(); }
    for (const q of T.hits.list) q.y += off;
  },

  frame3(dt, t, g, w, h) {
    T.hits.clear();
    const now = performance.now(), u = Math.min(w / 96, h / 60), m = u * 0.8;
    this.sweep = (this.sweep + dt * 1.45) % T.TAU;
    this.track(now, t); this.wfTick(dt); this.actTick(dt, t);
    for (let i = this.rips.length - 1; i >= 0; i--) {
      this.rips[i].p += dt * 0.45;
      if (this.rips[i].p >= 1) this.rips.splice(i, 1);
    }
    g.fillStyle = E.bg; g.fillRect(0, 0, w, h);

    const topH = u * 5.3, logH = u * 2.4, botH = u * 6.0;
    const y1 = m + topH + m, yLog = h - m - logH, yBot = yLog - m - botH;
    const portrait = h > w * 1.05;
    let L, C, R;
    if (!portrait) {
      const sw = Math.min(u * 22.2, w * 0.232);
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
    const lh = (L.h - 2 * m);
    this.blePanel(g, L.x, L.y, L.w, lh * 0.38, u, t);
    this.wifiPanel(g, L.x, L.y + lh * 0.38 + m, L.w, lh * 0.28, u);
    this.sensPanel(g, L.x, L.y + lh * 0.66 + 2 * m, L.w, lh * 0.34, u, t);
    this.radar(g, C.x, C.y, C.w, C.h, u, t, now);
    this.wxPanel(g, R.x, R.y, R.w, lh * 0.33, u, t);
    this.audioPanel(g, R.x, R.y + lh * 0.33 + m, R.w, lh * 0.22, u, t);
    this.probePanel(g, R.x, R.y + lh * 0.55 + 2 * m, R.w, lh * 0.45, u, t, now);
    const bw = (w - 3 * m) * 0.58;
    this.wfPanel(g, m, yBot, bw, botH, u, dt);
    this.lvlPanel(g, m * 2 + bw, yBot, w - 3 * m - bw, botH, u, dt, t);
    this.log(g, m, yLog, w - 2 * m, logH, u, t);

    const coarse = this._coarse ?? (this._coarse = !!(window.matchMedia && window.matchMedia('(pointer:coarse)').matches));
    if (T.Q.scan && !K() && !coarse) {
      if (!this.scan) {
        const c = document.createElement('canvas'); c.width = 4; c.height = 3; const q = c.getContext('2d');
        q.fillStyle = 'rgba(0,0,0,.25)'; q.fillRect(0, 0, 4, 1); this.scan = g.createPattern(c, 'repeat');
      }
      g.fillStyle = this.scan; g.fillRect(0, 0, w, h);
    }
  },

  track(now, t) {
    const I = S.ir, N = S.nfc;
    if (I && I.t !== this.prevIr) { this.prevIr = I.t; if (I.t) { this.flashIr = t; this.addRipple(false); } }
    if (N && N.t !== this.prevNfc) { this.prevNfc = N.t; if (N.t) { this.flashNfc = t; this.addRipple(false); } }
    if (S.devInit) for (const e of S.dev.values()) {
      if (!e.alive || this.seenAt.has(e.id)) continue;
      this.seenAt.set(e.id, now);
      const hot = S.isNew(e, now) || S.isTrk(e);
      this.addRipple(hot);
      if (hot) { this.alertT = t; this.alertTxt = { n: e.label || e.name || e.type || 'device', r: e.r, tr: e.type === 'tracker' }; }
    }
    if (this.seenAt.size > 400) this.seenAt.clear();
  },

  // MAGI Triple Command Header + Zen Lunar Phase Seal
  top(g, x, y, w, h, u, t) {
    stripes(g, x, y, u * 2.5, h, E.stripe, u * 0.65);
    let cx = x + u * 3.2;
    const tw0 = (() => { font(g, h * 0.7, JP()); return g.measureText('天眼司令室').width; })();
    const bw = tw0 + u * 14.5;
    cutBox(g, cx, y, bw, h, u * 0.8);
    g.fillStyle = orA(0.06); g.fill();
    g.strokeStyle = E.or; g.lineWidth = u * 0.11; g.stroke();
    jp(g, '天眼司令室', cx + u * 0.75, y + h * 0.78, h * 0.70, E.or, 'left');
    tx(g, T.tr('TENGEN HQ'), cx + tw0 + u * 1.5, y + h * 0.44, u * 1.45, E.or2, FT(), 'left', '600 ');
    tx(g, T.tr('PASSIVE MONITOR · ') + (T.VERSION || ''), cx + tw0 + u * 1.5, y + h * 0.76, u * 0.68, E.or);
    cx += bw + u * 0.9;

    // Right Lunar & Time Chronograph Module
    const d = new Date(), ageM = moonAge(), cw = u * 14.6, kx = x + w - cw;
    cutBox(g, kx, y, cw, h, u * 0.8);
    g.fillStyle = orA(0.05); g.fill();
    g.strokeStyle = E.or; g.lineWidth = u * 0.08; g.stroke();
    // Graphical Zen Moon / Ensō disc on the left of clock module
    const mr = h * 0.28, mcx = kx + u * 2.0, mcy = y + h * 0.5;
    g.strokeStyle = E.or; g.lineWidth = 1.2;
    g.beginPath(); g.arc(mcx, mcy, mr, 0, T.TAU); g.stroke();
    const litM = 0.5 * (1 - Math.cos((ageM / 29.53) * T.TAU));
    g.fillStyle = orA(0.2 + 0.55 * litM);
    g.beginPath(); g.arc(mcx, mcy, mr * 0.78, -Math.PI / 2, Math.PI / 2, ageM > 14.76); g.fill();
    tx(g, hms(d), kx + cw - u * 0.8, y + h * 0.60, h * 0.52, E.or2, FT(), 'right', '600 ');
    tx(g, d.getFullYear() + '.' + pad2(d.getMonth() + 1) + '.' + pad2(d.getDate()) + ' · 月齢 ' + ageM.toFixed(0), kx + cw - u * 0.8, y + h * 0.87, u * 0.68, E.or, FM(), 'right');

    // Center MAGI·01 / MAGI·02 / MAGI·03 Triple Status Cells
    const sx = cx, sw = kx - u * 0.9 - cx;
    if (sw > u * 18) {
      const live = T.Stream.mode === 'live' || T.Stream.mode === 'usb', poll = T.Stream.mode === 'poll' || T.Stream.mode === 'usbwait', s = S.st || {};
      const r = s.radio || '', srcs = ['b', 'w', 's', 'm', 'p', 'i', 'n'].filter(k => r.includes(k)).length;
      const cols = [
        ['MAGI·01 // ' + T.tr('STATUS 状態'), live ? '受信中' : poll ? '低速' : '断線', live ? E.grn : poll ? E.or2 : E.red, true],
        ['MAGI·02 // ' + T.tr('SOURCES 系統'), srcs + ' / 7 ' + T.tr('ONLINE'), E.or2, false],
        ['MAGI·03 // ' + T.tr('DEVICE 機'), s.bat >= 0 ? 'BAT ' + s.bat + '%' + (s.usb ? ' ⚡' : '') : 'BAT —', s.bat >= 0 && s.bat <= 15 ? E.red : E.or2, false],
      ];
      const gap = u * 0.6, cw2 = (sw - gap * 2) / 3;
      cols.forEach(([k, v, col, j], i) => {
        const px = sx + i * (cw2 + gap);
        cutBox(g, px, y, cw2, h, u * 0.55);
        g.fillStyle = orA(0.038); g.fill();
        g.strokeStyle = orA(0.55); g.lineWidth = 1; g.stroke();
        tx(g, k, px + u * 0.75, y + h * 0.34, u * 0.64, E.or);
        if (j) {
          jp(g, v, px + u * 0.75, y + h * 0.82, h * 0.42, col, 'left');
          if (live && blink(t)) { font(g, h * 0.42, JP()); g.fillStyle = col; g.beginPath(); g.arc(px + u * 0.75 + g.measureText(v).width + u * 0.95, y + h * 0.66, u * 0.38, 0, T.TAU); g.fill(); }
        } else tx(g, v, px + u * 0.75, y + h * 0.80, h * 0.39, col, FT(), 'left', '600 ');
      });
    }
  },

  blePanel(g, x, y, w, h, u, t) {
    const y0 = panel(g, x, y, w, h, u, T.tr('BLE · 近距離'), '機器');
    T.hits.rect(x, y, w, h, { go: 'ble' });
    const now = performance.now();
    if (!this._bleDevs || now - (this._bleT || 0) > 250) {
      this._bleT = now;
      this._bleDevs = [...S.dev.values()].filter(e => e.alive).sort((a, b) => b.rs - a.rs);
      this._bleFresh = this._bleDevs.filter(e => !e.first && now - e.born < 300000).length;
    }
    const devs = this._bleDevs, n = S.ble ? S.ble.n : devs.length, fresh = this._bleFresh || 0;
    tx(g, S.ble ? String(n) : '—', x + u * 0.7, y0 + u * 4.1, u * 4.5, E.or2, FT(), 'left', '600 ');
    font(g, u * 4.5, FT(), '600 '); const nx = x + u * 1.35 + g.measureText(S.ble ? String(n) : '—').width;
    tx(g, T.tr('DEVICES'), nx, y0 + u * 2.7, u * 0.7, E.or);
    tx(g, '+' + fresh + ' ' + T.tr('NEW / 5 MIN'), nx, y0 + u * 3.85, u * 0.68, fresh ? E.grn : E.dim);
    // Evangelion AT-Field Hexagonal Matrix (batched outlines = 1 stroke call)
    const top = y0 + u * 5.0, s = u * 0.62, dx = s * 1.75, dy = s * 1.52;
    const cols = Math.max(1, Math.floor((w - u * 1.2) / dx)), rows = Math.max(1, Math.floor((y + h - top - u * 0.4) / dy));
    const hx = [0.866 * s, 0, -0.866 * s, -0.866 * s, 0, 0.866 * s];
    const hy = [0.5 * s, s, 0.5 * s, -0.5 * s, -s, -0.5 * s];
    let i = 0;
    for (let r = 0; r < rows; r++) for (let c = 0; c < cols; c++, i++) {
      const e = devs[i]; if (!e) continue;
      const cx = x + u * 0.6 + s + c * dx + (r % 2) * dx / 2, cy = top + s + r * dy;
      if (cx + s > x + w - u * 0.3) continue;
      const p = T.pct(e.rs), nw = S.isNew(e, now), trk = S.isTrk(e);
      g.beginPath(); g.moveTo(cx + hx[0], cy + hy[0]);
      for (let k = 1; k < 6; k++) g.lineTo(cx + hx[k], cy + hy[k]);
      g.closePath();
      g.fillStyle = nw || trk ? (blink(t, 2) || !nw ? E.red : E.dk) : p > 0.55 ? E.or : p > 0.25 ? E.mid : E.dim;
      g.globalAlpha = 0.4 + 0.6 * e.h; g.fill();
    }
    g.globalAlpha = 1;
    g.strokeStyle = orA(.32); g.lineWidth = 1; g.beginPath();
    for (let r = 0; r < rows; r++) for (let c = 0; c < cols; c++) {
      const cx = x + u * 0.6 + s + c * dx + (r % 2) * dx / 2, cy = top + s + r * dy;
      if (cx + s > x + w - u * 0.3) continue;
      g.moveTo(cx + hx[0], cy + hy[0]);
      for (let k = 1; k < 6; k++) g.lineTo(cx + hx[k], cy + hy[k]);
      g.closePath();
    }
    g.stroke();
  },

  wifiPanel(g, x, y, w, h, u) {
    const y0 = panel(g, x, y, w, h, u, T.tr('WI-FI · 2.4 GHz'), '網');
    T.hits.rect(x, y, w, h, { go: 'wifi' });
    const best = this._wifiBest || (this._wifiBest = new Float32Array(14));
    best.fill(-100);
    for (const a of S.aps.values()) if (a.alive) { const c = a.c | 0; if (c >= 1 && c <= 13) best[c] = Math.max(best[c], a.rs); }
    const bx = x + u * 0.6, bw = (w - u * 1.2) / 13, bb = y + h - u * 1.45, bh = bb - y0 - u * 0.65, segs = 7, sh = bh / segs;
    for (let c = 1; c <= 13; c++) {
      const p = T.pct(best[c]), lit = Math.round(p * segs), xx = bx + (c - 1) * bw, mainCh = c === 1 || c === 6 || c === 11;
      for (let k = 0; k < segs; k++) {
        g.fillStyle = k < lit ? (k >= 5 ? E.red : k >= 3 ? E.or2 : E.or) : E.dk;
        g.fillRect(xx + bw * 0.14, bb - (k + 1) * sh + sh * 0.14, bw * 0.72, sh * 0.74);
      }
      if (lit > 0) { g.fillStyle = E.wht; g.fillRect(xx + bw * 0.14, bb - lit * sh - 1.5, bw * 0.72, 1.5); }
      tx(g, String(c), xx + bw / 2, bb + u * 0.98, u * (mainCh ? 0.68 : 0.58), mainCh ? E.or2 : E.or, FM(), 'center');
    }
  },

  sensPanel(g, x, y, w, h, u, t) {
    const y0 = panel(g, x, y, w, h, u, T.tr('433 MHz · SENSORS'), '感知');
    T.hits.rect(x, y, w, h, { go: 'sub' });
    const d = S.sens, list = (d && d.sensors) || [];
    let ry = y0 + u * 1.3;
    tx(g, 'ID', x + u * 0.6, ry, u * 0.62, E.or); tx(g, T.tr('TEMP · HUM'), x + w * 0.64, ry, u * 0.62, E.or, FM(), 'right'); tx(g, T.tr('SEEN'), x + w - u * 0.6, ry, u * 0.62, E.or, FM(), 'right');
    ry += u * 1.45;
    if (!list.length) { tx(g, T.tr(d ? 'LISTENING · NO SENSORS YET' : 'NO DATA'), x + u * 0.6, ry, u * 0.7, E.dim); }
    for (const s of list) {
      if (ry > y + h - u * 0.5) break;
      const rem = s.kind === 'remote', id = rem ? 'REMOTE #' + String(s.id).split('-').pop().toUpperCase() : (s.p + '·' + s.ch).toUpperCase();
      const col = rem ? E.cy : E.txt, fresh = s.ago < 3;
      font(g, u * 0.72, FM()); tx(g, T.fit(g, id, w * 0.4), x + u * 0.6, ry, u * 0.72, col);
      if (!rem && s.t != null) tx(g, (s.t > 0 ? '+' : '') + s.t.toFixed(1) + '°' + (s.h != null ? ' ' + s.h + '%' : ''), x + w * 0.64, ry, u * 0.9, E.or2, FT(), 'right', '600 ');
      else tx(g, rem ? 'BTN ' + (s.btn ?? '—') : '—', x + w * 0.64, ry, u * 0.72, E.dim, FM(), 'right');
      tx(g, fresh && blink(t, 2) ? 'NOW' : T.ago(s.ago), x + w - u * 0.6, ry, u * 0.72, fresh ? E.grn : E.or, FM(), 'right');
      if (s.bat === 0) tx(g, 'BAT', x + w * 0.67, ry, u * 0.6, E.red);
      ry += u * 1.35;
    }
  },

  // ---- CENTERPIECE: Evangelion AT-Field × Zen Dharma Mandala Radar ----
  radar(g, x, y, w, h, u, t, now) {
    const nB = [...S.dev.values()].filter(e => e.alive).length, nA = [...S.aps.values()].filter(a => a.alive).length, nR = ((S.sens && S.sens.sensors) || []).length;
    const y0 = panel(g, x, y, w, h, u, T.tr('RADIO HORIZON · 電波地平'), '索敵', 'RSSI · ' + nB + ' BLE · ' + nA + ' AP · ' + nR + ' RF');
    const cx = x + w / 2, cy = y0 + (y + h - y0) / 2, R = Math.min(w * 0.44, (y + h - y0) * 0.435);
    if (!(R > 8)) return;

    // Pre-rendered sacred mandala + AT-Field octagram + 72-tick compass -> 1 single GPU blit per frame
    const rh = Math.max(1, Math.ceil(y + h - y0)), rw = Math.max(1, Math.ceil(w));
    const dpr = Math.min(2, this.cv.dpr || 1);
    const rKey = `${rw}x${rh}@${dpr}:${this.th}:${T.LANG || ''}`;
    if (this._radKey !== rKey) {
      this._radKey = rKey;
      const rc = this._radBg || (this._radBg = document.createElement('canvas'));
      rc.width = Math.ceil(rw * dpr); rc.height = Math.ceil(rh * dpr);
      const rg = rc.getContext('2d'); rg.setTransform(dpr, 0, 0, dpr, 0, 0);
      const lcx = w / 2, lcy = (y + h - y0) / 2;
      const gr = rg.createRadialGradient(lcx, lcy, 0, lcx, lcy, R * 1.28);
      gr.addColorStop(0, orA(.13)); gr.addColorStop(0.65, orA(.04)); gr.addColorStop(1, orA(0));
      rg.fillStyle = gr; rg.fillRect(1, 0, w - 2, rh - 1);

      // Top-left sacred callout + vertical hanko seal
      jp(rg, '索敵', u * 0.85, u * 3.15, u * 2.75, E.or, 'left');
      tx(rg, T.tr('SCANNING ALL BANDS'), u * 0.9, u * 4.25, u * 0.68, E.or);
      tx(rg, 'DHARMA · AT-FIELD', u * 0.9, u * 5.15, u * 0.56, E.dim);
      hanko(rg, w - u * 3.2, u * 0.85, u * 2.2, '眼', orA(0.55), false);

      // Inscribed Sacred 8-Pointed Dharma / AT-Field Octagram inside outer ring
      rg.strokeStyle = orA(0.11); rg.lineWidth = 1;
      poly(rg, lcx, lcy, R * 0.995, 8, 0); rg.stroke();
      poly(rg, lcx, lcy, R * 0.995, 8, Math.PI / 8); rg.stroke();
      poly(rg, lcx, lcy, R * 0.60, 8, Math.PI / 8); rg.stroke();

      // 5 Concentric Range Circles
      for (let i = 1; i <= 5; i++) {
        rg.strokeStyle = orA(i === 5 ? 0.85 : i === 3 ? 0.38 : 0.24);
        rg.lineWidth = i === 5 ? 1.4 : 1;
        rg.setLineDash(i % 2 ? [] : [u * 0.22, u * 0.28]);
        rg.beginPath(); rg.arc(lcx, lcy, R * i / 5, 0, T.TAU); rg.stroke();
      }
      rg.setLineDash([]);

      // 8 Dharma Spokes + 4 Cardinal Crosshairs
      rg.strokeStyle = orA(.17); rg.lineWidth = 1; rg.beginPath();
      for (let a = 0; a < 8; a++) {
        const q = a * Math.PI / 4;
        rg.moveTo(lcx + R * 0.12 * Math.cos(q), lcy + R * 0.12 * Math.sin(q));
        rg.lineTo(lcx + R * Math.cos(q), lcy + R * Math.sin(q));
      }
      rg.stroke();

      // Outer 72-tick NERV Azimuth Compass + Degree Callouts
      rg.strokeStyle = E.or; rg.beginPath();
      for (let a = 0; a < 72; a++) {
        const q = a * Math.PI / 36, maj = a % 9 === 0, mid = a % 3 === 0;
        const l = maj ? u * 0.95 : mid ? u * 0.55 : u * 0.3;
        rg.moveTo(lcx + (R + u * 0.35) * Math.cos(q), lcy + (R + u * 0.35) * Math.sin(q));
        rg.lineTo(lcx + (R + u * 0.35 + l) * Math.cos(q), lcy + (R + u * 0.35 + l) * Math.sin(q));
      }
      rg.stroke();
      for (let deg = 0; deg < 360; deg += 45) {
        const q = (deg - 90) * Math.PI / 180, rr = R + u * 1.75;
        tx(rg, String(deg).padStart(3, '0') + '°', lcx + rr * Math.cos(q), lcy + rr * Math.sin(q) + u * 0.22, u * 0.56, deg % 90 === 0 ? E.or2 : E.dim, FM(), 'center');
      }

      ['-45', '-60', '-75', '-90'].forEach((s, i) => tx(rg, s + ' dBm', lcx + u * 0.35, lcy - R * (i + 1) / 5 - u * 0.35, u * 0.58, E.or));
      const ly = rh - u * 0.75; let lx = u * 0.8;
      for (const [c, s] of [[E.grn, T.tr('SAVED 自')], [E.or2, T.tr('KNOWN 既')], [E.red, T.tr('NEW 新')], [E.cy, T.tr('◆ WI-FI AP')], [E.wht, T.tr('▲ 433 RF')]]) {
        rg.fillStyle = c; rg.beginPath(); rg.arc(lx + u * 0.25, ly - u * 0.25, u * 0.22, 0, T.TAU); rg.fill();
        tx(rg, s, lx + u * 0.7, ly, u * 0.60, E.or); font(rg, u * 0.60, FM()); lx += u * 1.5 + rg.measureText(s).width;
      }
    }
    g.drawImage(this._radBg, x, y0, rw, rh);

    // Live counter-rotating sacred Zen Ensō arcs around the Heavenly Eye
    g.strokeStyle = orA(0.52); g.lineWidth = Math.max(1.2, u * 0.11);
    g.beginPath(); g.arc(cx, cy, R * 0.17, t * 0.45, t * 0.45 + Math.PI * 1.62); g.stroke();
    g.strokeStyle = orA(0.28); g.lineWidth = 1;
    g.beginPath(); g.arc(cx, cy, R * 0.24, -t * 0.32 + 1.2, -t * 0.32 + 1.2 + Math.PI * 1.35); g.stroke();

    // Live Temple Water Ripples from real RF events
    for (const rp of this.rips) {
      const rr = R * (0.1 + 0.9 * rp.p);
      g.strokeStyle = rp.hot ? `rgba(255,42,31,${(0.75 * (1 - rp.p)).toFixed(3)})` : orA((0.55 * (1 - rp.p)).toFixed(3));
      g.lineWidth = rp.hot ? 1.8 : 1.3;
      g.beginPath(); g.arc(cx, cy, rr, 0, T.TAU); g.stroke();
    }

    // Dual-Edge NERV Radar Sweep Cone
    const sw = this.sweep;
    if (g.createConicGradient) {
      const span = 0.88, cg = g.createConicGradient(sw - span, cx, cy), f = span / T.TAU;
      cg.addColorStop(0, orA(0)); cg.addColorStop(f * 0.98, orA(0.26)); cg.addColorStop(f, orA(0)); cg.addColorStop(1, orA(0));
      g.fillStyle = cg; g.beginPath(); g.moveTo(cx, cy); g.arc(cx, cy, R, sw - span, sw); g.closePath(); g.fill();
    } else {
      for (let i = 0; i < 6; i++) {
        const a1 = sw - i * 0.14, a2 = a1 - 0.145;
        g.fillStyle = orA((0.22 * (1 - i / 6)).toFixed(3));
        g.beginPath(); g.moveTo(cx, cy); g.arc(cx, cy, R, a2, a1); g.closePath(); g.fill();
      }
    }
    g.strokeStyle = E.or2; g.lineWidth = u * 0.13; g.beginPath(); g.moveTo(cx, cy); g.lineTo(cx + R * Math.cos(sw), cy + R * Math.sin(sw)); g.stroke();

    const lit = a => { let d = (sw - a) % T.TAU; if (d < 0) d += T.TAU; return 1 - Math.min(1, d / 2.6); };
    const pos = (id, rs) => { const a = (T.hash(id) % 3600) / 3600 * T.TAU; const rr = R * (0.10 + 0.88 * (1 - T.pct(rs))); return [a, cx + rr * Math.cos(a), cy + rr * Math.sin(a)]; };
    const labels = [];

    for (const a of S.aps.values()) {
      if (!a.alive && a.h < 0.05) continue;
      const [q, px, py] = pos('ap' + a.k, a.rs), s = u * 0.42, k = 0.45 + 0.55 * lit(q);
      g.globalAlpha = a.h * k; g.fillStyle = E.cy;
      poly(g, px, py, s, 4, 0); g.fill();
    }
    for (const s of ((S.sens && S.sens.sensors) || [])) {
      if (s.rssi == null) continue;
      const [q, px, py] = pos('rf' + s.id, s.rssi), z = u * 0.52, k = 0.5 + 0.5 * lit(q);
      g.globalAlpha = k; g.fillStyle = E.wht;
      poly(g, px, py, z, 3, -Math.PI / 2); g.fill();
      if (s.t != null) labels.push([px, py, (s.t > 0 ? '+' : '') + s.t.toFixed(1) + '°', E.wht, false]);
    }
    for (const e of S.dev.values()) {
      if (e.h < 0.05) continue;
      const [q, px, py] = pos(e.id, e.rs), nw = S.isNew(e, now), trk = S.isTrk(e), hot = nw || trk, k = 0.4 + 0.6 * lit(q);
      const col = e.label ? E.grn : hot ? E.red : E.or2, r = u * (hot ? 0.44 : 0.31);
      if (hot) {
        // Radial lock line from center Eye + rotating Evangelion Hexagonal AT-Field reticle
        g.globalAlpha = 0.28 * e.h; g.strokeStyle = col; g.lineWidth = 1;
        g.beginPath(); g.moveTo(cx, cy); g.lineTo(px, py); g.stroke();
        g.globalAlpha = blink(t, 2) ? 0.95 : 0.45; g.lineWidth = u * 0.11;
        poly(g, px, py, u * 1.35, 6, t * 1.2); g.stroke();
        poly(g, px, py, u * 1.85, 6, -t * 0.8); g.lineWidth = 0.8; g.stroke();
      }
      g.globalAlpha = e.h * (hot ? 1 : k); g.fillStyle = col;
      g.beginPath(); g.arc(px, py, r, 0, T.TAU); g.fill();
      if (e.label || e.name || hot) labels.push([px, py, e.label || e.name || e.type || 'new', col, hot]);
      T.hits.circle(px, py, u * 1.25, { go: 'ble' });
    }
    g.globalAlpha = 1;

    const boxes = [];
    for (const [px, py, s, col, hot] of labels) {
      if (boxes.length >= 14) break;
      g.font = `500 ${u * 0.72}px ${FC()}`; const str = T.fit(g, s, u * 9), bw = g.measureText(str).width;
      const b = [px + u * 0.8, py - u * 0.55, bw + u * 0.4, u * 1.05];
      if (boxes.some(q => b[0] < q[0] + q[2] && q[0] < b[0] + b[2] && b[1] < q[1] + q[3] && q[1] < b[1] + b[3])) continue;
      boxes.push(b);
      if (hot) { g.fillStyle = 'rgba(25,2,0,.78)'; g.fillRect(b[0] - u * 0.2, b[1], b[2], b[3]); }
      tx(g, str, b[0], py + u * 0.25, u * 0.72, col, FC(), 'left', '500 ');
    }

    // Center Heavenly Eye (眼) inside octagonal sanctuary seal
    g.fillStyle = K() ? E.bg : '#050302';
    poly(g, cx, cy, u * 1.15, 8, Math.PI / 8); g.fill();
    g.strokeStyle = E.or2; g.lineWidth = u * 0.14; g.stroke();
    jp(g, '眼', cx, cy + u * 0.42, u * 1.15, E.or2, 'center');

    // Warning plate 接近警告
    const age = t - this.alertT;
    if (age < 20 && this.alertTxt && S.cfg.al_plate) {
      const bw = Math.min(u * 15.5, w * 0.38), bh = u * 7.8, bx = x + w - bw - u * 0.8, by = y0 + u * 0.8;
      cutBox(g, bx, by, bw, bh, u * 0.7);
      g.fillStyle = E.plateBg; g.fill();
      g.save(); cutBox(g, bx, by, bw, bh, u * 0.7); g.clip();
      stripes(g, bx, by, bw, u * 0.58, E.red, u * 0.5); stripes(g, bx, by + bh - u * 0.58, bw, u * 0.58, E.red, u * 0.5);
      g.restore();
      cutBox(g, bx, by, bw, bh, u * 0.7); g.strokeStyle = E.red; g.lineWidth = u * 0.11; g.stroke();
      if (age < 4 ? blink(t, 2) : true) jp(g, '接近警告', bx + u * 0.7, by + u * 3.35, u * 2.35, E.red, 'left');
      tx(g, T.tr(this.alertTxt.tr ? 'TRACKER · NEARBY' : 'PROXIMITY · NEW DEVICE'), bx + u * 0.7, by + u * 4.55, u * 0.78, E.red, FT(), 'left', '600 ');
      g.font = `500 ${u * 0.75}px ${FC()}`;
      tx(g, T.fit(g, String(this.alertTxt.n), bw - u * 1.4), bx + u * 0.7, by + u * 5.75, u * 0.75, E.txt, FC(), 'left', '500 ');
      tx(g, this.alertTxt.r + ' dBm · ' + Math.round(age) + 's ' + T.tr('AGO'), bx + u * 0.7, by + u * 6.8, u * 0.7, E.txt);
    }
  },

  wxPanel(g, x, y, w, h, u, t) {
    const y0 = panel(g, x, y, w, h, u, T.tr('OUTSIDE · 外気'), '天気');
    T.hits.rect(x, y, w, h, { go: 'gal' });
    const d = S.wx || {}, has = d.temp != null;
    const W = S.wxs, calm = W.storm > 0.5 ? ['STORM', '嵐', E.red] : W.rain > 0.4 ? ['RAIN', '雨', E.or2] : W.wind > 0.4 ? ['WIND', '風', E.or2] : W.clouds > 0.5 ? ['CLOUDY', '曇', E.or2] : ['CALM', '静', E.grn];
    const big = has ? (d.temp > 0 ? '+' : '') + Number(d.temp).toFixed(1) : '—';
    tx(g, big, x + u * 0.8, y0 + u * 4.5, u * 4.5, E.or2, FT(), 'left', '600 ');
    if (has) { font(g, u * 4.5, FT(), '600 '); tx(g, '°C', x + u * 1.0 + g.measureText(big).width, y0 + u * 4.5, u * 1.9, E.or2, FT(), 'left', '600 '); }
    // Zen Ensō Weather Seal on top-right of panel
    const sr = u * 1.65, scx = x + w - u * 2.4, scy = y0 + u * 2.7;
    g.strokeStyle = calm[2]; g.lineWidth = 1.3;
    g.beginPath(); g.arc(scx, scy, sr, t * 0.3, t * 0.3 + Math.PI * 1.68); g.stroke();
    jp(g, calm[1], scx, scy + sr * 0.38, sr * 1.05, calm[2], 'center');

    const rows = [[T.tr('HUMIDITY'), d.hum != null ? d.hum + '%' : '—', E.txt], [T.tr('SOURCE'), has ? '433 · ' + (d.tsrc || '?') : T.tr('NO OUTDOOR SENSOR'), has ? E.txt : E.dim], [T.tr('RADIO WEATHER'), T.tr(calm[0]) + ' · ' + calm[1], calm[2]]];
    let ry = y0 + u * 6.1;
    for (const [k, v, c] of rows) { if (ry > y + h - u * 0.4) break; tx(g, k, x + u * 0.8, ry, u * 0.68, E.txt); tx(g, v, x + w - u * 0.8, ry, u * 0.68, c, FC(), 'right'); ry += u * 1.25; }
  },

  audioPanel(g, x, y, w, h, u, t) {
    const sn = S.snd && S.snd.ok;
    const y0 = panel(g, x, y, w, h, u, T.tr('AUDIO · 音'), '', sn ? Math.round(S.level) + ' dB' : T.tr('MIC OFF'));
    T.hits.rect(x, y, w, h, { go: 'snd' });
    const top = y0 + u * 0.35, hh = y + h - top - u * 0.35, mid = top + hh / 2;
    g.strokeStyle = orA(.14); g.lineWidth = 1; g.beginPath();
    for (let i = 1; i < 8; i++) { const xx = x + w * i / 8; g.moveTo(xx, top); g.lineTo(xx, top + hh); }
    g.moveTo(x + u * 0.3, mid); g.lineTo(x + w - u * 0.3, mid); g.stroke();
    // Dual-symmetric Zen harmonic envelope + live trace
    g.fillStyle = K() ? 'rgba(229,38,47,.20)' : 'rgba(105,255,110,.16)';
    g.beginPath(); g.moveTo(x + u * 0.3, mid);
    for (let i = 0; i < 64; i++) {
      const xx = x + u * 0.3 + (w - u * 0.6) * i / 63;
      const v = sn ? Math.abs(S.wave[i]) : 0.02 * Math.sin(t * 2 + i * 0.2);
      g.lineTo(xx, mid - v * hh * 0.46);
    }
    for (let i = 63; i >= 0; i--) {
      const xx = x + u * 0.3 + (w - u * 0.6) * i / 63;
      const v = sn ? Math.abs(S.wave[i]) : 0.02 * Math.sin(t * 2 + i * 0.2);
      g.lineTo(xx, mid + v * hh * 0.46);
    }
    g.closePath(); g.fill();
    g.strokeStyle = E.grn; g.lineWidth = u * 0.11; g.beginPath();
    for (let i = 0; i < 64; i++) { const xx = x + u * 0.3 + (w - u * 0.6) * i / 63, yy = mid - (sn ? S.wave[i] : 0) * hh * 0.45; i ? g.lineTo(xx, yy) : g.moveTo(xx, yy); }
    g.stroke();
  },

  probePanel(g, x, y, w, h, u, t, now) {
    const y0 = panel(g, x, y, w, h, u, T.tr('PROBES · 探針'), '解析');
    const bx = x + u * 0.6, bw = w - u * 1.2, bh = (y + h - y0 - u * 2.6) / 2;
    // IR
    const I = S.ir && S.ir.last, fI = T.clamp(1 - (t - this.flashIr) / 1.2);
    let by = y0 + u * 0.6;
    cutBox(g, bx, by, bw, bh, u * 0.5);
    g.fillStyle = `rgba(${E.cyRGB},${0.04 + 0.24 * fI})`; g.fill();
    g.strokeStyle = E.cy; g.lineWidth = u * (0.06 + 0.1 * fI); g.stroke();
    jp(g, '赤外線', bx + u * 0.5, by + u * 1.2, u * 0.75, E.cy, 'left');
    tx(g, 'IR' + (I ? ' · ' + T.ago((now - S.ir.t) / 1000) + ' ' + T.tr('AGO') : T.tr(' · LISTENING')), bx + u * 3.2, by + u * 1.2, u * 0.64, E.cy);
    if (I) {
      g.font = `600 ${u * 1.45}px ${FT()}`; tx(g, T.fit(g, (I.brand || I.p).toUpperCase(), bw - u), bx + u * 0.5, by + u * 2.85, u * 1.45, E.cy, FT(), 'left', '600 ');
      tx(g, I.p + ' · ' + T.hexs(I.addr, 4) + ' · ' + T.hexs(I.cmd) + (I.bits ? ' · ' + I.bits + 'b' : '') + (S.ir.hist[0] && S.ir.hist[0].x > 1 ? ' · ×' + S.ir.hist[0].x : ''), bx + u * 0.5, by + u * 3.95, u * 0.61, E.txt);
      const raw = I.raw || [];
      if (raw.length > 2 && bh > u * 5.2) {
        let tot = 0; for (const v of raw) tot += v;
        let xx = bx + u * 0.5; const ww = bw - u, yh = by + u * 4.55, yl = Math.min(by + bh - u * 0.35, yh + u * 1.05);
        g.strokeStyle = E.cy; g.lineWidth = u * 0.08; g.beginPath(); g.moveTo(xx, yl);
        raw.forEach((v, i) => { const nx = xx + ww * v / tot, yy = i % 2 ? yl : yh; g.lineTo(xx, yy); g.lineTo(nx, yy); xx = nx; });
        g.stroke();
      }
    } else tx(g, T.tr('POINT A REMOTE AT TENGEN'), bx + u * 0.5, by + u * 2.8, u * 0.68, E.dim);
    T.hits.rect(bx, by, bw, bh, { go: 'probe' });

    // NFC
    by += bh + u * 0.6;
    const N = S.nfc && S.nfc.last, fN = T.clamp(1 - (t - this.flashNfc) / 1.2);
    cutBox(g, bx, by, bw, bh, u * 0.5);
    g.fillStyle = `rgba(${E.or2RGB},${0.04 + 0.24 * fN})`; g.fill();
    g.strokeStyle = E.or2; g.lineWidth = u * (0.06 + 0.1 * fN); g.stroke();
    jp(g, '近接', bx + u * 0.5, by + u * 1.2, u * 0.75, E.or2, 'left');
    tx(g, 'NFC' + (N ? ' · ' + T.ago((now - S.nfc.t) / 1000) + ' ' + T.tr('AGO') : S.nfc && S.nfc.err ? T.tr(' · NO READER') : T.tr(' · WAITING')), bx + u * 2.4, by + u * 1.2, u * 0.64, E.or2);
    if (N) {
      g.font = `600 ${u * 1.45}px ${FT()}`; tx(g, T.fit(g, String(N.fam || 'CARD').toUpperCase(), bw - u), bx + u * 0.5, by + u * 2.85, u * 1.45, E.or2, FT(), 'left', '600 ');
      g.font = `500 ${u * 0.61}px ${FC()}`; tx(g, T.fit(g, (N.hint ? N.hint + ' · ' : '') + 'UID ' + (N.uid || '—') + (N.sak ? ' · SAK ' + N.sak : ''), bw - u), bx + u * 0.5, by + u * 3.95, u * 0.61, E.txt, FC(), 'left', '500 ');
    } else tx(g, T.tr('TOUCH A CARD TO TENGEN'), bx + u * 0.5, by + u * 2.8, u * 0.68, E.dim);
    T.hits.rect(bx, by, bw, bh, { go: 'probe' });

    const p = S.pc, py = y + h - u * 0.68;
    tx(g, 'PC 機', bx, py, u * 0.65, E.txt);
    const ps = p && p.live ? 'CPU ' + Math.round(p.cpu || 0) + '%' + (p.ram != null ? ' · RAM ' + Math.round(p.ram) + '%' : '') + (p.trk ? ' · ♪ ' + (p.trk.play ? 'PLAY' : 'PAUSE') : '') : 'OFFLINE';
    tx(g, ps, bx + bw, py, u * 0.65, p && p.live ? E.txt : E.dim, FM(), 'right');
  },

  wfPanel(g, x, y, w, h, u, dt) {
    const d = S.sens;
    const y0 = panel(g, x, y, w, h, u, T.tr('433.92 MHz · PULSE FLOW'), '滝', d ? (d.pk || 0) + ' PKTS' : 'OFF');
    T.hits.rect(x, y, w, h, { go: 'sub' });
    const top = y0 + u * 0.3, H = y + h - top - u * 0.3, rowH = H / 5;
    g.fillStyle = K() ? '#111' : '#0a0502'; g.fillRect(x + 2, top, w - 4, H);
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
    if (!this.wf.length) tx(g, T.tr(d ? 'WAITING FOR 433 MHz PACKETS' : 'SENSORS STREAM OFF'), x + w / 2, top + H / 2 + u * 0.3, u * 0.78, E.dim, FM(), 'center');
  },

  wfTick(dt) {
    const d = S.sens, last = d && d.last;
    if (d && d.pk !== this.wfPk && last && last.pt) {
      if (this.wfPk >= 0) { this.wf.unshift({ pt: last.pt, cls: last.cls, p: last.p, a: 1 }); this.addRipple(false); }
      this.wfPk = d.pk; if (this.wf.length > 5) this.wf.pop();
    }
  },

  actTick(dt, t) {
    const A = S.activity(), k = T.ease(dt, 1.5);
    this.lvl += (A.v - this.lvl) * k;
    this.parts = this.parts || { ble: 0, rf: 0, snd: 0, prb: 0 };
    for (const q in A.parts) this.parts[q] += (A.parts[q] - this.parts[q]) * k;
    this.partsEn = A.en;
  },

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
    const y0 = panel(g, x, y, w, h, u, T.tr('ACTIVITY LEVEL · 活動度'), '', T.tr(st[0]) + ' · ' + Math.round(v * 100) + '%', hot);
    // Framed Hanko stamp on the right
    const sz = Math.min(u * 3.8, y + h - y0 - u * 0.8);
    const hx = x + w - sz - u * 0.7, hy = y0 + (y + h - y0 - sz) / 2;
    cutBox(g, hx, hy, sz, sz, sz * 0.14);
    g.strokeStyle = st[2]; g.lineWidth = 1.4; g.stroke();
    jp(g, st[1], hx + sz / 2, hy + sz * 0.68, sz * 0.44, st[2], 'center');

    const n = 28, bx = x + u * 0.6, bw = hx - bx - u * 0.8, gap = u * 0.15, sw = (bw - gap * (n - 1)) / n, top = y0 + u * 0.5, bh = Math.max(u * 0.8, (y + h - top) * 0.48);
    const lit = Math.round(v * n), warm = Math.round(S.cfg.al_warn / 100 * n);
    for (let i = 0; i < n; i++) {
      const on = i < lit, red = i >= warm;
      g.fillStyle = on ? (red ? (blink(t, 1.5) ? E.red : E.redDk) : E.or) : E.dk;
      g.fillRect(bx + i * (sw + gap), top, sw, bh);
    }
    for (const [p, c] of [[S.cfg.al_warn, E.or2], [S.cfg.al_alarm, E.red]]) { const xx = bx + bw * p / 100; g.fillStyle = c; g.fillRect(xx - 1, top - u * 0.25, 2, bh + u * 0.5); }
    this.minis(g, bx, top + bh + u * 0.2, bw, y + h - (top + bh + u * 0.2) - u * 0.2, u, E.or, E.dk, E.txt);
  },

  log(g, x, y, w, h, u, t) {
    g.fillStyle = E.or; g.fillRect(x, y, w, u * 0.12);
    const my = y + h * 0.72;
    stripes(g, x, y + u * 0.45, u * 2.0, h - u * 0.65, E.stripe, u * 0.45);
    jp(g, '記録', x + u * 2.55, my, u * 1.18, E.or, 'left');
    let lx = x + u * 5.5;
    g.save(); g.beginPath(); g.rect(lx, y, w - (lx - x), h); g.clip();
    const tagCol = { BLE: E.red, IR: E.cy, NFC: E.or2, '433': E.grn, SUB: E.red, PC: E.txt, PWR: E.red };
    if (!S.events.length) tx(g, T.tr('QUIET SO FAR'), lx, my, u * 0.78, E.dim);
    for (const ev of S.events.slice(0, 8)) {
      const s1 = hms(new Date(ev.t)) + ' ', s2 = ev.tag + ' ', s3 = String(ev.text);
      font(g, u * 0.76, FC(), '500 ');
      tx(g, s1, lx, my, u * 0.76, E.txt, FC(), 'left', '500 '); lx += g.measureText(s1).width;
      tx(g, s2, lx, my, u * 0.76, ev.alert ? E.red : tagCol[ev.tag] || E.or, FC(), 'left', '500 '); lx += g.measureText(s2).width;
      tx(g, s3, lx, my, u * 0.76, E.txt, FC(), 'left', '500 '); lx += g.measureText(s3).width + u * 0.8;
      tx(g, '◆', lx, my, u * 0.65, E.dim); lx += u * 1.35;
      if (lx > x + w) break;
    }
    if (blink(t)) tx(g, '_', lx, my, u * 0.76, E.or);
    g.restore();
  },
});
})();
