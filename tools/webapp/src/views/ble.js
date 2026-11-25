// ---- BLUETOOTH (version B): depth radar, 3D city, dense table ----
// Shared helpers (T.bleUI) are also used by Overview.
const BU = T.bleUI = {};
BU.G = { phone: 'P', audio: 'A', watch: 'W', tracker: 'T', computer: 'C', TV: 'V', tv: 'V', sensor: 'S', band: 'B', input: 'G', beacon: 'i', unknown: '?' };
BU.glyph = e => BU.G[e.type] || (e.type ? String(e.type)[0].toUpperCase() : '?');
BU.ang = id => (T.hash(id) % 360) * Math.PI / 180;
BU.rad = rs => 0.08 + 0.88 * (1 - T.pct(rs));          // strong = closer to center
BU.list = () => [...S.dev.values()].filter(e => e.h > 0.02 && e.rs != null);
BU.label = e => e.label || e.name || (e.type && e.type !== 'unknown' ? e.type : 'device');   // custom label wins
BU.tr = e => e.type === 'tracker';
BU.rgb = (e, v) => BU.tr(e) ? T.ALERT : T.heatRGB(v);
BU.crossed = (a, b, x) => b >= a ? (x > a && x <= b) : (x > a || x <= b);
BU.dist = r => { const d = Math.pow(10, (-59 - r) / 20); return d < 10 ? d.toFixed(1) : String(Math.round(d)); };
BU.apiBase = () => T.apiBase();
// number that does not flicker: a new value is accepted only after holding for 1.5 s
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

// ---- perspective radar: disc, rings, wedge beam with a wall, light pillars ----
// opt: {sel, u, t, labels (how many to label), hits}
BU.scene = (g, cam, sw, opt) => {
  const { u, t } = opt;
  T.floorGrid(g, cam, 1.7, 0.2);
  for (let i = 1; i <= 4; i++) {
    g.beginPath();
    for (let k = 0; k <= 96; k++) { const a = k / 96 * T.TAU, p = cam.p(Math.cos(a) * i / 4, 0, Math.sin(a) * i / 4); k ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]); }
    g.strokeStyle = i === 4 ? T.C.acc : `rgba(${T.RGB.dim},.5)`; g.lineWidth = i === 4 ? 1.6 : 1; g.stroke();
  }
  g.beginPath();
  for (let a = 0; a < 360; a += 10) { const r = a * Math.PI / 180, l = a % 30 ? 1.03 : 1.07, p = cam.p(Math.cos(r), 0, Math.sin(r)), q = cam.p(Math.cos(r) * l, 0, Math.sin(r) * l); g.moveTo(p[0], p[1]); g.lineTo(q[0], q[1]); }
  g.strokeStyle = T.C.acc; g.lineWidth = 1; g.stroke();
  // beam wedge on the floor
  const tail = 1.3, N = 26, o = cam.p(0, 0, 0), s = sw.a;
  for (let i = 0; i < N; i++) {
    const a0 = s - tail * (1 - i / N), a1 = s - tail * (1 - (i + 1) / N), v = (i + 1) / N, p = cam.p(Math.cos(a0), 0, Math.sin(a0)), q = cam.p(Math.cos(a1), 0, Math.sin(a1));
    g.fillStyle = T.heat(0.2 + 0.65 * v, 0.3 * v * v); g.beginPath(); g.moveTo(o[0], o[1]); g.lineTo(p[0], p[1]); g.lineTo(q[0], q[1]); g.closePath(); g.fill();
  }
  { // beam wall
    const rx = Math.cos(s), rz = Math.sin(s), a = cam.p(0, 0, 0), b = cam.p(rx, 0, rz), c = cam.p(rx, 0.2, rz), d = cam.p(0, 0.2, 0);
    const gr = g.createLinearGradient(a[0], a[1], d[0], d[1]); gr.addColorStop(0, `rgba(${T.RGB.acc},.22)`); gr.addColorStop(1, `rgba(${T.RGB.acc},0)`);
    g.fillStyle = gr; g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.lineTo(c[0], c[1]); g.lineTo(d[0], d[1]); g.closePath(); g.fill();
    g.strokeStyle = T.C.hot; g.lineWidth = 2; g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke();
  }
  // pillars: far ones first
  const list = BU.list(), rank = [...list].sort((a, b) => b.rs - a.rs);
  const items = list.map(e => { const r = BU.rad(e.rs), a = BU.ang(e.id), x = Math.cos(a) * r, z = Math.sin(a) * r; return { e, x, z, p0: cam.p(x, 0, z) }; }).sort((a, b) => b.p0[3] - a.p0[3]);
  let selTop = null;
  for (const it of items) {
    const e = it.e, sg = T.pct(e.rs), hh = (0.06 + 0.55 * sg * sg) * e.h, p0 = it.p0, p1 = cam.p(it.x, hh, it.z);
    const since = (((s - BU.ang(e.id)) % T.TAU) + T.TAU) % T.TAU / T.TAU, ph = 1 - since, fog = T.clamp(1.15 - (p0[3] + 1) * 0.28, 0.35, 1) * e.h;
    const pa = sw.ping(e.id, t);
    if (pa < 1.1) {
      g.strokeStyle = BU.tr(e) ? `rgba(${T.RGB.alert},${(1 - pa / 1.1) * 0.9 * e.h})` : `rgba(${T.RGB.acc},${(1 - pa / 1.1) * 0.9 * e.h})`; g.lineWidth = 1.2; g.beginPath();
      for (let i = 0; i <= 28; i++) { const a = i / 28 * T.TAU, rr = 0.03 + pa * 0.15, q = cam.p(it.x + Math.cos(a) * rr, 0, it.z + Math.sin(a) * rr); i ? g.lineTo(q[0], q[1]) : g.moveTo(q[0], q[1]); }
      g.stroke();
    }
    const col = BU.rgb(e, 0.35 + 0.65 * sg);
    const gr = g.createLinearGradient(p0[0], p0[1], p1[0], p1[1]); gr.addColorStop(0, T.rgb(col, 0.05 * fog)); gr.addColorStop(1, T.rgb(col, (0.5 + 0.5 * ph) * fog));
    g.strokeStyle = gr; g.lineWidth = Math.max(1.5, p0[2] * 0.016); g.lineCap = 'round'; g.beginPath(); g.moveTo(p0[0], p0[1]); g.lineTo(p1[0], p1[1]); g.stroke(); g.lineCap = 'butt';
    const rr = Math.max(0.5, p1[2] * (0.014 + 0.02 * sg) * (1 + 0.6 * Math.max(0, 1 - pa * 2)) || 0);
    g.fillStyle = T.rgb(BU.tr(e) ? col : T.heatRGB(0.55 + 0.45 * ph), fog); g.beginPath(); g.arc(p1[0], p1[1], rr, 0, T.TAU); g.fill();
    g.fillStyle = T.rgb(col, 0.5 * fog); g.beginPath(); g.ellipse(p0[0], p0[1], p0[2] * 0.02, p0[2] * 0.02 * Math.sin(cam.pitch), 0, 0, T.TAU); g.fill();
    if (opt.hits) { T.hits.circle(p1[0], p1[1], Math.max(u * 1.6, rr * 2.5), { k: 'dev', id: e.id }); T.hits.circle((p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2, u * 1.2, { k: 'dev', id: e.id }); }
    if (e.id === opt.sel) selTop = { p: p1, e };
    else if (opt.labels && rank.indexOf(e) < opt.labels && opt.sel == null) { g.font = `500 ${u * 0.72}px ${T.FC}`; T.txt(g, T.fit(g, BU.label(e).toUpperCase(), u * 9), p1[0] + rr + u * 0.5, p1[1] + u * 0.25, u * 0.72, BU.tr(e) ? T.C.alert : T.C.label, 'c'); }
  }
  return selTop;
};

