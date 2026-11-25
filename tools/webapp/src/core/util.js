// ---- TENGEN HUD for the tablet: firmware tokens (core/theme.cpp) and brushes ----
// Same HUD as on the stick, plus depth, glow and particles.
const T = window.T = { views: [] };
// colors exactly as on the stick
T.C = { bg: '#05080d', acc: '#3ee6ff', hot: '#ebfaff', alert: '#ff7a1a', label: '#96afc0', dim: '#5a7080', line: '#203240', panel: '#0e2a34',
  steel: '#96afc0' };
T.FT = "'Exo 2','Arial Narrow',sans-serif";                              // headings, big numbers
T.FM = "'JetBrains Mono',ui-monospace,monospace";                 // HUD text
T.FC = "'JetBrains Mono',ui-monospace,monospace";                 // where Cyrillic may appear
T.TAU = Math.PI * 2;
T.clamp = (v, a = 0, b = 1) => v < a ? a : v > b ? b : v;
T.lerp = (a, b, k) => a + (b - a) * k;
T.ease = (dt, tau) => 1 - Math.exp(-dt / tau);                    // share of the way per frame for time constant tau
// scales as on the stick: HEAT (signal, heat) and COOL (background, terrain)
T.HEAT = [[0, [14, 30, 46]], [0.35, [40, 95, 130]], [0.75, [62, 230, 255]], [1, [235, 250, 255]]];
T.COOL = [[0, [5, 8, 13]], [0.25, [24, 40, 54]], [0.5, [95, 125, 145]], [0.72, [150, 185, 205]], [0.86, [62, 230, 255]], [1, [235, 250, 255]]];
T.ramp = (R, v) => {
  v = T.clamp(v);
  for (let i = 1; i < R.length; i++) if (v <= R[i][0]) {
    const a = R[i - 1], b = R[i], t = (v - a[0]) / (b[0] - a[0]);
    return [a[1][0] + (b[1][0] - a[1][0]) * t, a[1][1] + (b[1][1] - a[1][1]) * t, a[1][2] + (b[1][2] - a[1][2]) * t];
  }
  return R[R.length - 1][1];
};
T.rgb = (c, a = 1) => `rgba(${c[0] | 0},${c[1] | 0},${c[2] | 0},${a})`;
T.heatRGB = v => T.ramp(T.HEAT, v);
T.heat = (v, a = 1) => T.rgb(T.ramp(T.HEAT, v), a);
T.cool = (v, a = 1) => T.rgb(T.ramp(T.COOL, v), a);
T.ALERT = [255, 122, 26];
T.rgba = (hex, a) => { const n = parseInt(hex.slice(1), 16); return `rgba(${n >> 16},${(n >> 8) & 255},${n & 255},${a})`; };
T.hash = s => { let h = 2166136261; s = String(s); for (let i = 0; i < s.length; i++) h = Math.imul(h ^ s.charCodeAt(i), 16777619); return h >>> 0; };
T.ago = s => s < 60 ? (s | 0) + 's' : s < 3600 ? Math.floor(s / 60) + 'm' : Math.floor(s / 3600) + 'h ' + Math.floor(s / 60) % 60 + 'm';
T.hexs = (v, n = 2) => '0x' + (v >>> 0).toString(16).toUpperCase().padStart(n, '0');
T.pct = r => T.clamp((r + 95) / 50, 0, 1);            // signal strength 0..1 (-95..-45 dBm), as on the stick
T.hex8 = (h, signed) => {
  const n = (h || '').length >> 1, a = signed ? new Int8Array(n) : new Uint8Array(n);
  for (let i = 0; i < n; i++) { const v = parseInt(h.substr(i * 2, 2), 16); a[i] = signed && v > 127 ? v - 256 : v; }
  return a;
};
T.el = (tag, cls, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html != null) e.innerHTML = html; return e; };
// inside the tablet app (Capacitor): the page is local, the stick is reached by its address on the TENGEN network
T.VERSION = '@@VERSION@@';                               // TENGEN version (build.py fills it from the VERSION file)
T.native = !!(window.Capacitor && window.Capacitor.isNativePlatform && window.Capacitor.isNativePlatform());
T.STICK = '192.168.4.1';
const safeHost = () => { const h = new URLSearchParams(location.search).get('host'); return h && /^[A-Za-z0-9._-]+(:\d{1,5})?$/.test(h) ? h : ''; };
T.stickHost = () => (safeHost() || (T.native ? T.STICK : location.hostname || T.STICK)).replace(/:\d+$/, '');   // host without port: SSE uses its own port
T.apiBase = () => { const h = safeHost(); return h ? `http://${h}` : T.native ? `http://${T.STICK}` : ''; };
T.reduce = matchMedia('(prefers-reduced-motion: reduce)').matches;
// ---- quality: fixed (stable mode). Canvas ~1920 px on the long side, glow, dust and scanlines on;
// app.js turns glow off only if the device really cannot keep up.
T.RES = 1920;
T.Q = { bloom: 1, dust: 1, scan: 1 };

