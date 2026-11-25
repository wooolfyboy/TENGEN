package dev.tengen.app;

import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import com.getcapacitor.JSObject;
import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.PluginMethod;
import com.getcapacitor.annotation.CapacitorPlugin;
import android.view.View;
import android.view.WindowManager;
import android.widget.Toast;
import com.getcapacitor.BridgeActivity;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.zip.GZIPInputStream;
import org.json.JSONObject;

// TENGEN tablet app: wraps the same page as /full on the stick. Adds only what a page cannot do:
//  - keep the screen on, fullscreen without system bars;
//  - update the page FROM THE STICK: the firmware always holds the latest page; the app compares
//    versions (/api/pagever) and, if the stick is newer, fetches /full over the stick's Wi-Fi
//    (no internet needed), verifies it and switches to it. A new APK is only needed for shell changes.
public class MainActivity extends BridgeActivity {
    private static final String STICK = "http://192.168.4.1";
    private static final String CAP_PREFS = "CapWebViewSettings", CAP_PATH = "serverBasePath";   // this is how Capacitor itself reads them
    private final Handler ui = new Handler(Looper.getMainLooper());
    private volatile boolean busy = false;
    // update state; the page shows it in the header (window.TengenNative.status())
    static volatile String status = "start";
    static MainActivity self;
    // plugin for the page: Capacitor.Plugins.TengenUpdate.status() / .check()
    @CapacitorPlugin(name = "TengenUpdate")
    public static class UpdatePlugin extends Plugin {
        @PluginMethod public void status(PluginCall call) { JSObject r = new JSObject(); r.put("status", status); call.resolve(r); }
        @PluginMethod public void check(PluginCall call) { if (self != null) self.ui.post(self::checkStickPage); call.resolve(); }
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        forgetOldDownloadAfterApkUpdate();
        self = this;
        registerPlugin(UpdatePlugin.class);
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        ui.postDelayed(checker, 3000);
    }

    @Override
    public void onResume() {
        super.onResume();
        ui.postDelayed(this::checkStickPage, 1500);       // back in the app (the stick may have been reflashed): check at once
    }

    @Override
    public void onDestroy() {
        ui.removeCallbacksAndMessages(null);
        if (self == this) self = null;
        super.onDestroy();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
        }
    }

    // check every 30 s: cheap (short JSON), download only if the version differs
    private final Runnable checker = new Runnable() {
        @Override public void run() {
            checkStickPage();
            ui.postDelayed(this, 30000);
        }
    };

    // a new APK brings its own page: forget the downloaded one (the APK may be newer than the stick)
    private void forgetOldDownloadAfterApkUpdate() {
        try {
            PackageInfo pi = getPackageManager().getPackageInfo(getPackageName(), 0);
            String stamp = pi.lastUpdateTime + "";
            SharedPreferences my = getSharedPreferences("tengen", MODE_PRIVATE);
            if (!stamp.equals(my.getString("apkStamp", ""))) {
                getSharedPreferences(CAP_PREFS, MODE_PRIVATE).edit().remove(CAP_PATH).apply();
                my.edit().putString("apkStamp", stamp).apply();
            }
        } catch (Exception ignored) { }
    }

    private File pageDir() { return new File(getFilesDir(), "stickpage"); }

    // which page is active: downloaded from the stick or bundled in the APK
    private String currentSha() {
        try {
            String path = getSharedPreferences(CAP_PREFS, MODE_PRIVATE).getString(CAP_PATH, null);
            if (path != null && new File(path, "pagever.txt").exists()) return new String(read(new java.io.FileInputStream(new File(path, "pagever.txt"))), StandardCharsets.UTF_8).trim();
            return new String(read(getAssets().open("public/pagever.txt")), StandardCharsets.UTF_8).trim();
        } catch (Exception e) { return ""; }
    }

    private void checkStickPage() {
        if (busy) return;
        busy = true;
        new Thread(() -> {
            try {
                status = "check";
                JSONObject j = new JSONObject(new String(get(STICK + "/api/pagever", 2500), StandardCharsets.UTF_8));
                String sha = j.getString("sha"), cur = currentSha();
                if (sha.isEmpty()) { status = "stick: no version"; return; }
                if (sha.equals(cur)) { status = "same " + sha.substring(0, 6); return; }
                status = "download " + sha.substring(0, 6);
                byte[] html = get(STICK + "/full", 15000);
                String got = sha256(html);
                if (!sha.equals(got)) { status = "bad hash " + got.substring(0, 6) + " (" + html.length + " B)"; return; }
                File dir = pageDir();
                dir.mkdirs();
                write(new File(dir, "index.html"), html);
                write(new File(dir, "pagever.txt"), sha.getBytes(StandardCharsets.UTF_8));
                String path = dir.getAbsolutePath();
                getSharedPreferences(CAP_PREFS, MODE_PRIVATE).edit().putString(CAP_PATH, path).apply();
                status = "updated " + sha.substring(0, 6);
                ui.post(() -> {
                    boolean ru = java.util.Locale.getDefault().getLanguage().startsWith("ru");
                    Toast.makeText(this, ru ? "TENGEN: страница обновлена с палки" : "TENGEN: page updated from device", Toast.LENGTH_SHORT).show();
                    getBridge().setServerBasePath(path);
                });
            } catch (Exception e) {
                status = "no stick: " + e.getClass().getSimpleName() + (e.getMessage() != null ? " " + e.getMessage() : "");
            } finally { busy = false; }
        }).start();
    }

    private static byte[] get(String url, int timeoutMs) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(timeoutMs);
        c.setReadTimeout(timeoutMs);
        c.setUseCaches(false);
        try {
            if (c.getResponseCode() != 200) throw new Exception("http " + c.getResponseCode());
            byte[] b = read(c.getInputStream());
            if (b.length > 2 && (b[0] & 0xff) == 0x1f && (b[1] & 0xff) == 0x8b) b = read(new GZIPInputStream(new java.io.ByteArrayInputStream(b)));   // the stick sends gzip
            return b;
        } finally { c.disconnect(); }
    }

    private static byte[] read(InputStream in) throws Exception {
        try (InputStream s = in) {
            ByteArrayOutputStream o = new ByteArrayOutputStream();
            byte[] buf = new byte[16384];
            int n;
            while ((n = s.read(buf)) != -1) { if (n > 0) o.write(buf, 0, n); }
            return o.toByteArray();
        }
    }

    private static void write(File f, byte[] data) throws Exception {
        File tmp = new File(f.getPath() + ".tmp");
        try (FileOutputStream o = new FileOutputStream(tmp)) { o.write(data); }
        if (!tmp.renameTo(f)) { f.delete(); if (!tmp.renameTo(f)) throw new Exception("rename"); }
    }

    private static String sha256(byte[] data) throws Exception {
        byte[] d = MessageDigest.getInstance("SHA-256").digest(data);
        StringBuilder sb = new StringBuilder();
        for (byte x : d) sb.append(String.format("%02x", x));
        return sb.toString();
    }
}
