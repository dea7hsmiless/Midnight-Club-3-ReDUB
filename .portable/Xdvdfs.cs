/* XDVDFS reader for the ReDUB launcher.
 * Port of src/ios_xdvdfs.c from mcdub3-nx (itself an independent bounded
 * reader; layout reference: XboxDev/extract-xiso). It only reads an Xbox
 * disc image and writes its files under a new staging directory. No
 * executable code is produced at install time. */
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading;

namespace ReDUB {
public sealed class XdvdfsException : IOException {
    public XdvdfsException(string message) : base(message) {}
    public XdvdfsException(string message, Exception inner) : base(message, inner) {}
}
public static class Xdvdfs {
    public const int SectorSize = 2048;
    const int MaximumTableSize = 16 * 1024 * 1024;
    const int MaximumEntries = 200000;
    const int MaximumDepth = 48;
    const int ScanChunkSize = 1024 * 1024;
    const int CopyChunkSize = 256 * 1024;
    const int VolumeMagicLength = 20;
    const string VolumeMagic = "MICROSOFT*XBOX*MEDIA";
    static readonly byte[] VolumeMagicBytes = Encoding.ASCII.GetBytes(VolumeMagic);
    const long SupportedXbeSize = 5099520;
    const ulong Region7XbeHash = 0x193d6b1219ccbd63UL;
    const ulong EuropeXbeHash = 0x4438fd3ef9473ce0UL;
    const ulong UsaXbeHash = 0xb9644e6610d2f14aUL;
    const string MissingXbeMessage = "default.xbe is missing. Choose an original Xbox Remix disc.";
    const string UnsupportedXbeMessage = "Unsupported or damaged default.xbe. This build supports Xbox DUB Edition Remix (region7, USA, Europe).";

    sealed class Disc {
        public Stream Stream;
        public long Length;
        public long Base;
        public ulong Total;
        public ulong Done;
        public int Entries;
        public byte[] Copy;
        public readonly uint[] Ancestors = new uint[MaximumDepth + 1];
        public bool Writing;
        public string Destination;
        public Action<long, long, string> Progress;
        public CancellationToken Cancellation;
    }

    static XdvdfsException Error(string message) {return new XdvdfsException(message);}
    static XdvdfsException Error(string message, Exception inner) {return new XdvdfsException(message, inner);}
    static long Clamp(ulong value) {return value > (ulong)long.MaxValue ? long.MaxValue : (long)value;}
    static ushort U16(byte[] p, int offset) {return (ushort)(p[offset] | (p[offset + 1] << 8));}
    static uint U32(byte[] p, int offset) {return (uint)(U16(p, offset) | ((uint)U16(p, offset + 2) << 16));}

    static void ReadAt(Disc d, long offset, byte[] buffer, int count) {
        if (offset < 0 || offset > d.Length || count < 0 || count > d.Length - offset)
            throw Error("Disc image is truncated or unreadable.");
        try {
            d.Stream.Position = offset;
            int done = 0;
            while (done < count) {
                int got = d.Stream.Read(buffer, done, count - done);
                if (got <= 0) throw Error("Disc image is truncated or unreadable.");
                done += got;
            }
        } catch (XdvdfsException) {
            throw;
        } catch (IOException ex) {
            throw Error("Disc image is truncated or unreadable.", ex);
        } catch (NotSupportedException ex) {
            throw Error("Disc image is truncated or unreadable.", ex);
        }
    }

    static bool VolumeHeaderAt(Disc d, long descriptor, out uint root, out uint size) {
        root = 0;
        size = 0;
        d.Cancellation.ThrowIfCancellationRequested();
        if (descriptor < 32L * SectorSize || descriptor > d.Length - SectorSize) return false;
        byte[] header = new byte[SectorSize];
        try {ReadAt(d, descriptor, header, header.Length);}
        catch (IOException) {return false;}
        if (!MagicAt(header, 0) || !MagicAt(header, header.Length - VolumeMagicLength)) return false;
        uint r = U32(header, 20), s = U32(header, 24);
        long imageBase = descriptor - 32L * SectorSize;
        long rootOffset = (long)r * SectorSize;
        if (s == 0 || s > MaximumTableSize || imageBase > d.Length || rootOffset > d.Length - imageBase) return false;
        long rootStart = imageBase + rootOffset;
        if (s > d.Length - rootStart) return false;
        d.Base = imageBase;
        root = r;
        size = s;
        return true;
    }

    static bool MagicAt(byte[] buffer, int offset) {
        for (int i = 0; i < VolumeMagicLength; i++) if (buffer[offset + i] != VolumeMagicBytes[i]) return false;
        return true;
    }

