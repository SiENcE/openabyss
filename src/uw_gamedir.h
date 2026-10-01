/* SPDX-License-Identifier: MIT */
/* The game's files on the player's machine: where they are, which release
 * they are, and a file system that does not care about the case of their
 * names.
 *
 * THE CASE OF A NAME. The game's own code asks for DATA\STRINGS.PAK, and
 * DOS did not care how the file was spelled on disk; a Linux copy extracted
 * in lower case would otherwise read as a game with no files. uw_fopen and
 * uw_path_resolve find a path the way DOS would, one component at a time,
 * ignoring case where the exact name is not there. On Windows the file
 * system already ignores case and they are fopen and a copy.
 *
 * THE GAME'S DIRECTORY holds UW.EXE and the directories DATA, CRIT and
 * CUTS (and SOUND, which only the sound needs), and a directory whose UW
 * subdirectory is the game's is taken for that (the layout of the CD).
 *
 * GOG'S RELEASE holds the game as a CD image, game.gog: an ISO 9660 image
 * of the Ultima Underworld I & II CD, the game in its UW folder. A GOG
 * install is a directory with game.gog in it; the image's UW folder is
 * copied out once (uw_iso_extract_game) and the copy is the game's
 * directory from then on.
 *
 * THE RELEASE. The port reads the program's initial memory out of UW.EXE,
 * so it runs with the executable it was made against: GOG's. Any other is
 * identified by its CRC-32 and size and run untested.
 *
 * THE CONFIGURATION is a text file of `key = value` lines, a `#` starting
 * a comment. */
#ifndef UW_GAMEDIR_H
#define UW_GAMEDIR_H

#include "uw.h"
#include <stdio.h>

/* ---- the file system ---------------------------------------------------- */

/* `path` as it is on disk: 1 with `out` the path found (the exact one, or
 * each missing component matched ignoring case), 0 when there is no such
 * file or directory. */
int   uw_path_resolve(const char *path, char *out, size_t cap);
/* fopen with the path resolved; a file opened for writing keeps the name
 * given, in its directory as resolved. */
FILE *uw_fopen(const char *path, const char *mode);
int   uw_is_dir(const char *path);
/* mkdir, 0 when it was made or is there already. */
int   uw_mkdir(const char *path);

/* ---- the game's directory ----------------------------------------------- */

/* 1 when `dir` is the game's directory. Otherwise 0, and `why` names what
 * it lacks ("no UW.EXE", "no DATA/"), when `why` is given. */
int uw_game_check(const char *dir, char *why, size_t cap);
/* `dir`, or its UW subdirectory when that is the game's (GOG's layout):
 * written to `out`, 1 when either is the game's directory. */
int uw_game_find_in(const char *dir, char *out, size_t cap);
/* The places GOG installs the game, searched for a directory whose name
 * holds "underworld": 1 with the first found in `out` -- the game's
 * directory, or, *image set, a CD image holding it. `home` is the user's
 * home directory, or NULL. */
int uw_game_detect(const char *home, char *out, size_t cap, int *image);

/* ---- the CD image ------------------------------------------------------- */

/* 1 when `path` is an ISO 9660 image whose UW folder holds the game. */
int uw_iso_holds_game(const char *path);
/* The image's UW folder copied into `dst`, made as needed: 1, or 0 with
 * what failed in `why`. */
int uw_iso_extract_game(const char *path, const char *dst, char *why, size_t cap);
/* The CD image in `dir` (game.gog, the name GOG gives it) when it holds the
 * game: 1 with its path in `out`. */
int uw_game_image_in(const char *dir, char *out, size_t cap);

typedef struct {
    uint32_t    crc;          /* UW.EXE's CRC-32 */
    long        size;         /* and its size */
    const char *name;         /* the release, or NULL when it is not one we know */
    int         supported;    /* 1: the release the port is made against */
} uw_game_release;

/* UW.EXE identified: 1, or 0 when it will not read. */
int uw_game_release_of(const char *dir, uw_game_release *r);
uint32_t uw_crc32(const uint8_t *p, size_t n);

/* ---- the configuration -------------------------------------------------- */

/* The value of `key` in the file at `path`: 1 with it in `out`, 0 when
 * the file or the key is not there. */
int uw_config_get(const char *path, const char *key, char *out, size_t cap);
/* `key` set to `value`, the file's other lines kept: 1, or 0 when it will
 * not write. */
int uw_config_set(const char *path, const char *key, const char *value);

#endif
