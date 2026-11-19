package io.github.laureladastra.mcdub3;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.SeekableByteChannel;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/* XDVDFS (the Xbox DVD file system) reader: the Android arm of the disc
 * import that iOS (src/ios_xdvdfs.c) and Windows (.portable/Xdvdfs.cs) already
 * have, so an ISO is installed on the device instead of needing a PC.
 *
 * The format notes here follow xboxrecomp/tools/xiso/xdvdfs.py, this repo's
 * own reader:
 *
 *   <base> + 32 * 2048   the volume descriptor; "MICROSOFT*XBOX*MEDIA" at the
 *                        start, root directory sector at +0x14, its size at
 *                        +0x18 (both little-endian u32)
 *   directory tables     fixed entries of left/right u16, sector u32, size u32,
 *                        attributes u8, name length u8, name, padded to 4 bytes
 *   attributes 0x10      a directory
 *
 * A bare game partition starts at 0, but images carrying a video partition in
 * front are common, so the descriptor is probed at each known base and the one
 * holding the magic wins (the candidates are the Python reader's).
 *
 * Deliberately not implemented: writing, the video partition and timestamps;
 * nothing here needs them. */
public final class Xiso {
    public static final int SECTOR = 2048;
    private static final byte[] MAGIC =
        "MICROSOFT*XBOX*MEDIA".getBytes(StandardCharsets.US_ASCII);
    private static final long[] BASES = {
        0x00000000L, 0x0000FD90L, 0x00030600L, 0x0FD90000L, 0x18300000L
    };
    private static final int ATTR_DIRECTORY = 0x10;
    private static final int MAX_TABLE = 16 << 20;   /* a directory table's cap */

    /** One file or directory in a directory table. */
    public static final class Entry {
        public final String name;
        public final long sector, size;
        public final int attributes;

        Entry(String name, long sector, long size, int attributes) {
            this.name = name;
            this.sector = sector;
            this.size = size;
            this.attributes = attributes;
        }

        public boolean isDir() {
            return (attributes & ATTR_DIRECTORY) != 0;
        }
    }

    private final SeekableByteChannel ch;
    private final long base;
    private final long rootSector, rootSize;

    public Xiso(SeekableByteChannel ch) throws IOException {
        this.ch = ch;
        this.base = findBase();
        byte[] desc = read(base + 32L * SECTOR, SECTOR);
        ByteBuffer b = ByteBuffer.wrap(desc).order(ByteOrder.LITTLE_ENDIAN);
        this.rootSector = b.getInt(0x14) & 0xFFFFFFFFL;
        this.rootSize = b.getInt(0x18) & 0xFFFFFFFFL;
        if (rootSize <= 0 || rootSize > MAX_TABLE)
            throw new IOException("XDVDFS root directory size looks wrong: " + rootSize);
    }

    /** The game partition's base offset inside the image. */
    public long base() {
        return base;
    }

    private long findBase() throws IOException {
        byte[] probe = new byte[MAGIC.length];
        for (long base : BASES) {
            if (base + 32L * SECTOR + MAGIC.length > ch.size())
                continue;
            readInto(base + 32L * SECTOR, probe, 0, MAGIC.length);
            if (matches(probe))
                return base;
        }
        throw new IOException("not an XDVDFS image: no MICROSOFT*XBOX*MEDIA at sector 32 "
            + "of any known partition base");
    }

    private static boolean matches(byte[] a) {
        for (int i = 0; i < MAGIC.length; i++)
            if (a[i] != MAGIC[i])
                return false;
        return true;
    }

    private byte[] read(long pos, int len) throws IOException {
        byte[] out = new byte[len];
        readInto(pos, out, 0, len);
        return out;
    }

    private void readInto(long pos, byte[] buf, int off, int len) throws IOException {
        ch.position(pos);
        ByteBuffer b = ByteBuffer.wrap(buf, off, len);
        while (b.hasRemaining()) {
            if (ch.read(b) < 0)
                throw new IOException("image truncated at " + pos);
        }
    }

    /** The entries of a directory, given from the root. "" or "a/b" alike. */
    public List<Entry> list(String path) throws IOException {
        long sector = rootSector, size = rootSize;
        if (path != null && !path.isEmpty()) {
            for (String part : path.replace('\\', '/').split("/")) {
                if (part.isEmpty())
                    continue;
                Entry found = null;
                for (Entry e : list(sector, size))
                    if (e.isDir() && e.name.equalsIgnoreCase(part)) {
                        found = e;
                        break;
                    }
                if (found == null)
                    throw new IOException("no such directory: " + path);
                sector = found.sector;
                size = found.size;
            }
        }
        return list(sector, size);
    }

    /** One file or directory by name, case-insensitively; null if absent. */
    public Entry find(String name, String path) throws IOException {
        for (Entry e : list(path))
            if (e.name.equalsIgnoreCase(name))
                return e;
        return null;
    }

    /* Entries form a binary tree, but they are also laid out contiguously
     * within each 2048-byte sector, so a linear walk with a per-sector resync
     * reads them all without following the left/right links. A malformed link
     * would otherwise drop a whole subtree silently, and losing a file quietly
     * is worse than reading a little more (xdvdfs.py makes the same choice). */
    public List<Entry> list(long sector, long size) throws IOException {
        if (size <= 0 || size > MAX_TABLE)
            throw new IOException("directory table size looks wrong: " + size);
        byte[] data = read(base + sector * SECTOR, (int) size);
        List<Entry> out = new ArrayList<>();
        for (int at = 0; at < data.length; at += SECTOR) {
            int end = Math.min(at + SECTOR, data.length);
            int off = at;
            while (off + 0x0E <= end) {
                int left = (data[off] & 0xFF) | ((data[off + 1] & 0xFF) << 8);
                int right = (data[off + 2] & 0xFF) | ((data[off + 3] & 0xFF) << 8);
                long sec = ((long) (data[off + 4] & 0xFF)) | ((long) (data[off + 5] & 0xFF) << 8)
                         | ((long) (data[off + 6] & 0xFF) << 16)
                         | ((long) (data[off + 7] & 0xFF) << 24);
                long sz = ((long) (data[off + 8] & 0xFF)) | ((long) (data[off + 9] & 0xFF) << 8)
                        | ((long) (data[off + 10] & 0xFF) << 16)
                        | ((long) (data[off + 11] & 0xFF) << 24);
                int attr = data[off + 12] & 0xFF, nlen = data[off + 13] & 0xFF;
                if ((left == 0xFFFF && right == 0xFFFF) || nlen == 0)
                    break;                     /* padding: end of this sector */
                if (off + 0x0E + nlen > end)
                    break;
                String name = new String(data, off + 0x0E, nlen, StandardCharsets.US_ASCII);
                out.add(new Entry(name, sec, sz, attr));
                off += (0x0E + nlen + 3) & ~3; /* 4-byte aligned */
            }
        }
        return out;
    }

    /** One file's bytes, streamed, so a 4 GB disc never has to fit in memory. */
    public void copy(Entry e, OutputStream out) throws IOException {
        byte[] buf = new byte[1 << 20];
        long pos = base + e.sector * SECTOR, left = e.size;
        ch.position(pos);
        ByteBuffer b = ByteBuffer.wrap(buf);
        while (left > 0) {
            b.clear();
            b.limit((int) Math.min(buf.length, left));
            int n = ch.read(b);
            if (n < 0)
                throw new IOException("image truncated reading " + e.name);
            out.write(buf, 0, n);
            left -= n;
        }
    }
}
