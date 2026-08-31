#ifndef __DBG_CONSOLE_H__
#define __DBG_CONSOLE_H__

#include <stdint.h>
#include <stdarg.h>

/*
 * dbg_console.h
 * ----------------------------------------------------------------------------
 * Debug console output backend selector.
 *
 * Two backends are available:
 *   1. SEMIHOST  : ARM semihosting SYS_WRITE -> goes to the IDE/Debug Console
 *                  through the J-Link GDB server. No extra wire needed.
 *   2. RTT       : SEGGER RTT Buffer 0       -> goes to J-Link RTT Viewer.
 *
 * Selection is done via the DBG_CONSOLE_BACKEND compile switch:
 *   define DBG_CONSOLE_BACKEND_SEMIHOST  -> semihosting (debug console)
 *   define DBG_CONSOLE_BACKEND_RTT       -> SEGGER RTT
 *
 * Default is SEMIHOST so logs land in the Debug Console out of the box.
 * To switch to RTT, compile with -DDBG_CONSOLE_BACKEND_RTT.
 */

/* Backend selectors ------------------------------------------------------ */
#define DBG_CONSOLE_BACKEND_SEMIHOST    1
#define DBG_CONSOLE_BACKEND_RTT         2

/* Default to SEMIHOST if the user did not pick one at compile time -------- */
#if !defined(DBG_CONSOLE_BACKEND)
#define DBG_CONSOLE_BACKEND             DBG_CONSOLE_BACKEND_RTT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * dbg_console_init : optional one-time init (e.g. RTT buffer setup).
 *                    Semihosting needs no init but the call is harmless.
 */
void dbg_console_init(void);

/*
 * dbg_console_printf / dbg_console_vprintf
 *   printf()-style formatting into the selected backend.
 */
int  dbg_console_printf(const char *fmt, ...);
int  dbg_console_vprintf(const char *fmt, va_list ap);

/*
 * dbg_console_write
 *   Raw buffer write (no formatting). Used for hex / raw byte dumps.
 */
void dbg_console_write(const char *p_data, int len);

#ifdef __cplusplus
}
#endif

#endif /* __DBG_CONSOLE_H__ */
