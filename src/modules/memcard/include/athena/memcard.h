#ifndef ATHENA_MEMCARD_H
#define ATHENA_MEMCARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Memory Card access through libmc (XMCMAN/XMCSERV).
 *
 * libmc keeps one command in flight for the whole EE and has no locking:
 * every call here takes a module-wide lock around each command, so the
 * functions are safe from any thread. Waiting for the IOP polls with
 * DelayThread() instead of spinning, so other threads keep running.
 *
 * Paths are absolute on the card ("/SAVE/data.bin"); `port` is 0 (mc0:)
 * or 1 (mc1:). Names are at most ATHENA_MEMCARD_NAME_MAX bytes and cannot
 * contain '*', '?' or control characters. "." and ".." are resolved
 * before the path reaches the driver.
 *
 * A card swapped since the last access fails the first command with
 * "changed card"; path operations acknowledge it and retry once, as the
 * iomanX mc device does. Open files are lost with the card: once the card
 * leaves or another one is detected, their operations fail with
 * ATHENA_MEMCARD_ERR_CHANGED without reaching the driver, which would
 * otherwise read, write or close (rewriting a directory entry) with the old
 * card's clusters on the new one. A swap only iomanX noticed (fopen) is not
 * seen here.
 *
 * Unsupported by the XMCSERV driver this module embeds, so not exposed:
 * raw page and block access (mcReadPage/mcWritePage/mcEraseBlock),
 * mcChangeThreadPriority (ignored by libmc) and multitap slots.
 */

#define ATHENA_MEMCARD_PORTS 2
#define ATHENA_MEMCARD_NAME_MAX 31
#define ATHENA_MEMCARD_PATH_MAX 1023
/* File handles the driver has for every card together (fopen("mc0:") included). */
#define ATHENA_MEMCARD_MAX_OPEN 3
#define ATHENA_MEMCARD_CLUSTER_SIZE 1024
#define ATHENA_MEMCARD_ICON_SYS_SIZE 964

/* Result codes. Every failing call returns one of the negative values. */
#define ATHENA_MEMCARD_OK                 0
#define ATHENA_MEMCARD_ERR_ARGUMENT      -1
#define ATHENA_MEMCARD_ERR_NOT_READY     -2   /* drivers not loaded or libmc not initialized */
#define ATHENA_MEMCARD_ERR_NO_CARD       -3   /* no card, or it failed detection/authentication */
#define ATHENA_MEMCARD_ERR_UNFORMATTED   -4
#define ATHENA_MEMCARD_ERR_CHANGED       -5   /* the card was swapped: the open file is gone */
#define ATHENA_MEMCARD_ERR_FULL          -6
#define ATHENA_MEMCARD_ERR_NOT_FOUND     -7
#define ATHENA_MEMCARD_ERR_EXISTS        -8
#define ATHENA_MEMCARD_ERR_DENIED        -9   /* writing a read-only file (the driver still deletes it), or a file open twice for writing */
#define ATHENA_MEMCARD_ERR_NOT_EMPTY     -10
#define ATHENA_MEMCARD_ERR_TOO_MANY_OPEN -11
#define ATHENA_MEMCARD_ERR_IS_DIR        -12
#define ATHENA_MEMCARD_ERR_NOT_DIR       -13
#define ATHENA_MEMCARD_ERR_UNSUPPORTED   -14  /* PS1/PocketStation cards and ops their driver lacks */
#define ATHENA_MEMCARD_ERR_MEMORY        -15
#define ATHENA_MEMCARD_ERR_IO            -16
#define ATHENA_MEMCARD_ERR_CLOSED        -17  /* file closed, or lost to an IOP reset */
#define ATHENA_MEMCARD_ERR_CANCELLED     -18
#define ATHENA_MEMCARD_ERR_BUSY          -19

typedef enum AthenaMemcardType {
    ATHENA_MEMCARD_TYPE_NONE = 0,
    ATHENA_MEMCARD_TYPE_PS1 = 1,
    ATHENA_MEMCARD_TYPE_PS2 = 2,
    ATHENA_MEMCARD_TYPE_POCKETSTATION = 3,
} AthenaMemcardType;

