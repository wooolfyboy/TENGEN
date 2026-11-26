// ---- link to the stick: second port (SSE, port 81), fallback /api/* polling ----
const Stream = T.Stream = { es: null, want: '', mode: 'off', lastMsg: 0, pollT: null };
// the PC app (tools/desktop) sets window.TENGEN_DESKTOP; in USB mode data goes over the cable via the window.TengenUSB bridge (want / api / poll)
const USB = window.TENGEN_DESKTOP && window.TENGEN_DESKTOP.mode === 'usb' ? window.TengenUSB : null;
T.desktop = !!window.TENGEN_DESKTOP;
// stream events: same for Wi-Fi (SSE) and USB
const H = {
  st: d => { S.onSt(d); T.app && T.app.status(); },
  ble: d => S.onBle(d), wifi: d => S.onWifi(d), snd: d => S.onSnd(d), sub: d => S.onSub(d), pc: d => S.onPc(d),
  sat: d => S.onSat(d), wx: d => S.onWx(d), ir: d => S.onIr(d), nfc: d => S.onNfc(d), sens: d => S.onSens(d), loc: d => S.onLoc(d),
};
if (USB) {
  // /api/... requests also go over the cable (plain fetch, the page does not notice)
  const of = window.fetch.bind(window);
  window.fetch = (u, o) => {
    const url = typeof u === 'string' ? u : (u && u.url) || '';
    const i = url.indexOf('/api/');
    if (i < 0) return of(u, o);
    return USB.api(url.slice(i)).then(t => new Response(t, { status: 200, headers: { 'Content-Type': 'application/json' } }));
  };
}
const host = () => T.stickHost();
const ssePort = () => { const p = new URLSearchParams(location.search).get('sse'); return p && /^\d{1,5}$/.test(p) ? p : '81'; };
const apiBase = () => T.apiBase();

Stream.setLink = (mode) => {
  Stream.mode = mode;
  const d = document.getElementById('link'), t = document.getElementById('linkTxt');
  d.className = 'dot ' + (mode === 'live' || mode === 'usb' ? 'live' : mode === 'poll' || mode === 'usbwait' ? 'poll' : 'off');
  t.textContent = T.tr(mode === 'live' ? 'LIVE' : mode === 'poll' ? 'POLL' : mode === 'usb' ? 'USB' : mode === 'usbwait' ? 'USB · OPEN WEB ON TENGEN' : mode === 'usboff' ? 'USB · NO DEVICE' : 'NO LINK');
};
Stream.connect = (want) => {
  Stream.want = want;
  if (USB) { Stream.usb(want); return; }
  if (Stream.es) { Stream.es.close(); Stream.es = null; }
  clearInterval(Stream.pollT); Stream.pollT = null;
  clearTimeout(Stream.openT); clearTimeout(Stream.retryT);
  let opened = false;
  const es = new EventSource(`http://${host()}:${ssePort()}/stream?want=${want}`);
  Stream.es = es;
  const on = (ev, fn) => es.addEventListener(ev, e => { Stream.lastMsg = performance.now(); if (Stream.mode !== 'live') Stream.setLink('live'); try { fn(JSON.parse(e.data)); } catch (x) {} });
  es.onopen = () => { opened = true; Stream.setLink('live'); };
  for (const k in H) on(k, H[k]);
  es.onerror = () => {
    if (Stream.mode === 'live') Stream.setLink('off');
    if (es.readyState === 2 && Stream.es === es) {
      es.close(); Stream.es = null;
      clearTimeout(Stream.retryT);
      Stream.retryT = setTimeout(() => { if (Stream.want === want) Stream.connect(want); }, 2500);
    }
  };
  Stream.openT = setTimeout(() => { if (!opened && Stream.es === es) { es.close(); Stream.es = null; Stream.poll(want); } }, 5000);
};
// fallback: same data via /api/* (slower but works)
Stream.poll = (want) => {
  Stream.setLink('poll');
  // the stick enables radios per tab: only what is viewed, the rest stays off
  const has = k => want.split(/[,&]/).includes(k);
  const tab = has('snd') ? 'snd' : has('sub') ? 'sub' : has('ble') && has('wifi') ? 'ov' : has('ble') ? 'ble' : has('wifi') ? 'wifi' : 'sys';
  const J = u => fetch(apiBase() + u + (u.includes('?') ? '&' : '?') + 'tab=' + tab, { cache: 'no-store' }).then(r => r.json());
  let n = 0, busy = false;
  const step = async () => {
    if (busy) return;                                    // the stick has not answered yet: do not flood it
    busy = true;
    try {
      n++;
      if (has('snd')) {
        const j = await J('/api/snd');
        S.onSnd({ en: j.en ? 1 : 0, ok: j.ok ? 1 : 0, lv: j.lv, pk: j.pk, hz: j.hz, e: j.e,
          b: (j.b || []).map(v => Math.round(v * 2.55).toString(16).padStart(2, '0')).join(''), w: '' });
      }
      if (has('ble') && n % 8 === 1) {
        const j = await J('/api/ble');
        S.onBle({ on: j.on ? 1 : 0, n: j.count, m: j.macs, d: j.dev.map(d => [d.id, d.r, d.t, d.v, d.n, d.age, d.m]) });
      }
      if (has('wifi') && n % 40 === 1) S.onWifi(await J('/api/wifi'));
      if (has('sub') && n % 2 === 1) {
        const m = /sm=(\d+)/.exec(want), b = /sb=(\d+)/.exec(want);
        S.onSub(await J('/api/sub?m=' + (m ? m[1] : 0) + '&b=' + (b ? b[1] : 1)));
      }
      if (has('loc') && n % 2 === 1) S.onLoc(await J('/api/loc2x'));
      if (n % 16 === 1) { const i = await J('/api/info'); S.onSt({ name: 'TENGEN', up: i.up, bat: i.bat.ok ? i.bat.soc : -1, usb: i.bat.usb ? 1 : 0, cli: i.cli }); T.app.status(); }
      Stream.lastMsg = performance.now();
    } catch (e) { Stream.setLink('off'); }
    busy = false;
  };
  Stream.pollT = setInterval(step, 125); step();
  // fallback is temporary: retry the normal stream after 8 s
  clearTimeout(Stream.retryT);
  Stream.retryT = setTimeout(() => { if (Stream.mode === 'poll' && Stream.want === want) Stream.connect(want); }, 8000);
};

// ---- USB (PC app): subscribe + long-poll the bridge ----
// The bridge returns a batch "[[event, data], ...]" at least every 0.25 s; "_x" is a service event from the app/stick.
Stream.usb = (want) => {
  USB.want('want=' + want).catch(() => {});
  if (Stream.usbLoop) return;
  Stream.usbLoop = true;
  if (Stream.mode !== 'usb') Stream.setLink('usbwait');
  (async () => {
    let errs = 0;
    for (;;) {
      let batch;
      try { batch = JSON.parse(await USB.poll()); errs = 0; }
      catch (e) { if (++errs >= 3) Stream.setLink('usboff'); await new Promise(r => setTimeout(r, 500)); continue; }
      for (const [ev, d] of batch) {
        if (ev === '_x') { Stream.setLink(d === 'web' ? 'usbwait' : d === 'nostick' ? 'usboff' : Stream.mode); continue; }
        Stream.lastMsg = performance.now();
        if (Stream.mode !== 'usb') Stream.setLink('usb');
        const f = H[ev]; if (f) try { f(d); } catch (x) {}
      }
    }
  })();
};
