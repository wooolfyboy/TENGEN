// ---- BLUETOOTH: Evangelion MAGI × Zen Sanctuary Scope & Target Dossier ----
// Shared helpers (T.bleUI) are also used by HQ and PC views.
const BU = T.bleUI = {};
BU.G = { phone: 'P', audio: 'A', watch: 'W', tracker: 'T', computer: 'C', TV: 'V', tv: 'V', sensor: 'S', band: 'B', input: 'G', beacon: 'i', unknown: '?' };
BU.glyph = e => BU.G[e.type] || (e.type ? String(e.type)[0].toUpperCase() : '?');
BU.ang = id => (T.hash(id) % 360) * Math.PI / 180;
BU.rad = rs => 0.09 + 0.87 * (1 - T.pct(rs));          // strong = closer to center
BU.list = () => [...S.dev.values()].filter(e => e.h > 0.02 && e.rs != null);
BU.label = e => e.label || e.name || (e.type && e.type !== 'unknown' ? e.type : 'device');
BU.tr = e => e.type === 'tracker';
BU.rgb = (e, v) => BU.tr(e) ? T.ALERT : T.heatRGB(v);
BU.crossed = (a, b, x) => b >= a ? (x > a && x <= b) : (x > a || x <= b);
BU.dist = (r, tx) => {
  const ref = (tx != null && tx >= -40 && tx <= 20) ? T.clamp(tx - 58, -76, -44) : -59;
  const d = Math.pow(10, (ref - r) / 22.5);
  return d < 10 ? d.toFixed(1) : String(Math.round(d));
};
// Compact decoded passive-intel line (used in rows, cards & detail strip; always clipped by T.fit)
BU.intelLine = e => {
  const parts = [];
  const f = e.flags || 0;
  if (f & 4) parts.push('! ' + T.tr('SEPARATED'));
  if (e.info && !(f & 4)) parts.push(T.tr(e.info));
  else if (e.bat >= 0 && !(e.info && /L\d|R\d|C\d/.test(e.info))) parts.push(e.bat + '%');
  if (f & 1) parts.push(T.tr('ACTIVE'));
  if (f & 2) parts.push(T.tr('MOVING'));
  return parts.join(' · ');
};
BU.apiBase = () => T.apiBase();
BU.hold = (ms = 1500) => ({ v: null, c: null, t: 0, set(x, now) { if (this.v == null) { this.v = x; return x; } if (x === this.v) { this.c = null; return x; } if (x !== this.c) { this.c = x; this.t = now; } else if (now - this.t > ms) { this.v = x; this.c = null; } return this.v; } });

// beam: step and marks (ping = time the beam passed the target)
BU.sweeper = (period = 3.2) => ({
  a: 0, pings: new Map(),
  tick(dt, t) {
    const prev = this.a; this.a = (this.a + dt * T.TAU / (T.reduce ? 7 : period)) % T.TAU;
    for (const e of S.dev.values()) if (e.alive && BU.crossed(prev, this.a, BU.ang(e.id))) this.pings.set(e.id, t);
  },
  ping(id, t) { const p = this.pings.get(id); return p == null ? 9 : t - p; },
});

// Chamfered Evangelion HUD plate helper
BU.cutPlate = (g, x, y, w, h, cut, fill, stroke, lw = 1) => {
  const c = Math.min(cut, w * 0.22, h * 0.35);
  g.beginPath();
  g.moveTo(x + c, y); g.lineTo(x + w, y);
  g.lineTo(x + w, y + h - c); g.lineTo(x + w - c, y + h);
  g.lineTo(x, y + h); g.lineTo(x, y + c); g.closePath();
  if (fill) { g.fillStyle = fill; g.fill(); }
  if (stroke) { g.strokeStyle = stroke; g.lineWidth = lw; g.stroke(); }
};

// ---- WebGL Sanctuary Radar Floor (Enso rings + AT-Field octagram + temple water ripples) ----
BU.glScope = (() => {
  let PG = null, failed = false;
  const VG = `
attribute vec3 pos;uniform vec4 cam1;uniform vec4 cam2;uniform vec2 scr;varying vec3 vuv;varying float vdep;
void main(){vec3 p=pos;float x1=p.x*cam1.x-p.z*cam1.y,z1=p.x*cam1.y+p.z*cam1.x;float y2=p.y*cam1.z+z1*cam1.w,z2=-p.y*cam1.w+z1*cam1.z;
 float wv=max(.05,cam2.x+z2);
 gl_Position=vec4((2.*cam2.z/scr.x-1.)*wv+2.*x1*cam2.y/scr.x,(1.-2.*cam2.w/scr.y)*wv+2.*y2*cam2.y/scr.y,((wv-.05)/12.*2.-1.)*wv,wv);
 vuv=pos;vdep=wv;}`;
  const FG = `#extension GL_OES_standard_derivatives : enable
precision highp float;
varying vec3 vuv;varying float vdep;
uniform float t;uniform float sweep;uniform float trk;uniform float fogA;uniform float fogB;
float line(float d,float w){float f=fwidth(d);return 1.-smoothstep(w,w+f*1.45,abs(d));}
void main(){
 vec2 p=vuv.xz;float r=length(p),ang=atan(p.y,p.x);
 float edge=smoothstep(1.34,1.02,r);
 if(edge<=0.)discard;
 // Obsidian temple pond + calm concentric water harmonics
 vec3 col=vec3(.006,.016,.024)+vec3(.008,.028,.040)*exp(-r*1.8);
 float pond=sin(r*38.-t*2.1)*.5+.5;
 col+=vec3(.012,.045,.060)*pond*exp(-r*1.65)*.38;
 float gl=0.;
 // 4 concentric Enso brush arcs with organic gaps
 for(int i=1;i<=4;i++){
  float rr=float(i)*.25;
  float dir=(i==2||i==4)?-1.:1.;
  float aRel=mod(ang-t*.11*dir+float(i)*1.3,6.2831853);
  float enso=smoothstep(.22,.65,aRel)*smoothstep(6.18,5.65,aRel);
  gl+=line(r-rr,i==4?.0032:.0018)*(i==4?.88:.44)*(i==4?1.:(.35+.65*enso));
 }
 // Inscribed 8-fold Dharma / AT-Field octagram
 float a8=mod(ang+.3926991,.7853982)-.3926991;
 float octR=1.035*cos(.3926991)/max(.35,cos(a8));
 gl+=line(r-octR,.0026)*.65;
 // 8 sacred spokes
 float sp=mod(ang,.7853982)-.3926991;
 float card=step(abs(sin(ang*2.)),.08);
 gl+=line(sp*r,.0015)*(.24+.24*card)*step(.11,r)*step(r,1.04);
 // Phosphor sweep wake on the pond
 float da=mod(sweep-ang+6.2831853,6.2831853);
 float wake=exp(-da*2.35)*step(r,1.01)*step(.04,r);
 float lead=exp(-da*36.)*step(r,1.02);
 // Outward sonar ripple
 float wv=fract(t*.22),rw=wv*1.04;
 float rip=exp(-abs(r-rw)*34.)*(1.-wv)*.48;
 vec3 tint=trk>.5?vec3(.95,.42,.14):vec3(.16,.72,.84);
 col+=vec3(.16,.68,.80)*(gl*.58+rip*.75)+tint*(wake*.24+lead*.55);
 float a=(.86*edge)*(1.-smoothstep(fogA,fogB,vdep)*.76);
 gl_FragColor=vec4(col*a,a);}`;
  const init = () => {
    if (PG || failed) return !!PG;
    PG = T.GL.prog(FG, VG);
    if (!PG) { failed = true; return false; }
    const gd = [], N = 28, R = 1.42;
    for (let i = 0; i < N; i++) for (let j = 0; j < N; j++) {
      const x0 = -R + 2 * R * i / N, x1 = -R + 2 * R * (i + 1) / N, z0 = -R + 2 * R * j / N, z1 = -R + 2 * R * (j + 1) / N;
      gd.push(x0, 0, z0, x1, 0, z0, x1, 0, z1, x0, 0, z0, x1, 0, z1, x0, 0, z1);
    }
    T.GL.buf('scopeGround', new Float32Array(gd)); PG.n = gd.length / 3;
    return true;
  };
  return {
    ok: () => T.GL && T.GL.ok && init(),
    render(g, cam, W, H, t, sweep, trk) {
      if (!this.ok()) return false;
      const GL = T.GL, gl = GL.gl;
      GL.begin(W, H, 0.85);
      gl.disable(gl.DEPTH_TEST);
      gl.enable(gl.BLEND); gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
      gl.useProgram(PG.p); GL.buf('scopeGround');
      const lp = PG.a.pos; gl.enableVertexAttribArray(lp); gl.vertexAttribPointer(lp, 3, gl.FLOAT, false, 0, 0);
      GL.set(PG, {
        cam1: [cam.cs, cam.sn, cam.cp, cam.sp],
        cam2: [cam.D, cam.F, cam.cx, cam.cy],
        scr: [W, H], t, sweep, trk: trk ? 1 : 0, fogA: cam.D + 0.35, fogB: cam.D + 2.4,
      });
      gl.drawArrays(gl.TRIANGLES, 0, PG.n);
      gl.disableVertexAttribArray(lp);
      GL.draw(g, 0, 0, W, H);
      return true;
    },
  };
})();

