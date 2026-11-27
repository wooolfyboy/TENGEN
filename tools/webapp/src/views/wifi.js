// ---- WI-FI: Evangelion MAGI Spectral Mountain Ridges × Karesansui Spectrum Sanctuary ----
(() => {
const WF = {
  // best channel of 1/6/11: least interference from neighbors (sticky 20% hysteresis)
  bestCh(nets) {
    const sc = {};
    for (const c of [1, 6, 11]) {
      let s = 0;
      for (const n of nets) { const d = Math.abs(n.c - c); if (d < 5) s += (5 - d) * (Math.max(0, (n.rs ?? n.r) + 100)); }
      sc[c] = s;
    }
    let best = WF._best || 1;
    for (const c of [1, 6, 11]) if (sc[c] < sc[best] * 0.8) best = c;
    return (WF._best = best);
  },
  sig: r => T.clamp((r + 95) / 55),
  ssid: n => n.s ? n.s : '<hidden>',
  open: n => n.e === 'open',
  encG: n => n.e === 'open' ? 'O' : /ENT/.test(n.e || '') ? 'E' : /3/.test(n.e || '') ? '3' : /2/.test(n.e || '') ? '2' : 'W',
  dist: r => Math.pow(10, (-40 - r) / 30),
  list: () => [...S.aps.values()].filter(n => n.rs != null),
  cutPlate(g, x, y, w, h, cut, fill, stroke, lw = 1) {
    const c = Math.min(cut, w * 0.22, h * 0.35);
    g.beginPath();
    g.moveTo(x + c, y); g.lineTo(x + w, y);
    g.lineTo(x + w, y + h - c); g.lineTo(x + w - c, y + h);
    g.lineTo(x, y + h); g.lineTo(x, y + c); g.closePath();
    if (fill) { g.fillStyle = fill; g.fill(); }
    if (stroke) { g.strokeStyle = stroke; g.lineWidth = lw; g.stroke(); }
  },
};

// WebGL Karesansui (Zen dry-garden) raked spectrum floor + optimal channel sanctuary lane
const WF_GL = (() => {
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
uniform float t;uniform float bestX;uniform float scanX;uniform float fogA;uniform float fogB;
float line(float d,float w){float f=fwidth(d);return 1.-smoothstep(w,w+f*1.45,abs(d));}
void main(){
 vec2 p=vuv.xz;
 float ex=smoothstep(1.92,1.52,abs(p.x)),ez=smoothstep(1.78,1.35,abs(p.y-.28));
 float edge=ex*ez;
 if(edge<=0.)discard;
 vec3 col=vec3(.006,.016,.024);
 // Karesansui raked Zen gravel grooves along the 1..13 channel lanes
 float rake=sin(p.x*31.4159)*.5+.5;
 float zGrid=line(mod(p.y+.125,.25)-.125,.0016)*.32;
 float xGrid=line(mod(p.x+.1,.2)-.1,.0015)*.34;
 // Non-overlapping channels 1 (-1.2), 6 (-0.2), 11 (+0.8) axes
 float chMain=line(p.x+1.2,.0028)+line(p.x+.2,.0028)+line(p.x-.8,.0028);
 // Sacred optimal channel corridor
 float bLane=exp(-abs(p.x-bestX)*10.5)*(.28+.10*sin(t*2.4-p.y*3.2));
 float bEdge=line(abs(p.x-bestX)-.09,.0018)*.55;
 // Sweep laser line
 float sw=exp(-abs(p.x-scanX)*28.)*.55;
 col+=vec3(.010,.034,.048)*rake*.55+vec3(.16,.68,.80)*((xGrid+zGrid)*.55+chMain*.48+bLane+bEdge+sw);
 float a=(.88*edge)*(1.-smoothstep(fogA,fogB,vdep)*.78);
 gl_FragColor=vec4(col*a,a);}`;
  const init = () => {
    if (PG || failed) return !!PG;
    PG = T.GL.prog(FG, VG);
    if (!PG) { failed = true; return false; }
    const gd = [], N = 26, RX = 1.9, Z0 = -1.35, Z1 = 1.95;
    for (let i = 0; i < N; i++) for (let j = 0; j < N; j++) {
      const x0 = -RX + 2 * RX * i / N, x1 = -RX + 2 * RX * (i + 1) / N;
      const z0 = Z0 + (Z1 - Z0) * j / N, z1 = Z0 + (Z1 - Z0) * (j + 1) / N;
      gd.push(x0, 0, z0, x1, 0, z0, x1, 0, z1, x0, 0, z0, x1, 0, z1, x0, 0, z1);
    }
    T.GL.buf('wifiGround', new Float32Array(gd)); PG.n = gd.length / 3;
    return true;
  };
  return {
    ok: () => T.GL && T.GL.ok && init(),
    render(g, cam, W, H, t, bestX, scanX) {
      if (!this.ok()) return false;
      const GL = T.GL, gl = GL.gl;
      GL.begin(W, H, 0.85);
      gl.disable(gl.DEPTH_TEST);
      gl.enable(gl.BLEND); gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
      gl.useProgram(PG.p); GL.buf('wifiGround');
      const lp = PG.a.pos; gl.enableVertexAttribArray(lp); gl.vertexAttribPointer(lp, 3, gl.FLOAT, false, 0, 0);
      GL.set(PG, {
        cam1: [cam.cs, cam.sn, cam.cp, cam.sp],
        cam2: [cam.D, cam.F, cam.cx, cam.cy],
        scr: [W, H], t, bestX, scanX, fogA: cam.D + 0.3, fogB: cam.D + 2.5,
      });
      gl.drawArrays(gl.TRIANGLES, 0, PG.n);
      gl.disableVertexAttribArray(lp);
      GL.draw(g, 0, 0, W, H);
      return true;
    },
  };
})();

T.views.push({
  id: 'wifi', title: 'Wi-Fi', want: 'wifi', subs: ['Channels', 'Networks'], sub: 0,
  mount(main) {
    this.root = T.el('div', 'view');
    const st = T.el('div', 'stage'); this.root.appendChild(st);
    this.cv = T.canvas(st); main.appendChild(this.root);
    this.cam = T.orbit(0, 0.52, 3.35);
    this.drag = T.drag(this.cv.cv, this.cam, { minPitch: 0.25, maxPitch: 1.0 });
    this.drag.onTap = (x, y) => this.tap(x, y);
    this.dust = T.dust(150, 1.9, 1.2);
    this.z = {};
    this.order = []; this.orderT = 0;
    this.sel = null; this.t = 0;
  },
  unmount() { this.cv.dispose(); this.root.remove(); },
  onSub(i) { this.sub = i; },
  tap(x, y) {
    const h = T.hits.at(x, y);
    if (!h) { this.sel = null; return; }
    if (h.k === 'net') this.sel = this.sel === h.id ? null : h.id;
    else if (h.k === 'close') this.sel = null;
  },
  frame(dt, t) {
    this.t += dt;
    const { g, w, h } = this.cv;
    T.hits.clear();
    T.backdrop(g, w, h, 0.5, 0.58);
    const u = T.unit(w, h), m = u * 1.6, y0 = 50;
    const nets = WF.list();
    if (this.sel && !S.aps.has(this.sel)) this.sel = null;
    if (!S.wifi && !nets.length) {
      T.header(g, m, y0, w - 2 * m, u, this.sub ? 'Networks' : 'Channels', 'waiting', false);
      T.emptyState(g, w, h, u, 'SCANNING', 'first scan takes a few seconds');
      T.finish(g, w, h); return;
    }
    if (this.sub === 0) this.channels(g, w, h, u, m, y0, dt, nets);
    else this.networks(g, w, h, u, m, y0, dt, nets);
    T.finish(g, w, h);
  },

  // ---------- CHANNELS: Sculpted Topographic Spectral Mountains ----------
  channels(g, w, h, u, m, y0, dt, nets) {
    const cam = this.cam, tt = this.t, portrait = h > w * 1.05;
    this.drag.tick(dt);
    if (this.drag.idle()) cam.yaw = T.lerp(cam.yaw, 0.22 * Math.sin(tt * 0.08), T.ease(dt, 2));
    cam.yaw = T.clamp(cam.yaw, -0.7, 0.7);
    cam.frame(w / 2, portrait ? h * 0.51 : h * 0.56, portrait ? w * 0.95 : Math.min(w * 0.76, h * 1.32));

    const X = c => (c - 7) * 0.2;
    const best = WF.bestCh(nets);
    const sx = -1.6 + ((tt * 0.24) % 1) * 3.2;

    const sorted = [...nets].sort((a, b) => b.rs - a.rs);
    sorted.forEach((n, i) => {
      const tz = -0.58 + (sorted.length > 1 ? i / (sorted.length - 1) : 0) * 1.95;
      this.z[n.k] = this.z[n.k] == null ? tz : T.lerp(this.z[n.k], tz, T.ease(dt, 1.2));
    });

    // Karesansui WebGL ground (or 2D grid fallback)
    if (!WF_GL.render(g, cam, w, h, tt, X(best), sx)) {
      T.floorGrid(g, cam, 1.9, 0.25);
    }

    // Front NERV frequency scale & Sacred Optimal Channel Gate
    const zF = -0.88;
    {
      const a = cam.p(X(1) - 0.48, 0, zF), b = cam.p(X(13) + 0.48, 0, zF);
      g.strokeStyle = `rgba(${T.RGB.acc},.48)`; g.lineWidth = 1.2;
      g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke();
    }
    for (let c = 1; c <= 13; c++) {
      const main3 = c === 1 || c === 6 || c === 11, isB = c === best;
      const p = cam.p(X(c), 0, zF), q = cam.p(X(c), 0, zF + (main3 ? 0.10 : 0.055));
      g.strokeStyle = isB ? T.C.hot : main3 ? T.C.acc : T.C.dim;
      g.lineWidth = main3 ? 1.5 : 1;
      g.beginPath(); g.moveTo(p[0], p[1]); g.lineTo(q[0], q[1]); g.stroke();
      if (main3) {
        const zBack = cam.p(X(c), 0, 1.55);
        g.strokeStyle = isB ? `rgba(${T.RGB.acc},.34)` : `rgba(${T.RGB.dim},.18)`;
        g.lineWidth = 1; g.beginPath(); g.moveTo(q[0], q[1]); g.lineTo(zBack[0], zBack[1]); g.stroke();
      }
      T.txt(g, c, p[0], p[1] + u * 1.25, u * (isB ? 1.08 : main3 ? 0.96 : 0.76), isB ? T.C.hot : main3 ? T.C.acc : T.C.label, 't', 'center');
    }

    // Sacred Optimal Channel Zenith Beacon (Torii / Axis Pillar on best channel)
    {
      const bx = X(best), p0 = cam.p(bx, 0, zF - 0.16), p1 = cam.p(bx, 0.58 + 0.04 * Math.sin(tt * 2.0), zF - 0.16);
      const gr = g.createLinearGradient(p0[0], p0[1], p1[0], p1[1]);
      gr.addColorStop(0, `rgba(${T.RGB.acc},.68)`); gr.addColorStop(1, `rgba(${T.RGB.hot},0)`);
      g.strokeStyle = gr; g.lineWidth = 1.8; g.beginPath(); g.moveTo(p0[0], p0[1]); g.lineTo(p1[0], p1[1]); g.stroke();
      T.diamond(g, p0[0], p0[1], u * 0.62, T.C.acc, true);
      T.drawSealChar(g, p0[0] - u * 0.65, p0[1] + u * 0.65, u * 1.3, '光', T.C.acc, false);
      T.txt(g, 'BEST CH ' + best, p0[0] + u * 0.85, p0[1] + u * 0.32, u * 0.74, T.C.hot, 't');
    }

    // Volumetric scan curtain
    {
      const a = cam.p(sx, 0, zF), b = cam.p(sx, 0, 1.55), c2 = cam.p(sx, 0.88, 1.55), d = cam.p(sx, 0.88, zF);
      const gr = g.createLinearGradient(a[0], a[1], d[0], d[1]);
      gr.addColorStop(0, `rgba(${T.RGB.acc},.14)`); gr.addColorStop(1, `rgba(${T.RGB.acc},0)`);
      g.fillStyle = gr; g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.lineTo(c2[0], c2[1]); g.lineTo(d[0], d[1]); g.closePath(); g.fill();
      g.strokeStyle = `rgba(${T.RGB.acc},.32)`; g.lineWidth = 1.2; g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke();
    }

    // Spectral Mountain Ridges (far to near) with internal harmonic ribs & water reflection
    const draw = sorted.map(n => ({ n, z: this.z[n.k], d: cam.p(X(n.c), 0, this.z[n.k])[3] })).sort((a, b) => b.d - a.d);
    const tops = [];
    for (const { n, z } of draw) {
      const s = WF.sig(n.rs), H = (0.07 + 0.84 * s) * n.h;
      const hw = n.w === 40 ? 0.76 : 0.44, cx = X(n.c);
      const fog = T.clamp(1.16 - (z + 1) * 0.28, 0.32, 1), breath = 0.88 + 0.12 * Math.sin(this.t * 1.6 + (T.hash(n.k) % 100));
      const open = WF.open(n), col = open ? T.ALERT : T.heatRGB(0.28 + 0.72 * s), sel = this.sel === n.k;

      const N = 32, pts = [], refPts = [];
      for (let i = 0; i <= N; i++) {
        const k = i / N * 2 - 1, bell = Math.pow(Math.cos(k * Math.PI / 2), 2.15), y = H * bell;
        pts.push(cam.p(cx + k * hw, y, z));
        refPts.push(cam.p(cx + k * hw, -y * 0.22, z));
      }
      const b0 = cam.p(cx - hw, 0, z), b1 = cam.p(cx + hw, 0, z), top = cam.p(cx, H, z);

      // Subtle mirror reflection in the obsidian pond
      g.beginPath(); g.moveTo(b0[0], b0[1]); for (const p of refPts) g.lineTo(p[0], p[1]); g.lineTo(b1[0], b1[1]); g.closePath();
      g.fillStyle = T.rgb(col, 0.08 * fog); g.fill();

      // Solid silhouette occlusion + luminous gradient body
      g.beginPath(); g.moveTo(b0[0], b0[1]); for (const p of pts) g.lineTo(p[0], p[1]); g.lineTo(b1[0], b1[1]); g.closePath();
      g.fillStyle = `rgba(${T.RGB.bg2},.86)`; g.fill();
      const gr = g.createLinearGradient(0, top[1], 0, b0[1]);
      gr.addColorStop(0, T.rgb(col, (sel ? 0.50 : 0.30) * fog * breath));
      gr.addColorStop(0.75, T.rgb(col, 0.09 * fog));
      gr.addColorStop(1, T.rgb(col, 0.02 * fog));
      g.fillStyle = gr; g.fill();

      // Internal Evangelion MAGI vertical harmonic ribs
      g.strokeStyle = T.rgb(sel ? [235, 250, 255] : col, (sel ? 0.42 : 0.22) * fog);
      g.lineWidth = 1; g.beginPath();
      for (let i = 2; i < N; i += 2) {
        const k = i / N * 2 - 1, base = cam.p(cx + k * hw, 0, z), pk = pts[i];
        g.moveTo(base[0], base[1]); g.lineTo(pk[0], pk[1]);
      }
      g.stroke();

      // Base shoreline + glowing double crest
      g.beginPath(); g.moveTo(b0[0], b0[1]); g.lineTo(b1[0], b1[1]);
      g.strokeStyle = T.rgb(col, 0.42 * fog); g.lineWidth = 1.1; g.stroke();

      g.beginPath(); pts.forEach((p, i) => i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]));
      g.strokeStyle = T.rgb(col, 0.28 * fog); g.lineWidth = sel ? 5.0 : 3.4; g.stroke();
      g.strokeStyle = T.rgb(sel ? [245, 252, 255] : col, fog * (0.72 + 0.28 * breath));
      g.lineWidth = sel ? 2.4 : 1.55; g.stroke();

      tops.push({ n, top, s, fog, H, z, cx });
      T.hits.circle(top[0], top[1], Math.max(u * 1.7, top[2] * 0.065), { k: 'net', id: n.k });
    }

    this.dust.tick(dt); this.dust.draw(g, cam);
    T.bloom(this.cv, 0.58);

    // Non-overlapping peak callout flags on strongest networks
    const flags = [...tops].sort((a, b) => b.s - a.s).slice(0, portrait ? 3 : 5);
    const placed = [];
    for (const f of [...tops].sort((a, b) => a.top[1] - b.top[1])) {
      if (!flags.includes(f) && this.sel !== f.n.k) continue;
      const p = f.top, open = WF.open(f.n), isSel = this.sel === f.n.k;
      let fy = p[1] - u * 2.3;
      for (let tries = 0; tries < 6; tries++) {
        const hit = placed.find(r => p[0] + u * 12.5 > r[0] && p[0] < r[0] + u * 12.5 && Math.abs(fy - r[1]) < u * 2.3);
        if (!hit) break; fy = hit[1] - u * 2.35;
      }
      placed.push([p[0], fy]);
      const cCol = open ? T.C.alert : T.C.hot;
      g.strokeStyle = open ? T.C.alert : `rgba(${T.RGB.hot},.75)`; g.lineWidth = isSel ? 1.5 : 1.1;
      g.beginPath(); g.moveTo(p[0], p[1] - 2); g.lineTo(p[0], fy); g.lineTo(p[0] + u * 0.85, fy); g.stroke();
      T.diamond(g, p[0], p[1], u * (isSel ? 0.45 : 0.32), cCol, true);
      g.font = `600 ${u * 0.82}px ${T.FC}`;
      const lbl = T.fit(g, WF.ssid(f.n), u * 11), lw = T.tw(g, lbl, u * 0.82, 'c');
      g.fillStyle = `rgba(${T.RGB.bg},.76)`;
      g.fillRect(p[0] + u * 0.78, fy - u * 0.68, Math.max(lw, u * 7.2) + u * 0.55, u * 2.15);
      T.txt(g, lbl, p[0] + u * 0.98, fy + u * 0.25, u * 0.82, cCol, 'c');
      T.txt(g, `CH ${f.n.c}${f.n.w === 40 ? ' · 40M' : ''} · ${Math.round(f.n.rs)} DBM`, p[0] + u * 0.98, fy + u * 1.22, u * 0.64, open ? T.C.alert : T.C.label);
    }

    // Header + Chamfered Channel Load Spectrum Panel
    const age = S.wifiT ? (performance.now() - S.wifiT) / 1000 : 0;
    T.header(g, m, y0, w - 2 * m, u, 'Channels', `${nets.length} networks · best ch ${best} · scan ${T.ago(age)} ago`);

    const pw = Math.min(u * 18.2, w * 0.34), ph = u * 6.5, px = w - m - pw, py = y0 + u * 3.2;
    WF.cutPlate(g, px, py, pw, ph, u * 0.8, `rgba(${T.RGB.panel},.34)`, `rgba(${T.RGB.acc},.42)`, 1.2);
    g.fillStyle = T.C.acc; g.fillRect(px + u * 0.8, py, pw * 0.34, 2);
    T.txt(g, '// CHANNEL LOAD', px + u * 0.8, py + u * 1.25, u * 0.72, T.C.acc, 't');
    T.drawSealChar(g, px + pw - u * 1.9, py + u * 0.35, u * 1.35, '波', T.C.acc, false);

    const load = new Array(14).fill(0);
    for (const n of nets) {
      const span = n.w === 40 ? 4 : 2;
      for (let c = 1; c <= 13; c++) { const d = Math.abs(n.c - c); if (d <= span) load[c] += WF.sig(n.rs) * (1 - d / (span + 1)); }
    }
    const mx = Math.max(1, ...load), bw = (pw - u * 1.6) / 13;
    for (let c = 1; c <= 13; c++) {
      const v = load[c] / mx, maxH = ph - u * 3.1, bh = maxH * v;
      const bx = px + u * 0.8 + (c - 1) * bw, by = py + ph - u * 1.25;
      g.fillStyle = `rgba(${T.RGB.line},.65)`; g.fillRect(bx + bw * 0.16, by - maxH, bw * 0.68, maxH);
      g.fillStyle = c === best ? T.C.acc : T.heat(0.28 + 0.65 * v);
      g.fillRect(bx + bw * 0.16, by - bh, bw * 0.68, bh);
      if (c === 1 || c === 6 || c === 11) {
        T.txt(g, c, bx + bw / 2, py + ph - u * 0.26, u * 0.62, c === best ? T.C.hot : T.C.label, 'm', 'center');
      }
    }

    if (this.sel) {
      const f = tops.find(x => x.n.k === this.sel);
      if (f) this.callout(g, u, f.n, f.top, w, h);
    }
    T.footer(g, m, h - u * 2.2, w - 2 * m, u, 'drag: orbit  ·  tap peak: details', 'width = 20/40 MHz · scan ~20 s');
  },

  callout(g, u, n, p, w, h) {
    const bw = u * 15.8, bh = u * 8.0;
    let bx = p[0] + u * 3, by = p[1] - bh - u * 2;
    if (bx + bw > w - u) bx = p[0] - u * 3 - bw;
    by = T.clamp(by, 50 + u * 3, h - bh - u * 3);
    const open = WF.open(n), ac = open ? T.C.alert : T.C.acc, col = open ? T.C.alert : T.heat(0.4 + 0.6 * WF.sig(n.rs));
    g.strokeStyle = ac; g.lineWidth = 1.2;
    g.beginPath(); g.moveTo(p[0], p[1]); g.lineTo(bx < p[0] ? bx + bw : bx, by + bh * 0.5); g.stroke();
    WF.cutPlate(g, bx, by, bw, bh, u * 0.8, `rgba(${T.RGB.bg},.90)`, ac, 1.3);
    g.fillStyle = ac; g.fillRect(bx + u * 0.8, by, bw * 0.34, 2);
    T.drawSealChar(g, bx + bw - u * 2.25, by + u * 0.55, u * 1.55, open ? '警' : '網', ac, open);
    g.font = `600 ${u * 1.15}px ${T.FC}`;
    T.txt(g, T.fit(g, WF.ssid(n), bw - u * 3.4), bx + u, by + u * 1.7, u * 1.15, T.C.hot, 'c');
    T.txt(g, `${(n.v || 'unknown vendor')} · ${n.e}${n.w === 40 ? ' · 40MHZ' : ''}`.toUpperCase(), bx + u, by + u * 2.8, u * 0.7, open ? T.C.alert : T.C.label);
    T.bigNum(g, Math.round(n.rs), bx + u, by + u * 5.8, u * 2.75, col, 'DBM');
    T.spark(g, bx + u * 7.3, by + u * 3.65, bw - u * 8.3, u * 2.2, n.hist.slice(-30), -100, -35, ac);
    T.segBar(g, bx + u, by + u * 6.75, bw - u * 2, u * 0.4, WF.sig(n.rs), 16, false, open);
    T.hits.rect(bx, by, bw, bh, { k: 'close' });
  },

  // ---------- NETWORKS: Tactical Table + Network Dossier ----------
  networks(g, w, h, u, m, y0, dt, nets) {
    const now = performance.now();
    const have = new Set(nets.map(n => n.k));
    this.order = this.order.filter(k => have.has(k));
    for (const n of nets) if (!this.order.includes(n.k)) {
      let i = 0; while (i < this.order.length && (S.aps.get(this.order[i]).rs >= n.rs)) i++;
      this.order.splice(i, 0, n.k);
    }
    if (now - this.orderT > 3000) {
      this.orderT = now;
      for (let i = 0; i + 1 < this.order.length; i++) {
        const a = S.aps.get(this.order[i]), b = S.aps.get(this.order[i + 1]);
        if (b.rs - a.rs > 6) { this.order[i] = b.k; this.order[i + 1] = a.k; }
      }
    }
    const best = WF.bestCh(nets), open = nets.filter(WF.open).length;
    T.header(g, m, y0, w - 2 * m, u, 'Networks', `${nets.length} networks${open ? ' · ' + open + ' open' : ''} · best ch ${best}`, false);
    const top = y0 + u * 3.2, bot = h - u * 2.8, portrait = h > w * 1.05;
    const sel = this.sel && S.aps.get(this.sel);
    let tx = m, tw = w - 2 * m, th = bot - top, cx, cy, cw, ch;
    if (sel) {
      if (portrait) { th = (bot - top) * 0.52; cx = m; cy = top + th + u; cw = w - 2 * m; ch = bot - cy; }
      else { tw = (w - 2 * m) * 0.59; cx = m + tw + u; cy = top; cw = w - m - cx; ch = bot - top; }
    }
    this.table(g, tx, top, tw, th, u);
    if (sel) this.card(g, cx, cy, cw, ch, u, sel);
    T.footer(g, m, h - u * 2.2, w - 2 * m, u, sel ? 'tap row: switch  ·  ‹ all: back' : 'tap row: details', 'signal shown steady (±3 dB)');
  },

  table(g, x, y, w, h, u) {
    WF.cutPlate(g, x, y, w, h, u * 0.95, `rgba(${T.RGB.panel},.28)`, `rgba(${T.RGB.acc},.35)`, 1.2);
    g.fillStyle = T.C.acc; g.fillRect(x + u * 0.95, y, w * 0.28, 2);
    const pad = u * 0.85, rowH = u * 2.6, wide = w > u * 44;
    const cols = { g: x + pad, n: x + pad + rowH * 1.0, ch: x + w * (wide ? 0.42 : 0.5), enc: x + w * (wide ? 0.49 : 0.6), bar: x + w * (wide ? 0.58 : 0.7), sp: x + w * 0.72, db: x + w - pad };
    const barW = wide ? w * 0.12 : w * 0.17;
    let cy = y + pad;
    T.txt(g, 'NETWORK', cols.n, cy + u * 0.78, u * 0.68, T.C.dim);
    T.txt(g, 'CH', cols.ch, cy + u * 0.78, u * 0.68, T.C.dim);
    T.txt(g, 'SEC', cols.enc, cy + u * 0.78, u * 0.68, T.C.dim);
    if (wide) T.txt(g, 'HISTORY', cols.sp, cy + u * 0.78, u * 0.68, T.C.dim);
    T.txt(g, 'DBM', cols.db, cy + u * 0.78, u * 0.68, T.C.dim, 'm', 'right');
    g.fillStyle = `rgba(${T.RGB.acc},.25)`; g.fillRect(x + pad, cy + u * 1.08, w - 2 * pad, 1);
    cy += u * 1.4;
    let idx = 0;
    for (const k of this.order) {
      if (cy + rowH > y + h - pad * 0.5) break;
      const n = S.aps.get(k); if (!n) continue;
      const on = this.sel === k, open = WF.open(n), s = WF.sig(n.rs);
      g.globalAlpha = 0.35 + 0.65 * n.h;
      if (on) {
        WF.cutPlate(g, x + 3, cy + 1, w - 6, rowH - 2, u * 0.42, `rgba(${T.RGB.acc},.13)`, open ? T.C.alert : T.C.acc, 1.15);
        g.fillStyle = open ? T.C.alert : T.C.acc; g.fillRect(x + 3, cy + 3, 3, rowH - 6);
      } else if (idx % 2 === 1) {
        g.fillStyle = `rgba(${T.RGB.panel},.14)`; g.fillRect(x + 3, cy + 1, w - 6, rowH - 2);
      }
      idx++;
      const gs = rowH * 0.6, gc = open ? T.C.alert : on ? T.C.hot : T.C.acc;
      T.glyph(g, cols.g, cy + (rowH - gs) / 2, gs, WF.encG(n), gc);
      g.font = `600 ${rowH * 0.36}px ${T.FC}`;
      T.txt(g, T.fit(g, WF.ssid(n), cols.ch - cols.n - u), cols.n, cy + rowH * 0.45, rowH * 0.36, n.s ? (on ? T.C.hot : T.C.label) : T.C.dim, 'c');
      g.font = `${rowH * 0.27}px ${T.FM}`;
      const subParts = [n.v || '—'];
      if (n.g === 6) subParts.push('WI-FI 6');
      else if (n.g === 4) subParts.push('WI-FI 4');
      if (n.w === 40) subParts.push('40MHZ');
      if (n.hs) subParts.push(T.tr('HOTSPOT'));
      subParts.push(n.b);
      T.txt(g, T.fit(g, subParts.join(' · ').toUpperCase(), cols.ch - cols.n - u), cols.n, cy + rowH * 0.81, rowH * 0.27, n.hs ? T.C.acc : T.C.dim);
      T.txt(g, n.c, cols.ch, cy + rowH * 0.62, rowH * 0.42, T.C.hot, 't');
      T.txt(g, String(n.e).toUpperCase(), cols.enc, cy + rowH * 0.6, rowH * 0.3, open ? T.C.alert : T.C.label);
      T.segBar(g, cols.bar, cy + rowH * 0.4, barW, rowH * 0.2, s, 10, false, open);
      if (wide) T.spark(g, cols.sp, cy + rowH * 0.18, cols.db - cols.sp - u * 3.6, rowH * 0.62, n.hist.slice(-30), -100, -35, open ? T.C.alert : T.C.acc);
      T.txt(g, Math.round(n.rs), cols.db, cy + rowH * 0.68, rowH * 0.52, open ? T.C.alert : T.heat(0.4 + 0.6 * s), 't', 'right');
      g.globalAlpha = 1;
      T.hits.rect(x, cy, w, rowH, { k: 'net', id: k });
      cy += rowH;
    }
  },

  card(g, x, y, w, h, u, n) {
    const p = u * 1.15, open = WF.open(n), s = WF.sig(n.rs), ac = open ? T.C.alert : T.C.acc, col = open ? T.C.alert : T.heat(0.4 + 0.6 * s);
    WF.cutPlate(g, x, y, w, h, u * 0.95, `rgba(${T.RGB.bg},.88)`, ac, 1.25);
    g.fillStyle = ac; g.fillRect(x + u * 0.95, y, w * 0.34, 2);
    let cy = y + p;
    T.txt(g, '// NETWORK DOSSIER', x + p, cy + u * 0.9, u * 1.0, ac, 't');
    T.txt(g, '‹ ALL', x + w - p, cy + u * 0.9, u * 0.88, T.C.label, 'm', 'right');
    T.hits.rect(x + w - p - u * 4, cy - u * 0.3, u * 4.6, u * 1.8, { k: 'close' });
    cy += u * 2.3;

    T.drawSealChar(g, x + w - p - u * 2.0, cy, u * 1.9, open ? '警' : '網', ac, open);
    g.font = `600 ${u * 1.65}px ${T.FC}`;
    T.txt(g, T.fit(g, WF.ssid(n), w - 2 * p - u * 2.5), x + p, cy + u * 1.25, u * 1.65, n.s ? T.C.hot : T.C.dim, 'c'); cy += u * 2.0;
    T.txt(g, `${n.v || 'unknown vendor'} · ${n.e}${open ? ' · NO PASSWORD' : ''}`.toUpperCase(), x + p, cy + u * 0.6, u * 0.8, open ? T.C.alert : T.C.label); cy += u * 1.35;

    const big = Math.min(u * 4.4, h * 0.16);
    T.bigNum(g, Math.round(n.rs), x + p, cy + big * 0.8, big, col, 'DBM');
    const d = WF.dist(n.rs);
    T.txt(g, '≈ ' + (d < 10 ? d.toFixed(1) : Math.round(d)) + ' M', x + w - p, cy + big * 0.75, u * 1.25, T.C.hot, 't', 'right');
    cy += big + u * 0.55;
    T.segBar(g, x + p, cy, w - 2 * p, u * 0.5, s, 20, true, open); cy += u * 1.5;

    T.txt(g, '// SIGNAL · LAST SCANS', x + p, cy + u * 0.7, u * 0.68, T.C.dim); cy += u * 1.05;
    const kv = [['CHANNEL', n.c + (n.c === WF.bestCh(WF.list()) ? '  (best)' : '')], ['BSSID', n.b], ['SECURITY', n.e]];
    if (n.g || n.w) {
      const gn = n.g === 6 ? 'Wi-Fi 6' : n.g === 4 ? 'Wi-Fi 4' : n.g === 3 ? '11g' : '11b';
      kv.push(['STANDARD', gn + ' · ' + (n.w === 40 ? '40 MHz' : '20 MHz')]);
    }
    if (n.hs) kv.push(['TYPE', T.tr('PHONE HOTSPOT')]);
    if (n.wps) kv.push(['WPS', 'ON']);

    const sh = Math.max(u * 2.5, Math.min(u * 6.0, y + h - cy - kv.length * u * 1.42 - p - u));
    g.fillStyle = `rgba(${T.RGB.bg},.48)`; g.fillRect(x + p, cy, w - 2 * p, sh);
    T.brackets(g, x + p, cy, w - 2 * p, sh, u * 0.45, ac, 1.1);
    T.spark(g, x + p, cy, w - 2 * p, sh, n.hist, -100, -35, ac); cy += sh + u * 0.95;

    for (const [k, v] of kv) {
      if (cy + u * 1.38 > y + h - p * 0.5) break;
      g.fillStyle = `rgba(${T.RGB.line},.42)`; g.fillRect(x + p, cy + u * 1.15, w - 2 * p, 1);
      T.txt(g, k, x + p, cy + u * 0.88, u * 0.78, T.C.dim);
      T.txt(g, String(v), x + w - p, cy + u * 0.88, u * 0.78, T.C.hot, 'm', 'right');
      cy += u * 1.42;
    }
  },
});
})();
