#ifndef _ZEBRA_LOG_H
#define _ZEBRA_LOG_H

#include <stdarg.h>
#include <stdio.h>
/* #define assert(x) */

/* from syslog.h */
#define LOG_EMERG       0       /* system is unusable */
#define LOG_ALERT       1       /* action must be taken immediately */
#define LOG_CRIT        2       /* critical conditions */
#define LOG_ERR         3       /* error conditions */
#define LOG_WARNING     4       /* warning conditions */
#define LOG_NOTICE      5       /* normal but significant condition */
#define LOG_INFO        6       /* informational */
#define LOG_DEBUG       7       /* debug-level messages */

#define ZLOG_NOLOG              0x00
#define ZLOG_FILE               0x01
#define ZLOG_SYSLOG             0x02
#define ZLOG_STDOUT             0x04
#define ZLOG_STDERR             0x08
#define ZLOG_LOGPRIO            (0x01 << 8)
#define ZLOG_LOGTIME            (0x02 << 8)
#define ZLOG_LOGFILE            (0x04 << 8)
#define ZLOG_LOGCR              (0x08 << 8)

#define ZLOG_NOLOG_INDEX        0
#define ZLOG_FILE_INDEX         1
#define ZLOG_SYSLOG_INDEX       2
#define ZLOG_STDOUT_INDEX       3
#define ZLOG_STDERR_INDEX       4
#define ZLOG_MAX_INDEX          5

/* ZLOG FACILITIES */
#ifndef ZLOG_MODULE
#define ZLOG_MODULE 0xFFFFFFFF
#endif

/* log constants */
#define ZLOGMOD_MEMPOOL 0x01
#define ZLOGMOD_MEMBUF 0x02



#define ZLOG(zl, prio, format, ...) \
    zlog(zl, prio, ZLOG_MODULE, __FILE__, __FUNCTION__, __LINE__, format, ##__VA_ARGS__)

#define ZLOG_ERR(format, ...) \
    zlog_err(ZLOG_MODULE, __FILE__, (char *)__FUNCTION__, __LINE__, format, ##__VA_ARGS__)

#define ZLOG_WARN(format, ...) \
    zlog_warn(ZLOG_MODULE, __FILE__, (char *)__FUNCTION__, __LINE__, format, ##__VA_ARGS__)

#define ZLOG_NOTICE(format, ...) \
    zlog_notice(ZLOG_MODULE, __FILE__, (char *)__FUNCTION__, __LINE__, format, ##__VA_ARGS__)

#define ZLOG_INFO(format, ...) \
    zlog_info(ZLOG_MODULE, __FILE__, (char *)__FUNCTION__, __LINE__, format, ##__VA_ARGS__)

#define ZLOG_DEBUG(format, ...) \
    zlog_debug(ZLOG_MODULE, __FILE__, (char *)__FUNCTION__, __LINE__, format, ##__VA_ARGS__)

struct zlog
{
  int flags;
  FILE *fp;
  char *filename;
  int maskpri;      /* as per syslog setlogmask */
  int priority;     /* as per syslog priority */
  int modules;      /* modules mask */
};

/* Message structure. */
struct message
{
  int key;
  char *str;
};

/* GCC have printf type attribute check.  */
#ifdef __GNUC__
#define PRINTF_ATTRIBUTE(a,b) __attribute__ ((__format__ (__printf__, a, b)))
#else
#define PRINTF_ATTRIBUTE(a,b)
#endif /* __GNUC__ */

extern struct zlog *zlog_default;

/* Open zlog function */
struct zlog *openzlog (int);

/* Close zlog function. */
void closezlog (struct zlog *zl);


/* Generic function for zlog. */
void zlog (struct zlog *zl, int priority, int module, char *file, char *func, int line, const char *format, ...) PRINTF_ATTRIBUTE(7, 8);

/* Handy zlog functions. */
void zlog_err (int module, char *file, char *func, int line, const char *format, ...) PRINTF_ATTRIBUTE(5, 6);
void zlog_warn (int module, char *file, char *func, int line, const char *format, ...) PRINTF_ATTRIBUTE(5, 6);
void zlog_info (int module, char *file, char *func, int line, const char *format, ...) PRINTF_ATTRIBUTE(5, 6);
void zlog_notice (int module, char *file, char *func, int line, const char *format, ...) PRINTF_ATTRIBUTE(5, 6);
void zlog_debug (int module, char *file, char *func, int line, const char *format, ...) PRINTF_ATTRIBUTE(5, 6);

/* Set zlog flags. */
void zlog_set_flag (struct zlog *zl, int flags);
void zlog_reset_flag (struct zlog *zl, int flags);

/* Set zlog filename. */
int zlog_set_file (struct zlog *zl, int flags, char *filename);
int zlog_reset_file (struct zlog *zl);

/* set mask priority */
int zlog_set_pri(struct zlog *zl, int priority);

/* Rotate log. */
int zlog_rotate ();

char *lookup (struct message *, int);

#endif /* _ZEBRA_LOG_H */
