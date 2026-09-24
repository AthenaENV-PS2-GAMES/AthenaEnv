#ifndef ATH_NATIVE_ARCHIVE_H
#define ATH_NATIVE_ARCHIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Read-only access to zip, tar (.tar / .tar.gz) and gzip files.
 *
 * Every archive is a list of entries: a gzip file is a single-entry archive
 * named after its header (or the file name without ".gz"). The same calls
 * list, read and extract all three formats.
 *
 * Functions are not re-entrant on the same archive: callers serialize the
 * use of one handle. Different handles may be used from different threads.
 */

typedef enum AthenaArchiveType {
    ATHENA_ARCHIVE_ZIP = 0,
    ATHENA_ARCHIVE_TAR,
    ATHENA_ARCHIVE_GZ,
} AthenaArchiveType;

typedef struct AthenaArchiveEntry {
    const char *name;         /* Directories end with '/'. Owned by the archive. */
    uint64_t size;            /* Uncompressed bytes (gzip: ISIZE, modulo 2^32). */
    uint64_t compressed_size; /* Stored bytes; equals size when not compressed. */
    uint32_t mtime;           /* Unix seconds, 0 when unknown. */
    bool dir;
    bool encrypted;
    bool unsupported;         /* Zip compression method other than store/deflate. */
} AthenaArchiveEntry;

typedef struct AthenaArchive AthenaArchive;

/* Result codes. Every failing call returns one of the negative values. */
#define ATHENA_ARCHIVE_OK               0
#define ATHENA_ARCHIVE_ERR_ARGUMENT    -1
#define ATHENA_ARCHIVE_ERR_IO          -2
#define ATHENA_ARCHIVE_ERR_FORMAT      -3
#define ATHENA_ARCHIVE_ERR_MEMORY      -4
#define ATHENA_ARCHIVE_ERR_UNSUPPORTED -5
#define ATHENA_ARCHIVE_ERR_UNSAFE      -6
#define ATHENA_ARCHIVE_ERR_TOO_LARGE   -7
#define ATHENA_ARCHIVE_ERR_NOT_FOUND   -8
#define ATHENA_ARCHIVE_ERR_EXISTS      -9
#define ATHENA_ARCHIVE_ERR_ENCRYPTED   -10
#define ATHENA_ARCHIVE_ERR_ABORTED     -11

/* Default cap for data decompressed into memory. */
#define ATHENA_ARCHIVE_DEFAULT_MAX_MEMORY (16u * 1024u * 1024u)

/*
 * Extraction hooks. Both run before an entry is written:
 *   filter   returns 1 to extract, 0 to skip, < 0 to abort;
 *   progress returns 0 to continue, < 0 to abort.
 * progress receives the entry index among the selected entries.
 */
typedef int (*AthenaArchiveFilter)(const AthenaArchiveEntry *entry, void *user);
typedef int (*AthenaArchiveProgress)(const AthenaArchiveEntry *entry,
    int index, int count, void *user);
/* After each block written: total bytes so far. Return < 0 to abort. */
typedef int (*AthenaArchiveWritten)(uint64_t total, void *user);

typedef struct AthenaArchiveExtractOptions {
    bool overwrite;           /* false: fail with ERR_EXISTS before writing. */
    uint64_t max_size;        /* Total uncompressed bytes, 0 = unlimited. */
    AthenaArchiveFilter filter;
    AthenaArchiveProgress progress;
    AthenaArchiveWritten written;
    void *user;
} AthenaArchiveExtractOptions;

/* Opens a zip, tar, tar.gz or gzip file, detected by content. */
int athena_archive_open(const char *path, AthenaArchive **out_archive);
AthenaArchiveType athena_archive_type(const AthenaArchive *archive);
/* True for .tar.gz and gzip. */
bool athena_archive_is_compressed(const AthenaArchive *archive);
int athena_archive_close(AthenaArchive *archive);

/*
 * Entries stay valid until the archive is closed. The first call on a
 * tar.gz decompresses the stream once to build the index.
 */
int athena_archive_entries(AthenaArchive *archive, const AthenaArchiveEntry **out_entries,
    int *out_count);