// ---- calm value: jitter below the threshold is hidden, big changes flow ----
// const s = T.steady(4, 0.7); s.set(v); every frame s.tick(dt); s.v is what to draw
T.steady = (db = 4, tau = 0.7) => ({
  v: null, target: null,
  set(x) { if (x == null || isNaN(x)) return; if (this.target == null) { this.v = this.target = x; return; } if (Math.abs(x - this.target) >= db) this.target = x; },
  tick(dt) { if (this.target != null) this.v += (this.target - this.v) * T.ease(dt, tau); return this.v; },
});

// ---- text ----
T.txt = (g, s, x, y, px, col, f = 'm', al = 'left', bl = 'alphabetic') => {
  g.font = f === 't' ? `600 ${px}px ${T.FT}` : f === 'c' ? `500 ${px}px ${T.FC}` : f === 'k' ? `${px}px ${T.SEAL_FONT}` : `${px}px ${T.FM}`;
  g.fillStyle = col; g.textAlign = al; g.textBaseline = bl; g.fillText(s, x, y);
};
T.tw = (g, s, px, f = 'm') => { g.font = f === 't' ? `600 ${px}px ${T.FT}` : f === 'c' ? `500 ${px}px ${T.FC}` : f === 'k' ? `${px}px ${T.SEAL_FONT}` : `${px}px ${T.FM}`; return g.measureText(s).width; };
// truncate text with "…": results are cached (avoids dozens of text measurements per frame)
T._fitC = new Map();
T.fit = (g, s, max) => {
  s = T.tr ? T.tr(String(s)) : String(s); const key = g.font + '|' + Math.round(max) + '|' + s;
  const hit = T._fitC.get(key); if (hit !== undefined) return hit;
  let r = s;
  if (g.measureText(s).width > max) {
    let lo = 0, hi = s.length;                          // binary search for the length instead of char by char
    while (lo < hi) { const m = (lo + hi + 1) >> 1; if (g.measureText(s.slice(0, m) + '…').width <= max) lo = m; else hi = m - 1; }
    r = s.slice(0, Math.max(1, lo)) + '…';
  }
  if (T._fitC.size > 3000) T._fitC.clear();
  T._fitC.set(key, r); return r;
};

// ---- layout unit: 16 px on a 1280x800 tablet, proportional elsewhere ----
T.unit = (W, H) => Math.min(W / 80, H / 50);

