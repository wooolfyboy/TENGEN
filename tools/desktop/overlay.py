# -*- coding: utf-8 -*-
"""
TENGEN desktop - tray, popups and widget above all windows.

  Tray    - system tray icon (pystray): HQ / widget / popups / style / corner / mode / quit.
  Popups  - one small frameless always-on-top window with a stack of up to 3 cards (DENKO / KURENAI);
            it resizes to the stack and hides when empty. Click opens HQ.
  Widget  - frameless always-on-top window (mini radar, activity, temperature, remote, card);
            draggable, remembers position and size; opacity and click-through via Win32.
  SseFeed - in Wi-Fi mode the app listens to the stick's event stream (port 81) itself.

Windows / pystray / pywebview imports are lazy, so the module also loads on Linux.
"""
import json
import os
import subprocess
import sys
import threading
import time
import urllib.request

from overlay_html import POPUP_HTML, WIDGET_HTML

IS_WIN = os.name == "nt"
IS_MAC = sys.platform == "darwin"
# Widget drag: macOS uses pywebview easy_drag; Windows uses the system drag (DRAG_JS + WM_NCLBUTTONDOWN);
# Linux uses .pywebview-drag-region (easy_drag on Qt installs an app-wide event filter and crashes with SIGSEGV).
HERE = os.path.dirname(os.path.abspath(__file__))
POP_W = 380


def log(*a):
    print(time.strftime("%H:%M:%S"), "[overlay]", *a, flush=True)


# ---------- Win32: opacity, click-through, hide from taskbar ----------
GWL_EXSTYLE = -20
WS_EX_LAYERED = 0x00080000
WS_EX_TRANSPARENT = 0x00000020
WS_EX_TOOLWINDOW = 0x00000080
WS_EX_NOACTIVATE = 0x08000000
LWA_ALPHA = 0x2


def hwnd_of(win):
    """HWND of a pywebview window on Windows (WinForms backend: window.native is a Form)."""
    try:
        return int(win.native.Handle.ToInt64())
    except Exception:
        return None


def win_style(win, opacity=None, clickthrough=None, tool=True):
    if not IS_WIN:
        return False
    h = hwnd_of(win)
    if not h:
        return False
    try:
        import ctypes
        u = ctypes.windll.user32
        u.GetWindowLongW.restype = ctypes.c_long
        ex = u.GetWindowLongW(h, GWL_EXSTYLE)
        if tool:
            ex |= WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE        # not in taskbar / Alt-Tab, never takes focus
        layered = (opacity is not None and opacity < 0.999) or bool(clickthrough)
        ex = (ex | WS_EX_LAYERED) if layered else (ex & ~WS_EX_LAYERED)
        if clickthrough is not None:
            ex = (ex | WS_EX_TRANSPARENT) if clickthrough else (ex & ~WS_EX_TRANSPARENT)
        u.SetWindowLongW(h, GWL_EXSTYLE, ex)
        if layered:
            u.SetLayeredWindowAttributes(h, 0, int(255 * (opacity if opacity is not None else 1.0)), LWA_ALPHA)
        return True
    except Exception as e:
        log("win32 style:", e)
        return False


DRAG_JS = """(function(){if(window.__tgDrag)return;window.__tgDrag=1;var d=null;
addEventListener('mousedown',function(e){d=(e.button===0&&e.detail<2)?[e.screenX,e.screenY]:null},true);
addEventListener('mouseup',function(){d=null},true);
addEventListener('mousemove',function(e){if(!d||!(e.buttons&1))return;
 if(Math.abs(e.screenX-d[0])+Math.abs(e.screenY-d[1])<3)return;d=null;
 var a=window.pywebview&&window.pywebview.api;if(a&&a.widget_drag)a.widget_drag()},true)})();"""

