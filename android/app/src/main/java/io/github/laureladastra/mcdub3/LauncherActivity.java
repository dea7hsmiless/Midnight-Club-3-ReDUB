package io.github.laureladastra.mcdub3;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.os.StatFs;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.JavascriptInterface;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;

/* The start screen, laid out as the iOS launcher (src/ios_launcher.m): a rail
 * of pages down the left (Game, Install, Settings, Support, About), the page
 * itself in a scrolling pane, and a status bar along the bottom. Same art
 * (launcher/art), same palette and the same row metrics, so the three builds
 * look like one product.
 *
 * Install takes an Xbox disc image (an ISO/XISO) or an already-extracted
 * folder holding default.xbe. An ISO is read and extracted on the device into
 * the app's own folder, then verified, as the iOS launcher does; extraction
 * writes a staging folder and swaps it into place only once default.xbe is
 * there, so an interrupted import never damages a working install.
 *
 * Settings are written to <app folder>/mcdub3remix_env.txt, the file the game
 * reads at boot (android_host.c, windows_port_settings_attach), exactly as the
 * iOS launcher writes Documents/mcdub3remix_env.txt. */
public class LauncherActivity extends Activity {
    private static final int PICK_FOLDER = 1, PICK_ISO = 2, PICK_BACKUP_OUT = 3, PICK_BACKUP_IN = 4;

    private static final int PAGE_GAME = 0, PAGE_ONLINE = 1, PAGE_INSTALL = 2,
                             PAGE_SETTINGS = 3, PAGE_SUPPORT = 4, PAGE_ABOUT = 5;
    private static final String[] PAGES = { "Game", "Online", "Install", "Settings", "Support", "About" };

    private static final String PREFS = "launcher", FOLDERS = "folders", LAST = "last",
                                REMOVED = "removed";

    /* ios_launcher.m: RGB(0x080D1A) behind, the rail at 0x050912, the status
     * bar at 0x0E1627, text 0xE6EBF5, muted 0x99A5BC, cards 0x0F172A and the
     * accent 0xEE3E50. */
    private static final int BG = 0xFF080D1A, RAIL = 0xFF050912, FOOTER = 0xFF0E1627,
                             CARD = 0xFF0F172A, TEXT = 0xFFE6EBF5, MUTED = 0xFF99A5BC,
                             ACCENT = 0xFFEE3E50, LINE = 0xFF303D57;

    /* The settings file: one KEY=VALUE per line, comments and unknown keys
     * kept, so the launcher and Options > Port Options share one value. */
    private static final String ENV = "mcdub3remix_env.txt";

    /* One row of the Settings page, in the iOS launcher's shape: category,
     * name, env key, choices, default and the labels the row shows. */
    private static final class Row {
        final String cat, name, key, dflt, hint;
        final String[] values, labels;
        Row(String cat, String name, String key, String[] values, String[] labels,
            String dflt, String hint) {
            this.cat = cat; this.name = name; this.key = key;
            this.values = values; this.labels = labels;
            this.dflt = dflt; this.hint = hint;
        }
    }

    private SharedPreferences prefs;
    private LinearLayout rail, pane, page;
    private TextView footer, status;
    private int current = PAGE_GAME;
    private final Map<String, String> settings = new HashMap<>();
    private WebView onlineView;
    private final android.os.Handler onlineTicker =
        new android.os.Handler(android.os.Looper.getMainLooper());
    private final Map<String, TextView> values = new HashMap<>();
    private final List<Row> rows = new ArrayList<>();
    /* The backup/restore progress line, updated from the copy thread. */
    private TextView backupStatus;
    /* The chosen disc image and the import's progress (Install page). */
    private Uri isoUri;
    private String isoName;
    private String importText = "";
    private volatile boolean busy;
    private long extracted;
    private int shownMib = -1;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        rows.clear();
        rows.addAll(rows());
        loadSettings();
        String oldPreset=settings.get("MC3_GRAPHICS_PRESET");
        if("0".equals(oldPreset))write("MC3_GRAPHICS_PRESET","custom");
        else if("1".equals(oldPreset))write("MC3_GRAPHICS_PRESET","performance");
        else if("2".equals(oldPreset))write("MC3_GRAPHICS_PRESET","default");
        else if("3".equals(oldPreset))write("MC3_GRAPHICS_PRESET","quality");

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(BG);

        LinearLayout body = new LinearLayout(this);
        body.setOrientation(LinearLayout.HORIZONTAL);
        root.addView(body, new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        rail = new LinearLayout(this);
        rail.setOrientation(LinearLayout.VERTICAL);
        rail.setBackgroundColor(RAIL);
        rail.setPadding(dp(9), dp(16), dp(9), dp(8));
        body.addView(rail, new LinearLayout.LayoutParams(railWidth(),
            ViewGroup.LayoutParams.MATCH_PARENT));

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(BG);
        scroll.setFillViewport(true);
        pane = new LinearLayout(this);
        pane.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(pane);
        body.addView(scroll, new LinearLayout.LayoutParams(0,
            ViewGroup.LayoutParams.MATCH_PARENT, 1f));

        footer = label("", 11, false, MUTED);
        footer.setBackgroundColor(FOOTER);
        footer.setGravity(Gravity.CENTER_VERTICAL);
        footer.setPadding(dp(20), 0, dp(20), 0);
        root.addView(footer, new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, dp(25)));