/* Entry attributes, as stored on the card (sceMcFileAttr*). */
#define ATHENA_MEMCARD_ATTR_READABLE   0x0001
#define ATHENA_MEMCARD_ATTR_WRITABLE   0x0002
#define ATHENA_MEMCARD_ATTR_EXECUTABLE 0x0004
#define ATHENA_MEMCARD_ATTR_PROTECTED  0x0008  /* copy-protected in the browser */
#define ATHENA_MEMCARD_ATTR_FILE       0x0010
#define ATHENA_MEMCARD_ATTR_DIRECTORY  0x0020
#define ATHENA_MEMCARD_ATTR_CLOSED     0x0080
#define ATHENA_MEMCARD_ATTR_PDA_EXEC   0x0800
#define ATHENA_MEMCARD_ATTR_PS1        0x1000
#define ATHENA_MEMCARD_ATTR_HIDDEN     0x2000
#define ATHENA_MEMCARD_ATTR_EXISTS     0x8000
/* Bits athena_memcard_set_info() changes: readable, writable, executable, protected, hidden. */
#define ATHENA_MEMCARD_ATTR_SETTABLE   0x200F

typedef struct AthenaMemcardInfo {
    AthenaMemcardType type;
    bool formatted;
    uint32_t free_clusters;     /* ATHENA_MEMCARD_CLUSTER_SIZE bytes each */
    bool changed;               /* a card was inserted since the previous get_info on this port */
} AthenaMemcardInfo;

/* Date and time as the card stores them: the console clock, in JST (UTC+9). */
typedef struct AthenaMemcardTime {
    uint16_t year;
    uint8_t month, day, hour, min, sec;
} AthenaMemcardTime;

typedef struct AthenaMemcardEntry {
    char name[ATHENA_MEMCARD_NAME_MAX + 1];
    uint32_t size;              /* bytes; 0 for a directory */
    uint16_t attributes;
    AthenaMemcardTime created;
    AthenaMemcardTime modified;
} AthenaMemcardEntry;

static inline bool athena_memcard_entry_is_dir(const AthenaMemcardEntry *entry) {
    return (entry->attributes & ATHENA_MEMCARD_ATTR_DIRECTORY) != 0;
}

/* Unix seconds <-> card time. Times before 1970 or after 2105 are clamped. */
int64_t athena_memcard_time_to_unix(const AthenaMemcardTime *time);
void athena_memcard_time_from_unix(int64_t seconds, AthenaMemcardTime *time);

/*
 * Progress of long operations: bytes done out of total. Return < 0 to stop,
 * which fails the operation with ATHENA_MEMCARD_ERR_CANCELLED.
 */
typedef int (*AthenaMemcardProgress)(uint64_t done, uint64_t total, void *user);

/* ------------------------------------------------------------------------ */
/* Driver and paths                                                         */
/* ------------------------------------------------------------------------ */

/*
 * Starts the drivers when athena.ini kept them from loading at boot. It
 * uses the IOP manager, so only the script thread may call it. Returns
 * ATHENA_MEMCARD_OK or ATHENA_MEMCARD_ERR_NOT_READY.
 */
int athena_memcard_prepare(void);

/*
 * Parses "mc0:/DIR/file" (also "mc1:", "mc0:DIR" and "mc0:") into a port
 * and a normalized absolute path ("/" for the root).
 */
int athena_memcard_parse_path(const char *path, int *port, char *out, size_t size);
/* Normalizes an absolute or root-relative card path. */
int athena_memcard_normalize(const char *path, char *out, size_t size);

/* ------------------------------------------------------------------------ */
/* Card                                                                     */
/* ------------------------------------------------------------------------ */

/* Succeeds with type NONE when the slot is empty. */
int athena_memcard_get_info(int port, AthenaMemcardInfo *info);
/*
 * athena_memcard_get_info() with plain ints, for code that cannot include
 * this header (System.getMCInfo references it weakly).
 */
int athena_memcard_get_info_raw(int port, int *type, int *free_clusters, int *formatted);
/* Erases the whole card. Not cancellable; takes several seconds. */
int athena_memcard_format(int port);
int athena_memcard_unformat(int port);

