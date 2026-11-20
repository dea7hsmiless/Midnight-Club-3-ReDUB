/* xdvdfs_extract.c -- read an Xbox XDVDFS disc image and write its files.
 *
 * Port of .portable/Xdvdfs.cs for the Linux AppImage, which ships no .NET and
 * no Python. Same bounded reader: it only reads an image and writes files
 * under a new directory, and refuses names or tables that could escape it.
 * Layout reference: XboxDev/extract-xiso.
 *
 * Build:  cc -O2 -o redub-xdvdfs xdvdfs_extract.c
 * Use:    redub-xdvdfs <image.iso|.xiso> <destination-directory>
 *
 * The destination must be new or empty. Progress goes to stdout as
 *   progress 0.437 Copying some/file
 * one line per percent step, which is what RecompLauncher's prepare step and
 * tools/appimage/AppRun read. Errors go to stderr with exit status 1.
 */
#define _FILE_OFFSET_BITS 64

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef _WIN32
/* The tool ships on Linux; this shim only lets it be built and tried on a
 * Windows dev machine. */
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

#define SECTOR_SIZE        2048u
#define MAX_TABLE_SIZE     (16u * 1024u * 1024u)
#define MAX_ENTRIES        200000u
#define MAX_DEPTH          48
#define SCAN_CHUNK         (1024u * 1024u)
#define COPY_CHUNK         (256u * 1024u)
#define MAGIC_LEN          20u
#define SUPPORTED_XBE_SIZE 5099520LL

static const char k_magic[MAGIC_LEN + 1] = "MICROSOFT*XBOX*MEDIA";
static const uint64_t k_region7 = 0x193d6b1219ccbd63ULL;
static const uint64_t k_europe  = 0x4438fd3ef9473ce0ULL;
static const uint64_t k_usa     = 0xb9644e6610d2f14aULL;

typedef struct {
    FILE *f;
    long long length;
    long long base;         /* the image's start (partition offset) */
    uint64_t total;         /* bytes of file data, first pass */
    uint64_t done;
    unsigned entries;
    unsigned ancestors[MAX_DEPTH + 1];
    int writing;
    const char *dest;
    uint8_t *copy;
} disc;

static void fail(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)u16(p) | ((uint32_t)u16(p + 2) << 16); }

static void read_at(disc *d, long long offset, void *buf, size_t n)
{
    if (offset < 0 || offset > d->length || (long long)n > d->length - offset)
        fail("Disc image is truncated or unreadable.");
    if (fseeko(d->f, (off_t)offset, SEEK_SET) != 0)
        fail("Disc image is truncated or unreadable.");
    if (fread(buf, 1, n, d->f) != n)
        fail("Disc image is truncated or unreadable.");
}

static int magic_at(const uint8_t *p, size_t offset)
{
    return memcmp(p + offset, k_magic, MAGIC_LEN) == 0;
}

/* A volume header: the magic at both ends of one sector, root sector and table
 * size at +20/+24, with the partition based 32 sectors before it. */
static int volume_header_at(disc *d, long long descriptor, uint32_t *root, uint32_t *size)
{
    uint8_t header[SECTOR_SIZE];
    long long image_base, root_start;

    *root = 0;
    *size = 0;
    if (descriptor < 32LL * SECTOR_SIZE || descriptor > d->length - SECTOR_SIZE)
        return 0;
    if (fseeko(d->f, (off_t)descriptor, SEEK_SET) != 0)
        return 0;
    if (fread(header, 1, sizeof header, d->f) != sizeof header)
        return 0;
    if (!magic_at(header, 0) || !magic_at(header, sizeof header - MAGIC_LEN))
        return 0;
    *root = u32(header + 20);
    *size = u32(header + 24);
    image_base = descriptor - 32LL * SECTOR_SIZE;
    root_start = image_base + (long long)*root * SECTOR_SIZE;
    if (*size == 0 || *size > MAX_TABLE_SIZE || image_base > d->length)
        return 0;
    if (root_start > d->length || (long long)*size > d->length - root_start)
        return 0;
    d->base = image_base;
    return 1;
}

