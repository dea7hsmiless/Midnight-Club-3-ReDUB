package io.github.laureladastra.mcdub3;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Copies the game folder to a place the user picked, and back.
 *
 * The Android build installs the disc and keeps the career in
 * getExternalFilesDir()/game-iso, which is inside the app's own storage: an
 * uninstall deletes it. 0.2.6 had to be uninstalled over 0.2.5 (new signing
 * key), so users lost their disc copy and their saves. This is the escape
 * hatch: copy the folder out before uninstalling, copy it back afterwards.
 *
 * Deliberately plain java.io with no Android types, so it can be compiled and
 * exercised on a desktop: tools/windows/Test-GameFolderBackup.java runs the
 * same class through javac in CI (the pattern DiscSaveData already uses).
 *
 * A copy that fails part-way removes what it wrote, so a failed backup never
 * looks like a complete one.
 */
public final class GameFolderBackup {

    /** Called once per file, before it is copied. */
    public interface Progress {
        void onProgress(long copied, long total, String name);
    }

    private static final int BUFFER = 64 * 1024;

    private GameFolderBackup() {
    }

    /** Bytes in every regular file under root, for a progress total. */
    public static long size(File root) {
        if (root == null || !root.isDirectory())
            return 0;
        long total = 0;
        File[] kids = root.listFiles();
        if (kids == null)
            return 0;
        for (File f : kids) {
            if (f.isDirectory())
                total += size(f);
            else
                total += f.length();
        }
        return total;
    }

    /** Number of regular files under root. */
    public static int count(File root) {
        if (root == null || !root.isDirectory())
            return 0;
        int n = 0;
        File[] kids = root.listFiles();
        if (kids == null)
            return 0;
        for (File f : kids)
            n += f.isDirectory() ? count(f) : 1;
        return n;
    }

    /**
     * Copy the tree at src into the new directory dst.
     *
     * dst must not exist, or be an empty directory: this copies a folder, it
     * does not merge into one. Copying a folder inside itself is refused,
     * because that recurses until the disk is full.
     *
     * On failure the partial dst is removed and the exception is rethrown, so
     * "the directory exists" always means "the copy finished".
     */
    public static long copyTree(File src, File dst, Progress progress) throws IOException {
        if (src == null || !src.isDirectory())
            throw new IOException("not a folder: " + src);

        String from = src.getCanonicalPath();
        String to = dst.getCanonicalPath();
        if (to.equals(from) || to.startsWith(from + File.separator))
            throw new IOException("cannot copy a folder into itself: " + dst);

        if (dst.exists()) {
            String[] existing = dst.list();
            if (existing == null || existing.length > 0)
                throw new IOException("already there: " + dst);
        }

        long total = size(src);
        boolean created = dst.mkdirs();
        if (!created && !dst.isDirectory())
            throw new IOException("could not make " + dst);

        try {
            return copyInto(src, dst, total, new long[] {0}, progress);
        } catch (IOException e) {
            deleteTree(dst);
            throw e;
        } catch (RuntimeException e) {
            /* A callback that throws is a bug, not a reason to leave a
             * half-written backup that looks complete. */
            deleteTree(dst);
            throw e;
        }
    }

    private static long copyInto(File src, File dst, long total, long[] copied,
                                 Progress progress) throws IOException {
        File[] kids = src.listFiles();
        if (kids == null)
            throw new IOException("cannot read " + src);

        long written = 0;
        for (File f : kids) {
            File target = new File(dst, f.getName());
            if (f.isDirectory()) {
                if (!target.mkdirs() && !target.isDirectory())
                    throw new IOException("could not make " + target);
                written += copyInto(f, target, total, copied, progress);
                continue;
            }
            if (progress != null)
                progress.onProgress(copied[0], total, f.getName());
            written += copyFile(f, target);
            copied[0] += f.length();
        }
        return written;
    }

    private static long copyFile(File from, File to) throws IOException {
        InputStream in = new FileInputStream(from);
        try {
            OutputStream out = new FileOutputStream(to);
            try {
                byte[] buf = new byte[BUFFER];
                long written = 0;
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                    written += n;
                }
                return written;
            } finally {
                out.close();
            }
        } finally {
            in.close();
        }
    }

    /** True when the folder does not exist or holds nothing: the condition for
     *  restoring into a folder a previous install left behind. */
    public static boolean isEmpty(File dir) {
        if (dir == null || !dir.exists())
            return true;
        if (!dir.isDirectory())
            return false;
        String[] kids = dir.list();
        return kids == null || kids.length == 0;
    }

    /** Remove a tree; a file that will not delete is left, not fatal. */
    public static void deleteTree(File f) {
        if (f == null || !f.exists())
            return;
        if (f.isDirectory()) {
            File[] kids = f.listFiles();
            if (kids != null)
                for (File k : kids)
                    deleteTree(k);
        }
        //noinspection ResultOfMethodCallIgnored
        f.delete();
    }

    /** True when two trees hold the same relative paths and the same bytes. */
    public static boolean sameTree(File a, File b) {
        if (a == null || b == null)
            return false;
        if (!a.isDirectory() || !b.isDirectory())
            return false;
        File[] ka = a.listFiles();
        File[] kb = b.listFiles();
        if (ka == null || kb == null || ka.length != kb.length)
            return false;
        for (File fa : ka) {
            File fb = new File(b, fa.getName());
            if (fa.isDirectory()) {
                if (!sameTree(fa, fb))
                    return false;
            } else {
                if (!fb.isFile() || fb.length() != fa.length() || !sameBytes(fa, fb))
                    return false;
            }
        }
        return true;
    }

    private static boolean sameBytes(File a, File b) {
        InputStream ia = null;
        InputStream ib = null;
        try {
            ia = new FileInputStream(a);
            ib = new FileInputStream(b);
            byte[] ba = new byte[BUFFER];
            byte[] bb = new byte[BUFFER];
            while (true) {
                int na = readFully(ia, ba);
                int nb = readFully(ib, bb);
                if (na != nb)
                    return false;
                if (na <= 0)
                    return true;
                for (int i = 0; i < na; i++)
                    if (ba[i] != bb[i])
                        return false;
            }
        } catch (IOException e) {
            return false;
        } finally {
            closeQuietly(ia);
            closeQuietly(ib);
        }
    }

    private static int readFully(InputStream in, byte[] buf) throws IOException {
        int off = 0;
        while (off < buf.length) {
            int n = in.read(buf, off, buf.length - off);
            if (n < 0)
                break;
            off += n;
        }
        return off;
    }

    private static void closeQuietly(InputStream in) {
        if (in != null)
            try {
                in.close();
            } catch (IOException ignored) {
                // closing a file we only read cannot lose anything
            }
    }
}
