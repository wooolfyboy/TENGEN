# -*- coding: utf-8 -*-
"""
TENGEN laptop monitor.

Once a second it reads CPU, memory, disk, network and battery and sends them to TENGEN over USB
(shown in the PC section). TENGEN sends back remote commands (Media page in PC):
KEY VOLUP / VOLDN / PLAY / NEXT / PREV / MUTE, which the program turns into media key presses.

PC sound: while the Sound section is open on TENGEN it sends "AUD ON"; the program captures what plays
on the speakers/headphones (not the microphone), computes the spectrum and sends ~30 "PA ..." lines per second.

Run by double-clicking start_pc_monitor.bat (it installs everything). Stop: close the window or Ctrl+C.
"""
import os
import asyncio
import socket
import sys
import threading
import time
import unicodedata

try:
    import psutil
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Не хватает библиотек. Запусти программу через start_pc_monitor.bat — он их поставит.")
    input("Нажми Enter, чтобы закрыть окно...")
    sys.exit(1)

ESP_VID = 0x303A          # USB vendor ID of Espressif chips (our stick)

# ---------- media keys ----------
# Windows: virtual keys; macOS: AppleScript (system volume, Spotify / Music);
# Linux: playerctl (MPRIS: any player or browser) and pactl / amixer (volume).
IS_WIN = os.name == "nt"
IS_MAC = sys.platform == "darwin"
IS_LINUX = sys.platform.startswith("linux")
VK = {"VOLUP": 0xAF, "VOLDN": 0xAE, "MUTE": 0xAD, "PLAY": 0xB3, "NEXT": 0xB0, "PREV": 0xB1}
if IS_WIN:
    import ctypes
    _user32 = ctypes.windll.user32


def run_cmd(args, timeout=2.0):
    """Run a system command (macOS / Linux). Return its output, or None (missing program, error)."""
    import subprocess
    try:
        r = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
        return r.stdout if r.returncode == 0 else None
    except Exception:
        return None


def osa(script, timeout=2.0):
    return run_cmd(["osascript", "-e", script], timeout)


# macOS: control whichever player is running (Spotify first, then Music)
_MAC_PLAYER = ('if application "Spotify" is running then\n tell application "Spotify" to %s\n'
               'else if application "Music" is running then\n tell application "Music" to %s\nend if')
_MAC_CMD = {"PLAY": ("playpause", "playpause"), "NEXT": ("next track", "next track"), "PREV": ("previous track", "previous track")}


def press(name):
    """Press a media key, like on a laptop keyboard."""
    if IS_WIN:
        code = VK.get(name)
        if code is None:
            return
        _user32.keybd_event(code, 0, 0, 0)        # key down
        _user32.keybd_event(code, 0, 2, 0)        # key up
    elif IS_MAC:
        if name in _MAC_CMD:
            osa(_MAC_PLAYER % _MAC_CMD[name])
        elif name == "VOLUP":
            osa("set volume output volume ((output volume of (get volume settings)) + 6)")
        elif name == "VOLDN":
            osa("set volume output volume ((output volume of (get volume settings)) - 6)")
        elif name == "MUTE":
            osa("set volume output muted (not (output muted of (get volume settings)))")
    elif IS_LINUX:
        if name in ("PLAY", "NEXT", "PREV"):
            run_cmd(["playerctl", {"PLAY": "play-pause", "NEXT": "next", "PREV": "previous"}[name]])
        elif name in ("VOLUP", "VOLDN", "MUTE"):
            pa = {"VOLUP": ["set-sink-volume", "@DEFAULT_SINK@", "+5%"], "VOLDN": ["set-sink-volume", "@DEFAULT_SINK@", "-5%"],
                  "MUTE": ["set-sink-mute", "@DEFAULT_SINK@", "toggle"]}[name]
            if run_cmd(["pactl"] + pa) is None:
                run_cmd(["amixer", "-q", "sset", "Master", {"VOLUP": "5%+", "VOLDN": "5%-", "MUTE": "toggle"}[name]])


# ---------- current volume (Windows: pycaw; macOS: AppleScript; Linux: pactl / amixer) ----------
_vol_iface = None