/* Full-disc dumps put the game partition at several offsets; search for the
 * paired signature when the common ones do not fit. */
static int find_volume(disc *d, uint32_t *root, uint32_t *size)
{
    static const long long common[] = {
        32LL * SECTOR_SIZE,
        0xFD90000LL + 32LL * SECTOR_SIZE,
        0x2080000LL + 32LL * SECTOR_SIZE,
    };
    uint8_t *buffer;
    long long offset = 0;
    size_t carry = 0;

    for (size_t i = 0; i < sizeof common / sizeof common[0]; i++)
        if (volume_header_at(d, common[i], root, size))
            return 1;

    buffer = malloc(SCAN_CHUNK + SECTOR_SIZE);
    if (!buffer)
        fail("Out of memory while scanning the disc image.");

    while (offset < d->length) {
        size_t want = (size_t)((d->length - offset > (long long)SCAN_CHUNK)
                               ? SCAN_CHUNK : d->length - offset);
        size_t got;
        if (fseeko(d->f, (off_t)offset, SEEK_SET) != 0)
            break;
        got = fread(buffer + carry, 1, want, d->f);
        if (got == 0)
            break;
        {
            size_t available = carry + got, search = 0;
            while (search + MAGIC_LEN <= available) {
                uint8_t *at = memchr(buffer + search, k_magic[0], available - search - (MAGIC_LEN - 1));
                size_t pos;
                if (!at)
                    break;
                pos = (size_t)(at - buffer);
                if (memcmp(at, k_magic, MAGIC_LEN) == 0) {
                    long long absolute = offset - (long long)carry + (long long)pos;
                    if (volume_header_at(d, absolute, root, size)) {
                        free(buffer);
                        return 1;
                    }
                }
                search = pos + 1;
            }
            carry = available < SECTOR_SIZE - 1 ? available : SECTOR_SIZE - 1;
            memmove(buffer, buffer + available - carry, carry);
            offset += (long long)got;
        }
    }
    free(buffer);
    return 0;
}

static void make_dir(const char *path)
{
    if (mkdir(path, 0777) != 0 && errno != EEXIST)
        fail("Cannot create game folder (check free space and permissions).");
}

static void make_dir_strict(const char *path)
{
    if (mkdir(path, 0777) != 0)
        fail("Cannot create directory (duplicate name or storage error).");
}

static void write_file(disc *d, long long start, uint32_t bytes, const char *relative,
                       const char *destination)
{
    int fd = open(destination, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0)
        fail("Cannot create game file (duplicate name or storage error).");

    long long position = start;
    uint32_t remain = bytes;
    while (remain > 0) {
        size_t chunk = remain > COPY_CHUNK ? COPY_CHUNK : remain;
        size_t written = 0;
        read_at(d, position, d->copy, chunk);
        while (written < chunk) {
            ssize_t n = write(fd, d->copy + written, chunk - written);
            if (n <= 0) {
                close(fd);
                fail("Read/write failed. Check free storage.");
            }
            written += (size_t)n;
        }
        position += (long long)chunk;
        remain -= (uint32_t)chunk;
        d->done += chunk;
        if (d->total) {
            printf("progress %.3f %s\n", (double)d->done / (double)d->total, relative);
            fflush(stdout);
        }
    }
    if (close(fd) != 0)
        fail("Cannot finish writing game file.");
}

static void walk_table(disc *d, uint32_t sector, uint32_t size, const char *dirpath, int depth);

static void process_entry(disc *d, uint32_t sector, uint32_t bytes, uint8_t attributes,
                          const char *relative, const char *destination, long long start,
                          const char *dirpath, int depth)
{
    if (attributes & 0x10) {                       /* directory */
        if (d->writing)
            make_dir_strict(destination);
        if (bytes != 0)
            walk_table(d, sector, bytes, relative, depth + 1);
    } else if (d->writing) {
        write_file(d, start, bytes, relative, destination);
    } else {
        d->total += bytes;
        if (d->total > (uint64_t)d->length * 2ULL)
            fail("Disc contains excessive overlapping data.");
    }
    (void)dirpath;
}

