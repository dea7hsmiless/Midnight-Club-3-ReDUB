/* roam_addr.h -- an address that is not sitting in plain sight in a settings file.
 *
 * MC3_ROAM_HUB_IP=162.35.56.66 in <app>_env.txt is a line anyone who opens the
 * folder can read, and the server's address is the one thing in that file worth
 * keeping to yourself: everything else is a preference.
 *
 * "rd1:" followed by hex is that same text XORed with a fixed keystream, plus a
 * checksum byte so a mistyped character is refused instead of decoding to
 * nonsense. tools/redub_addr.py encodes and decodes it, and prints the line to
 * paste into the file.
 *
 * THIS IS OBFUSCATION, NOT ENCRYPTION, and it is important to be plain about
 * that. The client has to decode the address with no secret from the player, so
 * the same file holds everything needed to undo it, and the traffic is still
 * plain UDP on the wire. What it defeats is a glance at the file: a screenshot,
 * a stream, an install folder handed to someone, a settings file pasted into a
 * bug report. It is not a defence against anyone who decides to read it.
 *
 * A plain dotted quad or host name is still accepted and always will be; this is
 * an option, not a format change.
 *
 * Layout:  "rd1:" + hex(obf[0..n-1] || check)
 *   obf[i] = text[i] ^ keystream[i], keystream from xorshift32 seeded fixed
 *   check  = folded sum of the plaintext, so a typo is caught before use
 */
#ifndef ROAM_ADDR_H
#define ROAM_ADDR_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ROAM_ADDR_PREFIX "rd1:"
#define ROAM_ADDR_MAX    96          /* longest address worth carrying */

static inline uint8_t roam_addr_stream_byte(uint32_t *x)
{
    *x ^= *x << 13;
    *x ^= *x >> 17;
    *x ^= *x << 5;
    return (uint8_t)(*x >> 11);
}

static inline uint8_t roam_addr_check(const uint8_t *p, size_t n)
{
    uint8_t h = 0x5A;
    size_t i;
    for (i = 0; i < n; i++)
        h = (uint8_t)(h * 31u + p[i]);
    return h;
}

static inline int roam_addr_hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* 1 if this text is in the encoded form (regardless of whether it is valid). */
static inline int roam_addr_is_encoded(const char *s)
{
    return s && strncmp(s, ROAM_ADDR_PREFIX, 4) == 0;
}

/* 0 on success, -1 if the text is empty, too long, or the buffer too small. */
static inline int roam_addr_encode(const char *text, char *out, size_t cap)
{
    static const char hex[] = "0123456789abcdef";
    size_t n = text ? strlen(text) : 0, i;
    uint32_t x = 0x9E3779B9u;

    if (!n || n > ROAM_ADDR_MAX) return -1;
    if (cap < 4 + 2 * (n + 1) + 1) return -1;

    memcpy(out, ROAM_ADDR_PREFIX, 4);
    out += 4;
    for (i = 0; i < n; i++) {
        uint8_t b = (uint8_t)((uint8_t)text[i] ^ roam_addr_stream_byte(&x));
        *out++ = hex[b >> 4];
        *out++ = hex[b & 15];
    }
    {
        uint8_t c = roam_addr_check((const uint8_t *)text, n);
        *out++ = hex[c >> 4];
        *out++ = hex[c & 15];
    }
    *out = '\0';
    return 0;
}

/* 1 decoded, 0 not in the encoded form, -1 encoded but malformed.
 *
 * The -1 matters: a mistyped rd1: value must not fall through to a host-name
 * lookup that fails silently, which is the failure mode this project has paid
 * for twice already. The caller reports it. */
static inline int roam_addr_decode(const char *s, char *out, size_t cap)
{
    uint8_t buf[ROAM_ADDR_MAX + 1];
    uint32_t x = 0x9E3779B9u;
    size_t nh, nb, n, i;

    if (!roam_addr_is_encoded(s)) return 0;
    s += 4;
    nh = strlen(s);
    if (nh < 4 || (nh & 1) || nh > 2 * (ROAM_ADDR_MAX + 1)) return -1;
    nb = nh / 2;

    for (i = 0; i < nb; i++) {
        int hi = roam_addr_hexval(s[2 * i]), lo = roam_addr_hexval(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return -1;
        buf[i] = (uint8_t)((hi << 4) | lo);
    }

    n = nb - 1;                          /* the last byte is the checksum */
    if (!n) return -1;
    for (i = 0; i < n; i++)
        buf[i] = (uint8_t)(buf[i] ^ roam_addr_stream_byte(&x));
    if (roam_addr_check(buf, n) != buf[n]) return -1;
    if (n + 1 > cap) return -1;

    memcpy(out, buf, n);
    out[n] = '\0';
    return 1;
}

#endif /* ROAM_ADDR_H */
