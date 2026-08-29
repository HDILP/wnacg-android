package com.wnacg.android;

import android.app.Activity;
import android.os.Bundle;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;
import android.util.Log;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.text.Spannable;
import android.text.SpannableStringBuilder;
import android.text.style.ImageSpan;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import android.content.pm.PackageManager;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;

/**
 * Minimal shell for the native wnacg binary.
 *
 * The C binary is compiled as a shared object named libwnacg.so and packaged as a
 * native library under jniLibs/. At install time the system extracts it into the
 * app's nativeLibraryDir, which is one of the few places exec() is allowed on
 * modern Android (API 29+ forbids executing from the app's own data/files dir
 * and from assets). On API 9 the same path works fine. So a single layout covers
 * 2.3 through 16.
 *
 * The binary links BearSSL statically and does its own TLS, so it works on
 * Android 2.3 (API 9) where the system SSL is hopelessly dated. Certificate
 * validation is OFF in the binary by default (the site's CA chain is not in the
 * 2010 trust store); pragmatic trade-off for a download tool, documented in the
 * README.
 *
 * Search output is streamed to the TextView live; each "封面: <url>" line the C
 * binary prints becomes a placeholder that is swapped for an inline thumbnail as
 * soon as it is downloaded (through the SAME native binary so the TLS/IPv4 fixes
 * apply, cached in the app cache dir). Everything else streams through
 * verbatim, so download progress stays live too.
 */
public class MainActivity extends Activity {
    private static final String TAG = "wnacg";
    private static final String LIB_NAME = "wnacg";   // -> libwnacg.so
    /** Unique placeholder swapped for a cover thumbnail when it arrives.
     *  Each cover line gets a DISTINCT token (▣N▣, N = global sequence), so
     *  replacements always match the exact slot — earlier failures or out-of-
     *  order completions can never shift a later image to the wrong row. */
    private static final char COVER_TOKEN = '\u25A3'; // ▣
    private static final String TOKEN_FMT = "\u25A3%d\u25A3"; // ▣N▣
    /** Cover thumbnail target height in dp. */
    private static final int THUMB_H_DP = 500;
    private static final Pattern COVER_LINE =
            Pattern.compile("^\\s*封面:\\s*(\\S+)\\s*$");
    private static final Pattern NUM_RUN = Pattern.compile("\\d+");

    private TextView out;
    private EditText cmd;
    // Serialize native commands: one at a time, results keep their order.
    private final ExecutorService exec = Executors.newSingleThreadExecutor();
    // Cover fetches run off the command queue so they start immediately while
    // the search is still printing. Token index is global (never reset), so a
    // failed cover can't shift later replacements.
    private final ExecutorService coversExec = Executors.newFixedThreadPool(2);
    private int coverSeq = 0;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        cmd = (EditText) findViewById(R.id.cmd);
        out = (TextView) findViewById(R.id.output);
        Button run = (Button) findViewById(R.id.run);

        // On first launch (Android 11+), open the All-Files-Access grant page so
        // downloads can go to /sdcard/downloads. The toggle lives on the app-info
        // page, not the empty "权限管理" list — we explain that in the output box.
        out.setText("wnacg v1.5\n");  // version stamp: confirms which build is installed
        requestStorageAccess();

