/*
 * log.c
 *
 *   Rebase from zebra project
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "log.h"

/* For time string format. */
#define TIME_BUF 27

struct zlog *zlog_default;


const char *zlog_priority[] =
{
  "emergencies",
  "alerts",
  "critical",
  "errors",
  "warnings",
  "notifications",
  "informational",
  "debugging",
  NULL,
};

/* Utility routine for current time printing. */
static void
time_print (FILE *fp)
{
  int ret;
  char buf [TIME_BUF];
  time_t clock;
  struct tm *tm;

  time (&clock);
  tm = localtime (&clock);

  ret = strftime (buf, TIME_BUF, "%Y/%m/%d %H:%M:%S", tm);
  if (ret == 0) {
    return;
  }

  fprintf (fp, "%s ", buf);
}

/* va_list version of zlog. */
void
vzlog (struct zlog *zl, int priority, int module, char *file, char *func, int line, const char *format, va_list *args)
{
  if(zl == NULL) zl = zlog_default;

  /* When zl is NULL, use stderr for logging. */
  if (zl == NULL)
    {
      time_print (stderr);
      fprintf (stderr, "%s: ", "unknown");
      vfprintf (stderr, format, args[ZLOG_NOLOG_INDEX]);
      fprintf (stderr, "\n");
      fflush (stderr);

      /* In this case we return at here. */
      return;
    }

  /* only log this information if it has not been masked out */
  if ( priority > zl->maskpri )
    return ;

  /* module check */
  if (!(module & zl->modules))
    return ;

  /* File output. */
  if ((zl->flags & ZLOG_FILE) && (zl->fp))
    {
      if (zl->flags & ZLOG_LOGTIME) time_print (zl->fp);
      if (zl->flags & ZLOG_LOGPRIO) fprintf (zl->fp, "%s: ", zlog_priority[priority]);
      if (zl->flags & ZLOG_LOGFILE) fprintf (zl->fp, "%s %s():%d ", file, func, line);
      vfprintf (zl->fp, format, args[ZLOG_FILE_INDEX]);
      if (zl->flags & ZLOG_LOGCR) fprintf (zl->fp, "\n");
      fflush (zl->fp);
    }

  /* stdout output. */
  if (zl->flags & ZLOG_STDOUT)
    {
      if (zl->flags & ZLOG_LOGTIME) time_print (stdout);
      if (zl->flags & ZLOG_LOGPRIO) fprintf (stdout, "%s: ", zlog_priority[priority]);
      if (zl->flags & ZLOG_LOGFILE) fprintf (stdout, "%s %s():%d ", file, func, line);
      vfprintf (stdout, format, args[ZLOG_STDOUT_INDEX]);
      if (zl->flags & ZLOG_LOGCR) fprintf (stdout, "\n");
      fflush (stdout);
    }

  /* stderr output. */
  if (zl->flags & ZLOG_STDERR)
    {
      if (zl->flags & ZLOG_LOGTIME) time_print (stderr);
      if (zl->flags & ZLOG_LOGPRIO) fprintf (stderr, "%s: ", zlog_priority[priority]);
      if (zl->flags & ZLOG_LOGFILE) fprintf (stderr, "%s %s():%d ", file, func, line);
      vfprintf (stderr, format, args[ZLOG_STDERR_INDEX]);
      if (zl->flags & ZLOG_LOGCR) fprintf (stderr, "\n");
      fflush (stderr);
    }
}

void
zlog (struct zlog *zl, int priority, int module, char *file, char *func, int line, const char *format, ...)
{
  va_list args[ZLOG_MAX_INDEX];
  int index;

  for (index = 0; index < ZLOG_MAX_INDEX; index++)
    va_start(args[index], format);

  vzlog (zl, priority, module, file, func, line, format, args);

  for (index = 0; index < ZLOG_MAX_INDEX; index++)
    va_end (args[index]);
}

void
zlog_err (int module, char *file, char *func, int line, const char *format, ...)
{
  va_list args[ZLOG_MAX_INDEX];
  int index;

  for (index = 0; index < ZLOG_MAX_INDEX; index++)
    va_start(args[index], format);

  vzlog (NULL, LOG_ERR, module, file, func, line, format, args);

  for (index = 0; index < ZLOG_MAX_INDEX; index++)
    va_end (args[index]);
}