BU.drawOrigin = (g, cam, u, t, active) => {
  const o0 = cam.p(0, 0, 0), oTop = cam.p(0, 0.36 + 0.03 * Math.sin(t * 1.8), 0);
  // Rotating incomplete Enso ring at base
  const r0 = 0.125 + 0.01 * Math.sin(t * 1.5);
  g.strokeStyle = `rgba(${T.RGB.acc},.58)`; g.lineWidth = 1.4; g.beginPath();
  for (let i = 0; i <= 29; i++) {
    const a = i / 36 * T.TAU + t * 0.24, q = cam.p(Math.cos(a) * r0, 0.003, Math.sin(a) * r0);
    i ? g.lineTo(q[0], q[1]) : g.moveTo(q[0], q[1]);
  }
  g.stroke();
  // Axis Mundi vertical light needle
  const gr = g.createLinearGradient(o0[0], o0[1], oTop[0], oTop[1]);
  gr.addColorStop(0, `rgba(${T.RGB.acc},.72)`); gr.addColorStop(0.65, `rgba(${T.RGB.hot},.32)`); gr.addColorStop(1, `rgba(${T.RGB.hot},0)`);
  g.strokeStyle = gr; g.lineWidth = 1.6; g.beginPath(); g.moveTo(o0[0], o0[1]); g.lineTo(oTop[0], oTop[1]); g.stroke();
  // Floating 3D Ramiel / Dharma octahedron around the Eye
  const cy = 0.14 + 0.018 * Math.sin(t * 1.7), cr = 0.065, rot = t * 0.65;
  const topV = cam.p(0, cy + cr * 1.35, 0), botV = cam.p(0, cy - cr * 1.15, 0);
  const eq = [0, 1, 2, 3].map(k => { const a = rot + k * Math.PI / 2; return cam.p(Math.cos(a) * cr, cy, Math.sin(a) * cr); });
  g.strokeStyle = `rgba(${T.RGB.hot},.55)`; g.lineWidth = 1.1; g.beginPath();
  for (let k = 0; k < 4; k++) {
    const q = eq[k], qn = eq[(k + 1) & 3];
    g.moveTo(q[0], q[1]); g.lineTo(qn[0], qn[1]);
    g.moveTo(topV[0], topV[1]); g.lineTo(q[0], q[1]);
    g.moveTo(botV[0], botV[1]); g.lineTo(q[0], q[1]);
  }
  g.stroke();
  const sz = Math.max(u * 1.38, 15);
  T.drawSealChar(g, o0[0] - sz / 2, o0[1] - sz / 2, sz, active ? '眼' : '静', T.C.acc, false);
};

// 3D Hexagonal Evangelion AT-Field containment lock around selected / tracker targets
BU.drawLock = (g, cam, x, z, h, r, col, t, strong) => {
  const hex = (yy, rr, rot, lw, alpha) => {
    g.save(); g.globalAlpha = alpha; g.strokeStyle = col; g.lineWidth = lw; g.beginPath();
    for (let k = 0; k <= 6; k++) {
      const a = rot + k * T.TAU / 6, q = cam.p(x + Math.cos(a) * rr, yy, z + Math.sin(a) * rr);
      k ? g.lineTo(q[0], q[1]) : g.moveTo(q[0], q[1]);
    }
    g.stroke(); g.restore();
  };
  hex(0.005, r, t * (strong ? 0.85 : 0.4), strong ? 1.7 : 1.15, strong ? 0.92 : 0.68);
  if (strong) {
    hex(Math.max(0.06, h), r * 0.72, -t * 0.65, 1.25, 0.78);
    const p0 = cam.p(x, h, z), p1 = cam.p(x, h + 0.28, z);
    const gr = g.createLinearGradient(p0[0], p0[1], p1[0], p1[1]);
    gr.addColorStop(0, col); gr.addColorStop(1, `rgba(${T.RGB.hot},0)`);
    g.strokeStyle = gr; g.lineWidth = 1.5; g.beginPath(); g.moveTo(p0[0], p0[1]); g.lineTo(p1[0], p1[1]); g.stroke();
  }
};