/*
 * Reads one entry into memory. name may be NULL for a gzip archive.
 * max_size 0 means ATHENA_ARCHIVE_DEFAULT_MAX_MEMORY. Free *out_data with free().
 */
int athena_archive_read(AthenaArchive *archive, const char *name, size_t max_size,
    void **out_data, size_t *out_size);

/*
 * Writes the entries below dest_dir (NULL or "" = current directory).
 * Every selected entry is validated first (paths, size, existing files,
 * encryption), so a rejected archive writes nothing. A file that fails
 * midway is removed. options may be NULL (overwrite, no limit).
 * Returns the number of entries written, or a negative code.
 */
int athena_archive_extract(AthenaArchive *archive, const char *dest_dir,
    const AthenaArchiveExtractOptions *options);

/* In-memory gzip. max_size 0 means ATHENA_ARCHIVE_DEFAULT_MAX_MEMORY. */
int athena_archive_gunzip(const void *data, size_t size, size_t max_size,
    void **out_data, size_t *out_size);
/* level: 0-9, or -1 for the zlib default. */
int athena_archive_gzip(const void *data, size_t size, int level,
    void **out_data, size_t *out_size);

/*
 * Human-readable detail of the last failure on this archive (the entry or
 * path involved), or "" when there is none.
 */
const char *athena_archive_error_detail(const AthenaArchive *archive);
const char *athena_archive_strerror(int code);
/* Stable identifier for scripts, e.g. "UNSAFE_PATH". */
const char *athena_archive_error_code(int code);

/* ------------------------------------------------------------------------ */
/* Background jobs                                                          */
/* ------------------------------------------------------------------------ */

/*
 * A job opens its own archive and runs one extraction or read on a worker
 * thread, so the caller's frame loop keeps running. The caller polls the
 * status; the job never calls back into the caller.
 */
typedef struct AthenaArchiveJob AthenaArchiveJob;

typedef enum AthenaArchiveJobState {
    ATHENA_ARCHIVE_JOB_RUNNING = 0,
    ATHENA_ARCHIVE_JOB_DONE,
    ATHENA_ARCHIVE_JOB_FAILED,
    ATHENA_ARCHIVE_JOB_CANCELLED,
} AthenaArchiveJobState;

typedef struct AthenaArchiveJobStatus {
    AthenaArchiveJobState state;
    int result;               /* extract: entries written; failure: negative code */
    int entries_done;
    int entries_total;        /* 0 until the archive has been indexed */
    uint64_t bytes_done;
    uint64_t bytes_total;     /* declared size of the selected entries */
    char entry[256];          /* entry being processed, "" when none */
    char detail[512];         /* failure detail, as athena_archive_error_detail() */
} AthenaArchiveJobStatus;

typedef struct AthenaArchiveJobOptions {
    bool overwrite;
    uint64_t max_size;        /* extract: total bytes, read: memory cap (0 = default) */
    /* extract: entries whose name equals an item, or starts with it when it ends with '/'. */
    const char *const *include;
    int include_count;
} AthenaArchiveJobOptions;

/* The strings are copied. Returns NULL when out of memory or the worker cannot start. */
AthenaArchiveJob *athena_archive_job_extract(const char *path, const char *dest_dir,
    const AthenaArchiveJobOptions *options);
AthenaArchiveJob *athena_archive_job_read(const char *path, const char *name,
    const AthenaArchiveJobOptions *options);

void athena_archive_job_status(AthenaArchiveJob *job, AthenaArchiveJobStatus *out);
/* Asks the worker to stop at the next block; files already written stay. */
void athena_archive_job_cancel(AthenaArchiveJob *job);
/* Blocks until the job finishes or timeout_ms passes (< 0 = forever). True when finished. */
bool athena_archive_job_wait(AthenaArchiveJob *job, int timeout_ms);
/* Read jobs: hands over the data once the job is done (free() it). */
int athena_archive_job_take_data(AthenaArchiveJob *job, void **out_data, size_t *out_size);
/* Cancels, joins the worker and frees everything. */
void athena_archive_job_destroy(AthenaArchiveJob *job);

#endif /* ATH_NATIVE_ARCHIVE_H */
