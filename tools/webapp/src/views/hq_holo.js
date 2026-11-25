// ---- HQ > HOLO 3D: unified 3D RF hologram (BLE + Wi-Fi + 433 + Sound + IR/NFC shockwaves) ----
// Interactive 360° orbit dome: drag to rotate, tap any floating node to lock target.
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
  col += acc * exp(-r * 4.2) * (0.20 + energy * 0.28);
  // audio-reactive concentric energy ripples
  float ring = sin(r * 34.0 - t * 3.2) * 0.5 + 0.5;
  col += acc * pow(ring, 6.0) * exp(-r * 2.8) * (0.06 + energy * 0.16);
  // rotating radar sweep glow
  float da = mod(sweep - ang + 6.2831853, 6.2831853);
  float sw = exp(-da * 3.5) * smoothstep(0.62, 0.02, r);
  col += acc * sw * 0.18;
  // shockwave ring on IR / NFC / tracker / 433 burst
  if (pulseT > 0.0 && pulseT < 2.2) {
    float pr = pulseT * 0.42;
    float w = exp(-abs(r - pr) * 28.0) * (1.0 - pulseT / 2.2);
    col += mix(acc, hot, pulse) * w * 0.65;
  }
  // vignette
  col *= smoothstep(1.15, 0.25, r);
  gl_FragColor = vec4(col, 1.0);
}`;

let glProg = null;
const angFor = id => (T.hash(id) % 3600) * (T.TAU / 3600);

T.hqHolo = {
  init(hq) {
    this.hq = hq;
    this.cam = T.orbit(0.65, 0.88, 3.05);
    this.drag = T.drag(hq.cv.cv, this.cam, { minPitch: 0.25, maxPitch: 1.38 });
    this.dust = T.dust(180, 1.85, 1.25);
    this.sel = null;                         // locked target { kind, id, label, sub, rssi, go }
    this.pulseT = 9.0; this.pulseHot = 0; this.pulseMsg = '';
    this.lastIr = 0; this.lastNfc = 0; this.lastBurst = 0; this.lastTrk = 0;
    this.drag.onTap = (x, y) => {
      if (hq.sub !== 1) return;
      const h = T.hits.at(x, y);
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

    this.watchEvents(now);
    this.pulseT = Math.min(9.0, this.pulseT + dt);
    this.drag.tick(dt);
    if (this.drag.idle()) {
      this.cam.yaw += dt * (0.18 + Math.min(0.25, (S.energy || 0) * 0.2));
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
          t, sweep: -this.cam.yaw + t * 1.15, energy: S.energy || 0,
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
    for (let k = 1; k <= 4; k++) drawCircle3D(k * 0.38, 0, 64, cLine + (k === 4 ? '0.42)' : '0.16)'), k === 4 ? 1.5 : 1);
    // upper Wi-Fi orbital ring
    drawCircle3D(1.25, 0.72, 64, cLine + '0.14)', 1);
    // radial spokes
    g.strokeStyle = cLine + '0.12)'; g.lineWidth = 1;
    const o0 = this.cam.p(0, 0, 0);
    for (let i = 0; i < 8; i++) {
      const a = i * Math.PI / 4, q = this.cam.p(Math.cos(a) * 1.52, 0, Math.sin(a) * 1.52);
      g.beginPath(); g.moveTo(o0[0], o0[1]); g.lineTo(q[0], q[1]); g.stroke();
    }
    // subtle meridian dome arcs
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
    const specR = 0.42;
    g.lineWidth = Math.max(1.5, u * 0.14);
    for (let k = 0; k < 32; k++) {
      const a = (k / 32) * T.TAU + t * 0.15;
      const v = (S.bands && S.bands[k]) || (0.04 + 0.03 * Math.sin(t * 2.5 + k * 0.5));
      const bh = 0.03 + v * 0.48;
      const x = Math.cos(a) * specR, z = Math.sin(a) * specR;
      const p0 = this.cam.p(x, 0, z), p1 = this.cam.p(x, bh, z);
      g.strokeStyle = v > 0.65 ? cAlt : v > 0.3 ? cHot : cAcc;
      g.globalAlpha = 0.45 + v * 0.55;
      g.beginPath(); g.moveTo(p0[0], p0[1]); g.lineTo(p1[0], p1[1]); g.stroke();
    }
    g.globalAlpha = 1;

    // 4. Center Holographic TENGEN Core (rotating 3D octahedron + gyro rings)
    const coreY = 0.28 + Math.sin(t * 1.8) * 0.03;
    const cs = 0.14 + (S.energy || 0) * 0.035;
    const verts = [[0, cs * 1.4, 0], [0, -cs * 1.4, 0], [cs, 0, 0], [-cs, 0, 0], [0, 0, cs], [0, 0, -cs]];
    const cr = Math.cos(t * 0.9), sr = Math.sin(t * 0.9);
    const pv = verts.map(([x, y, z]) => this.cam.p(x * cr - z * sr, coreY + y, x * sr + z * cr));
    const edges = [[0, 2], [0, 3], [0, 4], [0, 5], [1, 2], [1, 3], [1, 4], [1, 5], [2, 4], [4, 3], [3, 5], [5, 2]];
    g.strokeStyle = cHot; g.lineWidth = 1.4;
    g.beginPath();
    for (const [a, b] of edges) { g.moveTo(pv[a][0], pv[a][1]); g.lineTo(pv[b][0], pv[b][1]); }
    g.stroke();
    drawCircle3D(0.24, coreY, 36, cLine + '0.55)', 1.2);

    // 5. Collect all 3D RF objects (BLE + Wi-Fi + 433 sensors) and depth-sort back-to-front
    const items = [];
    for (const e of S.dev.values()) {
      if (!e.alive) continue;
      const p = T.pct(e.rs), r = 0.48 + (1 - p) * 0.98, a = angFor(e.id);
      const y = 0.14 + p * 0.42;
      const trk = S.isTrk(e), nw = S.isNew(e, now);
      items.push({
        kind: 'ble', id: e.id, x: Math.cos(a) * r, y, z: Math.sin(a) * r,
        rssi: e.rs, alert: trk || nw, trk,
        col: trk || nw ? cAlt : p > 0.6 ? cHot : cAcc,
        label: e.label || e.name || e.vend || e.type || 'BLE',
        sub: (e.type || 'device').toUpperCase() + ' · ' + Math.round(e.rs) + ' dBm',
        go: 'ble', prio: (trk ? 100 : nw ? 80 : 0) + p * 50,
      });
    }
    for (const a of S.aps.values()) {
      if (!a.alive) continue;
      const p = T.pct(a.rs), r = 0.42 + (1 - p) * 0.96, ang = angFor(a.b || a.s);
      const y = 0.62 + ((a.c || 6) / 13) * 0.44;
      items.push({
        kind: 'wifi', id: a.b || a.s, x: Math.cos(ang) * r, y, z: Math.sin(ang) * r,
        rssi: a.rs, alert: a.e === 'open',
        col: a.e === 'open' ? cAlt : cHot,
        label: a.s || '<hidden>',
        sub: 'WI-FI CH ' + (a.c || '?') + ' · ' + Math.round(a.rs) + ' dBm',
        go: 'wifi', prio: 10 + p * 40,
      });
    }
    const sens = (S.sens && S.sens.s) || [];
    for (const s of sens) {
      const ang = angFor('433_' + s.id + '_' + s.m), r = 1.38, y = 0.34;
      const tStr = s.t != null ? (s.t > 0 ? '+' : '') + Number(s.t).toFixed(1) + '°C' : '433';
      items.push({
        kind: 'sens', id: s.id, x: Math.cos(ang) * r, y, z: Math.sin(ang) * r,
        rssi: s.r || -70, alert: false, col: cGrn,
        label: tStr + (s.h != null ? ' ' + s.h + '%' : ''),
        sub: '433 MHZ · ' + (s.m || 'SENSOR'),
        go: 'sub', prio: 70,
      });
    }

    // project & sort back-to-front
    for (const it of items) {
      it.p = this.cam.p(it.x, it.y, it.z);
      it.pb = this.cam.p(it.x, 0, it.z);
    }
    items.sort((a, b) => b.p[3] - a.p[3]);

    // top labels budget (avoid clutter)
    const labeled = new Set(items.slice().sort((a, b) => b.prio - a.prio).slice(0, 8));

    // draw stems + 3D nodes
    for (const it of items) {
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
    T.txt(g, '天眼 · ' + T.tr('HOLO-DOME 3D'), pad, pad + u * 1.2, u * 1.45, cHot, 't', 'left');
    T.txt(g, T.tr('DRAG TO ORBIT · TAP NODE'), pad, pad + u * 2.5, u * 0.72, cDim, 'm', 'left');

    const nBle = items.filter(x => x.kind === 'ble').length;
    const nWf = items.filter(x => x.kind === 'wifi').length;
    const nSn = items.filter(x => x.kind === 'sens').length;
    const sy0 = H - pad - u * 3.8;
    const stats = [
      ['BLE', nBle, cAcc],
      ['WI-FI', nWf, cHot],
      ['433', nSn, cGrn],
      ['AUDIO', Math.round((S.energy || 0) * 100) + '%', cAlt],
    ];
    stats.forEach(([k, v, col], i) => {
      const bx = pad + i * u * 7.8;
      g.fillStyle = kur ? 'rgba(18,18,18,.72)' : 'rgba(5,12,18,.68)';
      g.strokeStyle = cLine + '0.32)'; g.lineWidth = 1;
      g.fillRect(bx, sy0, u * 7.1, u * 3.4); g.strokeRect(bx, sy0, u * 7.1, u * 3.4);
      T.txt(g, k, bx + u * 0.5, sy0 + u * 1.1, u * 0.66, cDim, 'm', 'left');
      T.txt(g, String(v), bx + u * 0.5, sy0 + u * 2.85, u * 1.5, col, 't', 'left');
    });

    // shockwave event banner
    if (this.pulseT < 2.4 && this.pulseMsg) {
      const al = Math.min(1, (2.4 - this.pulseT) * 1.2);
      g.globalAlpha = al;
      const bw = Math.min(W * 0.6, u * 26), bh = u * 2.6, bx = (W - bw) / 2, by = pad + u * 0.4;
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