# widget right-click menu, so it works standalone: open HQ, hide, quit
MENU_JS = """(function(){if(window.__tgMenu)return;window.__tgMenu=1;
var T={en:['Open HQ','Hide widget (F3 brings it back)','Quit TENGEN'],ru:['Открыть штаб','Скрыть виджет (вернуть — F3)','Выйти из TENGEN']};
var A=['widget_open','widget_hide','widget_quit'],m=null;
function close(){if(m){m.remove();m=null}}
addEventListener('contextmenu',function(e){e.preventDefault();close();
 var t=T[window.LANG==='ru'?'ru':'en'],c=getComputedStyle(document.body).color;
 m=document.createElement('div');m.style.cssText='position:fixed;z-index:99;min-width:150px;background:#050302;border:1px solid '+c+
  ';font:12px/1 Consolas,monospace;letter-spacing:.06em;color:'+c+';box-shadow:0 4px 18px #000;padding:3px 0';
 t.forEach(function(s,i){var b=document.createElement('div');b.textContent=s;b.style.cssText='padding:8px 12px;cursor:pointer;white-space:nowrap';
  b.onmouseenter=function(){b.style.background='rgba(255,255,255,.12)'};b.onmouseleave=function(){b.style.background=''};
  b.onmousedown=function(ev){ev.stopPropagation()};
  b.onclick=function(){close();var a=window.pywebview&&window.pywebview.api;if(a&&a[A[i]])a[A[i]]()};m.appendChild(b)});
 document.body.appendChild(m);
 m.style.left=Math.max(0,Math.min(e.clientX,innerWidth-m.offsetWidth-2))+'px';m.style.top=Math.max(0,Math.min(e.clientY,innerHeight-m.offsetHeight-2))+'px'},true);
addEventListener('mousedown',function(e){if(m&&!m.contains(e.target))close()},true);
addEventListener('keydown',function(e){if(e.key==='Escape')close()})})();"""

PARK = -32000                      # popup window "parking" spot off-screen (Windows)


def work_area():
    """Main screen work area (minus taskbar) in logical pixels, which pywebview uses to move windows.
    Windows: SPI_GETWORKAREA returns physical pixels (DPI-aware process), so divide by the screen scale."""
    if IS_WIN:
        try:
            import ctypes
            from ctypes import wintypes
            r = wintypes.RECT()
            ctypes.windll.user32.SystemParametersInfoW(0x30, 0, ctypes.byref(r), 0)
            try:
                k = ctypes.windll.user32.GetDpiForSystem() / 96.0
            except Exception:
                k = 1.0
            k = k or 1.0
            return r.left / k, r.top / k, r.right / k, r.bottom / k
        except Exception as e:
            log("work area:", e)
    W, H = screen_size()
    return 0, 0, W, H - 56


def screen_size():
    try:
        import webview
        s = webview.screens[0]
        return int(s.width), int(s.height)
    except Exception:
        return 1920, 1080


# ============================================================
#  Popups
# ============================================================
class _PopupApi:
    def __init__(self, owner):
        self._o = owner

    def popup_size(self, h):
        # Run off the WebView2 COM callback stack so WinForms Form.Invoke never recurses
        threading.Thread(target=self._o._size, args=(int(h or 0),), daemon=True).start()

    def popup_click(self):
        threading.Thread(target=self._o.app.show_main, daemon=True).start()


class Popups:
    def __init__(self, app):
        import webview
        self.app = app
        self.ready = False
        self.pending = []
        self.visible = False
        self._last_h = -1
        self.lock = threading.Lock()
        # Windows: park the window off-screen instead of hiding it; WebView2 never rendered in a window created
        # hidden (blank grey card). Show = move into place, hide = move back out.
        kw = dict(x=PARK, y=PARK, hidden=False) if IS_WIN else dict(hidden=True)
        self.win = webview.create_window("TENGEN alert", html=POPUP_HTML, width=POP_W, height=120, frameless=True, easy_drag=False,
                                         on_top=True, resizable=False, focus=False, **kw,
                                         background_color="#000000", js_api=_PopupApi(self))
        self.win.events.loaded += self._loaded

    def _loaded(self):
        win_style(self.win, tool=True)
        self.ready = True
        with self.lock:
            p, self.pending = self.pending, []
        for style, dur, n in p:
            self._send(style, dur, n)

    def show(self, note, style, dur):
        if not self.ready:
            with self.lock:
                self.pending.append((style, dur, note))
            return
        self._send(style, dur, note)

    def _send(self, style, dur, n):
        try:
            self.win.evaluate_js("P.style(%s);P.dur(%s);P.add(%s)" % (json.dumps(style), float(dur), json.dumps(n, ensure_ascii=False)))
        except Exception as e:
            log("popup:", e)

    def _size(self, h):
        if h == self._last_h:
            return
        self._last_h = h
        if h <= 0:
            if self.visible:
                self.visible = False
                try:
                    self.win.move(PARK, PARK) if IS_WIN else self.win.hide()
                except Exception as e:
                    log("popup hide:", e)
            return
        L, T, R, B = work_area()
        corner = self.app.engine.prefs.get("corner", "br")
        m = 12                                             # margin from the work-area edge (taskbar already excluded)
        x = R - POP_W - m if corner in ("br", "tr") else L + m
        y = B - h - m if corner in ("br", "bl") else T + m
        try:
            self.win.resize(POP_W, h)
            self.win.move(x, y)
            if not self.visible:
                self.visible = True
                if not IS_WIN:
                    self.win.show()
                win_style(self.win, tool=True)
        except Exception as e:
            log("popup place:", e)


