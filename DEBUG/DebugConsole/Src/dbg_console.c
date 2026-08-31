#include "dbg_console.h"
#include <string.h>

/* ----------------------------------------------------------------------------
 *  dbg_console — selectable debug-output backend
 *  ----------------------------------------------------------------------------
 *  Chosen at compile time via the DBG_CONSOLE_BACKEND macro:
 *
 *      - DBG_CONSOLE_BACKEND_SEMIHOST  (default)
 *            Writes go through ARM semihosting SYS_WRITE and appear in the
 *            IDE / VSCode Debug Console via the J-Link GDB server.
 *
 *      - DBG_CONSOLE_BACKEND_RTT
 *            Writes go to SEGGER RTT Buffer 0 (J-Link RTT Viewer).
 *
 *  A tiny, self-contained formatter (no newlib printf dependency) is used so
 *  there is no heavyweight pull-in and no _write-vs-semihost recursion risk.
 * -------------------------------------------------------------------------- */

 /* Scratch-buffer size for one formatted log line. */
#ifndef DBG_CONSOLE_FMT_BUF_SIZE
#define DBG_CONSOLE_FMT_BUF_SIZE 128
#endif

/* ===========================================================================
 *  Backend: semihosting
 * ======================================================================== */
#if (DBG_CONSOLE_BACKEND == DBG_CONSOLE_BACKEND_SEMIHOST)

/* SYS_WRITE (Angel SWI 0x05): write `len` bytes from `buf` to file handle
 * `fd`.  fd==1 is stdout, fd==2 is stderr — both are forwarded by the
 * J-Link GDB server to the IDE's Debug Console.
 *
 * Layout per the ARM semihosting spec:
 *   r0 = 0x05                          (SYS_WRITE operation)
 *   r1 = pointer to a 3-word argument block {fd, buf, len}
 *   Execute Angel SWI 0xAB (Cortex-M semihosting)                        */
static void semihost_write(int fd, const char *buf, int len)
{
    if (len <= 0) {
        return;
    }
    const uint32_t args[3] = {
        (uint32_t)fd,
        (uint32_t)(uintptr_t)buf,
        (uint32_t)len
    };
    register uint32_t r0 __asm("r0") = 0x05u;                       /* SYS_WRITE */
    register uint32_t r1 __asm("r1") = (uint32_t)(uintptr_t)args;   /* arg block */
    __asm volatile (
        "bkpt 0xAB"
        : "+r"(r0)
        : "r"(r1)
        : "memory"
    );
    (void)r0;
}

#endif /* SEMIHOST */

/* ===========================================================================
 *  Backend: SEGGER RTT
 * ======================================================================== */
#if (DBG_CONSOLE_BACKEND == DBG_CONSOLE_BACKEND_RTT)

#include "SEGGER_RTT.h"
extern int SEGGER_RTT_printf(unsigned BufferIndex, const char *sFormat, ...);

#endif /* RTT */

/* ===========================================================================
 *  Tiny formatter — stdio-independent
 * ======================================================================== */

/* Unsigned integer -> base-N string written BACKWARDS into buf_end.
 * Returns pointer to the first character of the string, which lives inside
 * [buf .. buf_end], not necessarily at buf itself.                         */
static char *fmt_utoa_reverse(unsigned int value, char *buf_end,
                              unsigned int base, int uppercase)
{
    static const char lower[] = "0123456789abcdef";
    static const char upper[] = "0123456789ABCDEF";
    const char *digits = uppercase ? upper : lower;
    char *p = buf_end;

    *p = '\0';
    if (value == 0u) {
        *(--p) = '0';
        return p;
    }
    while (value != 0u) {
        *(--p) = digits[value % base];
        value /= base;
    }
    return p;
}

/* Copy src[0..len-1] into dst, stopping at out_end; returns pointer to the
 * next free byte.                                                          */
static char *write_copy(char *dst, const char *src, int len, char *out_end)
{
    for (int i = 0; i < len && dst < out_end - 1; i++) {
        *dst++ = src[i];
    }
    return dst;
}

/* Core formatter. Writes at most out_len-1 chars into out, always NUL-
 * terminated. Returns the number of characters produced (excluding NUL).   */
