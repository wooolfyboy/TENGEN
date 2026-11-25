// ---- page language EN / RU, follows the device (st.lang), switches live ----
// UI source text is English; i18n/ru.json is always embedded (T.DICT). Translation happens at output time:
//  - canvases: in fillText / strokeText / measureText (so layout measures the Russian text);
//  - DOM: via T.tr(...) where the string is inserted.
// T.setLang() switches: canvas frames translate themselves, DOM labels redraw (T.onLang).
// Dictionary: exact match first, else known phrases inside the string (word boundaries, longest first), case kept.
// Results are cached, so a frame costs a few Map lookups.
(() => {
  const DICT = /*@DICT*/null;
  let lang0 = /*@LANG*/'en';                                           // for a standalone HTML build (--lang)
  try { lang0 = new URLSearchParams(location.search).get('lang') || localStorage.getItem('lang') || lang0; } catch (e) {}
  T.LANG = lang0 === 'ru' && DICT ? 'ru' : 'en';
  T.L = (en, ru) => T.LANG === 'ru' && ru != null ? ru : en;          // explicit choice at the call site
  const low = new Map();                                               // lowercase key -> translation
  if (DICT) for (const k in DICT) { low.set(k.toLowerCase(), DICT[k]); const t = k.trim(); if (t !== k && !low.has(t.toLowerCase())) low.set(t.toLowerCase(), DICT[k].trim()); }
  const keys = [...low.keys()].filter(k => k.length >= 3).sort((a, b) => b.length - a.length);
  const esc = s => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const RX = keys.length ? new RegExp('(^|[^A-Za-z0-9])(' + keys.map(esc).join('|') + ')(?![A-Za-z0-9])', 'gi') : null;
  const isUp = s => s === s.toUpperCase() && s !== s.toLowerCase();
  const isCap = s => s[0] === s[0].toUpperCase() && s.slice(1) === s.slice(1).toLowerCase() && s !== s.toLowerCase();
  const fit = (src, dst) => isUp(src) ? dst.toUpperCase() : isCap(src) ? dst[0].toUpperCase() + dst.slice(1) : dst;
  const cache = new Map();
  const ru = s => {
    if (typeof s !== 'string' || !s || !/[A-Za-z]/.test(s)) return s;
    let r = cache.get(s);
    if (r !== undefined) return r;
    const exact = DICT[s] ?? low.get(s.toLowerCase());
    if (exact != null) r = DICT[s] != null ? exact : fit(s, exact);
    else r = RX ? s.replace(RX, (_, pre, w) => pre + fit(w, low.get(w.toLowerCase()))) : s;
    if (cache.size > 4000) cache.clear();
    cache.set(s, r);
    return r;
  };
  T.tr = s => T.LANG === 'ru' ? ru(s) : s;
  // callbacks that redraw DOM labels on language change (app.js menu and tabs; views their own panels)
  T.onLang = [];
  T.setLang = code => {
    code = code === 'ru' && DICT ? 'ru' : 'en';
    if (code === T.LANG) return;
    T.LANG = code;
    document.documentElement.lang = code;
    try { localStorage.setItem('lang', code); } catch (e) {}
    if (T._fitC) T._fitC.clear();
    for (const f of T.onLang) { try { f(code); } catch (e) {} }
  };
  document.documentElement.lang = T.LANG;
  // canvases: translate on draw and measure (no extra work in English)
  const P = CanvasRenderingContext2D.prototype, F = P.fillText, Sx = P.strokeText, M = P.measureText;
  P.fillText = function (s, ...a) { return F.call(this, T.LANG === 'ru' ? ru(String(s)) : s, ...a); };
  P.strokeText = function (s, ...a) { return Sx.call(this, T.LANG === 'ru' ? ru(String(s)) : s, ...a); };
  P.measureText = function (s) { return M.call(this, T.LANG === 'ru' ? ru(String(s)) : s); };
})();
