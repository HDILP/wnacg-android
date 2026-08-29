package com.wnacg.android;

import android.app.Activity;
import android.os.Bundle;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Spinner;
import android.widget.ArrayAdapter;
import android.widget.TextView;
import android.widget.AdapterView;
import android.widget.ImageView;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.LinearLayout;
import android.util.Log;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.text.Spannable;
import android.text.SpannableStringBuilder;
import android.text.style.ImageSpan;
import android.view.ViewGroup;

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
 * GUI shell for the native wnacg binary.
 *
 * The C binary is compiled as a shared object named libwnacg.so and packaged as a
 * native library under jniLibs/. At install time the system extracts it into the
 * app's nativeLibraryDir, which is one of the few places exec() is allowed on
 * modern Android (API 29+ forbids executing from the app's own data/files dir
 * and from assets). On API 9 the same path works fine. So a single layout covers
 * 2.3 through 16.
 *
 * The binary links BearSSL statically and does its own TLS, so it works on
 * Android 2.3 (API 9) where the system SSL is hopelessly dated.
 *
 * Two render modes:
 *  - SEARCH / DOWNLOAD commands stream into a structured view:
 *      * search/tag  -> a card per result (cover thumbnail + title + meta + a
 *                       "下载" button that fires `download <id>`).
 *      * download    -> a live horizontal progress bar + "N/M" counter, parsed
 *                       from the binary's "(N/M) 0001" progress lines.
 *  - everything else (detail / usage / errors) streams into the plain log view.
 *
 * All stdout/stderr keep streaming live (no buffering) — results appear as they
 * arrive, covers pop in as each fetch completes.
 */
public class MainActivity extends Activity {
    private static final String TAG = "wnacg";
    private static final String LIB_NAME = "wnacg";   // -> libwnacg.so
    /** Cover thumbnail target height in dp. Confirmed 500 by the user ("500够了"). */
    private static final int THUMB_H_DP = 500;
    /** Fixed card height in dp for the horizontal (left-cover / right-text) layout. */
    private static final int CARD_H_DP = 120;
    private static final Pattern COVER_LINE =
            Pattern.compile("^\\s*封面:\\s*(\\S+)\\s*$");
    private static final Pattern NUM_RUN = Pattern.compile("\\d+");
    private static final Pattern SEARCH_HEADER =
            Pattern.compile("^(搜索|标签)\\s*「(.+?)」\\s*第\\s*(\\d+)/(\\d+)\\s*页");
    private static final Pattern SEARCH_ITEM =
            Pattern.compile("^\\s*(\\d+)\\.\\s*(.*)$");
    private static final Pattern ID_LINE =
            Pattern.compile("^\\s*ID:\\s*(\\d+)(.*)$");
    private static final Pattern DOWNLOAD_LINE =
            Pattern.compile("^\\s*下载:\\s*download\\s+(\\d+)\\s*$");
    private static final Pattern DL_BEGIN =
            Pattern.compile("^开始下载\\s*漫画\\s*(\\d+):\\s*(\\d+)\\s*张");
    private static final Pattern DL_PROGRESS =
            Pattern.compile("^\\s*\\((\\d+)/(\\d+)\\)");

    private enum Mode { LOG, RESULTS }

    private TextView out;          // plain log (detail / errors / help)
    private Spinner verb;           // dropdown to pick the command verb
    private EditText cmd;           // argument box
    private ScrollView logScroll;
    private ScrollView resScroll;
    private LinearLayout results;  // structured card container
    private LinearLayout statusbar;
    private TextView statusText;
    private ProgressBar progress;

    private Mode mode = Mode.LOG;
    // Serialize native commands: one at a time, results keep their order.
    private final ExecutorService exec = Executors.newSingleThreadExecutor();
    // Cover fetches run off the command queue so they start immediately while
    // the search is still printing.
    private final ExecutorService coversExec = Executors.newFixedThreadPool(2);
    // Current search card being built from streamed lines (null outside search).
    private SearchCard curCard = null;
    // Total image count for an in-progress download (for the progress bar max).
    private int dlTotal = 0;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        cmd = (EditText) findViewById(R.id.cmd);
        out = (TextView) findViewById(R.id.output);
        logScroll = (ScrollView) findViewById(R.id.logscroll);
        resScroll = (ScrollView) findViewById(R.id.scroll);
        results = (LinearLayout) findViewById(R.id.results);
        statusbar = (LinearLayout) findViewById(R.id.statusbar);
        statusText = (TextView) findViewById(R.id.status_text);
        progress = (ProgressBar) findViewById(R.id.progress);
        Button run = (Button) findViewById(R.id.run);