def read_volume():
    """Return (volume 0..100, muted?) or None."""
    global _vol_iface
    if IS_MAC:
        out = osa('set s to get volume settings\nreturn ((output volume of s) as text) & "," & ((output muted of s) as text)')
        try:
            v, m = out.strip().split(",")
            return int(float(v)), m.strip() == "true"
        except Exception:
            return None
    if IS_LINUX:
        import re
        out = run_cmd(["pactl", "get-sink-volume", "@DEFAULT_SINK@"])
        if out:
            m = re.search(r"(\d+)%", out)
            mute = run_cmd(["pactl", "get-sink-mute", "@DEFAULT_SINK@"]) or ""
            if m:
                return int(m.group(1)), "yes" in mute.lower()
        out = run_cmd(["amixer", "get", "Master"])
        if out:
            m = re.search(r"\[(\d+)%\].*?\[(on|off)\]", out)
            if m:
                return int(m.group(1)), m.group(2) == "off"
        return None
    if not IS_WIN:
        return None
    try:
        if _vol_iface is None:
            try:
                import comtypes
                comtypes.CoInitialize()
            except Exception:
                pass
            from pycaw.pycaw import AudioUtilities, IAudioEndpointVolume
            spk = AudioUtilities.GetSpeakers()
            if hasattr(spk, "EndpointVolume"):           # newer pycaw
                _vol_iface = spk.EndpointVolume
            else:                                        # older pycaw
                from comtypes import CLSCTX_ALL
                from ctypes import POINTER, cast
                raw = spk.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None)
                _vol_iface = cast(raw, POINTER(IAudioEndpointVolume))
        level = _vol_iface.GetMasterVolumeLevelScalar()
        return int(round(level * 100)), bool(_vol_iface.GetMute())
    except Exception:
        _vol_iface = None
        return None


def say(msg=""):
    print(msg, flush=True)


def find_port(forced=None):
    """Find the stick among USB devices. Return the port name (e.g. COM5) or None."""
    if forced:
        return forced
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        if p.vid == ESP_VID:
            return p.device
    for p in ports:                                   # fallback: by name
        d = (p.description or "").lower()
        if "jtag" in d or "esp32" in d:
            return p.device
    for p in ports:                                   # macOS / Linux: USB-CDC port by name
        if p.device.startswith("/dev/cu.usbmodem") or p.device.startswith("/dev/ttyACM"):
            return p.device
    return None


def open_port(name):
    s = serial.Serial()
    s.port = name
    s.baudrate = 115200
    s.dtr = False             # otherwise the stick may reset on connect
    s.rts = False
    s.write_timeout = 2
    s.timeout = 1             # reading commands from the stick: wait at most 1 s per line
    s.open()
    return s


def clean(text, n=20):
    """Strip spaces and junk from a name (the stick splits lines on spaces)."""
    text = "".join(ch if ch.isalnum() or ch in "._-" else "_" for ch in (text or ""))
    return text[:n] or "-"


class Collector:
    def __init__(self):
        psutil.cpu_percent(interval=None)
        psutil.cpu_percent(interval=None, percpu=True)
        self.t = time.time()
        self.disk = psutil.disk_io_counters()
        self.net = psutil.net_io_counters()
        self.top = "-"
        self.city = None                      # "PP ..." line for the process city
        self.procs = {}
        # the hungriest-process search runs in its own thread: on Windows scanning
        # hundreds of processes takes 1-2 s and would delay sending
        threading.Thread(target=self._top_loop, daemon=True).start()
        self.root = "C:\\" if os.name == "nt" else "/"
        self.host = clean(socket.gethostname())

    def _top_loop(self):
        while True:
            try:
                self._find_top()
            except Exception:
                pass
            time.sleep(2)

    @staticmethod
    def _foreground_pid():
        """The program whose window is in the foreground (Windows only)."""
        if not IS_WIN:
            return -1
        try:
            hwnd = _user32.GetForegroundWindow()
            pid = ctypes.c_ulong()
            _user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            return pid.value
        except Exception:
            return -1

    def _find_top(self):
        """Hungriest process + process city (per program, not per window)."""
        best, best_cpu = "-", -1.0
        seen = {}
        apps = {}                                  # program name -> [cpu, memory MB, active?]
        fg = self._foreground_pid()
        ncpu = psutil.cpu_count() or 1
        for p in psutil.process_iter(["name", "memory_info"]):
            try:
                if p.pid == 0:
                    continue              # "System Idle Process" is idle time, not load
                proc = self.procs.get(p.pid) or p
                cpu = proc.cpu_percent(interval=None) / ncpu      # % of total CPU
                seen[p.pid] = proc
                name = clean((p.info["name"] or "-").replace(".exe", ""), 18)
                mem = (p.info["memory_info"].rss if p.info["memory_info"] else 0) / 1048576
                a = apps.setdefault(name, [0.0, 0.0, 0])
                a[0] += cpu; a[1] += mem
                if p.pid == fg:
                    a[2] = 1
                if cpu > best_cpu:
                    best, best_cpu = name, cpu
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pass
        self.procs = seen
        self.top = best
        # the city gets 24 programs: busiest by CPU, then biggest by memory, plus the active one
        items = sorted(apps.items(), key=lambda kv: (-kv[1][0], -kv[1][1]))
        pick = [kv for kv in items if kv[1][0] >= 0.3][:16]
        names = {k for k, _ in pick}
        for k, v in sorted(apps.items(), key=lambda kv: -kv[1][1]):
            if len(pick) >= 24:
                break
            if k not in names and (v[1] > 40 or v[2]):
                pick.append((k, v)); names.add(k)
        self.city = "PP\t" + "\t".join("%s,%d,%d,%d" % (k, int(v[0] * 10), int(v[1]), v[2]) for k, v in pick) + "\n"

    def line(self):
        now = time.time()
        dt = max(0.2, now - self.t)
        self.t = now
        cpu = psutil.cpu_percent(interval=None)
        cores = psutil.cpu_percent(interval=None, percpu=True)[:16]
        try:
            freq = (psutil.cpu_freq().current or 0) / 1000.0
        except Exception:
            freq = 0
        vm = psutil.virtual_memory()
        try:
            du = psutil.disk_usage(self.root).percent
        except Exception:
            du = 0
        d = psutil.disk_io_counters()
        dr = dw = 0.0
        if d and self.disk:
            dr = (d.read_bytes - self.disk.read_bytes) / dt / 1048576
            dw = (d.write_bytes - self.disk.write_bytes) / dt / 1048576
        self.disk = d
        n = psutil.net_io_counters()
        nd = (n.bytes_recv - self.net.bytes_recv) / dt / 1024
        nu = (n.bytes_sent - self.net.bytes_sent) / dt / 1024
        self.net = n
        bat = psutil.sensors_battery() if hasattr(psutil, "sensors_battery") else None
        up = int(now - psutil.boot_time())
        parts = [
            "PC",
            "cpu=%.1f" % cpu,
            "freq=%.2f" % freq,
            "c=" + ",".join(str(int(round(c))) for c in cores),
            "ram=%.1f" % vm.percent,
            "ramu=%.1f" % ((vm.total - vm.available) / 1073741824),
            "ramt=%.1f" % (vm.total / 1073741824),
            "dsk=%.0f" % du,
            "dr=%.1f" % max(0, dr),
            "dw=%.1f" % max(0, dw),
            "nd=%.0f" % max(0, nd),
            "nu=%.0f" % max(0, nu),
            "up=%d" % up,
            "top=" + self.top,
            "host=" + self.host,
            "ts=%d" % int(time.time()),                       # exact time for the sky and satellites on the stick
            "tz=%d" % (time.localtime().tm_gmtoff or 0),
        ]
        if bat is not None:
            parts += ["bat=%d" % bat.percent, "plug=%d" % (1 if bat.power_plugged else 0)]
        return " ".join(parts) + "\n", cpu, vm.percent