BU.sceneAny = (g, cam, sw, opt) => BU.scene(g, cam, sw, opt);

// ---- GPU CITY (WebGL): box towers with windows, wet asphalt with reflections, fog ----
// Shader projection matches T.orbit exactly, so labels and touches (cam.p) land on the towers.
// Shared by Bluetooth > City and PC > City.
// towers: [{x, z, sx, sz, h, col:[r,g,b] 0..255, roof:[r,g,b], seed, glow 0..1, tr, sel}]
// opt: {t, rings:[radii], rmax, grid:'radial'|'square', step, ext}
BU.glCity = (() => {
  const MAXP = 48, STRIDE = 14;
  let P = null, PG = null, PI = null, data = new Float32Array(MAXP * 30 * STRIDE), failed = false;
  const VS = `
attribute vec3 pos;attribute vec3 fuv;attribute float face;attribute vec3 col;attribute vec4 ex;
uniform vec4 cam1;uniform vec4 cam2;uniform vec2 scr;uniform float mir;
varying vec3 vuv;varying float vface;varying vec3 vcol;varying vec4 vex;varying float vdep;varying float vy;
void main(){vec3 p=pos;p.y*=mir;
 float x1=p.x*cam1.x-p.z*cam1.y,z1=p.x*cam1.y+p.z*cam1.x;float y2=p.y*cam1.z+z1*cam1.w,z2=-p.y*cam1.w+z1*cam1.z;
 float wv=max(.05,cam2.x+z2);
 gl_Position=vec4((2.*cam2.z/scr.x-1.)*wv+2.*x1*cam2.y/scr.x,(1.-2.*cam2.w/scr.y)*wv+2.*y2*cam2.y/scr.y,((wv-.05)/12.*2.-1.)*wv,wv);
 vuv=fuv;vface=face;vcol=col;vex=ex;vdep=wv;vy=pos.y;}`;
  // towers: facade with windows (floors every 0.035, columns every 0.03), glowing edges, roof
  const FS = `precision highp float;
varying vec3 vuv;varying float vface;varying vec3 vcol;varying vec4 vex;varying float vdep;varying float vy;
uniform float t;uniform float refl;uniform float fogA;uniform float fogB;
float h1(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
void main(){
 vec3 c=vcol;float h=vex.x,seed=vex.y,glow=vex.z,fl=vex.w;float sel=step(1.5,fl);
 vec3 col;float a=1.;
 if(vface<3.5){
  float along=vuv.x,y=vuv.y,wd=vuv.z;
  vec3 base=c*(.07+.05*mod(vface,2.))+vec3(.01,.02,.03);
  vec2 cell=vec2(floor(along/.03),floor(y/.035));vec2 lc=vec2(fract(along/.03),fract(y/.035));
  float win=step(.22,lc.x)*step(lc.x,.78)*step(.28,lc.y)*step(lc.y,.72)*step(.02,y)*step(y,h-.015)*step(.012,along)*step(along,wd-.012);
  float r=h1(cell+seed*13.1),lit=step(1.-(.22+.55*glow),r);
  float fl2=.85+.15*sin(t*(1.+r*3.)+r*40.);
  vec3 wc=mix(c,vec3(1.),.35+.3*r);
  col=base+win*(lit*wc*fl2*1.1+(1.-lit)*c*.06);
  float edge=1.-smoothstep(0.,.006,min(along,wd-along));
  float topb=smoothstep(h-.012,h,y);
  col+=c*(edge*.55+topb*.9)+(sel>.5?vec3(.8)*edge:vec3(0.));
  col+=c*exp(-y*28.)*.35;
 } else {
  vec2 q=vuv.xy;float e=1.-smoothstep(0.,.006,min(min(q.x,vuv.z-q.x),min(q.y,vuv.z-q.y)));
  col=c*(.55+.35*glow)+c*e*.8+(sel>.5?vec3(.9)*e:vec3(0.));
 }
 float fog=smoothstep(fogA,fogB,vdep);a*=1.-fog*.8;
 if(refl>0.){a*=refl*exp(-vy*5.);col*=.9;}
 gl_FragColor=vec4(col*a,a);}`;
  // ground: wet asphalt; rings/spokes or square grid; puddles; light spots under towers
  const FG = `#extension GL_OES_standard_derivatives : enable
precision highp float;
varying vec3 vuv;varying float vdep;
uniform float t;uniform float rmax;uniform float rings[6];uniform float mode;uniform float step0;
uniform vec4 tw[${MAXP}];uniform vec3 tc[${MAXP}];uniform float n;uniform float fogA;uniform float fogB;
${T.GL ? T.GL.NOISE : ''}
float line(float d,float w){float f=fwidth(d);return 1.-smoothstep(w,w+f*1.5,abs(d));}
void main(){
 vec2 p=vuv.xz;float r=length(p),ang=atan(p.y,p.x);
 float edge=smoothstep(rmax+.28,rmax-.05,r);
 if(edge<=0.)discard;
 float pud=smoothstep(.48,.62,fbm(p*2.6+3.));
 vec3 col;float a;
  col=vec3(.012,.03,.042)+vec3(.0,.02,.03)*fbm(p*9.);
  a=mix(.9,.5,pud)*edge;
  float gl=0.;
  if(mode<.5){for(int i=0;i<6;i++){if(rings[i]>0.)gl+=line(r-rings[i],.0025)*(i==3?.8:.45);}
   gl+=line(r-rmax,.004);float sp=mod(ang,.5236)-.2618;gl+=line(sp*r,.002)*.35*step(.1,r);}
  else{vec2 gq=mod(p+step0*.5,step0)-step0*.5;gl+=(line(gq.x,.0018)+line(gq.y,.0018))*.4;}
  col+=vec3(.15,.55,.65)*gl*.55;
  vec3 pool=vec3(0.);
  for(int i=0;i<${MAXP};i++){if(float(i)>=n)break;vec2 d=p-tw[i].xy;float dd=dot(d,d);
   pool+=tc[i]*(exp(-dd/(.006+.012*tw[i].z))*(.35+.5*tw[i].z)+exp(-dd/.12)*.06);}
  col+=pool*mix(.8,1.6,pud);
  col+=vec3(.25,.6,.7)*pud*.04*(.5+.5*sin(p.x*40.+t*.5));
 float fog=smoothstep(fogA,fogB,vdep);a*=1.-fog*.75;
 gl_FragColor=vec4(col*a,a);}`;
  const VG = `
attribute vec3 pos;uniform vec4 cam1;uniform vec4 cam2;uniform vec2 scr;varying vec3 vuv;varying float vdep;
void main(){vec3 p=pos;float x1=p.x*cam1.x-p.z*cam1.y,z1=p.x*cam1.y+p.z*cam1.x;float y2=p.y*cam1.z+z1*cam1.w,z2=-p.y*cam1.w+z1*cam1.z;
 float wv=max(.05,cam2.x+z2);
 gl_Position=vec4((2.*cam2.z/scr.x-1.)*wv+2.*x1*cam2.y/scr.x,(1.-2.*cam2.w/scr.y)*wv+2.*y2*cam2.y/scr.y,((wv-.05)/12.*2.-1.)*wv,wv);
 vuv=pos;vdep=wv;}`;
  const init = () => {
    if (P || failed) return !!P;
    P = T.GL.prog(FS, VS); PG = T.GL.prog(FG, VG);
    if (!P || !PG) { failed = true; P = null; return false; }
    const gd = []; const N = 48, R = 2.1;                 // ground: square grid (fine, for correct perspective)
    for (let i = 0; i < N; i++) for (let j = 0; j < N; j++) {
      const x0 = -R + 2 * R * i / N, x1 = -R + 2 * R * (i + 1) / N, z0 = -R + 2 * R * j / N, z1 = -R + 2 * R * (j + 1) / N;
      gd.push(x0, 0, z0, x1, 0, z0, x1, 0, z1, x0, 0, z0, x1, 0, z1, x0, 0, z1);
    }
    T.GL.buf('cityGround', new Float32Array(gd)); PG.nGround = gd.length / 3;
    return true;
  };
  let k = 0;
  const v = (x, y, z, a, b, c, f, col, ex) => { const o = k * STRIDE; data[o] = x; data[o + 1] = y; data[o + 2] = z; data[o + 3] = a; data[o + 4] = b; data[o + 5] = c; data[o + 6] = f; data[o + 7] = col[0]; data[o + 8] = col[1]; data[o + 9] = col[2]; data[o + 10] = ex[0]; data[o + 11] = ex[1]; data[o + 12] = ex[2]; data[o + 13] = ex[3]; k++; };
  const camU = (cam, W, H, vals) => { vals.cam1 = [cam.cs, cam.sn, cam.cp, cam.sp]; vals.cam2 = [cam.D, cam.F, cam.cx, cam.cy]; vals.scr = [W, H]; return vals; };
  const attrib = (prog, name, size, off) => { const gl = T.GL.gl, l = prog.a[name]; if (l == null || l < 0) return; gl.enableVertexAttribArray(l); gl.vertexAttribPointer(l, size, gl.FLOAT, false, STRIDE * 4, off * 4); };
  return {
    ok: () => T.GL && T.GL.ok && init(),
    // draw the whole scene into the shared GL canvas and copy it onto the view's 2D canvas
    render(g, cam, W, H, towers, opt) {
      if (!this.ok()) return false;
      const GL = T.GL, gl = GL.gl;
      GL.begin(W, H, opt.scale || 1);
      // tower vertices (order does not matter: depth buffer)
      k = 0;
      const list = towers.slice(0, MAXP);
      for (const b of list) {
        const col = b.col.map(q => q / 255), roof = (b.roof || b.col).map(q => q / 255), ex = [b.h, b.seed % 97, b.glow, (b.tr ? 1 : 0) + (b.sel ? 2 : 0)];
        const c = [[b.x - b.sx, b.z - b.sz], [b.x + b.sx, b.z - b.sz], [b.x + b.sx, b.z + b.sz], [b.x - b.sx, b.z + b.sz]];
        for (let i = 0; i < 4; i++) {
          const A = c[i], B = c[(i + 1) % 4], wd = i % 2 ? b.sz * 2 : b.sx * 2;
          v(A[0], 0, A[1], 0, 0, wd, i, col, ex); v(B[0], 0, B[1], wd, 0, wd, i, col, ex); v(B[0], b.h, B[1], wd, b.h, wd, i, col, ex);
          v(A[0], 0, A[1], 0, 0, wd, i, col, ex); v(B[0], b.h, B[1], wd, b.h, wd, i, col, ex); v(A[0], b.h, A[1], 0, b.h, wd, i, col, ex);
        }
        const s2 = b.sx * 2;
        v(c[0][0], b.h, c[0][1], 0, 0, s2, 4, roof, ex); v(c[1][0], b.h, c[1][1], s2, 0, s2, 4, roof, ex); v(c[2][0], b.h, c[2][1], s2, s2, s2, 4, roof, ex);
        v(c[0][0], b.h, c[0][1], 0, 0, s2, 4, roof, ex); v(c[2][0], b.h, c[2][1], s2, s2, s2, 4, roof, ex); v(c[3][0], b.h, c[3][1], 0, s2, s2, 4, roof, ex);
      }
      const fogA = cam.D + 0.2, fogB = cam.D + 2.2;
      const drawTowers = (mir, refl) => {
        if (!k) return;
        gl.useProgram(P.p); GL.buf('cityTw', data.subarray(0, k * STRIDE), false, true);
        attrib(P, 'pos', 3, 0); attrib(P, 'fuv', 3, 3); attrib(P, 'face', 1, 6); attrib(P, 'col', 3, 7); attrib(P, 'ex', 4, 10);
        GL.set(P, camU(cam, W, H, { mir, refl, t: opt.t, fogA, fogB }));
        gl.drawArrays(gl.TRIANGLES, 0, k);
        for (const n of ['pos', 'fuv', 'face', 'col', 'ex']) if (P.a[n] >= 0) gl.disableVertexAttribArray(P.a[n]);
      };
      gl.enable(gl.BLEND); gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
      gl.enable(gl.DEPTH_TEST); gl.depthFunc(gl.LEQUAL);
      drawTowers(-1, 0.3); gl.clear(gl.DEPTH_BUFFER_BIT);                     // reflections in wet asphalt
      // ground (reuse scratch Float32Arrays across frames)
      if (!PG.twU) { PG.twU = new Float32Array(MAXP * 4); PG.tcU = new Float32Array(MAXP * 3); PG.rings = new Float32Array(6); }
      gl.disable(gl.DEPTH_TEST);
      gl.useProgram(PG.p); GL.buf('cityGround');
      const lp = PG.a.pos; gl.enableVertexAttribArray(lp); gl.vertexAttribPointer(lp, 3, gl.FLOAT, false, 0, 0);
      const twU = PG.twU, tcU = PG.tcU, rings = PG.rings;
      twU.fill(0); tcU.fill(0); rings.fill(0);
      list.forEach((b, i) => { twU[i * 4] = b.x; twU[i * 4 + 1] = b.z; twU[i * 4 + 2] = b.glow; twU[i * 4 + 3] = 0; const cc = b.tr ? T.ALERT : b.col; tcU[i * 3] = cc[0] / 255; tcU[i * 3 + 1] = cc[1] / 255; tcU[i * 3 + 2] = cc[2] / 255; });
      (opt.rings || []).slice(0, 6).forEach((r, i) => rings[i] = r);
      GL.set(PG, camU(cam, W, H, { t: opt.t, rmax: opt.rmax || 1.5, rings, mode: opt.grid === 'square' ? 1 : 0, step0: opt.step || 0.2,
        tw: twU, tc: tcU, n: list.length, fogA, fogB }));
      gl.drawArrays(gl.TRIANGLES, 0, PG.nGround); gl.disableVertexAttribArray(lp);
      // towers
      gl.enable(gl.DEPTH_TEST); gl.clear(gl.DEPTH_BUFFER_BIT); drawTowers(1, 0);
      gl.disable(gl.DEPTH_TEST);
      GL.draw(g, 0, 0, W, H);
      return true;
    },
  };
})();