/* One directory table: the left/current/right tree, walked with an explicit
 * stack (real discs are deep) in left, current, right order as the reference
 * reader does. `seen` rejects cycles inside the table. */
static void walk_table(disc *d, uint32_t sector, uint32_t size, const char *dirpath, int depth)
{
    uint32_t *stack;
    size_t depth_used = 0, capacity;
    uint8_t *table, *seen;

    if (depth > MAX_DEPTH || size > MAX_TABLE_SIZE || size < 14)
        fail("Invalid or excessively nested directory.");
    for (int i = 0; i < depth; i++)
        if (d->ancestors[i] == sector)
            fail("Cyclic XDVDFS directories.");
    d->ancestors[depth] = sector;

    table = malloc(size);
    seen = calloc((size + 3u) / 4u, 1);
    capacity = size / 4u + 16u;
    stack = malloc(capacity * sizeof *stack);
    if (!table || !seen || !stack)
        fail("Insufficient memory for the disc directory.");
    read_at(d, d->base + (long long)sector * SECTOR_SIZE, table, size);

    /* Node offsets are visited in reverse push order so the first entry read
     * is the leftmost, as the recursive reference does. */
    stack[depth_used++] = 0;
    while (depth_used) {
        uint32_t offset = stack[--depth_used];
        uint32_t left, right, child_sector, child_bytes;
        uint8_t attributes, name_length;
        char name[256], relative[4096], destination[4096];

        if (offset > size || size - offset < 14 || (offset & 3u) != 0)
            fail("Invalid XDVDFS directory entry.");
        if (seen[offset / 4u] != 0)
            fail("Cyclic XDVDFS directory tree.");
        seen[offset / 4u] = 1;
        if (++d->entries > MAX_ENTRIES)
            fail("Too many disc entries.");

        left = u16(table + offset);
        right = u16(table + offset + 2);
        child_sector = u32(table + offset + 4);
        child_bytes = u32(table + offset + 8);
        attributes = table[offset + 12];
        name_length = table[offset + 13];
        if (name_length == 0 || (uint32_t)name_length > size - offset - 14)
            fail("Invalid disc filename length.");
        for (unsigned i = 0; i < name_length; i++) {
            uint8_t c = table[offset + 14 + i];
            if (c < 32 || c == '/' || c == '\\' || c == ':')
                fail("Unsafe disc filename.");
        }
        memcpy(name, table + offset + 14, name_length);
        name[name_length] = 0;
        if (name[strlen(name) - 1] == '.' || name[strlen(name) - 1] == ' ' ||
            strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            fail("Unsafe disc filename.");

        if (dirpath[0])
            snprintf(relative, sizeof relative, "%s/%s", dirpath, name);
        else
            snprintf(relative, sizeof relative, "%s", name);
        snprintf(destination, sizeof destination, "%s/%s", d->dest, relative);
        if (strlen(relative) >= sizeof relative - 1 || strlen(destination) >= sizeof destination - 1)
            fail("Disc path is too long.");

        if ((long long)child_sector > (d->length - d->base) / SECTOR_SIZE)
            fail("Disc file extends beyond image.");
        {
            long long start = d->base + (long long)child_sector * SECTOR_SIZE;
            if ((long long)child_bytes > d->length - start)
                fail("Disc file extends beyond image.");
            if (right != 0) {
                if (depth_used + 2 >= capacity)
                    fail("Invalid XDVDFS directory tree.");
                stack[depth_used++] = right * 4u;
            }
            process_entry(d, child_sector, child_bytes, attributes, relative, destination,
                          start, dirpath, depth);
            if (left != 0) {
                if (depth_used + 1 >= capacity)
                    fail("Invalid XDVDFS directory tree.");
                stack[depth_used++] = left * 4u;
            }
        }
    }
    free(stack);
    free(table);
    free(seen);
}

/* FNV-1a 64 over default.xbe, against the editions this lift supports. */
static int verify_xbe(const char *directory)
{
    char path[4096];
    unsigned char buffer[65536];
    uint64_t hash = 14695981039346656037ULL;
    long long size = 0;
    FILE *f;
    size_t got;

    snprintf(path, sizeof path, "%s/default.xbe", directory);
    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "default.xbe is missing. Choose an original Xbox Remix disc.\n");
        return 0;
    }
    while ((got = fread(buffer, 1, sizeof buffer, f)) > 0) {
        size += (long long)got;
        for (size_t i = 0; i < got; i++) {
            hash ^= buffer[i];
            hash *= 1099511628211ULL;
        }
    }
    fclose(f);
    if (size != SUPPORTED_XBE_SIZE) {
        fprintf(stderr, "Unsupported or damaged default.xbe. This build supports Xbox DUB Edition Remix (region7, USA, Europe).\n");
        return 0;
    }
    if (hash != k_region7 && hash != k_europe && hash != k_usa) {
        fprintf(stderr, "Unsupported or damaged default.xbe. This build supports Xbox DUB Edition Remix (region7, USA, Europe).\n");
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    disc d;
    uint32_t root = 0, size = 0;
    const char *image, *dest;
    size_t dest_len;

    if (argc != 3) {
        fprintf(stderr, "use: %s <image.iso|xiso> <destination-directory>\n", argv[0]);
        return 2;
    }
    image = argv[1];
    dest = argv[2];
    dest_len = strlen(dest);
    if (dest_len == 0 || dest_len >= 4000) {
        fprintf(stderr, "The game folder path is not valid.\n");
        return 1;
    }

    /* The destination must be new or empty: no stale files, no surprises. */
    {
        struct stat st;
        if (stat(dest, &st) == 0) {
            DIR *dir;
            struct dirent *entry;
            int count = 0;
            if (!S_ISDIR(st.st_mode)) {
                fprintf(stderr, "The game folder path is not a folder.\n");
                return 1;
            }
            dir = opendir(dest);
            if (!dir) {
                fprintf(stderr, "Cannot read the game folder path.\n");
                return 1;
            }
            while ((entry = readdir(dir)) != NULL)
                if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
                    count++;
            closedir(dir);
            if (count != 0) {
                fprintf(stderr, "The game folder must be new or empty.\n");
                return 1;
            }
        }
    }

    memset(&d, 0, sizeof d);
    d.dest = dest;
    d.f = fopen(image, "rb");
    if (!d.f) {
        fprintf(stderr, "Cannot open selected ISO.\n");
        return 1;
    }
    if (fseeko(d.f, 0, SEEK_END) != 0) {
        fprintf(stderr, "Disc image is truncated or unreadable.\n");
        return 1;
    }
    d.length = (long long)ftello(d.f);
    if (d.length <= 0) {
        fprintf(stderr, "Disc image is truncated or unreadable.\n");
        return 1;
    }
    if (!find_volume(&d, &root, &size)) {
        fprintf(stderr, "No Xbox XDVDFS volume found. The image may use raw 2352-byte sectors, be compressed, or not be an Xbox ISO.\n");
        return 1;
    }

    /* First pass: total bytes, for progress. */
    d.writing = 0;
    walk_table(&d, root, size, "", 0);

    d.copy = malloc(COPY_CHUNK);
    if (!d.copy) {
        fprintf(stderr, "Insufficient copy memory.\n");
        return 1;
    }
    make_dir(dest);
    d.writing = 1;
    d.entries = 0;
    memset(d.ancestors, 0, sizeof d.ancestors);
    walk_table(&d, root, size, "", 0);

    if (!verify_xbe(dest))
        return 1;
    printf("done %llu\n", (unsigned long long)d.done);
    fflush(stdout);
    free(d.copy);
    fclose(d.f);
    return 0;
}
