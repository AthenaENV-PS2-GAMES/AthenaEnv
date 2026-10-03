#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <delaythread.h>
#include <libmc.h>

#include <athena/debug.h>
#include <athena/memcard.h>
#include <athena/mutex.h>

#include "memcard_internal.h"

/* How long a thread sleeps between checks of a command running on the IOP. */
#define MC_POLL_US 500
/* Bytes per read/write command: the lock is released between them. */
#define MC_BLOCK_SIZE (16 * 1024)
/* Directory entries fetched per mcGetDir call. */
#define MC_DIR_BATCH 32
/* Entries a listing may return, a guard against a corrupt directory. */
#define MC_DIR_MAX_ENTRIES 16384
/* Directory levels a recursive remove goes down. */
#define MC_TREE_MAX_DEPTH 16
#define MC_JST_OFFSET (9 * 3600)

/* A driver descriptor and the card it was opened on. */
typedef struct {
    int port;
    int fd;
    uint32_t epoch;
} McFd;

struct AthenaMemcardFile {
    McFd fd;                    /* fd.fd < 0 once closed */
    int flags;
    int position;
    int size;                   /* as far as this handle knows: the driver keeps one writer */
    uint32_t generation;        /* driver generation the descriptor belongs to */
};

typedef struct {
    uint32_t detected;          /* card changes seen by any command */
    uint32_t reported;          /* value of `detected` at the last get_info */
    /*
     * Changes whenever the card leaves or a new one is detected. Once the
     * driver has acknowledged a new card, a descriptor from before would
     * read, write and even close (it rewrites the directory entry) with
     * the old card's clusters on the new one: such handles are refused.
     */
    uint32_t epoch;
    bool absent;                /* the last command found no card */
} McPortState;

/*
 * libmc serves one command at a time for the whole EE and does not lock:
 * mc_mutex guards every command, the port states and mc_table.
 */
static AthenaMutex *mc_mutex;
static McPortState mc_ports[ATHENA_MEMCARD_PORTS];
/* mcGetDir DMAs into it: aligned and sized in whole cache lines. */
static sceMcTblGetDir mc_table[MC_DIR_BATCH] __attribute__((aligned(64)));