// callout card at the target (Scope and City)
BU.card = (g, p, e, u, W, H, top) => {
  const sg = T.pct(e.rs), bw = u * 15, bh = u * 8.2;
  let bx = p[0] + u * 4, by = p[1] - u * 7;
  if (bx + bw > W - u) bx = p[0] - u * 4 - bw;
  by = T.clamp(by, top, H - bh - u * 3);
  T.diamond(g, p[0], p[1], u * 0.9, T.C.hot, false);
  const ax = bx > p[0] ? bx : bx + bw;
  g.strokeStyle = T.C.hot; g.lineWidth = 1; g.beginPath(); g.moveTo(p[0] + (bx > p[0] ? 1 : -1) * u * 0.7, p[1] - u * 0.7); g.lineTo(ax + (bx > p[0] ? -1 : 1) * u * 1.2, by + bh * 0.55); g.lineTo(ax, by + bh * 0.55); g.stroke();
  T.panel(g, bx, by, bw, bh, u, `rgba(${T.RGB.bg},.78)`, T.C.acc);
  g.font = `500 ${u * 1.2}px ${T.FC}`; T.txt(g, T.fit(g, BU.label(e).toUpperCase(), bw - u * 2), bx + u, by + u * 1.7, u * 1.2, T.C.hot, 'c');
  g.font = `${u * 0.75}px ${T.FM}`; T.txt(g, T.fit(g, ((e.vend || '—') + ' · ' + (e.type || '')).toUpperCase(), bw - u * 2), bx + u, by + u * 2.8, u * 0.75, BU.tr(e) ? T.C.alert : T.C.label);
  T.bigNum(g, Math.round(e.rs), bx + u, by + u * 5.6, u * 2.8, BU.tr(e) ? T.C.alert : T.heat(0.4 + 0.6 * sg), 'DBM');
  T.spark(g, bx + u * 7.5, by + u * 3.6, bw - u * 8.5, u * 2.1, e.hist.slice(-40), -100, -35, BU.tr(e) ? T.C.alert : T.C.acc);
  T.segBar(g, bx + u, by + u * 6.6, bw - u * 2, u * 0.4, sg, 16, false, BU.tr(e));
  T.hits.rect(bx, by, bw, bh, { k: 'card' });
};

