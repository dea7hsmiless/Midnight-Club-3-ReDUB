/* windows_host.c -- mcdub3.exe started on its own (double-click, shortcut).
 *
 * Windows' counterpart of switch_nx.c's set-up: what run_mcdub3.bat did, so
 * the exe works without it (it did nothing when double-clicked: no disc
 * found, a console that flashed and closed, 2026-10-23).
 *
 *   settings  mcdub3_defaults.txt (the build's SCALE / UI_SCALE / SETTINGS)
 *             then mcdub3_env.txt beside the exe, NAME=value per line, #
 *             comments; the later file wins, the real environment (make run
 *             RECOMP_GL_SCALE=2, a launcher) over both
 *   the disc  as tools/env.sh picks it: MC3_GAME_DIR; else EDITION (the
 *             settings; make build EDITION=eur writes it): the repo's
 *             game-<edition>\; else the only disc there is (game\ beside
 *             the exe, the repo's game\ or game-*\); of several, game\,
 *             then the lifted edition's, then the first by name
 *             (dist\ sits in work\build-windows-<arch>\)
 *   saves     MC3_SAVE_DIR, else the repo's work\save\ if there is one
 *   the log   started without a console of its own to report to (a
 *             double-click gives it a fresh one): stdout and stderr go to
 *             work\logs\windows_run_<time>.log (else mcdub3_log.txt beside
 *             the exe) and the console closes, leaving the game's window
 *   errors    fatal() (a missing disc, another edition) in a message box
 */
#if defined(_WIN32)
#include <windows.h>
#include <stdio.h>
#include "xbe_facts.h"         /* MC3_EDITION: the edition the code was lifted from */
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef MC3_APP                              /* per game, from CMake */
#  define MC3_APP "mcdub3"
#endif
#ifndef MC3_DISC_PREFIX
#  define MC3_DISC_PREFIX "game"
#endif
#ifndef MC3_GAME_TITLE
#  define MC3_GAME_TITLE "Midnight Club 3: DUB Edition"
#endif

static char s_exe_dir[MAX_PATH * 3];