int athena_memcard_module_init(void) {
    if (!mc_mutex)
        mc_mutex = athena_mutex_core_create();
    if (!mc_mutex)
        dbgprintf("[Memcard] cannot create the lock: memory card calls will fail\n");
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Errors                                                                   */
/* ------------------------------------------------------------------------ */

static const struct {
    int code;
    const char *id;
    const char *text;
} mc_errors[] = {
    { ATHENA_MEMCARD_OK, "OK", "success" },
    { ATHENA_MEMCARD_ERR_ARGUMENT, "INVALID_ARGUMENT", "invalid argument" },
    { ATHENA_MEMCARD_ERR_NOT_READY, "NOT_READY", "memory card drivers are not running" },
    { ATHENA_MEMCARD_ERR_NO_CARD, "NO_CARD", "no memory card (or it cannot be accessed)" },
    { ATHENA_MEMCARD_ERR_UNFORMATTED, "UNFORMATTED", "the memory card is not formatted" },
    { ATHENA_MEMCARD_ERR_CHANGED, "CARD_CHANGED", "the memory card was changed" },
    { ATHENA_MEMCARD_ERR_FULL, "FULL", "not enough free space on the memory card" },
    { ATHENA_MEMCARD_ERR_NOT_FOUND, "NOT_FOUND", "no such file or directory" },
    { ATHENA_MEMCARD_ERR_EXISTS, "EXISTS", "the entry already exists" },
    { ATHENA_MEMCARD_ERR_DENIED, "ACCESS_DENIED", "access denied" },
    { ATHENA_MEMCARD_ERR_NOT_EMPTY, "NOT_EMPTY", "the directory is not empty" },
    { ATHENA_MEMCARD_ERR_TOO_MANY_OPEN, "TOO_MANY_OPEN", "too many open memory card files" },
    { ATHENA_MEMCARD_ERR_IS_DIR, "IS_DIRECTORY", "the path is a directory" },
    { ATHENA_MEMCARD_ERR_NOT_DIR, "NOT_DIRECTORY", "the path is not a directory" },
    { ATHENA_MEMCARD_ERR_UNSUPPORTED, "UNSUPPORTED", "not supported by this memory card" },
    { ATHENA_MEMCARD_ERR_MEMORY, "NO_MEMORY", "out of memory" },
    { ATHENA_MEMCARD_ERR_IO, "IO", "memory card I/O error" },
    { ATHENA_MEMCARD_ERR_CLOSED, "CLOSED", "the file is closed" },
    { ATHENA_MEMCARD_ERR_CANCELLED, "CANCELLED", "the operation was cancelled" },
    { ATHENA_MEMCARD_ERR_BUSY, "BUSY", "the memory card is busy" },
};

const char *athena_memcard_strerror(int code) {
    for (size_t i = 0; i < sizeof(mc_errors) / sizeof(mc_errors[0]); i++)
        if (mc_errors[i].code == code)
            return mc_errors[i].text;
    return "unknown memory card error";
}

const char *athena_memcard_error_code(int code) {
    for (size_t i = 0; i < sizeof(mc_errors) / sizeof(mc_errors[0]); i++)
        if (mc_errors[i].code == code)
            return mc_errors[i].id;
    return "IO";
}

/* Driver result (sceMcRes*, or a negative internal code) to ATHENA_MEMCARD_ERR_*. */
static int mc_error(int result) {
    switch (result) {
    case sceMcResChangedCard: return ATHENA_MEMCARD_ERR_CHANGED;
    case sceMcResNoFormat: return ATHENA_MEMCARD_ERR_UNFORMATTED;
    case sceMcResFullDevice: return ATHENA_MEMCARD_ERR_FULL;
    case sceMcResNoEntry: return ATHENA_MEMCARD_ERR_NOT_FOUND;
    case sceMcResDeniedPermit: return ATHENA_MEMCARD_ERR_DENIED;
    case sceMcResNotEmpty: return ATHENA_MEMCARD_ERR_NOT_EMPTY;
    case sceMcResUpLimitHandle: return ATHENA_MEMCARD_ERR_TOO_MANY_OPEN;
    case sceMcResFailReplace: return ATHENA_MEMCARD_ERR_EXISTS;
    case sceMcResDeniedPS1Permit: return ATHENA_MEMCARD_ERR_UNSUPPORTED;
    case sceMcResFailAuth: return ATHENA_MEMCARD_ERR_NO_CARD;
    default:
        /* Probes below -9 mean that no card answered. */
        if (result <= -10 && result >= -13)
            return ATHENA_MEMCARD_ERR_NO_CARD;
        dbgprintf("[Memcard] driver error %d\n", result);
        return ATHENA_MEMCARD_ERR_IO;
    }
}

/* ------------------------------------------------------------------------ */
/* Commands                                                                 */
/* ------------------------------------------------------------------------ */

static int mc_lock(void) {
    int status;

    if (!mc_mutex)
        return ATHENA_MEMCARD_ERR_NOT_READY;
    if (athena_mutex_core_lock(mc_mutex) < 0)
        return ATHENA_MEMCARD_ERR_IO;
    status = memcard_driver_status();
    if (status < 0)
        athena_mutex_core_unlock(mc_mutex);
    return status;
}

static void mc_unlock(void) {
    athena_mutex_core_unlock(mc_mutex);
}

/*
 * Waits for the command libmc just queued, given what the mc* call
 * returned. libmc answers a positive command number when another command
 * is still in flight, which only code bypassing the lock can cause.
 */
static int mc_finish(int issued, int *result) {
    int done;

    if (issued > 0)
        return ATHENA_MEMCARD_ERR_BUSY;
    if (issued < 0)
        return issued == -1 ? ATHENA_MEMCARD_ERR_NOT_READY : ATHENA_MEMCARD_ERR_IO;
    /* mcSync(MC_WAIT) spins without yielding: poll and sleep instead. */
    while ((done = mcSync(MC_NOWAIT, NULL, result)) == 0)
        DelayThread(MC_POLL_US);
    return done == 1 ? ATHENA_MEMCARD_OK : ATHENA_MEMCARD_ERR_IO;
}

typedef struct {
    int port;
    const char *path;
    int flags;
    int maxent;
    const char *name;
    const sceMcTblGetDir *info;
} McArgs;

typedef int (*McIssue)(const McArgs *args);

static int mc_issue_getdir(const McArgs *a) {
    return mcGetDir(a->port, 0, a->path, (unsigned)a->flags, a->maxent, mc_table);
}
static int mc_issue_open(const McArgs *a) { return mcOpen(a->port, 0, a->path, a->flags); }
static int mc_issue_mkdir(const McArgs *a) { return mcMkDir(a->port, 0, a->path); }
static int mc_issue_delete(const McArgs *a) { return mcDelete(a->port, 0, a->path); }
static int mc_issue_rename(const McArgs *a) { return mcRename(a->port, 0, a->path, a->name); }
static int mc_issue_ent_space(const McArgs *a) { return mcGetEntSpace(a->port, 0, a->path); }
static int mc_issue_format(const McArgs *a) { return mcFormat(a->port, 0); }
static int mc_issue_unformat(const McArgs *a) { return mcUnformat(a->port, 0); }
static int mc_issue_set_info(const McArgs *a) {
    return mcSetFileInfo(a->port, 0, a->path, a->info, (unsigned)a->flags);
}

/* Port state updates, lock held. */
static void mc_card_detected(int port) {
    mc_ports[port].detected++;
    mc_ports[port].epoch++;
    mc_ports[port].absent = false;
}

static void mc_card_presence(int port, bool present) {
    if (!present && !mc_ports[port].absent)
        mc_ports[port].epoch++;
    mc_ports[port].absent = !present;
}

static bool mc_no_card_result(int result) {
    return (result <= -10 && result >= -13) || result == sceMcResFailAuth;
}

/*
 * Full detection, lock held. Commands only run the driver's light probe,
 * which fails on a card that was never authenticated (drivers just started,
 * card just inserted); mcGetInfo authenticates it, as the iomanX mc device
 * does before every operation.
 */
static void mc_detect_locked(int port) {
    int type = 0, free_clusters = 0, formatted = 0, result = 0;

    if (mc_finish(mcGetInfo(port, 0, &type, &free_clusters, &formatted), &result) < 0)
        return;
    if (result == sceMcResChangedCard || result == sceMcResNoFormat)
        mc_card_detected(port);
    mc_card_presence(port, type != ATHENA_MEMCARD_TYPE_NONE);
}

/*
 * Runs a command that names the card, lock held. The first command after a
 * swap only reports the change, and one on a card not authenticated yet
 * reports no card: acknowledge or detect it, then run the command again.
 */
static int mc_command_locked(McIssue issue, const McArgs *args, int *result) {
    for (int attempt = 0;; attempt++) {
        int ret = mc_finish(issue(args), result);
        if (ret < 0)
            return ret;
        if (attempt == 0 && *result == sceMcResChangedCard) {
            mc_card_detected(args->port);
            continue;
        }
        if (attempt == 0 && mc_no_card_result(*result)) {
            mc_detect_locked(args->port);
            continue;
        }
        if (mc_no_card_result(*result))
            mc_card_presence(args->port, false);
        return ATHENA_MEMCARD_OK;
    }
}

/* One command with its own lock. Returns the driver result or an error. */
static int mc_command(McIssue issue, const McArgs *args) {
    int ret, result = 0;

    ret = mc_lock();
    if (ret < 0)
        return ret;
    ret = mc_command_locked(issue, args, &result);
    mc_unlock();
    if (ret < 0)
        return ret;
    return result < 0 ? mc_error(result) : result;
}

/* A descriptor command, lock held: no retry, the descriptor dies with the card. */
static int mc_fd_result(int port, int result) {
    if (result == sceMcResChangedCard) {
        mc_card_detected(port);
        return ATHENA_MEMCARD_ERR_CHANGED;
    }
    if (mc_no_card_result(result))
        mc_card_presence(port, false);
    return result < 0 ? mc_error(result) : result;
}

/*
 * Runs a descriptor command given what the mc* call returned. Returns the
 * driver result (>= 0) or an error.
 */
static int mc_fd_command(int port, int issued) {
    int ret, result = 0;

    ret = mc_finish(issued, &result);
    return ret < 0 ? ret : mc_fd_result(port, result);
}

static int mc_check_port(int port) {
    return port >= 0 && port < ATHENA_MEMCARD_PORTS ? ATHENA_MEMCARD_OK : ATHENA_MEMCARD_ERR_ARGUMENT;
}

/* ------------------------------------------------------------------------ */
/* Paths                                                                    */
/* ------------------------------------------------------------------------ */

static bool mc_valid_name_char(unsigned char c) {
    return c >= 0x20 && c != 0x7F && c != '*' && c != '?' && c != '/';
}

static bool mc_valid_name(const char *name, size_t length) {
    if (length == 0 || length > ATHENA_MEMCARD_NAME_MAX)
        return false;
    if ((length == 1 && name[0] == '.') || (length == 2 && name[0] == '.' && name[1] == '.'))
        return false;
    for (size_t i = 0; i < length; i++)
        if (!mc_valid_name_char((unsigned char)name[i]))
            return false;
    return true;
}

int athena_memcard_normalize(const char *path, char *out, size_t size) {
    size_t length = 1;
    const char *p = path;

    if (!path || !out || size < 2)
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    out[0] = '/';
    out[1] = '\0';

    while (*p) {
        const char *start;
        size_t count, needed;

        while (*p == '/')
            p++;
        if (!*p)
            break;
        start = p;
        while (*p && *p != '/')
            p++;
        count = (size_t)(p - start);

        if (count == 1 && start[0] == '.')
            continue;
        if (count == 2 && start[0] == '.' && start[1] == '.') {
            if (length == 1)
                return ATHENA_MEMCARD_ERR_ARGUMENT; /* above the root */
            while (length > 1 && out[length - 1] != '/')
                length--;
            if (length > 1)
                length--;
            out[length] = '\0';
            continue;
        }
        if (!mc_valid_name(start, count))
            return ATHENA_MEMCARD_ERR_ARGUMENT;

        needed = (length > 1 ? 1 : 0) + count;
        if (length + needed >= size || length + needed > ATHENA_MEMCARD_PATH_MAX)
            return ATHENA_MEMCARD_ERR_ARGUMENT;
        if (length > 1)
            out[length++] = '/';
        memcpy(out + length, start, count);
        length += count;
        out[length] = '\0';
    }
    return ATHENA_MEMCARD_OK;
}

int athena_memcard_parse_path(const char *path, int *port, char *out, size_t size) {
    if (!path || !port)
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    if ((path[0] != 'm' && path[0] != 'M') || (path[1] != 'c' && path[1] != 'C') ||
        (path[2] != '0' && path[2] != '1') || path[3] != ':')
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    *port = path[2] - '0';
    return athena_memcard_normalize(path + 4, out, size);
}

/* Last component of a normalized path ("" for the root). */
static const char *mc_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static bool mc_is_root(const char *path) {
    return path[0] == '/' && path[1] == '\0';
}

typedef char McPath[ATHENA_MEMCARD_PATH_MAX + 1];

/* Validates the port and normalizes `path` into `out`. */
static int mc_prepare_path(int port, const char *path, McPath out) {
    int ret = mc_check_port(port);
    if (ret < 0)
        return ret;
    return athena_memcard_normalize(path, out, sizeof(McPath));
}

/* ------------------------------------------------------------------------ */
/* Time                                                                     */
/* ------------------------------------------------------------------------ */

/* Days since 1970-01-01 of a proleptic Gregorian date (Howard Hinnant). */
static int64_t mc_days_from_civil(int64_t y, unsigned m, unsigned d) {
    int64_t era;
    unsigned yoe, doy, doe;

    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

int64_t athena_memcard_time_to_unix(const AthenaMemcardTime *time) {
    int64_t seconds;

    if (!time || time->month < 1 || time->month > 12 || time->day < 1 || time->day > 31)
        return 0;
    seconds = mc_days_from_civil(time->year, time->month, time->day) * 86400 +
        time->hour * 3600 + time->min * 60 + time->sec - MC_JST_OFFSET;
    return seconds < 0 ? 0 : seconds;
}

void athena_memcard_time_from_unix(int64_t seconds, AthenaMemcardTime *time) {
    int64_t days, z, era;
    unsigned doe, yoe, doy, mp, secs;
    int64_t year;

    if (seconds < 0)
        seconds = 0;
    if (seconds > INT64_C(4291747199)) /* 2105-12-31 23:59:59 UTC */
        seconds = INT64_C(4291747199);
    seconds += MC_JST_OFFSET;
    days = seconds / 86400;
    secs = (unsigned)(seconds % 86400);

    z = days + 719468;
    era = z / 146097;
    doe = (unsigned)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    year = (int64_t)yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    time->day = (uint8_t)(doy - (153 * mp + 2) / 5 + 1);
    time->month = (uint8_t)(mp < 10 ? mp + 3 : mp - 9);
    time->year = (uint16_t)(year + (time->month <= 2));
    time->hour = (uint8_t)(secs / 3600);
    time->min = (uint8_t)(secs / 60 % 60);
    time->sec = (uint8_t)(secs % 60);
}

static void mc_time_in(const sceMcStDateTime *in, AthenaMemcardTime *out) {
    out->year = in->Year;
    out->month = in->Month;
    out->day = in->Day;
    out->hour = in->Hour;
    out->min = in->Min;
    out->sec = in->Sec;
}

static void mc_time_out(const AthenaMemcardTime *in, sceMcStDateTime *out) {
    memset(out, 0, sizeof(*out));
    out->Year = in->year;
    out->Month = in->month;
    out->Day = in->day;
    out->Hour = in->hour;
    out->Min = in->min;
    out->Sec = in->sec;
}

static void mc_entry_from_table(const sceMcTblGetDir *table, AthenaMemcardEntry *entry) {
    memcpy(entry->name, table->EntryName, ATHENA_MEMCARD_NAME_MAX);
    entry->name[ATHENA_MEMCARD_NAME_MAX] = '\0';
    entry->size = table->FileSizeByte;
    entry->attributes = table->AttrFile;
    mc_time_in(&table->_Create, &entry->created);
    mc_time_in(&table->_Modify, &entry->modified);
}

/* ------------------------------------------------------------------------ */
/* Card                                                                     */
/* ------------------------------------------------------------------------ */

int athena_memcard_get_info(int port, AthenaMemcardInfo *info) {
    int type = 0, free_clusters = 0, formatted = 0, result = 0, ret;

    if (!info || mc_check_port(port) < 0)
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    ret = mc_lock();
    if (ret < 0)
        return ret;
    ret = mc_finish(mcGetInfo(port, 0, &type, &free_clusters, &formatted), &result);
    if (ret == ATHENA_MEMCARD_OK) {
        /* -1 / -2: a formatted / unformatted card arrived since the last call. */
        if (result == sceMcResChangedCard || result == sceMcResNoFormat)
            mc_card_detected(port);
        /* An empty slot: the driver dropped the card's descriptors. */
        mc_card_presence(port, type != ATHENA_MEMCARD_TYPE_NONE);
        info->changed = mc_ports[port].detected != mc_ports[port].reported;
        mc_ports[port].reported = mc_ports[port].detected;
    }
    mc_unlock();
    if (ret < 0)
        return ret;

    /* Below -2 nothing answered: the driver still reports the type it saw. */
    info->type = (type >= ATHENA_MEMCARD_TYPE_NONE && type <= ATHENA_MEMCARD_TYPE_POCKETSTATION) ?
        (AthenaMemcardType)type : ATHENA_MEMCARD_TYPE_NONE;
    info->formatted = info->type != ATHENA_MEMCARD_TYPE_NONE && result >= sceMcResChangedCard &&
        formatted > 0;
    info->free_clusters = info->type != ATHENA_MEMCARD_TYPE_NONE && free_clusters > 0 ?
        (uint32_t)free_clusters : 0;
    return ATHENA_MEMCARD_OK;
}

int athena_memcard_get_info_raw(int port, int *type, int *free_clusters, int *formatted) {
    AthenaMemcardInfo info;
    int ret = athena_memcard_get_info(port, &info);

    if (ret < 0)
        return ret;
    if (type) *type = info.type;
    if (free_clusters) *free_clusters = (int)info.free_clusters;
    if (formatted) *formatted = info.formatted;
    return ATHENA_MEMCARD_OK;
}

static int mc_format_command(int port, McIssue issue) {
    McArgs args = { .port = port };
    int ret;

    if (mc_check_port(port) < 0)
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    ret = mc_command(issue, &args);
    return ret < 0 ? ret : ATHENA_MEMCARD_OK;
}

int athena_memcard_format(int port) {
    return mc_format_command(port, mc_issue_format);
}

int athena_memcard_unformat(int port) {
    return mc_format_command(port, mc_issue_unformat);
}

/* ------------------------------------------------------------------------ */
/* Entries                                                                  */
/* ------------------------------------------------------------------------ */

/* stat of a normalized path. */
static int mc_stat(int port, const char *path, AthenaMemcardEntry *entry) {
    McArgs args = { .port = port, .path = path, .flags = 0, .maxent = 1 };
    int ret, result = 0;

    /* The root has no entry of its own: list it to check the card. */
    if (mc_is_root(path))
        args.path = "/*";

    ret = mc_lock();
    if (ret < 0)
        return ret;
    ret = mc_command_locked(mc_issue_getdir, &args, &result);
    if (ret == ATHENA_MEMCARD_OK && result > 0 && !mc_is_root(path))
        mc_entry_from_table(&mc_table[0], entry);
    mc_unlock();

    if (ret < 0)
        return ret;
    if (result < 0)
        return mc_error(result);
    if (mc_is_root(path)) {
        memset(entry, 0, sizeof(*entry));
        entry->attributes = ATHENA_MEMCARD_ATTR_DIRECTORY | ATHENA_MEMCARD_ATTR_READABLE |
            ATHENA_MEMCARD_ATTR_WRITABLE | ATHENA_MEMCARD_ATTR_EXISTS;
        return ATHENA_MEMCARD_OK;
    }
    return result == 0 ? ATHENA_MEMCARD_ERR_NOT_FOUND : ATHENA_MEMCARD_OK;
}

int athena_memcard_stat(int port, const char *path, AthenaMemcardEntry *entry) {
    McPath normalized;
    int ret;

    if (!entry)
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    ret = mc_prepare_path(port, path, normalized);
    return ret < 0 ? ret : mc_stat(port, normalized, entry);
}

/* The listing names mcGetDir sends besides the real entries. */
static bool mc_is_dot_entry(const char *name) {
    return !strcmp(name, ".") || !strcmp(name, "..");
}

static int mc_list(int port, const char *dir, AthenaMemcardEntry **out, int *out_count) {
    McArgs args = { .port = port, .flags = 0, .maxent = MC_DIR_BATCH };
    AthenaMemcardEntry *entries = NULL;
    int count = 0, capacity = 0, ret, result = 0;
    McPath pattern;
    size_t length = strlen(dir);

    *out = NULL;
    *out_count = 0;
    if (length + 2 >= sizeof(pattern))
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    memcpy(pattern, dir, length);
    if (!mc_is_root(dir))
        pattern[length++] = '/';
    pattern[length++] = '*';
    pattern[length] = '\0';
    args.path = pattern;

    ret = mc_lock();
    if (ret < 0)
        return ret;
    /* The driver keeps the listing position: hold the lock through all pages. */
    ret = mc_command_locked(mc_issue_getdir, &args, &result);
    if (ret == ATHENA_MEMCARD_OK && result < 0)
        ret = mc_error(result);
    while (ret == ATHENA_MEMCARD_OK && result > 0) {
        int batch = result > MC_DIR_BATCH ? MC_DIR_BATCH : result;

        if (count + batch > capacity) {
            AthenaMemcardEntry *grown;
            capacity = capacity ? capacity * 2 : MC_DIR_BATCH;
            grown = realloc(entries, (size_t)capacity * sizeof(*entries));
            if (!grown) {
                ret = ATHENA_MEMCARD_ERR_MEMORY;
                break;
            }
            entries = grown;
        }
        for (int i = 0; i < batch; i++) {
            mc_entry_from_table(&mc_table[i], &entries[count]);
            if (!mc_is_dot_entry(entries[count].name))
                count++;
        }
        if (count >= MC_DIR_MAX_ENTRIES) {
            ret = ATHENA_MEMCARD_ERR_IO;
            break;
        }
        /* Next page: a swap now would continue a stale listing, so no retry. */
        args.flags = 1;
        ret = mc_finish(mc_issue_getdir(&args), &result);
        if (ret == ATHENA_MEMCARD_OK && result < 0)
            ret = mc_fd_result(port, result);
    }
    mc_unlock();

    if (ret < 0) {
        free(entries);
        /* A file named as the directory reads as "not found": tell them apart. */
        if (ret == ATHENA_MEMCARD_ERR_NOT_FOUND) {
            AthenaMemcardEntry entry;
            if (mc_stat(port, dir, &entry) == ATHENA_MEMCARD_OK && !athena_memcard_entry_is_dir(&entry))
                ret = ATHENA_MEMCARD_ERR_NOT_DIR;
        }
        return ret;
    }
    *out = entries;
    *out_count = count;
    return ATHENA_MEMCARD_OK;
}

int athena_memcard_list(int port, const char *dir, AthenaMemcardEntry **entries, int *count) {
    McPath normalized;
    int ret;

    if (!entries || !count)
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    *entries = NULL;
    *count = 0;
    ret = mc_prepare_path(port, dir, normalized);
    return ret < 0 ? ret : mc_list(port, normalized, entries, count);
}

/* Creates one directory: 1 created, 0 already a directory, or an error. */
static int mc_mkdir_one(int port, const char *path) {
    McArgs args = { .port = port, .path = path };
    AthenaMemcardEntry entry;
    int ret = mc_command(mc_issue_mkdir, &args);

    if (ret >= 0)
        return 1;
    /* The driver says "no entry" both for an existing name and a missing parent. */
    if (ret == ATHENA_MEMCARD_ERR_NOT_FOUND && mc_stat(port, path, &entry) == ATHENA_MEMCARD_OK)
        return athena_memcard_entry_is_dir(&entry) ? 0 : ATHENA_MEMCARD_ERR_EXISTS;
    return ret;
}

/*
 * mkdir -p from the leaf up: an existing directory costs two commands and a
 * new one under an existing parent one, whatever the depth. `path` is
 * modified and restored.
 */
static int mc_mkdir_parents(int port, char *path) {
    int ret = mc_mkdir_one(port, path);
    char *slash;

    /* Neither created nor there: the parent is missing. */
    if (ret != ATHENA_MEMCARD_ERR_NOT_FOUND)
        return ret;
    slash = strrchr(path, '/');
    if (slash == path)
        return ret;
    *slash = '\0';
    ret = mc_mkdir_parents(port, path);
    *slash = '/';
    return ret < 0 ? ret : mc_mkdir_one(port, path);
}

static int mc_mkdir(int port, const char *path, bool recursive) {
    McPath copy;
    int ret;

    if (mc_is_root(path))
        return recursive ? 0 : ATHENA_MEMCARD_ERR_EXISTS;
    if (!recursive) {
        ret = mc_mkdir_one(port, path);
        return ret == 0 ? ATHENA_MEMCARD_ERR_EXISTS : ret;
    }
    strcpy(copy, path);
    return mc_mkdir_parents(port, copy);
}

int athena_memcard_mkdir(int port, const char *path, bool recursive) {
    McPath normalized;
    int ret = mc_prepare_path(port, path, normalized);
    return ret < 0 ? ret : mc_mkdir(port, normalized, recursive);
}

typedef struct {
    AthenaMemcardProgress progress;
    void *user;
    uint64_t done;
} McRemoveState;

static int mc_delete(int port, const char *path) {
    McArgs args = { .port = port, .path = path };
    int ret = mc_command(mc_issue_delete, &args);
    return ret < 0 ? ret : ATHENA_MEMCARD_OK;
}

/* `path` is a buffer of McPath size that the recursion extends and restores. */
static int mc_remove_tree(int port, char *path, int depth, McRemoveState *state) {
    AthenaMemcardEntry *entries = NULL;
    size_t length = strlen(path);
    int count = 0, ret;

    if (depth > MC_TREE_MAX_DEPTH)
        return ATHENA_MEMCARD_ERR_IO;
    ret = mc_list(port, path, &entries, &count);
    if (ret < 0)
        return ret;

    for (int i = 0; i < count && ret == ATHENA_MEMCARD_OK; i++) {
        size_t name_length = strlen(entries[i].name);
        if (length + 1 + name_length >= sizeof(McPath)) {
            ret = ATHENA_MEMCARD_ERR_ARGUMENT;
            break;
        }
        path[length] = '/';
        memcpy(path + length + 1, entries[i].name, name_length + 1);
        if (athena_memcard_entry_is_dir(&entries[i]))
            ret = mc_remove_tree(port, path, depth + 1, state);
        else if (state->progress && state->progress(state->done, 0, state->user) < 0)
            ret = ATHENA_MEMCARD_ERR_CANCELLED;
        else if ((ret = mc_delete(port, path)) == ATHENA_MEMCARD_OK)
            state->done++;
        path[length] = '\0';
    }
    free(entries);
    if (ret < 0)
        return ret;
    if (state->progress && state->progress(state->done, 0, state->user) < 0)
        return ATHENA_MEMCARD_ERR_CANCELLED;
    ret = mc_delete(port, path);
    if (ret == ATHENA_MEMCARD_OK)
        state->done++;
    return ret;
}

int athena_memcard_remove(int port, const char *path, bool recursive,
    AthenaMemcardProgress progress, void *user) {
    McRemoveState state = { .progress = progress, .user = user };
    AthenaMemcardEntry entry;
    McPath normalized;
    int ret = mc_prepare_path(port, path, normalized);

    if (ret < 0)
        return ret;
    if (mc_is_root(normalized))
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    if (!recursive)
        return mc_delete(port, normalized);
    ret = mc_stat(port, normalized, &entry);
    if (ret < 0)
        return ret;
    if (!athena_memcard_entry_is_dir(&entry))
        return mc_delete(port, normalized);
    return mc_remove_tree(port, normalized, 0, &state);
}

/* Path of `name` next to the entry at `path`. */
static int mc_sibling(const char *path, const char *name, McPath out) {
    size_t parent = (size_t)(mc_basename(path) - path);
    size_t length = strlen(name);

    if (parent + length >= sizeof(McPath))
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    memcpy(out, path, parent);
    memcpy(out + parent, name, length + 1);
    return ATHENA_MEMCARD_OK;
}

static int mc_rename(int port, const char *path, const char *new_name) {
    McArgs args = { .port = port, .path = path, .name = new_name };
    AthenaMemcardEntry entry;
    McPath target;
    int ret;

    if (!strcmp(mc_basename(path), new_name))
        return mc_stat(port, path, &entry);
    ret = mc_sibling(path, new_name, target);
    if (ret < 0)
        return ret;
    /* The driver reports an existing target as "access denied". */
    ret = mc_stat(port, target, &entry);
    if (ret == ATHENA_MEMCARD_OK)
        return ATHENA_MEMCARD_ERR_EXISTS;
    if (ret != ATHENA_MEMCARD_ERR_NOT_FOUND)
        return ret;
    ret = mc_command(mc_issue_rename, &args);
    return ret < 0 ? ret : ATHENA_MEMCARD_OK;
}

int athena_memcard_rename(int port, const char *path, const char *new_name) {
    McPath normalized;
    int ret;

    if (!new_name || !mc_valid_name(new_name, strlen(new_name)))
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    ret = mc_prepare_path(port, path, normalized);
    if (ret < 0)
        return ret;
    if (mc_is_root(normalized))
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    return mc_rename(port, normalized, new_name);
}

int athena_memcard_free_entries(int port, const char *dir) {
    McPath normalized;
    McArgs args = { .port = port, .path = normalized };
    int ret = mc_prepare_path(port, dir, normalized);
    return ret < 0 ? ret : mc_command(mc_issue_ent_space, &args);
}

int athena_memcard_set_info(int port, const char *path, const AthenaMemcardSetInfo *info) {
    sceMcTblGetDir table;
    McPath normalized;
    McArgs args = { .port = port, .path = normalized, .info = &table };
    int ret;

    if (!info || (info->fields & ~(unsigned)(ATHENA_MEMCARD_SET_CREATED |
        ATHENA_MEMCARD_SET_MODIFIED | ATHENA_MEMCARD_SET_ATTRIBUTES)))
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    ret = mc_prepare_path(port, path, normalized);
    if (ret < 0)
        return ret;
    if (mc_is_root(normalized))
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    if (!info->fields)
        return ATHENA_MEMCARD_OK;

    memset(&table, 0, sizeof(table));
    if (info->fields & ATHENA_MEMCARD_SET_ATTRIBUTES) {
        AthenaMemcardEntry entry;
        /* The driver copies most mode bits: carry the others over unchanged. */
        ret = mc_stat(port, normalized, &entry);
        if (ret < 0)
            return ret;
        table.AttrFile = (uint16_t)((entry.attributes & ~ATHENA_MEMCARD_ATTR_SETTABLE) |
            (info->attributes & ATHENA_MEMCARD_ATTR_SETTABLE));
        args.flags |= sceMcFileInfoAttr;
    }
    if (info->fields & ATHENA_MEMCARD_SET_CREATED) {
        mc_time_out(&info->created, &table._Create);
        args.flags |= sceMcFileInfoCreate;
    }
    if (info->fields & ATHENA_MEMCARD_SET_MODIFIED) {
        mc_time_out(&info->modified, &table._Modify);
        args.flags |= sceMcFileInfoModify;
    }
    ret = mc_command(mc_issue_set_info, &args);
    return ret < 0 ? ret : ATHENA_MEMCARD_OK;
}

/* ------------------------------------------------------------------------ */
/* Descriptors                                                              */
/* ------------------------------------------------------------------------ */

/* Opens a normalized path without checking what it names. */
static int mc_open_fd(int port, const char *path, int flags, McFd *out) {
    McArgs args = { .port = port, .path = path, .flags = flags };
    int ret, result = 0;

    ret = mc_lock();
    if (ret < 0)
        return ret;
    ret = mc_command_locked(mc_issue_open, &args, &result);
    if (ret == ATHENA_MEMCARD_OK && result >= 0) {
        out->port = port;
        out->fd = result;
        out->epoch = mc_ports[port].epoch;
    }
    mc_unlock();
    if (ret < 0)
        return ret;
    return result < 0 ? mc_error(result) : result;
}

/* Takes the lock for a command on `fd`, unless its card is gone. */
static int mc_fd_lock(const McFd *fd) {
    int ret = mc_lock();

    if (ret < 0)
        return ret;
    if (mc_ports[fd->port].epoch != fd->epoch) {
        mc_unlock();
        return ATHENA_MEMCARD_ERR_CHANGED;
    }
    return ATHENA_MEMCARD_OK;
}

/*
 * A descriptor of a card that left is not closed: after a swap the driver
 * would rewrite its directory entry on the new card. Removing the card
 * already released it; after a quick swap it stays taken until the card
 * leaves again.
 */
static int mc_close_fd(const McFd *fd) {
    int ret = mc_fd_lock(fd);

    if (ret < 0)
        return ret;
    ret = mc_fd_command(fd->port, mcClose(fd->fd));
    mc_unlock();
    return ret < 0 ? ret : ATHENA_MEMCARD_OK;
}

/* Reads up to `size` bytes; stops early at the end of the file. */
static int mc_read_fd(const McFd *fd, uint8_t *buffer, size_t size, size_t *done,
    uint64_t total, AthenaMemcardProgress progress, void *user) {
    *done = 0;
    while (*done < size) {
        size_t chunk = size - *done > MC_BLOCK_SIZE ? MC_BLOCK_SIZE : size - *done;
        int ret;

        if (progress && progress(*done, total, user) < 0)
            return ATHENA_MEMCARD_ERR_CANCELLED;
        ret = mc_fd_lock(fd);
        if (ret < 0)
            return ret;
        ret = mc_fd_command(fd->port, mcRead(fd->fd, buffer + *done, (int)chunk));
        mc_unlock();
        if (ret < 0)
            return ret;
        *done += (size_t)ret;
        if ((size_t)ret < chunk)
            break;
    }
    if (progress)
        progress(*done, total, user);
    return ATHENA_MEMCARD_OK;
}

static int mc_write_fd(const McFd *fd, const uint8_t *buffer, size_t size, size_t *done,
    AthenaMemcardProgress progress, void *user) {
    *done = 0;
    while (*done < size) {
        size_t chunk = size - *done > MC_BLOCK_SIZE ? MC_BLOCK_SIZE : size - *done;
        int ret;

        if (progress && progress(*done, size, user) < 0)
            return ATHENA_MEMCARD_ERR_CANCELLED;
        ret = mc_fd_lock(fd);
        if (ret < 0)
            return ret;
        ret = mc_fd_command(fd->port, mcWrite(fd->fd, buffer + *done, (int)chunk));
        mc_unlock();
        if (ret < 0)
            return ret;
        *done += (size_t)ret;
        if ((size_t)ret < chunk)
            return ATHENA_MEMCARD_ERR_FULL;
    }
    if (progress)
        progress(*done, size, user);
    return ATHENA_MEMCARD_OK;
}

/* ------------------------------------------------------------------------ */
/* Whole files                                                              */
/* ------------------------------------------------------------------------ */

int athena_memcard_read_file(int port, const char *path, void **data, size_t *size,
    AthenaMemcardProgress progress, void *user) {
    AthenaMemcardEntry entry;
    McPath normalized;
    uint8_t *buffer;
    size_t done = 0;
    McFd fd;
    int ret, closed;

    if (!data || !size)
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    *data = NULL;
    *size = 0;
    ret = mc_prepare_path(port, path, normalized);
    if (ret < 0)
        return ret;
    ret = mc_stat(port, normalized, &entry);
    if (ret < 0)
        return ret;
    if (athena_memcard_entry_is_dir(&entry))
        return ATHENA_MEMCARD_ERR_IS_DIR;

    /*
     * Whole cache lines, so the DMA cannot touch a neighbouring allocation,
     * with room for the terminator after the data.
     */
    buffer = memalign(64, ((size_t)entry.size + 64) & ~(size_t)63);
    if (!buffer)
        return ATHENA_MEMCARD_ERR_MEMORY;

    ret = mc_open_fd(port, normalized, ATHENA_MEMCARD_OPEN_READ, &fd);
    if (ret < 0) {
        free(buffer);
        return ret;
    }
    ret = mc_read_fd(&fd, buffer, entry.size, &done, entry.size, progress, user);
    closed = mc_close_fd(&fd);
    if (ret == ATHENA_MEMCARD_OK && closed < 0)
        ret = closed;
    if (ret < 0) {
        free(buffer);
        return ret;
    }
    buffer[done] = '\0';
    *data = buffer;
    *size = done;
    return ATHENA_MEMCARD_OK;
}

/* Name of the temporary copy an atomic write uses: "<name>~". */
static int mc_temp_path(const char *path, McPath out) {
    char name[ATHENA_MEMCARD_NAME_MAX + 1];
    size_t length = strlen(mc_basename(path));

    if (length >= ATHENA_MEMCARD_NAME_MAX)
        length = ATHENA_MEMCARD_NAME_MAX - 1;
    memcpy(name, mc_basename(path), length);
    name[length] = '~';
    name[length + 1] = '\0';
    return mc_sibling(path, name, out);
}

int athena_memcard_write_file(int port, const char *path, const void *data, size_t size,
    const AthenaMemcardWriteOptions *options) {
    AthenaMemcardWriteOptions defaults = { 0 };
    AthenaMemcardEntry entry;
    McPath normalized, temp;
    const char *target;
    bool exists;
    size_t done = 0;
    McFd fd;
    int ret, closed;

    if ((!data && size) || size > INT32_MAX)
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    if (!options)
        options = &defaults;
    ret = mc_prepare_path(port, path, normalized);
    if (ret < 0)
        return ret;
    if (mc_is_root(normalized))
        return ATHENA_MEMCARD_ERR_IS_DIR;

    ret = mc_stat(port, normalized, &entry);
    if (ret < 0 && ret != ATHENA_MEMCARD_ERR_NOT_FOUND)
        return ret;
    exists = ret == ATHENA_MEMCARD_OK;
    if (exists && athena_memcard_entry_is_dir(&entry))
        return ATHENA_MEMCARD_ERR_IS_DIR;

    target = normalized;
    if (options->atomic) {
        ret = mc_temp_path(normalized, temp);
        if (ret < 0)
            return ret;
        target = temp;
    }

    ret = mc_open_fd(port, target, ATHENA_MEMCARD_OPEN_WRITE | ATHENA_MEMCARD_OPEN_CREATE, &fd);
    /* A missing parent reads as "not found": create the parents only then. */
    if (ret == ATHENA_MEMCARD_ERR_NOT_FOUND && !exists && options->create_dirs) {
        McPath parent;
        size_t length = (size_t)(mc_basename(normalized) - normalized);
        memcpy(parent, normalized, length);
        parent[length > 1 ? length - 1 : length] = '\0';
        ret = mc_mkdir(port, parent, true);
        if (ret >= 0)
            ret = mc_open_fd(port, target, ATHENA_MEMCARD_OPEN_WRITE | ATHENA_MEMCARD_OPEN_CREATE, &fd);
    }
    if (ret < 0)
        return ret;
    ret = mc_write_fd(&fd, data, size, &done, options->progress, options->user);
    closed = mc_close_fd(&fd);
    if (ret == ATHENA_MEMCARD_OK && closed < 0)
        ret = closed;
    if (ret < 0) {
        /* Never leave a partial file behind. */
        if (ret != ATHENA_MEMCARD_ERR_CHANGED)
            mc_delete(port, target);
        return ret;
    }

    if (options->atomic) {
        if (exists) {
            ret = mc_delete(port, normalized);
            if (ret < 0) {
                mc_delete(port, temp);
                return ret;
            }
        }
        /* Past this point the new data is safe in "<name>~" whatever happens. */
        McArgs args = { .port = port, .path = temp, .name = mc_basename(normalized) };
        ret = mc_command(mc_issue_rename, &args);
        if (ret < 0)
            return ret;
    }
    return (int)done;
}

/* ------------------------------------------------------------------------ */
/* Open files                                                               */
/* ------------------------------------------------------------------------ */

int athena_memcard_open(int port, const char *path, int flags, AthenaMemcardFile **out) {
    const int known = ATHENA_MEMCARD_OPEN_READ | ATHENA_MEMCARD_OPEN_WRITE | ATHENA_MEMCARD_OPEN_CREATE |
        ATHENA_MEMCARD_OPEN_APPEND;
    AthenaMemcardEntry entry;
    AthenaMemcardFile *file;
    McPath normalized;
    bool exists, append = (flags & ATHENA_MEMCARD_OPEN_APPEND) != 0;
    int ret;

    if (!out)
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    *out = NULL;
    if ((flags & ~known) || !(flags & (ATHENA_MEMCARD_OPEN_READ | ATHENA_MEMCARD_OPEN_WRITE)) ||
        ((flags & (ATHENA_MEMCARD_OPEN_CREATE | ATHENA_MEMCARD_OPEN_APPEND)) &&
         !(flags & ATHENA_MEMCARD_OPEN_WRITE)))
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    ret = mc_prepare_path(port, path, normalized);
    if (ret < 0)
        return ret;
    if (mc_is_root(normalized))
        return ATHENA_MEMCARD_ERR_IS_DIR;

    /* The driver opens directories too, and "create" would replace one. */
    ret = mc_stat(port, normalized, &entry);
    if (ret == ATHENA_MEMCARD_OK && athena_memcard_entry_is_dir(&entry))
        return ATHENA_MEMCARD_ERR_IS_DIR;
    exists = ret == ATHENA_MEMCARD_OK;
    if (!exists && !(ret == ATHENA_MEMCARD_ERR_NOT_FOUND && ((flags & ATHENA_MEMCARD_OPEN_CREATE) || append)))
        return ret;

    /* Append creates a missing file and keeps an existing one. */
    flags &= ~ATHENA_MEMCARD_OPEN_APPEND;
    if (append)
        flags = exists ? flags & ~ATHENA_MEMCARD_OPEN_CREATE : flags | ATHENA_MEMCARD_OPEN_CREATE;

    file = calloc(1, sizeof(*file));
    if (!file)
        return ATHENA_MEMCARD_ERR_MEMORY;
    ret = mc_open_fd(port, normalized, flags, &file->fd);
    if (ret < 0) {
        free(file);
        return ret;
    }
    file->flags = flags;
    file->size = exists && !(flags & ATHENA_MEMCARD_OPEN_CREATE) ? (int)entry.size : 0;
    file->generation = memcard_driver_generation();
    if (append && file->size > 0) {
        ret = athena_memcard_seek(file, 0, ATHENA_MEMCARD_SEEK_END);
        if (ret < 0) {
            athena_memcard_close(file);
            return ret;
        }
    }
    *out = file;
    return ATHENA_MEMCARD_OK;
}

/*
 * Open and from this IOP boot: after an IOP reset the number may belong to
 * a file someone else opened. The card itself is checked by mc_fd_lock().
 */
static int mc_file_check(const AthenaMemcardFile *file) {
    if (!file || file->fd.fd < 0)
        return ATHENA_MEMCARD_ERR_CLOSED;
    if (file->generation != memcard_driver_generation())
        return ATHENA_MEMCARD_ERR_CLOSED;
    return ATHENA_MEMCARD_OK;
}

int athena_memcard_read(AthenaMemcardFile *file, void *buffer, size_t size) {
    size_t done = 0;
    int ret = mc_file_check(file);

    if (ret < 0)
        return ret;
    if ((!buffer && size) || size > INT32_MAX)
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    if (!(file->flags & ATHENA_MEMCARD_OPEN_READ))
        return ATHENA_MEMCARD_ERR_DENIED;
    ret = mc_read_fd(&file->fd, buffer, size, &done, size, NULL, NULL);
    file->position += (int)done;
    return ret < 0 ? ret : (int)done;
}

int athena_memcard_write(AthenaMemcardFile *file, const void *buffer, size_t size) {
    size_t done = 0;
    int ret = mc_file_check(file);

    if (ret < 0)
        return ret;
    if ((!buffer && size) || size > INT32_MAX)
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    if (!(file->flags & ATHENA_MEMCARD_OPEN_WRITE))
        return ATHENA_MEMCARD_ERR_DENIED;
    ret = mc_write_fd(&file->fd, buffer, size, &done, NULL, NULL);
    file->position += (int)done;
    if (file->position > file->size)
        file->size = file->position;
    return ret < 0 ? ret : (int)done;
}

int athena_memcard_seek(AthenaMemcardFile *file, int offset, int whence) {
    int ret = mc_file_check(file);

    if (ret < 0)
        return ret;
    if (whence < ATHENA_MEMCARD_SEEK_SET || whence > ATHENA_MEMCARD_SEEK_END)
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    ret = mc_fd_lock(&file->fd);
    if (ret < 0)
        return ret;
    ret = mc_fd_command(file->fd.port, mcSeek(file->fd.fd, offset, whence));
    mc_unlock();
    if (ret >= 0)
        file->position = ret;
    return ret;
}

int athena_memcard_tell(const AthenaMemcardFile *file) {
    int ret = mc_file_check(file);
    return ret < 0 ? ret : file->position;
}

int athena_memcard_size(const AthenaMemcardFile *file) {
    int ret = mc_file_check(file);
    return ret < 0 ? ret : file->size;
}

int athena_memcard_flush(AthenaMemcardFile *file) {
    int ret = mc_file_check(file);

    if (ret < 0)
        return ret;
    ret = mc_fd_lock(&file->fd);
    if (ret < 0)
        return ret;
    ret = mc_fd_command(file->fd.port, mcFlush(file->fd.fd));
    mc_unlock();
    return ret < 0 ? ret : ATHENA_MEMCARD_OK;
}

int athena_memcard_close(AthenaMemcardFile *file) {
    int ret;

    if (!file)
        return ATHENA_MEMCARD_ERR_CLOSED;
    ret = mc_file_check(file);
    if (ret == ATHENA_MEMCARD_OK)
        ret = mc_close_fd(&file->fd);
    file->fd.fd = -1;
    free(file);
    return ret < 0 ? ret : ATHENA_MEMCARD_OK;
}

/* ------------------------------------------------------------------------ */
/* icon.sys                                                                 */
/* ------------------------------------------------------------------------ */

/* Full-width Shift-JIS of printable ASCII, 0 when there is none. */
static uint16_t mc_sjis(char c) {
    static const struct { char c; uint16_t code; } punctuation[] = {
        { ' ', 0x8140 }, { '!', 0x8149 }, { '"', 0x8168 }, { '#', 0x8194 }, { '$', 0x8190 },
        { '%', 0x8193 }, { '&', 0x8195 }, { '\'', 0x8166 }, { '(', 0x8169 }, { ')', 0x816A },
        { '*', 0x8196 }, { '+', 0x817B }, { ',', 0x8143 }, { '-', 0x817C }, { '.', 0x8144 },
        { '/', 0x815E }, { ':', 0x8146 }, { ';', 0x8147 }, { '<', 0x8183 }, { '=', 0x8181 },
        { '>', 0x8184 }, { '?', 0x8148 }, { '@', 0x8197 }, { '[', 0x816D }, { '\\', 0x815F },
        { ']', 0x816E }, { '^', 0x814F }, { '_', 0x8151 }, { '`', 0x814D }, { '{', 0x816F },
        { '|', 0x8162 }, { '}', 0x8170 }, { '~', 0x8160 },
    };

    if (c >= '0' && c <= '9') return (uint16_t)(0x824F + (c - '0'));
    if (c >= 'A' && c <= 'Z') return (uint16_t)(0x8260 + (c - 'A'));
    if (c >= 'a' && c <= 'z') return (uint16_t)(0x8281 + (c - 'a'));
    for (size_t i = 0; i < sizeof(punctuation) / sizeof(punctuation[0]); i++)
        if (punctuation[i].c == c)
            return punctuation[i].code;
    return 0;
}

void athena_memcard_icon_sys_defaults(AthenaMemcardIconSys *icon) {
    static const int background[4][3] = {
        { 68, 23, 116 }, { 255, 255, 255 }, { 255, 255, 255 }, { 68, 23, 116 },
    };
    static const float light_dir[3][3] = {
        { 0.5f, 0.5f, 0.5f }, { 0.0f, -0.4f, -0.1f }, { -0.5f, -0.5f, 0.5f },
    };
    static const float light_color[3][3] = {
        { 0.3f, 0.3f, 0.3f }, { 0.4f, 0.4f, 0.4f }, { 0.5f, 0.5f, 0.5f },
    };

    memset(icon, 0, sizeof(*icon));
    icon->background_alpha = 0x60;
    memcpy(icon->background, background, sizeof(background));
    memcpy(icon->light_dir, light_dir, sizeof(light_dir));
    memcpy(icon->light_color, light_color, sizeof(light_color));
    icon->ambient[0] = icon->ambient[1] = icon->ambient[2] = 0.5f;
}

static void mc_put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void mc_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void mc_put_float(uint8_t *p, float v) {
    uint32_t bits;
    memcpy(&bits, &v, sizeof(bits));
    mc_put_u32(p, bits);
}

/* Icon file name: 1-63 printable bytes, no directory. */
static bool mc_valid_icon_name(const char *name) {
    size_t length;

    if (!name)
        return false;
    length = strlen(name);
    if (length == 0 || length > 63)
        return false;
    for (size_t i = 0; i < length; i++)
        if (!mc_valid_name_char((unsigned char)name[i]))
            return false;
    return true;
}

int athena_memcard_build_icon_sys(const AthenaMemcardIconSys *icon,
    uint8_t out[ATHENA_MEMCARD_ICON_SYS_SIZE]) {
    const char *copy_icon, *delete_icon;
    size_t chars = 0, title_offset = 192, line_break = 0;
    bool has_break = false;

    if (!icon || !out || !icon->title || !mc_valid_icon_name(icon->icon))
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    copy_icon = icon->copy_icon ? icon->copy_icon : icon->icon;
    delete_icon = icon->delete_icon ? icon->delete_icon : icon->icon;
    if (!mc_valid_icon_name(copy_icon) || !mc_valid_icon_name(delete_icon) ||
        icon->background_alpha > 0x80)
        return ATHENA_MEMCARD_ERR_ARGUMENT;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 3; j++)
            if (icon->background[i][j] < 0 || icon->background[i][j] > 255)
                return ATHENA_MEMCARD_ERR_ARGUMENT;

    memset(out, 0, ATHENA_MEMCARD_ICON_SYS_SIZE);
    for (const char *p = icon->title; *p; p++) {
        uint16_t code;
        if (*p == '\n') {
            if (has_break)
                return ATHENA_MEMCARD_ERR_ARGUMENT;
            has_break = true;
            line_break = chars * 2;
            continue;
        }
        code = mc_sjis(*p);
        /* 68 bytes, the last two a terminator. */
        if (!code || chars >= 33)
            return ATHENA_MEMCARD_ERR_ARGUMENT;
        out[title_offset + chars * 2] = (uint8_t)(code >> 8);
        out[title_offset + chars * 2 + 1] = (uint8_t)code;
        chars++;
    }
    if (chars == 0)
        return ATHENA_MEMCARD_ERR_ARGUMENT;

    memcpy(out, "PS2D", 4);
    mc_put_u16(out + 4, 0);                         /* saved data */
    mc_put_u16(out + 6, (uint16_t)(has_break ? line_break : chars * 2));
    mc_put_u32(out + 12, icon->background_alpha);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 3; j++)
            mc_put_u32(out + 16 + i * 16 + j * 4, (uint32_t)icon->background[i][j]);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            mc_put_float(out + 80 + i * 16 + j * 4, icon->light_dir[i][j]);
            mc_put_float(out + 128 + i * 16 + j * 4, icon->light_color[i][j]);
        }
        mc_put_float(out + 176 + i * 4, icon->ambient[i]);
    }
    memcpy(out + 260, icon->icon, strlen(icon->icon));
    memcpy(out + 324, copy_icon, strlen(copy_icon));
    memcpy(out + 388, delete_icon, strlen(delete_icon));
    return ATHENA_MEMCARD_OK;
}
