#ifndef ATH_NATIVE_ARCHIVE_H
#define ATH_NATIVE_ARCHIVE_H

#include <stddef.h>
#include <stdint.h>

typedef enum AthenaArchiveType {
    ATHENA_ARCHIVE_ZIP = 0,
    ATHENA_ARCHIVE_GZ,
} AthenaArchiveType;

typedef struct AthenaArchiveEntry {
    char name[512];
    uint64_t size;
    uint32_t mtime;
} AthenaArchiveEntry;

typedef struct AthenaArchiveListing {
    AthenaArchiveEntry *entries;
    int count;
} AthenaArchiveListing;

typedef struct AthenaArchive AthenaArchive;

/* Result codes. Every failing call returns one of the negative values. */
#define ATHENA_ARCHIVE_OK             0
#define ATHENA_ARCHIVE_ERR_ARGUMENT  -1
#define ATHENA_ARCHIVE_ERR_IO        -2
#define ATHENA_ARCHIVE_ERR_FORMAT    -3
#define ATHENA_ARCHIVE_ERR_MEMORY    -4
#define ATHENA_ARCHIVE_ERR_TYPE      -5
#define ATHENA_ARCHIVE_ERR_UNSAFE    -6

/* Opens a zip or gzip file, detected by its magic number. */
int athena_archive_open(const char *path, AthenaArchive **out_archive);
AthenaArchiveType athena_archive_type(const AthenaArchive *archive);

/* Zip only. Free the result with athena_archive_listing_free(). */
int athena_archive_list(AthenaArchive *archive, AthenaArchiveListing **out_listing);
void athena_archive_listing_free(AthenaArchiveListing *listing);

/*
 * Zip only: writes every entry below dest_dir (NULL or "" = current
 * directory). Entries with absolute paths, devices or ".." are rejected.
 */
int athena_archive_extract_all(AthenaArchive *archive, const char *dest_dir);

/* Gzip only: decompresses the whole stream. Free *out_data with free(). */
int athena_archive_read_all(AthenaArchive *archive, void **out_data, size_t *out_size);

/* Releases the handle and the archive. Safe with NULL. */
int athena_archive_close(AthenaArchive *archive);

/*
 * Extracts a .tar or .tar.gz below dest_dir (NULL or "" = current
 * directory). The gzip layer is streamed; no intermediate .tar is written.
 */
int athena_archive_untar(const char *path, const char *dest_dir);

const char *athena_archive_strerror(int code);

#endif /* ATH_NATIVE_ARCHIVE_H */
