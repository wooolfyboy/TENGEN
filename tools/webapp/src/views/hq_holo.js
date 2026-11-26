// ---- HQ > HOLO 3D: unified 3D RF hologram (BLE + Wi-Fi + 433 + Sound + IR/NFC shockwaves) ----
// Interactive 360° orbit dome: drag to rotate, tap any floating node to lock target,
// toggle RF layers in top-right HUD, watch energy arcs & motion trails live.
(() => {
const GL_FS = `
uniform vec2 res;
uniform float t, sweep, energy, pulse, pulseT, kurenai;
uniform vec2 center;
void main() {
  vec2 uv = (gl_FragCoord.xy - center) / min(res.x, res.y);
  float r = length(uv);
  float ang = atan(uv.y, uv.x);
  vec3 bg = kurenai > 0.5 ? vec3(0.043, 0.043, 0.043) : vec3(0.015, 0.025, 0.04);
  vec3 acc = kurenai > 0.5 ? vec3(0.90, 0.15, 0.18) : vec3(0.24, 0.90, 1.0);
  vec3 hot = kurenai > 0.5 ? vec3(0.96, 0.94, 0.92) : vec3(1.0, 0.55, 0.10);
  vec3 col = bg;
  // subtle volumetric core aura
  col += acc * exp(-r * 4.0) * (0.22 + energy * 0.32);
  // audio-reactive concentric energy ripples
  float ring = sin(r * 34.0 - t * 3.2) * 0.5 + 0.5;
  col += acc * pow(ring, 6.0) * exp(-r * 2.8) * (0.06 + energy * 0.18);
  // rotating radar sweep glow
  float da = mod(sweep - ang + 6.2831853, 6.2831853);
  float sw = exp(-da * 3.5) * smoothstep(0.64, 0.02, r);
  col += acc * sw * 0.20;
  // shockwave ring on IR / NFC / tracker / 433 burst
  if (pulseT > 0.0 && pulseT < 2.2) {
    float pr = pulseT * 0.44;
    float w = exp(-abs(r - pr) * 28.0) * (1.0 - pulseT / 2.2);
    col += mix(acc, hot, pulse) * w * 0.7;
  }
  // vignette
  col *= smoothstep(1.18, 0.25, r);
  gl_FragColor = vec4(col, 1.0);
}`;

let glProg = null;
const angFor = id => (T.hash(id) % 3600) * (T.TAU / 3600);
const TRAIL_MAX = 14;

T.hqHolo = {
  init(hq) {
    this.hq = hq;
    this.cam = T.orbit(0.65, 0.86, 3.0);
    this.drag = T.drag(hq.cv.cv, this.cam, { minPitch: 0.25, maxPitch: 1.38 });
    this.dust = T.dust(200, 1.9, 1.25);
    this.sel = null;                         // locked target { kind, id, label, sub, rssi, go }
    this.layers = { ble: true, wifi: true, sens: true, audio: true };
    this.pos = new Map();                    // smoothed 3D positions per node key
    this.trails = new Map();                 // recent 3D trail points per node key
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
      this.cam.pitch += (0.86 - this.cam.pitch) * k;
      this.cam.D += (3.0 - this.cam.D) * k;
    } else if (this.drag.idle()) {
      this.cam.yaw += dt * (0.16 + Math.min(0.24, (S.energy || 0) * 0.2));
      this.cam.pitch += (0.82 + Math.sin(t * 0.25) * 0.12 - this.cam.pitch) * T.ease(dt, 2.5);
    }
    this.dust.tick(dt);
    T.hits.clear();

    const cx = W * 0.5, cy = H * 0.54;
    const F = Math.min(W, H) * 0.78;
    this.cam.frame(cx, cy, F);

    // 1. WebGL atmospheric backdrop + radar sweep + shockwave
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
      } else {
        T.backdrop(g, W, H, 0.5, 0.54);
      }
    } else {
      T.backdrop(g, W, H, 0.5, 0.54);
    }

    // 2. 3D floor rings, crosshairs & dome latitude/longitude arcs
    const drawCircle3D = (r, y, segs, col, lw = 1) => {
      g.strokeStyle = col; g.lineWidth = lw; g.beginPath();
      for (let i = 0; i <= segs; i++) {
        const a = (i / segs) * T.TAU, p = this.cam.p(Math.cos(a) * r, y, Math.sin(a) * r);
        i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]);
      }
      g.stroke();
    };
    const drawTiltedRing3D = (r, cy0, tiltX, rotY, segs, col, lw = 1) => {
      g.strokeStyle = col; g.lineWidth = lw; g.beginPath();
      const ct = Math.cos(tiltX), st = Math.sin(tiltX), cr = Math.cos(rotY), sr = Math.sin(rotY);
      for (let i = 0; i <= segs; i++) {
        const a = (i / segs) * T.TAU;
        const x0 = Math.cos(a) * r, z0 = Math.sin(a) * r;
        const y1 = -z0 * st, z1 = z0 * ct;
        const p = this.cam.p(x0 * cr - z1 * sr, cy0 + y1, x0 * sr + z1 * cr);
        i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]);
      }
      g.stroke();
    };
    for (let k = 1; k <= 4; k++) drawCircle3D(k * 0.38, 0, 64, cLine + (k === 4 ? '0.42)' : '0.16)'), k === 4 ? 1.5 : 1);
    if (this.layers.wifi) drawCircle3D(1.25, 0.72, 64, cLine + '0.15)', 1);
    g.strokeStyle = cLine + '0.12)'; g.lineWidth = 1;
    const o0 = this.cam.p(0, 0, 0);
    for (let i = 0; i < 8; i++) {
      const a = i * Math.PI / 4, q = this.cam.p(Math.cos(a) * 1.52, 0, Math.sin(a) * 1.52);
      g.beginPath(); g.moveTo(o0[0], o0[1]); g.lineTo(q[0], q[1]); g.stroke();
    }
    g.strokeStyle = cLine + '0.09)';
    for (let m = 0; m < 4; m++) {
      const phi = m * Math.PI / 4;
      g.beginPath();
      for (let i = 0; i <= 28; i++) {
        const th = (i / 28) * Math.PI;
        const rr = Math.sin(th) * 1.52, yy = Math.cos(th) * 1.15;
        if (yy < 0) continue;
        const p = this.cam.p(Math.cos(phi) * rr, yy, Math.sin(phi) * rr);
        i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]);
      }
      g.stroke();
    }

    // 3. 3D Audio Spectrum Ring around core (32 bars)
    if (this.layers.audio) {
      const specR = 0.42;
      g.lineWidth = Math.max(1.5, u * 0.14);
      for (let k = 0; k < 32; k++) {
        const a = (k / 32) * T.TAU + t * 0.15;
        const v = (S.bands && S.bands[k]) || (0.04 + 0.03 * Math.sin(t * 2.5 + k * 0.5));
        const bh = 0.03 + v * 0.50;
        const x = Math.cos(a) * specR, z = Math.sin(a) * specR;
        const p0 = this.cam.p(x, 0, z), p1 = this.cam.p(x, bh, z);
        g.strokeStyle = v > 0.65 ? cAlt : v > 0.3 ? cHot : cAcc;
        g.globalAlpha = 0.45 + v * 0.55;
        g.beginPath(); g.moveTo(p0[0], p0[1]); g.lineTo(p1[0], p1[1]); g.stroke();
      }
      g.globalAlpha = 1;
    }

    // 4. Center Holographic TENGEN Core (rotating 3D octahedron + dual counter-rotating gyro rings)
    const coreY = 0.28 + Math.sin(t * 1.8) * 0.03;
    const cs = 0.14 + (this.layers.audio ? (S.energy || 0) * 0.04 : 0);
    const verts = [[0, cs * 1.45, 0], [0, -cs * 1.45, 0], [cs, 0, 0], [-cs, 0, 0], [0, 0, cs], [0, 0, -cs]];
    const cr = Math.cos(t * 0.9), sr = Math.sin(t * 0.9);
    const pv = verts.map(([x, y, z]) => this.cam.p(x * cr - z * sr, coreY + y, x * sr + z * cr));
    const edges = [[0, 2], [0, 3], [0, 4], [0, 5], [1, 2], [1, 3], [1, 4], [1, 5], [2, 4], [4, 3], [3, 5], [5, 2]];
    const coreP = this.cam.p(0, coreY, 0);
    // glowing core heart
    const cg = g.createRadialGradient(coreP[0], coreP[1], 1, coreP[0], coreP[1], Math.max(12, u * 2.2));
    cg.addColorStop(0, cHot); cg.addColorStop(0.35, cLine + '0.45)'); cg.addColorStop(1, cLine + '0)');
    g.fillStyle = cg; g.beginPath(); g.arc(coreP[0], coreP[1], Math.max(12, u * 2.2), 0, T.TAU); g.fill();
    g.strokeStyle = cHot; g.lineWidth = 1.4;
    g.beginPath();
    for (const [a, b] of edges) { g.moveTo(pv[a][0], pv[a][1]); g.lineTo(pv[b][0], pv[b][1]); }
    g.stroke();
    drawTiltedRing3D(0.24, coreY, 0.35, t * 0.7, 36, cLine + '0.62)', 1.2);
    drawTiltedRing3D(0.30, coreY, -0.52, -t * 0.45, 36, cLine + '0.32)', 1.0);

    // 5. Collect 3D RF objects (BLE + Wi-Fi + 433 sensors) with smooth interpolation & motion trails
    const rawItems = [];
    let nBle = 0, nWf = 0, nSn = 0;
    for (const e of S.dev.values()) {
      if (!e.alive) continue;
      nBle++;
      if (!this.layers.ble) continue;
      const p = T.pct(e.rs), r = 0.48 + (1 - p) * 0.98, a = angFor(e.id);
      const y = 0.14 + p * 0.42;
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
      const p = T.pct(a.rs), r = 0.42 + (1 - p) * 0.96, ang = angFor(a.b || a.s);
      const y = 0.62 + ((a.c || 6) / 13) * 0.44;
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

    // Smooth positions & record motion trails at ~10 Hz (silky 1.1s time constant)
    this.trailTick += dt;
    const sampleTrail = this.trailTick >= 0.1;
    if (sampleTrail) this.trailTick = 0;
    const smoothK = T.ease(dt, 1.1);
    const activeKeys = new Set();
    const items = [];
    for (const it of rawItems) {
      activeKeys.add(it.key);
      let sp = this.pos.get(it.key);
      if (!sp) { sp = [it.tx, it.ty, it.tz]; this.pos.set(it.key, sp); }
      else {
        sp[0] += (it.tx - sp[0]) * smoothK;
        sp[1] += (it.ty - sp[1]) * smoothK;
        sp[2] += (it.tz - sp[2]) * smoothK;
      }
      it.x = sp[0]; it.y = sp[1]; it.z = sp[2];
      if (sampleTrail) {
        let tr = this.trails.get(it.key);
        if (!tr) { tr = []; this.trails.set(it.key, tr); }
        const last = tr[tr.length - 1];
        if (!last || Math.hypot(sp[0] - last[0], sp[1] - last[1], sp[2] - last[2]) > 0.012) {
          tr.push([sp[0], sp[1], sp[2]]);
          if (tr.length > TRAIL_MAX) tr.shift();
        } else if (tr.length > 1) {
          tr.shift(); // fade idle trails gracefully
        }
      }
      it.p = this.cam.p(it.x, it.y, it.z);
      it.pb = this.cam.p(it.x, 0, it.z);
      items.push(it);
    }
    if (sampleTrail) {
      for (const k of this.pos.keys()) if (!activeKeys.has(k)) { this.pos.delete(k); this.trails.delete(k); }
    }

    // 5a. Animated 3D Energy Arcs + traveling data packets to top 6 closest/alert nodes
    const arcNodes = items.slice().sort((a, b) => b.prio - a.prio).slice(0, 6);
    for (let idx = 0; idx < arcNodes.length; idx++) {
      const it = arcNodes[idx];
      const mx = it.x * 0.5, mz = it.z * 0.5, my = Math.max(coreY, it.y) + 0.28 + (idx % 2) * 0.08;
      const segs = 16;
      g.strokeStyle = it.alert ? cAltA + '0.42)' : cLine + '0.26)';
      g.lineWidth = it.alert ? 1.5 : 1.1;
      g.beginPath();
      for (let s = 0; s <= segs; s++) {
        const k = s / segs, ik = 1 - k;
        const ax = ik * ik * it.x + 2 * ik * k * mx;
        const ay = ik * ik * it.y + 2 * ik * k * my + k * k * coreY;
        const az = ik * ik * it.z + 2 * ik * k * mz;
        const pp = this.cam.p(ax, ay, az);
        s ? g.lineTo(pp[0], pp[1]) : g.moveTo(pp[0], pp[1]);
      }
      g.stroke();
      // traveling data packet spark (node -> core)
      const spd = it.alert ? 0.72 : 0.42 + it.pct * 0.25;
      const pk = (t * spd + idx * 0.27) % 1;
      const ipk = 1 - pk;
      const px = ipk * ipk * it.x + 2 * ipk * pk * mx;
      const py = ipk * ipk * it.y + 2 * ipk * pk * my + pk * pk * coreY;
      const pz = ipk * ipk * it.z + 2 * ipk * pk * mz;
      const sp = this.cam.p(px, py, pz);
      g.fillStyle = it.alert ? cAlt : cHot;
      g.beginPath(); g.arc(sp[0], sp[1], it.alert ? 3.2 : 2.4, 0, T.TAU); g.fill();
    }

    // 5b. Depth-sort back-to-front & draw motion trails + stems + nodes
    items.sort((a, b) => b.p[3] - a.p[3]);
    const labeled = new Set(items.slice().sort((a, b) => b.prio - a.prio).slice(0, 8));

    for (const it of items) {
      // glowing motion trail
      const tr = this.trails.get(it.key);
      if (tr && tr.length > 1) {
        for (let k = 1; k < tr.length; k++) {
          const pA = this.cam.p(tr[k - 1][0], tr[k - 1][1], tr[k - 1][2]);
          const pB = this.cam.p(tr[k][0], tr[k][1], tr[k][2]);
          g.strokeStyle = it.col;
          g.globalAlpha = (k / tr.length) * 0.42;
          g.lineWidth = Math.max(1, (k / tr.length) * 2.4);
          g.beginPath(); g.moveTo(pA[0], pA[1]); g.lineTo(pB[0], pB[1]); g.stroke();
        }
        g.globalAlpha = 1;
      }

      const [sx, sy, sc] = it.p, [bx, by] = it.pb;
      const rad = Math.max(3, Math.min(11, sc * 0.011 * (it.alert ? 1.45 : 1)));
      // vertical drop beam to floor
      g.strokeStyle = it.col; g.globalAlpha = it.alert ? 0.55 : 0.22; g.lineWidth = 1;
      g.beginPath(); g.moveTo(bx, by); g.lineTo(sx, sy); g.stroke();
      g.beginPath(); g.arc(bx, by, Math.max(1.5, rad * 0.4), 0, T.TAU); g.fillStyle = it.col; g.fill();
      g.globalAlpha = 1;

      // shape by source: circle = BLE, diamond = Wi-Fi, triangle = 433
      g.fillStyle = it.col; g.strokeStyle = it.col;
      if (it.kind === 'ble') {
        g.beginPath(); g.arc(sx, sy, rad, 0, T.TAU); g.fill();
      } else if (it.kind === 'wifi') {
        g.beginPath(); g.moveTo(sx, sy - rad * 1.25); g.lineTo(sx + rad * 1.1, sy); g.lineTo(sx, sy + rad * 1.25); g.lineTo(sx - rad * 1.1, sy); g.closePath();
        g.lineWidth = 1.6; g.stroke();
        g.globalAlpha = 0.25; g.fill(); g.globalAlpha = 1;
      } else {
        g.beginPath(); g.moveTo(sx, sy - rad * 1.3); g.lineTo(sx + rad * 1.2, sy + rad); g.lineTo(sx - rad * 1.2, sy + rad); g.closePath(); g.fill();
      }

      // alert / selected rotating reticle
      const isSel = this.sel && this.sel.id === it.id && this.sel.kind === it.kind;
      if (it.alert || isSel) {
        const rr = rad * (isSel ? 2.5 : 2.1 + 0.35 * Math.sin(t * 6));
        g.strokeStyle = isSel ? cHot : cAlt; g.lineWidth = isSel ? 1.8 : 1.2;
        g.beginPath(); g.arc(sx, sy, rr, t * 2, t * 2 + Math.PI * 1.4); g.stroke();
      }

      if (labeled.has(it) || isSel) {
        T.txt(g, T.fit(g, it.label, u * 12), sx + rad + 5, sy + 3, Math.max(10, u * 0.72), isSel ? cHot : it.col, 'm', 'left');
      }
      T.hits.circle(sx, sy, Math.max(14, rad * 2.2), { node: it });
    }

    // 6. 3D floating dust + optical bloom
    this.dust.draw(g, this.cam);
    T.bloom(this.hq.cv, 0.65);

    // 7. Tactical Corner HUD (minimal, translucent, non-intrusive)
    const pad = u * 1.1;
    const topY = pad; // canvas is already translated below #subs by hq.js
    T.txt(g, '天眼 · ' + T.tr('HOLO-DOME 3D'), pad, topY + u * 1.2, u * 1.45, cHot, 't', 'left');
    T.txt(g, T.tr('DRAG TO ORBIT · TAP NODE'), pad, topY + u * 2.5, u * 0.72, cDim, 'm', 'left');

    // Top-right layer filter pills + camera reset button
    const pills = [
      { key: 'ble', label: 'BLE', col: cAcc },
      { key: 'wifi', label: 'WI-FI', col: cHot },
      { key: 'sens', label: '433', col: cGrn },
      { key: 'audio', label: T.tr('AUDIO'), col: cAlt },
    ];
    const pw = u * 4.6, ph = u * 1.85, pgap = u * 0.45, rstW = u * 2.2;
    let px0 = W - pad - (pills.length * pw + (pills.length - 1) * pgap + pgap + rstW);
    const py0 = topY + u * 0.25;
    for (const pl of pills) {
      const on = this.layers[pl.key];
      g.fillStyle = on ? (kur ? 'rgba(229,38,47,.18)' : 'rgba(62,230,255,.14)') : 'rgba(8,14,20,.55)';
      g.strokeStyle = on ? pl.col : cLine + '0.24)'; g.lineWidth = on ? 1.2 : 1;
      g.fillRect(px0, py0, pw, ph); g.strokeRect(px0, py0, pw, ph);
      T.txt(g, pl.label, px0 + pw / 2, py0 + ph * 0.68, u * 0.68, on ? cHot : cDim, 'm', 'center');
      T.hits.rect(px0, py0 - 4, pw, ph + 8, { layer: pl.key });
      px0 += pw + pgap;
    }
    // camera reset button
    g.fillStyle = 'rgba(8,14,20,.65)'; g.strokeStyle = cLine + '0.45)'; g.lineWidth = 1;
    g.fillRect(px0, py0, rstW, ph); g.strokeRect(px0, py0, rstW, ph);
    T.txt(g, '⌖', px0 + rstW / 2, py0 + ph * 0.72, u * 0.95, cHot, 'm', 'center');
    T.hits.rect(px0, py0 - 4, rstW, ph + 8, { reset: true });

    // Bottom-left live telemetry tiles
    const sy0 = H - pad - u * 3.8;
    const stats = [
      ['BLE', nBle, cAcc, this.layers.ble],
      ['WI-FI', nWf, cHot, this.layers.wifi],
      ['433', nSn, cGrn, this.layers.sens],
      [T.tr('AUDIO'), Math.round((S.energy || 0) * 100) + '%', cAlt, this.layers.audio],
    ];
    stats.forEach(([k, v, col, on], i) => {
      const bx = pad + i * u * 7.8;
      g.globalAlpha = on ? 1 : 0.42;
      g.fillStyle = kur ? 'rgba(18,18,18,.72)' : 'rgba(5,12,18,.68)';
      g.strokeStyle = cLine + '0.32)'; g.lineWidth = 1;
      g.fillRect(bx, sy0, u * 7.1, u * 3.4); g.strokeRect(bx, sy0, u * 7.1, u * 3.4);
      T.txt(g, k, bx + u * 0.5, sy0 + u * 1.1, u * 0.66, cDim, 'm', 'left');
      T.txt(g, String(v), bx + u * 0.5, sy0 + u * 2.85, u * 1.5, col, 't', 'left');
      g.globalAlpha = 1;
    });

    // shockwave event banner
    if (this.pulseT < 2.4 && this.pulseMsg) {
      const al = Math.min(1, (2.4 - this.pulseT) * 1.2);
      g.globalAlpha = al;
      const bw = Math.min(W * 0.6, u * 26), bh = u * 2.6, bx = (W - bw) / 2, by = topY + u * 0.2;
      g.fillStyle = kur ? 'rgba(229,38,47,.9)' : 'rgba(255,122,26,.18)';
      g.strokeStyle = cAlt; g.lineWidth = 1.5;
      g.fillRect(bx, by, bw, bh); g.strokeRect(bx, by, bw, bh);
      T.txt(g, '⚡ ' + T.tr(this.pulseMsg), W / 2, by + bh * 0.68, u * 1.0, kur ? '#fff' : cHot, 't', 'center');
      g.globalAlpha = 1;
    }

    // locked target callout card (bottom-right)
    if (this.sel) {
      const cw = Math.min(W * 0.42, u * 21), ch = u * 6.4, cx0 = W - pad - cw, cy0 = H - pad - ch;
      g.fillStyle = kur ? 'rgba(14,14,14,.92)' : 'rgba(6,16,24,.88)';
      g.strokeStyle = this.sel.alert ? cAlt : cAcc; g.lineWidth = 1.5;
      g.fillRect(cx0, cy0, cw, ch); g.strokeRect(cx0, cy0, cw, ch);
      T.txt(g, T.fit(g, this.sel.label, cw - u * 1.4), cx0 + u * 0.7, cy0 + u * 1.7, u * 1.15, cHot, 't', 'left');
      T.txt(g, this.sel.sub, cx0 + u * 0.7, cy0 + u * 3.2, u * 0.78, this.sel.alert ? cAlt : cAcc, 'm', 'left');
      const btnY = cy0 + u * 4.1, btnW = u * 10, btnH = u * 1.7;
      g.fillStyle = cLine + '0.2)'; g.fillRect(cx0 + u * 0.7, btnY, btnW, btnH);
      g.strokeStyle = cAcc; g.strokeRect(cx0 + u * 0.7, btnY, btnW, btnH);
      T.txt(g, T.tr('OPEN SECTION') + ' →', cx0 + u * 0.7 + btnW / 2, btnY + btnH * 0.7, u * 0.7, cHot, 'm', 'center');
      T.hits.rect(cx0 + u * 0.7, btnY, btnW, btnH, { go: this.sel.go });
    }
  },
};
})();