// ---- HUD brushes (like ui.cpp on the stick) ----
// header "// TITLE" + status on the right + fading line
T.header = (g, x, y, w, u, title, status, alert) => {
  T.txt(g, '// ' + String(title).toUpperCase(), x, y + u * 1.55, u * 1.8, T.C.acc, 't');
  if (status) {
    g.font = `${u * 0.92}px ${T.FM}`;
    const room = w - T.tw(g, '// ' + String(title).toUpperCase(), u * 1.8, 't') - u * 2;
    g.font = `${u * 0.92}px ${T.FM}`;
    T.txt(g, T.fit(g, String(status).toUpperCase(), Math.max(u * 4, room)), x + w, y + u * 1.45, u * 0.92, alert ? T.C.alert : T.C.label, 'm', 'right');
  }
  const gr = g.createLinearGradient(x, 0, x + w, 0);
  gr.addColorStop(0, `rgba(${T.RGB.acc},1)`); gr.addColorStop(1, `rgba(${T.RGB.acc},.15)`);
  g.fillStyle = gr; g.fillRect(x, y + u * 2.1, w, Math.max(1, u * 0.08));
};
T.dots = (g, x, y, u, n, i) => { for (let k = 0; k < n; k++) { const on = k === i; g.fillStyle = on ? T.C.acc : T.C.line; g.fillRect(x + k * u * 0.85, y, on ? u * 0.55 : u * 0.3, u * 0.3); } };
T.footer = (g, x, y, w, u, l, r) => {
  g.fillStyle = T.C.line; g.fillRect(x, y, w, 1);
  if (l) T.txt(g, String(l).toUpperCase(), x, y + u * 1.25, u * 0.8, T.C.dim);
  if (r) T.txt(g, String(r).toUpperCase(), x + w, y + u * 1.25, u * 0.8, T.C.dim, 'm', 'right');
};
T.brackets = (g, x, y, w, h, L, col = T.C.acc, lw = 1.5) => {
  g.strokeStyle = col; g.lineWidth = lw; g.beginPath();
  g.moveTo(x, y + L); g.lineTo(x, y); g.lineTo(x + L, y);
  g.moveTo(x + w - L, y); g.lineTo(x + w, y); g.lineTo(x + w, y + L);
  g.moveTo(x + w, y + h - L); g.lineTo(x + w, y + h); g.lineTo(x + w - L, y + h);
  g.moveTo(x + L, y + h); g.lineTo(x, y + h); g.lineTo(x, y + h - L);
  g.stroke();
};
T.cutPath = (g, x, y, w, h, c) => { g.beginPath(); g.moveTo(x + c, y); g.lineTo(x + w, y); g.lineTo(x + w, y + h - c); g.lineTo(x + w - c, y + h); g.lineTo(x, y + h); g.lineTo(x, y + c); g.closePath(); };
// panel with cut corners; "glass" over the 3D scene by default
T.panel = (g, x, y, w, h, u, fill = `rgba(${T.RGB.panel},.28)`, stroke = `rgba(${T.RGB.acc},.22)`) => {
  T.cutPath(g, x, y, w, h, u * 0.8); g.fillStyle = fill; g.fill(); g.strokeStyle = stroke; g.lineWidth = 1; g.stroke();
};
// segmented bar with reflection
T.segBar = (g, x, y, w, h, v, n, refl = true, alert = false) => {
  const gap = Math.max(1, w / n * 0.24), sw = (w - gap * (n - 1)) / n, lit = Math.round(T.clamp(v) * n);
  for (let i = 0; i < n; i++) {
    g.fillStyle = i < lit ? (alert ? T.C.alert : T.heat(0.3 + 0.7 * i / Math.max(1, n - 1))) : T.C.line;
    g.fillRect(x + i * (sw + gap), y, sw, h);
    if (refl && i < lit) { g.globalAlpha = 0.15; g.fillRect(x + i * (sw + gap), y + h * 1.3, sw, h * 0.45); g.globalAlpha = 1; }
  }
};
// letter in a frame (device type)
T.glyph = (g, x, y, s, ch, col) => { g.strokeStyle = col; g.lineWidth = 1; g.strokeRect(x + 0.5, y + 0.5, s - 1, s - 1); T.txt(g, ch, x + s / 2, y + s * 0.56, s * 0.66, col, 't', 'center', 'middle'); };
T.diamond = (g, x, y, r, col, fill) => {
  g.beginPath(); g.moveTo(x, y - r); g.lineTo(x + r, y); g.lineTo(x, y + r); g.lineTo(x - r, y); g.closePath();
  if (fill) { g.fillStyle = col; g.fill(); } else { g.strokeStyle = col; g.lineWidth = 1.5; g.stroke(); }
};
// line chart with fill
T.spark = (g, x, y, w, h, arr, lo, hi, col = T.C.acc) => {
  if (!arr || arr.length < 2) return;
  g.beginPath();
  arr.forEach((v, i) => { const px = x + i / (arr.length - 1) * w, py = y + h - T.clamp((v - lo) / (hi - lo)) * h; i ? g.lineTo(px, py) : g.moveTo(px, py); });
  g.strokeStyle = col; g.lineWidth = 1.5; g.stroke();
  g.lineTo(x + w, y + h); g.lineTo(x, y + h); g.closePath();
  const gr = g.createLinearGradient(0, y, 0, y + h); gr.addColorStop(0, `rgba(${T.RGB.acc},.22)`); gr.addColorStop(1, `rgba(${T.RGB.acc},0)`);
  g.fillStyle = gr; g.fill();
};
// big number + unit label
T.bigNum = (g, v, x, y, px, col, unit, al = 'left') => {
  const s = String(v), w = T.tw(g, s, px, 't');
  const x0 = al === 'right' ? x - w - (unit ? T.tw(g, unit, px * 0.22) + px * 0.12 : 0) : x;
  T.txt(g, s, x0, y, px, col, 't');
  if (unit) T.txt(g, unit, x0 + w + px * 0.1, y - px * 0.04, px * 0.22, T.C.dim);
};
// 天眼 seal drawn as vectors: color fill with the glyphs cut out in the background color (font Tengen Seal,
// two IPAGothic glyphs in fonts.css). Sharp at any size. Returns the width.
T.SEAL_FONT = "'Tengen Seal','Noto Sans CJK JP','Noto Sans JP',sans-serif";
T.drawSeal = (g, x, y, h, col = T.C.seal || T.C.acc, bg = T.C.bg) => {
  const w = h * 0.56, c = h * 0.1;
  T.cutPath(g, x, y, w, h, c); g.fillStyle = col; g.fill();
  g.strokeStyle = bg; g.lineWidth = Math.max(1, h * 0.03);
  T.cutPath(g, x + h * 0.06, y + h * 0.06, w - h * 0.12, h - h * 0.12, c * 0.6); g.stroke();
  g.fillStyle = bg; g.textAlign = 'center'; g.textBaseline = 'middle'; g.font = `${h * 0.38}px ${T.SEAL_FONT}`;
  g.lineWidth = h * 0.018; g.lineJoin = 'round';                      // slightly thicker stroke: a seal, not thin type
  for (const [ch, k] of [['天', 0.29], ['眼', 0.71]]) { g.fillText(ch, x + w / 2, y + h * k); g.strokeText(ch, x + w / 2, y + h * k); }
  return w;
};
// single-glyph (square) seal: filled = solid with the glyph cut out, else outline
T.drawSealChar = (g, x, y, sz, ch, col = T.C.seal || T.C.acc, filled = true, bg = T.C.bg) => {
  const c = sz * 0.14;
  T.cutPath(g, x, y, sz, sz, c);
  if (filled) { g.fillStyle = col; g.fill(); g.strokeStyle = bg; g.lineWidth = Math.max(1, sz * 0.035); T.cutPath(g, x + sz * 0.08, y + sz * 0.08, sz * 0.84, sz * 0.84, c * 0.6); g.stroke(); }
  else { g.strokeStyle = col; g.lineWidth = Math.max(1, sz * 0.04); g.stroke(); }
  const ink = filled ? bg : col;
  g.fillStyle = ink; g.strokeStyle = ink; g.lineWidth = sz * 0.02; g.lineJoin = 'round';
  g.textAlign = 'center'; g.textBaseline = 'middle'; g.font = `${sz * 0.62}px ${T.SEAL_FONT}`;
  g.fillText(ch, x + sz / 2, y + sz * 0.53); g.strokeText(ch, x + sz / 2, y + sz * 0.53);
};