# ---------- now playing (Windows: the system media panel) ----------
# Needs the winrt library (installed by start_pc_monitor.bat); without it the stick just
# shows no track title and everything else works.
_media_mgr_cls = None
_media_import_err = ""
try:
    from winrt.windows.media.control import GlobalSystemMediaTransportControlsSessionManager as _media_mgr_cls
except Exception as e1:
    try:
        from winsdk.windows.media.control import GlobalSystemMediaTransportControlsSessionManager as _media_mgr_cls
    except Exception:
        _media_mgr_cls = None
        _media_import_err = "%s: %s" % (type(e1).__name__, e1)

PUNCT = {"\u2014": "-", "\u2013": "-", "\u2012": "-", "\u2212": "-", "\u2018": "'", "\u2019": "'", "\u201c": '"',
         "\u201d": '"', "\u00ab": '"', "\u00bb": '"', "\u2026": "...", "\u00a0": " ", "\u00d7": "x", "\u2022": "*"}


def screen_text(text, limit):
    """Keep what the stick can draw: Latin, digits, punctuation and Cyrillic."""
    out = []
    for ch in text or "":
        ch = PUNCT.get(ch, ch)
        if len(ch) > 1:
            out.append(ch)
            continue
        o = ord(ch)
        if 0x20 <= o < 0x7F or 0x400 <= o <= 0x45F:
            out.append(ch)
        elif ch in "\t\r\n":
            out.append(" ")
        else:                                     # e-acute -> e; skip the rest (CJK, emoji)
            base = unicodedata.normalize("NFKD", ch)[0]
            if 0x20 <= ord(base) < 0x7F:
                out.append(base)
    s = " ".join("".join(out).split())
    while len(s.encode("utf-8")) > limit:
        s = s[:-1]
    return s


def app_name(aumid):
    a = (aumid or "").lower()
    for key, name in (("spotify", "Spotify"), ("chrome", "Chrome"), ("msedge", "Edge"), ("firefox", "Firefox"),
                      ("yandex", "Yandex"), ("opera", "Opera"), ("vlc", "VLC"), ("telegram", "Telegram"),
                      ("zune", "Media Player"), ("music", "Music"), ("aimp", "AIMP"), ("foobar", "foobar"),
                      ("308046b0af4a39cb", "Firefox")):
        if key in a:
            return name
    tail = a.replace("\\", "!").split("!")[-1].split(".exe")[0]
    return screen_text(tail[:14] or "player", 14)


