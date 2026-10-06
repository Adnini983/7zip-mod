/* TorrentZipEngine.h
   Public API of the embedded TorrentZip engine for the 7-Zip MOD.
   Normalizes a zip archive in place to the TorrentZip format, byte-exact
   with the standalone trrntzip tool (0-wiz-0/trrntzip).

   trrntzip is GPLv2 (TorrentZip Team, 2005-2024). This integration is part
   of the 7-Zip MOD.
*/
#ifndef TORRENTZIP_ENGINE_H
#define TORRENTZIP_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TZ_OK
#define TZ_OK        0
#define TZ_ERR       (-1)
#define TZ_CRITICAL  (-2)
#define TZ_SKIPPED   (-3)
#endif

/* Normalize (re-pack) the zip archive at 'path' into TorrentZip format, in place.
   Return codes:
       TZ_OK       ( 0) : success
       TZ_ERR      (-1) : error (corrupt / unreadable input)
       TZ_CRITICAL (-2) : memory or I/O failure
       TZ_SKIPPED  (-3) : already TorrentZipped
*/
int TorrentZipNormalizeFile(const char *path);

#ifdef __cplusplus
}
#endif

#endif