// sticky order: neighbors swap only if the difference is > 6 dB (like ORDER_HYST_DB on the stick)
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

// list rows; ry glides to its place
BU.rows = (g, x, y, w, h, u, rowH, list, sel, dt, cols = {}) => {
  const fs = rowH * 0.42;
  T.txt(g, 'T', x + u * 0.35, y + u * 0.8, u * 0.7, T.C.dim);
  T.txt(g, 'NAME', x + rowH * 1.05, y + u * 0.8, u * 0.7, T.C.dim);
  if (cols.more) { T.txt(g, 'MACS', x + w * 0.5, y + u * 0.8, u * 0.7, T.C.dim); T.txt(g, 'NEAR', x + w * 0.58, y + u * 0.8, u * 0.7, T.C.dim); }
  T.txt(g, 'DBM', x + w - u * 0.4, y + u * 0.8, u * 0.7, T.C.dim, 'm', 'right');
  const y0 = y + u * 1.3;
  g.save(); g.beginPath(); g.rect(x - 2, y0, w + 4, h - u * 1.3); g.clip();
  list.forEach((e, i) => {
    if (e._ry == null) e._ry = i;
    e._ry += (i - e._ry) * T.ease(dt, 0.25);
    const cy = y0 + e._ry * rowH;
    if (cy > y + h || cy + rowH < y0) return;
    const on = e.id === sel, tr = BU.tr(e), sg = T.pct(e.rs), gs = rowH * 0.62, col = tr ? T.C.alert : (on ? T.C.hot : T.C.acc);
    g.globalAlpha = T.clamp(e.h);
    if (on) { g.fillStyle = `rgba(${T.RGB.acc},.08)`; g.fillRect(x, cy, w, rowH); T.brackets(g, x, cy, w, rowH, u * 0.45, T.C.acc, 1); }
    T.glyph(g, x + u * 0.3, cy + (rowH - gs) / 2, gs, BU.glyph(e), col);
    const nameW = (cols.more ? w * 0.5 : w * 0.62) - rowH * 1.05 - u * 0.3;
    g.font = `500 ${fs}px ${T.FC}`;
    T.txt(g, T.fit(g, BU.label(e).toUpperCase(), nameW), x + rowH * 1.05, cy + rowH * 0.45, fs, on ? T.C.hot : (tr ? T.C.alert : T.C.label), 'c');
    g.font = `${fs * 0.75}px ${T.FM}`;
    T.txt(g, T.fit(g, ((e.vend || '—') + (tr ? ' · TRACKER' : '')).toUpperCase(), nameW), x + rowH * 1.05, cy + rowH * 0.82, fs * 0.75, tr ? T.C.alert : T.C.dim);
    if (cols.more) { T.txt(g, String(e.macs || 1), x + w * 0.5, cy + rowH * 0.6, fs * 0.85, T.C.label); T.txt(g, T.ago(e.age || 0), x + w * 0.58, cy + rowH * 0.6, fs * 0.85, T.C.dim); }
    const bw = cols.more ? w * 0.16 : w * 0.22;
    T.segBar(g, x + w - u * 0.4 - u * 3.2 - bw, cy + rowH * 0.36, bw, rowH * 0.2, sg, 10, false, tr);
    T.txt(g, Math.round(e.rs), x + w - u * 0.4, cy + rowH * 0.66, rowH * 0.55, tr ? T.C.alert : T.heat(0.4 + 0.6 * sg), 't', 'right');
    T.hits.rect(x, cy, w, rowH, { k: 'dev', id: e.id });
    g.globalAlpha = 1;
  });
  g.restore();
};

