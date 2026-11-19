package io.github.laureladastra.mcdub3;

import android.content.Intent;
import android.net.Uri;
import android.os.Environment;
import android.os.Bundle;
import android.provider.Settings;
import android.system.Os;
import android.view.InputDevice;
import android.view.KeyEvent;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

import org.libsdl.app.SDLActivity;

/* SDL's activity, loading SDL2 and the game (libmain.so, android/app/jni). */
public class MainActivity extends SDLActivity {
    /* The game folder chosen on the start screen (LauncherActivity). */
    public static final String EXTRA_GAME_DIR = "gameDir";
    /* The on-screen gamepad: "auto", "1" always, "0" never (touch_pad.c). */
    public static final String EXTRA_TOUCH_PAD = "touchPad";
    /* Steering: "stick", "tilt" or "both"; response multiplier 0.5..2. */
    public static final String EXTRA_TOUCH_STEER = "touchSteer";
    public static final String EXTRA_TOUCH_SENS = "touchSteerSens";
    /* Dead zone percent 0..20; touch haptics "1" on, "0" off. */
    public static final String EXTRA_TOUCH_DEADZONE = "touchDeadzone";
    public static final String EXTRA_TOUCH_HAPTICS = "touchHaptics";
    public static final String EXTRA_TOUCH_INVERT = "touchTiltInvert";

    @Override
    protected void onCreate(Bundle state) {
        /* For android_host.c, before the game starts: it wins over the
         * settings file and the search. */
        String dir = getIntent() != null ? getIntent().getStringExtra(EXTRA_GAME_DIR) : null;
        String pad = getIntent() != null ? getIntent().getStringExtra(EXTRA_TOUCH_PAD) : null;
        String steer = getIntent() != null ? getIntent().getStringExtra(EXTRA_TOUCH_STEER) : null;
        String sens = getIntent() != null ? getIntent().getStringExtra(EXTRA_TOUCH_SENS) : null;
        String deadzone = getIntent() != null ? getIntent().getStringExtra(EXTRA_TOUCH_DEADZONE) : null;
        String haptics = getIntent() != null ? getIntent().getStringExtra(EXTRA_TOUCH_HAPTICS) : null;
        String invert = getIntent() != null ? getIntent().getStringExtra(EXTRA_TOUCH_INVERT) : null;
        try {
            applyHdPack();
            if (dir != null)
                Os.setenv("MC3_LAUNCHER_GAME_DIR", dir, true);
            if (pad != null)
                Os.setenv("MC3_LAUNCHER_TOUCH_PAD", pad, true);
            if (steer != null)
                Os.setenv("MC3_LAUNCHER_TOUCH_STEER", steer, true);
            if (sens != null)
                Os.setenv("MC3_LAUNCHER_TOUCH_SENS", sens, true);
            if (deadzone != null)
                Os.setenv("MC3_LAUNCHER_TOUCH_DEADZONE", deadzone, true);
            if (haptics != null)
                Os.setenv("MC3_LAUNCHER_TOUCH_HAPTICS", haptics, true);
            if (invert != null)
                Os.setenv("MC3_LAUNCHER_TOUCH_INVERT", invert, true);
        } catch (Exception e) {
            /* the native search and defaults then */
        }
        super.onCreate(state);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    /* "All files access", for a disc outside the app's folder (an SD card
     * shared with the Switch build); called from android_host.c. */
    public static boolean hasAllFilesAccess() {
        return Environment.isExternalStorageManager();
    }

    public static boolean openAllFilesAccess() {
        if (mSingleton == null)
            return false;
        try {
            Intent i = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                  Uri.parse("package:" + mSingleton.getPackageName()));
            mSingleton.startActivity(i);
        } catch (Exception e) {
            mSingleton.startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
        }
        return true;
    }

    /* A keyboard's keys go to SDL as keys. SDL takes any device that also
     * reports a d-pad for a joystick -- the emulator's keyboard and many real
     * ones do -- and its arrows then became a pad's d-pad, not the arrows
     * the keyboard layout drives with (input/sdl_keyboard.c). Gamepads and
     * the volume keys go the usual way. */
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        int code = event.getKeyCode();
        InputDevice dev = event.getDevice();
        if (dev != null && dev.getKeyboardType() == InputDevice.KEYBOARD_TYPE_ALPHABETIC
            && !dev.supportsSource(InputDevice.SOURCE_GAMEPAD)
            && !dev.supportsSource(InputDevice.SOURCE_JOYSTICK)
            && code != KeyEvent.KEYCODE_VOLUME_UP && code != KeyEvent.KEYCODE_VOLUME_DOWN
            && code != KeyEvent.KEYCODE_VOLUME_MUTE) {
            if (event.getAction() == KeyEvent.ACTION_DOWN) {
                if (event.getRepeatCount() == 0)
                    SDLActivity.onNativeKeyDown(code);
                return true;
            }
            if (event.getAction() == KeyEvent.ACTION_UP) {
                SDLActivity.onNativeKeyUp(code);
                return true;
            }
        }
        return super.dispatchKeyEvent(event);
    }

    /* The custom ReDUB logo pack (hd/textures, the same art the iOS and
     * Windows launchers ship) takes the iOS launcher's order: the user's own
     * copy beside the game first, then the set bundled in the APK
     * (ios_launcher.m). The renderer reads <dir>/textures/<hash>.png
     * (hd_pack.c); with neither found, <game folder>/../hd stays the default
     * and the title's original art shows. */
    private void applyHdPack() {
        File user = new File(Environment.getExternalStorageDirectory(),
                             getString(R.string.sd_dir) + "/hd");
        File bundled = new File(getExternalFilesDir(null), "hd");
        extractBundledHd(bundled);
        File dir = new File(user, "textures").isDirectory() ? user : bundled;
        File tex = new File(dir, "textures");
        String[] names = tex.isDirectory() ? tex.list() : null;
        android.util.Log.i("mcdub3remix", "[hd] pack " + dir + " ("
            + (names == null ? 0 : names.length) + " files)");
        try {
            Os.setenv("RECOMP_GL_HD_DIR", dir.getAbsolutePath(), true);
        } catch (Exception e) {
            /* the native default: hd beside the game folder */
        }
    }

    /* Android assets live inside the APK, where a file path cannot reach
     * them: copy the pack out once, then hd_pack.c opens it like any other
     * folder. Files already present are left alone, so a pack updated in a
     * later APK still wins after the app is reinstalled. */
    private void extractBundledHd(File dir) {
        File tex = new File(dir, "textures");
        try {
            String[] names = getAssets().list("hd/textures");
            if (names == null || names.length == 0)
                return;
            if (!tex.isDirectory() && !tex.mkdirs())
                return;
            byte[] buf = new byte[16384];
            for (String name : names) {
                File out = new File(tex, name);
                if (out.length() > 0)
                    continue;
                try (InputStream in = getAssets().open("hd/textures/" + name);
                     FileOutputStream o = new FileOutputStream(out)) {
                    int n;
                    while ((n = in.read(buf)) > 0)
                        o.write(buf, 0, n);
                }
            }
        } catch (Exception e) {
            /* no bundled pack: the user's copy or the original art */
        }
    }
}
