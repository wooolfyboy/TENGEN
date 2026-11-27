// ---- PC (version B): laptop via the stick - load rings, 3D cores, process city, record player ----
// The laptop sends data to the stick over USB once a second (pc_monitor.py); the stick forwards it here.
// Everything glides between packets; track position is extrapolated on the tablet.
(() => {
  const fmtT = s => s < 0 ? '--:--' : (s / 60 | 0) + ':' + String(s % 60 | 0).padStart(2, '0');
  const fmtUp = s => { s = s | 0; const d = s / 86400 | 0, h = (s / 3600 | 0) % 24, m = (s / 60 | 0) % 60; return (d ? d + 'd ' : '') + String(h).padStart(2, '0') + ':' + String(m).padStart(2, '0'); };
  const rate = kb => kb >= 1024 ? (kb / 1024).toFixed(1) + ' MB/s' : Math.round(kb) + ' KB/s';
  // tower box: far walls, near walls, roof (convex - draw order is enough)
  const box = (g, cam, x, z, sx, sz, hh, roof, side, edge, fog) => {
    const c = [[-1, -1], [1, -1], [1, 1], [-1, 1]].map(([a, b]) => [x + a * sx, z + b * sz]);
    const B = c.map(p => cam.p(p[0], 0, p[1])), U = c.map(p => cam.p(p[0], hh, p[1]));
    const sides = [0, 1, 2, 3].map(i => ({ i, j: (i + 1) % 4, d: (B[i][3] + B[(i + 1) % 4][3]) / 2 })).sort((a, b) => b.d - a.d);
    g.globalAlpha = fog;
    for (const s of sides) {
      g.beginPath(); g.moveTo(B[s.i][0], B[s.i][1]); g.lineTo(B[s.j][0], B[s.j][1]); g.lineTo(U[s.j][0], U[s.j][1]); g.lineTo(U[s.i][0], U[s.i][1]); g.closePath();
      g.fillStyle = T.rgb(side.map(v => v * (s.i % 2 ? 0.55 : 0.4)), 0.92); g.fill();
      g.strokeStyle = edge; g.lineWidth = 1; g.stroke();
    }
    g.beginPath(); U.forEach((p, k) => k ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1])); g.closePath();
    g.fillStyle = roof; g.fill();
    g.globalAlpha = 1;
    const tc = U.reduce((a, p) => [a[0] + p[0] / 4, a[1] + p[1] / 4], [0, 0]);
    return { top: tc, depth: (B[0][3] + B[2][3]) / 2 };
  };

  T.views.push({
    id: 'pc', title: 'PC', want: 'pc', subs: ['Overview', 'Cores', 'City', 'Media'], sub: 0,
    mount(main) {
      this.root = T.el('div', 'view');
      const st = T.el('div', 'stage'); this.root.appendChild(st);
      this.cv = T.canvas(st, 2);
      main.appendChild(this.root);
      this.cam = T.orbit(0.7, 0.72, 3.4);
      this.drag = T.drag(this.cv.cv, this.cam, { minPitch: 0.25, maxPitch: 1.35 });
      this.drag.onTap = (x, y) => { const hit = T.hits.at(x, y); if (hit && hit.key) { this.mediaKey(hit.key); return; } this.sel = hit && hit.app ? (this.sel === hit.app ? null : hit.app) : null; };
      this.dust = T.dust(160, 1.6, 1.4);
      this.cpu = T.steady(2, 0.6); this.ram = T.steady(1, 0.8);
      this.tw = new Map(); this.coreH = []; this.angle = 0; this.pos = -1; this.posT = 0; this.lastT = 0; this.wasLive = false;
      this.sel = null;
    },
    unmount() { this.cv.dispose(); this.root.remove(); },
    onSub() { this.sel = null; },
    frame(dt, t) {
      const o = this.cv, { g, w, h } = o; o.clear(); T.hits.clear();
      const u = T.unit(w, h), m = u * 1.6, y0 = 50;
      T.backdrop(g, w, h, 0.45, 0.6);
      const p = S.pc, names = ['PC', 'Cores', 'Process city', 'Media'];
      if (!p || !p.live) {
        T.header(g, m, y0, w - 2 * m, u, names[this.sub], this.wasLive ? 'PC offline' : 'waiting for PC', this.wasLive);
        T.emptyState(g, w, h, u, this.wasLive ? 'PC OFFLINE' : 'PC', this.wasLive ? 'no data for a while · is TENGEN Desktop running?' : 'waiting for PC · run TENGEN Desktop', this.wasLive);
        T.finish(g, w, h); return;
      }
      this.wasLive = true;
      this.cpu.set(p.cpu); this.cpu.tick(dt); this.ram.set(p.ram); this.ram.tick(dt);
      if (S.pcT !== this.lastT) { this.lastT = S.pcT; if (p.trk) { this.pos = p.trk.pos; this.posT = t; } }
      const top = y0 + u * 3.2, bot = h - m * 0.6 - u * 1.8, hot = this.cpu.v >= 90;
      const area = { x: m, y: top, w: w - 2 * m, h: bot - top };
      let status = `${p.host || 'PC'} · up ${fmtUp(p.up)}`;
      if (this.sub === 0) this.overview(g, area, u, p);
      else if (this.sub === 1) { status = `${p.c ? p.c.length : 0} cores · ${Math.round(this.cpu.v)}%` + (p.ghz ? ` · ${p.ghz.toFixed(1)} GHz` : ''); this.cores(g, w, h, area, u, dt, t, p); }
      else if (this.sub === 2) { status = `${(p.city || []).length} programs · top ${p.top || '—'}`; this.city(g, w, h, area, u, dt, t, p); }
      else { status = p.trk ? (p.trk.app || 'media') + (p.trk.play ? ' · playing' : ' · paused') : 'nothing playing'; this.media(g, area, u, dt, t, p); }
      T.header(g, m, y0, w - 2 * m, u, names[this.sub], status, hot);
      T.dots(g, m, y0 + u * 2.75, u, 4, this.sub);
      if (this.sub === 2 && this.selCard) this.selCard();
      T.footer(g, m, bot + u * 0.2, w - 2 * m, u, ['PC over USB', 'drag: orbit', 'drag: orbit · tap tower: details', 'now playing'][this.sub], (p.age || 0) >= 3 ? `data ${p.age}s old` : 'updates every second');
      T.finish(g, w, h);
    },
    // ---- overview: concentric chronograph rings, CPU graph, disk/net/battery ----
    ring(g, cx, cy, R, v, label, big, unit, sub, alert) {
      const a0 = Math.PI * 0.75, a1 = Math.PI * 2.25, lw = R * 0.115, va = T.clamp(v / 100);
      const ac = alert ? T.C.alert : T.C.acc, rgb = alert ? `${T.ALERT[0]},${T.ALERT[1]},${T.ALERT[2]}` : T.RGB.acc;
      // Outer astrolabe graduation limb + subtle Zen Ensō inner ring
      g.strokeStyle = `rgba(${rgb},.28)`; g.lineWidth = 1;
      g.beginPath(); g.arc(cx, cy, R + lw * 1.05, a0, a1); g.stroke();
      g.strokeStyle = `rgba(${T.RGB.line},.75)`; g.lineWidth = 1;
      g.beginPath(); g.arc(cx, cy, R - lw * 1.05, 0, T.TAU); g.stroke();
      for (let k = 0; k <= 24; k++) {
        const a = a0 + (a1 - a0) * k / 24, maj = k % 6 === 0;
        const r0 = R + lw * 1.05, r1 = r0 + (maj ? R * 0.085 : R * 0.042);
        g.strokeStyle = k / 24 <= va ? ac : (maj ? T.C.label : T.C.line);
        g.lineWidth = maj ? 1.4 : 1;
        g.beginPath(); g.moveTo(cx + Math.cos(a) * r0, cy + Math.sin(a) * r0); g.lineTo(cx + Math.cos(a) * r1, cy + Math.sin(a) * r1); g.stroke();
      }
      g.lineCap = 'butt';
      g.strokeStyle = T.C.line; g.lineWidth = lw; g.beginPath(); g.arc(cx, cy, R, a0, a1); g.stroke();
      const n = 40;
      for (let k = 0; k < n * va; k++) {
        const aa = a0 + (a1 - a0) * k / n, ab = a0 + (a1 - a0) * (k + 0.72) / n;
        g.strokeStyle = alert ? T.C.alert : T.heat(0.3 + 0.7 * k / n); g.beginPath(); g.arc(cx, cy, R, aa, ab); g.stroke();
      }
      // Live needle diamond at current value
      const angV = a0 + (a1 - a0) * va;
      T.diamond(g, cx + Math.cos(angV) * R, cy + Math.sin(angV) * R, Math.max(3, R * 0.055), T.C.hot, true);
      for (let k = 0; k <= 10; k++) {
        const a = a0 + (a1 - a0) * k / 10;
        g.beginPath(); g.moveTo(cx + Math.cos(a) * (R - lw * 0.85), cy + Math.sin(a) * (R - lw * 0.85));
        g.lineTo(cx + Math.cos(a) * (R - lw * 1.25), cy + Math.sin(a) * (R - lw * 1.25));
        g.strokeStyle = k % 5 ? T.C.dim : ac; g.lineWidth = 1; g.stroke();
      }
      const bs = String(Math.round(big)), bw = T.tw(g, bs, R * 0.62, 't');
      T.txt(g, bs, cx, cy + R * 0.2, R * 0.62, alert ? T.C.alert : T.C.hot, 't', 'center');
      T.txt(g, unit, cx + bw / 2 + R * 0.04, cy + R * 0.2, R * 0.16, T.C.dim);
      T.txt(g, label, cx, cy - R * 0.34, R * 0.15, ac, 't', 'center');
      if (sub) T.txt(g, String(sub).toUpperCase(), cx, cy + R * 0.86, R * 0.12, T.C.label, 'm', 'center');
    },
    overview(g, a, u, p) {
      const port = a.h > a.w * 1.05;
      const lw = port ? a.w : a.w * 0.6, chartH = Math.max(u * 6, a.h * 0.26);
      const ringsH = port ? a.h * 0.4 : a.h - chartH - u * 1.5;
      const R = Math.min(lw * 0.185, ringsH / 2.4);
      const cy = a.y + ringsH / 2 + u * 0.4;
      this.ring(g, a.x + lw * 0.27, cy, R, this.cpu.v, 'CPU', this.cpu.v, '%', p.ghz ? p.ghz.toFixed(2) + ' GHz' : '', this.cpu.v >= 90);
      this.ring(g, a.x + lw * 0.73, cy, R, this.ram.v, 'RAM', this.ram.v, '%', (p.ru != null ? p.ru.toFixed(1) + ' / ' + (p.rt || 0).toFixed(0) + ' GB' : ''), this.ram.v >= 90);
      // CPU history with precision reticle corners
      const cyy = port ? a.y + ringsH + u : a.y + a.h - chartH, cw = lw;
      T.txt(g, '// ' + T.tr('CPU · 2 MIN'), a.x, cyy + u * 0.7, u * 0.75, T.C.acc);
      const gy = cyy + u * 1.2, gh = chartH - u * 1.4;
      g.strokeStyle = `rgba(${T.RGB.line},.6)`; g.lineWidth = 1;
      for (let k = 0; k <= 4; k++) { const y = gy + gh * k / 4; g.beginPath(); g.moveTo(a.x, y); g.lineTo(a.x + cw, y); g.stroke(); }
      T.brackets(g, a.x, gy, cw, gh, u * 0.55, `rgba(${T.RGB.acc},.45)`, 1.2);
      const hist = S.pcHist.slice(-120);
      if (hist.length > 1) T.spark(g, a.x, gy, cw * (hist.length / 120), gh, hist, 0, 100, this.cpu.v >= 90 ? T.C.alert : T.C.acc);
      // right (or bottom): disk, net, battery, leader
      const px = port ? a.x : a.x + lw + u * 2, py = port ? gy + gh + u * 1.4 : a.y, pw = port ? a.w : a.w - lw - u * 2, ph = port ? a.y + a.h - py : a.h;
      T.panel(g, px, py, pw, ph, u);
      let y = py + u * 1.6;
      const row = (k, v, bar, alert) => {
        if (y + u * 2.4 > py + ph) return;
        T.txt(g, k, px + u, y, u * 0.8, T.C.dim);
        g.font = `${u * 0.95}px ${T.FM}`; T.txt(g, T.fit(g, String(v).toUpperCase(), pw * 0.62), px + pw - u, y, u * 0.95, alert ? T.C.alert : T.C.hot, 'm', 'right');
        if (bar != null) T.segBar(g, px + u, y + u * 0.6, pw - u * 2, u * 0.45, bar, 20, false, alert);
        y += bar != null ? u * 2.6 : u * 1.8;
      };
      row('TOP APP', p.top || '—');
      row('DISK C:', Math.round(p.disk || 0) + '%', (p.disk || 0) / 100, p.disk >= 95);
      row('DISK I/O', `R ${(p.dr || 0).toFixed(1)}  W ${(p.dw || 0).toFixed(1)} MB/s`, T.clamp(((p.dr || 0) + (p.dw || 0)) / 200));
      row('NET ↓', rate(p.nd || 0), T.clamp(Math.log10(1 + (p.nd || 0)) / 5));
      row('NET ↑', rate(p.nu || 0), T.clamp(Math.log10(1 + (p.nu || 0)) / 5));
      if (p.bat >= 0) row('BATTERY', p.bat + '%' + (p.plug ? ' ⚡' : ''), p.bat / 100, p.bat <= 15 && !p.plug);
      row('UPTIME', fmtUp(p.up || 0));
      if (p.vol >= 0) row('VOLUME', p.mute ? 'mute' : p.vol + '%', p.mute ? 0 : p.vol / 100);
    },
    // ---- cores: towers on the floor ----
    cores(g, w, h, a, u, dt, t, p) {
      const cam = this.cam, n = (p.c || []).length;
      this.drag.tick(dt); if (this.drag.idle()) cam.yaw += dt * 0.08;
      const port = h > w * 1.05;
      cam.frame(w / 2, a.y + a.h * (port ? 0.55 : 0.6), Math.min(w, a.h * 1.7) * 0.95);
      T.floorGrid(g, cam, 1.6, 0.2);
      this.dust.tick(dt);
      const cols = n <= 4 ? n : n <= 8 ? 4 : Math.ceil(Math.sqrt(n)), rows = Math.ceil(n / Math.max(1, cols)), sp = 0.42;
      const items = [];
      for (let i = 0; i < n; i++) {
        const v = S.cores[i] ? S.cores[i].v : p.c[i];
        this.coreH[i] = T.lerp(this.coreH[i] == null ? 0 : this.coreH[i], v, T.ease(dt, 0.35));
        const x = ((i % cols) - (cols - 1) / 2) * sp, z = ((i / cols | 0) - (rows - 1) / 2) * sp;
        items.push({ i, x, z, v: this.coreH[i], d: cam.p(x, 0, z)[3] });
      }
      items.sort((A, B) => B.d - A.d);
      const tops = [];
      for (const it of items) {
        const hh = 0.03 + it.v / 100 * 1.1, al = it.v >= 90, col = al ? T.ALERT : T.heatRGB(0.3 + 0.7 * it.v / 100);
        const fog = T.clamp(1.15 - (it.d + 1) * 0.25, 0.4, 1);
        const r = box(g, cam, it.x, it.z, 0.13, 0.13, hh, T.rgb(col, 0.95), col, T.rgb(col, 0.6), fog);
        tops.push({ it, r });
      }
      this.dust.draw(g, cam);
      T.bloom(this.cv, 0.8);
      for (const { it, r } of tops) {
        T.txt(g, Math.round(it.v) + '%', r.top[0], r.top[1] - u * 0.6, u * 0.85, it.v >= 90 ? T.C.alert : T.C.hot, 't', 'center');
        T.txt(g, 'C' + it.i, r.top[0], r.top[1] - u * 1.6, u * 0.62, T.C.dim, 'm', 'center');
      }
      // summary on the left
      T.bigNum(g, Math.round(this.cpu.v), a.x, a.y + u * 4, u * 3.6, this.cpu.v >= 90 ? T.C.alert : T.C.hot, '% CPU');
      const hist = S.pcHist.slice(-60);
      if (hist.length > 1) T.spark(g, a.x, a.y + u * 5, Math.min(a.w * 0.3, u * 14), u * 3, hist, 0, 100);
    },
    // ---- process city: Zen monolith sanctuary on 6x6 circuit plaza ----
    city(g, w, h, a, u, dt, t, p) {
      const cam = this.cam;
      this.drag.tick(dt);
      if (this.sel) {
        const se = this.tw.get(this.sel);
        if (se) {
          const tx = ((se.slot % 6) - 2.5) * 0.4, tz = ((se.slot / 6 | 0) - 2.5) * 0.4;
          const tyaw = -Math.PI / 2 - Math.atan2(tz, tx) - 0.28;
          const dy = ((tyaw - cam.yaw + Math.PI) % T.TAU + T.TAU) % T.TAU - Math.PI;
          if (!this.drag.down) cam.yaw += dy * Math.min(1, dt * 2.2);
        }
      } else if (this.drag.idle()) cam.yaw += dt * 0.065;
      cam.frame(w / 2, a.y + a.h * 0.64, Math.min(w, a.h * 1.7) * 1.05);
      const GC = T.bleUI && T.bleUI.glCity, useGL = GC && GC.ok();   // GPU (shared Zen monolith engine); 2D is the fallback
      if (!useGL) T.floorGrid(g, cam, 1.4, 0.233);
      this.dust.tick(dt);
      const now = new Set(), list = p.city || [];
      const maxMB = Math.max(300, ...list.map(r => r[2]));
      if (this._cityCnt != null && list.length > this._cityCnt) this._cityPulse = 0.01;
      this._cityCnt = list.length;
      this._cityPulse = this._cityPulse != null && this._cityPulse < 1.2 ? this._cityPulse + dt * 0.55 : 9;
      for (const [name, cpu, mb, fg, slot] of list) {
        now.add(name);
        let e = this.tw.get(name);
        if (!e) { e = { name, h: 0, w: 0 }; this.tw.set(name, e); }
        Object.assign(e, { cpu, mb, fg, slot, alive: true });
      }
      const items = [];
      for (const e of this.tw.values()) {
        if (!now.has(e.name)) e.alive = false;
        const th = e.alive ? 0.06 + Math.min(1, e.cpu / 40) * 1.2 : 0, tw = e.alive ? 0.04 + 0.075 * Math.sqrt(e.mb / maxMB) : 0;
        e.h = T.lerp(e.h, th, T.ease(dt, 0.6)); e.w = T.lerp(e.w, tw, T.ease(dt, 0.6));
        if (!e.alive && e.h < 0.005) { this.tw.delete(e.name); if (this.sel === e.name) this.sel = null; continue; }
        const x = ((e.slot % 6) - 2.5) * 0.4, z = ((e.slot / 6 | 0) - 2.5) * 0.4;
        items.push({ e, x, z, d: cam.p(x, 0, z)[3] });
      }
      items.sort((A, B) => B.d - A.d);
      const tops = [];
      if (useGL) GC.render(g, cam, w, h, items.filter(it => it.e.w > 0.002).map(it => {
        const e = it.e, base = T.heatRGB(0.3 + 0.6 * Math.min(1, e.cpu / 50)), hot = e.cpu > 50;
        const arch = hot ? 3 : e.fg ? 2 : (e.mb / maxMB > 0.45 ? 1 : 0);
        return { x: it.x, z: it.z, sx: e.w, sz: e.w, h: Math.max(0.01, e.h), col: hot ? T.ALERT : base, roof: e.fg ? T.heatRGB(1) : hot ? T.ALERT : base,
          seed: T.hash(e.name) % 997, glow: Math.min(1, e.cpu / 40), tr: hot, sel: this.sel === e.name, arch };
      }), { t, rmax: 1.48, grid: 'square', step: 0.233, pulse: this._cityPulse < 1 ? this._cityPulse : 0 });
      const o0 = cam.p(0, 0, 0), busy = [...items].filter(q => q.e.alive && (q.e.cpu > 2 || q.e.fg || q.e.name === this.sel)).sort((A, B) => B.e.cpu - A.e.cpu).slice(0, 5);
      busy.forEach((it, idx) => {
        const hot = it.e.cpu > 50, col = hot ? T.C.alert : T.C.acc, pB = cam.p(it.x, 0.003, it.z);
        g.strokeStyle = hot ? `rgba(${T.RGB.alert},.28)` : `rgba(${T.RGB.acc},.2)`; g.lineWidth = 1;
        g.beginPath(); g.moveTo(pB[0], pB[1]); g.lineTo(o0[0], o0[1]); g.stroke();
        const pk = (t * (0.45 + Math.min(0.8, it.e.cpu / 60)) + idx * 0.23) % 1;
        const sp = cam.p(it.x * (1 - pk), 0.005, it.z * (1 - pk));
        g.fillStyle = hot ? col : T.C.hot; g.beginPath(); g.arc(sp[0], sp[1], hot ? 2.4 : 1.8, 0, T.TAU); g.fill();
      });
      if (T.bleUI && T.bleUI.drawOrigin) T.bleUI.drawOrigin(g, cam, u, t, busy.length > 0);
      for (const it of items) {
        const e = it.e, base = T.heatRGB(0.3 + 0.6 * Math.min(1, e.cpu / 50)), hot = e.cpu > 50, isSel = this.sel === e.name;
        const roof = e.fg ? T.C.hot : hot ? T.C.alert : T.rgb(base, 0.95);
        const fog = T.clamp(1.15 - (it.d + 1) * 0.25, 0.35, 1);
        const r = useGL ? { top: cam.p(it.x, e.h, it.z) } : box(g, cam, it.x, it.z, e.w, e.w, e.h, roof, hot ? T.ALERT : base, isSel ? T.C.hot : T.rgb(base, 0.5), fog);
        if ((hot || isSel || e.fg) && T.bleUI && T.bleUI.drawLock) T.bleUI.drawLock(g, cam, it.x, it.z, e.h, Math.max(0.11, e.w * 2.1), hot ? T.C.alert : isSel ? T.C.hot : T.C.acc, t, isSel || hot);
        tops.push({ e, r });
        const pr = cam.p(it.x, e.h * 0.5, it.z);
        T.hits.circle(pr[0], pr[1], Math.max(u * 1.6, e.w * pr[2] * 1.4), { app: e.name });
      }
      this.dust.draw(g, cam);
      T.bloom(this.cv, useGL ? 0.45 : 0.75);
      const top5 = [...tops].filter(q => q.e.alive).sort((A, B) => B.e.cpu - A.e.cpu).slice(0, 5);
      for (const q of top5) {
        if (q.e.name === this.sel) continue;
        const tx0 = q.r.top[0], ty0 = q.r.top[1];
        g.strokeStyle = `rgba(${T.RGB.hot},.32)`; g.lineWidth = 1; g.beginPath(); g.moveTo(tx0, ty0 - u * 0.15); g.lineTo(tx0, ty0 - u * 1.15); g.stroke();
        g.font = `${u * 0.74}px ${T.FM}`;
        const lbl = T.fit(g, q.e.name.toUpperCase(), u * 9) + ' ' + Math.round(q.e.cpu) + '%', lw = T.tw(g, lbl, u * 0.74);
        g.fillStyle = `rgba(${T.RGB.bg},.72)`; g.fillRect(tx0 - lw / 2 - u * 0.3, ty0 - u * 2.15, lw + u * 0.6, u * 1.05);
        T.txt(g, lbl, tx0, ty0 - u * 1.35, u * 0.74, q.e.cpu > 50 ? T.C.alert : q.e.fg ? T.C.hot : T.C.label, 'm', 'center');
      }
      const s = tops.find(q => q.e.name === this.sel);
      this.selCard = s ? () => {
        const e = s.e, [tx, ty] = s.r.top, bw = u * 15.8, bh = u * 7.4, hot = e.cpu > 50, ac = hot ? T.C.alert : T.C.acc;
        const bx = T.clamp(tx + u * 3, a.x, a.x + a.w - bw), by = T.clamp(ty - u * 8, a.y + u, a.y + a.h - bh);
        T.diamond(g, tx, ty, u * 0.75, T.C.hot, false);
        g.strokeStyle = ac; g.lineWidth = 1.1; g.beginPath(); g.moveTo(tx, ty); g.lineTo(bx, by + bh * 0.5); g.stroke();
        T.panel(g, bx, by, bw, bh, u, `rgba(${T.RGB.bg},.86)`, ac);
        T.brackets(g, bx + 3, by + 3, bw - 6, bh - 6, u * 0.45, ac, 1);
        T.drawSealChar(g, bx + bw - u * 2.25, by + u * 0.6, u * 1.55, hot ? '警' : e.fg ? '動' : '機', ac, hot);
        g.font = `500 ${u * 1.2}px ${T.FC}`; T.txt(g, T.fit(g, e.name, bw - u * 3.4), bx + u, by + u * 1.7, u * 1.2, T.C.hot, 'c');
        T.txt(g, (e.fg ? 'active window' : 'background').toUpperCase(), bx + u, by + u * 2.8, u * 0.72, e.fg ? T.C.acc : T.C.label);
        T.bigNum(g, Math.round(e.cpu), bx + u, by + u * 5.7, u * 2.6, hot ? T.C.alert : T.C.hot, '% CPU');
        T.txt(g, (e.mb >= 1024 ? (e.mb / 1024).toFixed(1) + ' GB' : e.mb + ' MB') + ' RAM', bx + bw - u, by + u * 5.5, u * 0.85, T.C.label, 'm', 'right');
        T.segBar(g, bx + u, by + u * 6.3, bw - u * 2, u * 0.36, e.cpu / 100, 16, false, hot);
      } : null;
    },
    // ---- music: record player ----
    media(g, a, u, dt, t, p) {
      const trk = p.trk, port = a.h > a.w * 1.05;
      const R = port ? Math.min(a.w * 0.38, a.h * 0.26) : Math.min(a.h * 0.42, a.w * 0.24);
      const cx = port ? a.x + a.w / 2 : a.x + R + u * 2, cy = port ? a.y + R + u * 1.5 : a.y + a.h / 2;
      const play = trk && trk.play;
      this.spinV = T.lerp(this.spinV || 0, play ? 0.55 * T.TAU : 0, T.ease(dt, play ? 0.5 : 0.9));   // 33⅓ rpm, spin-up and coast-down
      this.angle += this.spinV * dt;
      // glow & Zen acoustic ripples around the disc (pure theme tokens: works in DENKO & KURENAI)
      const gl = g.createRadialGradient(cx, cy, R * 0.88, cx, cy, R * 1.32);
      gl.addColorStop(0, `rgba(${T.RGB.acc},${play ? 0.22 : 0.06})`); gl.addColorStop(1, `rgba(${T.RGB.acc},0)`);
      g.fillStyle = gl; g.beginPath(); g.arc(cx, cy, R * 1.32, 0, T.TAU); g.fill();
      if (play) {
        for (let k = 0; k < 2; k++) {
          const rp = (t * 0.35 + k * 0.5) % 1;
          g.strokeStyle = `rgba(${T.RGB.acc},${0.28 * (1 - rp)})`; g.lineWidth = 1;
          g.beginPath(); g.arc(cx, cy, R * (1.02 + rp * 0.26), 0, T.TAU); g.stroke();
        }
      }
      // disc and grooves
      g.fillStyle = T.C.bg2 || T.C.bg; g.beginPath(); g.arc(cx, cy, R, 0, T.TAU); g.fill();
      g.lineWidth = 1;
      for (let r = R * 0.38; r < R * 0.98; r += Math.max(1.8, R / 64)) { g.strokeStyle = `rgba(${T.RGB.label},${0.05 + 0.04 * ((r * 7) % 3 > 1.5)})`; g.beginPath(); g.arc(cx, cy, r, 0, T.TAU); g.stroke(); }
      if (g.createConicGradient) {
        const cg = g.createConicGradient(-0.6, cx, cy);
        cg.addColorStop(0, `rgba(${T.RGB.hot},0)`); cg.addColorStop(0.06, `rgba(${T.RGB.hot},.13)`); cg.addColorStop(0.12, `rgba(${T.RGB.hot},0)`);
        cg.addColorStop(0.5, `rgba(${T.RGB.hot},0)`); cg.addColorStop(0.56, `rgba(${T.RGB.hot},.1)`); cg.addColorStop(0.62, `rgba(${T.RGB.hot},0)`); cg.addColorStop(1, `rgba(${T.RGB.hot},0)`);
        g.fillStyle = cg; g.beginPath(); g.arc(cx, cy, R * 0.98, 0, T.TAU); g.arc(cx, cy, R * 0.38, 0, T.TAU, true); g.fill();
      }
      g.strokeStyle = T.C.acc; g.lineWidth = 1.5; g.beginPath(); g.arc(cx, cy, R, 0, T.TAU); g.stroke();
      // outer azimuth calibration ticks
      for (let k = 0; k < 12; k++) {
        const a2 = k * T.TAU / 12;
        g.strokeStyle = k % 3 === 0 ? T.C.acc : T.C.line; g.lineWidth = 1;
        g.beginPath(); g.moveTo(cx + Math.cos(a2) * (R * 1.03), cy + Math.sin(a2) * (R * 1.03));
        g.lineTo(cx + Math.cos(a2) * (R * (k % 3 === 0 ? 1.09 : 1.06)), cy + Math.sin(a2) * (R * (k % 3 === 0 ? 1.09 : 1.06))); g.stroke();
      }
      // label spins with Sound seal (音)
      g.save(); g.translate(cx, cy); g.rotate(this.angle);
      const lg = g.createRadialGradient(0, 0, 0, 0, 0, R * 0.34); lg.addColorStop(0, T.C.panel); lg.addColorStop(1, `rgba(${T.RGB.acc},.25)`);
      g.fillStyle = lg; g.beginPath(); g.arc(0, 0, R * 0.34, 0, T.TAU); g.fill();
      g.strokeStyle = T.C.acc; g.lineWidth = 1.2; g.beginPath(); g.arc(0, 0, R * 0.34, 0, T.TAU); g.stroke();
      T.txt(g, 'TENGEN · 音', 0, -R * 0.14, R * 0.085, T.C.acc, 't', 'center');
      for (let k = 0; k < 3; k++) { g.rotate(T.TAU / 3); g.fillStyle = `rgba(${T.RGB.acc},.55)`; g.fillRect(R * 0.2, -1, R * 0.1, 2); }
      g.restore();
      g.fillStyle = T.C.hot; g.beginPath(); g.arc(cx, cy, Math.max(2, R * 0.025), 0, T.TAU); g.fill();
      // tonearm
      const ax = cx + R * 1.08, ay = cy - R * 0.92, prog = trk && trk.dur > 0 ? T.clamp(this.curPos(t, trk) / trk.dur) : 0;
      const ang = Math.PI * (0.56 + (trk ? 0.03 + prog * 0.1 : 0));   // needle: from edge to middle
      g.strokeStyle = T.C.label; g.lineWidth = Math.max(2, R * 0.025); g.lineCap = 'round';
      g.beginPath(); g.moveTo(ax, ay); g.lineTo(ax + Math.cos(ang) * R * 0.9, ay + Math.sin(ang) * R * 0.9); g.stroke(); g.lineCap = 'butt';
      g.fillStyle = T.C.dim; g.beginPath(); g.arc(ax, ay, R * 0.06, 0, T.TAU); g.fill();
      T.bloom(this.cv, 0.7);
      // text
      const tx = port ? a.x : cx + R * 1.45, tw = port ? a.w : a.x + a.w - tx;
      let ty = port ? cy + R * 1.35 + u * 2 : a.y + a.h * 0.3;
      if (!trk) { T.txt(g, 'NOTHING PLAYING', tx, ty + u * 2, u * 2, T.C.label, 't'); T.txt(g, 'START A TRACK ON THE PC', tx, ty + u * 3.5, u * 0.85, T.C.dim); this.controls(g, tx, ty + u * 6, tw, u, t, false); return; }
      T.txt(g, (trk.app || 'MEDIA').toUpperCase() + (play ? '  ▸ PLAYING' : '  ▮▮ PAUSED'), tx, ty, u * 0.85, play ? T.C.acc : T.C.label);
      ty += u * 3;
      g.font = `500 ${u * 2.4}px ${T.FC}`; T.txt(g, T.fit(g, trk.t || '—', tw), tx, ty, u * 2.4, T.C.hot, 'c');
      ty += u * 2;
      g.font = `500 ${u * 1.3}px ${T.FC}`; T.txt(g, T.fit(g, trk.a || '', tw), tx, ty, u * 1.3, T.C.label, 'c');
      ty += u * 2.6;
      const pos = this.curPos(t, trk);
      g.fillStyle = T.C.line; g.fillRect(tx, ty, tw, Math.max(2, u * 0.18));
      g.fillStyle = T.C.acc; g.fillRect(tx, ty, tw * prog, Math.max(2, u * 0.18));
      T.diamond(g, tx + tw * prog, ty + u * 0.09, u * 0.4, T.C.hot, true);
      T.txt(g, fmtT(pos), tx, ty + u * 1.5, u * 0.85, T.C.hot);
      T.txt(g, fmtT(trk.dur), tx + tw, ty + u * 1.5, u * 0.85, T.C.dim, 'm', 'right');
      ty += u * 3.4;
      if (p.vol >= 0) {
        T.txt(g, p.mute ? 'VOLUME · MUTE' : 'VOLUME · ' + p.vol + '%', tx, ty, u * 0.8, p.mute ? T.C.alert : T.C.label);
        T.segBar(g, tx, ty + u * 0.6, Math.min(tw, u * 18), u * 0.5, p.mute ? 0 : p.vol / 100, 20, true);
      }
      this.controls(g, tx, ty + u * 2.6, tw, u, t, play);
    },
    // remote: prev / play-pause / next, vol - MUTE + - the stick sends the key to the laptop (pc_monitor presses the media key)
    controls(g, x, y, w, u, t, play) {
      const keys = [['PREV', 'prev'], ['PLAY', play ? 'pause' : 'play'], ['NEXT', 'next'], null, ['VOLDN', 'minus'], ['MUTE', 'mute'], ['VOLUP', 'plus']];
      const bs = Math.min(u * 4.2, (w - u * 2) / 7.2), gap = bs * 0.18;
      let bx = x;
      for (const k of keys) {
        if (!k) { bx += bs * 0.5; continue; }
        const [key, ic] = k, hot = this.pressK === key && t - this.pressT < 0.25, big = key === 'PLAY';
        T.panel(g, bx, y, bs, bs, u * 0.5, hot ? `rgba(${T.RGB.acc},.35)` : big ? `rgba(${T.RGB.acc},.12)` : `rgba(${T.RGB.panel},.35)`, hot || big ? T.C.acc : `rgba(${T.RGB.acc},.3)`);
        const cx = bx + bs / 2, cy = y + bs / 2, s = bs * 0.2, col = hot ? T.C.hot : big ? T.C.hot : T.C.acc;
        g.fillStyle = col; g.strokeStyle = col; g.lineWidth = Math.max(1.5, bs * 0.06);
        const tri = (x0, dir) => { g.beginPath(); g.moveTo(x0, cy - s); g.lineTo(x0 + dir * s * 1.3, cy); g.lineTo(x0, cy + s); g.closePath(); g.fill(); };
        if (ic === 'play') tri(cx - s * 0.6, 1);
        else if (ic === 'pause') { g.fillRect(cx - s * 0.8, cy - s, s * 0.55, s * 2); g.fillRect(cx + s * 0.25, cy - s, s * 0.55, s * 2); }
        else if (ic === 'next') { tri(cx - s * 1.1, 1); g.fillRect(cx + s * 0.35, cy - s, s * 0.35, s * 2); }
        else if (ic === 'prev') { tri(cx + s * 1.1, -1); g.fillRect(cx - s * 0.7, cy - s, s * 0.35, s * 2); }
        else if (ic === 'minus') g.fillRect(cx - s, cy - s * 0.15, s * 2, s * 0.3);
        else if (ic === 'plus') { g.fillRect(cx - s, cy - s * 0.15, s * 2, s * 0.3); g.fillRect(cx - s * 0.15, cy - s, s * 0.3, s * 2); }
        else if (ic === 'mute') { g.fillRect(cx - s * 1.1, cy - s * 0.4, s * 0.5, s * 0.8); g.beginPath(); g.moveTo(cx - s * 0.6, cy - s * 0.4); g.lineTo(cx, cy - s); g.lineTo(cx, cy + s); g.lineTo(cx - s * 0.6, cy + s * 0.4); g.closePath(); g.fill();
          g.beginPath(); g.moveTo(cx + s * 0.4, cy - s * 0.45); g.lineTo(cx + s * 1.2, cy + s * 0.45); g.moveTo(cx + s * 1.2, cy - s * 0.45); g.lineTo(cx + s * 0.4, cy + s * 0.45); g.stroke(); }
        T.hits.rect(bx, y, bs, bs, { key });
        bx += bs + gap;
      }
    },
    mediaKey(key) {
      this.pressK = key; this.pressT = performance.now() / 1000;
      const p = S.pc; if (key === 'PLAY' && p && p.trk) p.trk.play = p.trk.play ? 0 : 1;   // show at once, do not wait for the laptop
      fetch(`${T.apiBase()}/api/pckey?k=${key}`, { cache: 'no-store' }).catch(() => {});
    },
    curPos(t, trk) {
      if (this.pos < 0) return -1;
      let s = this.pos + (trk.play ? t - this.posT : 0);
      if (trk.dur > 0) s = Math.min(s, trk.dur);
      return s;
    },
  });
})();
