package io.github.laureladastra.mcdub3;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.util.Random;

/**
 * Desktop test for GameFolderBackup. Runs under plain javac/java in CI, the
 * same way TestDiscSaveData exercises DiscSaveData.
 *
 *   javac -d out GameFolderBackup.java Test-GameFolderBackup.java
 *   java -cp out io.github.laureladastra.mcdub3.TestGameFolderBackup &lt;workdir&gt;
 *
 * Exit 0 = every check passed.
 */
final class TestGameFolderBackup {

    private static int failures;

    public static void main(String[] args) throws Exception {
        File work = new File(args.length > 0 ? args[0] : "work/game-folder-backup");
        GameFolderBackup.deleteTree(work);
        if (!work.mkdirs())
            throw new IOException("cannot make " + work);

        copiesATree(work);
        countsBytesAndFiles(work);
        reportsProgress(work);
        refusesFolderInsideItself(work);
        refusesToMergeIntoFullFolder(work);
        rollsBackOnFailure(work);
        restoresByteForByte(work);

        System.out.println(failures == 0 ? "ALL PASS" : failures + " FAILED");
        GameFolderBackup.deleteTree(work);
        if (failures != 0)
            System.exit(1);
    }

    /** A disc-shaped tree: nested folders, a big file, an empty folder, and a
     *  name with a space and a non-ASCII character. */
    private static File makeTree(File parent, String name) throws IOException {
        File root = new File(parent, name);
        mkdirs(root);
        mkdirs(new File(root, "data/audio"));
        mkdirs(new File(root, "save"));
        mkdirs(new File(root, "empty"));

        write(new File(root, "default.xbe"), 4096);
        write(new File(root, "data/audio/streams.dat"), 300 * 1024);
        write(new File(root, "save/profile.sav"), 1234);
        write(new File(root, "Jeux \u00c9dition.txt"), 37);
        return root;
    }

    private static void copiesATree(File work) throws IOException {
        File src = makeTree(work, "src1");
        File dst = new File(work, "dst1");
        long written = GameFolderBackup.copyTree(src, dst, null);

        check("copies every file", GameFolderBackup.count(dst) == 4,
              GameFolderBackup.count(dst) + " files");
        check("copies the same bytes", GameFolderBackup.sameTree(src, dst), "sameTree");
        check("returns the copied size", written == GameFolderBackup.size(src),
              written + " vs " + GameFolderBackup.size(src));
        check("keeps empty folders", new File(dst, "empty").isDirectory(), "empty/");
        check("keeps a non-ASCII name",
              new File(dst, "Jeux \u00c9dition.txt").isFile(), "Jeux \u00c9dition.txt");
    }

    private static void countsBytesAndFiles(File work) {
        File src = new File(work, "src1");
        check("count() matches what was written", GameFolderBackup.count(src) == 4,
              GameFolderBackup.count(src) + " files");
        check("size() is the sum of the files",
              GameFolderBackup.size(src) == 4096 + 300 * 1024 + 1234 + 37,
              GameFolderBackup.size(src) + " bytes");
        check("a missing folder counts as zero",
              GameFolderBackup.size(new File(work, "nope")) == 0, "0");
    }

    private static void reportsProgress(File work) throws IOException {
        File src = new File(work, "src1");
        final long[] last = {-1};
        final int[] calls = {0};
        final boolean[] monotonic = {true};
        GameFolderBackup.copyTree(src, new File(work, "dst-progress"),
                new GameFolderBackup.Progress() {
                    public void onProgress(long copied, long total, String name) {
                        calls[0]++;
                        if (copied < last[0])
                            monotonic[0] = false;
                        last[0] = copied;
                        if (total != GameFolderBackup.size(new File(work, "src1")))
                            monotonic[0] = false;
                    }
                });
        check("progress is called once per file", calls[0] == 4, calls[0] + " calls");
        check("progress never goes backwards and knows the total", monotonic[0], "monotonic");
    }

    private static void refusesFolderInsideItself(File work) throws IOException {
        File src = new File(work, "src1");
        boolean threw = false;
        try {
            GameFolderBackup.copyTree(src, new File(src, "inner"), null);
        } catch (IOException e) {
            threw = true;
        }
        check("refuses to copy a folder into itself", threw, "threw");
        check("  and leaves nothing behind", !new File(src, "inner").exists(), "no inner/");
    }

    private static void refusesToMergeIntoFullFolder(File work) throws IOException {
        File src = new File(work, "src1");
        File dst = new File(work, "dst1");            // already has the copy
        boolean threw = false;
        try {
            GameFolderBackup.copyTree(src, dst, null);
        } catch (IOException e) {
            threw = true;
        }
        check("refuses to merge into a folder that has files", threw, "threw");
        check("  and leaves the existing copy intact",
              GameFolderBackup.sameTree(src, dst), "unchanged");
    }

    private static void rollsBackOnFailure(File work) throws IOException {
        File src = new File(work, "src1");
        File dst = new File(work, "dst-rollback");
        boolean threw = false;
        try {
            GameFolderBackup.copyTree(src, dst, new GameFolderBackup.Progress() {
                public void onProgress(long copied, long total, String name) {
                    throw new IllegalStateException("stop here");
                }
            });
        } catch (RuntimeException e) {
            threw = true;
        }
        check("a failure mid-copy propagates", threw, "threw");
        check("  and no half-written backup is left", !dst.exists(), "dst gone");
    }

    private static void restoresByteForByte(File work) throws IOException {
        /* The round trip a user does: copy out, delete the original (the
         * uninstall), copy back, compare. */
        File original = makeTree(work, "round-trip");
        File backup = new File(work, "round-trip-backup");
        GameFolderBackup.copyTree(original, backup, null);
        GameFolderBackup.deleteTree(original);
        check("original is gone after the delete", !original.exists(), "deleted");
        GameFolderBackup.copyTree(backup, original, null);
        check("restored tree is byte-for-byte identical",
              GameFolderBackup.sameTree(backup, original), "sameTree");
    }

    private static void check(String what, boolean ok, String detail) {
        System.out.println((ok ? "PASS: " : "FAIL: ") + what + "  [" + detail + "]");
        if (!ok)
            failures++;
    }

    private static void mkdirs(File f) throws IOException {
        if (!f.mkdirs() && !f.isDirectory())
            throw new IOException("cannot make " + f);
    }

    private static void write(File f, int bytes) throws IOException {
        Random r = new Random(f.getName().hashCode());
        byte[] buf = new byte[bytes];
        r.nextBytes(buf);
        OutputStream out = new FileOutputStream(f);
        try {
            out.write(buf);
        } finally {
            out.close();
        }
    }
}