    static int FindMagic(byte[] buffer, int start, int available) {
        for (int i = start; i + VolumeMagicLength <= available; i++) {
            if (buffer[i] != VolumeMagicBytes[0]) continue;
            if (MagicAt(buffer, i)) return i;
        }
        return -1;
    }

    /* Full-disc dumps can place the game partition at offsets other than the
     * handful used by common Redump/XISO layouts. Search for the paired XDVDFS
     * volume signature instead of rejecting a legitimate disc based on its
     * partition start. The overlap preserves signatures across chunk edges. */
    static bool FindVolume(Disc d, out uint root, out uint size) {
        long[] common = {32L * SectorSize, 0xFD90000L + 32L * SectorSize, 0x2080000L + 32L * SectorSize};
        for (int i = 0; i < common.Length; i++)
            if (VolumeHeaderAt(d, common[i], out root, out size)) return true;
        root = 0;
        size = 0;
        byte[] buffer = new byte[ScanChunkSize + SectorSize];
        long offset = 0;
        int carry = 0;
        while (offset < d.Length) {
            d.Cancellation.ThrowIfCancellationRequested();
            int want = d.Length - offset > ScanChunkSize ? ScanChunkSize : (int)(d.Length - offset);
            int got;
            try {
                d.Stream.Position = offset;
                got = d.Stream.Read(buffer, carry, want);
            } catch (IOException) {
                break;
            }
            if (got <= 0) break;
            int available = carry + got;
            int search = 0;
            while (search < available) {
                int at = FindMagic(buffer, search, available);
                if (at < 0) break;
                long absolute = offset - carry + at;
                if (VolumeHeaderAt(d, absolute, out root, out size)) return true;
                search = at + 1;
            }
            carry = available < 2047 ? available : 2047;
            Buffer.BlockCopy(buffer, available - carry, buffer, 0, carry);
            offset += got;
        }
        return false;
    }

    /* One entry in the explicit tree walk: either a directory node to read, or
     * the file/directory action to run after its left subtree. */
    sealed class Frame {
        public bool Entry;
        public byte[] Table;
        public byte[] Seen;
        public uint Size;
        public uint Offset;
        public string Path;
        public int Depth;
        public uint Sector;
        public uint Bytes;
        public byte Attributes;
        public string Relative;
        public string Destination;
        public long Start;
        public static Frame Node(byte[] table, byte[] seen, uint size, uint offset, string path, int depth) {
            return new Frame {Table = table, Seen = seen, Size = size, Offset = offset, Path = path, Depth = depth};
        }
        public static Frame Item(uint sector, uint bytes, byte attributes, string relative, string destination, long start, string path, int depth) {
            return new Frame {Entry = true, Sector = sector, Bytes = bytes, Attributes = attributes, Relative = relative, Destination = destination, Start = start, Path = path, Depth = depth};
        }
    }

    /* The reference reader recursed through the left/right tree and capped the
     * recursion at 48 calls / 128 levels. Real discs can be much deeper: the
     * Remix's flash directory reaches 754 levels. Walk the same left, current,
     * right order with an explicit stack instead; the seen bitmap, entry cap
     * and directory-depth cap still bound the work, without a stack overflow. */
    static void WalkTable(Disc d, byte[] table, byte[] seen, uint size, string path, int depth) {
        var stack = new Stack<Frame>();
        stack.Push(Frame.Node(table, seen, size, 0, path, depth));
        while (stack.Count != 0) {
            d.Cancellation.ThrowIfCancellationRequested();
            Frame frame = stack.Pop();
            if (frame.Entry) {
                ProcessEntry(d, frame);
                continue;
            }
            uint offset = frame.Offset;
            if (offset > size || size - offset < 14 || (offset & 3) != 0)
                throw Error("Invalid XDVDFS directory entry.");
            if (seen[offset / 4] != 0) throw Error("Cyclic XDVDFS directory tree.");
            seen[offset / 4] = 1;
            if (++d.Entries > MaximumEntries) throw Error("Too many disc entries.");
            int at = (int)offset;
            uint left = U16(table, at), right = U16(table, at + 2);
            uint sector = U32(table, at + 4), bytes = U32(table, at + 8);
            byte attributes = table[at + 12];
            int nameLength = table[at + 13];
            if (nameLength == 0 || (uint)nameLength > size - offset - 14)
                throw Error("Invalid disc filename length.");
            for (int i = 0; i < nameLength; i++) {
                byte c = table[at + 14 + i];
                if (c < 32 || c == (byte)'/' || c == (byte)'\\' || c == (byte)':')
                    throw Error("Unsafe disc filename.");
            }
            string name = Encoding.Default.GetString(table, at + 14, nameLength);
            if (path.Length == 0 && string.Equals(name, "default.xbe", StringComparison.OrdinalIgnoreCase))
                name = "default.xbe";
            if (name == "." || name == ".." || name[name.Length - 1] == '.' || name[name.Length - 1] == ' ')
                throw Error("Unsafe disc filename.");
            string relative = path.Length == 0 ? name : path + "/" + name;
            string destination = Path.Combine(d.Destination, relative.Replace('/', Path.DirectorySeparatorChar));
            string prefix = d.Destination.EndsWith(Path.DirectorySeparatorChar.ToString(), StringComparison.Ordinal)
                ? d.Destination : d.Destination + Path.DirectorySeparatorChar;
            if (!destination.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) ||
                relative.Length >= 2048 || destination.Length >= 4096)
                throw Error("Disc path is too long.");
            long available = d.Length - d.Base;
            if ((long)sector > available / SectorSize) throw Error("Disc file extends beyond image.");
            long start = d.Base + (long)sector * SectorSize;
            if (bytes > d.Length - start) throw Error("Disc file extends beyond image.");

            if (right != 0) stack.Push(Frame.Node(table, seen, size, right * 4u, path, depth));
            stack.Push(Frame.Item(sector, bytes, attributes, relative, destination, start, path, depth));
            if (left != 0) stack.Push(Frame.Node(table, seen, size, left * 4u, path, depth));
        }
    }