// ---- Perspective Sanctuary Radar: Enso disc, AT-Field octagram, sweep curtain, light pillars ----
// opt: {sel, u, t, labels (how many to label), hits, W, H}
BU.scene = (g, cam, sw, opt) => {
  const { u, t } = opt;
  const list = BU.list(), rank = [...list].sort((a, b) => b.rs - a.rs);
  const hasTrk = list.some(e => BU.tr(e) || ((e.flags || 0) & 4));
  const useGL = opt.W && opt.H && BU.glScope.render(g, cam, opt.W, opt.H, t, sw.a, hasTrk);

  if (!useGL) {
    T.floorGrid(g, cam, 1.55, 0.22);
    for (let i = 1; i <= 4; i++) {
      g.beginPath();
      const span = i === 4 ? 96 : 82, rot = (i % 2 ? 1 : -1) * t * 0.12 + i;
      for (let k = 0; k <= span; k++) {
        const a = rot + k / 96 * T.TAU, p = cam.p(Math.cos(a) * i / 4, 0, Math.sin(a) * i / 4);
        k ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]);
      }
      g.strokeStyle = i === 4 ? T.C.acc : `rgba(${T.RGB.dim},.52)`; g.lineWidth = i === 4 ? 1.6 : 1.05; g.stroke();
    }
    // 2D Octagram fallback
    g.strokeStyle = `rgba(${T.RGB.acc},.42)`; g.lineWidth = 1.1; g.beginPath();
    for (let k = 0; k <= 8; k++) {
      const a = k * Math.PI / 4, p = cam.p(Math.cos(a) * 1.04, 0, Math.sin(a) * 1.04);
      k ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]);
    }
    g.stroke();
    // 2D Floor sweep wedge
    const tail = 1.35, N = 24, o = cam.p(0, 0, 0), s = sw.a;
    for (let i = 0; i < N; i++) {
      const a0 = s - tail * (1 - i / N), a1 = s - tail * (1 - (i + 1) / N), v = (i + 1) / N;
      const p = cam.p(Math.cos(a0), 0, Math.sin(a0)), q = cam.p(Math.cos(a1), 0, Math.sin(a1));
      g.fillStyle = T.heat(0.2 + 0.65 * v, 0.28 * v * v);
      g.beginPath(); g.moveTo(o[0], o[1]); g.lineTo(p[0], p[1]); g.lineTo(q[0], q[1]); g.closePath(); g.fill();
    }
  }

  // Outer 72-tick NERV precision azimuth bezel + rotating outer calligraphic Enso arc
  g.strokeStyle = T.C.acc; g.lineWidth = 1; g.beginPath();
  for (let a = 0; a < 360; a += 5) {
    const r = a * Math.PI / 180, maj = a % 45 === 0, med = a % 15 === 0;
    const r0 = 1.02, r1 = maj ? 1.115 : med ? 1.075 : 1.048;
    const p = cam.p(Math.cos(r) * r0, 0, Math.sin(r) * r0), q = cam.p(Math.cos(r) * r1, 0, Math.sin(r) * r1);
    g.moveTo(p[0], p[1]); g.lineTo(q[0], q[1]);
  }
  g.stroke();
  // Outer slow incomplete Enso ring
  g.strokeStyle = `rgba(${T.RGB.acc},.48)`; g.lineWidth = 1.6; g.beginPath();
  for (let k = 0; k <= 68; k++) {
    const a = -t * 0.08 + k / 84 * T.TAU, p = cam.p(Math.cos(a) * 1.16, 0, Math.sin(a) * 1.16);
    k ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]);
  }
  g.stroke();
  // Azimuth degree callouts on 4 cardinals + range markers
  for (let a = 0; a < 360; a += 90) {
    const r = a * Math.PI / 180, p = cam.p(Math.cos(r) * 1.24, 0, Math.sin(r) * 1.24);
    T.txt(g, String(a).padStart(3, '0') + '°', p[0], p[1] + u * 0.25, u * 0.66, a === 0 ? T.C.hot : T.C.dim, 'm', 'center');
  }
  const rAxis = -cam.yaw - Math.PI / 2 + 0.28;
  [[0.25, '1M'], [0.50, '3M'], [0.75, '8M'], [1.0, 'MAX']].forEach(([rr, lbl]) => {
    const q = cam.p(Math.cos(rAxis) * rr, 0.004, Math.sin(rAxis) * rr);
    T.txt(g, lbl, q[0] + u * 0.25, q[1] - u * 0.2, u * 0.58, rr === 1.0 ? T.C.acc : T.C.dim);
  });

  // Vertical volumetric laser sweep wall
  {
    const s = sw.a, rx = Math.cos(s), rz = Math.sin(s);
    const a = cam.p(0, 0, 0), b = cam.p(rx * 1.03, 0, rz * 1.03), c = cam.p(rx * 1.03, 0.24, rz * 1.03), d = cam.p(0, 0.24, 0);
    const gr = g.createLinearGradient(a[0], a[1], d[0], d[1]);
    gr.addColorStop(0, `rgba(${T.RGB.acc},.28)`); gr.addColorStop(1, `rgba(${T.RGB.acc},0)`);
    g.fillStyle = gr; g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.lineTo(c[0], c[1]); g.lineTo(d[0], d[1]); g.closePath(); g.fill();
    g.strokeStyle = T.C.hot; g.lineWidth = 2.1; g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke();
  }

  // Ether tether threads from active / closest / tracker targets into Tengen (0,0,0)
  const o0 = cam.p(0, 0, 0);
  const items = list.map(e => {
    const r = BU.rad(e.rs), a = BU.ang(e.id), x = Math.cos(a) * r, z = Math.sin(a) * r;
    return { e, x, z, r, a, p0: cam.p(x, 0, z) };
  }).sort((a, b) => b.p0[3] - a.p0[3]);

  items.filter(it => BU.tr(it.e) || it.e.id === opt.sel || ((it.e.flags || 0) & 5) || rank.indexOf(it.e) < 4).slice(0, 7).forEach((it, idx) => {
    const al = BU.tr(it.e) || !!((it.e.flags || 0) & 4), isSel = it.e.id === opt.sel;
    g.strokeStyle = al ? `rgba(${T.RGB.alert},.38)` : isSel ? `rgba(${T.RGB.hot},.48)` : `rgba(${T.RGB.acc},.20)`;
    g.lineWidth = isSel ? 1.5 : 1;
    g.beginPath(); g.moveTo(it.p0[0], it.p0[1]); g.lineTo(o0[0], o0[1]); g.stroke();
    const pk = (t * (al ? 0.72 : 0.38) + idx * 0.19) % 1;
    const sp = cam.p(it.x * (1 - pk), 0.004, it.z * (1 - pk));
    g.fillStyle = al ? T.C.alert : T.C.hot;
    g.beginPath(); g.arc(sp[0], sp[1], al || isSel ? 2.4 : 1.7, 0, T.TAU); g.fill();
  });

  // Central Tengen Eye & Ramiel Core
  BU.drawOrigin(g, cam, u, t, list.length > 0);

  // Signal pillars (far to near)
  let selTop = null;
  const s = sw.a, callouts = [];
  for (const it of items) {
    const e = it.e, sg = T.pct(e.rs), tr = BU.tr(e), sep = !!((e.flags || 0) & 4), al = tr || sep, isSel = e.id === opt.sel;
    const hh = (0.07 + 0.60 * sg * sg) * e.h, p0 = it.p0, p1 = cam.p(it.x, hh, it.z);
    const since = (((s - it.a) % T.TAU) + T.TAU) % T.TAU / T.TAU, ph = 1 - since;
    const fog = T.clamp(1.18 - (p0[3] + 1) * 0.27, 0.36, 1) * e.h;
    const pa = sw.ping(e.id, t);

    // Hexagonal AT-Field floor shockwave on radar sweep hit
    if (pa < 1.25) {
      const k = (1 - pa / 1.25) * 0.92 * e.h, rr = 0.032 + pa * 0.16;
      g.strokeStyle = al ? `rgba(${T.RGB.alert},${k})` : `rgba(${T.RGB.acc},${k})`;
      g.lineWidth = 1.3; g.beginPath();
      for (let i = 0; i <= 6; i++) {
        const a = i * T.TAU / 6 + pa * 0.5, q = cam.p(it.x + Math.cos(a) * rr, 0.003, it.z + Math.sin(a) * rr);
        i ? g.lineTo(q[0], q[1]) : g.moveTo(q[0], q[1]);
      }
      g.stroke();
    }

    if (al || isSel) BU.drawLock(g, cam, it.x, it.z, hh, isSel ? 0.115 : 0.088, al ? T.C.alert : T.C.hot, t, isSel || al);

    const col = al ? T.ALERT : BU.rgb(e, 0.35 + 0.65 * sg);
    // Base reflection pool
    g.fillStyle = T.rgb(col, 0.45 * fog);
    g.beginPath(); g.ellipse(p0[0], p0[1], Math.max(2, p0[2] * 0.024), Math.max(1.2, p0[2] * 0.024 * Math.sin(cam.pitch)), 0, 0, T.TAU); g.fill();

    // Outer aura + sharp plasma core beam
    const bw = Math.max(1.6, p0[2] * 0.017);
    const gr = g.createLinearGradient(p0[0], p0[1], p1[0], p1[1]);
    gr.addColorStop(0, T.rgb(col, 0.08 * fog));
    gr.addColorStop(1, T.rgb(col, (0.58 + 0.42 * ph) * fog));
    g.strokeStyle = T.rgb(col, 0.22 * fog); g.lineWidth = bw * 2.4; g.lineCap = 'round';
    g.beginPath(); g.moveTo(p0[0], p0[1]); g.lineTo(p1[0], p1[1]); g.stroke();
    g.strokeStyle = gr; g.lineWidth = bw;
    g.beginPath(); g.moveTo(p0[0], p0[1]); g.lineTo(p1[0], p1[1]); g.stroke();
    g.lineCap = 'butt';

    // Tiered altitude ticks on strong pillars
    if (sg > 0.45 && hh > 0.18) {
      g.strokeStyle = T.rgb(col, 0.55 * fog); g.lineWidth = 1;
      for (let k = 1; k <= 2; k++) {
        const pm = cam.p(it.x, hh * k / 3, it.z), tw = Math.max(2.5, pm[2] * 0.015);
        g.beginPath(); g.moveTo(pm[0] - tw, pm[1]); g.lineTo(pm[0] + tw, pm[1]); g.stroke();
      }
    }

    // Crown diamond / beacon node
    const rr = Math.max(2.2, p1[2] * (0.015 + 0.022 * sg) * (1 + 0.55 * Math.max(0, 1 - pa * 2)) || 2.2);
    const headCol = T.rgb(al ? col : T.heatRGB(0.6 + 0.4 * ph), fog);
    T.diamond(g, p1[0], p1[1], rr * 1.25, headCol, true);
    if (isSel || al) T.diamond(g, p1[0], p1[1], rr * 2.1, al ? T.C.alert : T.C.hot, false);

    if (opt.hits) {
      T.hits.circle(p1[0], p1[1], Math.max(u * 1.7, rr * 2.6), { k: 'dev', id: e.id });
      T.hits.circle((p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2, u * 1.3, { k: 'dev', id: e.id });
    }
    if (isSel) selTop = { p: p1, e };
    else if (opt.labels && (al || rank.indexOf(e) < opt.labels) && opt.sel == null) callouts.push({ p: p1, e, al, rr });
  }

  // Non-overlapping tactical leader callouts on top pillars
  const placed = [];
  for (const c of callouts.slice(0, 5)) {
    g.font = `600 ${u * 0.72}px ${T.FC}`;
    const txt = T.fit(g, BU.label(c.e).toUpperCase() + ' · ' + Math.round(c.e.rs), u * 10.5);
    const tw = T.tw(g, txt, u * 0.72, 'c');
    let lx = c.p[0] + c.rr + u * 0.75, ly = c.p[1] - u * 0.65;
    for (const q of placed) if (Math.abs(ly - q.y) < u * 1.25 && Math.abs(lx - q.x) < tw * 0.8 + u * 1.2) ly = q.y - u * 1.3;
    placed.push({ x: lx, y: ly });
    g.strokeStyle = c.al ? `rgba(${T.RGB.alert},.6)` : `rgba(${T.RGB.hot},.42)`; g.lineWidth = 1;
    g.beginPath(); g.moveTo(c.p[0], c.p[1]); g.lineTo(lx - u * 0.25, ly - u * 0.22); g.lineTo(lx, ly - u * 0.22); g.stroke();
    g.fillStyle = `rgba(${T.RGB.bg},.78)`;
    g.fillRect(lx - u * 0.2, ly - u * 0.88, tw + u * 0.55, u * 1.12);
    T.txt(g, txt, lx + u * 0.08, ly, u * 0.72, c.al ? T.C.alert : T.C.hot, 'c');
  }
  return selTop;
};