def window_track():
    """Fallback: take the title from the window title (Spotify, YouTube tab)."""
    if not IS_WIN:
        return None
    found = []
    titles = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def cb(hwnd, _):
        if _user32.IsWindowVisible(hwnd):
            n = _user32.GetWindowTextLengthW(hwnd)
            if n > 0:
                buf = ctypes.create_unicode_buffer(n + 1)
                _user32.GetWindowTextW(hwnd, buf, n + 1)
                pid = ctypes.c_ulong()
                _user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                titles.append((buf.value, pid.value))
        return True
    _user32.EnumWindows(cb, 0)
    for title, pid in titles:
        try:
            pname = psutil.Process(pid).name().lower()
        except Exception:
            pname = ""
        if pname == "spotify.exe" and " - " in title:          # playing: "Artist - Song"
            artist, song = title.split(" - ", 1)
            found.append(("Spotify", song, artist))
        elif " - YouTube" in title:                             # YouTube tab: "Song - YouTube - Google Chrome"
            found.append(("YouTube", title.split(" - YouTube")[0], ""))
        else:                                                   # Yandex Music tab: "Song - Artist - Yandex Music"
            low = title.lower()
            for mark in ("яндекс музык", "yandex music", "яндекс.музык"):
                k = low.find(mark)
                if k > 0:
                    head = title[:k].rstrip(" -—–|·")
                    parts = [x.strip() for x in head.replace("—", "\u0001").replace(" - ", "\u0001").split("\u0001") if x.strip()]
                    if parts and parts[0].lower() not in ("главная", "моя волна", "коллекция", "поиск"):
                        found.append(("Yandex", parts[0], parts[1] if len(parts) > 1 else ""))
                    break
    if not found:
        return None
    app, song, artist = found[0]
    return "PM\t1\t-1\t-1\t%s\t%s\t%s\n" % (app, screen_text(song, 90), screen_text(artist, 70))


_has_playerctl = None


def unix_track():
    """macOS: AppleScript (Spotify / Music); Linux: playerctl (MPRIS: any player or browser).
    Return a "PM ..." line for TENGEN, "PM\\n" (nothing playing) or None (method unavailable)."""
    def pm(playing, pos, dur, app, title, artist):
        title, artist = screen_text(title, 90), screen_text(artist, 70)
        if not title and not artist:
            return "PM\n"
        if dur <= 0:
            pos = dur = -1
        elif pos > dur:
            pos = dur
        return "PM\t%d\t%d\t%d\t%s\t%s\t%s\n" % (playing, pos, dur, app, title, artist)
    if IS_MAC:
        for app, dur_ms in (("Spotify", True), ("Music", False)):
            script = ('if application "%s" is running then\n tell application "%s"\n'
                      '  if player state is stopped then return ""\n'
                      '  return (player state as text) & "\t" & (player position as text) & "\t" & '
                      '((duration of current track) as text) & "\t" & (name of current track) & "\t" & (artist of current track)\n'
                      ' end tell\nend if\nreturn ""') % (app, app)
            out = osa(script)
            if out is None:
                continue
            parts = out.rstrip("\n").split("\t")
            if len(parts) < 5:
                continue
            try:
                pos = int(float(parts[1].replace(",", ".")))
                dur = float(parts[2].replace(",", "."))
                dur = int(dur / 1000) if dur_ms else int(dur)
            except Exception:
                pos = dur = -1
            return pm(1 if parts[0].strip() == "playing" else 0, pos, dur, app, parts[3], parts[4])
        return "PM\n"
    if IS_LINUX:
        global _has_playerctl
        if _has_playerctl is False:
            return None
        out = run_cmd(["playerctl", "metadata", "--format",
                       "{{status}}\t{{position}}\t{{mpris:length}}\t{{playerName}}\t{{title}}\t{{artist}}"])
        if out is None:
            if _has_playerctl is None:
                _has_playerctl = run_cmd(["playerctl", "--version"]) is not None
            return None if not _has_playerctl else "PM\n"
        _has_playerctl = True
        parts = out.rstrip("\n").split("\t")
        if len(parts) < 6 or parts[0] not in ("Playing", "Paused"):
            return "PM\n"
        try:
            pos = int(int(parts[1] or 0) / 1000000)
            dur = int(int(parts[2] or 0) / 1000000)
        except Exception:
            pos = dur = -1
        return pm(1 if parts[0] == "Playing" else 0, pos, dur, screen_text((parts[3] or "player").capitalize(), 14), parts[4], parts[5])
    return None


