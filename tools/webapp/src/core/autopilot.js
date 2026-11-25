// ---- HQ > AUTOPILOT: Cinema Director mode for tablets & second monitors ----
// Smoothly cycles through the best 3D/HUD scenes, auto-hides chrome when idle,
// gently orbits 3D cameras, and immediately jumps to live RF / IR / NFC / tracker events.
(() => {
const TOUR = [
  { id: 'hq',  sub: 1, dur: 28, title: 'HQ · HOLO 3D' },
  { id: 'hq',  sub: 0, dur: 22, title: 'HQ · COMMAND' },
  { id: 'ble', sub: 1, dur: 24, title: 'BLUETOOTH · 3D CITY' },
  { id: 'snd', sub: 1, dur: 22, title: 'SOUND · 3D TERRAIN', when: () => S.snd && S.snd.ok },
  { id: 'wifi', sub: 0, dur: 20, title: 'WI-FI · CHANNELS' },
  { id: 'gal', sub: 5, dur: 24, title: 'GALLERY · GLOBE' },
  { id: 'gal', sub: 6, dur: 22, title: 'GALLERY · RADIO WEATHER' },
  { id: 'pc',  sub: 1, dur: 22, title: 'PC · PROCESS CITY', when: () => S.pc && S.pc.live },
];

const A = T.auto = {
  on: false, paused: false, idx: -1, elapsed: 0, dur: 24,
  _switching: false, _chromeT: 0, _el: null, _bar: null, _lbl: null, _pauseBtn: null,
  _evLock: 0, _evBanner: '', _lastIr: 0, _lastNfc: 0, _lastBurst: 0, _lastTrk: 0,

  ensureUI() {
    if (this._el) return;
    const el = T.el('div', 'auto-hud'); el.hidden = true;
    el.innerHTML = `<div class="auto-top"><span class="auto-dot"></span><b class="auto-lbl">AUTOPILOT</b>` +
      `<div class="auto-btns"><button data-a="pause">⏸</button><button data-a="next">⏭</button><button data-a="exit">✕</button></div></div>` +
      `<div class="auto-prog"><i></i></div>`;
    document.body.appendChild(el);
    this._el = el;
    this._lbl = el.querySelector('.auto-lbl');
    this._bar = el.querySelector('.auto-prog i');
    this._pauseBtn = el.querySelector('[data-a=pause]');
    this._pauseBtn.onclick = () => { this.paused = !this.paused; this._pauseBtn.textContent = this.paused ? '▶' : '⏸'; this.wakeChrome(); };
    el.querySelector('[data-a=next]').onclick = () => { this.next(); this.wakeChrome(); };
    el.querySelector('[data-a=exit]').onclick = () => this.stop();
    const wake = () => { if (this.on) this.wakeChrome(); };
    window.addEventListener('pointermove', wake, { passive: true });
    window.addEventListener('pointerdown', wake, { passive: true });
    window.addEventListener('keydown', e => { if (this.on && e.key === 'Escape') this.stop(); });
  },

  wakeChrome() {
    this._chromeT = performance.now() + 3000;
    if (document.body.classList.contains('cinema-hide')) {
      document.body.classList.remove('cinema-hide');
      if (T.app && T.app.cur && T.app.cur.cv) T.app.cur.cv.fit();
    }
  },

  start() {
    this.ensureUI();
    this.on = true; this.paused = false; this.idx = -1; this._evLock = 0;
    this._lastIr = (S.ir && S.ir.t) || 0;
    this._lastNfc = (S.nfc && S.nfc.t) || 0;
    this._lastBurst = performance.now();
    this._lastTrk = performance.now();
    this._pauseBtn.textContent = '⏸';
    this._el.hidden = false;
    document.body.classList.add('cinema');
    this.wakeChrome();
    this.next();
  },

  stop() {
    if (!this.on) return;
    this.on = false; this.paused = false;
    document.body.classList.remove('cinema', 'cinema-hide');
    if (T.app && T.app.cur && T.app.cur.cv) T.app.cur.cv.fit();
    if (this._el) this._el.hidden = true;
    const hq = T.views.find(v => v.id === 'hq');
    if (hq && hq.sub === 2) hq.sub = 0;
    if (T.app && T.app.cur && T.app.cur.id === 'hq') T.app.renderSubs();
  },

  onUserNav() {
    if (this.on && !this._switching) this.stop();
  },

  jump(id, sub, dur, label) {
    this._switching = true;
    try {
      T.app.go(id, sub);
    } finally {
      this._switching = false;
    }
    this.elapsed = 0;
    this.dur = dur;
    if (this._lbl) this._lbl.textContent = T.tr('AUTOPILOT') + ' · ' + T.tr(label);
    if (this._bar) this._bar.style.width = '0%';
  },

  next() {
    for (let k = 0; k < TOUR.length; k++) {
      this.idx = (this.idx + 1) % TOUR.length;
      const sc = TOUR[this.idx];
      if (!sc.when || sc.when()) {
        this.jump(sc.id, sc.sub, sc.dur, sc.title);
        return;
      }
    }
  },

  watchEvents(now) {
    if (now < this._evLock) return;
    if (S.ir && S.ir.t && S.ir.t !== this._lastIr) {
      this._lastIr = S.ir.t;
      this._evLock = now + 12000;
      this.jump('probe', 0, 12, '⚡ IR REMOTE');
      return;
    }
    if (S.nfc && S.nfc.t && S.nfc.t !== this._lastNfc) {
      this._lastNfc = S.nfc.t;
      this._evLock = now + 12000;
      this.jump('probe', 1, 12, '⚡ NFC CARD');
      return;
    }
    if (S.sub && S.sub.burst && now - this._lastBurst > 18000) {
      this._lastBurst = now;
      this._evLock = now + 12000;
      this.jump('hq', 1, 14, '⚡ 433 MHZ BURST');
      return;
    }
    if (S.devInit && now - this._lastTrk > 25000) {
      for (const e of S.dev.values()) {
        if (e.alive && S.isTrk(e) && now - e.born < 4000) {
          this._lastTrk = now;
          this._evLock = now + 14000;
          this.jump('hq', 1, 14, '⚡ TRACKER DETECTED');
          return;
        }
      }
    }
  },

  tick(dt, now) {
    if (!this.on) return;
    if (now > this._chromeT && !document.body.classList.contains('cinema-hide')) {
      document.body.classList.add('cinema-hide');
      if (T.app && T.app.cur && T.app.cur.cv) T.app.cur.cv.fit();
    }
    this.watchEvents(now);
    if (!this.paused) {
      this.elapsed += dt;
      if (this._bar) this._bar.style.width = Math.min(100, (this.elapsed / Math.max(1, this.dur)) * 100).toFixed(1) + '%';
      if (this.elapsed >= this.dur) this.next();
    }
  },
};
})();