BU.sceneAny = (g, cam, sw, opt) => BU.scene(g, cam, sw, opt);

// Callout card at the selected 3D target
BU.card = (g, p, e, u, W, H, top) => {
  const sg = T.pct(e.rs), extra = BU.intelLine(e), sep = !!((e.flags || 0) & 4), tr = BU.tr(e), al = tr || sep;
  const bw = u * 16.8, bh = extra ? u * 9.8 : u * 8.6;
  let bx = p[0] + u * 4, by = p[1] - u * 7;
  if (bx + bw > W - u) bx = p[0] - u * 4 - bw;
  by = T.clamp(by, top, H - bh - u * 3);
  const acCol = al ? T.C.alert : T.C.acc;
  T.diamond(g, p[0], p[1], u * 0.9, T.C.hot, false);
  const ax = bx > p[0] ? bx : bx + bw;
  g.strokeStyle = acCol; g.lineWidth = 1.2; g.beginPath();
  g.moveTo(p[0] + (bx > p[0] ? 1 : -1) * u * 0.7, p[1] - u * 0.7);
  g.lineTo(ax + (bx > p[0] ? -1 : 1) * u * 1.2, by + bh * 0.52);
  g.lineTo(ax, by + bh * 0.52); g.stroke();
  BU.cutPlate(g, bx, by, bw, bh, u * 0.85, `rgba(${T.RGB.bg},.90)`, acCol, 1.3);
  g.fillStyle = acCol; g.fillRect(bx + u * 0.85, by, bw * 0.34, 2);
  T.drawSealChar(g, bx + bw - u * 2.45, by + u * 0.65, u * 1.7, al ? '警' : sg > 0.65 ? '接' : '感', acCol, al);
  g.font = `600 ${u * 1.18}px ${T.FC}`; T.txt(g, T.fit(g, BU.label(e).toUpperCase(), bw - u * 3.7), bx + u, by + u * 1.7, u * 1.18, T.C.hot, 'c');
  const az = Math.round(((BU.ang(e.id) * 180 / Math.PI) % 360 + 360) % 360);
  g.font = `${u * 0.72}px ${T.FM}`;
  T.txt(g, T.fit(g, ((e.vend || '—') + ' · ≈' + BU.dist(e.rs, e.tx) + 'M · ' + String(az).padStart(3, '0') + '°').toUpperCase(), bw - u * 3.7), bx + u, by + u * 2.8, u * 0.72, al ? T.C.alert : T.C.label);
  T.bigNum(g, Math.round(e.rs), bx + u, by + u * 5.75, u * 2.85, al ? T.C.alert : T.heat(0.4 + 0.6 * sg), 'DBM');
  T.spark(g, bx + u * 7.7, by + u * 3.65, bw - u * 8.7, u * 2.15, e.hist.slice(-40), -100, -35, acCol);
  if (extra) {
    g.font = `${u * 0.72}px ${T.FM}`;
    T.txt(g, T.fit(g, extra.toUpperCase(), bw - u * 2), bx + u, by + u * 7.15, u * 0.72, sep ? T.C.alert : T.C.acc);
  }
  T.segBar(g, bx + u, by + (extra ? u * 8.05 : u * 6.9), bw - u * 2, u * 0.4, sg, 16, false, al);
  T.hits.rect(bx, by, bw, bh, { k: 'card' });
};

