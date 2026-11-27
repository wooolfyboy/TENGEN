// ---- HQ > HOLO 3D: Evangelion Sephirotic / AT-Field Dome × Zen Dharma Mandala ----
// Interactive 360° orbit sanctuary: Axis Mundi light pillar, triple armillary gyro-sphere,
// inscribed octagonal floor mandala, hexagonal AT-Field target locks & live energy arcs.
(() => {
const GL_FS = `
uniform vec2 res;
uniform float t, sweep, energy, pulse, pulseT, kurenai;
uniform vec2 center;
void main() {
  vec2 uv = (gl_FragCoord.xy - center) / min(res.x, res.y);
  float r = length(uv);
  float ang = atan(uv.y, uv.x);
  vec3 bg = kurenai > 0.5 ? vec3(0.043, 0.043, 0.043) : vec3(0.012, 0.022, 0.036);
  vec3 acc = kurenai > 0.5 ? vec3(0.90, 0.15, 0.18) : vec3(0.24, 0.90, 1.0);
  vec3 hot = kurenai > 0.5 ? vec3(0.96, 0.94, 0.92) : vec3(1.0, 0.56, 0.12);
  vec3 col = bg;
  // Sacred core aura + octagonal AT-Field interference harmonic
  float oct = cos(ang * 8.0 + t * 0.25) * 0.5 + 0.5;
  col += acc * exp(-r * 3.8) * (0.24 + energy * 0.34);
  col += hot * exp(-abs(r - 0.34) * 26.0) * oct * (0.06 + energy * 0.14);
  // Zen temple water ripples (audio-reactive)
  float ring = sin(r * 36.0 - t * 3.0) * 0.5 + 0.5;
  col += acc * pow(ring, 6.0) * exp(-r * 2.6) * (0.07 + energy * 0.20);
  // Rotating NERV sweep wake
  float da = mod(sweep - ang + 6.2831853, 6.2831853);
  float sw = exp(-da * 3.4) * smoothstep(0.68, 0.02, r);
  col += acc * sw * 0.22;
  // Shockwave ring on IR / NFC / tracker / 433 burst
  if (pulseT > 0.0 && pulseT < 2.2) {
    float pr = pulseT * 0.44;
    float w = exp(-abs(r - pr) * 28.0) * (1.0 - pulseT / 2.2);
    col += mix(acc, hot, pulse) * w * 0.82;
  }
  col *= smoothstep(1.18, 0.22, r);
  gl_FragColor = vec4(col, 1.0);
}`;

let glProg = null;
const angFor = id => (T.hash(id) % 3600) * (T.TAU / 3600);
const TRAIL_MAX = 14;

T.hqHolo = {
  init(hq) {
    this.hq = hq;
    this.cam = T.orbit(0.65, 0.84, 2.95);
    this.drag = T.drag(hq.cv.cv, this.cam, { minPitch: 0.25, maxPitch: 1.38 });
    this.dust = T.dust(210, 1.95, 1.3);
    this.sel = null;
    this.layers = { ble: true, wifi: true, sens: true, audio: true };
    this.pos = new Map();
    this.trails = new Map();
    this.trailTick = 0;
    this.resetAnim = 0;
    this.pulseT = 9.0; this.pulseHot = 0; this.pulseMsg = '';
    this.lastIr = 0; this.lastNfc = 0; this.lastBurst = 0; this.lastTrk = 0;
    this.drag.onTap = (x, y) => {
      if (hq.sub !== 1) return;
      const h = T.hits.at(x, y);
      if (h && h.layer) { this.layers[h.layer] = !this.layers[h.layer]; return; }
      if (h && h.reset) { this.resetAnim = 1.0; this.sel = null; return; }
      if (h && h.go) { T.app.go(h.go); return; }
      if (h && h.node) { this.sel = h.node; return; }
      this.sel = null;
    };
  },

  trigger(msg, hot = 0) {
    this.pulseT = 0.01;
    this.pulseHot = hot ? 1 : 0;
    this.pulseMsg = msg || '';
  },

  watchEvents(now) {
    if (S.ir && S.ir.t && S.ir.t !== this.lastIr) {
      this.lastIr = S.ir.t;
      const c = S.ir.last;
      this.trigger('IR · ' + ((c && (c.brand || c.p)) || 'REMOTE'), 1);
    }
    if (S.nfc && S.nfc.t && S.nfc.t !== this.lastNfc) {
      this.lastNfc = S.nfc.t;
      const c = S.nfc.last;
      this.trigger('NFC · ' + ((c && c.fam) || 'CARD'), 1);
    }
    if (S.sub && S.sub.burst && now - this.lastBurst > 2500) {
      this.lastBurst = now;
      this.trigger('433 MHZ BURST', 1);
    }
    if (S.devInit && now - this.lastTrk > 8000) {
      for (const e of S.dev.values()) {
        if (e.alive && S.isTrk(e) && now - e.born < 4000) {
          this.lastTrk = now;
          this.trigger('TRACKER · ' + (e.label || e.name || 'TAG'), 1);
          break;
        }
      }
    }
  },

  frame(dt, t, g, W, H) {
    const now = performance.now();
    const u = Math.min(W / 86, H / 52);
    const kur = T.theme === 'kurenai';
    const cAcc = kur ? '#e5262f' : '#3ee6ff';
    const cHot = kur ? '#f4f1ea' : '#ebfaff';
    const cAlt = kur ? '#ff3b30' : '#ff8c1a';
    const cGrn = kur ? '#f4f1ea' : '#69ff6e';
    const cDim = kur ? '#8a8a8a' : '#6c8aa0';
    const cLine = kur ? 'rgba(244,241,234,' : 'rgba(62,230,255,';
    const cAltA = kur ? 'rgba(255,59,48,' : 'rgba(255,140,26,';

    this.watchEvents(now);
    this.pulseT = Math.min(9.0, this.pulseT + dt);
    this.drag.tick(dt);
    if (this.resetAnim > 0) {
      this.resetAnim = Math.max(0, this.resetAnim - dt * 2.2);
      const k = T.ease(dt, 0.14);
      this.cam.yaw += (0.65 - this.cam.yaw) * k;
      this.cam.pitch += (0.84 - this.cam.pitch) * k;
      this.cam.D += (2.95 - this.cam.D) * k;
    } else if (this.drag.idle()) {
      this.cam.yaw += dt * (0.15 + Math.min(0.22, (S.energy || 0) * 0.2));
      this.cam.pitch += (0.80 + Math.sin(t * 0.25) * 0.11 - this.cam.pitch) * T.ease(dt, 2.5);
    }
    this.dust.tick(dt);
    T.hits.clear();

    const cx = W * 0.5, cy = H * 0.55;
    const F = Math.min(W, H) * 0.78;
    this.cam.frame(cx, cy, F);

    // 1. WebGL atmospheric mandala + shockwave
    if (T.GL && T.GL.ok) {
      if (!glProg) glProg = T.GL.prog(GL_FS);
      if (glProg) {
        T.GL.begin(W, H, 0.65);
        T.GL.quad(glProg, {
          t, sweep: -this.cam.yaw + t * 1.15, energy: this.layers.audio ? (S.energy || 0) : 0,
          pulse: this.pulseHot, pulseT: this.pulseT, kurenai: kur ? 1 : 0,
          center: [T.GL.w * 0.5, T.GL.h * (1.0 - cy / Math.max(1, H))],
        });
        T.GL.draw(g, 0, 0, W, H);
      } else T.backdrop(g, W, H, 0.5, 0.55);
    } else T.backdrop(g, W, H, 0.5, 0.55);

    // 2. Sacred 3D Floor Mandala (Octagram AT-Field + Incomplete Ensō Outer Halo + Dome Meridians)
    const drawArc3D = (r, y, a0, a1, segs, col, lw = 1) => {
      g.strokeStyle = col; g.lineWidth = lw; g.beginPath();
      for (let i = 0; i <= segs; i++) {
        const a = a0 + (a1 - a0) * (i / segs), p = this.cam.p(Math.cos(a) * r, y, Math.sin(a) * r);
        i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]);
      }
      g.stroke();
    };
    const drawPoly3D = (r, y, sides, rot, col, lw = 1) => {
      g.strokeStyle = col; g.lineWidth = lw; g.beginPath();
      for (let i = 0; i <= sides; i++) {
        const a = rot + (i / sides) * T.TAU, p = this.cam.p(Math.cos(a) * r, y, Math.sin(a) * r);
        i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]);
      }
      g.stroke();
    };
    const drawTiltedRing3D = (r, cy0, tiltX, rotY, segs, col, lw = 1, span = T.TAU) => {
      g.strokeStyle = col; g.lineWidth = lw; g.beginPath();
      const ct = Math.cos(tiltX), st = Math.sin(tiltX), cr = Math.cos(rotY), sr = Math.sin(rotY);
      for (let i = 0; i <= segs; i++) {
        const a = (i / segs) * span;
        const x0 = Math.cos(a) * r, z0 = Math.sin(a) * r;
        const y1 = -z0 * st, z1 = z0 * ct;
        const p = this.cam.p(x0 * cr - z1 * sr, cy0 + y1, x0 * sr + z1 * cr);
        i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]);
      }
      g.stroke();
    };

    for (let k = 1; k <= 4; k++) drawArc3D(k * 0.38, 0, 0, T.TAU, 64, cLine + (k === 4 ? '0.45)' : '0.16)'), k === 4 ? 1.5 : 1);
    // Outer Rotating Incomplete Zen Ensō Ring
    drawArc3D(1.66, 0, t * 0.14, t * 0.14 + Math.PI * 1.72, 60, cAltA + '0.48)', 1.6);
    // Inscribed Sacred 8-Pointed Dharma / AT-Field Octagram on floor
    drawPoly3D(1.52, 0, 8, t * 0.04, cLine + '0.16)', 1);
    drawPoly3D(1.52, 0, 8, t * 0.04 + Math.PI / 8, cLine + '0.16)', 1);
    if (this.layers.wifi) {
      drawArc3D(1.26, 0.72, 0, T.TAU, 64, cLine + '0.18)', 1);
      drawPoly3D(1.26, 0.72, 6, -t * 0.06, cAltA + '0.14)', 1);
    }

    const o0 = this.cam.p(0, 0, 0), oZen = this.cam.p(0, 1.18, 0);
    g.strokeStyle = cLine + '0.12)'; g.lineWidth = 1;
    for (let i = 0; i < 8; i++) {
      const a = i * Math.PI / 4, q = this.cam.p(Math.cos(a) * 1.52, 0, Math.sin(a) * 1.52);
      g.beginPath(); g.moveTo(o0[0], o0[1]); g.lineTo(q[0], q[1]); g.stroke();
    }
    g.strokeStyle = cLine + '0.09)';
    for (let m = 0; m < 4; m++) {
      const phi = m * Math.PI / 4;
      g.beginPath();
      for (let i = 0; i <= 28; i++) {
        const th = (i / 28) * Math.PI, rr = Math.sin(th) * 1.52, yy = Math.cos(th) * 1.15;
        if (yy < 0) continue;
        const p = this.cam.p(Math.cos(phi) * rr, yy, Math.sin(phi) * rr);
        i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]);
      }
      g.stroke();
    }

    // Axis Mundi Vertical Light Pillar piercing the dome
    const axG = g.createLinearGradient(o0[0], o0[1], oZen[0], oZen[1]);
    axG.addColorStop(0, cLine + '0.45)'); axG.addColorStop(0.35, cAltA + '0.55)'); axG.addColorStop(1, cLine + '0)');
    g.strokeStyle = axG; g.lineWidth = 1.6;
    g.beginPath(); g.moveTo(o0[0], o0[1]); g.lineTo(oZen[0], oZen[1]); g.stroke();

    // 3. 3D Audio Spectrum Lotus Ring around core (32 bars)
    if (this.layers.audio) {
      const specR = 0.42;
      g.lineWidth = Math.max(1.5, u * 0.14);
      for (let k = 0; k < 32; k++) {
        const a = (k / 32) * T.TAU + t * 0.15;
        const v = (S.bands && S.bands[k]) || (0.04 + 0.03 * Math.sin(t * 2.5 + k * 0.5));
        const bh = 0.03 + v * 0.52;
        const x = Math.cos(a) * specR, z = Math.sin(a) * specR;
        const p0 = this.cam.p(x, 0, z), p1 = this.cam.p(x, bh, z);
        g.strokeStyle = v > 0.65 ? cAlt : v > 0.3 ? cHot : cAcc;
        g.globalAlpha = 0.45 + v * 0.55;
        g.beginPath(); g.moveTo(p0[0], p0[1]); g.lineTo(p1[0], p1[1]); g.stroke();
      }
      g.globalAlpha = 1;
    }

    // 4. Center Ramiel Octahedron + Triple Armillary Ensō Sphere + Heavenly Eye (眼)
    const coreY = 0.30 + Math.sin(t * 1.7) * 0.03;
    const cs = 0.15 + (this.layers.audio ? (S.energy || 0) * 0.045 : 0);
    const verts = [[0, cs * 1.5, 0], [0, -cs * 1.5, 0], [cs, 0, 0], [-cs, 0, 0], [0, 0, cs], [0, 0, -cs]];
    const cr = Math.cos(t * 0.85), sr = Math.sin(t * 0.85);
    const pv = verts.map(([x, y, z]) => this.cam.p(x * cr - z * sr, coreY + y, x * sr + z * cr));
    const edges = [[0, 2], [0, 3], [0, 4], [0, 5], [1, 2], [1, 3], [1, 4], [1, 5], [2, 4], [4, 3], [3, 5], [5, 2]];
    const coreP = this.cam.p(0, coreY, 0);
    const cg = g.createRadialGradient(coreP[0], coreP[1], 1, coreP[0], coreP[1], Math.max(16, u * 2.8));
    cg.addColorStop(0, cHot); cg.addColorStop(0.35, cLine + '0.45)'); cg.addColorStop(1, cLine + '0)');
    g.fillStyle = cg; g.beginPath(); g.arc(coreP[0], coreP[1], Math.max(16, u * 2.8), 0, T.TAU); g.fill();
    g.strokeStyle = cHot; g.lineWidth = 1.5;
    g.beginPath(); for (const [a, b] of edges) { g.moveTo(pv[a][0], pv[a][1]); g.lineTo(pv[b][0], pv[b][1]); } g.stroke();
    drawTiltedRing3D(0.24, coreY, 0.38, t * 0.72, 36, cLine + '0.68)', 1.3, Math.PI * 1.75);
    drawTiltedRing3D(0.30, coreY, -0.54, -t * 0.48, 36, cAltA + '0.52)', 1.1, Math.PI * 1.68);
    drawTiltedRing3D(0.36, coreY, 0.92, t * 0.28, 36, cLine + '0.28)', 1.0, T.TAU);
    T.txt(g, '眼', coreP[0], coreP[1] + u * 0.35, u * 1.05, cHot, 'k', 'center');

    // 5. Collect & Smooth 3D RF Objects
    const rawItems = [];
    let nBle = 0, nWf = 0, nSn = 0;
    for (const e of S.dev.values()) {
      if (!e.alive) continue;
      nBle++;
      if (!this.layers.ble) continue;
      const p = T.pct(e.rs), r = 0.48 + (1 - p) * 0.98, a = angFor(e.id), y = 0.14 + p * 0.44;
      const trk = S.isTrk(e), nw = S.isNew(e, now);
      rawItems.push({
        kind: 'ble', key: 'b:' + e.id, id: e.id, tx: Math.cos(a) * r, ty: y, tz: Math.sin(a) * r,
        rssi: e.rs, pct: p, alert: trk || nw, trk,
        col: trk || nw ? cAlt : p > 0.6 ? cHot : cAcc,
        label: e.label || e.name || e.vend || e.type || 'BLE',
        sub: (e.type || 'device').toUpperCase() + ' · ' + Math.round(e.rs) + ' dBm',
        go: 'ble', prio: (trk ? 100 : nw ? 80 : 0) + p * 50,
      });
    }
    for (const a of S.aps.values()) {
      if (!a.alive) continue;
      nWf++;
      if (!this.layers.wifi) continue;
      const p = T.pct(a.rs), r = 0.42 + (1 - p) * 0.96, ang = angFor(a.b || a.s), y = 0.62 + ((a.c || 6) / 13) * 0.44;
      rawItems.push({
        kind: 'wifi', key: 'w:' + (a.b || a.s), id: a.b || a.s, tx: Math.cos(ang) * r, ty: y, tz: Math.sin(ang) * r,
        rssi: a.rs, pct: p, alert: a.e === 'open',
        col: a.e === 'open' ? cAlt : cHot,
        label: a.s || '<hidden>',
        sub: 'WI-FI CH ' + (a.c || '?') + ' · ' + Math.round(a.rs) + ' dBm',
        go: 'wifi', prio: 10 + p * 40,
      });
    }
    const sens = (S.sens && S.sens.s) || [];
    for (const s of sens) {
      nSn++;
      if (!this.layers.sens) continue;
      const ang = angFor('433_' + s.id + '_' + s.m), r = 1.38, y = 0.34;
      const tStr = s.t != null ? (s.t > 0 ? '+' : '') + Number(s.t).toFixed(1) + '°C' : '433';
      rawItems.push({
        kind: 'sens', key: 's:' + s.id, id: s.id, tx: Math.cos(ang) * r, ty: y, tz: Math.sin(ang) * r,
        rssi: s.r || -70, pct: 0.7, alert: false, col: cGrn,
        label: tStr + (s.h != null ? ' ' + s.h + '%' : ''),
        sub: '433 MHZ · ' + (s.m || 'SENSOR'),
        go: 'sub', prio: 70,
      });
    }

    this.trailTick += dt;
    const sampleTrail = this.trailTick >= 0.1;
    if (sampleTrail) this.trailTick = 0;
    const smoothK = T.ease(dt, 1.1);
    const activeKeys = new Set(), items = [];
    for (const it of rawItems) {
      activeKeys.add(it.key);
      let sp = this.pos.get(it.key);
      if (!sp) { sp = [it.tx, it.ty, it.tz]; this.pos.set(it.key, sp); }
      else { sp[0] += (it.tx - sp[0]) * smoothK; sp[1] += (it.ty - sp[1]) * smoothK; sp[2] += (it.tz - sp[2]) * smoothK; }
      it.x = sp[0]; it.y = sp[1]; it.z = sp[2];
      if (sampleTrail) {
        let tr = this.trails.get(it.key);
        if (!tr) { tr = []; this.trails.set(it.key, tr); }
        const last = tr[tr.length - 1];
        if (!last || Math.hypot(sp[0] - last[0], sp[1] - last[1], sp[2] - last[2]) > 0.012) {
          tr.push([sp[0], sp[1], sp[2]]); if (tr.length > TRAIL_MAX) tr.shift();
        } else if (tr.length > 1) tr.shift();
      }
      it.p = this.cam.p(it.x, it.y, it.z);
      it.pb = this.cam.p(it.x, 0, it.z);
      items.push(it);
    }
    if (sampleTrail) for (const k of this.pos.keys()) if (!activeKeys.has(k)) { this.pos.delete(k); this.trails.delete(k); }

    // 5a. Energy Arcs to top 6 nodes
    const arcNodes = items.slice().sort((a, b) => b.prio - a.prio).slice(0, 6);
    for (let idx = 0; idx < arcNodes.length; idx++) {
      const it = arcNodes[idx];
      const mx = it.x * 0.5, mz = it.z * 0.5, my = Math.max(coreY, it.y) + 0.28 + (idx % 2) * 0.08;
      g.strokeStyle = it.alert ? cAltA + '0.48)' : cLine + '0.28)';
      g.lineWidth = it.alert ? 1.6 : 1.1;
      g.beginPath();
      for (let s = 0; s <= 16; s++) {
        const k = s / 16, ik = 1 - k;
        const pp = this.cam.p(ik * ik * it.x + 2 * ik * k * mx, ik * ik * it.y + 2 * ik * k * my + k * k * coreY, ik * ik * it.z + 2 * ik * k * mz);
        s ? g.lineTo(pp[0], pp[1]) : g.moveTo(pp[0], pp[1]);
      }
      g.stroke();
      const spd = it.alert ? 0.72 : 0.42 + it.pct * 0.25, pk = (t * spd + idx * 0.27) % 1, ipk = 1 - pk;
      const sp = this.cam.p(ipk * ipk * it.x + 2 * ipk * pk * mx, ipk * ipk * it.y + 2 * ipk * pk * my + pk * pk * coreY, ipk * ipk * it.z + 2 * ipk * pk * mz);
      T.diamond(g, sp[0], sp[1], it.alert ? 3.8 : 2.8, it.alert ? cAlt : cHot, true);
    }

    // 5b. Depth-sorted nodes + Hexagonal AT-Field Reticles
    items.sort((a, b) => b.p[3] - a.p[3]);
    const labeled = new Set(items.slice().sort((a, b) => b.prio - a.prio).slice(0, 8));
    const hex2D = (x, y, r, rot) => {
      g.beginPath();
      for (let i = 0; i <= 6; i++) { const a = rot + i * T.TAU / 6; i ? g.lineTo(x + Math.cos(a) * r, y + Math.sin(a) * r) : g.moveTo(x + Math.cos(a) * r, y + Math.sin(a) * r); }
    };

    for (const it of items) {
      const tr = this.trails.get(it.key);
      if (tr && tr.length > 1) {
        for (let k = 1; k < tr.length; k++) {
          const pA = this.cam.p(tr[k - 1][0], tr[k - 1][1], tr[k - 1][2]), pB = this.cam.p(tr[k][0], tr[k][1], tr[k][2]);
          g.strokeStyle = it.col; g.globalAlpha = (k / tr.length) * 0.45; g.lineWidth = Math.max(1, (k / tr.length) * 2.4);
          g.beginPath(); g.moveTo(pA[0], pA[1]); g.lineTo(pB[0], pB[1]); g.stroke();
        }
        g.globalAlpha = 1;
      }
      const [sx, sy, sc] = it.p, [bx, by] = it.pb;
      const rad = Math.max(3.2, Math.min(11, sc * 0.011 * (it.alert ? 1.45 : 1)));
      const isSel = this.sel && this.sel.id === it.id && this.sel.kind === it.kind;
      g.strokeStyle = it.col; g.globalAlpha = it.alert || isSel ? 0.58 : 0.22; g.lineWidth = 1;
      g.beginPath(); g.moveTo(bx, by); g.lineTo(sx, sy); g.stroke();
      T.diamond(g, bx, by, Math.max(2, rad * 0.45), it.col, true);
      g.globalAlpha = 1;

      g.fillStyle = it.col; g.strokeStyle = it.col;
      if (it.kind === 'ble') {
        g.beginPath(); g.arc(sx, sy, rad, 0, T.TAU); g.fill();
      } else if (it.kind === 'wifi') {
        T.diamond(g, sx, sy, rad * 1.25, it.col, false);
        g.globalAlpha = 0.28; T.diamond(g, sx, sy, rad * 1.25, it.col, true); g.globalAlpha = 1;
      } else {
        g.beginPath(); g.moveTo(sx, sy - rad * 1.3); g.lineTo(sx + rad * 1.2, sy + rad); g.lineTo(sx - rad * 1.2, sy + rad); g.closePath(); g.fill();
      }

      if (it.alert || isSel) {
        const rr = rad * (isSel ? 2.7 : 2.2 + 0.3 * Math.sin(t * 5));
        g.strokeStyle = isSel ? cHot : cAlt; g.lineWidth = isSel ? 1.8 : 1.3;
        hex2D(sx, sy, rr, t * 1.4); g.stroke();
        if (isSel) { g.lineWidth = 0.9; hex2D(sx, sy, rr * 1.35, -t * 0.9); g.stroke(); }
      }

      if (labeled.has(it) || isSel) {
        T.txt(g, T.fit(g, it.label, u * 12), sx + rad + 6, sy + 3, Math.max(10, u * 0.72), isSel ? cHot : it.col, 'm', 'left');
      }
      T.hits.circle(sx, sy, Math.max(14, rad * 2.2), { node: it });
    }

    this.dust.draw(g, this.cam);
    T.bloom(this.hq.cv, 0.58);

    // 6. Chamfered Evangelion × Zen Corner HUD
    const pad = u * 1.1, topY = pad;
    T.drawSealChar(g, pad, topY + u * 0.1, u * 2.4, '天', cAcc, false);
    T.txt(g, T.tr('HOLO-DOME 3D'), pad + u * 2.9, topY + u * 1.25, u * 1.45, cHot, 't', 'left');
    T.txt(g, T.tr('DRAG TO ORBIT · TAP NODE'), pad + u * 2.9, topY + u * 2.4, u * 0.70, cDim, 'm', 'left');

    const pills = [
      { key: 'ble', label: 'BLE', col: cAcc },
      { key: 'wifi', label: 'WI-FI', col: cHot },
      { key: 'sens', label: '433', col: cGrn },
      { key: 'audio', label: T.tr('AUDIO'), col: cAlt },
    ];
    const pw = u * 4.7, ph = u * 1.9, pgap = u * 0.45, rstW = u * 2.3;
    let px0 = W - pad - (pills.length * pw + (pills.length - 1) * pgap + pgap + rstW);
    const py0 = topY + u * 0.25;
    for (const pl of pills) {
      const on = this.layers[pl.key];
      T.panel(g, px0, py0, pw, ph, u * 0.45, on ? (kur ? 'rgba(229,38,47,.22)' : 'rgba(62,230,255,.16)') : 'rgba(8,14,20,.6)', on ? pl.col : cLine + '0.24)');
      T.txt(g, pl.label, px0 + pw / 2, py0 + ph * 0.68, u * 0.68, on ? cHot : cDim, 'm', 'center');
      T.hits.rect(px0, py0 - 4, pw, ph + 8, { layer: pl.key });
      px0 += pw + pgap;
    }
    T.panel(g, px0, py0, rstW, ph, u * 0.45, 'rgba(8,14,20,.65)', cLine + '0.45)');
    T.txt(g, '◇', px0 + rstW / 2, py0 + ph * 0.70, u * 0.9, cHot, 'm', 'center');
    T.hits.rect(px0, py0 - 4, rstW, ph + 8, { reset: true });

    const sy0 = H - pad - u * 3.8;
    const stats = [
      ['BLE · 近距離', nBle, cAcc, this.layers.ble],
      ['WI-FI · 網', nWf, cHot, this.layers.wifi],
      ['433 · 感知', nSn, cGrn, this.layers.sens],
      [T.tr('AUDIO') + ' · 音', Math.round((S.energy || 0) * 100) + '%', cAlt, this.layers.audio],
    ];
    stats.forEach(([k, v, col, on], i) => {
      const bx = pad + i * u * 8.1;
      g.globalAlpha = on ? 1 : 0.42;
      T.panel(g, bx, sy0, u * 7.5, u * 3.45, u * 0.55, kur ? 'rgba(18,18,18,.78)' : 'rgba(5,12,18,.74)', cLine + '0.34)');
      T.txt(g, k, bx + u * 0.55, sy0 + u * 1.1, u * 0.62, cDim, 'k', 'left');
      T.txt(g, String(v), bx + u * 0.55, sy0 + u * 2.88, u * 1.5, col, 't', 'left');
      g.globalAlpha = 1;
    });

    if (this.pulseT < 2.4 && this.pulseMsg) {
      const al = Math.min(1, (2.4 - this.pulseT) * 1.2);
      g.globalAlpha = al;
      const bw = Math.min(W * 0.6, u * 26), bh = u * 2.6, bx = (W - bw) / 2, by = topY + u * 0.2;
      T.panel(g, bx, by, bw, bh, u * 0.6, kur ? 'rgba(229,38,47,.92)' : 'rgba(255,122,26,.22)', cAlt);
      T.txt(g, '⚡ ' + T.tr(this.pulseMsg), W / 2, by + bh * 0.68, u * 1.0, kur ? '#fff' : cHot, 't', 'center');
      g.globalAlpha = 1;
    }

    if (this.sel) {
      const cw = Math.min(W * 0.42, u * 21.5), ch = u * 6.6, cx0 = W - pad - cw, cy0 = H - pad - ch;
      const ac = this.sel.alert ? cAlt : cAcc;
      T.panel(g, cx0, cy0, cw, ch, u * 0.8, kur ? 'rgba(14,14,14,.94)' : 'rgba(6,16,24,.92)', ac);
      T.brackets(g, cx0 + 3, cy0 + 3, cw - 6, ch - 6, u * 0.45, ac, 1);
      T.drawSealChar(g, cx0 + cw - u * 2.3, cy0 + u * 0.65, u * 1.65, this.sel.alert ? '警' : '探', ac, !!this.sel.alert);
      T.txt(g, T.fit(g, this.sel.label, cw - u * 3.4), cx0 + u * 0.75, cy0 + u * 1.75, u * 1.15, cHot, 't', 'left');
      T.txt(g, this.sel.sub, cx0 + u * 0.75, cy0 + u * 3.2, u * 0.78, ac, 'm', 'left');
      const btnY = cy0 + u * 4.15, btnW = u * 10.6, btnH = u * 1.75;
      T.panel(g, cx0 + u * 0.75, btnY, btnW, btnH, u * 0.4, cLine + '0.2)', cAcc);
      T.txt(g, T.tr('OPEN SECTION') + ' →', cx0 + u * 0.75 + btnW / 2, btnY + btnH * 0.7, u * 0.7, cHot, 'm', 'center');
      T.hits.rect(cx0 + u * 0.75, btnY, btnW, btnH, { go: this.sel.go });
    }
  },
};
})();