# ============================================================
#  Widget
# ============================================================
class _WidgetApi:
    def __init__(self, owner):
        self._o = owner

    def widget_open(self):
        self._o.app.show_main()

    def widget_drag(self):
        self._o.native_drag()

    def widget_hide(self):
        self._o.app.set_pref("widget", False)

    def widget_quit(self):
        threading.Thread(target=self._o.app.quit, daemon=True).start()


class Widget:
    def __init__(self, app):
        import webview
        self.app = app
        p = app.engine.prefs
        r = p.get("widget_rect") or [None, None, 320, 200]
        W, H = screen_size()
        x = r[0] if r[0] is not None else W - 340
        y = r[1] if r[1] is not None else 80
        if not (-100 <= x <= W - 120 and -20 <= y <= H - 80):       # off-screen (monitor changed): reset to corner
            x, y = W - 340, 80
        self.ready = False
        self.shown = bool(p.get("widget"))
        # Windows: park the window off-screen (PARK) instead of hiding it; WebView2 does not render in a window
        # created hidden, so a widget enabled later never appeared. Show = move back.
        if IS_WIN:
            kw = dict(x=int(x) if self.shown else PARK, y=int(y) if self.shown else PARK, hidden=False)
        else:
            kw = dict(x=int(x), y=int(y), hidden=not self.shown)
        self.win = webview.create_window("TENGEN widget", html=WIDGET_HTML, width=int(r[2]), height=int(r[3]), **kw,
                                         frameless=True, easy_drag=IS_MAC, on_top=True, resizable=True,
                                         focus=False, min_size=(240, 150), background_color="#050302", js_api=_WidgetApi(self))
        self.win.events.loaded += self._loaded
        try:
            self.win.events.moved += self._moved
            self.win.events.resized += self._resized
        except Exception:
            pass
        self._rect = [x, y, r[2], r[3]]
        self._save_tmr = None

    def _loaded(self):
        self.ready = True
        try:
            # Windows: the OS drags (like a title bar), which is smooth; the request is sent once, when the button
            # is down and the mouse moved 3+ px, so click and double-click (HQ) still work.
            # Linux: pywebview drags by .pywebview-drag-region.
            self.win.evaluate_js(MENU_JS)
            if IS_WIN:
                self.win.evaluate_js(DRAG_JS)
            elif not IS_MAC:
                self.win.evaluate_js("document.getElementById('w').classList.add('pywebview-drag-region')")
        except Exception as e:
            log("widget drag:", e)
        self.apply_style()
        try:
            self.win.evaluate_js("W.pause(%s)" % ("false" if self.shown else "true"))
        except Exception:
            pass

    def native_drag(self):
        """System drag (like a title bar). WebView2 holds the mouse capture in the window thread and it can only
        be released there, so both calls go to the form thread (BeginInvoke)."""
        h = hwnd_of(self.win)
        if not (IS_WIN and h):
            return
        import ctypes
        u = ctypes.windll.user32

        def go():
            u.ReleaseCapture()
            u.SendMessageW(h, 0x00A1, 2, 0)            # WM_NCLBUTTONDOWN, HTCAPTION
        try:
            from System import Action                  # pythonnet: pywebview windows run on it on Windows
            self.win.native.BeginInvoke(Action(go))
        except Exception as e:
            log("widget drag:", e)
            try:
                go()
            except Exception:
                pass

    def apply_style(self):
        p = self.app.engine.prefs
        win_style(self.win, opacity=float(p.get("opacity", 1.0)), clickthrough=bool(p.get("clickthrough")), tool=True)
        if self.ready:
            try:
                self.win.evaluate_js("W.style(%s);W.lang(%s)" % (json.dumps(self.app.style()), json.dumps(self.app.lang)))
            except Exception:
                pass

    def _moved(self, x, y):
        if not self.shown or x <= PARK // 2:           # parked: not a new widget position
            return
        self._rect[0], self._rect[1] = x, y
        self._save_later()

    def _resized(self, w, h):
        self._rect[2], self._rect[3] = w, h
        self._save_later()

    def _save_later(self):
        if self._save_tmr is not None:
            self._save_tmr.cancel()

        def go():
            self.app.set_pref("widget_rect", list(self._rect))
        tmr = threading.Timer(1.0, go)
        tmr.daemon = True
        self._save_tmr = tmr
        tmr.start()

    def set_shown(self, on):
        self.shown = on
        try:
            if self.ready:
                try:
                    self.win.evaluate_js("W.pause(%s)" % ("false" if on else "true"))
                except Exception:
                    pass
            if IS_WIN:
                self.win.move(*(self._rect[:2] if on else (PARK, PARK)))
                if on:
                    self.apply_style()
            elif on:
                self.win.show()
                self.apply_style()
            else:
                self.win.hide()
        except Exception as e:
            log("widget:", e)

    def update(self, snap):
        if not (self.ready and self.shown):
            return
        try:
            self.win.evaluate_js("W.update(%s)" % json.dumps(snap, ensure_ascii=False))
        except Exception:
            pass