        // Verb dropdown. Labels and the command tokens they emit are kept in
        // string-arrays so they stay in sync. The "其他" entry emits an empty
        // token, meaning the argument box is sent through verbatim (help, etc).
        verb = (Spinner) findViewById(R.id.verb);
        ArrayAdapter<CharSequence> verbAdapter = ArrayAdapter.createFromResource(
                this, R.array.verb_labels, android.R.layout.simple_spinner_item);
        verbAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        verb.setAdapter(verbAdapter);
        final String[] tokens = getResources().getStringArray(R.array.verb_tokens);
        final String[] hints = getResources().getStringArray(R.array.verb_hints);
        cmd.setHint(hints[0]);
        verb.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            public void onItemSelected(AdapterView<?> parent, android.view.View view,
                                       int position, long id) {
                cmd.setHint(hints[position]);
            }
            public void onNothingSelected(AdapterView<?> parent) { /* keep current hint */ }
        });

        // On first launch (Android 11+), open the All-Files-Access grant page so
        // downloads can go to /sdcard/downloads.
        out.setText("wnacg v1.8\n");  // version stamp: confirms which build is installed
        requestStorageAccess();
        showLogView();

        run.setOnClickListener(new Button.OnClickListener() {
            public void onClick(android.view.View v) {
                int pos = verb.getSelectedItemPosition();
                String token = tokens[pos];
                String arg = cmd.getText().toString().trim();
                String line;
                if (token.length() == 0) {
                    line = arg;                       // 其他: 原样执行
                } else if (arg.length() == 0) {
                    line = token;                     // 仅动词, 无参数
                } else {
                    line = token + " " + arg;
                }
                if (line.trim().length() == 0) return;
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

    // ---------------------------------------------------------------- view mux

    /** Switch to the structured (card / progress) view. */
    private void showResultsView() {
        logScroll.setVisibility(android.view.View.GONE);
        resScroll.setVisibility(android.view.View.VISIBLE);
    }

    /** Switch to the plain log view (detail / usage / errors). */
    private void showLogView() {
        resScroll.setVisibility(android.view.View.GONE);
        logScroll.setVisibility(android.view.View.VISIBLE);
        statusbar.setVisibility(android.view.View.GONE);
    }

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density + 0.5f);
    }

    // ------------------------------------------------------------- card helper

    /** One search result rendered as a rounded horizontal card:
     *  [cover | text column(title / meta / download)] with a fixed height. */
    private class SearchCard {
        final LinearLayout root;
        final ImageView cover;
        final TextView title;
        final TextView meta;
        final Button dlBtn;
        long id = -1;

        SearchCard() {
            float density = getResources().getDisplayMetrics().density;
            final int cardH = (int) (CARD_H_DP * density + 0.5f);

            root = new LinearLayout(MainActivity.this);
            root.setOrientation(LinearLayout.HORIZONTAL);
            root.setGravity(android.view.Gravity.CENTER_VERTICAL);
            root.setPadding(dp(8), dp(6), dp(8), dp(6));
            root.setBackgroundDrawable(cardBg());
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.FILL_PARENT, cardH);
            lp.setMargins(0, 0, 0, dp(6));
            root.setLayoutParams(lp);

            // cover: fixed-height, fixed-width (derived from a 3:4 cover) so the
            // row stays a uniform width; scale to fill (CENTER_CROP).
            cover = new ImageView(MainActivity.this);
            cover.setScaleType(ImageView.ScaleType.CENTER_CROP);
            cover.setVisibility(android.view.View.GONE);
            final int coverW = (int) (cardH * 0.72f + 0.5f); // ~3:4 aspect
            LinearLayout.LayoutParams clp = new LinearLayout.LayoutParams(coverW, cardH);
            clp.setMargins(0, 0, dp(8), 0);
            cover.setLayoutParams(clp);
            root.addView(cover);

            // text column takes the rest of the width.
            LinearLayout textCol = new LinearLayout(MainActivity.this);
            textCol.setOrientation(LinearLayout.VERTICAL);
            textCol.setGravity(android.view.Gravity.CENTER_VERTICAL);
            LinearLayout.LayoutParams tlp = new LinearLayout.LayoutParams(
                    0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            textCol.setLayoutParams(tlp);

            title = new TextView(MainActivity.this);
            title.setTextSize(14);
            title.setTextColor(Color.parseColor("#303030"));
            title.setMaxLines(2);
            title.setEllipsize(android.text.TextUtils.TruncateAt.END);
            textCol.addView(title);

            meta = new TextView(MainActivity.this);
            meta.setTextSize(11);
            meta.setTextColor(Color.parseColor("#777777"));
            meta.setMaxLines(2);
            meta.setEllipsize(android.text.TextUtils.TruncateAt.END);
            meta.setPadding(0, dp(2), 0, 0);
            textCol.addView(meta);

            dlBtn = new Button(MainActivity.this);
            dlBtn.setText("下载");
            dlBtn.setTextSize(12);
            LinearLayout.LayoutParams blp = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            blp.setMargins(0, dp(4), 0, 0);
            dlBtn.setLayoutParams(blp);
            dlBtn.setOnClickListener(new android.view.View.OnClickListener() {
                public void onClick(android.view.View v) {
                    if (id > 0) execNative("download " + id);
                }
            });
            textCol.addView(dlBtn);

            root.addView(textCol);
        }

        /** Place a decoded cover bitmap sized to the fixed cover slot. */
        void setCover(Bitmap bmp) {
            if (bmp == null) return;
            float density = getResources().getDisplayMetrics().density;
            int cardH = (int) (CARD_H_DP * density + 0.5f);
            int coverW = (int) (cardH * 0.72f + 0.5f);
            Bitmap small;
            try {
                small = Bitmap.createScaledBitmap(bmp, coverW, cardH, true);
            } catch (OutOfMemoryError e) {
                meta.setText(meta.getText() + "  (封面过大)");
                return;
            }
            cover.setImageBitmap(small);
            cover.setVisibility(android.view.View.VISIBLE);
        }

        void setCoverFallback(String text) {
            meta.setText(meta.getText() + "  " + text);
        }
    }

    private GradientDrawable cardBg() {
        GradientDrawable d = new GradientDrawable();
        d.setColor(Color.parseColor("#FFF8FA"));
        d.setCornerRadius(dp(8));
        d.setStroke(1, Color.parseColor("#FFD6DE"));
        return d;
    }

    private void addInfoLine(final String text) {
        runOnUiThread(new Runnable() {
            public void run() {
                if (mode != Mode.RESULTS) return;
                TextView t = new TextView(MainActivity.this);
                t.setTextSize(13);
                t.setTypeface(android.graphics.Typeface.MONOSPACE);
                t.setText(text);
                results.addView(t);
            }
        });
    }

    // ----------------------------------------------------------- command driver

    private String binaryPath() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.JELLY_BEAN) {
            return extractLegacyBinary();
        }
        File lib = new File(getApplicationInfo().nativeLibraryDir, "lib" + LIB_NAME + ".so");
        return lib.getAbsolutePath();
    }

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

    private void requestStorageAccess() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return; // API 29 and below: not needed
        if (Environment.isExternalStorageManager()) return;       // already granted
        appendLog("正在打开「所有文件访问」授权页…\n");
        appendLog("(该开关在应用信息页里, 不在「权限管理」列表; 若打开的是应用属性页, 请往下滑找「所有文件访问权限」并开启)\n");
        appendLog("(ColorOS/OPPO 等系统可能不显示该开关: 去 设置→搜索「所有文件访问」→ 点进 wnacg 开启)\n");
        try {
            Intent intent = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
            intent.setData(Uri.parse("package:" + getPackageName()));
            startActivity(intent);
        } catch (Exception e1) {
            try {
                Intent intent = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS);
                intent.setData(Uri.parse("package:" + getPackageName()));
                startActivity(intent);
            } catch (Exception e2) {
                appendLog("无法自动打开授权页: " + e1.getMessage() + "\n可手动去 设置→应用→wnacg→所有文件访问权限 开启\n");
            }
        }
    }

    private static final String FIXED_BASE_DIR = "/sdcard/downloads";
    private String defaultDownloadDir() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R &&
            !Environment.isExternalStorageManager()) {
            File ext = getExternalFilesDir(null);
            if (ext != null) return ext.getAbsolutePath();
            return getFilesDir().getAbsolutePath();
        }
        return FIXED_BASE_DIR;
    }

    private void execNative(final String args) {
        // Pick the render mode from the command, on the UI thread.
        final String[] tok = args.trim().split("\\s+");
        final String verb = (tok.length >= 1) ? tok[0] : "";
        if (verb.equals("search") || verb.equals("tag")) {
            runOnUiThread(new Runnable() {
                public void run() {
                    mode = Mode.RESULTS;
                    curCard = null;
                    results.removeAllViews();
                    statusbar.setVisibility(android.view.View.GONE);
                    showResultsView();
                }
            });
        } else if (verb.equals("download")) {
            runOnUiThread(new Runnable() {
                public void run() {
                    mode = Mode.RESULTS;
                    statusbar.setVisibility(android.view.View.VISIBLE);
                    progress.setProgress(0);
                    statusText.setText("准备下载…");
                    showResultsView();
                }
            });
        } else {
            runOnUiThread(new Runnable() {
                public void run() {
                    mode = Mode.LOG;
                    showLogView();
                }
            });
        }

        exec.execute(new Runnable() {
            public void run() {
                String bin = binaryPath();
                String cmdArgs = args;
                if (tok.length >= 1 && tok[0].equals("download") && tok.length == 2) {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R &&
                        !Environment.isExternalStorageManager()) {
                        appendLog("未授予「所有文件访问」权限, 正在打开授权页…\n");
                        appendLog("请在该页面找到「所有文件访问权限」(或「允许管理所有文件」)并开启, 返回后重跑 download\n");
                        requestStorageAccess();
                    }
                    cmdArgs = args + " " + defaultDownloadDir();
                }
                try {
                    String[] argv = (bin + " " + cmdArgs).split(" ");
                    Process p = Runtime.getRuntime().exec(argv);
                    new ReaderThread(p.getInputStream(), true).start();
                    new ReaderThread(p.getErrorStream(), true).start();
                    int rc = p.waitFor();
                    if (mode == Mode.RESULTS && verb.equals("download")) {
                        setStatus("下载结束 [进程退出码: " + rc + "]");
                    } else if (mode == Mode.LOG) {
                        appendLog("\n[进程退出码: " + rc + "]\n");
                    } else {
                        addInfoLine("[进程退出码: " + rc + "]");
                    }
                } catch (IOException e) {
                    if (mode == Mode.LOG) appendLog("执行失败: " + e.getMessage() + "\n");
                    else setStatus("执行失败: " + e.getMessage());
                } catch (InterruptedException e) {
                    if (mode == Mode.LOG) appendLog("被中断\n");
                    else setStatus("被中断");
                }
            }
        });
    }

    // ------------------------------------------------------------- line routing

    /** Parse one streamed stdout/stderr line into the active view. */
    private void handleLine(final String line) {
        if (mode == Mode.LOG) {
            appendLog(line + "\n");
            return;
        }
        // RESULTS mode: search cards or download progress.
        // --- download progress ---
        Matcher db = DL_BEGIN.matcher(line);
        if (db.find()) {
            dlTotal = Integer.parseInt(db.group(2));
            runOnUiThread(new Runnable() {
                public void run() {
                    statusbar.setVisibility(android.view.View.VISIBLE);
                    progress.setMax(Math.max(1, dlTotal));
                    progress.setProgress(0);
                    statusText.setText(line.trim());
                }
            });
            return;
        }
        Matcher dp = DL_PROGRESS.matcher(line);
        if (dp.find()) {
            final int cur = Integer.parseInt(dp.group(1));
            final int tot = Integer.parseInt(dp.group(2));
            runOnUiThread(new Runnable() {
                public void run() {
                    statusbar.setVisibility(android.view.View.VISIBLE);
                    progress.setMax(Math.max(1, tot));
                    progress.setProgress(cur);
                    statusText.setText("下载中 " + cur + "/" + tot);
                }
            });
            return;
        }
        if (line.startsWith("完成:") || line.matches(".*成功\\s*\\d+.*失败\\s*\\d+.*")) {
            setStatus(line.trim());
            runOnUiThread(new Runnable() {
                public void run() { progress.setProgress(progress.getMax()); }
            });
            return;
        }
        // --- search cards ---
        Matcher sh = SEARCH_HEADER.matcher(line);
        if (sh.find()) {
            curCard = null;
            addInfoLine(line.trim());
            return;
        }
        Matcher si = SEARCH_ITEM.matcher(line);
        if (si.find()) {
            final String titleText = si.group(2).trim();
            curCard = new SearchCard();
            final SearchCard card = curCard;
            // A card's views MUST be created/added/touched on the UI thread.
            // handleLine runs on the reader (worker) thread, so post both the
            // title write and the addView to the UI thread; FIFO ordering in the
            // UI queue guarantees addView happens before the later meta write.
            runOnUiThread(new Runnable() {
                public void run() {
                    card.title.setText(titleText);
                    results.addView(card.root);
                }
            });
            return;
        }
        if (curCard != null) {
            Matcher idm = ID_LINE.matcher(line);
            if (idm.find()) {
                try { curCard.id = Long.parseLong(idm.group(1)); } catch (NumberFormatException e) {}
                final String extra = idm.group(2).trim();
                if (extra.length() > 0) {
                    final SearchCard card = curCard;
                    runOnUiThread(new Runnable() {
                        public void run() { card.meta.setText(extra); }
                    });
                }
                return;
            }
            Matcher cm = COVER_LINE.matcher(line.trim());
            if (cm.matches()) {
                final String url = cm.group(1);
                // Covers are now re-encoded to BMP by the native binary (WebP
                // decoded server-side), so even API 9 (no WebP decoder in
                // BitmapFactory) can display them. No SDK_INT guard needed.
                if (url.startsWith("http")) {
                    final SearchCard card = curCard;
                    coversExec.execute(new Runnable() {
                        public void run() { fetchCoverInto(card, url); }
                    });
                }
                return;
            }
            Matcher dm = DOWNLOAD_LINE.matcher(line.trim());
            if (dm.matches()) {
                // id already set from the ID line; nothing else to do.
                return;
            }
        }
        // Anything else in RESULTS mode (notes, errors) -> info line.
        // Swallow the horizontal separator line so it doesn't clutter the cards.
        String t = line.trim();
        if (!t.isEmpty() && !t.matches("^[─\\-—]+$")) addInfoLine(t);
    }

    /** Download a cover through the native binary and place it in the card. */
    private void fetchCoverInto(final SearchCard card, String url) {
        final File f = fetchCover(url);
        if (f == null) {
            runOnUiThread(new Runnable() { public void run() { card.setCoverFallback("(封面获取失败)"); } });
            return;
        }
        final Bitmap bmp = decodeScaled(f);
        if (bmp == null) {
            runOnUiThread(new Runnable() { public void run() { card.setCoverFallback("(封面解码失败)"); } });
            return;
        }
        runOnUiThread(new Runnable() {
            public void run() { card.setCover(bmp); }
        });
    }

    /** Download one cover through the native binary (same TLS/IPv4 fixes),
     *  cached under cacheDir/covers/<id>.<ext>. */
    private File fetchCover(String url) {
        long id = 0;
        Matcher nm = NUM_RUN.matcher(url);
        while (nm.find()) {
            long v = 0;
            try { v = Long.parseLong(nm.group()); } catch (NumberFormatException e) { continue; }
            if (v > id) id = v;
        }
        File dir = new File(getCacheDir(), "covers");
        if (!dir.exists()) dir.mkdirs();
        String ext = ".img";
        int dot = url.lastIndexOf('.');
        if (dot >= 0 && url.length() - dot <= 6) {
            String e = url.substring(dot);
            if (!e.contains("/")) ext = e;
        }
        // WebP covers are re-encoded to PNG by the native binary (BitmapFactory
        // on API 9 has no WebP decoder AND no BMP decoder, but PNG decodes fine),
        // so name them .png to keep the cache honest. The Java side never reads
        // the extension for decoding — BitmapFactory sniffs the magic bytes — but
        // matching the real format avoids confusion.
        if (ext.equalsIgnoreCase(".webp")) ext = ".png";
        File target = new File(dir, id + ext);
        if (target.exists()) return target;
        try {
            String bin = binaryPath();
            Process p = Runtime.getRuntime().exec(new String[]{
                    bin, "cover", String.valueOf(id), url, target.getAbsolutePath()});
            new ReaderThread(p.getInputStream(), false).start();
            new ReaderThread(p.getErrorStream(), false).start();
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

    private void setStatus(final String s) {
        runOnUiThread(new Runnable() {
            public void run() {
                statusbar.setVisibility(android.view.View.VISIBLE);
                statusText.setText(s);
            }
        });
    }

    private void appendLog(final String s) {
        runOnUiThread(new Runnable() {
            public void run() { out.append(s); }
        });
    }

    private class ReaderThread extends Thread {
        final InputStream is;
        final boolean live;
        ReaderThread(InputStream is, boolean live) {
            this.is = is;
            this.live = live;
        }
        public void run() {
            try {
                BufferedReader br = new BufferedReader(new InputStreamReader(is, "UTF-8"));
                String l;
                while ((l = br.readLine()) != null) {
                    if (live) handleLine(l);
                }
                br.close();
            } catch (IOException e) {
                // swallow; process is going away
            }
        }
    }
}