class NowPlaying:
    """Background thread: once a second finds what is playing and reports it in the window."""
    def __init__(self):
        self.line = None                          # ready line for the stick
        self.ok = _media_mgr_cls is not None or IS_WIN or IS_MAC or IS_LINUX
        self.err = None
        self.shown = None
        if self.ok:
            threading.Thread(target=self._run, daemon=True).start()

    def _note(self, text):
        say("")
        say("     " + text)

    def _run(self):
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        mgr = None
        while True:
            line, how = None, ""
            if not IS_WIN:                                       # macOS / Linux: own methods
                try:
                    line = unix_track()
                    how = "AppleScript" if IS_MAC else "playerctl"
                    if line is None:
                        if self.err != "none":
                            self.err = "none"
                            self._note("[трек] не нашёл playerctl — название трека не покажу (sudo apt install playerctl)")
                        time.sleep(5)
                        continue
                except Exception as e:
                    line = None
                    if str(e) != self.err:
                        self.err = str(e)
                        self._note("[трек] " + str(e)[:150])
            elif _media_mgr_cls is not None:
                try:
                    if mgr is None:
                        mgr = loop.run_until_complete(self._request())
                    line = loop.run_until_complete(self._read(mgr))
                    how = "Windows"
                    if self.err:
                        self.err = None
                except Exception as e:
                    mgr = None
                    msg = "%s: %s" % (type(e).__name__, e)
                    if msg != self.err:                      # report the same error once
                        self.err = msg
                        self._note("[трек] системная панель не ответила: " + msg[:150])
            if (line is None or line == "PM\n") and IS_WIN:
                try:
                    w = window_track()
                    if w:
                        line, how = w, "заголовок окна"
                except Exception as e:
                    msg = "window: %s" % e
                    if msg != self.err:
                        self.err = msg
                        self._note("[трек] " + msg[:150])
            self.line = line or "PM\n"
            shown = self.line.strip().split("\t")[4:6]
            if shown != self.shown:                          # track changed: report in the window
                self.shown = shown
                self._note("[трек] " + (" | ".join(shown) + "  (" + how + ")" if shown else "ничего не играет"))
            time.sleep(1)

    @staticmethod
    async def _request():
        return await _media_mgr_cls.request_async()

    @staticmethod
    async def _read(mgr):
        # There can be several sources (browser, player...). Windows' "current" one is the last
        # touched, not the one playing, so pick the playing one.
        s = None
        try:
            sessions = list(mgr.get_sessions())
        except Exception:
            sessions = []
        for cand in sessions:
            try:
                if int(cand.get_playback_info().playback_status) == 4:
                    s = cand
                    break
            except Exception:
                pass
        if s is None:
            s = mgr.get_current_session()
        if s is None:
            return "PM\n"
        props = await s.try_get_media_properties_async()
        info = s.get_playback_info()
        tl = s.get_timeline_properties()
        playing = 1 if int(info.playback_status) == 4 else 0      # 4 = playing, 5 = paused

        def secs(v):
            try:
                return int(v.total_seconds())
            except Exception:
                try:
                    return int(v.duration / 10000000)             # older versions: 100 ns ticks
                except Exception:
                    return -1
        pos, dur = secs(tl.position), secs(tl.end_time)
        # the app updates position rarely; Windows remembers WHEN it was written, so add the elapsed
        # time (otherwise the stick lags)
        if playing and pos >= 0:
            try:
                import datetime as _dt
                upd = tl.last_updated_time
                if upd is not None and upd.year > 2000:
                    if upd.tzinfo is None:
                        upd = upd.replace(tzinfo=_dt.timezone.utc)
                    late = (_dt.datetime.now(_dt.timezone.utc) - upd).total_seconds()
                    if 0 < late < 3600:
                        pos += int(late)
            except Exception:
                pass
        if dur <= 0:
            pos = dur = -1
        elif pos > dur:
            pos = dur
        title = screen_text(props.title, 90)
        artist = screen_text(props.artist or props.album_artist, 70)
        if not title and not artist:
            return "PM\n"
        return "PM\t%d\t%d\t%d\t%s\t%s\t%s\n" % (playing, pos, dur, app_name(s.source_app_user_model_id), title, artist)


# ---------- port writes from several threads, one at a time ----------
_write_lock = threading.Lock()


def send(port, data):
    with _write_lock:
        port.write(data)