    static void ProcessEntry(Disc d, Frame frame) {
        if ((frame.Attributes & 0x10) != 0) {
            if (d.Writing) {
                if (Directory.Exists(frame.Destination)) throw Error("Cannot create directory (duplicate name or storage error).");
                try {Directory.CreateDirectory(frame.Destination);}
                catch (IOException ex) {throw Error("Cannot create directory (duplicate name or storage error).", ex);}
                catch (UnauthorizedAccessException ex) {throw Error("Cannot create directory (duplicate name or storage error).", ex);}
            }
            if (frame.Bytes != 0) Table(d, frame.Sector, frame.Bytes, frame.Relative, frame.Depth + 1);
        } else if (d.Writing) {
            WriteFile(d, frame.Start, frame.Bytes, frame.Relative, frame.Destination);
        } else {
            d.Total += frame.Bytes;
            if (d.Total > (ulong)d.Length * 2UL) throw Error("Disc contains excessive overlapping data.");
        }
    }

    /* A fresh staging directory and exclusive creation prohibit aliases and
     * overwrites, matching the iOS reader's fopen(..., "wbx"). */
    static void WriteFile(Disc d, long start, uint bytes, string relative, string destination) {
        FileStream output;
        try {
            output = new FileStream(destination, FileMode.CreateNew, FileAccess.Write, FileShare.None, 65536, FileOptions.SequentialScan);
        } catch (IOException ex) {
            throw Error("Cannot create game file (duplicate name or storage error).", ex);
        } catch (UnauthorizedAccessException ex) {
            throw Error("Cannot create game file (duplicate name or storage error).", ex);
        }
        bool failed = false;
        try {
            long position = start;
            long remain = bytes;
            while (remain > 0) {
                d.Cancellation.ThrowIfCancellationRequested();
                int chunk = remain > CopyChunkSize ? CopyChunkSize : (int)remain;
                ReadAt(d, position, d.Copy, chunk);
                try {output.Write(d.Copy, 0, chunk);}
                catch (IOException ex) {throw Error("Read/write failed. Check free storage.", ex);}
                position += chunk;
                remain -= chunk;
                d.Done += (ulong)chunk;
                if (d.Progress != null) d.Progress(Clamp(d.Done), Clamp(d.Total), relative);
            }
            output.Flush();
        } catch (XdvdfsException) {
            failed = true;
            throw;
        } catch (IOException ex) {
            failed = true;
            throw Error("Read/write failed. Check free storage.", ex);
        } finally {
            try {output.Dispose();}
            catch (IOException ex) {if (!failed) throw Error("Cannot finish writing game file.", ex);}
        }
    }

    static void Table(Disc d, uint sector, uint size, string path, int depth) {
        if (depth > MaximumDepth || size > MaximumTableSize || size < 14)
            throw Error("Invalid or excessively nested directory.");
        for (int i = 0; i < depth; i++) if (d.Ancestors[i] == sector) throw Error("Cyclic XDVDFS directories.");
        d.Ancestors[depth] = sector;
        byte[] table, seen;
        try {
            table = new byte[(int)size];
            seen = new byte[(int)((size + 3) / 4)];
        } catch (OutOfMemoryException ex) {
            throw Error("Insufficient memory for disc directory.", ex);
        }
        ReadAt(d, d.Base + (long)sector * SectorSize, table, (int)size);
        WalkTable(d, table, seen, size, path, depth);
    }