def os_notify(title, text):
    """Native notification on macOS (AppleScript) and Linux (notify-send)."""
    try:
        if IS_MAC:
            esc = lambda x: (x or "").replace("\\", "\\\\").replace('"', '\\"')
            subprocess.run(["osascript", "-e", 'display notification "%s" with title "%s"' % (esc(text) or " ", esc(title))],
                           timeout=3, capture_output=True)
            return True
        r = subprocess.run(["notify-send", "-a", "TENGEN Desktop", title or "TENGEN", text or " "], timeout=3, capture_output=True)
        return r.returncode == 0
    except Exception as e:
        log("notify:", e)
        return False


# ============================================================
#  Tray
# ============================================================
class Tray:
    def __init__(self, app):
        self.app = app
        self.icon = None
        if os.environ.get("TENGEN_NO_TRAY"):             # CI / headless: no tray needed
            return
        try:
            import pystray
            from PIL import Image
        except Exception as e:
            log("трей недоступен:", e)
            return
        self.pystray = pystray
        img = Image.open(os.path.join(HERE, "tengen.ico"))
        M, I = pystray.Menu, pystray.MenuItem
        a = app
        p = lambda k: (lambda item: bool(a.engine.prefs.get(k)))
        radio = lambda k, v: (lambda item: a.engine.prefs.get(k) == v)
        T = lambda k: (lambda item: a.t(k))                 # labels in the window language (it may arrive later)
        L2 = lambda en, ru: (lambda item: ru if a.lang == "ru" else en)
        self.icon = pystray.Icon("TENGEN", img, "TENGEN Desktop", menu=M(
            I(T("tray_open"), lambda: a.show_main(), default=True),
            M.SEPARATOR,
            I(T("tray_widget"), lambda: a.toggle_pref("widget"), checked=p("widget")),
            I(T("tray_popups"), lambda: a.toggle_pref("popups"), checked=p("popups")),
            I(T("tray_native"), lambda: a.toggle_pref("native"), checked=p("native")),
            I(T("tray_click"), lambda: a.toggle_pref("clickthrough"), checked=p("clickthrough")),
            I(T("tray_style"), M(*[I(n, (lambda v: lambda: a.set_pref("style", v))(v), checked=radio("style", v), radio=True)
                                   for n, v in ((L2("as the TENGEN theme", "как тема TENGEN"), "auto"), ("DENKO", "denko"), ("KURENAI", "kurenai"))])),
            I(T("tray_corner"), M(*[I(n, (lambda v: lambda: a.set_pref("corner", v))(v), checked=radio("corner", v), radio=True)
                                    for n, v in ((L2("bottom right", "справа внизу"), "br"), (L2("bottom left", "слева внизу"), "bl"),
                                                 (L2("top right", "справа вверху"), "tr"), (L2("top left", "слева вверху"), "tl"))])),
            I(T("tray_mode"), M(*[I(n, (lambda v: lambda: a.start(v))(v), checked=(lambda v: lambda item: a.cfg.get("mode") == v)(v), radio=True)
                                  for n, v in (("USB", "usb"), ("Wi-Fi", "wifi"), (L2("Auto", "Авто"), "auto"))])),
            I(T("tray_prefs"), lambda: a.show_main(prefs=True)),
            M.SEPARATOR,
            I(T("tray_quit"), lambda: a.quit()),
        ))
        if IS_MAC:
            # macOS: the icon lives in the Cocoa loop started by webview.start, no own thread
            self.icon.run_detached()
        else:
            def _run():
                try:
                    self.icon.run()
                except Exception as e:
                    log("трей не запустился:", e)
            threading.Thread(target=_run, daemon=True).start()

    def refresh(self):
        if self.icon:
            try:
                self.icon.update_menu()
            except Exception:
                pass

    def notify(self, title, text):
        if not IS_WIN:                                   # macOS / Linux: native notifications are more reliable than the tray
            return os_notify(title, text)
        if self.icon:
            try:
                self.icon.notify(text or " ", title)
                return True
            except Exception as e:
                log("notify:", e)
        return False

    def stop(self):
        if self.icon:
            try:
                self.icon.stop()
            except Exception:
                pass


