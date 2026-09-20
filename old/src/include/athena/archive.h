#ifndef ATHENA_ARCHIVE_H
#define ATHENA_ARCHIVE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum AthenaArchiveType {
    ATHENA_ARCHIVE_ZIP = 0,
    ATHENA_ARCHIVE_GZ,
} AthenaArchiveType;

typedef struct AthenaArchiveEntry {
    char name[512];
    uint32_t size;
    uint32_t mtime;
} AthenaArchiveEntry;

typedef struct AthenaArchiveEntryListing {
    AthenaArchiveEntry *entries;
    int count;
} AthenaArchiveEntryListing;

typedef struct AthenaArchive {
    void *handle;
    AthenaArchiveType type;
} AthenaArchive;

AthenaArchive *athena_archive_open(const char *path);
AthenaArchiveEntryListing *athena_archive_list(AthenaArchive *archive);
void athena_archive_entry_listing_free(AthenaArchiveEntryListing *listing);
int athena_archive_extract_all(AthenaArchive *archive, void **out_data, size_t *out_size);
int athena_archive_close(AthenaArchive *archive);
void athena_archive_destroy(AthenaArchive *archive);
int athena_archive_untar(const char *path);

#endif /* ATHENA_ARCHIVE_H */