T.views.push({
  id: 'ble', title: 'Bluetooth', want: 'ble', subs: ['Scope', 'City', 'Devices'], sub: 0,
  mount(main) {
    this.root = T.el('div', 'view'); const st = T.el('div', 'stage'); this.root.appendChild(st); this.cv = T.canvas(st); main.appendChild(this.root);
    this.cam = T.orbit(0.5, 0.98, 3.2); this.camT = { yaw: 0.5, D: 3.2 };
    this.drag = T.drag(this.cv.cv, this.cam, { minPitch: 0.5, maxPitch: 1.35 });
    this.drag.onTap = (x, y) => this.tap(x, y);
    this.sw = BU.sweeper(); this.dust = T.dust(220); this.sel = null; this.sticky = BU.sticky(); this.info = null; this.infoT = 0;
    this.onSub(this.sub);
  },
  unmount() { this.cv.dispose(); this.root.remove(); },
  onSub(i) {
    if (i === 1) { this.cam.pitch = 0.62; this.cam.D = 3.6; this.camT.D = 3.6; } else { this.cam.pitch = 0.98; this.camT.D = 3.2; }
    this.camT.yaw = this.cam.yaw;
  },
  tap(x, y) {
    const h = T.hits.at(x, y);
    if (!h) { if (this.sub !== 2) this.sel = null; return; }
    if (h.k === 'dev') { this.sel = (this.sel === h.id && this.sub !== 2) ? null : h.id; this.info = null; this.infoT = 0; }
    else if (h.k === 'back') this.sel = null;
    else if (h.k === 'name') { const e = S.dev.get(this.sel); if (e && T.nameDevice) T.nameDevice(e, e.label || (this.info && this.info.label) || ''); }
  },
  // details of the stick-selected device: addresses, Company ID, services
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
    if (this.drag.down && this.drag.moved && this.sub !== 2) this.sel = null;
    T.backdrop(g, w, h, portrait ? 0.5 : 0.4, 0.55);
    const n = BU.list().length;
    if (this.sub === 0) this.scope(g, w, h, u, m, y0, portrait, dt, t);
    else if (this.sub === 1) this.city(g, w, h, u, m, y0, portrait, dt, t);
    else this.devices(g, w, h, u, m, y0, portrait, dt, t);
    if (!S.ble) T.emptyState(g, w, h, u, 'WAITING FOR TENGEN', 'connect the tablet to the TENGEN Wi-Fi');
    else if (!n) T.emptyState(g, w, h, u, 'NO DEVICES', S.ble.on ? 'listening…' : 'bluetooth is off');
    T.finish(g, w, h);
  },
  camStep(dt) {
    const c = this.cam;
    if (this.sel != null) { const e = S.dev.get(this.sel); if (e && this.sub === 0) this.camT.yaw = -Math.PI / 2 - BU.ang(e.id) - 0.32; this.camT.D = this.sub === 1 ? 3.0 : 2.85; }
    else { if (this.drag.idle()) this.camT.yaw = c.yaw + dt * (this.sub === 1 ? 0.05 : 0.07); else this.camT.yaw = c.yaw; this.camT.D = this.sub === 1 ? 3.6 : 3.2; }
    const dy = ((this.camT.yaw - c.yaw + Math.PI) % T.TAU + T.TAU) % T.TAU - Math.PI;
    if (!this.drag.down) c.yaw += dy * Math.min(1, dt * 2.4);
    c.D = T.lerp(c.D, this.camT.D, Math.min(1, dt * 2.4));
  },
  hud(g, w, h, u, m, y0, title, status) {
    T.header(g, m, y0, w - 2 * m, u, title, status, false);
    T.dots(g, m, y0 + u * 2.75, u, 3, this.sub);
  },
  // ---------- SCOPE ----------
  scope(g, w, h, u, m, y0, portrait, dt, t) {
    this.camStep(dt);
    const c = this.cam;
    c.frame(portrait ? w * 0.5 : w * 0.37, portrait ? y0 + (h - y0) * 0.27 : y0 + (h - y0) * 0.55, portrait ? w * 0.9 : Math.min((h - y0) * 1.42, w * 0.95));
    const selTop = BU.sceneAny(g, c, this.sw, { u, t, sel: this.sel, labels: 3, hits: true });
    this.dust.draw(g, c);
    T.bloom(this.cv);
    const list = BU.list(), trk = list.some(BU.tr);
    this.hud(g, w, h, u, m, y0, 'Scope · depth', `${list.length} devices · cam ${Math.round(((c.yaw * 180 / Math.PI) % 360 + 360) % 360)}°${trk ? ' · tracker' : ''}`);
    const lst = this.sticky.get(list, performance.now(), false);
    if (!portrait) {
      const lw = w * 0.27, lx = w - m - lw, ly = y0 + u * 4.2, lh = Math.min(h - ly - u * 4, u * 2.25 * 12 + u * 2);
      T.panel(g, lx, ly, lw, lh, u, `rgba(${T.RGB.panel},.25)`, `rgba(${T.RGB.acc},.25)`);
      BU.rows(g, lx + u * 0.5, ly + u * 0.6, lw - u, lh - u * 1.2, u, u * 2.25, lst, this.sel, dt);
    } else {
      const ly = y0 + (h - y0) * 0.6, lh = h - ly - u * 3;
      T.panel(g, m, ly, w - 2 * m, lh, u, `rgba(${T.RGB.panel},.25)`, `rgba(${T.RGB.acc},.25)`);
      BU.rows(g, m + u * 0.5, ly + u * 0.6, w - 2 * m - u, lh - u * 1.2, u, u * 2.4, lst, this.sel, dt, { more: true });
    }
    if (selTop) BU.card(g, selTop.p, selTop.e, u, portrait ? w : w * 0.7, h, y0 + u * 4);
    T.footer(g, m, h - u * 2.4, w - 2 * m, u, 'drag: orbit  ·  tap pillar: focus', 'height = signal · ring = sweep hit');
  },
  // ---------- CITY ----------
  city(g, w, h, u, m, y0, portrait, dt, t) {
    this.camStep(dt);
    const c = this.cam;
    c.frame(w * 0.5, y0 + (h - y0) * (portrait ? 0.5 : 0.58), portrait ? w * 0.78 : Math.min((h - y0) * 1.2, w * 0.72));
    // radial grid: rings = stick range thresholds (settings.h RANGES), spokes every 30°
    const RINGS = [[-60, 'VERY NEAR'], [-70, 'NEAR'], [-80, 'ROOM'], [-90, 'FAR']], RMAX = 1.55;
    const ringR = db => { const P = [[-45, 0.22], [-60, 0.42], [-70, 0.68], [-80, 0.96], [-90, 1.24], [-100, 1.5]];
      if (db >= P[0][0]) return P[0][1]; for (let i = 1; i < P.length; i++) if (db >= P[i][0]) { const a = P[i - 1], b = P[i]; return b[1] + (a[1] - b[1]) * (db - b[0]) / (a[0] - b[0]); } return P[P.length - 1][1]; };
    const ring = (r, col, lw) => { g.beginPath(); for (let i = 0; i <= 96; i++) { const a = i / 96 * T.TAU, q = c.p(Math.cos(a) * r, 0, Math.sin(a) * r); i ? g.lineTo(q[0], q[1]) : g.moveTo(q[0], q[1]); } g.strokeStyle = col; g.lineWidth = lw; g.stroke(); };
    const useGL = BU.glCity.ok();                         // GPU: ground and towers by shader; 2D is the fallback
    if (!useGL) {
      g.lineWidth = 1; g.strokeStyle = `rgba(${T.RGB.line},.55)`; g.beginPath();
      for (let a = 0; a < 360; a += 30) { const r = a * Math.PI / 180, p0 = c.p(Math.cos(r) * 0.1, 0, Math.sin(r) * 0.1), p1 = c.p(Math.cos(r) * RMAX, 0, Math.sin(r) * RMAX); g.moveTo(p0[0], p0[1]); g.lineTo(p1[0], p1[1]); }
      g.stroke();
      RINGS.forEach(([db], i) => ring(ringR(db), i === 3 ? `rgba(${T.RGB.acc},.35)` : `rgba(${T.RGB.dim},.4)`, 1));
      ring(RMAX, `rgba(${T.RGB.acc},.5)`, 1.4);
    }
    const list = BU.list(), rank = [...list].sort((a, b) => b.rs - a.rs);
    const SZ = { computer: 0.09, TV: 0.1, tv: 0.1, phone: 0.065, audio: 0.06, watch: 0.05, band: 0.045, tracker: 0.045, sensor: 0.05 };
    // tower position: distance from center = signal strength (rings), angle = from id with small jitter;
    // neighbors push apart, everything glides
    for (const e of list) {
      const ang = (T.hash(e.id) % 3600) / 3600 * T.TAU, rr = ringR(e.rs) + (((T.hash(e.id * 7 + 3) & 255) / 255) - 0.5) * 0.12;
      e._tx = Math.cos(ang) * rr; e._tz = Math.sin(ang) * rr;
      if (e._cx == null) { e._cx = e._tx; e._cz = e._tz; }
    }
    for (let i = 0; i < list.length; i++) for (let j = i + 1; j < list.length; j++) {
      const a = list[i], b = list[j], dx = b._tx - a._tx, dz = b._tz - a._tz, d = Math.hypot(dx, dz) || 1e-3, min = 0.26;
      if (d < min) { const k = (min - d) / 2 / d; a._tx -= dx * k; a._tz -= dz * k; b._tx += dx * k; b._tz += dz * k; }
    }
    const ke = T.ease(dt, 0.9);
    for (const e of list) { e._cx += (e._tx - e._cx) * ke; e._cz += (e._tz - e._cz) * ke; }
    const towers = list.map(e => {
      const x = e._cx, z = e._cz;
      const sg = T.pct(e.rs); e._ch = e._ch == null ? 0 : e._ch + ((0.05 + 0.7 * Math.pow(sg, 1.3)) * e.h - e._ch) * T.ease(dt, 0.6);
      return { e, x, z, s: SZ[e.type] || 0.055, hh: e._ch, sg, d: c.p(x, 0, z)[3] };
    }).sort((a, b) => b.d - a.d);
    let selP = null; const labels = [];
    if (useGL) BU.glCity.render(g, c, w, h, towers.map(tw => {
      const e = tw.e, tr = BU.tr(e);
      return { x: tw.x, z: tw.z, sx: tw.s, sz: tw.s, h: Math.max(0.01, tw.hh), col: BU.rgb(e, 0.35 + 0.65 * tw.sg), roof: tr ? T.ALERT : T.heatRGB(0.55 + 0.45 * tw.sg),
        seed: T.hash(e.id) % 997, glow: tw.sg, tr, sel: e.id === this.sel };
    }), { t, rings: RINGS.map(([db]) => ringR(db)), rmax: RMAX, grid: 'radial' });
    for (const [db, name] of [...RINGS, [-127, 'ALL']]) {             // ring labels on the near side
      const r = db < -100 ? RMAX : ringR(db), a = -c.yaw - Math.PI / 2 + 0.22, q = c.p(Math.cos(a) * r, 0, Math.sin(a) * r);
      T.txt(g, name + (db > -100 ? '  ' + db : ''), q[0] + u * 0.3, q[1] - u * 0.3, u * 0.66, db === -90 || db < -100 ? T.C.acc : T.C.dim);
    }
    for (const tw of towers) {
      const { e, x, z, s, hh, sg } = tw, tr = BU.tr(e), fog = T.clamp(1.2 - (tw.d + 1) * 0.3, 0.3, 1) * T.clamp(e.h);
      const col = BU.rgb(e, 0.35 + 0.65 * sg);
      if (useGL) {                                         // tower drawn by GPU - only touches and labels here
        const top = c.p(x, hh, z), bot = c.p(x, 0, z);
        T.hits.circle(top[0], top[1], Math.max(u * 1.6, top[2] * s * 1.2), { k: 'dev', id: e.id });
        T.hits.rect(Math.min(top[0], bot[0]) - top[2] * s, top[1], top[2] * s * 2, Math.max(4, bot[1] - top[1]), { k: 'dev', id: e.id });
        if (e.id === this.sel) selP = { p: top, e };
        else if (this.sel == null && rank.indexOf(e) < 5) labels.push([top, e]);
        continue;
      }
      const P = [[-s, -s], [s, -s], [s, s], [-s, s]].map(([a, b]) => [c.p(x + a, 0, z + b), c.p(x + a, hh, z + b)]);
      // side faces: visible ones (clockwise on screen), far ones first
      const faces = [0, 1, 2, 3].map(i => { const j = (i + 1) % 4; return { i, j, d: (P[i][0][3] + P[j][0][3]) / 2 }; }).sort((a, b) => b.d - a.d);
      for (const f of faces) {
        const a = P[f.i], b = P[f.j];
        const cross = (b[0][0] - a[0][0]) * (a[1][1] - a[0][1]) - (b[0][1] - a[0][1]) * (a[1][0] - a[0][0]);
        if (cross > 0) continue;
        const lit = 0.18 + 0.22 * Math.abs(Math.sin(f.i + 0.5));
        g.fillStyle = T.rgb(col, lit * 0.4 * fog); g.beginPath(); g.moveTo(a[0][0], a[0][1]); g.lineTo(b[0][0], b[0][1]); g.lineTo(b[1][0], b[1][1]); g.lineTo(a[1][0], a[1][1]); g.closePath(); g.fill();
        g.strokeStyle = T.rgb(col, 0.45 * fog); g.lineWidth = 1; g.stroke();
        // windows: floor stripes
        g.strokeStyle = T.rgb(col, 0.18 * fog); g.beginPath();
        for (let y = 0.05; y < hh - 0.02; y += 0.05) { const k = y / Math.max(hh, 1e-3), ax = T.lerp(a[0][0], a[1][0], k), ay = T.lerp(a[0][1], a[1][1], k), bx = T.lerp(b[0][0], b[1][0], k), by = T.lerp(b[0][1], b[1][1], k); g.moveTo(ax, ay); g.lineTo(bx, by); }
        g.stroke();
      }
      // roof glows; orange for trackers
      g.fillStyle = T.rgb(tr ? T.ALERT : T.heatRGB(0.55 + 0.45 * sg), (0.45 + 0.35 * sg) * fog);
      g.beginPath(); P.forEach((q, k) => k ? g.lineTo(q[1][0], q[1][1]) : g.moveTo(q[1][0], q[1][1])); g.closePath(); g.fill();
      const top = c.p(x, hh, z);
      T.hits.circle(top[0], top[1], Math.max(u * 1.6, top[2] * s * 1.2), { k: 'dev', id: e.id });
      const bot = c.p(x, 0, z); T.hits.rect(Math.min(top[0], bot[0]) - top[2] * s, top[1], top[2] * s * 2, Math.max(4, bot[1] - top[1]), { k: 'dev', id: e.id });
      if (e.id === this.sel) selP = { p: top, e };
      else if (this.sel == null && rank.indexOf(e) < 5) labels.push([top, e]);
    }
    for (const [top, e] of labels) { const tr = BU.tr(e); g.strokeStyle = `rgba(${T.RGB.hot},.35)`; g.lineWidth = 1; g.beginPath(); g.moveTo(top[0], top[1] - u * 0.2); g.lineTo(top[0], top[1] - u * 1.4); g.stroke(); g.font = `500 ${u * 0.72}px ${T.FC}`; const s = T.fit(g, BU.label(e).toUpperCase() + '  ' + Math.round(e.rs), u * 11), tw = T.tw(g, s, u * 0.72, 'c'); g.fillStyle = `rgba(${T.RGB.bg},.7)`; g.fillRect(top[0] - tw / 2 - u * 0.3, top[1] - u * 2.4, tw + u * 0.6, u * 1.1); T.txt(g, s, top[0], top[1] - u * 1.55, u * 0.72, tr ? T.C.alert : T.C.hot, 'c', 'center'); }
    this.dust.draw(g, c);
    T.bloom(this.cv);
    this.hud(g, w, h, u, m, y0, 'City', `${list.length} towers · height = signal`);
    if (selP) BU.card(g, selP.p, selP.e, u, w, h, y0 + u * 4);
    T.footer(g, m, h - u * 2.4, w - 2 * m, u, 'drag: orbit  ·  tap tower: focus', 'roof orange = tracker');
  },
  // ---------- DEVICES ----------
  devices(g, w, h, u, m, y0, portrait, dt, t) {
    const now = performance.now(), list = BU.list(), lst = this.sticky.get(list, now, this.sel != null);
    this.hud(g, w, h, u, m, y0, 'Devices', `${S.ble ? S.ble.n : 0} devices · ${S.ble ? S.ble.m : 0} addresses`);
    const top = y0 + u * 4.2, bot = h - u * 3;
    let tx = m, tw = portrait ? w - 2 * m : w * 0.58 - m, th = portrait ? (bot - top) * 0.55 : bot - top;
    T.panel(g, tx, top, tw, th, u, `rgba(${T.RGB.panel},.22)`, T.C.line);
    BU.rows(g, tx + u * 0.6, top + u * 0.8, tw - u * 1.2, th - u * 1.6, u, u * 2.5, lst, this.sel, dt, { more: tw > u * 34 });
    const dx = portrait ? m : w * 0.58 + u, dy = portrait ? top + th + u : top, dw = portrait ? w - 2 * m : w - m - dx, dh = portrait ? bot - dy : bot - top;
    T.panel(g, dx, dy, dw, dh, u, `rgba(${T.RGB.panel},.22)`, T.C.line);
    const e = this.sel != null && S.dev.get(this.sel);
    if (e) { this.fetchInfo(now); this.detail(g, dx, dy, dw, dh, u, e); }
    else this.summary(g, dx, dy, dw, dh, u, list);
    T.footer(g, m, h - u * 2.4, w - 2 * m, u, e ? 'tap row: switch  ·  ‹ all: back' : 'tap row: details', 'order holds until 6 dB difference');
  },
  summary(g, x, y, w, h, u, list) {
    const p = u * 1.1; let cy = y + p;
    T.txt(g, '// AROUND', x + p, cy + u * 0.9, u * 1.05, T.C.acc, 't'); cy += u * 2.4;
    T.bigNum(g, list.length, x + p, cy + u * 3.4, u * 4.4, T.C.hot, 'DEVICES'); cy += u * 4.6;
    T.txt(g, 'COUNT · 3 MIN', x + p, cy + u * 0.7, u * 0.72, T.C.dim); cy += u * 1.1;
    const sh = Math.min(u * 5, h * 0.22);
    g.strokeStyle = T.C.line; g.lineWidth = 1; g.strokeRect(x + p, cy, w - 2 * p, sh);
    const hi = Math.max(5, ...S.bleHist); T.spark(g, x + p, cy, w - 2 * p, sh, S.bleHist, 0, hi * 1.15); cy += sh + u * 1.4;
    const cnt = {}; for (const e of list) cnt[e.type || 'unknown'] = (cnt[e.type || 'unknown'] || 0) + 1;
    const types = Object.entries(cnt).sort((a, b) => b[1] - a[1]), mx = Math.max(1, ...types.map(q => q[1]));
    for (const [tp, n] of types) {
      if (cy + u * 1.6 > y + h - p) break;
      const tr = tp === 'tracker';
      T.glyph(g, x + p, cy, u * 1.3, BU.G[tp] || tp[0].toUpperCase(), tr ? T.C.alert : T.C.acc);
      T.txt(g, tp.toUpperCase(), x + p + u * 1.9, cy + u * 1, u * 0.8, tr ? T.C.alert : T.C.label);
      T.segBar(g, x + w * 0.45, cy + u * 0.45, w * 0.4, u * 0.4, n / mx, 12, false, tr);
      T.txt(g, n, x + w - p, cy + u * 1.05, u * 1.1, T.C.hot, 't', 'right');
      cy += u * 1.75;
    }
  },
  detail(g, x, y, w, h, u, e) {
    const p = u * 1.1, tr = BU.tr(e), sg = T.pct(e.rs), d = this.info; let cy = y + p;
    T.txt(g, '// DEVICE', x + p, cy + u * 0.9, u * 1.05, T.C.acc, 't');
    T.txt(g, '‹ ALL', x + w - p, cy + u * 0.9, u * 0.9, T.C.label, 'm', 'right');
    T.hits.rect(x + w - p - u * 4.5, cy - u * 0.4, u * 5, u * 2, { k: 'back' });
    // 'name it' button: custom label for the device (stored on the stick)
    { const bw = u * 5.4, bx = x + w - p - u * 5 - bw - u * 0.8, by = cy - u * 0.35;
      T.panel(g, bx, by, bw, u * 1.7, u * 0.4, `rgba(${T.RGB.acc},.1)`, T.C.acc);
      T.txt(g, e.label ? 'RENAME' : 'NAME', bx + bw / 2, by + u * 1.15, u * 0.8, T.C.hot, 'm', 'center');
      T.hits.rect(bx - u * 0.3, by - u * 0.3, bw + u * 0.6, u * 2.3, { k: 'name' }); }
    cy += u * 2.6;
    g.font = `500 ${u * 1.7}px ${T.FC}`; T.txt(g, T.fit(g, BU.label(e).toUpperCase(), w - 2 * p), x + p, cy + u * 1.3, u * 1.7, T.C.hot, 'c'); cy += u * 2.1;
    g.font = `${u * 0.85}px ${T.FM}`; T.txt(g, T.fit(g, ((e.vend || '—') + ' · ' + (e.type || '') + (tr ? ' · TRACKER' : '')).toUpperCase(), w - 2 * p), x + p, cy + u * 0.6, u * 0.85, tr ? T.C.alert : T.C.label); cy += u * 1.5;
    T.bigNum(g, Math.round(e.rs), x + p, cy + u * 3.6, u * 4.6, tr ? T.C.alert : T.heat(0.4 + 0.6 * sg), 'DBM');
    T.txt(g, '≈ ' + BU.dist(e.rs) + ' M', x + w - p, cy + u * 3.5, u * 1.2, T.C.label, 'm', 'right'); cy += u * 4.6;
    T.segBar(g, x + p, cy, w - 2 * p, u * 0.55, sg, 20, true, tr); cy += u * 1.6;
    T.txt(g, 'RSSI · 2 MIN', x + p, cy + u * 0.7, u * 0.72, T.C.dim); cy += u * 1.1;
    const sh = T.clamp(h * 0.18, u * 3, u * 6);
    g.strokeStyle = T.C.line; g.lineWidth = 1; g.strokeRect(x + p, cy, w - 2 * p, sh);
    T.spark(g, x + p, cy, w - 2 * p, sh, e.hist, -100, -35, tr ? T.C.alert : T.C.acc); cy += sh + u * 1.1;
    const kv = [['ADDRESSES', String(e.macs || 1)], ['NEAR FOR', T.ago(e.age || 0)]];
    if (d) {
      if (d.rmin != null) kv.push(['RANGE', `${d.rmin} … ${d.rmax} dBm`]);
      if (d.pk != null) kv.push(['PACKETS', String(d.pk)]);
      if (d.cid) kv.push(['COMPANY ID', d.cid]);
      for (const a of (d.addr || []).slice(0, 3)) kv.push([a.mac, (a.kind || '') + ' · ' + a.r]);
      for (const s of (d.uu || []).slice(0, 3)) kv.push(['SERVICE ' + s.u, s.o || '']);
    }
    for (const [k, v] of kv) {
      if (cy + u * 1.4 > y + h - p) break;
      T.txt(g, k, x + p, cy + u * 0.9, u * 0.8, T.C.dim);
      g.font = `500 ${u * 0.8}px ${T.FC}`; T.txt(g, T.fit(g, String(v).toUpperCase(), w * 0.55), x + w - p, cy + u * 0.9, u * 0.8, T.C.hot, 'c', 'right');
      cy += u * 1.4;
    }
  },
});