# ============================================================
#  Wi-Fi: own stick event stream (SSE, port 81) for the alert engine
# ============================================================
class SseFeed:
    WANT = "ble,ir,nfc,sens,wx"

    def __init__(self, on_event, on_link, host="192.168.4.1", port=81):
        self.on_event, self.on_link = on_event, on_link
        self.url = "http://%s:%d/stream?want=%s" % (host, port, self.WANT)
        self.run_flag = False
        self.th = None

    def start(self):
        if self.run_flag:
            return
        self.run_flag = True
        self._resp = None
        self.th = threading.Thread(target=self._loop, daemon=True)
        self.th.start()

    def stop(self):
        self.run_flag = False
        r = getattr(self, "_resp", None)
        if r is not None:
            try:
                r.close()
            except Exception:
                pass

    def _loop(self):
        while self.run_flag:
            try:
                with urllib.request.urlopen(self.url, timeout=8) as r:
                    self._resp = r
                    self.on_link(True)
                    ev, data = "", []
                    for raw in r:
                        if not self.run_flag:
                            break
                        line = raw.decode("utf-8", "replace").rstrip("\r\n")
                        if line.startswith("event:"):
                            ev = line[6:].strip()
                        elif line.startswith("data:"):
                            data.append(line[5:].lstrip())
                        elif line == "":
                            if ev and data:
                                self.on_event(ev, "\n".join(data))
                            ev, data = "", []
            except Exception:
                pass
            self.on_link(False)
            for _ in range(30):
                if not self.run_flag:
                    return
                time.sleep(0.1)


# ---------- single instance ----------
_mutex = None


def single_instance(port):
    """True if we are first; otherwise ask the running instance to show its window and exit."""
    global _mutex
    if IS_WIN:
        try:
            import ctypes
            _mutex = ctypes.windll.kernel32.CreateMutexW(None, False, "TENGEN_desktop_single")
            if ctypes.windll.kernel32.GetLastError() != 183:      # ERROR_ALREADY_EXISTS
                return True
        except Exception:
            return True
    else:
        import socket
        s = socket.socket()
        try:
            s.settimeout(0.3)
            s.connect(("127.0.0.1", port))
        except Exception:
            return True
        finally:
            s.close()
    for p in range(port, port + 20):
        try:
            urllib.request.urlopen("http://127.0.0.1:%d/__app/show" % p, timeout=1).read()
            break
        except Exception:
            continue
    return False
