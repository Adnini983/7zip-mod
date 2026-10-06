/* TorrentZipEngine.c
   Embedded TorrentZip normalization engine for the 7-Zip MOD.

   This unit reuses the exact trrntzip implementation (trrntzip.c) so the
   produced zip bytes are identical to the standalone trrntzip tool
   (https://github.com/0-wiz-0/trrntzip), pinned to its documented dependency
   versions (zlib 1.2.2).

   trrntzip.c is GPLv2 (TorrentZip Team, 2005-2024). Its main() is renamed
   here so the engine can be linked into 7-Zip without becoming a program
   entry point. The unrelated command-line / directory-walk code pulled in by
   that include is dead code and is never called.
*/
#include <string.h>
#include <time.h>

#define main TorrentZip_trrntzip_main_unused
#ifndef TZ_VERSION
#define TZ_VERSION "1.3"
#endif
#include "trrntzip.c"
#undef main

/* Split a path into directory + basename. Buffers are MAX_PATH+1 (from
   global.h, included via trrntzip.c). */
static void TorrentZip_SplitPath(const char *path,
                                 char *dir,  size_t dirSize,
                                 char *base, size_t baseSize)
{
  const char *slash = NULL;
  const char *p;
  for (p = path; *p; p++)
    if (*p == '\\' || *p == '/')
      slash = p;

  if (slash)
  {
    size_t len = (size_t)(slash - path);
    if (len >= dirSize)
      len = dirSize - 1;
    memcpy(dir, path, len);
    dir[len] = 0;
    if (strlen(slash + 1) >= baseSize)
    {
      strcpy(base, "");
      return;
    }
    strcpy(base, slash + 1);
  }
  else
  {
    strcpy(dir, ".");
    if (strlen(path) >= baseSize)
    {
      strcpy(base, "");
      return;
    }
    strcpy(base, path);
  }
}

/* Normalize the zip archive at 'path' into TorrentZip format, in place. */
int TorrentZipNormalizeFile(const char *path)
{
  WORKSPACE *ws = AllocateWorkspace();
  if (!ws)
    return TZ_CRITICAL;

  {
    MIGRATE mig;
    memset(&mig, 0, sizeof(mig));
    mig.StartTime = time(NULL);

    char dir[MAX_PATH + 1];
    char base[MAX_PATH + 1];
    TorrentZip_SplitPath(path, dir, sizeof(dir), base, sizeof(base));

    if (base[0] == 0)
    {
      FreeWorkspace(ws);
      return TZ_ERR;
    }

    int rc = MigrateZip(base, dir, ws, &mig);
    FreeWorkspace(ws);
    return rc;
  }
}
