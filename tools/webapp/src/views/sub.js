// ---- SUB-GHZ (CC1101, receive only): 3D waterfall and burst monitor ----
// Spectrum: time goes into depth as terrain, glowing spectrum line in front.
// Monitor: level at the band frequency, bursts and their list.
// ---- WebGL: terrain mesh for the same T.orbit camera as the HUD (labels match vertices) ----
// T.glOrbit.grid(name, C, R) -> { set(heights), draw(P, cam, vp, geo, extra) }; heights[r*C+c], r=0 - nearest row.
// Vertex shader repeats T.orbit.p(): yaw/pitch rotation, perspective F/(D+z) - HUD and mesh do not diverge.
if (T.GL && T.GL.ok && !T.glOrbit) (() => {
  const gl = T.GL.gl, deriv = !!gl.getExtension('OES_standard_derivatives');
  const VS = `precision highp float;
attribute vec2 gr; attribute float hh;
uniform vec4 cam; uniform vec4 prj; uniform vec2 vp; uniform vec4 geo; uniform float zoff; uniform float hs; uniform float spread; uniform float fitw;
varying vec3 wp; varying float vd; varying float vh; varying vec2 vg;
void main(){
  float z=geo.z+(gr.y+zoff)*geo.w, y=hh*hs;
  float x=(geo.x+gr.x*geo.y)*(fitw>0.?fitw*max(.2,prj.x+z*cam.z):1.+spread*abs(gr.y+zoff));   // fitw: each row spans the full frame width
  float x1=x*cam.x-z*cam.y, z1=x*cam.y+z*cam.x;
  float y2=y*cam.z+z1*cam.w, z2=-y*cam.w+z1*cam.z, d=max(.05,prj.x+z2);
  vec2 s=vec2(prj.z+x1*prj.y/d, prj.w-y2*prj.y/d);
  vec2 ndc=vec2(s.x/vp.x*2.-1., 1.-s.y/vp.y*2.);
  wp=vec3(x,y,z); vd=d; vh=hh; vg=vec2(gr.x,gr.y+zoff);
  gl_Position=vec4(ndc*d, (clamp(d/60.,0.,1.)*2.-1.)*d, d);
}`;
  const head = (deriv ? '#extension GL_OES_standard_derivatives : enable\n' : '#define NODERIV\n') + 'precision highp float;\n';
  const grids = new Map();
  T.glOrbit = {
    deriv, VS, head,
    prog: fs => T.GL.prog(head + fs, VS),
    grid(name, C, R) {
      let G = grids.get(name); if (G) return G;
      const n = C * R, gr = new Float32Array(n * 2), idx = new Uint16Array((C - 1) * (R - 1) * 6);
      for (let r = 0; r < R; r++) for (let c = 0; c < C; c++) { gr[(r * C + c) * 2] = c; gr[(r * C + c) * 2 + 1] = r; }
      let k = 0;
      for (let r = 0; r < R - 1; r++) for (let c = 0; c < C - 1; c++) { const a = r * C + c, b = a + 1, d = a + C, e = d + 1; idx[k++] = a; idx[k++] = d; idx[k++] = b; idx[k++] = b; idx[k++] = d; idx[k++] = e; }
      const bg = T.GL.buf(name + ':g', gr), bi = T.GL.buf(name + ':i', idx, true), bh = T.GL.buf(name + ':h', new Float32Array(n), false, true);
      G = {
        C, R, n: idx.length,
        set(h) { gl.bindBuffer(gl.ARRAY_BUFFER, bh); gl.bufferSubData(gl.ARRAY_BUFFER, 0, h); },
        // cam: T.orbit (after frame); vp: [w,h] CSS; geo: [x0, dx, z0, dz]; extra: other uniforms
        draw(P, cam, vp, geo, extra = {}) {
          if (!P) return;
          gl.useProgram(P.p);
          gl.enable(gl.DEPTH_TEST); gl.depthFunc(gl.LEQUAL); gl.enable(gl.BLEND); gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
          gl.bindBuffer(gl.ARRAY_BUFFER, bg); gl.enableVertexAttribArray(P.a.gr); gl.vertexAttribPointer(P.a.gr, 2, gl.FLOAT, false, 0, 0);
          gl.bindBuffer(gl.ARRAY_BUFFER, bh); gl.enableVertexAttribArray(P.a.hh); gl.vertexAttribPointer(P.a.hh, 1, gl.FLOAT, false, 0, 0);
          gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, bi);
          T.GL.set(P, Object.assign({ cam: [cam.cs, cam.sn, cam.cp, cam.sp], prj: [cam.D, cam.F, cam.cx, cam.cy], vp, geo, zoff: 0, hs: 1, spread: 0, fitw: 0, res: [T.GL.w, T.GL.h], k: T.GL.w / vp[0] }, extra));
          gl.drawElements(gl.TRIANGLES, this.n, gl.UNSIGNED_SHORT, 0);
          gl.disableVertexAttribArray(P.a.hh); gl.disableVertexAttribArray(P.a.gr); gl.disable(gl.DEPTH_TEST);
        },
      };
      grids.set(name, G); return G;
    },
    // '#rrggbb' -> [r,g,b] 0..1
    rgb: hex => { const n = parseInt(String(hex).slice(1), 16); return [(n >> 16) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255]; },
  };
})();