// ---- 3D: orbit camera ----
// const cam = T.orbit(); cam.frame(cx, cy, F); const p = cam.p(x, y, z) -> [sx, sy, scale, depth]
T.orbit = (yaw = 0.6, pitch = 0.95, D = 3.2) => ({
  yaw, pitch, D, cx: 0, cy: 0, F: 1, cs: 1, sn: 0, cp: 1, sp: 0,
  frame(cx, cy, F) { this.cx = cx; this.cy = cy; this.F = F; this.cs = Math.cos(this.yaw); this.sn = Math.sin(this.yaw); this.cp = Math.cos(this.pitch); this.sp = Math.sin(this.pitch); },
  p(x, y, z) {
    const x1 = x * this.cs - z * this.sn, z1 = x * this.sn + z * this.cs;
    const y2 = y * this.cp + z1 * this.sp, z2 = -y * this.sp + z1 * this.cp, s = this.F / Math.max(0.05, this.D + z2);   // a point behind the camera must not give a negative/infinite scale
    return [this.cx + x1 * s, this.cy - y2 * s, s, z2];
  },
});
// finger: drag = orbit, release = inertia. Returns {down, tap(cb)}
T.drag = (cv, cam, opt = {}) => {
  const st = { down: null, vy: 0, last: 0, onTap: null, moved: false };
  const minP = opt.minPitch ?? 0.45, maxP = opt.maxPitch ?? 1.4;
  cv.addEventListener('pointerdown', e => { st.down = { x: e.offsetX, y: e.offsetY, yaw: cam.yaw, pitch: cam.pitch, t: performance.now() }; st.moved = false; cv.setPointerCapture(e.pointerId); });
  cv.addEventListener('pointermove', e => {
    if (!st.down) return;
    const dx = e.offsetX - st.down.x, dy = e.offsetY - st.down.y;
    if (Math.abs(dx) + Math.abs(dy) > 7) st.moved = true;
    if (!st.moved) return;
    const ny = st.down.yaw - dx * 0.006;
    st.vy = (ny - cam.yaw) * 60; cam.yaw = ny;
    cam.pitch = T.clamp(st.down.pitch - dy * 0.004, minP, maxP);
    st.last = performance.now();
  });
  const up = e => { if (!st.down) return; const moved = st.moved; st.down = null; if (!moved && st.onTap && e.type === 'pointerup') st.onTap(e.offsetX, e.offsetY); };
  cv.addEventListener('pointerup', up); cv.addEventListener('pointercancel', up);
  st.tick = dt => { if (!st.down && Math.abs(st.vy) > 0.01) { cam.yaw += st.vy * dt; st.vy *= Math.exp(-dt * 3); } };
  st.idle = () => !st.down && performance.now() - st.last > 4000;   // untouched for a while: orbit on its own
  return st;
};

