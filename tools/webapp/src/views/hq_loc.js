// ---- HQ > LOCATOR 3D: Dedicated 2-Stick Stereo BLE Triangulation Scene ----
// Activates conflict-free Locator mode on T-Embed (locks Channel 6 ESP-NOW + fast BLE).
// Renders TENGEN #1 and M5StickC Plus EAR #2 in 3D room space with dual-laser locks,
// confidence circles, motion trails, baseline distance adjustment, and 1-tap calibration.
(() => {
const TRAIL_MAX = 14;
const rssiToM = r => Math.max(0.25, Math.min(14, Math.pow(10, (-58 - r) / 22)));

T.hqLoc = {
  init(hq) {
    this.hq = hq;
    this.cam = T.orbit(0.0, 0.88, 3.25);
    this.drag = T.drag(hq.cv.cv, this.cam, { minPitch: 0.28, maxPitch: 1.42 });
    this.dust = T.dust(140, 1.9, 1.2);
    this.sel = null;
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
      if (h.act === 'rst')  { this.resetAnim = 1.0; this.sel = null; return; }
      if (h.node)           { this.sel = h.node; return; }
      this.sel = null;
    };
  },

  toast(msg, bad = false) {
    this.toastMsg = msg;
    this.toastBad = bad;
    this.toastT = 2.8;
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
    fetch(T.apiBase() + '/api/loc2x?a=cal', { cache: 'no-store' })
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
      const k = T.ease(dt, 0.14);
      this.cam.yaw += (0.0 - this.cam.yaw) * k;
      this.cam.pitch += (0.88 - this.cam.pitch) * k;
      this.cam.D += (3.25 - this.cam.D) * k;
    } else if (this.drag.idle()) {
      this.cam.yaw += Math.sin(t * 0.22) * dt * 0.08;
    }
    this.dust.tick(dt);
    T.hits.clear();

    const cx = W * 0.5, cy = H * 0.56;
    const F = Math.min(W, H) * 0.78;
    this.cam.frame(cx, cy, F);
    T.backdrop(g, W, H, 0.5, 0.56);

    const L = S.loc || { on: 1, base: 3.0, ofs: 6, ear: { ok: 0, bat: 0, rssi: -70 }, matched: 0, d: [] };
    const baseM = Math.max(1.0, +L.base || 3.0);
    const earOk = !!(L.ear && L.ear.ok);
    // Map physical room meters to 3D scene units: baseline sits at z0 = -0.55, room extends to +z
    const sc = 1.55 / baseM;
    const zWall = -0.58;
    const x1 = -baseM * 0.5 * sc, x2 = baseM * 0.5 * sc;

    // 1. 3D Room Floor Grid (in real meters) + range rings around both sticks
    const drawCircle3D = (cx0, cz0, r, segs, col, lw = 1) => {
      g.strokeStyle = col; g.lineWidth = lw; g.beginPath();
      for (let i = 0; i <= segs; i++) {
        const a = (i / segs) * T.TAU, p = this.cam.p(cx0 + Math.cos(a) * r, 0, cz0 + Math.sin(a) * r);
        i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]);
      }
      g.stroke();
    };

    const gridW = Math.ceil(baseM * 1.1);
    const gridD = Math.ceil(baseM * 1.6);
    g.strokeStyle = cLine + '0.11)'; g.lineWidth = 1; g.beginPath();
    for (let xm = -gridW; xm <= gridW; xm++) {
      const p0 = this.cam.p(xm * sc, 0, zWall - 0.25);
      const p1 = this.cam.p(xm * sc, 0, zWall + gridD * sc);
      g.moveTo(p0[0], p0[1]); g.lineTo(p1[0], p1[1]);
    }
    for (let zm = 0; zm <= gridD; zm++) {
      const p0 = this.cam.p(-gridW * sc, 0, zWall + zm * sc);
      const p1 = this.cam.p(gridW * sc, 0, zWall + zm * sc);
      g.moveTo(p0[0], p0[1]); g.lineTo(p1[0], p1[1]);
    }
    g.stroke();

    // Meter labels along depth axis
    for (let zm = 1; zm <= Math.min(6, gridD); zm++) {
      const pm = this.cam.p(-gridW * sc - 0.12, 0, zWall + zm * sc);
      T.txt(g, zm + 'm', pm[0], pm[1], Math.max(9, u * 0.62), cDim, 'm', 'right');
    }

    // Concentric range rings around Stick #1 and Stick #2
    for (let k = 1; k <= 3; k++) {
      const rm = (baseM * 0.45 * k) * sc;
      drawCircle3D(x1, zWall, rm, 48, cLine + (k === 2 ? '0.24)' : '0.13)'), 1);
      drawCircle3D(x2, zWall, rm, 48, earOk ? cAltA + (k === 2 ? '0.24)' : '0.13)') : cLine + '0.07)', 1);
    }

    // 2. Baseline Link Beam between TENGEN #1 and M5StickC Plus EAR #2
    const pSt1 = this.cam.p(x1, 0.06, zWall);
    const pSt2 = this.cam.p(x2, 0.06, zWall);
    g.strokeStyle = earOk ? cAcc : cLine + '0.3)';
    g.lineWidth = earOk ? 2.0 : 1.2;
    if (!earOk) g.setLineDash([6, 6]);
    g.beginPath(); g.moveTo(pSt1[0], pSt1[1]); g.lineTo(pSt2[0], pSt2[1]); g.stroke();
    g.setLineDash([]);

    if (earOk) {
      // Animated ESP-NOW data pulses flying from EAR #2 -> TENGEN #1
      for (let k = 0; k < 3; k++) {
        const f = (t * 0.85 + k / 3) % 1;
        const px = x2 + (x1 - x2) * f;
        const pp = this.cam.p(px, 0.06 + Math.sin(f * Math.PI) * 0.08, zWall);
        g.fillStyle = cHot;
        g.beginPath(); g.arc(pp[0], pp[1], 3.0, 0, T.TAU); g.fill();
      }
    }
    const pMid = this.cam.p(0, 0.02, zWall - 0.12);
    T.txt(g, `← ${baseM.toFixed(1)} ${T.tr('m')} →`, pMid[0], pMid[1], Math.max(10, u * 0.72), cHot, 'm', 'center');

    // 3. Draw Beacons: TENGEN #1 (left) and M5StickC Plus EAR #2 (right)
    const drawBeacon = (bx, bz, col, title, sub) => {
      const h = 0.22 + Math.sin(t * 2.2 + bx) * 0.02;
      const pb = this.cam.p(bx, 0, bz), pt = this.cam.p(bx, h, bz);
      drawCircle3D(bx, bz, 0.14, 24, col, 1.5);
      g.strokeStyle = col; g.lineWidth = 2;
      g.beginPath(); g.moveTo(pb[0], pb[1]); g.lineTo(pt[0], pt[1]); g.stroke();
      g.fillStyle = col;
      g.beginPath(); g.arc(pt[0], pt[1], Math.max(5, u * 0.48), 0, T.TAU); g.fill();
      T.txt(g, title, pt[0], pt[1] - u * 1.35, Math.max(11, u * 0.82), cHot, 't', 'center');
      T.txt(g, sub, pt[0], pt[1] - u * 0.55, Math.max(9, u * 0.64), col, 'm', 'center');
    };
    drawBeacon(x1, zWall, cAcc, 'TENGEN #1', 'CORE · CH 6');
    drawBeacon(x2, zWall, earOk ? cAlt : cDim, 'M5 EAR #2',
      earOk ? `BAT ${L.ear.bat}% · ${L.ear.rssi} dBm` : T.tr('WAITING...'));

    // 4. Process & Smooth Triangulated Devices
    this.trailTick += dt;
    const sampleTrail = this.trailTick >= 0.1;
    if (sampleTrail) this.trailTick = 0;
    const smoothK = T.ease(dt, 0.22);
    const activeKeys = new Set();
    const items = [];

    for (const row of (L.d || [])) {
      // [id, mask(1=stick1,2=stick2,3=both), r1, r2, x, z, err, type, vendor, name, label]
      const [id, mask, r1, r2, rx, rz, rerr, type, vend, name, label] = row;
      const key = String(id);
      activeKeys.add(key);
      const tx = rx * sc, tz = zWall + rz * sc, ty = 0.16 + T.pct(Math.max(r1, r2)) * 0.28;
      let sp = this.pos.get(key);
      if (!sp) { sp = [tx, ty, tz]; this.pos.set(key, sp); }
      else {
        sp[0] += (tx - sp[0]) * smoothK;
        sp[1] += (ty - sp[1]) * smoothK;
        sp[2] += (tz - sp[2]) * smoothK;
      }
      if (sampleTrail) {
        let tr = this.trails.get(key);
        if (!tr) { tr = []; this.trails.set(key, tr); }
        const last = tr[tr.length - 1];
        if (!last || Math.hypot(sp[0] - last[0], sp[2] - last[2]) > 0.012) {
          tr.push([sp[0], sp[1], sp[2]]);
          if (tr.length > TRAIL_MAX) tr.shift();
        } else if (tr.length > 1) tr.shift();
      }
      const both = (mask & 3) === 3;
      const trk = type === 'tracker';
      const col = trk ? cAlt : both ? cGrn : (mask & 1) ? cAcc : cHot;
      items.push({
        id, key, mask, both, r1, r2, rx, rz, rerr,
        x: sp[0], y: sp[1], z: sp[2], err3d: Math.max(0.08, rerr * sc * 0.45),
        type, vend, name, label: label || name || vend || type || 'BLE',
        col, alert: trk,
        p: this.cam.p(sp[0], sp[1], sp[2]),
        pb: this.cam.p(sp[0], 0, sp[2]),
        prio: (both ? 100 : 0) + (trk ? 80 : 0) + Math.max(r1, r2),
      });
    }
    if (sampleTrail) {
      for (const k of this.pos.keys()) if (!activeKeys.has(k)) { this.pos.delete(k); this.trails.delete(k); }
    }

    items.sort((a, b) => b.p[3] - a.p[3]);
    const topDual = new Set(items.slice().sort((a, b) => b.prio - a.prio).slice(0, 10));
    const labelBoxes = [];

    // 5. Render Triangulation Lasers, Confidence Circles, Trails & Nodes
    for (const it of items) {
      const isSel = this.sel && this.sel.id === it.id;
      const [sx, sy, scP] = it.p, [bx, by] = it.pb;
      const rad = Math.max(3.5, Math.min(11, scP * 0.012 * (it.both ? 1.25 : 0.95)));

      // Dual-laser triangulation lock lines on the floor + confidence circle
      if (it.both && (topDual.has(it) || isSel)) {
        g.strokeStyle = isSel ? cHot : cLine + '0.24)';
        g.lineWidth = isSel ? 1.6 : 0.9;
        g.beginPath();
        g.moveTo(pSt1[0], pSt1[1]); g.lineTo(bx, by);
        g.moveTo(pSt2[0], pSt2[1]); g.lineTo(bx, by);
        g.stroke();
        drawCircle3D(it.x, it.z, it.err3d * (isSel ? 1.0 : 0.65), 28, isSel ? cHot : cLine + '0.25)', isSel ? 1.6 : 0.9);
      } else if (!it.both && isSel) {
        const pSrc = (it.mask & 1) ? pSt1 : pSt2;
        g.strokeStyle = cLine + '0.35)'; g.lineWidth = 1;
        g.beginPath(); g.moveTo(pSrc[0], pSrc[1]); g.lineTo(bx, by); g.stroke();
      }

      // Motion trail
      const tr = this.trails.get(it.key);
      if (tr && tr.length > 1) {
        for (let k = 1; k < tr.length; k++) {
          const pA = this.cam.p(tr[k - 1][0], tr[k - 1][1], tr[k - 1][2]);
          const pB = this.cam.p(tr[k][0], tr[k][1], tr[k][2]);
          g.strokeStyle = it.col;
          g.globalAlpha = (k / tr.length) * 0.45;
          g.lineWidth = Math.max(1, (k / tr.length) * 2.4);
          g.beginPath(); g.moveTo(pA[0], pA[1]); g.lineTo(pB[0], pB[1]); g.stroke();
        }
        g.globalAlpha = 1;
      }

      // Vertical stem to floor
      g.strokeStyle = it.col; g.globalAlpha = it.both ? 0.5 : 0.25; g.lineWidth = 1;
      g.beginPath(); g.moveTo(bx, by); g.lineTo(sx, sy); g.stroke();
      g.fillStyle = it.col; g.beginPath(); g.arc(bx, by, Math.max(2, rad * 0.4), 0, T.TAU); g.fill();
      g.globalAlpha = 1;

      // Node body + dual-lock reticle
      g.fillStyle = it.col;
      g.beginPath(); g.arc(sx, sy, rad, 0, T.TAU); g.fill();
      if (it.both || isSel) {
        const rr = rad * (isSel ? 2.4 : 1.9);
        g.strokeStyle = isSel ? cHot : it.col; g.lineWidth = isSel ? 1.8 : 1.2;
        g.beginPath(); g.arc(sx, sy, rr, t * 2, t * 2 + Math.PI * 1.5); g.stroke();
      }

      // Smart anti-overlap label placement
      if (topDual.has(it) || isSel) {
        const dTxt = it.both ? ` (${it.rx >= 0 ? '+' : ''}${Number(it.rx).toFixed(1)}m, ${Number(it.rz).toFixed(1)}m)` : '';
        const fullTxt = T.fit(g, it.label, u * 11) + dTxt;
        const fpx = Math.max(10, u * 0.72);
        const tw = fullTxt.length * fpx * 0.58;
        let lx = sx + rad + 6, ly = sy + 3;
        // Shift label vertically if another label occupies this screen rectangle
        for (let step = 0; step < 5; step++) {
          const candY = ly + (step === 0 ? 0 : (step % 2 ? -1 : 1) * Math.ceil(step / 2) * (fpx + 3));
          const hit = labelBoxes.some(b => lx < b.x + b.w && lx + tw > b.x && candY - fpx < b.y && candY > b.y - b.h);
          if (!hit || isSel) { ly = candY; break; }
        }
        labelBoxes.push({ x: lx, y: ly, w: tw, h: fpx + 2 });
        if (Math.abs(ly - (sy + 3)) > 4) {
          g.strokeStyle = it.col; g.globalAlpha = 0.4; g.lineWidth = 0.8;
          g.beginPath(); g.moveTo(sx + rad, sy); g.lineTo(lx - 2, ly - fpx * 0.3); g.stroke();
          g.globalAlpha = 1;
        }
        T.txt(g, fullTxt, lx, ly, fpx, isSel ? cHot : it.col, 'm', 'left');
      }
      T.hits.circle(sx, sy, Math.max(15, rad * 2.3), { node: it });
    }

    this.dust.draw(g, this.cam);
    T.bloom(this.hq.cv, 0.55);

    // 6. Tactical HUD & Interactive Calibration Bar
    const pad = u * 1.1;
    T.txt(g, '天眼 · ' + T.tr('LOCATOR 3D (2 STICKS)'), pad, pad + u * 1.2, u * 1.45, cHot, 't', 'left');
    const earSub = earOk
      ? `${T.tr('EAR #2 ONLINE')} · ${T.tr('BAT')} ${L.ear.bat}% · ${L.ear.rssi} dBm · ${T.tr('OFS')} ${L.ofs >= 0 ? '+' : ''}${L.ofs} dB`
      : T.tr('TURN ON M5STICKC PLUS (EAR #2) — CONNECTS AUTOMATICALLY ON CH 6');
    T.txt(g, earSub, pad, pad + u * 2.5, u * 0.72, earOk ? cGrn : cAlt, 'm', 'left');

    // Top-right controls: [ - ] BASE 3.0m [ + ] | [ 🎯 CALIBRATE ] | [ ⌖ ]
    const bh = u * 1.95, gap = u * 0.4, py0 = pad + u * 0.25;
    const wMinus = u * 2.2, wBase = u * 7.2, wPlus = u * 2.2, wCal = u * 10.5, wRst = u * 2.2;
    let px0 = W - pad - (wMinus + wBase + wPlus + wCal + wRst + gap * 4);

    const drawBtn = (w, txt, col, hit, fill = false) => {
      g.fillStyle = fill ? (kur ? 'rgba(229,38,47,.22)' : 'rgba(62,230,255,.16)') : 'rgba(8,14,20,.68)';
      g.strokeStyle = col; g.lineWidth = 1.1;
      g.fillRect(px0, py0, w, bh); g.strokeRect(px0, py0, w, bh);
      T.txt(g, txt, px0 + w / 2, py0 + bh * 0.7, u * 0.72, cHot, 'm', 'center');
      if (hit) T.hits.rect(px0, py0 - 4, w, bh + 8, hit);
      px0 += w + gap;
    };
    drawBtn(wMinus, '−', cLine + '0.45)', { act: 'base', d: -0.5 });
    drawBtn(wBase, `${T.tr('BASE')} ${baseM.toFixed(1)}${T.tr('m')}`, cLine + '0.35)', null);
    drawBtn(wPlus, '+', cLine + '0.45)', { act: 'base', d: 0.5 });
    drawBtn(wCal, '🎯 ' + T.tr('CALIBRATE'), cAcc, { act: 'cal' }, true);
    drawBtn(wRst, '⌖', cLine + '0.45)', { act: 'rst' });

    // Bottom-left telemetry tiles
    const sy0 = H - pad - u * 3.8;
    const n1 = items.filter(x => x.mask & 1).length;
    const n2 = items.filter(x => x.mask & 2).length;
    const nBoth = items.filter(x => x.both).length;
    const stats = [
      [T.tr('LOCKED 2X'), nBoth, cGrn],
      ['TENGEN #1', n1, cAcc],
      ['M5 EAR #2', earOk ? n2 : 'OFF', earOk ? cAlt : cDim],
      [T.tr('OFFSET'), (L.ofs >= 0 ? '+' : '') + L.ofs + ' dB', cHot],
    ];
    stats.forEach(([k, v, col], i) => {
      const bx = pad + i * u * 7.8;
      g.fillStyle = kur ? 'rgba(18,18,18,.72)' : 'rgba(5,12,18,.68)';
      g.strokeStyle = cLine + '0.32)'; g.lineWidth = 1;
      g.fillRect(bx, sy0, u * 7.1, u * 3.4); g.strokeRect(bx, sy0, u * 7.1, u * 3.4);
      T.txt(g, k, bx + u * 0.5, sy0 + u * 1.1, u * 0.64, cDim, 'm', 'left');
      T.txt(g, String(v), bx + u * 0.5, sy0 + u * 2.85, u * 1.45, col, 't', 'left');
    });

    // Toast banner
    if (this.toastT > 0 && this.toastMsg) {
      const bw = Math.min(W * 0.65, u * 28), th = u * 2.6, bx = (W - bw) / 2, by = pad + u * 3.4;
      g.fillStyle = this.toastBad ? 'rgba(255,59,48,.22)' : 'rgba(62,230,255,.18)';
      g.strokeStyle = this.toastBad ? cAlt : cAcc; g.lineWidth = 1.5;
      g.fillRect(bx, by, bw, th); g.strokeRect(bx, by, bw, th);
      T.txt(g, this.toastMsg, W / 2, by + th * 0.68, u * 0.95, cHot, 't', 'center');
    }

    // Selected device details card (bottom-right)
    if (this.sel) {
      const s = this.sel;
      const cw = Math.min(W * 0.44, u * 22), ch = u * 6.4, cx0 = W - pad - cw, cy0 = H - pad - ch;
      g.fillStyle = kur ? 'rgba(14,14,14,.92)' : 'rgba(6,16,24,.9)';
      g.strokeStyle = s.both ? cGrn : cAcc; g.lineWidth = 1.5;
      g.fillRect(cx0, cy0, cw, ch); g.strokeRect(cx0, cy0, cw, ch);
      T.txt(g, T.fit(g, s.label, cw - u * 1.4), cx0 + u * 0.7, cy0 + u * 1.65, u * 1.15, cHot, 't', 'left');
      const d1 = (s.mask & 1) ? rssiToM(s.r1).toFixed(1) + 'm (' + s.r1 + ' dBm)' : '—';
      const d2 = (s.mask & 2) ? rssiToM(s.r2).toFixed(1) + 'm (' + s.r2 + ' dBm)' : '—';
      T.txt(g, `#1: ${d1}  ·  #2: ${d2}`, cx0 + u * 0.7, cy0 + u * 3.2, u * 0.72, cAcc, 'm', 'left');
      const posStr = s.both
        ? `X: ${s.rx >= 0 ? '+' : ''}${Number(s.rx).toFixed(1)}m · Z: ${Number(s.rz).toFixed(1)}m · ±${Number(s.rerr).toFixed(1)}m`
        : T.tr('HEARD BY 1 STICK ONLY');
      T.txt(g, posStr, cx0 + u * 0.7, cy0 + u * 4.8, u * 0.75, s.both ? cGrn : cDim, 'm', 'left');
    }
  },
};
})();