static int tiny_format(char *out, int out_len, const char *fmt, va_list ap)
{
    char * const out_end = out + out_len;
    char *       wp      = out;

    if (out_len <= 0) {
        return 0;
    }

    for (const char *p = fmt; *p != '\0'; p++) {
        if (*p != '%') {
            if (wp < out_end - 1) *wp++ = *p;
            continue;
        }

        p++;                                           /* consume '%' */

        /* flags — left-justify is accepted but we render right-justified */
        if (*p == '-') { p++; }

        /* width */
        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        const char *segment = NULL;    /* start of the textual payload   */
        int         seg_len = 0;       /* its length                     */
        char        inbuf[13];         /* scratch for numeric conversions */
        memset(inbuf, 0, sizeof(inbuf));

        switch (*p) {
        case 'c': {
            inbuf[0] = (char)va_arg(ap, int);
            segment  = inbuf;
            seg_len  = 1;
            break;
        }
        case 's': {
            segment = va_arg(ap, const char *);
            if (segment == NULL) segment = "(null)";
            seg_len = (int)strlen(segment);
            break;
        }
        case 'p': {
            unsigned int v = (unsigned int)(uintptr_t)va_arg(ap, void *);
            char *r = fmt_utoa_reverse(v, inbuf + sizeof(inbuf) - 1, 16, 0);
            /* 0x + zero-padded 8 hex digits */
            char *q = inbuf;
            *q++ = '0'; *q++ = 'x';
            int digits = 0;
            for (char *rr = r; *rr; rr++) digits++;
            while (digits < 8) { *q++ = '0'; digits++; }
            for (char *rr = r; *rr; rr++) { *q++ = *rr; }
            *q = '\0';
            segment = inbuf;
            seg_len = (int)(q - inbuf);
            break;
        }
        case 'x': {
            unsigned int v = va_arg(ap, unsigned int);
            char *r = fmt_utoa_reverse(v, inbuf + sizeof(inbuf) - 1, 16, 0);
            segment = r;
            seg_len = (int)strlen(r);
            break;
        }
        case 'X': {
            unsigned int v = va_arg(ap, unsigned int);
            char *r = fmt_utoa_reverse(v, inbuf + sizeof(inbuf) - 1, 16, 1);
            segment = r;
            seg_len = (int)strlen(r);
            break;
        }
        case 'u': {
            unsigned int v = va_arg(ap, unsigned int);
            char *r = fmt_utoa_reverse(v, inbuf + sizeof(inbuf) - 1, 10, 0);
            segment = r;
            seg_len = (int)strlen(r);
            break;
        }
        case 'o': {
            unsigned int v = va_arg(ap, unsigned int);
            char *r = fmt_utoa_reverse(v, inbuf + sizeof(inbuf) - 1, 8, 0);
            segment = r;
            seg_len = (int)strlen(r);
            break;
        }
        case 'd':
        case 'i': {
            int v = va_arg(ap, int);
            if (v < 0) {
                /* magnitude as unsigned — note the two's-complement-safe cast so
                 * INT_MIN (whose magnitude doesn't fit in a signed int) works. */
                unsigned int uv = (unsigned int)(-(unsigned int)v);
                /* write the digits backwards into the END of a small scratch */
                char scratch[12];
                char *r = fmt_utoa_reverse(uv, scratch + sizeof(scratch) - 1, 10, 0);
                int digits = (int)strlen(r);
                /* r is most-significant-first (e.g. "124"). Prepend '-'
                 * by writing it one slot earlier in the same scratch.        */
                *(--r) = '-';
                segment = r;
                seg_len  = digits + 1;
            } else {
                unsigned int uv = (unsigned int)v;
                char *r = fmt_utoa_reverse(uv, inbuf + sizeof(inbuf) - 1, 10, 0);
                segment = r;
                seg_len = (int)strlen(r);
            }
            break;
        }
        case '%':
            if (wp < out_end - 1) *wp++ = '%';
            segment = NULL;
            break;
        default:
            if (wp < out_end - 1) *wp++ = '%';
            if (wp < out_end - 1) *wp++ = *p;
            segment = NULL;
            break;
        }

        if (segment == NULL) {
            continue;
        }

        /* right-justify to `width` with spaces */
        for (int pad = seg_len; pad < width; pad++) {
            if (wp < out_end - 1) *wp++ = ' ';
        }
        wp = write_copy(wp, segment, seg_len, out_end);
    }

    if (wp < out_end) *wp = '\0';
    else *(out_end - 1) = '\0';
    return (int)(wp - out);
}

/* ===========================================================================
 *  Public API
 * ======================================================================== */

void dbg_console_init(void)
{
#if (DBG_CONSOLE_BACKEND == DBG_CONSOLE_BACKEND_RTT)
    SEGGER_RTT_Init();
#endif
}

int dbg_console_printf(const char *fmt, ...)
{
    char buf[DBG_CONSOLE_FMT_BUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    int n = tiny_format(buf, (int)sizeof(buf), fmt, ap);
    va_end(ap);

    if (n <= 0) {
        return n;
    }

#if (DBG_CONSOLE_BACKEND == DBG_CONSOLE_BACKEND_SEMIHOST)
    semihost_write(1, buf, n);
#elif (DBG_CONSOLE_BACKEND == DBG_CONSOLE_BACKEND_RTT)
    (void)SEGGER_RTT_printf(0, "%s", buf);
#endif
    return n;
}

int dbg_console_vprintf(const char *fmt, va_list ap)
{
    char buf[DBG_CONSOLE_FMT_BUF_SIZE];
    int n = tiny_format(buf, (int)sizeof(buf), fmt, ap);

    if (n <= 0) {
        return n;
    }

#if (DBG_CONSOLE_BACKEND == DBG_CONSOLE_BACKEND_SEMIHOST)
    semihost_write(1, buf, n);
#elif (DBG_CONSOLE_BACKEND == DBG_CONSOLE_BACKEND_RTT)
    (void)SEGGER_RTT_printf(0, "%s", buf);
#endif
    return n;
}

void dbg_console_write(const char *p_data, int len)
{
    if (p_data == NULL || len <= 0) {
        return;
    }
#if (DBG_CONSOLE_BACKEND == DBG_CONSOLE_BACKEND_SEMIHOST)
    semihost_write(1, p_data, len);
#elif (DBG_CONSOLE_BACKEND == DBG_CONSOLE_BACKEND_RTT)
    SEGGER_RTT_Write(0, p_data, len);
#endif
}