// Sticky order: neighbors swap only if the difference is > 6 dB
BU.sticky = () => ({
  ids: [], t: 0,
  get(list, now, frozen) {
    const byId = new Map(list.map(e => [e.id, e]));
    if (!this.ids.length || (!frozen && now - this.t > 1000)) {
      this.t = now;
      const o = this.ids.filter(id => byId.has(id));
      for (const e of [...list].sort((a, b) => b.rs - a.rs)) if (!o.includes(e.id)) { let k = 0; while (k < o.length && byId.get(o[k]).rs >= e.rs) k++; o.splice(k, 0, e.id); }
      for (let pass = 0; pass < o.length; pass++) { let sw = false; for (let i = 0; i + 1 < o.length; i++) if (byId.get(o[i + 1]).rs > byId.get(o[i]).rs + 6) { [o[i], o[i + 1]] = [o[i + 1], o[i]]; sw = true; } if (!sw) break; }
      this.ids = o;
    }
    return this.ids.filter(id => byId.has(id)).map(id => byId.get(id));
  },
});

// Evangelion MAGI tactical rows with smooth vertical glide, inline distance & sparkline
BU.rows = (g, x, y, w, h, u, rowH, list, sel, dt, cols = {}) => {
  const fs = rowH * 0.40, wide = !!cols.more;
  T.txt(g, 'T', x + u * 0.35, y + u * 0.78, u * 0.66, T.C.dim);
  T.txt(g, 'DESIGNATION', x + rowH * 1.04, y + u * 0.78, u * 0.66, T.C.dim);
  if (wide) {
    T.txt(g, 'DIST', x + w * 0.46, y + u * 0.78, u * 0.66, T.C.dim);
    T.txt(g, 'TRACE', x + w * 0.56, y + u * 0.78, u * 0.66, T.C.dim);
  }
  T.txt(g, 'DBM', x + w - u * 0.35, y + u * 0.78, u * 0.66, T.C.dim, 'm', 'right');
  g.fillStyle = `rgba(${T.RGB.acc},.25)`; g.fillRect(x, y + u * 1.08, w, 1);

  const y0 = y + u * 1.35;
  g.save(); g.beginPath(); g.rect(x - 2, y0, w + 4, h - u * 1.35); g.clip();
  list.forEach((e, i) => {
    if (e._ry == null) e._ry = i;
    e._ry += (i - e._ry) * T.ease(dt, 0.25);
    const cy = y0 + e._ry * rowH;
    if (cy > y + h || cy + rowH < y0) return;
    const on = e.id === sel, tr = BU.tr(e), sep = !!((e.flags || 0) & 4), al = tr || sep;
    const sg = T.pct(e.rs), gs = rowH * 0.60, col = al ? T.C.alert : (on ? T.C.hot : T.C.acc);
    g.globalAlpha = T.clamp(e.h);
    if (on) {
      BU.cutPlate(g, x + 1, cy + 1, w - 2, rowH - 2, u * 0.42, `rgba(${T.RGB.acc},.13)`, al ? T.C.alert : T.C.acc, 1.15);
      g.fillStyle = al ? T.C.alert : T.C.acc; g.fillRect(x + 1, cy + 3, 3, rowH - 6);
    } else if (i % 2 === 1) {
      g.fillStyle = `rgba(${T.RGB.panel},.14)`; g.fillRect(x + 1, cy + 1, w - 2, rowH - 2);
    }
    T.glyph(g, x + u * 0.28, cy + (rowH - gs) / 2, gs, BU.glyph(e), col);
    const nameW = (wide ? w * 0.45 : w * 0.60) - rowH * 1.04 - u * 0.3;
    g.font = `600 ${fs}px ${T.FC}`;
    T.txt(g, T.fit(g, BU.label(e).toUpperCase(), nameW), x + rowH * 1.04, cy + rowH * 0.44, fs, on ? T.C.hot : (al ? T.C.alert : T.C.label), 'c');
    g.font = `${fs * 0.74}px ${T.FM}`;
    const intel = BU.intelLine(e);
    const subTxt = (e.vend || '—') + (wide ? '' : ' · ≈' + BU.dist(e.rs, e.tx) + 'M') + (intel ? ' · ' + intel : (tr ? ' · TRACKER' : ''));
    T.txt(g, T.fit(g, subTxt.toUpperCase(), nameW), x + rowH * 1.04, cy + rowH * 0.81, fs * 0.74, al ? T.C.alert : (intel ? T.C.acc : T.C.dim));

    if (wide) {
      T.txt(g, '≈' + BU.dist(e.rs, e.tx) + 'M', x + w * 0.46, cy + rowH * 0.46, fs * 0.82, T.C.hot, 't');
      T.txt(g, T.ago(e.age || 0), x + w * 0.46, cy + rowH * 0.81, fs * 0.68, T.C.dim);
      if (e.hist && e.hist.length > 1) {
        T.spark(g, x + w * 0.56, cy + rowH * 0.20, w * 0.13, rowH * 0.58, e.hist.slice(-28), -100, -35, al ? T.C.alert : T.C.acc);
      }
    }
    const bw = wide ? w * 0.14 : w * 0.20;
    T.segBar(g, x + w - u * 0.35 - u * 3.1 - bw, cy + rowH * 0.38, bw, rowH * 0.22, sg, 10, false, al);
    T.txt(g, Math.round(e.rs), x + w - u * 0.35, cy + rowH * 0.67, rowH * 0.54, al ? T.C.alert : T.heat(0.4 + 0.6 * sg), 't', 'right');
    T.hits.rect(x, cy, w, rowH, { k: 'dev', id: e.id });
    g.globalAlpha = 1;
  });
  g.restore();
};

