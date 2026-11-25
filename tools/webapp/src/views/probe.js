// ---- IR / NFC: 'whose remote?' and 'what card?' (the stick only listens and reads) ----
// IR: an ir event per press - protocol, brand (a guess), code, pulse shape.
// NFC: an nfc event per touch - card type by ATQA/SAK, part of UID, NDEF records.
(() => {
const AGO = ms => T.ago(ms / 1000);

T.views.push({
  id: 'probe', group: 'tools', title: 'IR · NFC', subs: ['IR', 'NFC'], sub: 0,
  want() { return this.sub ? 'nfc' : 'ir'; },
  mount(main) {
    this.root = T.el('div', 'view');
    const st = T.el('div', 'stage'); this.root.appendChild(st);
    this.cv = T.canvas(st); main.appendChild(this.root);
    this.t = 0; this.irSeen = 0; this.irRev = 1; this.nfcSeen = 0; this.tap = 1;
  },
  unmount() { this.cv.dispose(); this.root.remove(); },
  onSub(i) { this.sub = i; },
  frame(dt) {
    this.t += dt;
    const { g, w, h } = this.cv;
    T.backdrop(g, w, h, 0.5, 0.55);
    const u = T.unit(w, h), m = u * 1.6, y0 = 50;
    if (this.sub === 0) this.ir(g, w, h, u, m, y0, dt); else this.nfc(g, w, h, u, m, y0, dt);
    T.finish(g, w, h);
  },

  // ---------- IR ----------
  ir(g, w, h, u, m, y0, dt) {
    const I = S.ir, d = I.last, now = performance.now(), portrait = h > w * 1.05;
    if (I.t !== this.irSeen) { this.irSeen = I.t; this.irRev = 0; }
    this.irRev = Math.min(1, this.irRev + dt / 0.55);
    const hold = d && d.rep && now - I.t < 1200, fresh = d ? T.clamp(1 - (now - I.t) / 900) : 0;
    T.header(g, m, y0, w - 2 * m, u, 'IR · whose remote?', d ? `${I.n} presses · last ${AGO(now - I.t)} ago` : 'listening · 38 kHz', false);
    const top = y0 + u * 3.6, bot = h - u * 3;
    if (!d) { this.irIdle(g, w, h, u, top, bot); T.bloom(this.cv, 0.7); T.footer(g, m, h - u * 2.2, w - 2 * m, u, 'point any remote at TENGEN and press a button', 'receive only · nothing is sent'); return; }
    const lw = portrait ? w - 2 * m : (w - 2 * m) * 0.64, lh = portrait ? (bot - top) * 0.62 : bot - top;
    T.panel(g, m, top, lw, lh, u);
    if (fresh > 0) { T.cutPath(g, m, top, lw, lh, u * 0.8); g.strokeStyle = `rgba(${T.RGB.acc},${0.7 * fresh})`; g.lineWidth = 2; g.stroke(); }
    const x0 = m + u * 1.4;
    T.txt(g, '// LAST CODE', x0, top + u * 1.8, u * 1.0, T.C.acc, 't');
    // protocol large + HOLD
    const px = Math.min(u * 5.4, lw * 0.11);
    T.txt(g, d.p, x0, top + u * 2.4 + px, px, T.C.hot, 't');
    if (hold) {
      const hx = x0 + T.tw(g, d.p, px, 't') + u * 1.2, hy = top + u * 2.4 + px * 0.35, pulse = 0.6 + 0.4 * Math.sin(this.t * 9);
      T.cutPath(g, hx, hy, u * 5, u * 2, u * 0.5); g.fillStyle = `rgba(${T.RGB.alert},${0.25 * pulse})`; g.fill(); g.strokeStyle = T.C.alert; g.lineWidth = 1.2; g.stroke();
      T.txt(g, 'HOLD', hx + u * 2.5, hy + u * 1.45, u * 1.1, T.C.alert, 't', 'center');
    }
    let y = top + u * 3.6 + px;
    g.font = `500 ${u * 1.7}px ${T.FC}`;
    T.txt(g, T.fit(g, d.brand || 'unknown brand', lw - u * 3), x0, y + u * 0.6, u * 1.7, d.brand ? T.C.hot : T.C.label, 'c');
    T.txt(g, d.brand ? 'LIKELY · GUESS BY ADDRESS' : 'NO MATCH IN THE BRAND TABLE', x0, y + u * 2, u * 0.75, T.C.dim);
    y += u * 3.6;
    // fields
    const fields = [['ADDR', T.hexs(d.addr, 2)], ['CMD', T.hexs(d.cmd, 2)], ['BITS', String(d.bits)]];
    const fw = (lw - u * 2.8) / 3;
    fields.forEach(([k, v], i) => {
      const fx = x0 + i * fw;
      T.txt(g, k, fx, y, u * 0.8, T.C.dim);
      g.font = `600 ${u * 2.3}px ${T.FT}`; T.txt(g, T.fit(g, v, fw - u), fx, y + u * 2.4, u * 2.3, T.C.hot, 't');
    });
    y += u * 4.6;
    // pulse shape (mark up, space down), drawn left to right
    const wx = x0, ww = lw - u * 2.8, wy = y + u * 0.6, wh = Math.max(u * 3, top + lh - wy - u * 2.6);
    T.txt(g, '// PULSES · ' + (d.raw ? d.raw.length : 0) + ' EDGES', wx, y, u * 0.8, T.C.acc);
    this.wave(g, d.raw || [], wx, wy, ww, wh, u, this.irRev, T.C.acc);
    // history
    const hx = portrait ? m : m + lw + u, hy = portrait ? top + lh + u : top, hw = portrait ? w - 2 * m : w - m - hx, hh = portrait ? bot - hy : lh;
    T.panel(g, hx, hy, hw, hh, u);
    T.txt(g, '// HISTORY', hx + u, hy + u * 1.8, u * 1.0, T.C.acc, 't');
    let ly = hy + u * 3.6;
    for (let i = 0; i < I.hist.length; i++) {
      if (ly + u * 2.6 > hy + hh) break;
      const e = I.hist[i];
      g.globalAlpha = Math.max(0.35, 1 - i * 0.09);
      T.txt(g, e.p, hx + u, ly, u * 1.05, i ? T.C.label : T.C.hot, 't');
      T.txt(g, AGO(now - e.at), hx + hw - u, ly, u * 0.75, T.C.dim, 'm', 'right');
      g.font = `500 ${u * 0.78}px ${T.FC}`;
      T.txt(g, T.fit(g, (e.brand || '—') + '  ' + T.hexs(e.addr) + ':' + T.hexs(e.cmd) + (e.x > 1 ? '  ×' + e.x : ''), hw - u * 2), hx + u, ly + u * 1.2, u * 0.78, T.C.label, 'c');
      g.fillStyle = T.C.line; g.fillRect(hx + u, ly + u * 1.75, hw - 2 * u, 1);
      g.globalAlpha = 1;
      ly += u * 2.7;
    }
    T.bloom(this.cv, 0.55 + 0.5 * fresh);
    T.footer(g, m, h - u * 2.2, w - 2 * m, u, 'brand is a guess · RAW = unknown protocol, same button gives the same code', 'receive only');
  },
  irIdle(g, w, h, u, top, bot) {
    const cx = w / 2, cy = top + (bot - top) * 0.45, t = this.t;
    // remote on the left, receiver on the right, arcs run between them
    const rx = cx - u * 12, ry = cy;
    g.strokeStyle = T.C.label; g.lineWidth = 1.5;
    T.cutPath(g, rx - u * 1.6, ry - u * 3.4, u * 3.2, u * 6.8, u * 0.7); g.stroke();
    g.fillStyle = T.C.label; for (let k = 0; k < 6; k++) g.fillRect(rx - u * 0.9 + (k % 2) * u * 1.2, ry - u * 1.4 + (k >> 1) * u * 1.3, u * 0.6, u * 0.6);
    g.fillStyle = T.C.alert; g.fillRect(rx - u * 0.3, ry - u * 3.1, u * 0.6, u * 0.4);
    for (let k = 0; k < 5; k++) {
      const p = ((t * 0.6 + k / 5) % 1), x = rx + u * 3 + p * u * 18, a = Math.sin(p * Math.PI);
      g.strokeStyle = `rgba(${T.RGB.acc},${0.8 * a})`; g.lineWidth = 2; g.beginPath(); g.arc(x, ry, u * 1.4, -0.7, 0.7); g.stroke();
    }
    const sx = cx + u * 12;
    for (let k = 0; k < 3; k++) { const p = (t * 0.5 + k / 3) % 1; g.strokeStyle = `rgba(${T.RGB.acc},${0.5 * (1 - p)})`; g.lineWidth = 1.2; g.beginPath(); g.arc(sx, ry, u * (1.2 + p * 3), 0, T.TAU); g.stroke(); }
    g.fillStyle = T.C.hot; g.beginPath(); g.arc(sx, ry, u * 0.9, 0, T.TAU); g.fill();
    T.txt(g, 'POINT ANY REMOTE AT TENGEN', cx, cy + u * 6.5, u * 1.6, T.C.hot, 't', 'center');
    T.txt(g, 'PRESS A BUTTON — WE GUESS WHOSE REMOTE IT IS', cx, cy + u * 8.2, u * 0.85, T.C.label, 'm', 'center');
  },
  // square wave from durations [mark, space, ...] (us)
  wave(g, raw, x, y, w, h, u, rev, col) {
    g.fillStyle = `rgba(${T.RGB.bg2},.5)`; g.fillRect(x, y, w, h);
    g.strokeStyle = T.C.line; g.lineWidth = 1;
    for (let k = 1; k < 8; k++) { g.beginPath(); g.moveTo(x + w * k / 8, y); g.lineTo(x + w * k / 8, y + h); g.stroke(); }
    if (!raw.length) return;
    let tot = 0; for (const v of raw) tot += v;
    const hi = y + h * 0.18, lo = y + h * 0.82, lim = x + w * rev;
    g.beginPath(); g.moveTo(x, lo);
    let cx = x;
    for (let i = 0; i < raw.length && cx < lim; i++) {
      const nx = Math.min(lim, cx + raw[i] / tot * w), yy = i % 2 ? lo : hi;
      g.lineTo(cx, yy); g.lineTo(nx, yy); cx = nx;
    }
    g.lineTo(cx, lo);
    g.strokeStyle = col; g.lineWidth = 2; g.stroke();
    if (rev < 1) { g.fillStyle = T.C.hot; g.fillRect(lim - 1, y, 2, h); }
    T.txt(g, (tot / 1000).toFixed(1) + ' MS', x + w - u * 0.4, y + h - u * 0.4, u * 0.7, T.C.dim, 'm', 'right');
  },

  // ---------- NFC ----------
  nfc(g, w, h, u, m, y0, dt) {
    const N = S.nfc, d = N.last, now = performance.now(), portrait = h > w * 1.05;
    if (N.err) {
      T.header(g, m, y0, w - 2 * m, u, 'NFC · what card?', 'no reader', true);
      T.emptyState(g, w, h, u, 'NFC CHIP NOT FOUND', 'this device may have no NFC reader (PN532)', true);
      T.footer(g, m, h - u * 2.2, w - 2 * m, u, 'read only', ''); return;
    }
    if (N.t !== this.nfcSeen) { this.nfcSeen = N.t; this.tap = 0; }
    this.tap = Math.min(1, this.tap + dt / 1.2);
    const on = d && now - N.t < 4000;
    T.header(g, m, y0, w - 2 * m, u, 'NFC · what card?', d ? `${N.hist.length} cards · last ${AGO(now - N.t)} ago` : 'scanning · 13.56 MHz');
    const top = y0 + u * 3.6, bot = h - u * 3;
    const lw = portrait ? w - 2 * m : (w - 2 * m) * 0.44, lh = portrait ? (bot - top) * 0.42 : bot - top;
    // scene: reader (stick) and card
    const cx = m + lw / 2, cy = top + lh * 0.55, t = this.t;
    const cw = Math.min(lw * 0.62, lh * 0.75), ch = cw * 0.63;
    if (!(cw > 4)) return;                               // window not laid out yet (rotation/resize)
    const rx = cx, ry = cy + ch * 0.55;
    // field rings from the reader
    for (let k = 0; k < 4; k++) {
      const p = (t * 0.45 + k / 4) % 1, r = cw * (0.3 + p * 0.7);
      g.strokeStyle = `rgba(${T.RGB.acc},${(on ? 0.35 : 0.18) * (1 - p)})`; g.lineWidth = 1.2;
      g.beginPath(); g.ellipse(rx, ry, Math.max(0, r), Math.max(0, r * 0.32), 0, 0, T.TAU); g.stroke();
    }
    // touch flash
    if (this.tap < 1) {
      const p = this.tap, r = cw * (0.2 + p * 1.1);
      g.strokeStyle = `rgba(${T.RGB.hot},${0.9 * (1 - p)})`; g.lineWidth = 3 * (1 - p) + 1;
      g.beginPath(); g.ellipse(rx, ry, Math.max(0, r), Math.max(0, r * 0.32), 0, 0, T.TAU); g.stroke();
    }
    g.fillStyle = T.C.line; g.beginPath(); g.ellipse(rx, ry, Math.max(0, cw * 0.28), Math.max(0, cw * 0.09), 0, 0, T.TAU); g.fill();
    T.txt(g, 'TENGEN', rx, ry + cw * 0.2, u * 0.75, T.C.dim, 'm', 'center');
    // card: without a card it hovers higher, dashed; on touch it lowers
    const lift = on ? (1 - T.clamp(this.tap * 2.5)) * ch * 0.6 : ch * 0.55 + Math.sin(t * 1.6) * u * 0.6;
    const kx = cx - cw / 2, ky = cy - ch / 2 - lift;
    T.cutPath(g, kx, ky, cw, ch, u * 0.9);
    g.fillStyle = on ? 'rgba(18,58,72,.85)' : `rgba(${T.RGB.bg2},.6)`; g.fill();
    if (!on) g.setLineDash([u * 0.6, u * 0.5]);
    g.strokeStyle = on ? T.C.acc : T.C.label; g.lineWidth = on ? 2 : 1.2; g.stroke(); g.setLineDash([]);
    g.fillStyle = on ? T.C.acc : T.C.dim; g.fillRect(kx + cw * 0.09, ky + ch * 0.3, cw * 0.14, ch * 0.22);
    g.strokeStyle = on ? T.C.hot : T.C.dim; g.lineWidth = 1.2;
    for (let k = 0; k < 3; k++) { g.beginPath(); g.arc(kx + cw * 0.84, ky + ch * 0.2, u * (0.5 + k * 0.45), -0.8, 0.8); g.stroke(); }
    if (d && on && /bank|emv/i.test(d.fam)) T.txt(g, '•••• •••• •••• ••••', kx + cw * 0.09, ky + ch * 0.8, u * 0.9, T.C.label);
    T.txt(g, on ? 'ON THE READER' : d ? 'LAST CARD ' + AGO(now - N.t) + ' AGO' : 'TOUCH A CARD TO TENGEN', cx, Math.min(top + lh - u, ry + cw * 0.36), u * 0.95, on ? T.C.hot : T.C.label, 't', 'center');
    // data card
    const ix = portrait ? m : m + lw + u, iy = portrait ? top + lh + u : top, iw = portrait ? w - 2 * m : w - m - ix, ih = portrait ? bot - iy : lh;
    T.panel(g, ix, iy, iw, ih, u);
    if (!d) {
      T.txt(g, '// WAITING FOR A CARD', ix + u * 1.4, iy + u * 2, u * 1.0, T.C.acc, 't');
      const lines = ['transit cards, door keys, NFC stickers, bank cards', 'shows the card type and public tag data', 'bank cards: number and name are never read'];
      lines.forEach((s, i) => T.txt(g, s.toUpperCase(), ix + u * 1.4, iy + u * (4 + i * 1.6), u * 0.8, T.C.label));
    } else {
      const x0 = ix + u * 1.4, mw = iw - u * 2.8;
      T.txt(g, '// CARD', x0, iy + u * 2, u * 1.0, T.C.acc, 't');
      let y = iy + u * 5.2;
      const fpx = Math.min(u * 3.4, mw * 0.09);
      g.font = `600 ${fpx}px ${T.FT}`; T.txt(g, T.fit(g, d.fam || 'unknown card', mw), x0, y, fpx, T.C.hot, 't');
      y += u * 1.7;
      g.font = `500 ${u * 1.1}px ${T.FC}`; T.txt(g, T.fit(g, d.hint || '', mw), x0, y, u * 1.1, T.C.label, 'c');
      y += u * 2.8;
      const fw = mw / 3;
      [['UID', d.uid || '—'], ['ATQA', d.atqa || '—'], ['SAK', d.sak || '—']].forEach(([k, v], i) => {
        T.txt(g, k, x0 + i * fw, y, u * 0.8, T.C.dim);
        g.font = `500 ${u * 1.9}px ${T.FC}`; T.txt(g, T.fit(g, v, fw - u), x0 + i * fw, y + u * 2.2, u * 1.9, T.C.hot, 'c');
      });
      y += u * 4;
      if (/bank|emv/i.test(d.fam)) { T.txt(g, 'NUMBER AND NAME ARE NOT READ — ONLY "IS IT A PAYMENT CARD"', x0, y, u * 0.8, T.C.alert); y += u * 1.8; }
      const nd = d.ndef || [];
      if (nd.length) {
        T.txt(g, '// TAG SAYS', x0, y, u * 0.9, T.C.acc, 't'); y += u * 1.8;
        for (const r of nd) { if (y > iy + ih - u * 6) break; g.font = `500 ${u * 1.3}px ${T.FC}`; T.txt(g, T.fit(g, r, mw), x0, y, u * 1.3, T.C.hot, 'c'); y += u * 1.9; }
      }
      // recent cards
      const hy = iy + ih - u * 1.2 - Math.min(3, N.hist.length - 1) * u * 1.4;
      if (N.hist.length > 1) {
        g.fillStyle = T.C.line; g.fillRect(x0, hy - u * 1.6, mw, 1);
        T.txt(g, 'EARLIER', x0, hy - u * 0.4, u * 0.7, T.C.dim);
        for (let i = 1; i < Math.min(4, N.hist.length); i++) {
          const e = N.hist[i], yy = hy + (i - 1) * u * 1.4 + u * 0.9;
          g.font = `500 ${u * 0.8}px ${T.FC}`; T.txt(g, T.fit(g, e.fam + '  ' + e.uid, mw * 0.75), x0, yy, u * 0.8, T.C.label, 'c');
          T.txt(g, AGO(now - e.at), x0 + mw, yy, u * 0.75, T.C.dim, 'm', 'right');
        }
      }
    }
    T.bloom(this.cv, on ? 0.75 + 0.5 * (1 - this.tap) : 0.55);
    T.footer(g, m, h - u * 2.2, w - 2 * m, u, 'type by ATQA/SAK · UID partly hidden', 'read only · nothing is written');
  },
});
})();