# ---------- laptop sound (what plays on the speakers) ----------
# Needs numpy and PyAudioWPatch (installed by start_pc_monitor.bat). Without them everything
# else works and the stick just shows sound from its own microphone.
class Analyzer:
    """Sound frame (int16 as float32, shape (N, channels)) -> "PA ..." line for TENGEN:
    level, 32 spectrum bands, waveform and XY. Same for Windows and Linux."""
    def __init__(self, np, rate, ch, nfft):
        self.np, self.rate, self.ch, self.NFFT = np, rate, ch, nfft
        self.mono = np.zeros(nfft, dtype=np.float32)
        self.win = np.hanning(nfft).astype(np.float32)
        self.freqs = np.fft.rfftfreq(nfft, 1.0 / rate)
        edges = np.geomspace(60, min(8000, rate / 2 - 1), 33)     # same as the TENGEN mic, so the frequency labels match
        self.idx = [(max(1, int(np.searchsorted(self.freqs, edges[k]))), max(1, int(np.searchsorted(self.freqs, edges[k + 1])))) for k in range(32)]
        self.top = -40.0
        self.gain = 120.0 / 8000.0

    def frame(self, data):
        np, ch, NFFT = self.np, self.ch, self.NFFT
        m = data.mean(axis=1)
        self.mono = mono = np.concatenate((self.mono, m))[-NFFT:]
        # level
        rms = float(np.sqrt(np.mean(m * m))) / 32768.0
        level = 20 * np.log10(rms + 1e-7)
        # spectrum: 32 bands, auto gain
        spec = np.abs(np.fft.rfft(mono * self.win))
        db = np.array([20 * np.log10(spec[a:max(b, a + 1)].max() / (NFFT * 8192.0) + 1e-9) for a, b in self.idx])
        self.top = max(float(db.max()), self.top - 0.25, -60.0)
        bands = np.clip((db - (self.top - 48)) / 48.0, 0, 1)
        k = int(np.argmax(spec[2:])) + 2
        hz = int(self.freqs[k]) if level > -60 else 0
        # waveform and XY: 64 points, auto gain so the shape fills the screen
        pk = float(np.abs(data).max()) if data.size else 0.0
        target = 120.0 / max(pk * 1.05, 400.0)
        self.gain = target if target < self.gain else self.gain + (target - self.gain) * 0.2   # louder: instantly, quieter: smoothly
        gain = self.gain
        tail = mono[-512:][::8]
        wave = np.clip(tail * gain, -127, 127).astype(np.int8)
        st = data[-256:] if len(data) >= 256 else data
        stp = st[::max(1, len(st) // 64)][:64]
        if len(stp) < 64:
            stp = np.vstack([stp, np.zeros((64 - len(stp), ch), dtype=np.float32)])
        lr = stp if ch == 2 else np.repeat(stp, 2, axis=1)
        xy = np.clip(lr * gain, -127, 127).astype(np.int8)
        return "PA %d %d %s %s %s %d\n" % (int(level * 10), hz, (bands * 255).astype(np.uint8).tobytes().hex(),
                                           wave.tobytes().hex(), xy.tobytes().hex(), 1 if ch == 2 else 0)


class AudioTap:
    FPS = 30
    NFFT = 2048

    def __init__(self):
        self.port = None
        self.want_until = 0.0                   # the stick wants sound until this time
        self.err_shown = False
        self.thread = None

    def request(self):
        """The stick sent "AUD ON" (repeated every 1.5 s while the Sound section is open)."""
        first = time.time() > self.want_until
        self.want_until = time.time() + 4.0
        if first:
            say("")
            say("     [звук] TENGEN просит звук компьютера — начинаю слушать колонки")
        if self.thread is None or not self.thread.is_alive():
            self.thread = threading.Thread(target=self._run, daemon=True)
            self.thread.start()

    def stop(self):
        if time.time() < self.want_until:
            say("")
            say("     [звук] на TENGEN закрыт раздел «Звук» — перестаю слушать")
        self.want_until = 0.0

    def _run(self):
        if IS_LINUX:
            return self._run_linux()
        if IS_MAC:                                          # macOS cannot capture speaker output without an extra driver (BlackHole)
            if not self.err_shown:
                self.err_shown = True
                say("")
                say("     [звук] на macOS звук колонок недоступен — TENGEN покажет свой микрофон.")
            return
        try:
            import numpy as np
            import pyaudiowpatch as pyaudio
        except Exception as e:
            if not self.err_shown:
                self.err_shown = True
                say("")
                say("     [звук] нет библиотек для звука ноутбука (%s). Палка покажет микрофон." % e)
            return
        pa = pyaudio.PyAudio()
        try:
            while time.time() < self.want_until:
                try:
                    self._capture(np, pyaudio, pa)
                except Exception as e:
                    say("")
                    say("     [звук] ошибка: %s — пробую снова" % str(e)[:120])
                    time.sleep(1)
        finally:
            pa.terminate()

    @staticmethod
    def _loopback_device(pyaudio, pa):
        wasapi = pa.get_host_api_info_by_type(pyaudio.paWASAPI)
        spk = pa.get_device_info_by_index(wasapi["defaultOutputDevice"])
        if spk.get("isLoopbackDevice"):
            return spk
        for lb in pa.get_loopback_device_info_generator():
            if spk["name"] in lb["name"]:
                return lb
        raise RuntimeError("не нашёл «петлю» для устройства вывода %s" % spk["name"])

    def _capture(self, np, pyaudio, pa):
        dev = self._loopback_device(pyaudio, pa)
        rate = int(dev["defaultSampleRate"])
        ch = max(1, min(2, int(dev["maxInputChannels"])))
        block = rate // self.FPS
        stream = pa.open(format=pyaudio.paInt16, channels=ch, rate=rate, input=True,
                         input_device_index=dev["index"], frames_per_buffer=block)
        say("")
        say("     [звук] слушаю: %s (%d Гц, %s)" % (dev["name"][:60], rate, "стерео" if ch == 2 else "моно"))
        name = dev["name"]
        an = Analyzer(np, rate, ch, self.NFFT)
        silent_since = None
        try:
            while time.time() < self.want_until:
                # in silence Windows delivers no audio at all: do not wait, send "empty"
                avail = stream.get_read_available()
                if avail < block:
                    if silent_since is None:
                        silent_since = time.time()
                    if time.time() - silent_since > 0.15:
                        data = np.zeros((block, ch), dtype=np.float32)
                        time.sleep(1.0 / self.FPS)
                    else:
                        time.sleep(0.005)
                        continue
                else:
                    silent_since = None
                    raw = stream.read(block, exception_on_overflow=False)
                    data = np.frombuffer(raw, dtype=np.int16).astype(np.float32).reshape(-1, ch)
                if self._device_changed(pyaudio, pa, name):
                    return                                   # speakers/headphones switched: reopen
                self._emit(an.frame(data))
        finally:
            stream.stop_stream()
            stream.close()

    def _emit(self, line):
        port = self.port
        if port is not None:
            try:
                send(port, line.encode())
            except Exception:
                pass

    # Linux: speaker sound is the PulseAudio / PipeWire output "monitor", via sounddevice
    def _run_linux(self):
        try:
            import numpy as np
            import sounddevice as sd
        except Exception as e:
            if not self.err_shown:
                self.err_shown = True
                say("")
                say("     [звук] нет библиотек для звука компьютера (%s). TENGEN покажет свой микрофон." % e)
            return
        sink = (run_cmd(["pactl", "get-default-sink"]) or "").strip()
        if sink:
            os.environ["PULSE_SOURCE"] = sink + ".monitor"           # listen to what plays on the speakers
        dev = None
        try:
            for i, d in enumerate(sd.query_devices()):
                if d["name"] in ("pulse", "pipewire") and d["max_input_channels"] > 0:
                    dev = i
                    break
        except Exception:
            pass
        rate, ch = 48000, 2
        block = rate // self.FPS
        while time.time() < self.want_until:
            try:
                with sd.InputStream(device=dev, channels=ch, samplerate=rate, blocksize=block, dtype="int16") as st:
                    say("")
                    say("     [звук] слушаю: %s" % (sink + ".monitor" if sink else "вход по умолчанию"))
                    an = Analyzer(np, rate, ch, self.NFFT)
                    while time.time() < self.want_until:
                        raw, _ = st.read(block)
                        self._emit(an.frame(np.asarray(raw, dtype=np.float32).reshape(-1, ch)))
            except Exception as e:
                say("")
                say("     [звук] ошибка: %s — пробую снова" % str(e)[:120])
                time.sleep(1)

    _dev_check_t = 0.0

    def _device_changed(self, pyaudio, pa, name):
        if time.time() - self._dev_check_t < 3:
            return False
        self._dev_check_t = time.time()
        try:
            return self._loopback_device(pyaudio, pa)["name"] != name
        except Exception:
            return False


# ---------- satellite orbits for Gallery > Globe ----------
# Every 6 h fetch fresh orbital elements (TLE) from CelesTrak and send them to the stick once a minute;
# cached in a file next to the program, so it also works offline.
SATS = [("ISS", 25544), ("Tiangong", 48274), ("Hubble", 20580)]
if os.name == "nt":
    _CACHE_DIR = os.path.join(os.environ.get("APPDATA") or os.path.expanduser("~"), "TENGEN")
elif sys.platform == "darwin":
    _CACHE_DIR = os.path.join(os.path.expanduser("~"), "Library", "Application Support", "TENGEN")
else:
    _CACHE_DIR = os.path.join(os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config"), "TENGEN")
try:
    os.makedirs(_CACHE_DIR, exist_ok=True)
    TLE_CACHE = os.path.join(_CACHE_DIR, "tle_cache.txt")
except Exception:
    TLE_CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tle_cache.txt")


class Orbits:
    def __init__(self):
        self.lines = []                         # ready "TLE\t..." lines
        self._load_cache()
        threading.Thread(target=self._run, daemon=True).start()

    def _load_cache(self):
        try:
            with open(TLE_CACHE, encoding="ascii") as f:
                self.lines = [l.rstrip("\n") + "\n" for l in f if l.startswith("TLE\t")]
        except Exception:
            pass

    def _fetch(self):
        import urllib.request
        got = []
        for name, num in SATS:
            url = "https://celestrak.org/NORAD/elements/gp.php?CATNR=%d&FORMAT=TLE" % num
            req = urllib.request.Request(url, headers={"User-Agent": "tengen-pc-monitor"})
            with urllib.request.urlopen(req, timeout=15) as r:
                rows = [x.strip() for x in r.read().decode("ascii", "ignore").splitlines() if x.strip()]
            l1 = next((x for x in rows if x.startswith("1 ")), None)
            l2 = next((x for x in rows if x.startswith("2 ")), None)
            if l1 and l2:
                got.append("TLE\t%s\t%s\t%s\n" % (name, l1, l2))
        return got

    def _run(self):
        while True:
            try:
                got = self._fetch()
                if got:
                    self.lines = got
                    with open(TLE_CACHE, "w", encoding="ascii") as f:
                        f.writelines(got)
                    say("")
                    say("     [спутники] свежие орбиты: " + ", ".join(n for n, _ in SATS))
                time.sleep(6 * 3600)
            except Exception as e:
                say("")
                say("     [спутники] не скачал орбиты (%s) — беру сохранённые" % str(e)[:80])
                time.sleep(15 * 60)


def reader(port, wake, tap):
    """Separate thread: listens for commands from the stick and runs them at once."""
    while True:
        try:
            raw = port.readline()                # waits for a line (up to 1 s), then again
        except Exception:
            return                               # port closed: the thread ends
        if not raw:
            continue
        line = raw.decode("ascii", "ignore").strip()
        if line == "AUD ON":
            tap.request()
            continue
        if line == "AUD OFF":
            tap.stop()
            continue
        if line.startswith("KEY "):
            name = line[4:].strip()
            press(name)
            label = {"VOLUP": "громче", "VOLDN": "тише", "PLAY": "пауза/играть", "NEXT": "следующий трек", "PREV": "предыдущий трек", "MUTE": "без звука"}.get(name, name)
            sys.stdout.write("\r     пульт: %-14s                              " % label)
            sys.stdout.flush()
            wake.set()                           # send the new volume at once


def main():
    forced = None
    if len(sys.argv) > 2 and sys.argv[1] == "--port":
        forced = sys.argv[2]
    say("=" * 56)
    say("  TENGEN — монитор ноутбука")
    say("  Окно не закрывай: пока оно открыто, TENGEN получает данные.")
    say("  Остановить — закрыть окно (или Ctrl+C).")
    say("=" * 56)
    col = Collector()
    media = NowPlaying()
    tap = AudioTap()
    orbits = Orbits()
    last_tle = 0.0
    if IS_WIN:
        if _media_mgr_cls is not None:
            say("  Название трека: через системную панель Windows (winrt).")
        else:
            say("  Название трека: winrt не загрузился (%s)." % _media_import_err[:120])
            say("  Беру название из заголовка окна Spotify / вкладки YouTube.")
    port = None
    warned = False
    next_t = time.monotonic()
    wake = threading.Event()
    while True:
        # 1. find and open the stick
        if port is None:
            name = find_port(forced)
            if not name:
                if not warned:
                    say("")
                    say("[..] Ищу TENGEN... Подключи устройство к компьютеру USB-кабелем и включи.")
                    say("     (Ищу дальше сама — ничего нажимать не надо.)")
                    warned = True
                time.sleep(2)
                continue
            try:
                port = open_port(name)
                say("")
                say("[OK] TENGEN найден: %s. Отправляю данные. Открой на устройстве раздел ПК." % name)
                say("     Колесо громкости: в разделе PC прокрути колесом до страницы Media.")
                warned = False
                tap.port = port
                threading.Thread(target=reader, args=(port, wake, tap), daemon=True).start()
            except Exception as e:
                text = str(e).lower()
                if "denied" in text or "busy" in text or "permission" in text or "отказано" in text:
                    say("[!!] Порт %s занят другой программой." % name)
                    say("     Закрой сайт прошивки (esptool-js) и другие окна этой программы.")
                else:
                    say("[!!] Не получилось открыть %s: %s" % (name, e))
                time.sleep(3)
                continue
        # 2. send the metrics
        try:
            if wake.is_set():                    # wheel just turned: send volume at once
                wake.clear()
                time.sleep(0.05)                 # Windows needs a moment to apply it
                v = read_volume()
                if v:
                    send(port, ("PV vol=%d mute=%d\n" % (v[0], 1 if v[1] else 0)).encode())
                wake.wait(max(0.0, next_t - time.monotonic()))   # then wait until our next second
                continue
            line, cpu, ram = col.line()
            send(port, line.encode("ascii", "replace"))
            v = read_volume()
            if v:
                send(port, ("PV vol=%d mute=%d\n" % (v[0], 1 if v[1] else 0)).encode())
            if media.line:
                send(port, media.line.encode("utf-8"))
            if col.city:
                send(port, col.city.encode("ascii", "replace"))
            if orbits.lines and time.time() - last_tle > 60:       # orbits: once a minute
                last_tle = time.time()
                for l in orbits.lines:
                    send(port, l.encode("ascii", "ignore"))
            sys.stdout.write("\r     CPU %3d%%   RAM %3d%%   -> %s   " % (cpu, ram, port.port))
            sys.stdout.flush()
        except Exception:
            say("")
            say("[..] Связь с TENGEN пропала (кабель отключили?). Жду, когда вернётся...")
            tap.port = None
            try:
                port.close()
            except Exception:
                pass
            port = None
        # exactly once a second, however long the sampling takes
        next_t += 1.0
        wait = next_t - time.monotonic()
        if wait < 0:
            next_t = time.monotonic()
            wait = 0
        wake.wait(wait)                          # wake early if the wheel was turned


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        say("")
        say("Остановлено.")