    /* FNV-1a 64 over the whole default.xbe, checked against the editions the
     * Remix lift supports (config/remix/xbe_editions.h). */
    public static bool TryVerify(string directory, out string edition, out string error) {
        edition = null;
        error = null;
        FileStream stream;
        try {
            stream = new FileStream(Path.Combine(directory, "default.xbe"), FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.SequentialScan);
        } catch (IOException) {
            error = MissingXbeMessage;
            return false;
        } catch (UnauthorizedAccessException) {
            error = MissingXbeMessage;
            return false;
        } catch (ArgumentException) {
            error = MissingXbeMessage;
            return false;
        } catch (NotSupportedException) {
            error = MissingXbeMessage;
            return false;
        }
        ulong hash = 14695981039346656037UL;
        long size = 0;
        bool readError = false;
        try {
            byte[] buffer = new byte[65536];
            int got;
            while ((got = stream.Read(buffer, 0, buffer.Length)) > 0) {
                size += got;
                for (int i = 0; i < got; i++) {
                    unchecked {
                        hash ^= buffer[i];
                        hash *= 1099511628211UL;
                    }
                }
            }
        } catch (IOException) {
            readError = true;
        } finally {
            try {stream.Dispose();}
            catch (IOException) {readError = true;}
        }
        if (readError || size != SupportedXbeSize) {
            error = UnsupportedXbeMessage;
            return false;
        }
        if (hash == Region7XbeHash) edition = "region7";
        else if (hash == EuropeXbeHash) edition = "eur";
        else if (hash == UsaXbeHash) edition = "usa";
        else {
            error = UnsupportedXbeMessage;
            return false;
        }
        return true;
    }

    public static string Verify(string directory) {
        string edition, error;
        if (!TryVerify(directory, out edition, out error)) throw Error(error);
        return edition;
    }

    public static void Extract(string imagePath, string destinationDirectory, Action<long, long, string> progress) {
        Extract(imagePath, destinationDirectory, progress, CancellationToken.None);
    }

    public static void Extract(string imagePath, string destinationDirectory, Action<long, long, string> progress, CancellationToken cancellationToken) {
        if (imagePath == null) throw new ArgumentNullException("imagePath");
        if (destinationDirectory == null) throw new ArgumentNullException("destinationDirectory");
        string destination;
        try {destination = Path.GetFullPath(destinationDirectory);}
        catch (Exception ex) {throw Error("The game folder path is not valid.", ex);}
        if (File.Exists(destination)) throw Error("The game folder path is not a folder.");
        if (Directory.Exists(destination) && Directory.GetFileSystemEntries(destination).Length != 0)
            throw Error("The game folder must be new or empty.");
        if (destination.Length > 3) destination = destination.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        var d = new Disc {Destination = destination, Progress = progress, Cancellation = cancellationToken};
        try {
            d.Stream = new FileStream(imagePath, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.RandomAccess);
        } catch (IOException ex) {
            throw Error("Cannot open selected ISO.", ex);
        } catch (UnauthorizedAccessException ex) {
            throw Error("Cannot open selected ISO.", ex);
        } catch (ArgumentException ex) {
            throw Error("Cannot open selected ISO.", ex);
        } catch (NotSupportedException ex) {
            throw Error("Cannot open selected ISO.", ex);
        }
        try {
            try {d.Length = d.Stream.Length;}
            catch (IOException ex) {throw Error("Disc image is truncated or unreadable.", ex);}
            uint root, size;
            if (!FindVolume(d, out root, out size))
                throw Error("No Xbox XDVDFS volume found. The image may use raw 2352-byte sectors, be compressed, or not be an Xbox ISO.");
            Table(d, root, size, "", 0);
            try {d.Copy = new byte[CopyChunkSize];}
            catch (OutOfMemoryException ex) {throw Error("Insufficient copy memory.", ex);}
            d.Writing = true;
            d.Entries = 0;
            try {Directory.CreateDirectory(destination);}
            catch (IOException ex) {throw Error("Cannot create the game folder. Check free storage and permissions.", ex);}
            catch (UnauthorizedAccessException ex) {throw Error("Cannot create the game folder. Check free storage and permissions.", ex);}
            Table(d, root, size, "", 0);
            Verify(destination);
        } finally {
            d.Stream.Dispose();
        }
    }
}
}
