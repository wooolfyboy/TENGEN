// ---- TENGEN SETTINGS FROM THE TABLET: control panel ----
// Everything the device turns with the wheel in Settings is here by finger. The device supplies the list
// (/api/settings: pages, items, type, options, value); the tablet only draws and sends changes (/api/set).
// A value changes at once (optimistic) and rolls back on error. Plain DOM, not canvas: no frame cost.
(() => {
  const esc = s => String(s == null ? '' : s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const api = (path) => fetch(T.apiBase() + path, { cache: 'no-store' }).then(r => { if (!r.ok) throw new Error('http ' + r.status); return r.json(); });

  T.views.push({
    id: 'set', title: 'Settings', want: 'st',
    onLang() { if (this.load) this.load(); else if (this.render) this.render(); },   // language change: labels come from the device in its language - reload
    mount(main) {
      this.root = T.el('div', 'view setv');
      this.root.innerHTML = `<div class="set-wrap"><aside class="set-tabs"></aside><section class="set-body"></section></div><div class="set-toast"></div>`;
      main.appendChild(this.root);
      this.tabs = this.root.querySelector('.set-tabs'); this.body = this.root.querySelector('.set-body'); this.toastEl = this.root.querySelector('.set-toast');
      this.page = 0; this.data = null; this.busy = 0; this.screenOn = true; this.err = null;
      try { this.page = +(localStorage.getItem('set_page') || 0); } catch (e) {}
      this.load();
      this.timer = setInterval(() => { if (!this.root.hidden && !this.busy && !this.dragging) this.load(true); }, 5000);
    },
    unmount() { clearInterval(this.timer); this.root.remove(); },
    frame() {},
    load(quiet) {
      api('/api/settings').then(d => { this.data = d; this.err = null; S.cfgFromSettings(d); quiet ? this.refresh() : this.render(); })
        .catch(e => { this.err = e; if (!this.data || !quiet) { this.data = null; this.render(); } });
    },
    toast(msg, bad) {
      const t = this.toastEl; t.textContent = T.tr(msg); t.className = 'set-toast on' + (bad ? ' bad' : '');
      clearTimeout(this.toastT); this.toastT = setTimeout(() => { t.className = 'set-toast'; }, 1800);
    },
    // ---- rendering ----
    render() {
      const d = this.data;
      if (!d || !d.pages || !d.pages.length) {
        this.tabs.innerHTML = '';
        this.body.innerHTML = `<div class="set-empty"><b>${T.tr('NO LINK')}</b><span>${T.tr('settings are stored on TENGEN · open Web on the device and connect')}</span></div>`;
        return;
      }
      if (this.page >= d.pages.length) this.page = 0;
      this.tabs.innerHTML = d.pages.map((p, i) => `<button data-i="${i}" class="${i === this.page ? 'on' : ''}">${esc(T.tr(p.name))}</button>`).join('')
        + `<button data-i="act" class="act ${this.page === 'act' ? 'on' : ''}"><b class="kj">動</b>${T.tr('Actions')}</button>`;
      this.tabs.querySelectorAll('button').forEach(b => b.onclick = () => {
        this.page = b.dataset.i === 'act' ? 'act' : +b.dataset.i;
        try { localStorage.setItem('set_page', b.dataset.i === 'act' ? 0 : this.page); } catch (e) {}
        this.render();
      });
      if (this.page === 'act') { this.renderActions(); return; }
      const p = d.pages[this.page];
      this.body.innerHTML = `<h2 class="set-h">// ${esc(T.tr(p.name))}</h2>` +
        p.items.map((it, j) => `<div class="set-row" data-j="${j}">${this.rowHtml(it)}</div>`).join('');
      this.body.querySelectorAll('.set-row').forEach(row => this.bind(row, p.items[+row.dataset.j]));
    },
    refresh() {                                            // quiet value update without redraw (does not break a touch)
      const d = this.data; if (!d || this.page === 'act' || !d.pages[this.page]) { if (!d) this.render(); return; }
      const p = d.pages[this.page], rows = this.body.querySelectorAll('.set-row');
      if (rows.length !== p.items.length) { this.render(); return; }
      rows.forEach(row => { const it = p.items[+row.dataset.j]; const html = this.rowHtml(it); if (row._html !== html) { row.innerHTML = html; row._html = html; this.bind(row, it); } });
    },
    rowHtml(it) {
      const head = `<div class="set-name"><b>${esc(T.tr(it.name))}</b><span>${esc(T.tr(it.hint || ''))}</span></div>`;
      let ctl = '';
      if (it.type === 'enum') {
        const opts = it.opts || [];
        ctl = `<div class="chips">${opts.map((o, i) => `<button data-o="${i}" class="${this.isSel(it, o, i) ? 'on' : ''}">${esc(T.tr(String(o)))}</button>`).join('')}</div>`;
      } else if (it.type === 'range') {
        ctl = `<div class="rng"><input type="range" min="${esc(it.min)}" max="${esc(it.max)}" step="${esc(it.step || 1)}" value="${esc(it.v)}"><output>${esc(it.v)}${esc(it.unit || '')}</output></div>`;
      } else if (it.type === 'bool') {
        const on = it.v === true || it.v === 1 || it.v === '1' || it.v === 'on';
        ctl = `<button class="tgl ${on ? 'on' : ''}" aria-pressed="${on}"><i></i><span>${T.tr(on ? 'ON' : 'OFF')}</span></button>`;
      } else {
        ctl = `<div class="info">${esc(T.tr(String(it.v)))}${esc(it.unit || '')}</div>`;
      }
      return head + ctl;
    },
    isSel(it, o, i) { return typeof it.v === 'number' ? it.v === i : String(it.v) === String(o); },
    bind(row, it) {
      row._html = row._html || row.innerHTML;
      if (it.type === 'enum') row.querySelectorAll('.chips button').forEach(b => b.onclick = () => {
        const i = +b.dataset.o, o = it.opts[i];
        this.set(it, typeof it.v === 'number' ? i : o);
      });
      else if (it.type === 'range') {
        const r = row.querySelector('input'), out = row.querySelector('output');
        r.oninput = () => { this.dragging = true; out.textContent = r.value + (it.unit || ''); };
        r.onchange = () => { this.dragging = false; this.set(it, +r.value); };
      } else if (it.type === 'bool') {
        row.querySelector('.tgl').onclick = () => { const on = it.v === true || it.v === 1 || it.v === '1' || it.v === 'on'; this.set(it, typeof it.v === 'boolean' ? !on : on ? 0 : 1); };
      }
    },
    set(it, v) {
      const old = it.v; it.v = v; this.busy++;
      if (this.page !== 'act') this.refresh();
      api(`/api/set?k=${encodeURIComponent(it.k)}&v=${encodeURIComponent(v)}`).then(r => {
        if (!r || !r.ok) throw new Error((r && r.err) || 'refused');
        if (r.item) Object.assign(it, r.item);
        if (String(it.k).startsWith('al_')) { S.cfgFromItem(r.item || it); if (it.k === 'al_warn' || it.k === 'al_alarm') this.load(true); }   // HQ sees new thresholds at once
        this.toast('saved');
      }).catch(e => { it.v = old; this.toast(T.tr('not saved') + ' · ' + T.tr(e.message || 'no link'), true); })
        .finally(() => { this.busy--; if (this.page !== 'act') this.refresh(); });
    },
    // ---- actions ----
    renderActions() {
      const tg = (k, on) => `<button class="tgl ${on ? 'on' : ''}" data-a="${k}"><i></i><span>${T.tr(on ? 'ON' : 'OFF')}</span></button>`;
      const row = (name, hint, ctl) => `<div class="set-row"><div class="set-name"><b>${esc(T.tr(name))}</b><span>${esc(T.tr(hint))}</span></div>${ctl}</div>`;
      const UP = window.Capacitor && window.Capacitor.Plugins && window.Capacitor.Plugins.TengenUpdate;
      this.body.innerHTML = `<h2 class="set-h"><span class="kj">動</span>// ${T.tr('Actions')}</h2>` +
        row('Device screen', "turn TENGEN's display off to save battery", tg('screen', this.screenOn)) +
        row('LEDs', 'turn the LED strip off now', `<div class="chips"><button data-a="leds">${T.tr('OFF')}</button></div>`) +
        `<h2 class="set-h">// ${T.tr('This screen')}</h2>` +
        row('Show FPS', 'frame rate in the top bar', tg('fps', T.showFps)) +
        (UP ? row('App update', 'the app takes new screens from TENGEN · tap to check now', `<div class="chips"><button data-a="upd">${esc(T.tr(T.updStatus || 'check'))}</button></div>`) : '');
      this.body.querySelector('[data-a=screen]').onclick = () => {
        const v = this.screenOn ? 0 : 1;
        api(`/api/act?a=screen&v=${v}`).then(r => { if (!r.ok) throw new Error(r.err || 'refused'); this.screenOn = !!v; this.renderActions(); this.toast(v ? 'device screen on' : 'device screen off'); })
          .catch(e => this.toast(T.tr('failed') + ' · ' + T.tr(e.message || 'no link'), true));
      };
      this.body.querySelector('[data-a=leds]').onclick = () => {
        api('/api/act?a=leds&v=0').then(r => { if (!r.ok) throw new Error(r.err || 'refused'); this.toast('LEDs off'); })
          .catch(e => this.toast(T.tr('failed') + ' · ' + T.tr(e.message || 'no link'), true));
      };
      this.body.querySelector('[data-a=fps]').onclick = () => { T.setShowFps(!T.showFps); this.renderActions(); };
      const u = this.body.querySelector('[data-a=upd]');
      if (u) u.onclick = () => { UP.check().catch(() => {}); this.toast('checking for updates'); };
    },
  });

  // ---- device labels: 'name it' window (called from Bluetooth > Devices) ----
  T.nameDevice = (e, cur) => {
    const modal = document.getElementById('modal'), card = document.getElementById('modalCard');
    card.innerHTML = `<div class="lbl-dlg"><h3>// ${T.tr('NAME THIS DEVICE')}</h3>
      <p class="lbl-sub">${esc((e.vend || '') + ' · ' + T.tr(e.type || 'device') + (e.name ? ' · ' + e.name : ''))}</p>
      <input id="lblIn" maxlength="20" autocomplete="off" spellcheck="false" placeholder="${T.tr('my AirPods')}" value="${esc(cur || '')}">
      <p class="lbl-note" hidden></p>
      <div class="lbl-btns"><button data-b="save" class="pri">${T.tr('SAVE')}</button><button data-b="del">${T.tr('REMOVE')}</button><button data-b="cancel">${T.tr('CANCEL')}</button></div></div>`;
    modal.classList.remove('hide');
    const inp = card.querySelector('#lblIn'), note = card.querySelector('.lbl-note');
    setTimeout(() => { inp.focus(); inp.select(); }, 30);
    const close = () => { modal.classList.add('hide'); card.innerHTML = ''; };
    const send = n => {
      S.setLabel(e.id, n);                                  // visible on the tablet at once
      api(`/api/label?id=${e.id}&n=${encodeURIComponent(n)}`).then(r => {
        if (!r.ok) throw new Error(r.err || 'refused');
        if (r.stable === false && n) { note.hidden = false; note.textContent = T.tr('this device changes its address — the name may be lost'); setTimeout(close, 2600); }
        else close();
      }).catch(err => { note.hidden = false; note.textContent = T.tr('not saved') + ' · ' + T.tr(err.message || 'no link'); });
    };
    card.querySelector('[data-b=save]').onclick = () => send(inp.value.trim().slice(0, 20));
    card.querySelector('[data-b=del]').onclick = () => send('');
    card.querySelector('[data-b=cancel]').onclick = close;
    inp.onkeydown = ev => { if (ev.key === 'Enter') send(inp.value.trim().slice(0, 20)); if (ev.key === 'Escape') close(); };
    modal.onclick = ev => { if (ev.target === modal) close(); };
  };
})();