static int is_dir(const char *p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static int is_file(const char *p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

/* Names the real environment had before the files were read. */
#define MAX_KEPT 256
static char *s_kept[MAX_KEPT];
static int s_nkept;

static int was_set_outside(const char *name)
{
    int i;
    for (i = 0; i < s_nkept; i++)
        if (!strcmp(s_kept[i], name))
            return 1;
    return 0;
}

static void remember_environment(void)
{
    LPCH env = GetEnvironmentStringsA(), p;
    if (!env)
        return;
    for (p = env; *p && s_nkept < MAX_KEPT; p += strlen(p) + 1) {
        const char *eq = strchr(p + 1, '=');    /* "=C:=C:\..." entries start with '=' */
        if (eq && (!strncmp(p, "RECOMP_", 7) || !strncmp(p, "MC3_", 4))) {
            char *name = _strdup(p);
            if (!name)
                break;
            name[eq - p] = 0;
            s_kept[s_nkept++] = name;
        }
    }
    FreeEnvironmentStringsA(env);
}

static void load_settings(const char *file)
{
    char path[sizeof s_exe_dir + 64], line[1024];
    FILE *f;
    snprintf(path, sizeof path, "%s\\%s", s_exe_dir, file);
    if (!(f = fopen(path, "r")))
        return;
    while (fgets(line, sizeof line, f)) {
        char *s = line, *eq, *e, *name_end, *value;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '#' || !(eq = strchr(s, '=')))
            continue;
        for (e = s + strlen(s); e > eq && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'); e--) { }
        *e = 0;
        /* "KEY = value" is what INI writers produce; trim both sides so it
         * sets the same variable as "KEY=value". */
        name_end = eq;
        while (name_end > s && (name_end[-1] == ' ' || name_end[-1] == '\t')) name_end--;
        *name_end = 0;
        value = eq + 1;
        while (*value == ' ' || *value == '\t') value++;
        /* INI writers sometimes quote the value; a matching pair is dropped so
         * KEY = "60" and KEY = 60 set the same thing. */
        {
            size_t n = strlen(value);
            if (n >= 2 && (value[0] == '"' || value[0] == '\'') && value[n - 1] == value[0]) {
                value[n - 1] = 0;
                value++;
            }
        }
        if (*s && !was_set_outside(s))
            _putenv_s(s, value);
    }
    fclose(f);
}

static void find_dir(const char *var, const char *const *cands)
{
    char p[sizeof s_exe_dir + 64];
    const char *e = getenv(var);
    if (e && *e)
        return;
    for (; *cands; cands++) {
        snprintf(p, sizeof p, "%s\\%s", s_exe_dir, *cands);
        if (is_dir(p)) {
            _putenv_s(var, p);
            return;
        }
    }
}

/* A double-click gives the exe a console of its own (only this process in
 * it): send the log to a file and close that console. Started from a
 * terminal or with its output redirected (run_mcdub3.bat, make run), it
 * leaves the output where it is. */
static void log_to_file_if_alone(void)
{
    DWORD procs[2];
    char logs[sizeof s_exe_dir + 64], path[sizeof s_exe_dir + 96], ts[32];
    time_t t = time(NULL);
    if (GetConsoleProcessList(procs, 2) != 1)
        return;
    strftime(ts, sizeof ts, "%Y%m%d_%H%M%S", localtime(&t));
    snprintf(logs, sizeof logs, "%s\\..\\..\\logs", s_exe_dir);
    if (is_dir(logs))
        snprintf(path, sizeof path, "%s\\windows_run_%s.log", logs, ts);
    else
        snprintf(path, sizeof path, "%s\\" MC3_APP "_log.txt", s_exe_dir);
    if (freopen(path, "w", stdout) && freopen(path, "a", stderr)) {
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
        FreeConsole();
    }
}

static int has_disc(const char *dir)
{
    char xbe[sizeof s_exe_dir + 96];
    snprintf(xbe, sizeof xbe, "%s\\default.xbe", dir);
    return is_file(xbe);
}

static void pick_game_dir(void)
{
    char repo[sizeof s_exe_dir + 16], cand[16][sizeof s_exe_dir + 64], pick[sizeof s_exe_dir + 64] = "";
    const char *ed = getenv("EDITION"), *e = getenv("MC3_GAME_DIR");
    int n = 0, i;
    WIN32_FIND_DATAA fd;
    HANDLE h;
    if (e && *e)
        return;
    snprintf(repo, sizeof repo, "%s\\..\\..\\..", s_exe_dir);
    if (ed && *ed) {                                       /* EDITION first */
        snprintf(pick, sizeof pick, "%s\\" MC3_DISC_PREFIX "-%s", repo, ed);
        if (!has_disc(pick) && (strcmp(MC3_DISC_PREFIX, "game") || !strcmp(ed, "usa")))
            snprintf(pick, sizeof pick, "%s\\" MC3_DISC_PREFIX, repo);   /* the folder before editions */
        _putenv_s("MC3_GAME_DIR", pick);
        return;
    }
    snprintf(cand[n], sizeof cand[n], "%s\\game", s_exe_dir);
    if (has_disc(cand[n])) n++;
    snprintf(cand[n], sizeof cand[n], "%s\\" MC3_DISC_PREFIX, repo);
    if (has_disc(cand[n])) n++;
    {
        char pat[sizeof repo + 16];
        snprintf(pat, sizeof pat, "%s\\" MC3_DISC_PREFIX "-*", repo);
        if ((h = FindFirstFileA(pat, &fd)) != INVALID_HANDLE_VALUE) {
            do {
                /* game-* holds the Remix's folders too: not the DUB Edition's */
                if (!strcmp(MC3_DISC_PREFIX, "game") && !strncmp(fd.cFileName, "game-remix", 10))
                    continue;
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && n < 16) {
                    snprintf(cand[n], sizeof cand[n], "%s\\%s", repo, fd.cFileName);
                    if (has_disc(cand[n])) n++;
                }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    if (n == 0)
        return;                                            /* fatal() at the XBE says so */
    if (n == 1) {
        _putenv_s("MC3_GAME_DIR", cand[0]);
        return;
    }
    /* Several: game\, then the lifted edition's, then the first by name. */
    for (i = 0; i < n && !pick[0]; i++) {
        const char *base = strrchr(cand[i], '\\');
        if (base && (!strcmp(base + 1, "game") || !strcmp(base + 1, MC3_DISC_PREFIX)))
            strcpy(pick, cand[i]);
    }
    for (i = 0; i < n && !pick[0]; i++) {
        const char *base = strrchr(cand[i], '\\');
        if (base && !strncmp(base + 1, MC3_DISC_PREFIX "-", sizeof MC3_DISC_PREFIX)
            && !strcmp(base + sizeof MC3_DISC_PREFIX + 1, MC3_EDITION))
            strcpy(pick, cand[i]);
    }
    if (!pick[0]) {
        strcpy(pick, cand[0]);
        for (i = 1; i < n; i++)
            if (strcmp(cand[i], pick) < 0)
                strcpy(pick, cand[i]);
    }
    fprintf(stderr, "note: several discs; %s is used (EDITION=<name> in " MC3_APP "_env.txt picks "
            MC3_DISC_PREFIX "-<name>)\n",
            strrchr(pick, '\\') + 1);
    _putenv_s("MC3_GAME_DIR", pick);
}

/* First run (or an install whose folder was replaced): the shipped example
 * becomes the player's settings file. It is never shipped itself, so an
 * update cannot overwrite it; this only fills the gap once. */
static void seed_player_settings(void)
{
    char env[sizeof s_exe_dir + 64], example[sizeof s_exe_dir + 64];
    snprintf(env, sizeof env, "%s\\" MC3_APP "_env.txt", s_exe_dir);
    if (GetFileAttributesA(env) != INVALID_FILE_ATTRIBUTES)
        return;
    snprintf(example, sizeof example, "%s\\" MC3_APP "_env.example.txt", s_exe_dir);
    if (GetFileAttributesA(example) != INVALID_FILE_ATTRIBUTES &&
        CopyFileA(example, env, FALSE)) {
        fprintf(stderr, "settings: created %s from the example\n", env);
        return;
    }
    /* No example shipped with this build: create the file anyway. A player
     * who opens it must find it, and the game must have somewhere to save. */
    {
        FILE *f = fopen(env, "wb");
        if (f) {
            fputs("# " MC3_APP " settings. One NAME=value per line.\r\n", f);
            fputs("# Online play is on by default; MC3_ONLINE=0 turns it off.\r\n", f);
            fputs("# The room is picked in game: Options > ROOM.\r\n", f);
            fclose(f);
            fprintf(stderr, "settings: created %s\n", env);
        }
    }
}

/* Online play on unless the player turned it off, the same default iOS has
 * always had: an install nobody configured must still be online. */
#ifndef MC3_ONLINE_SERVER
#  define MC3_ONLINE_SERVER "162.35.56.66"
#endif
static void apply_online_default(void)
{
    const char *off = getenv("MC3_ONLINE");
    if (off && off[0] == '0') {
        _putenv_s("MC3_ROAM_JOIN", "");
        _putenv_s("MC3_NETPLAY", "");
        return;
    }
    if (!getenv("MC3_ROAM_JOIN"))
        _putenv_s("MC3_ROAM_JOIN", MC3_ONLINE_SERVER);
    if (!getenv("MC3_NETPLAY"))
        _putenv_s("MC3_NETPLAY", "1");
}

void windows_boot(void)
{
    static const char *const save[] = { "..\\..\\save", NULL };
    char *slash;
    GetModuleFileNameA(NULL, s_exe_dir, sizeof s_exe_dir);
    if ((slash = strrchr(s_exe_dir, '\\')) != NULL)
        *slash = 0;
    log_to_file_if_alone();
    remember_environment();
    load_settings(MC3_APP "_defaults.txt");
    seed_player_settings();
    load_settings(MC3_APP "_env.txt");
    apply_online_default();
    pick_game_dir();
    find_dir("MC3_SAVE_DIR", save);
}

/* fatal() in main.c: the log is not where a player looks. */
void windows_error_dialog(const char *msg)
{
    MessageBoxA(NULL, msg, MC3_GAME_TITLE, MB_OK | MB_ICONERROR);
}
#endif
