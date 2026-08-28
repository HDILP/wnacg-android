package com.wnacg.android;

import android.app.Activity;
import android.os.Bundle;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;

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
 */
public class MainActivity extends Activity {
    private static final String TAG = "wnacg";
    private static final String LIB_NAME = "wnacg";   // -> libwnacg.so
    private TextView out;
    private EditText cmd;

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
        requestStorageAccess();

        run.setOnClickListener(new Button.OnClickListener() {
            public void onClick(android.view.View v) {
                String line = cmd.getText().toString().trim();
                if (line.length() == 0) return;
                execNative(line);
            }
        });
    }

    /** Resolve the on-disk path of libwnacg.so.
     *  The binary is packaged as a native library (jniLibs), so the system
     *  installs it into nativeLibraryDir — the only path where exec() is allowed
     *  on modern Android (API 29+ blocks exec from data/files and assets). On
     *  Gingerbread (API 9) this same path is also exec-allowed. */
    private String binaryPath() {
        File lib = new File(getApplicationInfo().nativeLibraryDir, "lib" + LIB_NAME + ".so");
        return lib.getAbsolutePath();
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

    private void execNative(String args) {
        String bin = binaryPath();
        // Auto-append a default download dir for `download <id>` so the user
        // never has to remember a path. Only when no extra arg is present.
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
            args = args + " " + defaultDownloadDir();
        }
        try {
            String[] argv = (bin + " " + args).split(" ");
            Process p = Runtime.getRuntime().exec(argv);
            // stream stdout + stderr concurrently so we never deadlock
            new ReaderThread(p.getInputStream()).start();
            new ReaderThread(p.getErrorStream()).start();
            int rc = p.waitFor();
            append("\n[进程退出码: " + rc + "]\n");
        } catch (IOException e) {
            append("执行失败: " + e.getMessage() + "\n");
        } catch (InterruptedException e) {
            append("被中断\n");
        }
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
        ReaderThread(InputStream is) { this.is = is; }
        public void run() {
            try {
                BufferedReader br = new BufferedReader(new InputStreamReader(is, "UTF-8"));
                String l;
                while ((l = br.readLine()) != null) append(l + "\n");
                br.close();
            } catch (IOException e) {
                // swallow; process is going away
            }
        }
    }
}