        run.setOnClickListener(new Button.OnClickListener() {
            public void onClick(android.view.View v) {
                String line = cmd.getText().toString().trim();
                if (line.length() == 0) return;
                execNative(line);
            }
        });
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        exec.shutdownNow();
        coversExec.shutdownNow();
    }

    /** Resolve the on-disk binary path.
     *  API 16+ : PIE executable shipped as libwnacg.so in nativeLibraryDir —
     *            the only exec-allowed path on Android 10+, and the 4.1 (API
     *            16) linker is the first that supports PIE at all.
     *  API 9–15: Gingerbread's linker cannot load PIE binaries (exit code 11 /
     *            SIGSEGV), but old systems have no exec-path restriction, so
     *            we run the classic non-PIE binary shipped in assets/,
     *            extracted to filesDir. */
    private String binaryPath() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.JELLY_BEAN) {
            return extractLegacyBinary();
        }
        File lib = new File(getApplicationInfo().nativeLibraryDir, "lib" + LIB_NAME + ".so");
        return lib.getAbsolutePath();
    }

    /** Copy assets/wnacg-legacy (non-PIE, for API 9–15) into filesDir and make
     *  it executable. Gingerbread's linker can't run the PIE libwnacg.so, but
     *  pre-API-16 Android allows exec from the app-private dir. Returns the
     *  executable path, falling back to the PIE path if extraction failed. */
    private String extractLegacyBinary() {
        File exe = new File(getFilesDir(), "wnacg-legacy");
        if (!exe.exists()) {
            try {
                InputStream in = getAssets().open("wnacg-legacy");
                java.io.FileOutputStream fos = new java.io.FileOutputStream(exe);
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) > 0) fos.write(buf, 0, n);
                fos.close();
                in.close();
            } catch (IOException e) {
                Log.w(TAG, "legacy binary extract failed: " + e.getMessage());
            }
        }
        if (exe.exists()) {
            exe.setExecutable(true, false);
            return exe.getAbsolutePath();
        }
        return new File(getApplicationInfo().nativeLibraryDir,
                        "lib" + LIB_NAME + ".so").getAbsolutePath();
    }

    /** Open the system "All Files Access" grant page so the user can enable
     *  MANAGE_EXTERNAL_STORAGE. On Android 11+ this is a special-app permission;
     *  ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION jumps straight to the per-app
     *  "所有文件访问权限" toggle (it lives on the app-info page, NOT in the
     *  runtime "权限管理" list — that list only shows normal permissions, which
     *  is why it looks empty). If that dedicated page isn't exposed we fall back
     *  to the general app-info page; the toggle is still there, just scroll down. */
    private void requestStorageAccess() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return; // API 29 and below: not needed
        if (Environment.isExternalStorageManager()) return;       // already granted
        append("正在打开「所有文件访问」授权页…\n");
        append("(该开关在应用信息页里, 不在「权限管理」列表; 若打开的是应用属性页, 请往下滑找「所有文件访问权限」并开启)\n");
        append("(ColorOS/OPPO 等系统可能不显示该开关: 去 设置→搜索「所有文件访问」→ 点进 wnacg 开启)\n");
        try {
            Intent intent = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
            intent.setData(Uri.parse("package:" + getPackageName()));
            startActivity(intent);
        } catch (Exception e1) {
            // dedicated page not exposed — fall back to app-info page
            try {
                Intent intent = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS);
                intent.setData(Uri.parse("package:" + getPackageName()));
                startActivity(intent);
            } catch (Exception e2) {
                append("无法自动打开授权页: " + e1.getMessage() + "\n可手动去 设置→应用→wnacg→所有文件访问权限 开启\n");
            }
        }
    }

    /** Pick where `download <id>` should save when the user gave no path.
     *  The native binary itself appends /<id> to whatever base dir we pass, so we
     *  just return the BASE dir here.
     *  Fixed path /sdcard/downloads on every Android version (API 9 included:
     *  WRITE_EXTERNAL_STORAGE is install-time there, no prompt). On Android 11+
     *  this needs the All-Files access grant; if that isn't granted we fall back
     *  to the app-private dir so the download still succeeds. */
    private static final String FIXED_BASE_DIR = "/sdcard/downloads";
    private String defaultDownloadDir() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R &&
            !Environment.isExternalStorageManager()) {
            // Android 11+ without the All-Files grant: can't write /sdcard/downloads.
            File ext = getExternalFilesDir(null);
            if (ext != null) return ext.getAbsolutePath();
            return getFilesDir().getAbsolutePath();
        }
        return FIXED_BASE_DIR;
    }

    private void execNative(final String args) {
        exec.execute(new Runnable() {
            public void run() {
                String bin = binaryPath();
                // Auto-append a default download dir for `download <id>` so the user
                // never has to remember a path. Only when no extra arg is present.
                String cmdArgs = args;
                String[] tok = args.trim().split("\\s+");
                if (tok.length >= 1 && tok[0].equals("download") && tok.length == 2) {
                    // On Android 11+, if All-Files access isn't granted yet, open the
                    // system grant page (the real "所有文件访问" toggle) BEFORE running,
                    // so the user can enable it; this run still falls back to the
                    // app-private dir, the next run lands in /sdcard/downloads.
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R &&
                        !Environment.isExternalStorageManager()) {
                        append("未授予「所有文件访问」权限, 正在打开授权页…\n");
                        append("请在该页面找到「所有文件访问权限」(或「允许管理所有文件」)并开启, 返回后重跑 download\n");
                        requestStorageAccess();
                    }
                    cmdArgs = args + " " + defaultDownloadDir();
                }
                try {
                    String[] argv = (bin + " " + cmdArgs).split(" ");
                    Process p = Runtime.getRuntime().exec(argv);
                    // stream stdout + stderr concurrently so we never deadlock;
                    // stdout may contain cover lines (intercepted), stderr is
                    // shown verbatim (errors must never be swallowed)
                    new ReaderThread(p.getInputStream(), true, true).start();
                    new ReaderThread(p.getErrorStream(), true, false).start();
                    int rc = p.waitFor();
                    append("\n[进程退出码: " + rc + "]\n");
                } catch (IOException e) {
                    append("执行失败: " + e.getMessage() + "\n");
                } catch (InterruptedException e) {
                    append("被中断\n");
                }
            }
        });
    }

    /** Handles one output line. Returns true if the line was a "封面:" line
     *  (a placeholder + async cover fetch was queued). Called from the native
     *  command's reader thread, so appends keep their display order.
     *  API < 14 (Android 2.3/3.x): BitmapFactory has NO WebP support (added in
     *  API 14), so inline covers are impossible — leave the line as text so at
     *  least the URL is visible and no "解码失败" spam appears. */
    private boolean handleStreamLine(final String line) {
        if (Build.VERSION.SDK_INT < 14) return false; // stream the line verbatim
        Matcher m = COVER_LINE.matcher(line.trim());
        if (!m.matches()) return false;
        final String url = m.group(1);
        if (!url.startsWith("http")) return false;
        final int idx = coverSeq++;
        final String token = String.format(TOKEN_FMT, idx); // ▣N▣
        append(token + "\n");
        coversExec.execute(new Runnable() {
            public void run() {
                final File f = fetchCover(url);
                if (f == null) {
                    replaceTokenText(idx, "(封面获取失败)");
                    return;
                }
                final Bitmap bmp = decodeScaled(f);
                if (bmp == null) {
                    replaceTokenText(idx, "(封面解码失败)");
                    return;
                }
                runOnUiThread(new Runnable() {
                    public void run() {
                        final float density = getResources().getDisplayMetrics().density;
                        // Target height THUMB_H_DP, width follows aspect — BUT
                        // clamp to the TextView's usable width so the image can
                        // never overflow its line and overlap nearby text.
                        int availW = out.getWidth() - out.getPaddingLeft() - out.getPaddingRight();
                        int h = (int) (THUMB_H_DP * density + 0.5f);
                        int w = Math.max(1, bmp.getWidth() * h / bmp.getHeight());
                        if (availW > 0 && w > availW) {
                            w = availW;
                            h = Math.max(1, w * bmp.getHeight() / bmp.getWidth());
                        }
                        applyCoverAt(String.format(TOKEN_FMT, idx), bmp, w, h);
                    }
                });
            }
        });
        return true;
    }

    /** Download one cover through the native binary (same TLS/IPv4 fixes),
     *  cached under cacheDir/covers/<id>.<ext>. */
    private File fetchCover(String url) {
        long id = 0;
        // comic id = longest numeric run in the URL (e.g. /data/371091/...)
        Matcher nm = NUM_RUN.matcher(url);
        while (nm.find()) {
            long v = 0;
            try { v = Long.parseLong(nm.group()); } catch (NumberFormatException e) { continue; }
            if (v > id) id = v;
        }
        File dir = new File(getCacheDir(), "covers");
        if (!dir.exists()) dir.mkdirs();
        // keep the URL's extension so the decoder can sniff the format
        String ext = ".img";
        int dot = url.lastIndexOf('.');
        if (dot >= 0 && url.length() - dot <= 6) {
            String e = url.substring(dot);
            if (!e.contains("/")) ext = e;
        }
        File target = new File(dir, id + ext);
        if (target.exists()) return target;
        try {
            String bin = binaryPath();
            Process p = Runtime.getRuntime().exec(new String[]{
                    bin, "cover", String.valueOf(id), url, target.getAbsolutePath()});
            new ReaderThread(p.getInputStream(), false, false).start();
            new ReaderThread(p.getErrorStream(), false, false).start();
            int r = p.waitFor();
            return (r == 0 && target.exists()) ? target : null;
        } catch (IOException e) {
            Log.w(TAG, "cover fetch failed: " + e.getMessage());
            return null;
        } catch (InterruptedException e) {
            return null;
        }
    }

    /** Decode a cover file, downsampled so a full-res image never hits memory. */
    private Bitmap decodeScaled(File f) {
        BitmapFactory.Options opts = new BitmapFactory.Options();
        opts.inJustDecodeBounds = true;
        BitmapFactory.decodeFile(f.getAbsolutePath(), opts);
        float density = getResources().getDisplayMetrics().density;
        int targetH = (int) (THUMB_H_DP * density + 0.5f);
        int sample = 1;
        while (opts.outHeight / (sample * 2) >= targetH) sample *= 2;
        opts.inJustDecodeBounds = false;
        opts.inSampleSize = sample;
        try {
            return BitmapFactory.decodeFile(f.getAbsolutePath(), opts);
        } catch (OutOfMemoryError e) {
            Log.w(TAG, "cover decode OOM");
            return null;
        }
    }

    /** Replace the token ▣N▣ (exact unique match) with an image. Using the
     *  unique token string means completion order and earlier failures can
     *  never map an image onto the wrong row. Runs on the UI thread. */
    private void applyCoverAt(final String token, final Bitmap bmp,
                              final int w, final int h) {
        CharSequence cur = out.getText();
        if (!(cur instanceof SpannableStringBuilder)) return;
        SpannableStringBuilder ss = (SpannableStringBuilder) cur;
        int pos = ss.toString().indexOf(token);
        if (pos < 0) return; // already replaced or cleared
        final Bitmap small;
        try {
            small = Bitmap.createScaledBitmap(bmp, w, h, true);
        } catch (OutOfMemoryError e) {
            Log.w(TAG, "cover scale OOM");
            ss.replace(pos, pos + token.length(), "(封面过大)");
            return;
        }
        // swap the token for a wide space carrying the image
        ss.replace(pos, pos + token.length(), " ");
        ss.setSpan(new ImageSpan(small), pos, pos + 1, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
        // Without this the TextView keeps its stale layout and the image
        // renders on top of the following text (results "叠在一起") until
        // the next full redraw (app switch). Force re-measure + repaint.
        out.invalidate();
        out.requestLayout();
    }

    /** Replace the token ▣N▣ with a fallback text (fetch/decode failure). */
    private void replaceTokenText(final int idx, final String text) {
        final String token = String.format(TOKEN_FMT, idx);
        runOnUiThread(new Runnable() {
            public void run() {
                CharSequence cur = out.getText();
                if (!(cur instanceof SpannableStringBuilder)) return;
                SpannableStringBuilder ss = (SpannableStringBuilder) cur;
                int pos = ss.toString().indexOf(token);
                if (pos < 0) return;
                ss.replace(pos, pos + token.length(), text);
                out.invalidate();
                out.requestLayout();
            }
        });
    }

    private void append(final String s) {
        runOnUiThread(new Runnable() {
            public void run() {
                out.append(s);
            }
        });
    }

    private class ReaderThread extends Thread {
        final InputStream is;
        final boolean appendLive;
        final boolean interceptCovers;
        ReaderThread(InputStream is, boolean appendLive, boolean interceptCovers) {
            this.is = is;
            this.appendLive = appendLive;
            this.interceptCovers = interceptCovers;
        }
        public void run() {
            try {
                BufferedReader br = new BufferedReader(new InputStreamReader(is, "UTF-8"));
                String l;
                while ((l = br.readLine()) != null) {
                    if (appendLive && interceptCovers && handleStreamLine(l)) continue;
                    if (appendLive) append(l + "\n");
                }
                br.close();
            } catch (IOException e) {
                // swallow; process is going away
            }
        }
    }
}