/* ------------------------------------------------------------------------ */
/* Entries                                                                  */
/* ------------------------------------------------------------------------ */

int athena_memcard_stat(int port, const char *path, AthenaMemcardEntry *entry);
/* Directory contents without "." and "..". Free *entries with free(). */
int athena_memcard_list(int port, const char *dir, AthenaMemcardEntry **entries, int *count);
/*
 * Creates a directory. Returns 1 when created, 0 when it already existed
 * (recursive only; otherwise ATHENA_MEMCARD_ERR_EXISTS). recursive also
 * creates the missing parents.
 */
int athena_memcard_mkdir(int port, const char *path, bool recursive);
/* Removes a file or an empty directory; recursive removes a whole tree. */
int athena_memcard_remove(int port, const char *path, bool recursive,
    AthenaMemcardProgress progress, void *user);
/* Renames in place: new_name is a name, not a path. */
int athena_memcard_rename(int port, const char *path, const char *new_name);
/* Free directory entries left in `dir` (each directory holds a fixed number). */
int athena_memcard_free_entries(int port, const char *dir);

#define ATHENA_MEMCARD_SET_CREATED    0x01
#define ATHENA_MEMCARD_SET_MODIFIED   0x02
#define ATHENA_MEMCARD_SET_ATTRIBUTES 0x04

typedef struct AthenaMemcardSetInfo {
    unsigned fields;            /* ATHENA_MEMCARD_SET_* */
    uint16_t attributes;        /* only ATHENA_MEMCARD_ATTR_SETTABLE bits change */
    AthenaMemcardTime created;
    AthenaMemcardTime modified;
} AthenaMemcardSetInfo;

int athena_memcard_set_info(int port, const char *path, const AthenaMemcardSetInfo *info);

/* ------------------------------------------------------------------------ */
/* Whole files                                                              */
/* ------------------------------------------------------------------------ */

/*
 * Reads a file into a new buffer, followed by a '\0' not counted in *size
 * (text can be parsed in place). Free *data with free().
 */
int athena_memcard_read_file(int port, const char *path, void **data, size_t *size,
    AthenaMemcardProgress progress, void *user);

typedef struct AthenaMemcardWriteOptions {
    /* Creates the missing parent directories. */
    bool create_dirs;
    /*
     * Writes a sibling "<name>~" and swaps it in only once complete, so a
     * failure or a pulled card leaves the previous file intact. Needs room
     * for both copies while it runs.
     */
    bool atomic;
    AthenaMemcardProgress progress;
    void *user;
} AthenaMemcardWriteOptions;

/*
 * Creates or replaces a file. A write that fails midway removes what it
 * wrote. Returns the bytes written. options may be NULL.
 */
int athena_memcard_write_file(int port, const char *path, const void *data, size_t size,
    const AthenaMemcardWriteOptions *options);

/* ------------------------------------------------------------------------ */
/* Open files                                                               */
/* ------------------------------------------------------------------------ */

/*
 * Streams a file. Uses one of the ATHENA_MEMCARD_MAX_OPEN driver handles
 * until it is closed. A handle is used by one thread at a time.
 */
typedef struct AthenaMemcardFile AthenaMemcardFile;

#define ATHENA_MEMCARD_OPEN_READ   0x0001
#define ATHENA_MEMCARD_OPEN_WRITE  0x0002
/* Creates the file, replacing an existing one (the driver has no O_TRUNC-less create). */
#define ATHENA_MEMCARD_OPEN_CREATE 0x0200
/* With WRITE: keeps an existing file and starts at its end, creates a missing one. */
#define ATHENA_MEMCARD_OPEN_APPEND 0x10000

#define ATHENA_MEMCARD_SEEK_SET 0
#define ATHENA_MEMCARD_SEEK_CUR 1
#define ATHENA_MEMCARD_SEEK_END 2

