package com.wnacg.android;

import android.app.Activity;
import android.os.Bundle;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;
import android.util.Log;

import java.io.BufferedReader;
import java.io.DataOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.FileOutputStream;

/**
 * Minimal shell for the native wnacg binary.
 *
 * The C binary is bundled in assets/ as "wnacg". On first launch we extract it
 * to the app's private data dir, chmod 0700, then drive it with Runtime.exec.
 * We deliberately avoid any JNI / NDK glue and avoid the platform TLS stack:
 * the binary links BearSSL statically and does its own TLS, so it works on
 * Android 2.3 (API 9) where the system SSL is hopelessly dated.
 *
 * Certificate validation is OFF in the binary by default (the site's CA chain
 * is not in the 2010 trust store); this is a pragmatic trade-off for a download
 * tool, documented in the README.
 */
public class MainActivity extends Activity {
    private static final String TAG = "wnacg";
    private static final String ASSET = "wnacg";
    private TextView out;
    private EditText cmd;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        cmd = (EditText) findViewById(R.id.cmd);
        out = (TextView) findViewById(R.id.output);
        Button run = (Button) findViewById(R.id.run);

        ensureBinary();

        run.setOnClickListener(new Button.OnClickListener() {
            public void onClick(android.view.View v) {
                String line = cmd.getText().toString().trim();
                if (line.length() == 0) return;
                execNative(line);
            }
        });
    }

    /** Extract the native binary from assets into our private dir. */
    private void ensureBinary() {
        File bin = new File(getFilesDir(), ASSET);
        if (bin.exists() && bin.length() > 0) {
            // already extracted; just make sure perms are right
            bin.setExecutable(true, false);
            return;
        }
        try {
            InputStream in = getAssets().open(ASSET);
            FileOutputStream fos = new FileOutputStream(bin);
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) fos.write(buf, 0, n);
            in.close();
            fos.close();
            // 0700: owner read/write/exec only
            Runtime.getRuntime().exec("chmod 700 " + bin.getAbsolutePath()).waitFor();
            Log.i(TAG, "extracted native binary -> " + bin.getAbsolutePath());
        } catch (IOException e) {
            Log.e(TAG, "failed to extract native binary", e);
            append("提取原生二进制失败: " + e.getMessage() + "\n");
        } catch (InterruptedException e) {
            Log.e(TAG, "interrupted during chmod", e);
        }
    }

    private void execNative(String args) {
        File bin = new File(getFilesDir(), ASSET);
        try {
            String[] argv = (bin.getAbsolutePath() + " " + args).split(" ");
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
