// ---- start-up: boot bar first, then the splash ----
// 1) BOOT: step by step bring up the stick link, the section's radio (Bluetooth by default), first data and the scene (a few frames under the curtain) while the bar fills.
// 2) SPLASH (like core/boot.cpp on the stick): everything is up, the scene is paused so the animation runs alone without lag.
// 3) CHOICE: LITE / FULL. While you choose, Bluetooth > City already runs under the curtain, so the city does not start from zero.
//    LITE = the stick's light phone page (/lite), FULL = this scene.
T.boot = (first) => {
  const box = document.getElementById('boot'), cv = document.getElementById('bootCv');
  // boot and choice run on every page open; with reduced motion (system setting) the splash is skipped
  if (new URLSearchParams(location.search).has('noboot')) { box.remove(); T.app.go((T.views.find(v => v.id === first) || T.views[0]).id); return; }   // for development
  // a full start leads to HQ (with BLE already warming up on the choice screen)
  const view = T.views.find(v => v.id === first) || T.views.find(v => v.id === 'hq') || T.views[0];
  const g = cv.getContext('2d'), MS = 2700;
  let t0 = 0;
  const seg = (t, a, b) => t <= a ? 0 : t >= b ? 1 : (t - a) / (b - a), eo = x => 1 - Math.pow(1 - T.clamp(x), 3);
  const BL = [[35, 72], [80, 38], [140, 88], [200, 55], [250, 80], [310, 30], [335, 64]];
  const POOL = 'ABCDEFGHKLMNPRSTVXZ0123456789', TITLE = 'TENGEN', SUB = '// ENVIRONMENT ANALYZER';
  const hash = x => { x ^= x >>> 16; x = Math.imul(x, 0x7feb352d); x ^= x >>> 15; x = Math.imul(x, 0x846ca68b); x ^= x >>> 16; return x >>> 0; };
  const end = () => { phase = 'done'; T.app.hold = false; box.classList.add('done'); setTimeout(() => box.remove(), 400); };
  let phase = 'launch', L = null;
  let pick = null, picks = [], pickT = 0;
  // device choice only when entering via the stick ("/"); /tablet is already the tablet, go straight to the section
  const askDevice = !T.native && !/\/(tablet|full)\/?$/.test(location.pathname);   // not needed in the app
  const afterAnim = now => askDevice ? toChoice(now) : end();
  const toChoice = now => { phase = 'choice'; pickT = now; T.app.hold = false; box.style.background = 'transparent'; };   // scene comes alive under the curtain
  box.addEventListener('pointerdown', e => {
    if (phase === 'anim') { afterAnim(performance.now()); return; }              // tap skips the splash
    if (phase !== 'choice') return;
    const h = picks.find(p => e.clientX >= p.x && e.clientX <= p.x + p.w && e.clientY >= p.y && e.clientY <= p.y + p.h);
    if (!h) return;
    pick = h.id;
    if (h.id === 'phone') { const q = new URLSearchParams(location.search).get('host'); location.href = (q ? `http://${q}` : '') + '/lite'; }
    else end();
  });
  // choice screen: two panels with device silhouettes
  const device = (x, y, w, h, r, col) => {
    T.cutPath(g, x, y, w, h, r); g.strokeStyle = col; g.lineWidth = 2; g.stroke();
    g.fillStyle = `rgba(${T.RGB.acc},.06)`; g.fill();
    g.strokeStyle = T.rgba(col, 0.5); g.lineWidth = 1; g.strokeRect(x + w * 0.08, y + h * 0.08, w * 0.84, h * 0.84);
  };
  const choiceFrame = (now, W, H, u) => {
    const k = T.clamp((now - pickT) / 450), eo = 1 - Math.pow(1 - k, 3);
    g.clearRect(0, 0, W, H);
    g.fillStyle = `rgba(${T.RGB.bg},${0.94 - 0.12 * eo})`; g.fillRect(0, 0, W, H);  // the live city shows faintly through the curtain
    const cx = W / 2, cy = H / 2, portrait = H > W;
    T.txt(g, '// SELECT DEVICE', cx, cy - u * (portrait ? 17 : 11), u * 1.9, T.C.acc, 't', 'center');
    T.txt(g, 'CITY IS WARMING UP · ' + T.S.dev.size + ' DEVICES', cx, cy - u * (portrait ? 15 : 9), u * 0.9, T.C.label, 'm', 'center');
    const pw = portrait ? Math.min(W * 0.7, u * 22) : Math.min(W * 0.3, u * 20), ph = portrait ? u * 11 : u * 14, gap = u * 2.5;
    const items = [{ id: 'phone', t: 'LITE', s: 'light page · phone' }, { id: 'tablet', t: 'FULL', s: 'full scene · tablet' }];
    picks = items.map((it, i) => {
      const x = portrait ? cx - pw / 2 : cx + (i ? gap / 2 : -gap / 2 - pw), y = portrait ? cy - u * 12 + i * (ph + gap) : cy - ph / 2 + u * 2;
      return { ...it, x, y: y + (1 - eo) * u * 2, w: pw, h: ph };
    });
    for (const p of picks) {
      const on = p.id === 'tablet', col = on ? T.C.acc : T.C.label;
      g.globalAlpha = eo;
      T.panel(g, p.x, p.y, p.w, p.h, u, on ? 'rgba(8,24,32,.92)' : 'rgba(8,16,22,.9)', on ? T.C.acc : T.C.line);
      if (on) T.brackets(g, p.x - 4, p.y - 4, p.w + 8, p.h + 8, u, T.C.acc, 1.5);
      const sx = p.x + p.w / 2, sy = p.y + p.h * 0.4;
      if (p.id === 'phone') device(sx - u * 1.6, sy - u * 2.9, u * 3.2, u * 5.8, u * 0.6, col);
      else device(sx - u * 4.2, sy - u * 2.7, u * 8.4, u * 5.4, u * 0.6, col);
      T.txt(g, p.t, sx, p.y + p.h * 0.8, u * 2, on ? T.C.hot : T.C.label, 't', 'center');
      T.txt(g, p.s.toUpperCase(), sx, p.y + p.h * 0.8 + u * 1.4, u * 0.8, T.C.dim, 'm', 'center');
      g.globalAlpha = 1;
    }
  };
  const want = (typeof view.want === 'function' ? view.want() : view.want) || 'st';
  const needBle = want.split(/[,&]/).includes('ble');
  const startLaunch = now => {
    phase = 'launch';
    L = { t0: now, bar: 0, step: 0, mounted: 0, warm: 0, stepT: now,
      steps: [
        ['LINK', 'connecting to TENGEN', () => T.Stream.mode === 'live' || T.Stream.mode === 'poll'],
        ['DEVICE', 'device status', () => !!T.S.st],
        ['RADIO', needBle ? 'Bluetooth' : 'section radio', () => !needBle || (T.S.ble && T.S.ble.on)],
        ['DATA', 'first data', () => !needBle || T.S.dev.size > 0],
        ['RENDER', 'scene', () => L.warm >= 3],
      ] };
    T.Stream.connect(want);
  };
  startLaunch(performance.now());
  const launchFrame = (now, W, H, u) => {
    const cx = W / 2, cy = H / 2;
    // step done or waited too long (6 s): move on; the scene opens even without a link
    const st = L.steps[L.step];
    if (st && (st[2]() || now - L.stepT > 6000)) { st.ok = st[2](); L.step++; L.stepT = now; }
    if (L.step >= 4 && !L.mounted) { L.mounted = 1; T.app.go(view.id); }   // scene draws under the curtain
    if (L.mounted) L.warm++;
    const target = Math.min(1, (L.step + (L.step < 5 ? 0.6 * Math.min(1, (now - L.stepT) / 1500) : 0)) / 5);
    L.bar += (target - L.bar) * 0.12;
    g.fillStyle = T.C.bg; g.fillRect(0, 0, W, H);
    // layout: header, percent, [天] bar [眼], steps. 眼 "opens" when all is ready
    const bw = Math.min(W * 0.66, u * 46), x = cx - bw / 2, sz = u * 3.4, gap = u * 1.1;
    const barX = x + sz + gap, barW = bw - 2 * (sz + gap), barY = cy - u * 0.55, ready = L.bar > 0.97;
    T.header(g, x, cy - u * 8.6, bw, u, 'Starting', view.title, false);
    T.txt(g, 'v' + T.VERSION + (T.native ? ' · app' : ''), W - u, H - u, u * 0.7, T.C.dim, 'm', 'right');
    T.txt(g, 'INITIALIZING', barX, cy - u * 1.6, u * 0.8, T.C.dim);
    T.txt(g, Math.round(L.bar * 100) + '%', barX + barW, cy - u * 1.35, u * 1.9, T.C.hot, 't', 'right');
    T.drawSealChar(g, x, cy - sz / 2, sz, '天', T.C.acc, true);
    T.drawSealChar(g, x + bw - sz, cy - sz / 2, sz, '眼', ready ? T.C.acc : T.C.dim, ready);
    T.segBar(g, barX, barY, barW, u * 1.1, L.bar, 32, true);
    const horiz = bw > u * 36, sy0 = cy + u * 4.2;
    L.steps.forEach((s, i) => {
      const done = i < L.step, cur = i === L.step, bad = done && s.ok === false;
      const col = bad ? T.C.alert : done ? T.C.acc : cur ? T.C.hot : T.C.dim;
      const note = done ? (bad ? 'skipped' : 'ok') : cur ? ((now / 300 | 0) % 2 ? '…' : '') : '';
      if (horiz) {                                         // in a row under the bar: diamond, code, what is being brought up
        const cw = barW / L.steps.length, sx = barX + cw * i + cw / 2;
        T.diamond(g, sx, sy0, u * 0.36, col, done || cur);
        T.txt(g, s[0], sx, sy0 + u * 1.6, u * 0.85, done || cur ? T.C.hot : T.C.dim, 'm', 'center');
        T.txt(g, s[1], sx, sy0 + u * 2.7, u * 0.72, done ? T.C.label : cur ? T.C.hot : T.C.dim, 'c', 'center');
        T.txt(g, note, sx, sy0 + u * 3.7, u * 0.7, bad ? T.C.alert : T.C.dim, 'm', 'center');
        if (i < L.steps.length - 1) { g.fillStyle = done ? `rgba(${T.RGB.acc},.4)` : T.C.line; g.fillRect(sx + u * 0.7, sy0 - 0.5, cw - u * 1.4, 1); }
      } else {                                             // narrow (phone): as a list
        const y = sy0 + i * u * 1.5;
        T.diamond(g, x + u * 0.4, y - u * 0.3, u * 0.32, col, done || cur);
        T.txt(g, s[0], x + u * 1.4, y, u * 0.9, done || cur ? T.C.hot : T.C.dim);
        T.txt(g, s[1] + (note ? ' · ' + note : ''), x + u * 6.5, y, u * 0.9, bad ? T.C.alert : done ? T.C.label : cur ? T.C.hot : T.C.dim, 'c');
      }
    });
    if (L.step >= 5 && L.bar > 0.985) {
      // boot done: switch to the stick's theme at once (boot was black and white); the splash is themed
      T.themeHold = false;
      if (!new URLSearchParams(location.search).get('theme')) { if (T.S.st && T.S.st.theme) T.setTheme(T.S.st.theme); else T.themeFallback(); }
      if (!new URLSearchParams(location.search).get('lang') && T.S.st && T.S.st.lang) T.setLang(T.S.st.lang);
      if (T.reduce) { afterAnim(now); return false; } phase = 'anim'; t0 = now; T.app.hold = true; return false; }   // all up: pause the scene, show the splash
    return true;
  };
  let lastDraw = 0;
  const frame = now => {
    if (!box.isConnected || phase === 'done') return;
    requestAnimationFrame(frame);
    if (now - lastDraw < 15) return;                   // at most ~60 fps
    lastDraw = now;
    const W = innerWidth, H = innerHeight, d = Math.min(devicePixelRatio || 1, T.RES / Math.max(W, H));
    if (cv.width !== Math.round(W * d)) { cv.width = Math.round(W * d); cv.height = Math.round(H * d); }
    g.setTransform(d, 0, 0, d, 0, 0);
    if (phase === 'launch') { launchFrame(now, W, H, T.unit(W, H)); return; }
    if (phase === 'choice') { choiceFrame(now, W, H, T.unit(W, H)); return; }
    if (now - t0 > MS) { afterAnim(now); return; }
    const t = now - t0;
    const cx = W / 2, cy = H / 2, R = Math.min(W, H) * 0.36, u = T.unit(W, H);
    g.fillStyle = T.C.bg; g.fillRect(0, 0, W, H);
    // crosshair
    const gr = eo(seg(t, 0, 450));
    g.fillStyle = T.C.dim; g.fillRect(cx - gr * W / 2, cy, gr * W, 1); g.fillRect(cx, cy - gr * H / 2, 1, gr * H);
    // rings
    for (let i = 0; i < 4; i++) { const e = eo(seg(t, 80 + i * 110, 420 + i * 110)); if (e > 0) { g.strokeStyle = i === 3 ? T.C.acc : T.C.dim; g.lineWidth = i === 3 ? 1.5 : 1; g.beginPath(); g.arc(cx, cy, R * (i + 1) / 4 * e, 0, T.TAU); g.stroke(); } }
    const tk = seg(t, 500, 800);
    g.lineWidth = 1;
    for (let a = 0; a < 360 * tk; a += 10) { const r = a * Math.PI / 180, l = a % 30 ? 0.025 : 0.05; g.strokeStyle = a % 30 ? T.C.dim : T.C.acc; g.beginPath(); g.moveTo(cx + Math.cos(r) * R, cy + Math.sin(r) * R); g.lineTo(cx + Math.cos(r) * R * (1 + l), cy + Math.sin(r) * R * (1 + l)); g.stroke(); }
    // beam with a tail, and targets
    if (t > 300 && t < 2400) {
      const sw = (t - 300) * 0.48 * Math.PI / 180, fade = 1 - seg(t, 2050, 2400);
      if (g.createConicGradient) { const c = g.createConicGradient(sw - 1.3, cx, cy), f = 1.3 / T.TAU; c.addColorStop(0, `rgba(${T.RGB.mid},0)`); c.addColorStop(f * 0.985, `rgba(${T.RGB.acc},${0.4 * fade})`); c.addColorStop(f, `rgba(${T.RGB.acc},0)`); c.addColorStop(1, `rgba(${T.RGB.acc},0)`); g.fillStyle = c; g.beginPath(); g.moveTo(cx, cy); g.arc(cx, cy, R, sw - 1.3, sw); g.closePath(); g.fill(); }
      g.strokeStyle = `rgba(${T.RGB.hot},${fade})`; g.lineWidth = 2; g.beginPath(); g.moveTo(cx, cy); g.lineTo(cx + Math.cos(sw) * R, cy + Math.sin(sw) * R); g.stroke();
      const swd = (t - 300) * 0.48;
      for (const [deg, rr] of BL) {
        if (swd < deg) continue;
        const since = ((swd - deg) % 360 + 360) % 360, v = T.clamp(1 - since / 300) * fade; if (v < 0.02) continue;
        const a = deg * Math.PI / 180, x = cx + Math.cos(a) * R * rr / 100, y = cy + Math.sin(a) * R * rr / 100;
        T.diamond(g, x, y, u * 0.5, T.heat(0.3 + 0.7 * v), true);
        if (since < 40) { g.strokeStyle = T.heat(v, v); g.beginPath(); g.arc(x, y, u * (0.7 + since / 12), 0, T.TAU); g.stroke(); }
      }
    }
    // corner brackets
    const c = eo(seg(t, 0, 500)), L = u * 2;
    for (const sx of [-1, 1]) for (const sy of [-1, 1]) {
      const x = cx + sx * (W / 2 - u) * c, y = cy + sy * (H / 2 - u) * c;
      g.fillStyle = T.C.acc; g.fillRect(sx < 0 ? x : x - L, y - (sy < 0 ? 0 : 1), L, 1.5); g.fillRect(x - (sx < 0 ? 0 : 1), sy < 0 ? y : y - L, 1.5, L);
    }
    // plate, title, seal
    const bar = eo(seg(t, 1000, 1280)), bw = W * 0.6 * bar, bh = u * 9;
    if (bar > 0) { g.fillStyle = T.C.bg; g.fillRect(cx - bw / 2, cy - bh / 2, bw, bh); g.fillStyle = T.C.acc; g.fillRect(cx - bw / 2, cy - bh / 2, bw, 1.5); g.fillRect(cx - bw / 2, cy + bh / 2, bw, 1.5); }
    const px = u * 4.6, tw = T.tw(g, TITLE, px, 't'), sealH = px * 1.15, sealCol = t < 1700 ? T.C.hot : T.C.acc, seal = true, sw2 = sealH * 0.56;
    const bx = cx - (tw + u * 2 + sw2) / 2;
    if (t >= 1120) {
      let x = bx;
      for (let i = 0; i < TITLE.length; i++) {
        const ch = TITLE[i], w = T.tw(g, ch, px, 't'), lock = 1150 + i * 55;
        if (t >= 1120 + i * 25) T.txt(g, t < lock ? POOL[hash(i * 977 + (t / 45 | 0)) % POOL.length] : ch, x, cy + px * 0.3, px, t < lock + 90 ? T.C.acc : T.C.hot, 't');
        x += w;
      }
    }
    if (t >= 1640 && seal) {
      const k = seg(t, 1640, 1820), sx = bx + tw + u * 2, sy = cy - sealH / 2 - px * 0.06;
      T.drawSeal(g, sx, sy, sealH, sealCol);
      if (k < 1) { const gg = u * (0.3 + k * 2); g.strokeStyle = `rgba(${T.RGB.hot},${0.6 * (1 - k)})`; g.strokeRect(sx - gg, sy - gg, sw2 + 2 * gg, sealH + 2 * gg); }
    }
    if (t >= 1700) { const k = Math.min(SUB.length, (t - 1700) / 14 | 0); T.txt(g, SUB.slice(0, k) + ((t / 120 | 0) & 1 && k < SUB.length ? '_' : ''), cx, cy + bh / 2 - u * 1.2, u * 1.05, T.C.label, 'm', 'center'); }
    // the sweep wipes it
    const wp = seg(t, 2350, MS);
    if (wp > 0) { const y = eo(wp) * (H + 6); g.fillStyle = T.C.bg; g.fillRect(0, 0, W, Math.min(y, H)); g.fillStyle = T.C.hot; g.fillRect(0, y, W, 2); g.fillStyle = T.C.acc; g.fillRect(0, y - 2, W, 1); g.fillRect(0, y + 3, W, 1); }
  };
  (document.fonts ? document.fonts.ready : Promise.resolve()).then(() => requestAnimationFrame(frame));
};
