/* logging_quiet.c
   A silent replacement for trrntzip's logging.c, so that the embedded engine
   never writes to stdout/stderr during a 7-Zip operation (harmless to the
   produced bytes, but would be noisy / undefined in a GUI app).
*/
#include <stdio.h>

#include "logging.h"
#include "util.h"

int OpenProcessLog(const char *pszWritePath, const char *pszRelPath,
                   MIGRATE *mig)
{
  (void)pszWritePath;
  (void)pszRelPath;
  (void)mig;
  return TZ_OK;
}

int SetupErrorLog(WORKSPACE *ws, char qGUILaunch)
{
  (void)qGUILaunch;
  if (ws)
    ws->fErrorLog = NULL;
  return TZ_OK;
}

FILE *ErrorLog(WORKSPACE *ws)
{
  (void)ws;
  return NULL;
}

void logprint(FILE *stdf, FILE *f, char *format, ...)
{
  (void)stdf;
  (void)f;
  (void)format;
}

void logprint3(FILE *stdf, FILE *f1, FILE *f2, char *format, ...)
{
  (void)stdf;
  (void)f1;
  (void)f2;
  (void)format;
}