        setContentView(root);
        render();
    }

    @Override
    protected void onResume() {
        super.onResume();
        render();                   /* back from the permission page or the picker */
    }

    private int dp(float v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    private int railWidth() {
        float w = getResources().getDisplayMetrics().widthPixels
                / getResources().getDisplayMetrics().density;
        return dp(w < 800 ? 126 : 160);
    }

    /* ── Widgets in the iOS launcher's shapes ────────────────────────── */

    private TextView label(String s, float size, boolean bold, int color) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(size);
        t.setTextColor(color);
        if (bold)
            t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    private GradientDrawable box(int fill, boolean bordered) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(fill);
        g.setCornerRadius(dp(9));
        if (bordered)
            g.setStroke(dp(1), LINE);
        return g;
    }

    /* A button: 9pt corners, bold 14, white; the accent fill means primary. */
    private TextView button(String title, boolean primary, View.OnClickListener click) {
        TextView b = label(title, 14, true, 0xFFFFFFFF);
        b.setGravity(Gravity.CENTER);
        b.setBackground(box(primary ? ACCENT : CARD, !primary));
        b.setPadding(dp(10), 0, dp(10), 0);
        b.setOnClickListener(click);
        return b;
    }

    private View spacer(int h) {
        View v = new View(this);
        v.setLayoutParams(new LinearLayout.LayoutParams(1, dp(h)));
        return v;
    }

    /* ── The shell: rail, page, status bar ───────────────────────────── */

    private void render() {
        rail.removeAllViews();
        pane.removeAllViews();

        /* The rail's mark: the project logo, as the iOS launcher's sidebar
         * shows; text only if the asset is missing (user request 2026-11-18:
         * replace the plain text there). */
        android.graphics.Bitmap markArt = bitmap("launcher/logo.png");
        View mark;
        if (markArt != null) {
            ImageView logo = new ImageView(this);
            logo.setImageBitmap(markArt);
            logo.setScaleType(ImageView.ScaleType.FIT_START);
            logo.setPadding(dp(7), 0, 0, 0);
            mark = logo;
        } else {
            TextView name = label("ReDUB", 26, true, TEXT);
            name.setPadding(dp(7), 0, 0, 0);
            mark = name;
        }
        rail.addView(mark, new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, dp(42)));

        for (int i = 0; i < PAGES.length; i++) {
            final int page = i;
            TextView b = button(PAGES[i], current == i, v -> {
                if (busy)
                    return;
                if (current != page) {
                    current = page;
                    render();
                }
            });
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(41));
            lp.topMargin = dp(i == 0 ? 16 : 8);
            if (i >= PAGE_SUPPORT)
                lp.topMargin = dp(i == PAGE_SUPPORT ? 0 : 8);
            rail.addView(b, lp);
            if (i == PAGE_SETTINGS)
                rail.addView(new View(this), new LinearLayout.LayoutParams(
                    1, 0, 1f));      /* Support and About sit at the bottom */
        }

        switch (current) {
        case PAGE_GAME:      gamePage(); break;
        case PAGE_ONLINE:    onlinePage(); break;
        case PAGE_INSTALL:   installPage(); break;
        case PAGE_SETTINGS:  settingsPage(); break;
        case PAGE_SUPPORT:   textPage("Support",
                                 "Choose the folder holding your extracted disc on Install. Pair a "
                                 + "controller in Android's Bluetooth settings for rumble and to hide "
                                 + "the on-screen pad. Settings apply on the next game launch.\n\n"
                                 + "ReDUB never changes the disc folder: saves and settings live in the "
                                 + "app's own folder."); break;
        default:             textPage("About ReDUB",
                                 "Midnight Club 3: DUB Edition Remix, statically recompiled for "
                                 + "Android.\n\nYour own disc is required: the app reads the extracted "
                                 + "folder that holds default.xbe. The runtime is bundled with the app; "
                                 + "no native code is compiled here.\n\n" + versionLine()); break;
        }

        if(current==PAGE_SUPPORT)page.addView(button("Share diagnostics",false,v->shareDiagnostics()));
        footer.setText(statusText());
    }

    /* The app's own version, read from the package so it can never disagree
     * with the APK: versionName is ANDROID_VERSION, and the versionCode is the
     * same number the release uses. Android's cadence differs from the game's,
     * so this is the number to quote in a bug report. */
    private String versionLine() {
        try {
            android.content.pm.PackageInfo pi = getPackageManager()
                .getPackageInfo(getPackageName(), 0);
            return "ReDUB version " + pi.versionName + " (build " + pi.versionCode + ")";
        } catch (Exception e) {
            return "ReDUB version unknown";
        }
    }

    private void shareDiagnostics() {
        try {
            File log=new File(envFile().getParentFile(),"mcdub3remix_log.txt");
            String text="ReDUB Android diagnostics\n";
            if(log.isFile())try(java.io.RandomAccessFile f=new java.io.RandomAccessFile(log,"r")){
                f.seek(Math.max(0,f.length()-100000));byte[] bytes=new byte[(int)(f.length()-f.getFilePointer())];f.readFully(bytes);text+=new String(bytes,StandardCharsets.UTF_8);
            }else text+="No game log yet. Launch the game first.";
            Intent share=new Intent(Intent.ACTION_SEND);share.setType("text/plain");share.putExtra(Intent.EXTRA_TEXT,text);startActivity(Intent.createChooser(share,"Share diagnostics"));
        }catch(Exception e){new android.app.AlertDialog.Builder(this).setMessage("Could not read diagnostics: "+e.getMessage()).setPositiveButton("OK",null).show();}
    }

    private void textPage(String title, String body) {
        page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(dp(22), dp(20), dp(22), dp(24));
        pane.addView(page);

        page.addView(label(title, 27, true, TEXT));
        TextView b = label(body, 15, false, MUTED);
        b.setLineSpacing(dp(3), 1f);
        b.setPadding(0, dp(24), 0, 0);
        page.addView(b);
    }

    /* The Game page: the hero art with the logo over it, then the badge,
     * the title and the three buttons (ios_launcher.m render). */
    private void gamePage() {
        page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        pane.addView(page);

        int heroH = Math.max(dp(150), Math.min(dp(280),
            getResources().getDisplayMetrics().heightPixels - dp(220)));
        android.widget.FrameLayout hero = new android.widget.FrameLayout(this);
        hero.setLayoutParams(new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, heroH));

        ImageView art = new ImageView(this);
        art.setScaleType(ImageView.ScaleType.CENTER_CROP);
        art.setImageBitmap(bitmap("launcher/hero.jpg"));
        hero.addView(art, new android.widget.FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        View wash = new View(this);
        wash.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,
            new int[] { 0x1F080D1A, BG }));
        hero.addView(wash, new android.widget.FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        android.graphics.Bitmap logoArt = bitmap("launcher/logo.png");
        if (logoArt != null) {
            ImageView logo = new ImageView(this);
            logo.setImageBitmap(logoArt);
            logo.setScaleType(ImageView.ScaleType.FIT_CENTER);
            int w = getResources().getDisplayMetrics().widthPixels;
            android.widget.FrameLayout.LayoutParams lp =
                new android.widget.FrameLayout.LayoutParams(
                    Math.min(dp(300), Math.round(w * 0.62f)), heroH - dp(34));
            lp.leftMargin = dp(24);
            lp.topMargin = dp(20);
            lp.gravity = Gravity.START | Gravity.TOP;
            hero.addView(logo, lp);
        } else {
            TextView t = label("Midnight Club 3: ReDUB", 26, true, TEXT);
            t.setGravity(Gravity.CENTER);
            hero.addView(t);
        }
        page.addView(hero);

        LinearLayout text = new LinearLayout(this);
        text.setOrientation(LinearLayout.VERTICAL);
        text.setPadding(dp(22), dp(8), dp(22), 0);
        page.addView(text);

        boolean ready = selectedFolder() != null;
        text.addView(label(ready ? "INSTALLED  /  ANDROID" : "BRING YOUR OWN XBOX DISC",
                           11, true, MUTED));
        text.addView(label("Midnight Club 3: ReDUB", 24, true, TEXT));
        text.addView(label("DUB Edition Remix, rebuilt for Android.", 13, false, MUTED));

        LinearLayout actions = new LinearLayout(this);
        actions.setOrientation(LinearLayout.HORIZONTAL);
        actions.setPadding(dp(22), dp(16), dp(22), 0);
        page.addView(actions);

        TextView play = button(ready ? "Play" : "Install", true, v -> play());
        actions.addView(play, new LinearLayout.LayoutParams(0, dp(48), 0.40f));

        TextView verify = button("Verify setup", false, v -> verify());
        LinearLayout.LayoutParams vp = new LinearLayout.LayoutParams(0, dp(48), 0.27f);
        vp.leftMargin = dp(10);
        actions.addView(verify, vp);

        TextView files = button("Game files", false, v -> { });
        LinearLayout.LayoutParams fp = new LinearLayout.LayoutParams(0, dp(48), 0.27f);
        fp.leftMargin = dp(10);
        actions.addView(files, fp);
        files.setOnClickListener(v -> {
            current = PAGE_INSTALL;
            render();
        });
    }

    /* Install takes an Xbox disc image (read and extracted here, as iOS does)
     * or a folder that already holds default.xbe. */
    private void installPage() {
        page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(dp(22), dp(20), dp(22), dp(24));
        pane.addView(page);

        page.addView(label("Install ReDUB", 27, true, TEXT));
        TextView help = label("Select your original Xbox DUB Edition Remix ISO, or the folder that "
            + "already holds default.xbe. PS2 and Switch images are not supported.", 13, false, MUTED);
        help.setPadding(0, dp(8), 0, 0);
        page.addView(help);

        String chosen = selectedFolder();
        TextView name = label(isoName != null ? isoName
            : chosen != null ? chosen : "Choose your game source", 19, true, TEXT);
        name.setPadding(0, dp(20), 0, 0);
        page.addView(name);

        LinearLayout actions = new LinearLayout(this);
        actions.setOrientation(LinearLayout.HORIZONTAL);
        actions.setPadding(0, dp(18), 0, 0);
        page.addView(actions);

        TextView choose = button("Choose Xbox ISO", false, v -> pickIso());
        choose.setEnabled(!busy);
        actions.addView(choose, new LinearLayout.LayoutParams(dp(200), dp(46)));

        TextView install = button(busy ? "Installing…" : isoUri != null ? "Install game"
            : "Browse folder", true, v -> {
            if (busy)
                return;
            if (isoUri != null)
                importIso();
            else
                pick();
        });
        install.setEnabled(!busy);
        LinearLayout.LayoutParams ap = new LinearLayout.LayoutParams(0, dp(46), 1f);
        ap.leftMargin = dp(12);
        actions.addView(install, ap);

        TextView progressText = label(importText.isEmpty() ? statusText() : importText,
                                      12, false, MUTED);
        progressText.setPadding(0, dp(18), 0, 0);
        page.addView(progressText);

        TextView note = label("Installation extracts and verifies your disc on this device. The "
            + "runtime is in the app; no code is compiled here. Allow at least 5 GB free for the "
            + "extracted files, and keep the app open during installation.", 12, false, MUTED);
        note.setPadding(0, dp(14), 0, 0);
        page.addView(note);

        TextView head2 = label("Back up before you uninstall", 18, true, TEXT);
        head2.setPadding(0, dp(24), 0, dp(4));
        page.addView(head2);
        TextView backupHelp = label("Uninstalling deletes this app's own folder, and that is where "
            + "the disc and the career in save/ live. An update that needs an uninstall (0.2.6 does) "
            + "should be preceded by a backup to a folder you choose, and followed by a restore.",
            12, false, MUTED);
        backupHelp.setPadding(0, dp(6), 0, 0);
        page.addView(backupHelp);

        LinearLayout backupRow = new LinearLayout(this);
        backupRow.setOrientation(LinearLayout.HORIZONTAL);
        backupRow.setPadding(0, dp(12), 0, 0);
        page.addView(backupRow);

        TextView backup = button("Back up game folder", false, v -> pickBackupOut());
        backup.setEnabled(!busy);
        backupRow.addView(backup, new LinearLayout.LayoutParams(dp(230), dp(44)));

        TextView restore = button("Restore from backup", false, v -> pickBackupIn());
        restore.setEnabled(!busy);
        LinearLayout.LayoutParams rlp = new LinearLayout.LayoutParams(0, dp(44), 1f);
        rlp.leftMargin = dp(12);
        backupRow.addView(restore, rlp);

        backupStatus = label("", 12, false, MUTED);
        backupStatus.setPadding(0, dp(10), 0, 0);
        page.addView(backupStatus);

        List<String> found = folders();
        if (found.isEmpty())
            return;

        TextView head = label("Game folders", 18, true, TEXT);
        head.setPadding(0, dp(22), 0, dp(4));
        page.addView(head);

        RadioGroup list = new RadioGroup(this);
        for (String f : found) {
            boolean ok = isGame(new File(f));
            RadioButton rb = new RadioButton(this);
            rb.setId(1000 + found.indexOf(f));
            rb.setTag(f);
            rb.setText(f + "\n" + (ok ? "default.xbe found" : "no default.xbe here"));
            rb.setTextColor(ok ? TEXT : MUTED);
            rb.setTextSize(13);
            list.addView(rb);
            if (f.equals(chosen))
                list.check(rb.getId());
        }
        list.setOnCheckedChangeListener((g, id) -> {
            View v = g.findViewById(id);
            if (v != null && v.getTag() != null)
                prefs.edit().putString(LAST, (String) v.getTag()).apply();
        });
        page.addView(list);

        TextView remove = button("Remove this folder", false, v -> remove());
        LinearLayout.LayoutParams rp = new LinearLayout.LayoutParams(dp(220), dp(42));
        rp.topMargin = dp(8);
        page.addView(remove, rp);
    }

    /* ── Importing a disc image ──────────────────────────────────────── */

    private void pickIso() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(i, PICK_ISO);
    }

    private String displayName(Uri uri) {
        try (android.database.Cursor c = getContentResolver().query(uri,
                new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst())
                return c.getString(0);
        } catch (Exception e) {
            /* the path name then */
        }
        return uri.getLastPathSegment();
    }

    /* Extraction writes game-iso.importing beside the install, checks that
     * default.xbe arrived, and only then replaces game-iso: an interrupted or
     * failed import leaves a working install untouched (as iOS does). */
    private void importIso() {
        final Uri uri = isoUri;
        if (uri == null || busy)
            return;
        final File home = getExternalFilesDir(null);
        final File target = new File(home, "game-iso");
        final File staging = new File(home, "game-iso.importing");
        busy = true;
        extracted = 0;
        shownMib = -1;
        importText = "Reading the disc…";
        render();

        new Thread(() -> {
            String error = null;
            try {
                // A failed rollback may have left the only intact saves here.
                File recovery = new File(home, "game-iso.previous");
                if (!target.exists() && recovery.exists() && !recovery.renameTo(target))
                    throw new java.io.IOException("could not restore the previous install; it was left in " + recovery);
                ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(uri, "r");
                if (pfd == null)
                    throw new java.io.IOException("the disc image could not be opened");
                /* AutoCloseInputStream owns the descriptor for the stream's
                 * whole life. A bare FileInputStream over getFileDescriptor()
                 * leaves the ParcelFileDescriptor unreferenced, so a GC during
                 * the minutes-long extraction could finalise it and close the
                 * file descriptor under the reader -- "Bad file descriptor"
                 * (BlueStacks, 2026-11-18). */
                try (ParcelFileDescriptor.AutoCloseInputStream in =
                         new ParcelFileDescriptor.AutoCloseInputStream(pfd)) {
                    Xiso disc = new Xiso(in.getChannel());
                    long total = measure(disc, "");
                    StatFs stat = new StatFs(home.getAbsolutePath());
                    if (stat.getAvailableBytes() < total + (256L << 20))
                        throw new java.io.IOException("not enough free space: about "
                            + (total >> 20) + " MB is needed");
                    deleteTree(staging);
                    if (!staging.mkdirs())
                        throw new java.io.IOException("could not write " + staging);
                    extract(disc, "", staging, total);
                }
                File xbe = new File(staging, "default.xbe");
                if (!xbe.isFile() || xbe.length() == 0)
                    throw new java.io.IOException("the image has no default.xbe");
                DiscSaveData.preserve(target, staging);
                /* Back the old install up, then replace: a rename failure
                 * must not lose a working install (or its saves). */
                File previous = new File(home, "game-iso.previous");
                deleteTree(previous);
                if (target.isDirectory() && !target.renameTo(previous))
                    throw new java.io.IOException("could not back up the existing install");
                if (!staging.renameTo(target)) {
                    if (previous.isDirectory())
                        previous.renameTo(target);      /* put the old one back */
                    throw new java.io.IOException("could not move the disc into place");
                }
                deleteTree(previous);
            } catch (Exception e) {
                error = e.getMessage() == null ? e.toString() : e.getMessage();
                android.util.Log.e("mcdub3remix", "disc import failed", e);
                deleteTree(staging);
            }
            final String failed = error;
            runOnUiThread(() -> {
                busy = false;
                if (failed == null) {
                    List<String> fs = folders();
                    if (!fs.contains(target.getAbsolutePath()))
                        fs.add(target.getAbsolutePath());
                    save(fs);
                    prefs.edit().putString(LAST, target.getAbsolutePath()).apply();
                    importText = "Installed  /  default.xbe verified";
                    isoUri = null;
                    isoName = null;
                } else {
                    importText = "";
                    new android.app.AlertDialog.Builder(this)
                        .setTitle("Import failed")
                        .setMessage(failed)
                        .setPositiveButton("OK", null).show();
                }
                render();
            });
        }, "disc-import").start();
    }

    private long measure(Xiso disc, String path) throws java.io.IOException {
        long total = 0;
        for (Xiso.Entry e : disc.list(path))
            total += e.isDir()
                ? measure(disc, path.isEmpty() ? e.name : path + "/" + e.name)
                : e.size;
        return total;
    }

    private void extract(Xiso disc, String path, File dir, long total)
            throws java.io.IOException {
        for (Xiso.Entry e : disc.list(path)) {
            File out = new File(dir, e.name);
            if (e.isDir()) {
                if (!out.isDirectory() && !out.mkdirs())
                    throw new java.io.IOException("could not write " + out);
                extract(disc, path.isEmpty() ? e.name : path + "/" + e.name, out, total);
            } else {
                try (java.io.FileOutputStream o = new java.io.FileOutputStream(out)) {
                    disc.copy(e, o);
                }
                extracted += e.size;
            }
            int mib = (int) (extracted >> 20);
            if (mib != shownMib && mib % 64 == 0) {
                shownMib = mib;
                final String text = "Extracting… " + mib + " of " + (total >> 20) + " MB";
                runOnUiThread(() -> {
                    importText = text;
                    if (current == PAGE_INSTALL)
                        render();
                });
            }
        }
    }

    private static void deleteTree(File f) {
        if (f == null || !f.exists())
            return;
        File[] kids = f.listFiles();
        if (kids != null)
            for (File k : kids)
                deleteTree(k);
        f.delete();
    }

    /* ── Settings, rows laid out as ios_launcher.m renderSettings ────── */

    private String settingsGroup(Row r) {
        String k=r.key;
        if(k.equals("MC3_FPS_OVERLAY"))return "Display";
        if(r.cat.equals("Diagnostics"))return "Diagnostics";
        if(r.cat.equals("Advanced")||k.equals("RECOMP_GL_VSYNC")||k.equals("RECOMP_GL_QUEUE_FRAMES"))return "Advanced";
        if(k.contains("HAPTICS")||k.equals("MC3_RUMBLE_STRENGTH"))return "Feedback";
        if(k.contains("TILT")||k.equals("RECOMP_TOUCH_STEER_SENS"))return "Tilt & steering";
        if(r.cat.equals("Controls"))return "Touch controls";
        if(r.cat.equals("Display")||k.equals("RECOMP_GL_ASPECT"))return "Display";
        return "Graphics";
    }
    private void settingsPage() {
        page=new LinearLayout(this);page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(dp(22),dp(20),dp(22),dp(24));pane.addView(page);
        page.addView(label("Settings",27,true,TEXT));
        page.addView(label("Saved automatically. Close and reopen the game to apply changes.",12,false,MUTED));
        values.clear();
        SharedPreferences prefs=getSharedPreferences(PREFS,MODE_PRIVATE);
        java.util.Set<String> expanded=new java.util.HashSet<>(prefs.getStringSet("expandedSettingsV2",new java.util.HashSet<>(Arrays.asList("Display","Graphics"))));
        for(String group:new String[]{"Display","Graphics","Touch controls","Tilt & steering","Feedback","Advanced","Diagnostics"}) {
            boolean open=expanded.contains(group);
            page.addView(spacer(10));
            page.addView(button((open?"▼  ":"▶  ")+group,false,v->{
                if(expanded.contains(group))expanded.remove(group);else expanded.add(group);
                prefs.edit().putStringSet("expandedSettingsV2",expanded).apply();render();
            }));
            if(!open)continue;
            for(int i=0;i<rows.size();i++) {
                Row row=rows.get(i);if(!settingsGroup(row).equals(group))continue;
                final int index=i;
                LinearLayout line=new LinearLayout(this);line.setGravity(Gravity.CENTER_VERTICAL);
                line.setPadding(dp(8),dp(6),0,0);
                boolean narrow=getResources().getConfiguration().screenWidthDp<650;
                line.setOrientation(narrow?LinearLayout.VERTICAL:LinearLayout.HORIZONTAL);
                TextView name=label(row.name,13,false,TEXT);
                line.addView(name,narrow?new LinearLayout.LayoutParams(-1,-2):new LinearLayout.LayoutParams(0,-2,1));
                String current=settings.containsKey(row.key)?settings.get(row.key):row.dflt;
                boolean binary=row.values.length==2&&Arrays.asList(row.values).contains("0")&&Arrays.asList(row.values).contains("1");
                if(binary){
                    android.widget.Switch toggle=new android.widget.Switch(this);toggle.setChecked(current.equals("1"));toggle.setContentDescription(row.name);
                    toggle.setOnCheckedChangeListener((view,checked)->{int from=Arrays.asList(row.values).indexOf(settings.containsKey(row.key)?settings.get(row.key):row.dflt);if(from<0)from=0;shift(index,Arrays.asList(row.values).indexOf(checked?"1":"0")-from);});line.addView(toggle);
                }else{
                    TextView value=button(valueLabel(row),false,v->{
                        String now=settings.containsKey(row.key)?settings.get(row.key):row.dflt;
                        new android.app.AlertDialog.Builder(this).setTitle(row.name).setSingleChoiceItems(row.labels,Arrays.asList(row.values).indexOf(now),(dialog,which)->{
                            int from=Arrays.asList(row.values).indexOf(settings.containsKey(row.key)?settings.get(row.key):row.dflt);if(from<0)from=0;
                            shift(index,which-from);dialog.dismiss();
                        }).setNegativeButton("Cancel",null).show();
                    });
                    value.setContentDescription(row.name+", "+valueLabel(row));line.addView(value,new LinearLayout.LayoutParams(dp(180),dp(44)));values.put(row.key,value);
                }
                page.addView(line);
                if(row.hint!=null){TextView hint=label(row.hint,11,false,MUTED);hint.setPadding(dp(8),dp(4),dp(8),dp(6));page.addView(hint);}
            }
        }
    }

    private String valueLabel(Row r) {
        String v = settings.get(r.key);
        if (v == null)
            v = r.dflt;
        for (int i = 0; i < r.values.length; i++)
            if (r.values[i].equals(v))
                return r.labels[i];
        return v;
    }

    private void shift(int i, int delta) {
        Row r = rows.get(i);
        String cur = settings.containsKey(r.key) ? settings.get(r.key) : r.dflt;
        int at = 0;
        for (int k = 0; k < r.values.length; k++)
            if (r.values[k].equals(cur))
                at = k;
        at = (at + delta + r.values.length) % r.values.length;
        write(r.key, r.values[at]);
        if (r.cat.equals("Graphics") && !r.key.equals("MC3_GRAPHICS_PRESET"))
            write("MC3_GRAPHICS_PRESET", "custom");      /* a manual change is Custom */
        if (r.key.equals("MC3_GRAPHICS_PRESET"))
            applyPreset(at);
        refreshValues();
    }

    /* Performance, Balanced and Quality as the Windows launcher's table, so a
     * preset means the same here as in-game. */
    private void applyPreset(int n) {
        String[][] presets={{"2","3","4","1","0.5"},{"1","2","1","0","0"},{"3","3","16","2","0.75"}};
        String[] keys={"RECOMP_GL_SCALE","RECOMP_GL_UI_SCALE","RECOMP_GL_ANISO","RECOMP_GL_FXAA","RECOMP_GL_SHARPEN"};
        for(int i=0;i<keys.length;i++)write(keys[i],presets[n][i]);
        write("MC3_GRAPHICS_PRESET",new String[]{"default","performance","quality"}[n]);
    }

    private void refreshValues() {
        for (Row r : rows) {
            TextView t = values.get(r.key);
            if (t != null)
                t.setText(valueLabel(r));
        }
        footer.setText(statusText());
    }

    /* ── The settings file ───────────────────────────────────────────── */

    private File envFile() {
        File dir = getExternalFilesDir(null);
        return new File(dir != null ? dir : getFilesDir(), ENV);
    }

    private void loadSettings() {
        settings.clear();
        File f = envFile();
        if (!f.exists())
            return;
        try (BufferedReader in = new BufferedReader(
                new InputStreamReader(new FileInputStream(f), StandardCharsets.UTF_8))) {
            String line;
            while ((line = in.readLine()) != null) {
                String s = line.trim();
                if (s.isEmpty() || s.startsWith("#"))
                    continue;
                int eq = s.indexOf('=');
                if (eq > 0)
                    settings.put(s.substring(0, eq).trim(), s.substring(eq + 1).trim());
            }
        } catch (Exception e) {
            /* unreadable: the defaults */
        }
    }

    private void write(String key, String value) {
        settings.put(key, value);
        envSet(key, value);
    }

    /* One key replaced in place, comments and other settings kept, the rename
     * committing the file only when it is complete (android_host.c writes it
     * the same way). */
    private void envSet(String key, String value) {
        File f = envFile();
        File dir = f.getParentFile();
        if (dir != null)
            dir.mkdirs();
        StringBuilder text = new StringBuilder();
        if (f.exists()) {
            try (BufferedReader in = new BufferedReader(
                    new InputStreamReader(new FileInputStream(f), StandardCharsets.UTF_8))) {
                String line;
                while ((line = in.readLine()) != null) {
                    String s = line.trim();
                    int eq = s.indexOf('=');
                    if (eq > 0 && s.substring(0, eq).trim().equals(key))
                        continue;
                    text.append(line).append('\n');
                }
            } catch (Exception e) {
                return;
            }
        }
        text.append(key).append('=').append(value).append('\n');
        File tmp = new File(dir, f.getName() + ".tmp");
        try (BufferedWriter out = new BufferedWriter(
                new OutputStreamWriter(new FileOutputStream(tmp), StandardCharsets.UTF_8))) {
            out.write(text.toString());
        } catch (Exception e) {
            tmp.delete();
            return;
        }
        if (!tmp.renameTo(f))
            tmp.delete();
    }

    private android.graphics.Bitmap bitmap(String path) {
        try (InputStream in = getAssets().open(path)) {
            return android.graphics.BitmapFactory.decodeStream(in);
        } catch (Exception e) {
            return null;
        }
    }

    private String statusText() {
        String f = selectedFolder();
        if (f == null)
            return "Choose your game folder to begin";
        return isGame(new File(f)) ? "Ready to play  /  Android" : "No default.xbe in that folder";
    }

    /* ── The disc folders ────────────────────────────────────────────── */

    private static boolean isGame(File dir) {
        return new File(dir, "default.xbe").canRead();
    }

    private static List<String> expand(File dir) {
        List<String> out = new ArrayList<>();
        if (isGame(dir)) {
            out.add(dir.getAbsolutePath());
            return out;
        }
        File[] kids = dir.listFiles();
        if (kids != null) {
            Arrays.sort(kids);
            for (File k : kids)
                if (k.isDirectory() && isGame(k))
                    out.add(k.getAbsolutePath());
        }
        return out;
    }

    private List<String> found() {
        List<String> out = new ArrayList<>();
        List<File> roots = new ArrayList<>();
        roots.add(Environment.getExternalStorageDirectory());
        File[] vols = new File("/storage").listFiles();
        if (vols != null)
            for (File v : vols)
                if (!v.getName().equals("self") && !v.getName().equals("emulated"))
                    roots.add(v);
        File own = getExternalFilesDir(null);
        if (own != null)
            out.addAll(expand(own));
        String sd = getString(R.string.sd_dir);
        for (File r : roots)
            for (String sub : new String[] { sd, "switch/" + sd })
                out.addAll(expand(new File(r, sub)));
        return out;
    }

    private List<String> folders() {
        LinkedHashSet<String> all = new LinkedHashSet<>();
        for (String s : prefs.getString(FOLDERS, "").split("\n"))
            if (!s.isEmpty())
                all.add(s);
        String removed = prefs.getString(REMOVED, "");
        for (String f : found())
            if (!("\n" + removed + "\n").contains("\n" + f + "\n"))
                all.add(f);
        return new ArrayList<>(all);
    }

    private String selectedFolder() {
        String last = prefs.getString(LAST, null);
        List<String> all = folders();
        if (last != null && all.contains(last) && isGame(new File(last)))
            return last;
        for (String f : all)
            if (isGame(new File(f)))
                return f;
        return null;
    }

    private void save(List<String> folders) {
        prefs.edit().putString(FOLDERS, String.join("\n", folders)).apply();
    }

    /* -- Backing the game folder out, and back in ------------------------
     *
     * Uninstalling deletes getExternalFilesDir()/game-iso, which holds the disc
     * and the career in save/. 0.2.6 has to be uninstalled over 0.2.5 (new
     * signing key), so the folder has to be movable to somewhere that survives
     * an uninstall, without adb. GameFolderBackup does the copying; it is plain
     * java.io and is exercised in CI by tools/windows/Test-GameFolderBackup. */

    private static final String BACKUP_DIR = "ReDUB-backup";

    private File gameFolder() {
        String chosen = selectedFolder();
        return chosen == null ? null : new File(chosen);
    }

    private void pickBackupOut() {
        if (busy)
            return;
        startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), PICK_BACKUP_OUT);
    }

    private void pickBackupIn() {
        if (busy)
            return;
        startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), PICK_BACKUP_IN);
    }

    private void note(String text) {
        if (backupStatus != null)
            backupStatus.setText(text);
    }

    private void warn(String title, String body) {
        new android.app.AlertDialog.Builder(this).setTitle(title).setMessage(body)
            .setPositiveButton("OK", null).show();
    }

    private static String megabytes(long bytes) {
        return (bytes / (1024L * 1024L)) + " MB";
    }

    private void backupOut(Uri tree) {
        final File src = gameFolder();
        if (src == null || !isGame(src)) {
            warn("Choose a game folder first", "There is no installed game folder to back up.");
            return;
        }
        final File dst = new File(new File(treePath(tree)), BACKUP_DIR);
        if (dst.exists()) {
            new android.app.AlertDialog.Builder(this)
                .setTitle("Replace the backup?")
                .setMessage(dst + "\n\nalready holds a backup. Replacing it deletes the old one.")
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Replace", (d, w) -> startCopy(src, dst, true))
                .show();
            return;
        }
        startCopy(src, dst, true);
    }

    private void backupIn(Uri tree) {
        File picked = new File(treePath(tree));
        File src = new File(picked, BACKUP_DIR);
        if (!isGame(src))
            src = isGame(picked) ? picked : null;     /* ReDUB-backup/, else the folder itself */
        if (src == null) {
            warn("No backup there", picked + "\n\nholds neither a " + BACKUP_DIR
                + " folder nor a default.xbe.");
            return;
        }
        final File dst = new File(getExternalFilesDir(null), "game-iso");
        if (dst.exists() && !GameFolderBackup.isEmpty(dst)) {
            warn("A game is already installed",
                "Remove the installed folder on this page first, then restore.");
            return;
        }
        startCopy(src, dst, false);
    }

    /** One worker for both directions: the only difference is where the result
     *  is announced and whether the game page replaces the installed folder. */
    private void startCopy(final File src, final File dst, final boolean out) {
        busy = true;
        render();
        new Thread(() -> {
            try {
                final long[] last = {0};
                GameFolderBackup.copyTree(src, dst, (copied, total, name) -> {
                    if (copied - last[0] < 64L * 1024L * 1024L)
                        return;                 /* do not flood the UI thread */
                    last[0] = copied;
                    runOnUiThread(() -> note((out ? "Backing up " : "Restoring ")
                        + megabytes(copied) + " of " + megabytes(total)));
                });
                runOnUiThread(() -> {
                    busy = false;
                    if (!out) {
                        prefs.edit().putString(LAST, dst.getAbsolutePath()).apply();
                        current = PAGE_GAME;
                    }
                    render();
                });
            } catch (Exception e) {
                final String why = e.getMessage();
                runOnUiThread(() -> {
                    busy = false;
                    render();
                    warn(out ? "Could not back up" : "Could not restore",
                         why == null ? "The copy failed." : why);
                });
            }
        }).start();
    }

    private void pick() {
        startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), PICK_FOLDER);
    }

    private static String treePath(Uri uri) {
        String id = DocumentsContract.getTreeDocumentId(uri);
        int c = id.indexOf(':');
        String vol = c < 0 ? id : id.substring(0, c), rest = c < 0 ? "" : id.substring(c + 1);
        String base = vol.equalsIgnoreCase("primary")
            ? Environment.getExternalStorageDirectory().getAbsolutePath() : "/storage/" + vol;
        return rest.isEmpty() ? base : base + "/" + rest;
    }

    @Override
    protected void onActivityResult(int req, int result, Intent data) {
        super.onActivityResult(req, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null)
            return;
        if (req == PICK_ISO) {
            isoUri = data.getData();
            isoName = displayName(isoUri);
            try {
                getContentResolver().takePersistableUriPermission(isoUri,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (Exception e) {
                /* the grant lasts this launch then */
            }
            current = PAGE_INSTALL;
            render();
            return;
        }
        if (req == PICK_BACKUP_OUT) {
            backupOut(data.getData());
            return;
        }
        if (req == PICK_BACKUP_IN) {
            backupIn(data.getData());
            return;
        }
        if (req != PICK_FOLDER)
            return;
        File dir = new File(treePath(data.getData()));
        List<String> games = expand(dir);
        if (games.isEmpty()) {
            new android.app.AlertDialog.Builder(this)
                .setTitle("No game here")
                .setMessage(dir + "\n\nholds no default.xbe, nor do its folders. Choose the "
                    + "extracted disc's folder (the one with default.xbe in it).")
                .setPositiveButton("OK", null).show();
            return;
        }
        List<String> folders = folders();
        String removed = prefs.getString(REMOVED, "");
        for (String g : games) {
            if (!folders.contains(g))
                folders.add(g);
            removed = ("\n" + removed + "\n").replace("\n" + g + "\n", "\n").trim();
        }
        prefs.edit().putString(REMOVED, removed).putString(LAST, games.get(0)).apply();
        save(folders);
        render();
    }

    private void remove() {
        String f = selectedFolder();
        if (f == null)
            return;
        List<String> folders = folders();
        folders.remove(f);
        save(folders);
        String removed = prefs.getString(REMOVED, "");
        prefs.edit().putString(REMOVED, removed.isEmpty() ? f : removed + "\n" + f).apply();
        render();
    }

    /* ---- Online: the shared page (assets/ui/online.html) ----------------- */

    private static final String ONLINE_HOST = "162.35.56.66";
    private static final int ONLINE_PORT = 47210;

    private void onlinePage() {
        WebView w = new WebView(this);
        WebSettings s = w.getSettings();
        s.setJavaScriptEnabled(true);
        w.setBackgroundColor(BG);
        w.addJavascriptInterface(new OnlineBridge(), "ReDUBAndroid");
        onlineView = w;
        pane.addView(w, new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        /* The page is a full screen, not a document inside the scrolling
         * pane: match the viewport so its buttons are not cut off. */
        w.post(new Runnable() {
            public void run() {
                View scroll = (View) pane.getParent();
                int h = scroll == null ? 0 : scroll.getHeight();
                if (h > 0)
                    onlineView.setLayoutParams(new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT, h));
            }
        });
        w.loadUrl("file:///android_asset/ui/online.html");
        refreshOnline();
        onlineTicker.removeCallbacksAndMessages(null);
        onlineTicker.postDelayed(new Runnable() {
            public void run() {
                if (current == PAGE_ONLINE && onlineView != null) {
                    refreshOnline();
                    onlineTicker.postDelayed(this, 3000);
                }
            }
        }, 3000);
    }

    private void refreshOnline() {
        new Thread(new Runnable() {
            public void run() {
                String rooms = null, roster = null, error = null;
                int ms = -1;
                try {
                    long t0 = System.currentTimeMillis();
                    rooms = udp("MC3Q");
                    ms = (int) (System.currentTimeMillis() - t0);
                } catch (Exception e) {
                    error = e.getClass().getSimpleName();
                }
                if (error == null) {
                    try { roster = udp("MC3P"); } catch (Exception ignored) { }
                }
                final String fr = rooms, fp = roster, fe = error;
                final int fms = ms;
                runOnUiThread(new Runnable() {
                    public void run() { pushOnline(fr, fp, fe, fms); }
                });
            }
        }).start();
    }

    private String udp(String query) throws Exception {
        DatagramSocket sock = new DatagramSocket();
        try {
            sock.setSoTimeout(800);
            byte[] q = query.getBytes("US-ASCII");
            sock.send(new DatagramPacket(q, q.length,
                InetAddress.getByName(ONLINE_HOST), ONLINE_PORT));
            byte[] buf = new byte[2048];
            DatagramPacket p = new DatagramPacket(buf, buf.length);
            sock.receive(p);
            return new String(buf, 0, p.getLength(), "US-ASCII");
        } finally {
            sock.close();
        }
    }

    private void pushOnline(String rooms, String roster, String error, int ms) {
        if (onlineView == null)
            return;
        onlineView.evaluateJavascript("redubApply(" + onlineJson(rooms, roster, error, ms) + ")", null);
    }

    private String onlineJson(String rooms, String roster, String error, int ms) {
        String mine = settings.get("MC3_ROAM_ROOM");
        int mineN;
        try { mineN = Integer.parseInt(mine == null || mine.isEmpty() ? "0" : mine); }
        catch (NumberFormatException e) { mineN = 0; }
        StringBuilder rj = new StringBuilder("[");
        boolean first = true;
        if (rooms != null && rooms.startsWith("MC3ROOMS")) {
            String[] parts = rooms.split(" ");
            for (int i = 3; i < parts.length; i++) {
                String[] kv = parts[i].split(":");
                if (kv.length != 2)
                    continue;
                try {
                    int g = Integer.parseInt(kv[0]), n = Integer.parseInt(kv[1]);
                    if (!first)
                        rj.append(',');
                    first = false;
                    rj.append("{\"n\":").append(g).append(",\"players\":").append(n).append('}');
                } catch (NumberFormatException ignored) { }
            }
        }
        rj.append(']');
        StringBuilder pj = new StringBuilder("[");
        boolean firstP = true;
        if (roster != null && roster.startsWith("MC3PLAYERS")) {
            String[] lines = roster.replace("\r", "").split("\n");
            for (int i = 1; i < lines.length; i++) {
                String[] f = lines[i].split("\\|", 3);
                if (f.length < 3)
                    continue;
                try {
                    int rm = Integer.parseInt(f[0]), st = Integer.parseInt(f[1]);
                    if (!firstP)
                        pj.append(',');
                    firstP = false;
                    pj.append("{\"room\":").append(rm).append(",\"status\":").append(st)
                      .append(",\"name\":").append(json(f[2])).append('}');
                } catch (NumberFormatException ignored) { }
            }
        }
        pj.append(']');
        String name = settings.get("MC3_ROAM_NAME");
        if (name == null || name.trim().isEmpty())
            name = settings.get("MC3_PLAYER_NAME");
        if (name == null || name.trim().isEmpty())
            name = "Player";   /* the game's own fallback, so the page agrees */
        StringBuilder s = new StringBuilder("{");
        s.append("\"ms\":").append(ms);
        s.append(",\"yourRoom\":").append(mineN);
        s.append(",\"playerName\":").append(json(name == null ? "" : name));
        s.append(",\"rooms\":").append(rj);
        s.append(",\"roster\":").append(pj);
        s.append(",\"rosterOk\":").append(roster != null && roster.startsWith("MC3PLAYERS"));
        if (error != null)
            s.append(",\"error\":").append(json(error));
        s.append('}');
        return s.toString();
    }

    private static String json(String v) {
        if (v == null)
            return "null";
        StringBuilder b = new StringBuilder("\"");
        for (int i = 0; i < v.length(); i++) {
            char c = v.charAt(i);
            if (c == '"' || c == '\\')
                b.append('\\').append(c);
            else if (c == '\n')
                b.append("\\n");
            else if (c == '\r')
                b.append("\\r");
            else if (c == '\t')
                b.append("\\t");
            else if (c < 32)
                b.append(String.format("\\u%04x", (int) c));
            else
                b.append(c);
        }
        return b.append('"').toString();
    }

    /* window.ReDUBAndroid.call(action, json): the page's only handle on this
     * launcher. Runs on the JS bridge thread, so anything touching views goes
     * through the UI thread inside the helpers. */
    private final class OnlineBridge {
        @JavascriptInterface
        public void call(String action, String payload) {
            if ("refresh".equals(action)) {
                refreshOnline();
                return;
            }
            if ("invite".equals(action)) {
                inviteFriends();
                return;
            }
            if ("name".equals(action)) {
                final String who = jsonName(payload);
                runOnUiThread(new Runnable() {
                    public void run() {
                        if (!nameOk(who)) {
                            toast("Names use 1-24 letters, numbers, spaces, _ - or .");
                            return;
                        }
                        write("MC3_ROAM_NAME", who);
                        toast("Your name is " + who + ". The game uses it when it starts.");
                        refreshOnline();
                    }
                });
                return;
            }
            if ("join".equals(action)) {
                final int room = jsonRoom(payload);
                runOnUiThread(new Runnable() {
                    public void run() {
                        if (room < 0) {
                            toast("Paste the invite link or type the room number (0-255).");
                            return;
                        }
                        write("MC3_ROAM_ROOM", Integer.toString(room));
                        toast("Room " + room + " saved. Starting the game.");
                        play();
                    }
                });
            }
        }
    }

    /* The name typed on the Online page: {"name":"..."}. The same rule as the
     * game's mc3_nickname_normalize, so a bad name is refused before it can
     * reach the relay. */
    private static String jsonName(String payload) {
        if (payload == null)
            return "";
        int i = payload.indexOf("\"name\"");
        if (i < 0)
            return "";
        int j = payload.indexOf(':', i + 6);
        if (j < 0)
            return "";
        i = payload.indexOf('"', j + 1);
        if (i < 0)
            return "";
        StringBuilder b = new StringBuilder();
        for (int k = i + 1; k < payload.length(); k++) {
            char c = payload.charAt(k);
            if (c == '"')
                break;
            if (c == '\\' && k + 1 < payload.length()) {
                char e = payload.charAt(++k);
                b.append(e == 'n' ? '\n' : e == 'r' ? '\r' : e == 't' ? '\t' : e);
                continue;
            }
            b.append(c);
        }
        return b.toString().trim();
    }

    private static boolean nameOk(String name) {
        if (name == null || name.isEmpty() || name.length() > 24)
            return false;
        for (int i = 0; i < name.length(); i++) {
            char c = name.charAt(i);
            boolean ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                      || (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-' || c == '.';
            if (!ok)
                return false;
        }
        return true;
    }

    private static int jsonRoom(String payload) {
        if (payload == null)
            return -1;
        int i = payload.indexOf("\"room\"");
        if (i < 0)
            return -1;
        int j = payload.indexOf(':', i + 6);
        if (j < 0)
            return -1;
        int n = 0;
        boolean any = false;
        for (int k = j + 1; k < payload.length(); k++) {
            char c = payload.charAt(k);
            if (c >= '0' && c <= '9') {
                n = n * 10 + (c - '0');
                any = true;
            } else if (any) {
                break;
            } else if (c != ' ') {
                return -1;
            }
        }
        return any && n >= 0 && n <= 255 ? n : -1;
    }

    private void inviteFriends() {
        String mine = settings.get("MC3_ROAM_ROOM");
        int room;
        try { room = Integer.parseInt(mine == null || mine.isEmpty() ? "0" : mine); }
        catch (NumberFormatException e) { room = 0; }
        if (room == 0)
            room = 1 + (int) (Math.random() * 254);
        final int chosen = room;
        String who = settings.get("MC3_ROAM_NAME");
        if (who == null || who.trim().isEmpty())
            who = settings.get("MC3_PLAYER_NAME");
        if (who == null || who.trim().isEmpty())
            who = "Player";
        final String text = "ReDUB - " + who + " invites you to room " + chosen
                          + "\r\nredub://room/" + chosen
                          + "\r\n(In ReDUB: Online > Join a friend > " + chosen + ")";
        runOnUiThread(new Runnable() {
            public void run() {
                write("MC3_ROAM_ROOM", Integer.toString(chosen));
                ClipboardManager cm = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
                if (cm != null)
                    cm.setPrimaryClip(ClipData.newPlainText("ReDUB invite", text));
                toast("Invite copied for room " + chosen + ". Close the game before joining a new room.");
                refreshOnline();
            }
        });
    }

    private void toast(String text) {
        android.widget.Toast.makeText(this, text, android.widget.Toast.LENGTH_LONG).show();
    }

    private void verify() {
        String f = selectedFolder();
        new android.app.AlertDialog.Builder(this)
            .setTitle("Verify setup")
            .setMessage(f == null ? "No game folder chosen yet."
                : isGame(new File(f)) ? f + "\n\ndefault.xbe found. Ready to play."
                : f + "\n\nNo default.xbe in this folder.")
            .setPositiveButton("OK", null).show();
    }

    private void play() {
        if (busy)
            return;
        String f = selectedFolder();
        if (f == null || !isGame(new File(f))) {
            current = PAGE_INSTALL;
            render();
            return;
        }
        prefs.edit().putString(LAST, f).apply();
        Intent i = new Intent(this, MainActivity.class);
        i.putExtra(MainActivity.EXTRA_GAME_DIR, f);
        startActivity(i);
        finish();
    }

    /* ── The settings the Android host reads ─────────────────────────── */

    private static String[] range(double from, double step, int n, int decimals) {
        String[] out = new String[n];
        for (int i = 0; i < n; i++) {
            double v = from + step * i;
            out[i] = decimals == 1 ? String.format(java.util.Locale.US, "%.1f", v)
                                   : String.format(java.util.Locale.US, "%.2f", v);
        }
        return out;
    }

    private List<Row> rows() {
        List<Row> r = new ArrayList<>();
        r.add(new Row("Display", "Gamma", "MC3_GAMMA", new String[] { "0.8", "0.9", "1", "1.1", "1.2", "1.3", "1.4" }, new String[] { "0.8", "0.9", "1.0 (Default)", "1.1", "1.2", "1.3", "1.4" }, "1", null));
        r.add(new Row("Display", "Brightness", "MC3_BRIGHTNESS", new String[] { "0.8", "0.9", "1", "1.1", "1.2" }, new String[] { "80%", "90%", "100% (Default)", "110%", "120%" }, "1", null));
        r.add(new Row("Display", "VSync", "RECOMP_GL_VSYNC", new String[] { "0", "1", "-1" }, new String[] { "Off", "On", "Adaptive" }, "1", "Availability depends on the iOS graphics driver. Frame timing diagnostics report whether the request was accepted."));
        r.add(new Row("Display", "Frame rate", "MC3_FRAME_RATE", new String[] { "30", "60" }, new String[] { "30 FPS (Default)", "60 FPS (Restart)" }, "30", null));
        r.add(new Row("Graphics", "Preset", "MC3_GRAPHICS_PRESET", new String[] { "default", "performance", "quality" }, new String[] { "Default", "Performance", "Quality" }, "default", "Applies a matching set of graphics values; the rows below update to match. Internal resolution, HUD and filtering apply on the next launch."));
        r.add(new Row("Graphics", "Internal resolution", "RECOMP_GL_SCALE", new String[] { "1", "1.5", "2", "3", "5" }, new String[] { "1x native", "1.5x native", "2x native", "3x native", "5x native" }, "2", "Multiplies the game's 720x480 internal surface; 5x is 4K-class detail."));
        r.add(new Row("Graphics", "HUD texture quality", "RECOMP_GL_UI_SCALE", new String[] { "1", "2", "3", "5" }, new String[] { "1x", "2x", "3x (Default)", "5x" }, "3", null));
        r.add(new Row("Graphics", "Anti-aliasing", "RECOMP_GL_FXAA", new String[] { "0", "1", "2", "3" }, new String[] { "Off", "FXAA", "FXAA Low", "FXAA High" }, "1", null));
        r.add(new Row("Graphics", "Texture filtering", "RECOMP_GL_ANISO", new String[] { "1", "4", "16" }, new String[] { "1x", "4x (Default)", "16x" }, "4", null));
        r.add(new Row("Graphics", "Sharpening", "RECOMP_GL_SHARPEN", new String[] { "0", "0.25", "0.5", "0.75", "1" }, new String[] { "Off", "25%", "50%", "75%", "100%" }, "0.5", null));
        r.add(new Row("Graphics", "Frame queue", "RECOMP_GL_QUEUE_FRAMES", new String[] { "1", "2" }, new String[] { "1 frame", "2 frames" }, "2", null));
        r.add(new Row("Graphics", "Display", "RECOMP_GL_ASPECT", new String[] { "auto", "4:3", "fill" }, new String[] { "Auto (16:9)", "4:3", "Stretch" }, "auto", "Auto fills widescreen frames to 16:9 (pillarboxed on a tall phone); 4:3 keeps the original shape; Stretch uses every pixel of the screen."));
        r.add(new Row("Controls", "On-screen controls", "RECOMP_TOUCH_PAD", new String[] { "-1", "1", "0" }, new String[] { "Auto", "Always", "Off" }, "-1", null));
        r.add(new Row("Controls", "Pad opacity", "RECOMP_TOUCH_PAD_ALPHA", new String[] { "0.35", "0.45", "0.55", "0.7", "0.85", "1" }, new String[] { "35%", "45%", "55% (Default)", "70%", "85%", "100%" }, "0.55", null));
        r.add(new Row("Controls", "Pad size", "RECOMP_TOUCH_PAD_SCALE", new String[] { "0.8", "0.9", "1", "1.1", "1.2" }, new String[] { "80%", "90%", "100% (Default)", "110%", "120%" }, "1", null));
        r.add(new Row("Controls", "Pedals", "RECOMP_TOUCH_PEDALS", new String[] { "stick", "triggers", "bars", "sliders" }, new String[] { "Stick up / down (Default)", "LT / RT buttons", "Shoulder bars", "Edge sliders" }, "stick", null));
        r.add(new Row("Controls", "Tap-to-latch gas", "RECOMP_TOUCH_LATCH", new String[] { "0", "1" }, new String[] { "Off", "On" }, "0", null));
        r.add(new Row("Controls", "Auto-accelerate", "RECOMP_TOUCH_AUTO_GAS", new String[] { "0", "1" }, new String[] { "Off", "On" }, "0", null));
        r.add(new Row("Controls", "Touch haptics", "RECOMP_TOUCH_HAPTICS", new String[] { "1", "0" }, new String[] { "On", "Off" }, "1", "A light click when a pedal engages and a firmer one at full throttle, in every pedal layout."));
        r.add(new Row("Controls", "Stick haptics", "RECOMP_TOUCH_STICK_HAPTICS", new String[] { "1", "0" }, new String[] { "On", "Off" }, "1", "Clicks when the analog stick leaves centre and reaches the outer ring, in every pedal mode. Touch haptics must also be on."));
        r.add(new Row("Controls", "Controller vibration", "MC3_RUMBLE_STRENGTH", new String[] { "0", "50", "75", "100", "125", "150" }, new String[] { "Off", "50%", "75%", "100%", "125%", "150%" }, "100", null));
        r.add(new Row("Controls", "Steering", "RECOMP_TOUCH_STEER", new String[] { "stick", "tilt", "both" }, new String[] { "Stick", "Tilt", "Stick + tilt" }, "stick", "Tilt uses the phone's accelerometer. Hold the phone level when the race starts to set its centre."));
        r.add(new Row("Controls", "Steering response", "RECOMP_TOUCH_STEER_SENS", new String[] { "1", "1.35", "1.7", "2" }, new String[] { "Normal", "Quick", "Sharp", "Very sharp" }, "1", "Boosts small steering inputs; full lock is unchanged."));
        r.add(new Row("Controls", "Tilt range", "RECOMP_TOUCH_TILT_RANGE", new String[] { "0.35", "0.45", "0.55", "0.70" }, new String[] { "Short", "Medium (Default)", "Wide", "Extra wide" }, "0.45", "A wider range needs more phone tilt for full steering lock."));
        r.add(new Row("Controls", "Tilt dead zone", "RECOMP_TOUCH_TILT_DEADZONE", new String[] { "0.02", "0.06", "0.10" }, new String[] { "Small", "Normal (Default)", "Large" }, "0.06", "Ignores small movements around the calibrated centre."));
        r.add(new Row("Controls", "Invert tilt", "RECOMP_TOUCH_TILT_INVERT", new String[] { "0", "1" }, new String[] { "Off", "On" }, "0", "Turn on if tilting left steers right."));
        r.add(new Row("Advanced", "Steering dead zone", "RECOMP_TOUCH_DEADZONE", new String[] { "0", "3", "5", "8", "12" }, new String[] { "Off", "3%", "5% (Default)", "8%", "12%" }, "5", "How far the stick must move before it steers. Ignores tiny thumb movements around the centre; full lock is still reached at the ring's edge."));
        r.add(new Row("Advanced", "Throttle ramp", "RECOMP_TOUCH_RAMP", new String[] { "0", "150", "300" }, new String[] { "Off", "Fast (150 ms)", "Smooth (300 ms)" }, "0", "Eases the touch pedals from rest to full throttle or brake over 150 or 300 ms instead of an instant press. Gives smoother launches and less wheelspin; a real controller's triggers are not affected."));
        r.add(new Row("Diagnostics", "FPS counter", "MC3_FPS_OVERLAY", new String[] { "0", "1" }, new String[] { "Off", "On" }, "0", "Draws the frame rate at the top of the screen."));
        r.add(new Row("Diagnostics", "Frame timing log", "MC3_TIME_TRACE", new String[] { "0", "1" }, new String[] { "Off", "On" }, "0", "Logs game steps, gameplay transitions and presentation waits once per second. Enable before Play; share diagnostics after a race."));
        r.add(new Row("Diagnostics", "Performance report", "MC3_PERF", new String[] { "0", "3" }, new String[] { "Off", "Every 3 seconds" }, "0", "Measures CPU waits, guest lock contention and renderer work. Enable for a short race, then share diagnostics. Restart after changing."));
        r.add(new Row("Diagnostics", "GL error log", "RECOMP_GL_ERRORS", new String[] { "0", "1", "2" }, new String[] { "Off", "Step", "Call" }, "0", "Step names the stage that raised a GL error; Call names the exact GL call and is slow, for chasing a driver problem."));
        r.add(new Row("Diagnostics", "Renderer trace", "RECOMP_GL_TRACE", new String[] { "0", "1" }, new String[] { "Off", "On" }, "0", "Writes shader sources, surface and texture creation, and any texture mode the renderer does not implement (bump, dot, cube) to the launch log. Diagnostic only: it slows loading and makes a large log. Turn it off when done."));
        r.add(new Row("Diagnostics", "Texgen trace", "RECOMP_GL_TEXGEN_TRACE", new String[] { "0", "1" }, new String[] { "Off", "On" }, "0", "Logs fixed-function texture-coordinate generation that the renderer cannot yet do. Use with Renderer trace when chasing graphics bugs in the garage or menus."));
        r.add(new Row("Diagnostics", "Feedback trace", "RECOMP_GL_FEEDBACK_TRACE", new String[] { "0", "1" }, new String[] { "Off", "On" }, "0", "Logs draws that sample the surface they are drawing into, which GL does not define. Changes nothing on screen."));
        return r;
    }
}
