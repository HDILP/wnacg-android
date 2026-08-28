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

    /** On Android 11+ (API 30+) writing to /sdcard/wnacg needs the
     *  MANAGE_EXTERNAL_STORAGE (All Files Access) permission. We open the system
     *  settings page and let the user grant it; if that specific page isn't
     *  exposed by the ROM (some Android 16 builds), we fall back to the app's
     *  general settings page. If even that fails, we just keep using the
     *  app-private dir — downloads still work, just not on /sdcard. */
    private void requestStorageAccess() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return; // API 29 and below: not needed
        if (Environment.isExternalStorageManager()) return;       // already granted
        append("需要「所有文件访问」权限才能下载到 /sdcard/wnacg\n正在打开授权页…\n");
        try {
            Intent intent = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
            intent.setData(Uri.parse("package:" + getPackageName()));
            startActivity(intent);
        } catch (Exception e1) {
            // ROM didn't expose that exact page — try the general app settings.
            try {
                Intent intent = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS);
                intent.setData(Uri.parse("package:" + getPackageName()));
                startActivity(intent);
            } catch (Exception e2) {
                append("无法自动打开授权页: " + e1.getMessage() + "\n(可手动在系统设置里授予存储权限，或不授权改用 app 私有目录)\n");
            }
        }
    }

    /** Pick where `download <id>` should save when the user gave no path.
     *  Android 11+ with All-Files access: use /sdcard/wnacg (user wants this).
     *  Otherwise: app-private external dir (no permission needed, always works). */
    private String defaultDownloadDir() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R &&
            Environment.isExternalStorageManager()) {
            File root = Environment.getExternalStorageDirectory();
            return new File(root, "wnacg").getAbsolutePath();
        }
        File ext = getExternalFilesDir(null);
        if (ext != null) return new File(ext, "wnacg").getAbsolutePath();
        return new File(getFilesDir(), "wnacg").getAbsolutePath();
    }

    private void execNative(String args) {
        String bin = binaryPath();
        // Auto-append a default download dir for `download <id>` so the user
        // never has to remember a path. Only when no extra arg is present.
        String[] tok = args.trim().split("\\s+");
        if (tok.length >= 1 && tok[0].equals("download") && tok.length == 2) {
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