(() => {
const BANDS = ['315', '433.92', '868', '915'];
const lvl = v => T.clamp((v + 120 - 10) / 70);          // like wfColor on the stick
const ROWS = 36, BINS = 96;

T.views.push({
  id: 'sub', group: 'tools', title: 'Sub-GHz', subs: ['Spectrum', 'Monitor', 'Sensors'], sub: 0,
  want() { return this.sub === 2 ? 'sens' : `sub&sm=${this.sub}&sb=${this.band}`; },
  mount(main) {
    try { const b = localStorage.getItem('sub_band'); this.band = b != null ? +b : 1; } catch (e) { this.band = this.band ?? 1; }
    if (!(this.band >= 0 && this.band < 4)) this.band = 1;
    this.root = T.el('div', 'view');
    const st = T.el('div', 'stage'); this.root.appendChild(st);
    this.cv = T.canvas(st); main.appendChild(this.root);
    this.cam = T.orbit(0, 0.72, 3.3);
    this.drag = T.drag(this.cv.cv, this.cam, { minPitch: 0.35, maxPitch: 1.15 });
    this.drag.onTap = (x, y) => { const h = T.hits.at(x, y); if (h && h.k === 'band') this.setBand(h.i); };
    this.peak = new Float32Array(BINS).fill(-120);
    this.flash = 0; this.t = 0; this.chart = null;
  },
  unmount() { this.cv.dispose(); this.root.remove(); },
  reset() { S.subWf.length = 0; S.sub = null; this.peak.fill(-120); },
  onSub(i) { this.sub = i; this.reset(); },
  setBand(i) {
    if (i === this.band) return;
    this.band = i; try { localStorage.setItem('sub_band', i); } catch (e) {}
    this.reset(); T.app.rewant();
  },
  chips(g, w, u, m, y) {                                   // band selection - right under the header
    const cw = u * 5.2, chh = u * 1.9, gap = u * 0.4;
    let x = w - m - BANDS.length * cw - (BANDS.length - 1) * gap;
    T.txt(g, 'BAND MHZ', x - u * 0.8, y + chh * 0.66, u * 0.7, T.C.dim, 'm', 'right');
    BANDS.forEach((b, i) => {
      const on = i === this.band;
      T.cutPath(g, x, y, cw, chh, u * 0.5);
      g.fillStyle = on ? `rgba(${T.RGB.acc},.14)` : `rgba(${T.RGB.bg},.5)`; g.fill();
      g.strokeStyle = on ? T.C.acc : T.C.line; g.lineWidth = 1; g.stroke();
      T.txt(g, b, x + cw / 2, y + chh * 0.7, u * 1.0, on ? T.C.hot : T.C.label, 't', 'center');
      T.hits.rect(x, y - u * 0.3, cw, chh + u * 0.6, { k: 'band', i });
      x += cw + gap;
    });
  },
  frame(dt) {
    this.t += dt;
    const { g, w, h } = this.cv;
    T.hits.clear();
    T.backdrop(g, w, h, 0.5, 0.62);
    const u = T.unit(w, h), m = u * 1.6, y0 = 50;
    if (this.sub === 2) { this.sensors(g, w, h, u, m, y0, dt); T.finish(g, w, h); return; }
    const d = S.sub;
    const title = (this.sub ? 'Monitor ' : 'Spectrum ') + BANDS[this.band];
    if (!d || d.mode !== this.sub) {
      T.header(g, m, y0, w - 2 * m, u, title, 'waiting');
      this.chips(g, w, u, m, y0 + u * 3);
      T.emptyState(g, w, h, u, 'WAITING', 'starting CC1101 receiver');
      T.finish(g, w, h); return;
    }
    if (!d.ok) {
      T.header(g, m, y0, w - 2 * m, u, 'Sub-GHz', 'radio not found', true);
      T.emptyState(g, w, h, u, 'NO RADIO', 'CC1101 not found', true);
      T.finish(g, w, h); return;
    }
    if (this.sub === 0) this.spectrum(g, w, h, u, m, y0, dt, d, title);
    else this.monitor(g, w, h, u, m, y0, dt, d, title);
    T.finish(g, w, h);
  },

  // WebGL waterfall: mesh of up to ROWS_GL rows, color by height, glowing rows and peaks, fog into depth
  spectrumGL(g, w, h, cam, n, X, Z, HY) {
    const RG = 48, O = T.glOrbit;
    if (!this.glP) {
      this.glP = O.prog(`
uniform vec3 cBg, cAcc, cHot, cLine, cAlert; uniform float flash, rows;
varying vec3 wp; varying float vd; varying float vh; varying vec2 vg;
void main(){
  float v=clamp(vh/.75,0.,1.);
  float fog=clamp(1.-vg.y/rows*.9,.08,1.);
  float shade=.6;
#ifndef NODERIV
  vec3 nn=normalize(cross(dFdx(wp),dFdy(wp))); if(nn.y<0.) nn=-nn;
  shade=clamp(dot(nn,normalize(vec3(-.3,.8,-.5))),0.,1.);
  float aa=fwidth(vg.y), ax=fwidth(vg.x);
#else
  float aa=.05, ax=.05;
#endif
  float dr=abs(fract(vg.y+.5)-.5), lr=1.-smoothstep(aa*.6,aa*1.6,dr);
  float dc=abs(fract(vg.x/8.+.5)-.5)*8., lc=(1.-smoothstep(ax*.6,ax*1.6,dc))*.35;
  vec3 heat=v<.5?mix(cLine,cAcc,v*2.):mix(cAcc,cHot,(v-.5)*2.);
  vec3 col; float a=.94;
  col=cBg*.7+cAcc*.05*shade+cAcc*.07*v;
  col+=heat*((lr*(.35+.75*v)+lc*v)*fog+v*v*.35*fog);
  col+=cHot*pow(v,5.)*(1.2+flash)*fog;
  gl_FragColor=vec4(col*a,a);
}`);
      this.glH = new Float32Array(n * RG);
    }
    const G = O.grid('subwf' + n, n, RG);
    if (this.glN !== n) { this.glN = n; this.glH = new Float32Array(n * RG); this.glKey = null; }
    const rows = S.subWf;
    if (this.glKey !== S.subT) {
      this.glKey = S.subT;
      const H = this.glH;
      for (let r = 0; r < RG; r++) { const row = rows[r]; for (let i = 0; i < n; i++) H[r * n + i] = row ? HY * Math.pow(lvl(row[i] ?? -120), 1.2) : 0; }
      G.set(H);
    }
    T.floorGrid(g, cam, 1.8, 0.2);
    T.GL.begin(w, h);
    const C = T.C;
    G.draw(this.glP, cam, [w, h], [X(0), X(1) - X(0), Z(0), Z(1) - Z(0)], {
      cBg: O.rgb(C.bg), cAcc: O.rgb(C.acc), cHot: O.rgb(C.hot), cLine: O.rgb(C.line), cAlert: O.rgb(C.alert),
      flash: this.flash, rows: Math.min(RG, rows.length || 1),
    });
    T.GL.draw(g, 0, 0, w, h);
  },

  // ---------- SPECTRUM: 3D waterfall ----------
  spectrum(g, w, h, u, m, y0, dt, d, title) {
    const cam = this.cam, portrait = h > w * 1.05, cur = d.cur || [], n = cur.length || BINS;
    this.drag.tick(dt);
    cam.yaw = T.clamp(cam.yaw, -0.8, 0.8);
    cam.frame(w / 2, portrait ? h * 0.42 : h * 0.5, portrait ? w * 1.05 : Math.min(w * 0.7, h * 1.2));
    const X = i => (i / (n - 1) - 0.5) * 3.0, Z = r => -1.0 + r * (2.4 / ROWS), HY = 0.75;
    // peak hold (falls slowly)
    let mi = 0;
    for (let i = 0; i < n; i++) { this.peak[i] = Math.max(cur[i] ?? -120, this.peak[i] - dt * 6); if (cur[i] > cur[mi]) mi = i; }
    const pk = cur[mi] ?? -120, burst = pk > -70;
    this.flash = Math.max(burst ? 1 : 0, this.flash - dt * 1.5);
    // 'fake': terrain changes only with a new data row (~7 times/s) or camera rotation -
    // draw it into a separate layer and in other frames just paste the ready picture
    if (T.glOrbit && T.GL.ok) this.spectrumGL(g, w, h, cam, n, X, Z, HY);
    else {
    const rows = S.subWf, R = Math.min(ROWS, rows.length), cv = this.cv.cv;
    const key = `${S.subT}|${cam.yaw.toFixed(3)}|${cam.pitch.toFixed(3)}|${cv.width}x${cv.height}`;
    if (!this.layer) { this.layer = document.createElement('canvas'); this.lg = this.layer.getContext('2d'); }
    const fresh = key !== this.layerKey;
    if (fresh) {
      this.layerKey = key;
      if (this.layer.width !== cv.width || this.layer.height !== cv.height) { this.layer.width = cv.width; this.layer.height = cv.height; }
      const keep = g; g = this.lg;
      g.setTransform(1, 0, 0, 1, 0, 0); g.clearRect(0, 0, cv.width, cv.height); g.setTransform(this.cv.dpr, 0, 0, this.cv.dpr, 0, 0);
      T.floorGrid(g, cam, 1.8, 0.2);
    for (let r = R - 1; r >= 0; r--) {
      const row = rows[r], z = Z(r), fog = T.clamp(1 - r / ROWS * 0.85, 0.12, 1);
      const pts = new Array(n);
      for (let i = 0; i < n; i++) pts[i] = cam.p(X(i), HY * Math.pow(lvl(row[i] ?? -120), 1.2), z);
      const b0 = cam.p(X(0), 0, z), b1 = cam.p(X(n - 1), 0, z), top = cam.p(0, HY, z);
      g.beginPath(); g.moveTo(b0[0], b0[1]); for (const p of pts) g.lineTo(p[0], p[1]); g.lineTo(b1[0], b1[1]); g.closePath();
      g.fillStyle = 'rgba(5,9,14,.9)'; g.fill();
      let stroke;
      if (r < 8) {                                         // near rows - color by height, far ones - one color (cheaper)
        stroke = g.createLinearGradient(0, b0[1], 0, top[1]);
        stroke.addColorStop(0, T.heat(0.12, 0.5 * fog)); stroke.addColorStop(0.3, T.heat(0.45, 0.85 * fog)); stroke.addColorStop(0.65, T.heat(0.8, fog)); stroke.addColorStop(1, T.heat(1, fog));
      } else stroke = T.heat(0.3 + 0.45 * fog, 0.35 + 0.6 * fog);
      g.beginPath(); pts.forEach((p, i) => i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]));
      g.strokeStyle = stroke; g.lineWidth = r === 0 ? 2.2 : 1.1; g.stroke();
    }
      g = keep;
    }
    g.save(); g.setTransform(1, 0, 0, 1, 0, 0); g.drawImage(this.layer, 0, 0); g.restore();
    }
    // front line + peaks
    if (cur.length) {
      const z = Z(0) - 0.02;
      g.beginPath();
      for (let i = 0; i < n; i++) { const p = cam.p(X(i), HY * Math.pow(lvl(cur[i]), 1.2), z); i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]); }
      g.strokeStyle = T.C.hot; g.lineWidth = 2; g.stroke();
      g.fillStyle = `rgba(${T.RGB.hot},.75)`;
      for (let i = 0; i < n; i += 2) { const p = cam.p(X(i), HY * Math.pow(lvl(this.peak[i]), 1.2), z); g.fillRect(p[0] - 1, p[1] - 1, 2, 2); }
    }
    // frequency axis
    const zF = Z(0) - 0.15;
    for (let k = 0; k <= 8; k++) {
      const i = k / 8 * (n - 1), p = cam.p(X(i), 0, zF), f = d.lo + (d.hi - d.lo) * k / 8;
      g.fillStyle = k % 4 ? T.C.dim : T.C.acc; g.fillRect(p[0], p[1] - u * 0.3, 1, u * 0.6);
      if (k % 2 === 0) T.txt(g, f.toFixed(2), p[0], p[1] + u * 1.2, u * (k % 4 ? 0.65 : 0.8), k % 4 ? T.C.label : T.C.hot, 'm', 'center');
    }
    T.bloom(this.cv, 0.8 + 0.5 * this.flash);
    // peak frequency callout
    const fpk = d.lo + (d.hi - d.lo) * mi / (n - 1);
    if (cur.length) {
      const p = cam.p(X(mi), HY * Math.pow(lvl(pk), 1.2), Z(0));
      const lx = T.clamp(p[0] + u * 3, u * 2, w - u * 15), ly = Math.max(y0 + u * 6, p[1] - u * 5);
      T.diamond(g, p[0], p[1], u * 0.6, burst ? T.C.alert : T.C.hot, false);
      g.strokeStyle = burst ? T.C.alert : `rgba(${T.RGB.hot},.7)`; g.lineWidth = 1;
      g.beginPath(); g.moveTo(p[0] + u * 0.5, p[1] - u * 0.5); g.lineTo(lx, ly); g.lineTo(lx + u * 10, ly); g.stroke();
      T.txt(g, fpk.toFixed(3) + ' MHZ', lx, ly - u * 0.5, u * 1.25, burst ? T.C.alert : T.C.hot, 't');
      T.txt(g, pk + ' DBM' + (burst ? '  · BURST' : ''), lx, ly + u * 1.1, u * 0.75, burst ? T.C.alert : T.C.label);
    }
    T.header(g, m, y0, w - 2 * m, u, title, `peak ${fpk.toFixed(2)} MHz  ${pk} dBm`, burst);
    this.chips(g, w, u, m, y0 + u * 3);
    // color scale
    const sx = m, sy = y0 + u * 3.3, sw = u * 9;
    for (let i = 0; i < 40; i++) { g.fillStyle = T.heat(i / 39); g.fillRect(sx + i * sw / 40, sy, sw / 40 + 0.5, u * 0.45); }
    T.txt(g, '-110', sx, sy + u * 1.4, u * 0.62, T.C.dim); T.txt(g, '-40 DBM', sx + sw, sy + u * 1.4, u * 0.62, T.C.dim, 'm', 'right');
    T.footer(g, m, h - u * 2.2, w - 2 * m, u, 'drag: orbit  ·  time flows into depth', `${Math.min(T.glOrbit && T.GL.ok ? 48 : ROWS, S.subWf.length)} rows · receive only`);
  },

  // ---------- MONITOR: level and bursts ----------
  monitor(g, w, h, u, m, y0, dt, d, title) {
    const portrait = h > w * 1.05, burst = !!d.burst, rssi = d.rssi || [];
    this.flash = Math.max(burst ? 1 : 0, this.flash - dt * 1.2);
    T.header(g, m, y0, w - 2 * m, u, title, `${(d.mon || 0).toFixed(2)} MHz · floor ${d.floor} dBm · ${d.events || 0} events`, burst);
    this.chips(g, w, u, m, y0 + u * 3);
    const top = y0 + u * 6, bot = h - u * 3;
    const cx = m, cw = portrait ? w - 2 * m : (w - 2 * m) * 0.66, ch = portrait ? (bot - top) * 0.6 : bot - top;
    // graph
    T.panel(g, cx, top, cw, ch, u);
    const L = cx + u * 3.4, Rr = cx + cw - u * 1.2, Tp = top + u * 4.2, B = top + ch - u * 1.6;
    const Y = v => B - T.clamp((v + 110) / 80) * (B - Tp);
    g.lineWidth = 1;
    for (let v = -110; v <= -30; v += 10) { g.strokeStyle = v % 20 ? `rgba(${T.RGB.line},.5)` : T.C.line; g.beginPath(); g.moveTo(L, Y(v)); g.lineTo(Rr, Y(v)); g.stroke(); if (v % 20 === 0) T.txt(g, v, L - u * 0.5, Y(v) + u * 0.3, u * 0.65, T.C.dim, 'm', 'right'); }
    // threshold (floor + margin) dashed
    if (d.floor != null) {
      g.setLineDash([u * 0.5, u * 0.4]); g.strokeStyle = `rgba(${T.RGB.label},.6)`;
      g.beginPath(); g.moveTo(L, Y(d.floor)); g.lineTo(Rr, Y(d.floor)); g.stroke(); g.setLineDash([]);
      T.txt(g, 'FLOOR', Rr, Y(d.floor) - u * 0.35, u * 0.6, T.C.label, 'm', 'right');
    }
    // line smoothing between packets
    if (!this.chart || this.chart.length !== rssi.length) this.chart = Float32Array.from(rssi);
    for (let i = 0; i < rssi.length; i++) this.chart[i] = T.lerp(this.chart[i], rssi[i], T.ease(dt, 0.15));
    if (this.chart.length > 1) {
      const N = this.chart.length, px = i => L + i / (N - 1) * (Rr - L);
      g.beginPath(); for (let i = 0; i < N; i++) { const yy = Y(this.chart[i]); i ? g.lineTo(px(i), yy) : g.moveTo(px(i), yy); }
      g.strokeStyle = burst ? T.C.alert : T.C.acc; g.lineWidth = 2; g.stroke();
      g.lineTo(Rr, B); g.lineTo(L, B); g.closePath();
      const gr = g.createLinearGradient(0, Tp, 0, B); gr.addColorStop(0, burst ? `rgba(${T.RGB.alert},.3)` : `rgba(${T.RGB.acc},.28)`); gr.addColorStop(1, `rgba(${T.RGB.acc},0)`);
      g.fillStyle = gr; g.fill();
      const last = this.chart[N - 1];
      T.diamond(g, Rr, Y(last), u * 0.45, burst ? T.C.alert : T.C.hot, true);
    }
    T.bloom(this.cv, 0.55 + 0.6 * this.flash);
    // big value and banner
    const last = rssi.length ? rssi[rssi.length - 1] : -120;
    T.txt(g, '// LEVEL', cx + u, top + u * 1.6, u * 1.0, T.C.acc, 't');
    T.bigNum(g, last, cx + u * 6.5, top + u * 3.3, u * 2.8, burst ? T.C.alert : T.heat(0.3 + 0.7 * lvl(last)), 'DBM');
    {
      const bw = u * 13, bh = u * 2.4, bx = cx + cw - bw - u, by = top + u * 0.9, pulse = burst ? 0.6 + 0.4 * Math.sin(this.t * 10) : 1;
      T.cutPath(g, bx, by, bw, bh, u * 0.6);
      g.fillStyle = burst ? `rgba(${T.RGB.alert},${0.22 * pulse})` : `rgba(${T.RGB.acc},.06)`; g.fill();
      g.strokeStyle = burst ? T.C.alert : T.C.line; g.lineWidth = 1.2; g.stroke();
      T.txt(g, burst ? '// BURST' : '// LISTENING', bx + bw / 2, by + bh * 0.68, u * 1.15, burst ? T.C.alert : T.C.label, 't', 'center');
    }
    // event list
    const ex = portrait ? m : cx + cw + u, ey = portrait ? top + ch + u : top, ew = portrait ? w - 2 * m : w - m - ex, eh = portrait ? bot - ey : ch;
    T.panel(g, ex, ey, ew, eh, u);
    T.txt(g, '// EVENTS', ex + u, ey + u * 1.6, u * 1.0, T.C.acc, 't');
    T.txt(g, d.events || 0, ex + ew - u, ey + u * 1.8, u * 1.8, T.C.hot, 't', 'right');
    let yy = ey + u * 3.4;
    const evs = d.ev || [];
    if (!evs.length) T.txt(g, 'NO BURSTS YET', ex + u, yy + u, u * 0.8, T.C.dim);
    for (const e of evs) {
      if (yy + u * 2.6 > ey + eh) break;
      const v = lvl(e.pk);
      g.fillStyle = T.C.line; g.fillRect(ex + u, yy + u * 2.3, ew - 2 * u, 1);
      T.txt(g, T.ago(e.ago) + ' AGO', ex + u, yy + u * 0.9, u * 0.75, T.C.label);
      T.txt(g, e.ms + ' MS', ex + u, yy + u * 1.9, u * 0.65, T.C.dim);
      T.segBar(g, ex + ew * 0.42, yy + u * 0.8, ew * 0.3, u * 0.45, v, 8, false, e.pk > -55);
      T.txt(g, e.pk, ex + ew - u, yy + u * 1.6, u * 1.4, e.pk > -55 ? T.C.alert : T.heat(0.3 + 0.7 * v), 't', 'right');
      yy += u * 2.6;
    }
    T.footer(g, m, h - u * 2.2, w - 2 * m, u, 'level at band frequency · tap band to switch', 'receive only');
  },
  // ---------- SENSORS: 433 sensors (thermometers, doorbells, remotes) + packet analyzer ----------
  sensors(g, w, h, u, m, y0, dt) {
    const d = S.sens, now = performance.now(), portrait = h > w * 1.05;
    if (!d) {
      T.header(g, m, y0, w - 2 * m, u, 'Sensors 433.92', 'waiting');
      T.emptyState(g, w, h, u, 'WAITING', 'starting the 433 MHz decoder');
      return;
    }
    const fresh = T.clamp(1 - (now - S.sensPkT) / 700);
    if (S.sensPkT !== this.pkSeen) { this.pkSeen = S.sensPkT; this.rev = 0; }
    this.rev = Math.min(1, (this.rev ?? 1) + dt / 0.6);
    T.header(g, m, y0, w - 2 * m, u, 'Sensors 433.92', `${d.pk || 0} packets · noise ${d.noise || 0}`, false);
    const top = y0 + u * 3.6, bot = h - u * 3;
    const list = d.sensors || [];
    // table
    const tw = w - 2 * m, rowH = u * 3.1, th = portrait ? Math.min((bot - top) * 0.55, u * 4 + rowH * Math.max(3, list.length)) : Math.min((bot - top) * 0.52, u * 4 + rowH * Math.max(3, list.length));
    T.panel(g, m, top, tw, th, u);
    const cols = [0.02, 0.32, 0.5, 0.63, 0.73, 0.9];                 // name, temp, humidity, battery, signal, when
    const X = k => m + tw * cols[k];
    T.txt(g, '// SENSORS', X(0), top + u * 1.8, u * 1.0, T.C.acc, 't');
    ['TEMP', 'HUM', 'BAT', 'SIGNAL', 'SEEN'].forEach((s, i) => T.txt(g, s, X(i + 1), top + u * 1.8, u * 0.75, T.C.dim));
    let y = top + u * 3.2;
    if (!list.length) {
      T.txt(g, 'NOTHING HEARD YET', X(0), y + u * 1.4, u * 1.1, T.C.label, 't');
      T.txt(g, 'OUTDOOR THERMOMETERS (NEXUS, LACROSSE, ACURITE, BRESSER...), DOORBELLS, REMOTES', X(0), y + u * 2.8, u * 0.75, T.C.dim);
    }
    for (const r of list) {
      if (y + rowH > top + th) break;
      const stale = r.ago > 600, rem = r.kind === 'remote' || r.kind === 'rolling';
      g.globalAlpha = stale ? 0.45 : 1;
      if (r.ago < 2) { g.fillStyle = `rgba(${T.RGB.acc},.10)`; g.fillRect(m + u * 0.5, y, tw - u, rowH - u * 0.3); }
      const nm = rem ? (r.kind === 'rolling' ? 'CAR KEY' : 'REMOTE / BELL') : String(r.p).toUpperCase();
      g.font = `600 ${u * 1.2}px ${T.FT}`; T.txt(g, T.fit(g, nm, tw * 0.28), X(0), y + u * 1.35, u * 1.2, T.C.hot, 't');
      T.txt(g, rem ? '#' + String(r.id).split('-').pop() + (r.btn != null ? '  BTN ' + r.btn : '') : 'CH ' + r.ch + '  ·  ' + r.n + '×', X(0), y + u * 2.5, u * 0.7, T.C.label);
      if (r.t != null) { const ts = (r.t > 0 ? '+' : '') + r.t.toFixed(1); T.txt(g, ts, X(1), y + u * 2.3, u * 2.1, T.C.hot, 't'); T.txt(g, '°C', X(1) + T.tw(g, ts, u * 2.1, 't') + u * 0.3, y + u * 2.2, u * 0.8, T.C.label); }
      else T.txt(g, '—', X(1), y + u * 2, u * 1.2, T.C.dim, 't');
      T.txt(g, r.h != null ? r.h + '%' : '—', X(2), y + u * 2, u * 1.4, r.h != null ? T.C.hot : T.C.dim, 't');
      T.txt(g, r.bat == null ? '—' : r.bat ? 'OK' : 'LOW', X(3), y + u * 2, u * 1.1, r.bat === 0 ? T.C.alert : r.bat ? T.C.label : T.C.dim, 't');
      T.segBar(g, X(4), y + u * 1.1, tw * 0.1, u * 0.55, T.pct(r.rssi), 8, false);
      T.txt(g, r.rssi + ' DBM', X(4), y + u * 2.6, u * 0.65, T.C.dim);
      T.txt(g, T.ago(r.ago), X(5), y + u * 2, u * 1.1, r.ago < 60 ? T.C.hot : T.C.label, 't');
      g.globalAlpha = 1;
      g.fillStyle = T.C.line; g.fillRect(m + u, y + rowH - u * 0.2, tw - 2 * u, 1);
      y += rowH;
    }
    // analyzer of the last packet
    const ay = top + th + u, ah = bot - ay, L = d.last;
    T.panel(g, m, ay, tw, ah, u);
    if (fresh > 0) { T.cutPath(g, m, ay, tw, ah, u * 0.8); g.strokeStyle = `rgba(${T.RGB.acc},${0.6 * fresh})`; g.lineWidth = 2; g.stroke(); }
    T.txt(g, '// LAST PACKET', m + u * 1.4, ay + u * 1.8, u * 1.0, T.C.acc, 't');
    if (!L) { T.txt(g, 'NO PACKETS YET', m + u * 1.4, ay + u * 4, u * 0.9, T.C.dim); }
    else {
      const cls = { sensor: 'SENSOR', remote: 'REMOTE / BELL', rolling: 'ROLLING CODE', unknown: 'UNKNOWN', noise: 'NOISE' }[L.cls] || String(L.cls || '?').toUpperCase();
      T.txt(g, `${L.p && L.p !== '?' && L.cls === 'sensor' ? L.p + ' · ' : ''}${cls}  ·  ${T.ago(L.ago ?? 0)} AGO  ·  ${L.rssi} DBM`, m + tw - u * 1.4, ay + u * 1.8, u * 0.85, L.cls === 'sensor' ? T.C.hot : T.C.label, 'm', 'right');
      const fx = m + u * 1.4, fw = portrait ? tw - u * 2.8 : tw * 0.3, fy = ay + u * 3.6;
      const rows = [['MOD', L.mod], ['BITS', L.bits + (L.rows > 1 ? '  ×' + L.rows : '')], ['PULSES', L.pulses], ['PULSE', L.short === L.long ? L.short + ' µS' : L.short + ' / ' + L.long + ' µS'], ['GAP', L.gs + ' / ' + L.gl + ' µS'], ['SYNC', L.gap + ' µS']];
      if (L.hex) rows.push(['HEX', L.hex.toUpperCase()]); else if (L.hash) rows.push(['ID', '#' + L.hash + '  (code not shown)']);
      const per = portrait ? 2 : 1, rh = u * 1.55;
      rows.forEach(([k, v], i) => {
        const cx = fx + (i % per) * (fw / per), cy = fy + Math.floor(i / per) * rh;
        if (cy > ay + ah - u * 0.8) return;
        T.txt(g, k, cx, cy, u * 0.72, T.C.dim);
        g.font = `600 ${u * 1.05}px ${T.FT}`; T.txt(g, T.fit(g, String(v), fw / per - u * 4.5), cx + u * 4.2, cy + u * 0.05, u * 1.05, T.C.hot, 't');
      });
      const wx = portrait ? fx : fx + fw + u, wy = portrait ? fy + Math.ceil(rows.length / per) * rh : fy - u * 1.2, ww = portrait ? tw - u * 2.8 : m + tw - u * 1.4 - wx, wh = ay + ah - wy - u * 1.2;
      if (wh > u * 2) this.pulses(g, L.pt || [], wx, wy, ww, wh, u, this.rev, L.cls === 'sensor');
    }
    T.bloom(this.cv, 0.5 + 0.5 * fresh);
    T.footer(g, m, h - u * 2.2, w - 2 * m, u, 'outdoor sensors feed radio weather · remotes show only a fingerprint', 'receive only');
  },
  // packet: [pulse, space, ...] us - square wave with width labels
  pulses(g, pt, x, y, w, h, u, rev, sensor) {
    g.fillStyle = `rgba(${T.RGB.bg2},.5)`; g.fillRect(x, y, w, h);
    g.strokeStyle = T.C.line; g.lineWidth = 1;
    for (let k = 1; k < 10; k++) { g.beginPath(); g.moveTo(x + w * k / 10, y); g.lineTo(x + w * k / 10, y + h); g.stroke(); }
    if (!pt.length) return;
    let tot = 0; for (const v of pt) tot += v;
    const hi = y + h * 0.2, lo = y + h * 0.8, lim = x + w * rev;
    g.beginPath(); g.moveTo(x, lo);
    let cx = x;
    for (let i = 0; i < pt.length && cx < lim; i++) { const nx = Math.min(lim, cx + pt[i] / tot * w), yy = i % 2 ? lo : hi; g.lineTo(cx, yy); g.lineTo(nx, yy); cx = nx; }
    g.lineTo(cx, lo);
    g.strokeStyle = sensor ? T.C.acc : T.C.label; g.lineWidth = 2; g.stroke();
    if (rev < 1) { g.fillStyle = T.C.hot; g.fillRect(lim - 1, y, 2, h); }
    T.txt(g, (tot / 1000).toFixed(1) + ' MS SHOWN', x + w - u * 0.4, y + h - u * 0.4, u * 0.65, T.C.dim, 'm', 'right');
  },
});
})();
