"""
Build a one-file flasher: an HTML page with the firmware (tengen_full_0x0.bin) and the esptool-js
library embedded. Everything is written except the settings area (NVS, 0x9000..0xE000), so device
settings survive. Open in Chrome/Edge, press Install, done: no addresses, no files.

    python tools/flasher/make_flasher.py <full_0x0.bin> <out.html> <version>

The page is bilingual (EN / RU): chosen by browser language, with a switch at the top.
"""
import base64, datetime, html as htmlmod, json, os, re, sys

here = os.path.dirname(os.path.abspath(__file__))
fw_path, out_path, version = sys.argv[1], sys.argv[2], sys.argv[3]

# page texts. h_* keys may contain <b>; the others are plain text for JS
TEXT = {
    "en": {
        "title": "TENGEN %(v)s — firmware installer",
        "sub": "firmware installer · version %(v)s · built %(b)s",
        "btn": "INSTALL FIRMWARE",
        "idle": "Connect TENGEN with a USB cable and press the button.",
        "h_s1": "Close <b>TENGEN Desktop</b> and any other app that uses the device's USB port.",
        "h_s2": "Press <b>Install firmware</b>, pick <b>USB JTAG/serial debug unit</b> in the list, then <b>Connect</b>.",
        "h_s3": "Wait about a minute until the progress bar is full.",
        "h_s4": "When you see <b>Done</b>, unplug the cable and plug it back in.",
        "h_note": "No addresses or files to choose: the firmware is built into this page. Your settings are kept.",
        "details": "technical log",
        "noserial": "This browser can't access USB devices. Open this file in Chrome or Edge.",
        "nopick": "No device selected. Press the button again and choose TENGEN in the list.",
        "connect": "Connecting to TENGEN…",
        "wrongchip": "this is not a TENGEN board",
        "write": "Installing firmware…",
        "done": "Done. Unplug the cable and plug it back in — TENGEN will start with the new firmware.",
        "busy": "The USB port is busy. Close TENGEN Desktop and other serial tools, then try again.",
        "fail1": "Installation failed: ",
        "fail2": ". Reconnect the cable and try again. If it keeps failing, open the technical log below.",
    },
    "ru": {
        "title": "TENGEN %(v)s — установка прошивки",
        "sub": "установка прошивки · версия %(v)s · сборка %(b)s",
        "btn": "УСТАНОВИТЬ ПРОШИВКУ",
        "idle": "Подключите TENGEN USB-кабелем и нажмите кнопку.",
        "h_s1": "Закройте <b>TENGEN Desktop</b> и другие программы, которые используют USB-порт устройства.",
        "h_s2": "Нажмите <b>«Установить прошивку»</b>, выберите в списке <b>USB JTAG/serial debug unit</b> и нажмите <b>«Подключиться»</b>.",
        "h_s3": "Подождите около минуты, пока шкала не заполнится.",
        "h_s4": "Когда появится <b>«Готово»</b>, отключите кабель и подключите снова.",
        "h_note": "Адрес и файл выбирать не нужно: прошивка уже встроена в эту страницу. Настройки сохраняются.",
        "details": "технический журнал",
        "noserial": "Этот браузер не работает с USB-устройствами. Откройте файл в Chrome или Edge.",
        "nopick": "Устройство не выбрано. Нажмите кнопку ещё раз и выберите TENGEN в списке.",
        "connect": "Подключение к TENGEN…",
        "wrongchip": "это не плата TENGEN",
        "write": "Установка прошивки…",
        "done": "Готово. Отключите кабель и подключите снова — TENGEN запустится с новой прошивкой.",
        "busy": "USB-порт занят. Закройте TENGEN Desktop и другие программы для порта, затем повторите.",
        "fail1": "Не удалось установить: ",
        "fail2": ". Переподключите кабель и попробуйте снова. Если ошибка повторяется, откройте технический журнал ниже.",
    },
}

bundle = open(os.path.join(here, "esptool-js-0.5.4.bundle.js"), encoding="utf-8").read()
# the library is an ES module ending in "export{...}": replace that with window.__esptool
# so page code in the same <script type="module"> can use it
m = re.search(r"export\{([^}]*)\};?\s*$", bundle)
if not m:
    sys.exit("esptool-js bundle: export list not found")
pairs = [p.strip().split(" as ") for p in m.group(1).split(",")]
glue = "window.__esptool={" + ",".join("%s:%s" % (b, a) for a, b in pairs) + "};"
bundle = bundle[:m.start()] + glue
lic = open(os.path.join(here, "esptool-js.LICENSE"), encoding="utf-8").read()
bundle = "/* esptool-js 0.5.4 — https://github.com/espressif/esptool-js\n" + lic.replace("*/", "* /") + "\n*/\n" + bundle

fw = open(fw_path, "rb").read()
if fw[0] != 0xE9 or fw[0x8000:0x8002] != b"\xaa\x50":
    sys.exit("not a full 0x0 image (bootloader + partition table expected)")

built = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")
page = open(os.path.join(here, "flasher_template.html"), encoding="utf-8").read()
js = {}
for lg, t in TEXT.items():                          # both languages are embedded; picked on open
    d = {k: (v % {"v": version, "b": built} if isinstance(v, str) and k in ("title", "sub") else v) for k, v in t.items()}
    js[lg] = d
# same fonts as the tablet (JetBrains Mono + Exo 2, Latin and Cyrillic), embedded so no internet is needed
fonts_css = open(os.path.join(here, "..", "webapp", "src", "fonts.css"), encoding="utf-8").read()
fonts_css = "\n".join(l for l in fonts_css.splitlines() if "font-family:'JetBrains Mono'" in l or "font-family:'Exo 2'" in l)
page = page.replace("__FONTS__", fonts_css)
page = page.replace("__VER__", htmlmod.escape(version)).replace("__JS_TEXT__", json.dumps(js, ensure_ascii=False))
if "{{" in page or "__FONTS__" in page or "__VER__" in page or "__JS_TEXT__" in page:
    sys.exit("flasher template: unfilled placeholder")
html = (page.replace("__ESPTOOL__", bundle)
            .replace("__FIRMWARE_B64__", base64.b64encode(fw).decode()))
open(out_path, "w", encoding="utf-8").write(html)
print("%s [en+ru]: %.1f MB (firmware %.2f MB)" % (out_path, len(html) / 1048576, len(fw) / 1048576))