void
zlog_warn (int module, char *file, char *func, int line, const char *format, ...)
{
  va_list args[ZLOG_MAX_INDEX];
  int index;

  for (index = 0; index < ZLOG_MAX_INDEX; index++)
    va_start(args[index], format);

  vzlog (NULL, LOG_WARNING, module, file, func, line, format, args);

  for (index = 0; index < ZLOG_MAX_INDEX; index++)
    va_end (args[index]);
}

void
zlog_info (int module, char *file, char *func, int line, const char *format, ...)
{
  va_list args[ZLOG_MAX_INDEX];
  int index;

  for (index = 0; index < ZLOG_MAX_INDEX; index++)
    va_start(args[index], format);

  vzlog (NULL, LOG_INFO, module, file, func, line, format, args);

  for (index = 0; index < ZLOG_MAX_INDEX; index++)
    va_end (args[index]);
}

void
zlog_notice (int module, char *file, char *func, int line, const char *format, ...)
{
  va_list args[ZLOG_MAX_INDEX];
  int index;

  for (index = 0; index < ZLOG_MAX_INDEX; index++)
    va_start(args[index], format);

  vzlog (NULL, LOG_NOTICE, module, file, func, line, format, args);

  for (index = 0; index < ZLOG_MAX_INDEX; index++)
    va_end (args[index]);
}

void
zlog_debug (int module, char *file, char *func, int line, const char *format, ...)
{
  va_list args[ZLOG_MAX_INDEX];
  int index;

  for (index = 0; index < ZLOG_MAX_INDEX; index++)
    va_start(args[index], format);

  vzlog (NULL, LOG_DEBUG, module, file, func, line, format, args);

  for (index = 0; index < ZLOG_MAX_INDEX; index++)
    va_end (args[index]);
}

/* Open log stream */
struct zlog * openzlog (int flags)
{
  struct zlog *zl;

  zl = malloc(sizeof (struct zlog));
  memset (zl, 0, sizeof (struct zlog));

  zl->flags = flags;
  zl->flags |= (ZLOG_LOGPRIO | ZLOG_LOGTIME | ZLOG_LOGFILE);
  zl->modules = 0xFFFFFFFF;
  zl->maskpri = LOG_DEBUG;

  return zl;
}

void
closezlog (struct zlog *zl)
{
  if(zl->fp) fclose (zl->fp);

  /* fix memory leak */
  if (zl->filename)
    free (zl->filename);

  free(zl);
}

/* Called from command.c. */
void
zlog_set_flag (struct zlog *zl, int flags)
{
  if (zl == NULL)
    return;

  zl->flags |= flags;
}

void
zlog_reset_flag (struct zlog *zl, int flags)
{
  if (zl == NULL)
    return;

  zl->flags &= ~flags;
}

int
zlog_set_file (struct zlog *zl, int flags, char *filename)
{
  FILE *fp;

  if (zl == NULL)
    return 0;

  /* There is opened file. */
  zlog_reset_file (zl);

  /* Open file. */
  fp = fopen (filename, "a");
  if (fp == NULL)
    return 0;

  /* Set flags. */
  zl->filename = malloc(strlen(filename) + 1);
  strcpy(zl->filename, filename);																   
  //zl->filename = strdup (filename);
  zl->flags |= ZLOG_FILE;
  zl->fp = fp;

  return 1;
}

/* Reset opened file. */
int
zlog_reset_file (struct zlog *zl)
{
  if (zl == NULL)
    return 0;

  zl->flags &= ~ZLOG_FILE;

  if (zl->fp)
    fclose (zl->fp);
  zl->fp = NULL;

  if (zl->filename)
    free (zl->filename);
  zl->filename = NULL;

  return 1;
}

int zlog_set_pri(struct zlog *zl, int priority)
{
	if (zl == NULL)
		return 0;

	zl->maskpri = priority;

	return 1;
}

/* Reopen log file. */
int
zlog_rotate (struct zlog *zl)
{
  FILE *fp;

  if (zl == NULL)
    return 0;

  if (zl->fp)
    fclose (zl->fp);
  zl->fp = NULL;

  if (zl->filename)
    {
      fp = fopen (zl->filename, "a");
      if (fp == NULL)
    return -1;
      zl->fp = fp;
    }

  return 1;
}

/* Message lookup function. */
char *
lookup (struct message *mes, int key)
{
  struct message *pnt;

  for (pnt = mes; pnt->key != 0; pnt++)
    if (pnt->key == key)
      return pnt->str;

  return "";
}
