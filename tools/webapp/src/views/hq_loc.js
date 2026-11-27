// ---- HQ > LOCATOR 3D: Evangelion Stereo Triangulation Field × Zen Karesansui Wave Sanctuary ----
// WebGL live dual-source wave interference field + 3D crystalline receiver monoliths (天 & 眼),
// hexagonal AT-Field triangulation locks, smooth damping, filter modes, and 1-tap calibration.
(() => {
const TRAIL_MAX = 16;
const TX_REF = { computer: -53.5, TV: -53.5, phone: -58.0, audio: -60.5, watch: -63.0, tracker: -62.5 };
const rssiToM = (r, type) => Math.max(0.20, Math.min(14, Math.pow(10, ((TX_REF[type] ?? -58.5) - r) / 22.5)));
const FILTERS = ['ALL', '2X ONLY', 'NEAR'];

// WebGL Dual-Receiver Interference Backdrop Shader (constructive wave patterns between TENGEN #1 & EAR #2)
const LOC_FS = `
uniform vec2 res, s1, s2;
uniform float t, earOk, kurenai;
void main() {
  vec2 p = gl_FragCoord.xy / res;
  vec2 aspect = vec2(res.x / min(res.x, res.y), res.y / min(res.x, res.y));
  vec2 uv = (p - 0.5) * aspect;
  vec2 p1 = (s1 - 0.5) * aspect, p2 = (s2 - 0.5) * aspect;
  float d1 = length(uv - p1), d2 = length(uv - p2);
  vec3 bg = kurenai > 0.5 ? vec3(0.043, 0.043, 0.043) : vec3(0.012, 0.020, 0.032);
  vec3 c1 = kurenai > 0.5 ? vec3(0.90, 0.15, 0.18) : vec3(0.24, 0.90, 1.0);
  vec3 c2 = kurenai > 0.5 ? vec3(0.96, 0.94, 0.92) : vec3(1.0, 0.55, 0.12);
  vec3 col = bg;
  float w1 = sin(d1 * 34.0 - t * 2.6);
  float w2 = earOk > 0.5 ? sin(d2 * 34.0 - t * 2.6) : 0.0;
  float interf = earOk > 0.5 ? pow(max(0.0, (w1 + w2) * 0.5), 5.0) : pow(max(0.0, w1), 6.0);
  col += c1 * exp(-d1 * 3.6) * 0.22;
  if (earOk > 0.5) col += c2 * exp(-d2 * 3.6) * 0.18;
  col += mix(c1, c2, earOk * 0.45) * interf * exp(-min(d1, d2) * 2.1) * 0.13;
  col *= smoothstep(1.25, 0.2, length(uv));
  gl_FragColor = vec4(col, 1.0);
}`;
let locProg = null;

T.hqLoc = {
  init(hq) {
    this.hq = hq;
    this.cam = T.orbit(0.0, 0.86, 3.20);
    this.drag = T.drag(hq.cv.cv, this.cam, { minPitch: 0.28, maxPitch: 1.42 });
    this.dust = T.dust(150, 1.9, 1.2);
    this.sel = null;
    this.filterIdx = 0;
    this.pos = new Map();
    this.trails = new Map();
    this.trailTick = 0;
    this.resetAnim = 0;
    this.toastMsg = '';
    this.toastT = 0;
    this.toastBad = false;

    this.drag.onTap = (x, y) => {
      if (hq.sub !== 2) return;
      const h = T.hits.at(x, y);
      if (!h) { this.sel = null; return; }
      if (h.act === 'base') { this.changeBase(h.d); return; }
      if (h.act === 'cal')  { this.calibrate(); return; }
      if (h.act === 'flt')  { this.filterIdx = (this.filterIdx + 1) % FILTERS.length; return; }
      if (h.act === 'rst')  { this.resetAnim = 1.0; this.sel = null; return; }
      if (h.act === 'name' && this.sel) { this.promptName(this.sel); return; }
      if (h.node)           { this.sel = h.node; return; }
      this.sel = null;
    };
  },

  toast(msg, bad = false) {
    this.toastMsg = msg; this.toastBad = bad; this.toastT = 2.8;
  },

  promptName(node) {
    if (T.nameDevice) T.nameDevice(node, node.rawLabel || '');
  },

  changeBase(delta) {
    const cur = (S.loc && S.loc.base) ? +S.loc.base : 3.0;
    const next = Math.max(1.0, Math.min(12.0, Math.round((cur + delta) * 2) / 2));
    if (S.loc) S.loc.base = next;
    fetch(T.apiBase() + `/api/loc2x?base=${next}`, { cache: 'no-store' })
      .then(r => r.json()).then(d => { if (d && d.base) S.onLoc(d); })
      .catch(() => {});
  },

  calibrate() {
    this.toast(T.tr('CALIBRATING...'));
    const q = (this.sel && this.sel.both) ? `&id=${this.sel.id}` : '';
    fetch(T.apiBase() + '/api/loc2x?a=cal' + q, { cache: 'no-store' })
      .then(r => r.json())
      .then(d => {
        if (d && d.ok) {
          if (S.loc) S.loc.ofs = d.ofs;
          this.toast(T.tr('CALIBRATED') + `: ${d.ofs >= 0 ? '+' : ''}${d.ofs} dB (${d.dev || 'OK'})`);
        } else {
          this.toast(T.tr('NEED PHONE HEARD BY BOTH STICKS'), true);
        }
      })
      .catch(() => this.toast(T.tr('NO LINK'), true));
  },

  frame(dt, t, g, W, H) {
    const u = Math.min(W / 86, H / 52);
    const kur = T.theme === 'kurenai';
    const cAcc = kur ? '#e5262f' : '#3ee6ff';
    const cHot = kur ? '#f4f1ea' : '#ebfaff';
    const cAlt = kur ? '#ff3b30' : '#ff8c1a';
    const cGrn = kur ? '#f4f1ea' : '#69ff6e';
    const cDim = kur ? '#8a8a8a' : '#6c8aa0';
    const cLine = kur ? 'rgba(244,241,234,' : 'rgba(62,230,255,';
    const cAltA = kur ? 'rgba(255,59,48,' : 'rgba(255,140,26,';

    if (this.toastT > 0) this.toastT = Math.max(0, this.toastT - dt);
    this.drag.tick(dt);
    if (this.resetAnim > 0) {
      this.resetAnim = Math.max(0, this.resetAnim - dt * 2.2);
      const k = T.ease(dt, 0.18);
      this.cam.yaw += (0.0 - this.cam.yaw) * k;
      this.cam.pitch += (0.86 - this.cam.pitch) * k;
      this.cam.D += (3.20 - this.cam.D) * k;
    } else if (this.drag.idle()) {
      this.cam.yaw += Math.sin(t * 0.18) * dt * 0.05;
    }
    this.dust.tick(dt);
    T.hits.clear();

    const cx = W * 0.5, cy = H * 0.56;
    const F = Math.min(W, H) * 0.78;
    this.cam.frame(cx, cy, F);

    const L = S.loc || { on: 1, base: 3.0, ofs: 6, ear: { ok: 0, bat: 0, rssi: -70 }, matched: 0, d: [] };
    const baseM = Math.max(1.0, +L.base || 3.0);
    const earOk = !!(L.ear && L.ear.ok);
    const roomSpan = Math.max(baseM, 2.4);
    const sc = 1.55 / roomSpan;
    const zWall = -0.68;
    const x1 = -baseM * 0.5 * sc, x2 = baseM * 0.5 * sc;
    const pSt1 = this.cam.p(x1, 0.06, zWall);
    const pSt2 = this.cam.p(x2, 0.06, zWall);

    // 1. WebGL Dual-Receiver Wave Interference Backdrop
    if (T.GL && T.GL.ok) {
      if (!locProg) locProg = T.GL.prog(LOC_FS);
      if (locProg) {
        T.GL.begin(W, H, 0.6);
        T.GL.quad(locProg, {
          t, earOk: earOk ? 1 : 0, kurenai: kur ? 1 : 0,
          s1: [pSt1[0] / Math.max(1, W), 1 - pSt1[1] / Math.max(1, H)],
          s2: [pSt2[0] / Math.max(1, W), 1 - pSt2[1] / Math.max(1, H)],
        });
        T.GL.draw(g, 0, 0, W, H);
      } else T.backdrop(g, W, H, 0.5, 0.56);
    } else T.backdrop(g, W, H, 0.5, 0.56);

    // 2. 3D Room Floor Grid + Sacred Ensō Range Arcs around both receivers
    const drawPoly3D = (cx0, cz0, r, sides, rot, col, lw = 1, span = T.TAU) => {
      g.strokeStyle = col; g.lineWidth = lw; g.beginPath();
      for (let i = 0; i <= sides; i++) {
        const a = rot + (i / sides) * span, p = this.cam.p(cx0 + Math.cos(a) * r, 0, cz0 + Math.sin(a) * r);
        i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]);
      }
      g.stroke();
    };

    const gridW = Math.max(3, Math.ceil(roomSpan * 1.2));
    const gridD = Math.max(5, Math.ceil(roomSpan * 1.9));
    g.strokeStyle = cLine + '0.12)'; g.lineWidth = 1; g.beginPath();
    for (let xm = -gridW; xm <= gridW; xm++) {
      const p0 = this.cam.p(xm * sc, 0, zWall - 0.25), p1 = this.cam.p(xm * sc, 0, zWall + gridD * sc);
      g.moveTo(p0[0], p0[1]); g.lineTo(p1[0], p1[1]);
    }
    for (let zm = 0; zm <= gridD; zm++) {
      const p0 = this.cam.p(-gridW * sc, 0, zWall + zm * sc), p1 = this.cam.p(gridW * sc, 0, zWall + zm * sc);
      g.moveTo(p0[0], p0[1]); g.lineTo(p1[0], p1[1]);
    }
    g.stroke();

    for (let zm = 1; zm <= Math.min(6, gridD); zm++) {
      const pm = this.cam.p(-gridW * sc - 0.12, 0, zWall + zm * sc);
      T.txt(g, zm + 'm', pm[0], pm[1], Math.max(9, u * 0.62), cDim, 'm', 'right');
    }

    for (let k = 1; k <= 3; k++) {
      const rm = (baseM * 0.45 * k) * sc;
      drawPoly3D(x1, zWall, rm, 48, t * 0.08 * k, cLine + (k === 2 ? '0.26)' : '0.14)'), 1, Math.PI * 1.82);
      drawPoly3D(x2, zWall, rm, 48, -t * 0.08 * k, earOk ? cAltA + (k === 2 ? '0.26)' : '0.14)') : cLine + '0.07)', 1, Math.PI * 1.82);
    }

    // 3. Harmonic Dual-Helix Baseline Link between TENGEN #1 and M5 EAR #2
    g.strokeStyle = earOk ? cAcc : cLine + '0.3)';
    g.lineWidth = earOk ? 1.8 : 1.2;
    if (!earOk) g.setLineDash([6, 6]);
    g.beginPath(); g.moveTo(pSt1[0], pSt1[1]); g.lineTo(pSt2[0], pSt2[1]); g.stroke();
    g.setLineDash([]);

    if (earOk) {
      g.strokeStyle = cAltA + '0.45)'; g.lineWidth = 1.1; g.beginPath();
      for (let s = 0; s <= 32; s++) {
        const f = s / 32, px = x1 + (x2 - x1) * f, py = 0.06 + Math.sin(f * Math.PI * 3 - t * 4) * 0.035 * Math.sin(f * Math.PI);
        const pp = this.cam.p(px, py, zWall);
        s ? g.lineTo(pp[0], pp[1]) : g.moveTo(pp[0], pp[1]);
      }
      g.stroke();
      for (let k = 0; k < 3; k++) {
        const f = (t * 0.72 + k / 3) % 1, px = x2 + (x1 - x2) * f;
        const pp = this.cam.p(px, 0.06 + Math.sin(f * Math.PI) * 0.07, zWall);
        T.diamond(g, pp[0], pp[1], 3.4, cHot, true);
      }
    }
    const pMid = this.cam.p(0, 0.02, zWall - 0.14);
    T.txt(g, `← ${baseM.toFixed(1)} ${T.tr('m')} →`, pMid[0], pMid[1], Math.max(10, u * 0.72), cHot, 'm', 'center');

    // 4. Sculptural 3D Receiver Obelisks: TENGEN #1 (天) & M5 EAR #2 (眼)
    const drawMonolithBeacon = (bx, bz, col, sealCh, title, sub, active) => {
      const h = 0.26 + Math.sin(t * 2.0 + bx) * 0.018, s = 0.055;
      const pb = this.cam.p(bx, 0, bz), pt = this.cam.p(bx, h, bz), pm = this.cam.p(bx, h * 0.55, bz);
      // Hexagonal AT-Field ground pad
      drawPoly3D(bx, bz, 0.16, 6, t * (active ? 0.45 : 0.1), col, 1.5);
      // 3D Octahedral crystal tower
      const cr = Math.cos(t * 0.9), sr = Math.sin(t * 0.9);
      const ringPts = [[s, 0], [0, s], [-s, 0], [0, -s]].map(([ox, oz]) => this.cam.p(bx + ox * cr - oz * sr, h * 0.55, bz + ox * sr + oz * cr));
      g.strokeStyle = col; g.lineWidth = 1.4; g.beginPath();
      for (let i = 0; i < 4; i++) {
        const n = ringPts[(i + 1) & 3];
        g.moveTo(pb[0], pb[1]); g.lineTo(ringPts[i][0], ringPts[i][1]);
        g.moveTo(pt[0], pt[1]); g.lineTo(ringPts[i][0], ringPts[i][1]);
        g.moveTo(ringPts[i][0], ringPts[i][1]); g.lineTo(n[0], n[1]);
      }
      g.stroke();
      T.txt(g, sealCh, pm[0], pm[1] + u * 0.28, u * 0.85, cHot, 'k', 'center');
      T.txt(g, title, pt[0], pt[1] - u * 1.4, Math.max(11, u * 0.84), cHot, 't', 'center');
      T.txt(g, sub, pt[0], pt[1] - u * 0.58, Math.max(9, u * 0.64), col, 'm', 'center');
    };
    drawMonolithBeacon(x1, zWall, cAcc, '天', 'TENGEN #1', 'CORE · CH 6', true);
    drawMonolithBeacon(x2, zWall, earOk ? cAlt : cDim, '眼', 'M5 EAR #2',
      earOk ? `BAT ${L.ear.bat}% · ${L.ear.rssi} dBm` : T.tr('WAITING...'), earOk);

    // 5. Process & Smooth Triangulated Targets
    this.trailTick += dt;
    const sampleTrail = this.trailTick >= 0.12;
    if (sampleTrail) this.trailTick = 0;
    const smoothK = T.ease(dt, 1.25), readK = T.ease(dt, 1.6);
    const activeKeys = new Set(), items = [];
    let n1 = 0, n2 = 0, nBoth = 0, nMoving = 0;

    for (const row of (L.d || [])) {
      const [id, mask, r1, r2, rx, rz, rerr, type, vend, name, rawLabel] = row;
      if (mask & 1) n1++;
      if (mask & 2) n2++;
      const both = (mask & 3) === 3;
      if (both) nBoth++;
      if (this.filterIdx === 1 && !both) continue;
      if (this.filterIdx === 2 && Math.hypot(rx, rz) > 2.6) continue;

      const key = String(id);
      activeKeys.add(key);
      const tx = rx * sc, tz = zWall + rz * sc, ty = 0.16 + T.pct(Math.max(r1, r2)) * 0.28;
      let sp = this.pos.get(key);
      if (!sp) { sp = { x: tx, y: ty, z: tz, rx, rz, err: rerr, speed: 0, ly: null }; this.pos.set(key, sp); }
      else {
        const prevX = sp.rx, prevZ = sp.rz;
        sp.x += (tx - sp.x) * smoothK; sp.y += (ty - sp.y) * smoothK; sp.z += (tz - sp.z) * smoothK;
        sp.rx += (rx - sp.rx) * readK; sp.rz += (rz - sp.rz) * readK; sp.err += (rerr - sp.err) * readK;
        const instVel = dt > 0.001 ? Math.hypot(sp.rx - prevX, sp.rz - prevZ) / dt : 0;
        sp.speed = sp.speed * 0.92 + instVel * 0.08;
      }
      if (sampleTrail) {
        let tr = this.trails.get(key);
        if (!tr) { tr = []; this.trails.set(key, tr); }
        const last = tr[tr.length - 1];
        if (!last || Math.hypot(sp.x - last[0], sp.z - last[2]) > 0.015) {
          tr.push([sp.x, sp.y, sp.z]); if (tr.length > TRAIL_MAX) tr.shift();
        } else if (tr.length > 1) tr.shift();
      }
      const isAnchor = (L.anchor && id === L.anchor);
      const moving = !isAnchor && both && sp.speed > 0.18;
      if (moving) nMoving++;
      const trk = type === 'tracker';
      const localLbl = S.labels && S.labels.get(id);
      const effLabel = (localLbl ? localLbl.n : rawLabel) || '';
      const col = isAnchor ? cHot : trk ? cAlt : moving ? cHot : both ? cGrn : (mask & 1) ? cAcc : cDim;

      items.push({
        id, key, mask, both, moving, isAnchor, speed: sp.speed, r1, r2,
        rx: sp.rx, rz: sp.rz, rerr: sp.err,
        x: sp.x, y: sp.y, z: sp.z, err3d: Math.max(0.08, sp.err * sc * 0.42),
        sp, type, vend, name, rawLabel: effLabel,
        label: (isAnchor ? '◆ ' : '') + (effLabel || name || vend || type || 'BLE'),
        col, alert: trk,
        p: this.cam.p(sp.x, sp.y, sp.z),
        pb: this.cam.p(sp.x, 0, sp.z),
        prio: (isAnchor ? 260 : 0) + (both ? 100 : 0) + (trk ? 80 : 0) + (moving ? 50 : 0) + Math.max(r1, r2),
      });
    }
    if (sampleTrail) for (const k of this.pos.keys()) if (!activeKeys.has(k)) { this.pos.delete(k); this.trails.delete(k); }

    const topDual = new Set(items.slice().sort((a, b) => b.prio - a.prio).slice(0, 6));
    const labeledList = items.filter(it => topDual.has(it) || it.isAnchor || (this.sel && this.sel.id === it.id)).sort((a, b) => b.prio - a.prio);
    const labelBoxes = [], fpx = Math.max(10, u * 0.72), lySmooth = T.ease(dt, 0.25);

    for (const it of labeledList) {
      const [sx, sy, scP] = it.p, rad = Math.max(3.5, Math.min(11, scP * 0.012 * (it.both ? 1.25 : 0.95)));
      const dTxt = it.both ? ` (${it.rx >= 0 ? '+' : ''}${it.rx.toFixed(1)}m, ${it.rz.toFixed(1)}m)` : '';
      it.fullTxt = T.fit(g, it.label, u * 11) + dTxt;
      const tw = it.fullTxt.length * fpx * 0.62, lx = sx + rad + 6;
      let targetLy = sy + 3, placed = false;
      for (let step = 0; step < 12; step++) {
        const candY = (sy + 3) + (step === 0 ? 0 : (step % 2 ? -1 : 1) * Math.ceil(step / 2) * (fpx + 5));
        const hit = labelBoxes.some(b => lx < b.x + b.w + 4 && lx + tw + 4 > b.x && Math.abs(candY - b.y) < fpx + 4);
        if (!hit) { targetLy = candY; placed = true; break; }
      }
      if (!placed && !it.isAnchor && !(this.sel && this.sel.id === it.id)) { it.fullTxt = null; continue; }
      it.sp.ly = it.sp.ly == null ? targetLy : it.sp.ly + (targetLy - it.sp.ly) * lySmooth;
      labelBoxes.push({ x: lx, y: targetLy, w: tw, h: fpx + 4 });
    }

    items.sort((a, b) => b.p[3] - a.p[3]);
    const hex2D = (x, y, r, rot) => {
      g.beginPath();
      for (let i = 0; i <= 6; i++) { const a = rot + i * T.TAU / 6; i ? g.lineTo(x + Math.cos(a) * r, y + Math.sin(a) * r) : g.moveTo(x + Math.cos(a) * r, y + Math.sin(a) * r); }
    };

    for (const it of items) {
      const isSel = this.sel && this.sel.id === it.id;
      const [sx, sy, scP] = it.p, [bx, by] = it.pb;
      const rad = Math.max(3.5, Math.min(11, scP * 0.012 * (it.both ? 1.25 : 0.95)));

      if (it.both && (topDual.has(it) || isSel)) {
        g.strokeStyle = isSel ? cHot : cLine + '0.26)';
        g.lineWidth = isSel ? 1.7 : 0.95;
        g.beginPath(); g.moveTo(pSt1[0], pSt1[1]); g.lineTo(bx, by); g.stroke();
        g.strokeStyle = isSel ? cAlt : cAltA + '0.26)';
        g.beginPath(); g.moveTo(pSt2[0], pSt2[1]); g.lineTo(bx, by); g.stroke();
        // Octagonal AT-Field confidence zone on floor
        drawPoly3D(it.x, it.z, it.err3d * (isSel ? 1.0 : 0.58), 8, t * 0.4, isSel ? cHot : cLine + '0.28)', isSel ? 1.6 : 0.95);
      } else if (!it.both && isSel) {
        const pSrc = (it.mask & 1) ? pSt1 : pSt2;
        g.strokeStyle = cLine + '0.35)'; g.lineWidth = 1;
        g.beginPath(); g.moveTo(pSrc[0], pSrc[1]); g.lineTo(bx, by); g.stroke();
      }

      if (it.moving) {
        const mr = 0.08 + ((t * 0.9) % 1) * 0.24;
        drawPoly3D(it.x, it.z, mr, 24, 0, cAltA + (0.68 * (1 - ((t * 0.9) % 1))).toFixed(2) + ')', 1.3);
      }

      const tr = this.trails.get(it.key);
      if (tr && tr.length > 1) {
        for (let k = 1; k < tr.length; k++) {
          const pA = this.cam.p(tr[k - 1][0], tr[k - 1][1], tr[k - 1][2]), pB = this.cam.p(tr[k][0], tr[k][1], tr[k][2]);
          g.strokeStyle = it.col; g.globalAlpha = (k / tr.length) * 0.45; g.lineWidth = Math.max(1, (k / tr.length) * 2.4);
          g.beginPath(); g.moveTo(pA[0], pA[1]); g.lineTo(pB[0], pB[1]); g.stroke();
        }
        g.globalAlpha = 1;
      }

      g.strokeStyle = it.col; g.globalAlpha = it.both ? 0.55 : 0.25; g.lineWidth = 1;
      g.beginPath(); g.moveTo(bx, by); g.lineTo(sx, sy); g.stroke();
      T.diamond(g, bx, by, Math.max(2.2, rad * 0.45), it.col, true);
      g.globalAlpha = 1;

      T.diamond(g, sx, sy, rad * 1.15, it.col, true);
      if (it.both || isSel) {
        const rr = rad * (isSel ? 2.5 : 1.9);
        g.strokeStyle = isSel ? cHot : it.moving ? cAlt : it.col;
        g.lineWidth = isSel ? 1.8 : 1.25;
        hex2D(sx, sy, rr, t * 1.5); g.stroke();
      }

      if (it.fullTxt) {
        const lx = sx + rad + 6, ly = it.sp.ly != null ? it.sp.ly : sy + 3;
        if (Math.abs(ly - (sy + 3)) > 4) {
          g.strokeStyle = it.col; g.globalAlpha = 0.42; g.lineWidth = 0.8;
          g.beginPath(); g.moveTo(sx + rad, sy); g.lineTo(lx - 2, ly - fpx * 0.3); g.stroke();
          g.globalAlpha = 1;
        }
        T.txt(g, it.fullTxt, lx, ly, fpx, isSel ? cHot : it.col, 'm', 'left');
      }
      T.hits.circle(sx, sy, Math.max(15, rad * 2.3), { node: it });
      if (isSel) this.sel = it;
    }

    this.dust.draw(g, this.cam);
    T.bloom(this.hq.cv, 0.52);

    // 6. Chamfered Evangelion × Zen Tactical Controls
    const pad = u * 1.1;
    T.drawSealChar(g, pad, pad + u * 0.1, u * 2.4, '距', cAcc, false);
    T.txt(g, T.tr('LOCATOR 3D (2 STICKS)'), pad + u * 2.9, pad + u * 1.25, u * 1.45, cHot, 't', 'left');
    const earSub = earOk
      ? `${T.tr('EAR #2 ONLINE')} · ${T.tr('BAT')} ${L.ear.bat}% · ${L.ear.rssi} dBm · ${T.tr('OFS')} ${L.ofs >= 0 ? '+' : ''}${L.ofs} dB`
      : T.tr('TURN ON M5STICKC PLUS (EAR #2) — CONNECTS AUTOMATICALLY ON CH 6');
    T.txt(g, earSub, pad + u * 2.9, pad + u * 2.4, u * 0.70, earOk ? cGrn : cAlt, 'm', 'left');

    const bh = u * 1.95, gap = u * 0.4, py0 = pad + u * 0.25;
    const wFlt = u * 7.4, wMinus = u * 2.2, wBase = u * 7.2, wPlus = u * 2.2, wCal = u * 10.0, wRst = u * 2.3;
    let px0 = W - pad - (wFlt + wMinus + wBase + wPlus + wCal + wRst + gap * 5);

    const drawBtn = (w, txt, col, hit, fill = false) => {
      T.panel(g, px0, py0, w, bh, u * 0.45, fill ? (kur ? 'rgba(229,38,47,.22)' : 'rgba(62,230,255,.16)') : 'rgba(8,14,20,.68)', col);
      T.txt(g, txt, px0 + w / 2, py0 + bh * 0.7, u * 0.68, cHot, 'm', 'center');
      if (hit) T.hits.rect(px0, py0 - 4, w, bh + 8, hit);
      px0 += w + gap;
    };
    drawBtn(wFlt, '◆ ' + T.tr(FILTERS[this.filterIdx]), this.filterIdx ? cGrn : cLine + '0.45)', { act: 'flt' }, this.filterIdx > 0);
    drawBtn(wMinus, '−', cLine + '0.45)', { act: 'base', d: -0.5 });
    drawBtn(wBase, `${T.tr('BASE')} ${baseM.toFixed(1)}${T.tr('m')}`, cLine + '0.35)', null);
    drawBtn(wPlus, '+', cLine + '0.45)', { act: 'base', d: 0.5 });
    drawBtn(wCal, '▸ ' + T.tr('CALIBRATE'), cAcc, { act: 'cal' }, true);
    drawBtn(wRst, '◇', cLine + '0.45)', { act: 'rst' });

    const sy0 = H - pad - u * 3.8;
    const stats = [
      [T.tr('LOCKED 2X'), nBoth, cGrn],
      ['TENGEN #1 · 天', n1, cAcc],
      ['M5 EAR #2 · 眼', earOk ? n2 : 'OFF', earOk ? cAlt : cDim],
      [T.tr('MOVING') + ' · 動', nMoving, nMoving ? cAlt : cHot],
    ];
    stats.forEach(([k, v, col], i) => {
      const bx = pad + i * u * 8.1;
      T.panel(g, bx, sy0, u * 7.5, u * 3.45, u * 0.55, kur ? 'rgba(18,18,18,.78)' : 'rgba(5,12,18,.74)', cLine + '0.34)');
      T.txt(g, k, bx + u * 0.55, sy0 + u * 1.1, u * 0.62, cDim, 'k', 'left');
      T.txt(g, String(v), bx + u * 0.55, sy0 + u * 2.88, u * 1.45, col, 't', 'left');
    });

    if (this.toastT > 0 && this.toastMsg) {
      const bw = Math.min(W * 0.65, u * 28), th = u * 2.6, bx = (W - bw) / 2, by = pad + u * 3.4;
      T.panel(g, bx, by, bw, th, u * 0.6, this.toastBad ? 'rgba(255,59,48,.24)' : 'rgba(62,230,255,.18)', this.toastBad ? cAlt : cAcc);
      T.txt(g, this.toastMsg, W / 2, by + th * 0.68, u * 0.95, cHot, 't', 'center');
    }

    if (this.sel) {
      const s = this.sel;
      const cw = Math.min(W * 0.44, u * 23.5), ch = u * 7.3, cx0 = W - pad - cw, cy0 = H - pad - ch;
      const ac = s.both ? cGrn : cAcc;
      T.panel(g, cx0, cy0, cw, ch, u * 0.8, kur ? 'rgba(14,14,14,.94)' : 'rgba(6,16,24,.92)', ac);
      T.brackets(g, cx0 + 3, cy0 + 3, cw - 6, ch - 6, u * 0.45, ac, 1);
      T.txt(g, T.fit(g, s.label, cw - u * 7.8), cx0 + u * 0.75, cy0 + u * 1.65, u * 1.15, cHot, 't', 'left');

      const nbW = u * 6.2, nbH = u * 1.65, nbX = cx0 + cw - nbW - u * 0.65, nbY = cy0 + u * 0.55;
      T.panel(g, nbX, nbY, nbW, nbH, u * 0.4, cLine + '0.18)', cAcc);
      T.txt(g, '▸ ' + T.tr('NAME'), nbX + nbW / 2, nbY + nbH * 0.72, u * 0.66, cHot, 'm', 'center');
      T.hits.rect(nbX, nbY - 4, nbW, nbH + 8, { act: 'name' });

      const d1 = (s.mask & 1) ? rssiToM(s.r1, s.type).toFixed(1) + 'm (' + s.r1 + ' dBm)' : '—';
      const d2 = (s.mask & 2) ? rssiToM(s.r2, s.type).toFixed(1) + 'm (' + s.r2 + ' dBm)' : '—';
      T.txt(g, `#1: ${d1}  ·  #2: ${d2}`, cx0 + u * 0.75, cy0 + u * 3.35, u * 0.72, cAcc, 'm', 'left');
      const posStr = s.both
        ? `X: ${s.rx >= 0 ? '+' : ''}${s.rx.toFixed(1)}m · Z: ${s.rz.toFixed(1)}m · ±${s.rerr.toFixed(1)}m`
        : T.tr('HEARD BY 1 STICK ONLY');
      T.txt(g, posStr, cx0 + u * 0.75, cy0 + u * 4.9, u * 0.75, s.both ? cGrn : cDim, 'm', 'left');
      const stStr = s.moving ? '⚡ ' + T.tr('MOVING') : (s.vend || s.type || 'BLE').toUpperCase();
      T.txt(g, stStr, cx0 + u * 0.75, cy0 + u * 6.35, u * 0.68, s.moving ? cAlt : cDim, 'm', 'left');
    }
  },
};
})();