// ---- atmosphere: glow, dust, scanlines, vignette ----
// Post-effects: on mobile, fewer full-screen repaints per frame is the main rule (pre-render what does not change). Now 2 full passes per frame instead of 4.
//
// Bloom: the frame is shrunk to 1/4 and 1/8 (alternating frames), both copies merge into ONE small "light map",
// which is laid over the screen in a single pass.
T.bloom = (o, strength = 0.85) => {
  if (!T.Q.bloom || T.FLAT) return;            // KURENAI is flat: no glow at all
  const cv = o.cv, g = o.g;
  if (!o.bl) o.bl = [0, 1, 2].map(() => { const c = document.createElement('canvas'); return { c, g: c.getContext('2d', { alpha: false }) }; });
  o.blN = (o.blN || 0) + 1;
  const w1 = Math.max(1, cv.width >> 2), h1 = Math.max(1, cv.height >> 2), w2 = Math.max(1, w1 >> 1), h2 = Math.max(1, h1 >> 1);
  const [A, B, L] = o.bl;
  if (A.c.width !== w1 || A.c.height !== h1) { A.c.width = L.c.width = w1; A.c.height = L.c.height = h1; B.c.width = w2; B.c.height = h2; o.blN = 1; }
  if (o.blN & 1) {
    A.g.imageSmoothingEnabled = B.g.imageSmoothingEnabled = L.g.imageSmoothingEnabled = true;
    A.g.imageSmoothingQuality = 'low'; A.g.drawImage(cv, 0, 0, w1, h1);
    B.g.drawImage(A.c, 0, 0, w2, h2);
    // light map = 0.45*(1/4) + 0.6*(1/8), all small, so cheap
    L.g.globalCompositeOperation = 'copy'; L.g.globalAlpha = 0.45; L.g.drawImage(A.c, 0, 0);
    L.g.globalCompositeOperation = 'lighter'; L.g.globalAlpha = 0.6; L.g.drawImage(B.c, 0, 0, w1, h1);
    L.g.globalAlpha = 1;
  }
  g.save(); g.setTransform(1, 0, 0, 1, 0, 0); g.globalCompositeOperation = 'lighter'; g.imageSmoothingEnabled = true;
  g.globalAlpha = strength; g.drawImage(L.c, 0, 0, cv.width, cv.height);
  g.restore();
};
// background: gradient drawn once into a small image, then stretched (not rebuilt every frame)
T.backdrop = (g, W, H, fx = 0.45, fy = 0.55) => {
  if (T.FLAT) { g.fillStyle = T.C.bg; g.fillRect(0, 0, W, H); return; }   // KURENAI: flat background
  const key = W + 'x' + H + ':' + fx + ':' + fy + ':' + T.C.bg;
  let c = T._bdCache && T._bdCache.get(key);
  if (!c) {
    if (!T._bdCache || T._bdCache.size > 24) T._bdCache = new Map();
    c = document.createElement('canvas'); const s = 1 / 4; c.width = Math.max(1, Math.round(W * s)); c.height = Math.max(1, Math.round(H * s));
    const x = c.getContext('2d', { alpha: false }); x.scale(s, s);
    const r = x.createRadialGradient(W * fx, H * fy, 0, W * fx, H * fy, Math.max(W, H) * 0.75);
    r.addColorStop(0, '#0a1722'); r.addColorStop(1, T.C.bg); x.fillStyle = r; x.fillRect(0, 0, W, H);
    T._bdCache.set(key, c);
  }
  g.drawImage(c, 0, 0, W, H);
};
// scanlines + vignette: one ready image per screen size, one pass instead of two, no gradient/pattern rebuild per frame
T.finishLayer = (W, H, pw, ph, paint) => {
  const key = pw + 'x' + ph + ':' + paint.name;
  if (T._finKey === key) return T._finC;
  const c = T._finC || document.createElement('canvas'); c.width = pw; c.height = ph;
  const x = c.getContext('2d'); x.setTransform(pw / W, 0, 0, ph / H, 0, 0); x.clearRect(0, 0, W, H);
  paint(x, W, H, pw / W);
  T._finC = c; T._finKey = key; return c;
};
const paintScan = (x, W, H, d) => {
  x.setTransform(1, 0, 0, 1, 0, 0); x.fillStyle = 'rgba(0,0,0,.16)';
  for (let y = 0; y < H * d; y += 4) x.fillRect(0, y, W * d, 1);
  x.setTransform(d, 0, 0, d, 0, 0);
  const v = x.createRadialGradient(W / 2, H / 2, Math.min(W, H) * 0.45, W / 2, H / 2, Math.max(W, H) * 0.75);
  v.addColorStop(0, `rgba(${T.RGB.bg},0)`); v.addColorStop(1, `rgba(${T.RGB.bg},.7)`); x.fillStyle = v; x.fillRect(0, 0, W, H);
};
T.finish = (g, W, H, paint = paintScan) => {
  if (!T.Q.scan || T.FLAT) return;             // KURENAI: no scanlines or vignette
  const cv = g.canvas, c = T.finishLayer(W, H, cv.width, cv.height, paint);
  g.save(); g.setTransform(1, 0, 0, 1, 0, 0); g.globalCompositeOperation = 'source-over'; g.globalAlpha = 1; g.drawImage(c, 0, 0); g.restore();
};
// 3D dust: T.dust(n) -> {tick(dt), draw(g, cam)}
T.dust = (n = 220, ext = 1.7, top = 1.1) => {
  if (T.reduce) n = 0;
  const P = new Float32Array(n * 3);
  for (let i = 0; i < n; i++) { P[i * 3] = (Math.random() - 0.5) * ext * 2; P[i * 3 + 1] = Math.random() * top; P[i * 3 + 2] = (Math.random() - 0.5) * ext * 2; }
  return {
    tick(dt) { for (let i = 0; i < n; i++) { P[i * 3 + 1] += dt * 0.035; if (P[i * 3 + 1] > top) P[i * 3 + 1] = 0; } },
    draw(g, cam) {
      if (!T.Q.dust) return;
      const b = [[], [], []];
      for (let i = 0; i < n; i++) { const p = cam.p(P[i * 3], P[i * 3 + 1], P[i * 3 + 2]); if (cam.D + p[3] < 0.3) continue; b[p[3] < -0.4 ? 2 : p[3] < 0.6 ? 1 : 0].push(p); }
      [0.18, 0.32, 0.55].forEach((a, k) => { g.fillStyle = `rgba(${T.RGB.acc},${a})`; g.beginPath(); for (const p of b[k]) { const s = Math.max(0.8, p[2] * 0.0045); g.rect(p[0] - s / 2, p[1] - s / 2, s, s); } g.fill(); });
    },
  };
};
// floor grid with fog (3D scenes): cells of size step, out to ext
T.floorGrid = (g, cam, ext = 1.7, step = 0.2, y = 0) => {
  g.lineWidth = 1;
  const n = Math.round(ext / step);
  for (let k = -n; k <= n; k++) {
    const c = k * step, a = 0.48 * (1 - Math.abs(c) / (ext + 0.05));
    g.strokeStyle = `rgba(${T.RGB.line},${a})`;
    let p = cam.p(c, y, -ext), q = cam.p(c, y, ext); g.beginPath(); g.moveTo(p[0], p[1]); g.lineTo(q[0], q[1]); g.stroke();
    p = cam.p(-ext, y, c); q = cam.p(ext, y, c); g.beginPath(); g.moveTo(p[0], p[1]); g.lineTo(q[0], q[1]); g.stroke();
  }
};
// touch hits: views put their zones here every frame
T.hits = {
  list: [],
  clear() { this.list.length = 0; },
  circle(x, y, r, data) { this.list.push({ x, y, r, data }); },
  rect(x, y, w, h, data) { this.list.push({ x, y, w, h, data }); },
  at(x, y) { for (let i = this.list.length - 1; i >= 0; i--) { const h = this.list[i]; if (h.r != null) { if ((x - h.x) ** 2 + (y - h.y) ** 2 <= h.r * h.r) return h.data; } else if (x >= h.x && x <= h.x + h.w && y >= h.y && y <= h.y + h.h) return h.data; } return null; },
};
// empty / no link: the same in every section
T.emptyState = (g, W, H, u, title, line, alert) => {
  T.txt(g, title, W / 2, H / 2 - u * 0.4, u * 1.9, alert ? T.C.alert : T.C.hot, 't', 'center');
  if (line) T.txt(g, String(line).toUpperCase(), W / 2, H / 2 + u * 1.3, u * 0.85, T.C.label, 'm', 'center');
};
/*@DATA*/