T.views.push({
  id: 'ble', title: 'Bluetooth', want: 'ble', subs: ['Scope', 'Devices'], sub: 0,
  mount(main) {
    this.root = T.el('div', 'view'); const st = T.el('div', 'stage'); this.root.appendChild(st); this.cv = T.canvas(st); main.appendChild(this.root);
    this.cam = T.orbit(0.5, 0.94, 3.12); this.camT = { yaw: 0.5, D: 3.12 };
    this.drag = T.drag(this.cv.cv, this.cam, { minPitch: 0.48, maxPitch: 1.35 });
    this.drag.onTap = (x, y) => this.tap(x, y);
    this.sw = BU.sweeper(); this.dust = T.dust(220); this.sel = null; this.sticky = BU.sticky(); this.info = null; this.infoT = 0;
    this.onSub(this.sub);
  },
  unmount() { this.cv.dispose(); this.root.remove(); },
  onSub(i) {
    this.sub = i;
    if (i === 0) { this.cam.pitch = 0.94; this.camT.D = 3.12; }
    this.camT.yaw = this.cam.yaw;
  },
  tap(x, y) {
    const h = T.hits.at(x, y);
    if (!h) { if (this.sub === 0) this.sel = null; return; }
    if (h.k === 'dev') { this.sel = (this.sel === h.id && this.sub === 0) ? null : h.id; this.info = null; this.infoT = 0; }
    else if (h.k === 'back') this.sel = null;
    else if (h.k === 'name') { const e = S.dev.get(this.sel); if (e && T.nameDevice) T.nameDevice(e, e.label || (this.info && this.info.label) || ''); }
  },
  fetchInfo(now) {
    if (this.sel == null || now - this.infoT < 3000) return;
    this.infoT = now; const id = this.sel;
    fetch(`${BU.apiBase()}/api/bledev?id=${id}`, { cache: 'no-store' }).then(r => r.json()).then(d => { if (this.sel === id) this.info = d; }).catch(() => {});
  },
  frame(dt, t) {
    const { g, w, h } = this.cv; T.hits.clear();
    const u = T.unit(w, h), m = u * 1.6, y0 = 50, portrait = h > w * 1.05;
    this.sw.tick(dt, t); this.drag.tick(dt); this.dust.tick(dt);
    if (this.sel != null && !S.dev.has(this.sel)) this.sel = null;
    if (this.drag.down && this.drag.moved && this.sub === 0) this.sel = null;
    T.backdrop(g, w, h, portrait ? 0.5 : 0.37, 0.55);
    const n = BU.list().length;
    if (this.sub === 0) this.scope(g, w, h, u, m, y0, portrait, dt, t);
    else this.devices(g, w, h, u, m, y0, portrait, dt, t);
    if (!S.ble) T.emptyState(g, w, h, u, 'WAITING FOR TENGEN', 'connect the tablet to the TENGEN Wi-Fi');
    else if (!n) T.emptyState(g, w, h, u, 'NO DEVICES', S.ble.on ? 'listening…' : 'bluetooth is off');
    T.finish(g, w, h);
  },
  camStep(dt) {
    const c = this.cam;
    if (this.sel != null) {
      const e = S.dev.get(this.sel);
      if (e) this.camT.yaw = -Math.PI / 2 - BU.ang(e.id) - 0.30;
      this.camT.D = 2.82;
    } else {
      if (this.drag.idle()) this.camT.yaw = c.yaw + dt * 0.068;
      else this.camT.yaw = c.yaw;
      this.camT.D = 3.12;
    }
    const dy = ((this.camT.yaw - c.yaw + Math.PI) % T.TAU + T.TAU) % T.TAU - Math.PI;
    if (!this.drag.down) c.yaw += dy * Math.min(1, dt * 2.4);
    c.D = T.lerp(c.D, this.camT.D, Math.min(1, dt * 2.4));
  },
  hud(g, w, h, u, m, y0, title, status) {
    T.header(g, m, y0, w - 2 * m, u, title, status, false);
    T.dots(g, m, y0 + u * 2.75, u, 2, this.sub);
  },

  // ---------- SCOPE: Sanctuary Depth Radar + Live Contact Deck ----------
  scope(g, w, h, u, m, y0, portrait, dt, t) {
    this.camStep(dt);
    const c = this.cam;
    c.frame(portrait ? w * 0.5 : w * 0.365, portrait ? y0 + (h - y0) * 0.28 : y0 + (h - y0) * 0.54, portrait ? w * 0.88 : Math.min((h - y0) * 1.38, w * 0.92));
    const selTop = BU.sceneAny(g, c, this.sw, { u, t, sel: this.sel, labels: 4, hits: true, W: w, H: h });
    this.dust.draw(g, c);
    T.bloom(this.cv, 0.55);

    const list = BU.list(), trk = list.some(e => BU.tr(e) || ((e.flags || 0) & 4));
    const actCnt = list.filter(e => (e.flags || 0) & 1).length;
    this.hud(g, w, h, u, m, y0, 'Scope · depth', `${list.length} devices · ${actCnt} active · cam ${Math.round(((c.yaw * 180 / Math.PI) % 360 + 360) % 360)}°${trk ? ' · tracker!' : ''}`);

    const lst = this.sticky.get(list, performance.now(), false);
    if (!portrait) {
      const lw = w * 0.285, lx = w - m - lw, ly = y0 + u * 4.1, lh = Math.min(h - ly - u * 3.6, u * 2.35 * 12 + u * 3.2);
      BU.cutPlate(g, lx, ly, lw, lh, u * 0.9, `rgba(${T.RGB.panel},.32)`, trk ? `rgba(${T.RGB.alert},.5)` : `rgba(${T.RGB.acc},.38)`, 1.2);
      g.fillStyle = trk ? T.C.alert : T.C.acc; g.fillRect(lx + u * 0.9, ly, lw * 0.34, 2);
      T.txt(g, '// LIVE CONTACTS', lx + u * 0.75, ly + u * 1.15, u * 0.76, trk ? T.C.alert : T.C.acc, 't');
      T.drawSealChar(g, lx + lw - u * 2.1, ly + u * 0.35, u * 1.45, trk ? '警' : list.length > 8 ? '活' : '眼', trk ? T.C.alert : T.C.acc, trk);
      BU.rows(g, lx + u * 0.55, ly + u * 1.75, lw - u * 1.1, lh - u * 2.3, u, u * 2.3, lst, this.sel, dt);
    } else {
      const ly = y0 + (h - y0) * 0.58, lh = h - ly - u * 3;
      BU.cutPlate(g, m, ly, w - 2 * m, lh, u * 0.9, `rgba(${T.RGB.panel},.32)`, `rgba(${T.RGB.acc},.38)`, 1.2);
      BU.rows(g, m + u * 0.55, ly + u * 0.6, w - 2 * m - u * 1.1, lh - u * 1.1, u, u * 2.4, lst, this.sel, dt, { more: true });
    }
    if (selTop) BU.card(g, selTop.p, selTop.e, u, portrait ? w : w * 0.71, h, y0 + u * 4);
    T.footer(g, m, h - u * 2.4, w - 2 * m, u, 'drag: orbit  ·  tap pillar: focus', 'height = signal · hex = AT-lock');
  },

  // ---------- DEVICES: Tactical Ledger + Dharma Mandala / Target Dossier ----------
  devices(g, w, h, u, m, y0, portrait, dt, t) {
    const now = performance.now(), list = BU.list(), lst = this.sticky.get(list, now, this.sel != null);
    const trk = list.some(e => BU.tr(e) || ((e.flags || 0) & 4));
    this.hud(g, w, h, u, m, y0, 'Devices', `${S.ble ? S.ble.n : 0} devices · ${S.ble ? S.ble.m : 0} addresses${trk ? ' · alert!' : ''}`);
    const top = y0 + u * 4.1, bot = h - u * 3;
    const tx = m, tw = portrait ? w - 2 * m : w * 0.57 - m, th = portrait ? (bot - top) * 0.53 : bot - top;
    BU.cutPlate(g, tx, top, tw, th, u * 0.95, `rgba(${T.RGB.panel},.28)`, `rgba(${T.RGB.acc},.34)`, 1.2);
    g.fillStyle = T.C.acc; g.fillRect(tx + u * 0.95, top, tw * 0.28, 2);
    BU.rows(g, tx + u * 0.65, top + u * 0.75, tw - u * 1.3, th - u * 1.4, u, u * 2.55, lst, this.sel, dt, { more: tw > u * 32 });

    const dx = portrait ? m : w * 0.57 + u * 0.9, dy = portrait ? top + th + u * 0.9 : top;
    const dw = portrait ? w - 2 * m : w - m - dx, dh = portrait ? bot - dy : bot - top;
    const e = this.sel != null && S.dev.get(this.sel);
    const al = e ? (BU.tr(e) || !!((e.flags || 0) & 4)) : trk;
    BU.cutPlate(g, dx, dy, dw, dh, u * 0.95, `rgba(${T.RGB.panel},.30)`, al ? `rgba(${T.RGB.alert},.55)` : `rgba(${T.RGB.acc},.38)`, 1.2);
    g.fillStyle = al ? T.C.alert : T.C.acc; g.fillRect(dx + u * 0.95, dy, dw * 0.34, 2);

    if (e) { this.fetchInfo(now); this.detail(g, dx, dy, dw, dh, u, e, t); }
    else this.summary(g, dx, dy, dw, dh, u, list, t);
    T.footer(g, m, h - u * 2.4, w - 2 * m, u, e ? 'tap row: switch  ·  ‹ all: back' : 'tap row: target dossier', 'order holds until 6 dB difference');
  },

  // Sacred Zen Dharma Mandala + Ether Summary when no row is selected
  summary(g, x, y, w, h, u, list, t) {
    const p = u * 1.15, trk = list.some(e => BU.tr(e) || ((e.flags || 0) & 4));
    let cy = y + p;
    T.txt(g, '// ETHER SANCTUARY', x + p, cy + u * 0.9, u * 1.0, trk ? T.C.alert : T.C.acc, 't');
    T.drawSealChar(g, x + w - p - u * 1.85, cy - u * 0.1, u * 1.85, trk ? '警' : list.length > 8 ? '活' : '静', trk ? T.C.alert : T.C.acc, trk);
    cy += u * 2.1;

    // Top telemetry + 8-sector Dharma Signal Rose
    const cnt = {}, maxRssi = {};
    let actCnt = 0, movCnt = 0, bestE = null;
    for (const e of list) {
      const k = e.type || 'unknown';
      cnt[k] = (cnt[k] || 0) + 1;
      maxRssi[k] = Math.max(maxRssi[k] ?? -110, e.rs);
      if ((e.flags || 0) & 1) actCnt++;
      if ((e.flags || 0) & 2) movCnt++;
      if (!bestE || e.rs > bestE.rs) bestE = e;
    }

    const roseH = Math.min(u * 10.2, h * 0.32), R = Math.min(roseH * 0.44, w * 0.22);
    const rx = x + w - p - R - u * 0.5, ry = cy + roseH * 0.5;
    // Left metrics next to the mandala
    T.bigNum(g, list.length, x + p, cy + u * 3.4, u * 4.2, T.C.hot, 'DEVICES');
    const nearStr = bestE ? `≈ ${BU.dist(bestE.rs, bestE.tx)}M (${Math.round(bestE.rs)} DBM)` : '—';
    T.txt(g, 'CLOSEST · ' + nearStr, x + p, cy + u * 4.75, u * 0.72, T.C.acc);
    T.txt(g, `${actCnt} ${T.tr('ACTIVE')}  ·  ${movCnt} ${T.tr('MOVING')}`, x + p, cy + u * 5.85, u * 0.72, T.C.label);

    // Draw 8-spoke Zen Dharma / AT-Field Mandala Rose
    if (R > u * 2.2) {
      g.save();
      // Outer incomplete Enso brush circle
      g.strokeStyle = trk ? T.C.alert : T.C.acc; g.lineWidth = 1.6; g.beginPath();
      g.arc(rx, ry, R, t * 0.16, t * 0.16 + T.TAU * 0.84); g.stroke();
      // Inscribed octagon + inner ring
      g.strokeStyle = `rgba(${T.RGB.line},.85)`; g.lineWidth = 1; g.beginPath();
      for (let k = 0; k <= 8; k++) {
        const a = k * Math.PI / 4, qx = rx + Math.cos(a) * R * 0.72, qy = ry + Math.sin(a) * R * 0.72;
        k ? g.lineTo(qx, qy) : g.moveTo(qx, qy);
      }
      g.stroke();
      for (let k = 0; k < 8; k++) {
        const a = k * Math.PI / 4;
        g.beginPath(); g.moveTo(rx + Math.cos(a) * R * 0.22, ry + Math.sin(a) * R * 0.22);
        g.lineTo(rx + Math.cos(a) * R * 0.94, ry + Math.sin(a) * R * 0.94); g.stroke();
      }
      // 8 sector harmonic polygon from live device azimuths
      const sec = new Float32Array(8);
      for (const e of list) { const bi = Math.floor(((BU.ang(e.id) % T.TAU + T.TAU) % T.TAU) / (T.TAU / 8)) & 7; sec[bi] = Math.max(sec[bi], 0.22 + 0.78 * T.pct(e.rs)); }
      g.fillStyle = trk ? `rgba(${T.RGB.alert},.20)` : `rgba(${T.RGB.acc},.22)`;
      g.strokeStyle = trk ? T.C.alert : T.C.hot; g.lineWidth = 1.3; g.beginPath();
      for (let k = 0; k <= 8; k++) {
        const i = k & 7, a = i * Math.PI / 4, rr = R * (0.20 + 0.74 * sec[i]);
        const qx = rx + Math.cos(a) * rr, qy = ry + Math.sin(a) * rr;
        k ? g.lineTo(qx, qy) : g.moveTo(qx, qy);
      }
      g.closePath(); g.fill(); g.stroke();
      T.drawSealChar(g, rx - u * 0.72, ry - u * 0.72, u * 1.44, '天', T.C.hot, false);
      g.restore();
    }
    cy += Math.max(roseH, u * 6.8) + u * 0.5;

    // 3-minute crowd density sparkline
    T.txt(g, '// CROWD DENSITY · 3 MIN', x + p, cy + u * 0.7, u * 0.70, T.C.dim); cy += u * 1.05;
    const sh = Math.min(u * 4.4, h * 0.18);
    g.fillStyle = `rgba(${T.RGB.bg},.45)`; g.fillRect(x + p, cy, w - 2 * p, sh);
    T.brackets(g, x + p, cy, w - 2 * p, sh, u * 0.45, `rgba(${T.RGB.acc},.45)`, 1);
    const hi = Math.max(5, ...S.bleHist);
    T.spark(g, x + p, cy, w - 2 * p, sh, S.bleHist, 0, hi * 1.15, T.C.acc);
    cy += sh + u * 1.25;

    // Device class breakdown
    const types = Object.entries(cnt).sort((a, b) => b[1] - a[1]), mx = Math.max(1, ...types.map(q => q[1]));
    for (const [tp, n] of types) {
      if (cy + u * 1.65 > y + h - p * 0.7) break;
      const tr = tp === 'tracker';
      T.glyph(g, x + p, cy, u * 1.28, BU.G[tp] || tp[0].toUpperCase(), tr ? T.C.alert : T.C.acc);
      T.txt(g, tp.toUpperCase(), x + p + u * 1.85, cy + u * 0.98, u * 0.78, tr ? T.C.alert : T.C.label);
      T.segBar(g, x + w * 0.44, cy + u * 0.42, w * 0.39, u * 0.4, n / mx, 12, false, tr);
      T.txt(g, n, x + w - p, cy + u * 1.02, u * 1.05, tr ? T.C.alert : T.C.hot, 't', 'right');
      cy += u * 1.68;
    }
  },

  // MAGI Target Dossier with Chronograph Enso Signal Dial
  detail(g, x, y, w, h, u, e, t) {
    const p = u * 1.15, tr = BU.tr(e), sep = !!((e.flags || 0) & 4), al = tr || sep;
    const sg = T.pct(e.rs), d = this.info, ac = al ? T.C.alert : T.C.acc;
    let cy = y + p;

    T.txt(g, '// TARGET DOSSIER', x + p, cy + u * 0.9, u * 1.0, ac, 't');
    T.txt(g, '‹ ALL', x + w - p, cy + u * 0.9, u * 0.88, T.C.label, 'm', 'right');
    T.hits.rect(x + w - p - u * 4.5, cy - u * 0.4, u * 5, u * 2, { k: 'back' });
    {
      const bw = u * 5.5, bx = x + w - p - u * 4.8 - bw - u * 0.6, by = cy - u * 0.32;
      BU.cutPlate(g, bx, by, bw, u * 1.68, u * 0.38, `rgba(${T.RGB.acc},.12)`, ac, 1);
      T.txt(g, e.label ? 'RENAME' : 'NAME', bx + bw / 2, by + u * 1.14, u * 0.78, T.C.hot, 'm', 'center');
      T.hits.rect(bx - u * 0.3, by - u * 0.3, bw + u * 0.6, u * 2.3, { k: 'name' });
    }
    cy += u * 2.3;

    // Target header + Hanko stamp
    T.drawSealChar(g, x + w - p - u * 2.1, cy, u * 2.0, al ? '警' : sg > 0.62 ? '接' : '探', ac, al);
    g.font = `600 ${u * 1.65}px ${T.FC}`;
    T.txt(g, T.fit(g, BU.label(e).toUpperCase(), w - 2 * p - u * 2.6), x + p, cy + u * 1.25, u * 1.65, al ? T.C.alert : T.C.hot, 'c');
    cy += u * 1.95;
    g.font = `${u * 0.82}px ${T.FM}`;
    T.txt(g, T.fit(g, ((e.vend || '—') + ' · ' + (e.type || '') + (tr ? ' · TRACKER' : '')).toUpperCase(), w - 2 * p - u * 2.6), x + p, cy + u * 0.55, u * 0.82, al ? T.C.alert : T.C.label);
    cy += u * 1.3;

    const intel = BU.intelLine(e);
    if (intel) {
      g.font = `${u * 0.76}px ${T.FM}`;
      T.txt(g, T.fit(g, intel.toUpperCase(), w - 2 * p), x + p, cy + u * 0.5, u * 0.76, sep ? T.C.alert : T.C.acc);
      cy += u * 1.2;
    }

    // Chronograph Enso Signal Dial + Distance Readout
    const dialH = u * 5.2, R = u * 2.25, dcx = x + p + R + u * 0.2, dcy = cy + dialH * 0.5;
    g.save();
    g.strokeStyle = T.C.line; g.lineWidth = u * 0.32;
    g.beginPath(); g.arc(dcx, dcy, R, Math.PI * 0.75, Math.PI * 2.25); g.stroke();
    g.strokeStyle = al ? T.C.alert : T.heat(0.38 + 0.62 * sg);
    g.beginPath(); g.arc(dcx, dcy, R, Math.PI * 0.75, Math.PI * (0.75 + 1.5 * sg)); g.stroke();
    // Outer rotating Enso hairline
    g.strokeStyle = `rgba(${al ? T.RGB.alert : T.RGB.acc},.48)`; g.lineWidth = 1.1;
    g.beginPath(); g.arc(dcx, dcy, R + u * 0.42, -t * 0.35, -t * 0.35 + T.TAU * 0.72); g.stroke();
    T.txt(g, Math.round(e.rs), dcx, dcy + u * 0.35, u * 1.55, al ? T.C.alert : T.C.hot, 't', 'center');
    T.txt(g, 'DBM', dcx, dcy + u * 1.32, u * 0.58, T.C.dim, 'm', 'center');
    g.restore();

    const rx = dcx + R + u * 1.2;
    T.txt(g, 'EST. RANGE', rx, cy + u * 1.05, u * 0.68, T.C.dim);
    T.txt(g, '≈ ' + BU.dist(e.rs, e.tx) + ' M', rx, cy + u * 3.0, u * 2.15, T.C.hot, 't');
    T.segBar(g, rx, cy + u * 3.85, Math.max(u * 4, x + w - p - rx), u * 0.46, sg, 14, true, al);
    cy += dialH + u * 0.65;

    // 2-minute RSSI oscillogram with grid & brackets
    T.txt(g, '// RSSI OSCILLOGRAM · 2 MIN', x + p, cy + u * 0.68, u * 0.68, T.C.dim); cy += u * 1.0;
    const sh = T.clamp(h * 0.14, u * 2.5, u * 4.8);
    g.fillStyle = `rgba(${T.RGB.bg},.48)`; g.fillRect(x + p, cy, w - 2 * p, sh);
    g.strokeStyle = `rgba(${T.RGB.line},.65)`; g.lineWidth = 1;
    for (let k = 1; k <= 2; k++) { const gy = cy + sh * k / 3; g.beginPath(); g.moveTo(x + p, gy); g.lineTo(x + w - p, gy); g.stroke(); }
    T.brackets(g, x + p, cy, w - 2 * p, sh, u * 0.45, ac, 1.1);
    T.spark(g, x + p, cy, w - 2 * p, sh, e.hist, -100, -35, ac);
    cy += sh + u * 0.95;

    // Key-Value Telemetry Matrix
    const kv = [];
    const f = (d && d.f != null) ? d.f : (e.flags || 0);
    const stParts = [];
    if (f & 4) stParts.push(T.tr('SEPARATED'));
    else if (d && d.state) stParts.push(T.tr(d.state));
    if (f & 1) stParts.push(T.tr('ACTIVE'));
    else if (d && d.pps10 != null) stParts.push(T.tr('IDLE'));
    if (f & 2) stParts.push(T.tr('MOVING'));
    if (stParts.length) kv.push(['STATUS', stParts.join(' · ')]);
    if (d && (d.batL != null || d.batR != null || d.batC != null)) {
      const bp = [];
      if (d.batL != null) bp.push('L ' + d.batL + '%');
      if (d.batR != null) bp.push('R ' + d.batR + '%');
      if (d.batC != null) bp.push(T.tr('CASE') + ' ' + d.batC + '%');
      kv.push(['BATTERY', bp.join(' · ')]);
    } else if ((d && d.bat != null) || e.bat >= 0) {
      kv.push(['BATTERY', (d && d.bat != null ? d.bat : e.bat) + '%']);
    }
    if (d && d.model) kv.push(['MODEL', d.model]);
    if ((d && d.tx != null) || e.tx != null) {
      const txv = (d && d.tx != null) ? d.tx : e.tx;
      kv.push(['TX POWER', (txv >= 0 ? '+' : '') + txv + ' dBm']);
    }
    if (d && d.pps10) kv.push(['RATE', (d.pps10 / 10).toFixed(1) + ' pkt/s']);
    kv.push(['ADDRESSES', String(e.macs || 1)], ['NEAR FOR', T.ago(e.age || 0)]);
    if (d) {
      if (d.rmin != null) kv.push(['RANGE', `${d.rmin} … ${d.rmax} dBm`]);
      if (d.pk != null) kv.push(['PACKETS', String(d.pk)]);
      if (d.cid) kv.push(['COMPANY ID', d.cid]);
      for (const a of (d.addr || []).slice(0, 3)) kv.push([a.mac, (a.kind || '') + ' · ' + a.r]);
      for (const s of (d.uu || []).slice(0, 3)) kv.push(['SERVICE ' + s.u, s.o || '']);
    }
    for (const [k, v] of kv) {
      if (cy + u * 1.38 > y + h - p * 0.6) break;
      g.fillStyle = `rgba(${T.RGB.line},.42)`; g.fillRect(x + p, cy + u * 1.15, w - 2 * p, 1);
      T.txt(g, k, x + p, cy + u * 0.88, u * 0.76, T.C.dim);
      g.font = `500 ${u * 0.78}px ${T.FC}`;
      T.txt(g, T.fit(g, String(v).toUpperCase(), w * 0.56), x + w - p, cy + u * 0.88, u * 0.78, k === 'STATUS' && sep ? T.C.alert : T.C.hot, 'c', 'right');
      cy += u * 1.38;
    }
  },
});
