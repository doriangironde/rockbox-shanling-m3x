package org.rockbox.m3x.launcher;

import android.app.Activity;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;

/** Android entry point for the separately installed native M3X player. */
public final class LaunchActivity extends Activity {
    private TextView message;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        int padding = (int) (32 * getResources().getDisplayMetrics().density);
        layout.setPadding(padding, padding, padding, padding);
        message = new TextView(this);
        message.setTextSize(24);
        message.setGravity(Gravity.CENTER);
        message.setText("Starting Rockbox…\n\nPlease wait for the player to open.");
        layout.addView(message);
        setContentView(layout);
        // A recreated activity must not request another root session.
        if (state == null) new Thread(new Runnable() {
            @Override public void run() { launch(); }
        }, "Rockbox launch").start();
    }

    private void launch() {
        try {
            Process root = new ProcessBuilder("su", "-c",
                "/system/bin/sh /data/adb/modules/rockbox_m3x/launch.sh")
                .redirectErrorStream(true).start();
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            InputStream stream = root.getInputStream();
            byte[] buffer = new byte[1024];
            int count;
            while ((count = stream.read(buffer)) != -1) bytes.write(buffer, 0, count);
            stream.close();
            if (root.waitFor() != 0) {
                String reason = bytes.toString("UTF-8").trim();
                showError(reason.isEmpty() ? "Root access was not granted." : reason);
            }
        } catch (Exception error) {
            showError("Could not start Rockbox. Check that Magisk root access is available.");
        }
    }

    private void showError(final String reason) {
        runOnUiThread(new Runnable() {
            @Override public void run() {
                message.setText("Rockbox could not start\n\n" + reason);
                Button close = new Button(LaunchActivity.this);
                close.setText("Back to Android");
                close.setOnClickListener(new View.OnClickListener() {
                    @Override public void onClick(View view) { finish(); }
                });
                ((LinearLayout) message.getParent()).addView(close);
            }
        });
    }
}