int athena_memcard_open(int port, const char *path, int flags, AthenaMemcardFile **file);
/* Returns the bytes read (0 at the end of the file). */
int athena_memcard_read(AthenaMemcardFile *file, void *buffer, size_t size);
/* Returns the bytes written. */
int athena_memcard_write(AthenaMemcardFile *file, const void *buffer, size_t size);
/* Returns the new position. */
int athena_memcard_seek(AthenaMemcardFile *file, int offset, int whence);
int athena_memcard_tell(const AthenaMemcardFile *file);
/* File size, kept by the handle (no command): the driver allows one writer per file. */
int athena_memcard_size(const AthenaMemcardFile *file);
int athena_memcard_flush(AthenaMemcardFile *file);
/* Closes and frees the handle, also when the close itself fails. */
int athena_memcard_close(AthenaMemcardFile *file);

/* ------------------------------------------------------------------------ */
/* icon.sys                                                                 */
/* ------------------------------------------------------------------------ */

typedef struct AthenaMemcardIconSys {
    /* Printable ASCII; '\n' splits the two lines. At most 33 characters. */
    const char *title;
    const char *icon;           /* list icon file name, required */
    const char *copy_icon;      /* NULL: same as icon */
    const char *delete_icon;    /* NULL: same as icon */
    uint8_t background_alpha;   /* 0-128 */
    int background[4][3];       /* RGB 0-255 of the corners: top-left, top-right, bottom-left, bottom-right */
    float light_dir[3][3];
    float light_color[3][3];
    float ambient[3];
} AthenaMemcardIconSys;

/* Fills the defaults of the PS2SDK sample (purple gradient, three lights). */
void athena_memcard_icon_sys_defaults(AthenaMemcardIconSys *icon);
/* Serializes icon.sys (title converted to Shift-JIS). */
int athena_memcard_build_icon_sys(const AthenaMemcardIconSys *icon,
    uint8_t out[ATHENA_MEMCARD_ICON_SYS_SIZE]);

/* ------------------------------------------------------------------------ */
/* Errors                                                                   */
/* ------------------------------------------------------------------------ */

const char *athena_memcard_strerror(int code);
/* Stable identifier for scripts, e.g. "NOT_FOUND". */
const char *athena_memcard_error_code(int code);

/* ------------------------------------------------------------------------ */
/* Background jobs                                                          */
/* ------------------------------------------------------------------------ */

/*
 * A job runs one operation on a worker thread so the caller's frame loop
 * keeps running. The caller polls the status; the job never calls back.
 */
typedef struct AthenaMemcardJob AthenaMemcardJob;

typedef enum AthenaMemcardJobState {
    ATHENA_MEMCARD_JOB_RUNNING = 0,
    ATHENA_MEMCARD_JOB_DONE,
    ATHENA_MEMCARD_JOB_FAILED,
    ATHENA_MEMCARD_JOB_CANCELLED,
} AthenaMemcardJobState;

typedef struct AthenaMemcardJobStatus {
    AthenaMemcardJobState state;
    int result;                 /* failure: negative code */
    uint64_t bytes_done;
    uint64_t bytes_total;
} AthenaMemcardJobStatus;

/* Paths and data are copied. NULL when out of memory or the worker cannot start. */
AthenaMemcardJob *athena_memcard_job_read(int port, const char *path);
AthenaMemcardJob *athena_memcard_job_write(int port, const char *path, const void *data,
    size_t size, const AthenaMemcardWriteOptions *options);
AthenaMemcardJob *athena_memcard_job_remove(int port, const char *path, bool recursive);
AthenaMemcardJob *athena_memcard_job_format(int port);

void athena_memcard_job_status(AthenaMemcardJob *job, AthenaMemcardJobStatus *out);
/* Stops before the next block. A format cannot be stopped. */
void athena_memcard_job_cancel(AthenaMemcardJob *job);
/* Blocks until the job settles or timeout_ms passes (< 0 = forever). True when settled. */
bool athena_memcard_job_wait(AthenaMemcardJob *job, int timeout_ms);
/* Read jobs: hands over the data once done (free() it). */
int athena_memcard_job_take_data(AthenaMemcardJob *job, void **data, size_t *size);
/* Cancels, joins the worker and frees everything. */
void athena_memcard_job_destroy(AthenaMemcardJob *job);

#endif /* ATHENA_MEMCARD_H */
