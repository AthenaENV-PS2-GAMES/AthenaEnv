#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <zip.h>
#include <zlib.h>

#include <athena/archive.h>
#include <athena/debug.h>

#define ARCHIVE_PATH_MAX 512
#define ARCHIVE_CHUNK 8192
#define TAR_BLOCK 512

struct AthenaArchive {
    AthenaArchiveType type;
    union {
        zip_t *zip;
        gzFile gz;
    } handle;
};

const char *athena_archive_strerror(int code)
{
    switch (code) {
    case ATHENA_ARCHIVE_OK: return "success";
    case ATHENA_ARCHIVE_ERR_ARGUMENT: return "invalid argument";
    case ATHENA_ARCHIVE_ERR_IO: return "I/O error";
    case ATHENA_ARCHIVE_ERR_FORMAT: return "unsupported or corrupted archive";
    case ATHENA_ARCHIVE_ERR_MEMORY: return "out of memory";
    case ATHENA_ARCHIVE_ERR_TYPE: return "operation not supported by this archive type";
    case ATHENA_ARCHIVE_ERR_UNSAFE: return "archive entry escapes the destination directory";
    default: return "unknown error";
    }
}

/* ------------------------------------------------------------------------ */
/* Paths                                                                    */
/* ------------------------------------------------------------------------ */

static int path_has_device(const char *path)
{
    return strchr(path, ':') != NULL;
}

/* libzip needs a path it can reopen, so relative paths are anchored at cwd. */
static int path_resolve(const char *path, char *out, size_t size)
{
    char cwd[ARCHIVE_PATH_MAX];
    int written;

    if (path_has_device(path) || path[0] == '/') {
        written = snprintf(out, size, "%s", path);
    } else {
        if (!getcwd(cwd, sizeof(cwd)))
            return ATHENA_ARCHIVE_ERR_IO;
        while (path[0] == '.' && path[1] == '/')
            path += 2;
        written = snprintf(out, size, "%s%s%s", cwd,
            cwd[0] && cwd[strlen(cwd) - 1] != '/' ? "/" : "", path);
    }
    return written < 0 || (size_t)written >= size ? ATHENA_ARCHIVE_ERR_ARGUMENT : ATHENA_ARCHIVE_OK;
}

static int path_join(const char *dir, const char *name, char *out, size_t size)
{
    char cwd[ARCHIVE_PATH_MAX];
    size_t len;
    int written;

    if (!dir || !dir[0]) {
        if (!getcwd(cwd, sizeof(cwd)))
            return ATHENA_ARCHIVE_ERR_IO;
        dir = cwd;
    }
    len = strlen(dir);
    written = snprintf(out, size, "%s%s%s", dir,
        len && dir[len - 1] != '/' && dir[len - 1] != ':' ? "/" : "", name);
    return written < 0 || (size_t)written >= size ? ATHENA_ARCHIVE_ERR_ARGUMENT : ATHENA_ARCHIVE_OK;
}

/* Rejects names that would leave the destination directory. */
static int entry_name_is_safe(const char *name)
{
    const char *segment = name;

    if (!name[0] || name[0] == '/' || name[0] == '\\' || strchr(name, ':'))
        return 0;
    while (*segment) {
        const char *end = segment;
        while (*end && *end != '/' && *end != '\\')
            end++;
        if (end - segment == 2 && segment[0] == '.' && segment[1] == '.')
            return 0;
        segment = *end ? end + 1 : end;
    }
    return 1;
}

/* mkdir -p. Existing directories are not an error. */
static void make_dirs(char *path)
{
    char *p = strchr(path, ':');
    size_t len = strlen(path);

    while (len > 1 && path[len - 1] == '/')
        path[--len] = '\0';

    p = p ? p + 1 : path;
    while (*p == '/')
        p++;
    for (; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        mkdir(path, 0755);
        *p = '/';
    }
    mkdir(path, 0755);
}

static void make_parent_dirs(char *path)
{
    char *slash = strrchr(path, '/');
    if (!slash || slash == path || slash[-1] == ':')
        return;
    *slash = '\0';
    make_dirs(path);
    *slash = '/';
}

static FILE *create_file(char *path)
{
    make_parent_dirs(path);
    return fopen(path, "wb");
}

/* ------------------------------------------------------------------------ */
/* Archive handles                                                          */
/* ------------------------------------------------------------------------ */

int athena_archive_open(const char *path, AthenaArchive **out_archive)
{
    char resolved[ARCHIVE_PATH_MAX];
    unsigned char magic[4];
    AthenaArchive *archive;
    FILE *fp;
    size_t read;
    int err = 0;
    int ret;

    if (!path || !path[0] || !out_archive)
        return ATHENA_ARCHIVE_ERR_ARGUMENT;
    *out_archive = NULL;

    ret = path_resolve(path, resolved, sizeof(resolved));
    if (ret < 0)
        return ret;

    fp = fopen(resolved, "rb");
    if (!fp)
        return ATHENA_ARCHIVE_ERR_IO;
    read = fread(magic, 1, sizeof(magic), fp);
    fclose(fp);
    if (read < 2)
        return ATHENA_ARCHIVE_ERR_FORMAT;

    archive = calloc(1, sizeof(*archive));
    if (!archive)
        return ATHENA_ARCHIVE_ERR_MEMORY;

    if (read == 4 && magic[0] == 'P' && magic[1] == 'K' &&
        ((magic[2] == 3 && magic[3] == 4) || (magic[2] == 5 && magic[3] == 6))) {
        archive->type = ATHENA_ARCHIVE_ZIP;
        archive->handle.zip = zip_open(resolved, ZIP_RDONLY, &err);
        if (!archive->handle.zip) {
            dbgprintf("[Archive] zip_open(%s) failed: %d\n", resolved, err);
            free(archive);
            return err == ZIP_ER_NOZIP || err == ZIP_ER_INCONS ?
                ATHENA_ARCHIVE_ERR_FORMAT : ATHENA_ARCHIVE_ERR_IO;
        }
    } else if (magic[0] == 0x1f && magic[1] == 0x8b) {
        archive->type = ATHENA_ARCHIVE_GZ;
        archive->handle.gz = gzopen(resolved, "rb");
        if (!archive->handle.gz) {
            free(archive);
            return ATHENA_ARCHIVE_ERR_IO;
        }
    } else {
        free(archive);
        return ATHENA_ARCHIVE_ERR_FORMAT;
    }

    *out_archive = archive;
    return ATHENA_ARCHIVE_OK;
}

AthenaArchiveType athena_archive_type(const AthenaArchive *archive)
{
    return archive->type;
}

int athena_archive_close(AthenaArchive *archive)
{
    int ret = ATHENA_ARCHIVE_OK;

    if (!archive)
        return ATHENA_ARCHIVE_OK;
    if (archive->type == ATHENA_ARCHIVE_ZIP && archive->handle.zip) {
        /* Read-only: nothing to write back. */
        zip_discard(archive->handle.zip);
    } else if (archive->type == ATHENA_ARCHIVE_GZ && archive->handle.gz) {
        if (gzclose(archive->handle.gz) != Z_OK)
            ret = ATHENA_ARCHIVE_ERR_IO;
    }
    free(archive);
    return ret;
}

int athena_archive_list(AthenaArchive *archive, AthenaArchiveListing **out_listing)
{
    AthenaArchiveListing *listing;
    zip_int64_t count;
    struct zip_stat sb;

    if (!archive || !out_listing)
        return ATHENA_ARCHIVE_ERR_ARGUMENT;
    if (archive->type != ATHENA_ARCHIVE_ZIP)
        return ATHENA_ARCHIVE_ERR_TYPE;
    *out_listing = NULL;

    count = zip_get_num_entries(archive->handle.zip, 0);
    if (count < 0)
        return ATHENA_ARCHIVE_ERR_FORMAT;

    listing = calloc(1, sizeof(*listing));
    if (!listing)
        return ATHENA_ARCHIVE_ERR_MEMORY;
    if (count > 0) {
        listing->entries = calloc((size_t)count, sizeof(*listing->entries));
        if (!listing->entries) {
            free(listing);
            return ATHENA_ARCHIVE_ERR_MEMORY;
        }
    }

    for (zip_int64_t i = 0; i < count; i++) {
        AthenaArchiveEntry *entry;

        zip_stat_init(&sb);
        if (zip_stat_index(archive->handle.zip, (zip_uint64_t)i, 0, &sb) != 0 || !sb.name)
            continue;
        entry = &listing->entries[listing->count++];
        strncpy(entry->name, sb.name, sizeof(entry->name) - 1);
        entry->size = (sb.valid & ZIP_STAT_SIZE) ? sb.size : 0;
        entry->mtime = (sb.valid & ZIP_STAT_MTIME) ? (uint32_t)sb.mtime : 0;
    }

    *out_listing = listing;
    return ATHENA_ARCHIVE_OK;
}

void athena_archive_listing_free(AthenaArchiveListing *listing)
{
    if (!listing)
        return;
    free(listing->entries);
    free(listing);
}

static int zip_extract_entry(zip_t *zip, zip_uint64_t index, const struct zip_stat *sb,
    const char *dest_dir, unsigned char *buf)
{
    char out_path[ARCHIVE_PATH_MAX];
    size_t name_len = strlen(sb->name);
    zip_file_t *zf;
    FILE *fp;
    zip_int64_t len;
    int ret;

    ret = path_join(dest_dir, sb->name, out_path, sizeof(out_path));
    if (ret < 0)
        return ret;

    if (name_len > 0 && sb->name[name_len - 1] == '/') {
        make_dirs(out_path);
        return ATHENA_ARCHIVE_OK;
    }

    zf = zip_fopen_index(zip, index, 0);
    if (!zf)
        return ATHENA_ARCHIVE_ERR_FORMAT;
    fp = create_file(out_path);
    if (!fp) {
        dbgprintf("[Archive] cannot create %s\n", out_path);
        zip_fclose(zf);
        return ATHENA_ARCHIVE_ERR_IO;
    }

    ret = ATHENA_ARCHIVE_OK;
    while ((len = zip_fread(zf, buf, ARCHIVE_CHUNK)) > 0) {
        if (fwrite(buf, 1, (size_t)len, fp) != (size_t)len) {
            ret = ATHENA_ARCHIVE_ERR_IO;
            break;
        }
    }
    if (len < 0)
        ret = ATHENA_ARCHIVE_ERR_FORMAT;
    if (fclose(fp) != 0 && ret == ATHENA_ARCHIVE_OK)
        ret = ATHENA_ARCHIVE_ERR_IO;
    zip_fclose(zf);
    return ret;
}

int athena_archive_extract_all(AthenaArchive *archive, const char *dest_dir)
{
    struct zip_stat sb;
    unsigned char *buf;
    zip_int64_t count;
    int ret = ATHENA_ARCHIVE_OK;

    if (!archive)
        return ATHENA_ARCHIVE_ERR_ARGUMENT;
    if (archive->type != ATHENA_ARCHIVE_ZIP)
        return ATHENA_ARCHIVE_ERR_TYPE;

    count = zip_get_num_entries(archive->handle.zip, 0);
    if (count < 0)
        return ATHENA_ARCHIVE_ERR_FORMAT;

    /* Validate every name first so a hostile archive writes nothing. */
    for (zip_int64_t i = 0; i < count; i++) {
        const char *name = zip_get_name(archive->handle.zip, (zip_uint64_t)i, 0);
        if (!name)
            return ATHENA_ARCHIVE_ERR_FORMAT;
        if (!entry_name_is_safe(name))
            return ATHENA_ARCHIVE_ERR_UNSAFE;
    }

    if (dest_dir && dest_dir[0]) {
        char dir[ARCHIVE_PATH_MAX];
        if (strlen(dest_dir) >= sizeof(dir))
            return ATHENA_ARCHIVE_ERR_ARGUMENT;
        strcpy(dir, dest_dir);
        make_dirs(dir);
    }

    buf = malloc(ARCHIVE_CHUNK);
    if (!buf)
        return ATHENA_ARCHIVE_ERR_MEMORY;

    for (zip_int64_t i = 0; i < count && ret == ATHENA_ARCHIVE_OK; i++) {
        zip_stat_init(&sb);
        if (zip_stat_index(archive->handle.zip, (zip_uint64_t)i, 0, &sb) != 0) {
            ret = ATHENA_ARCHIVE_ERR_FORMAT;
            break;
        }
        ret = zip_extract_entry(archive->handle.zip, (zip_uint64_t)i, &sb, dest_dir, buf);
    }

    free(buf);
    return ret;
}

int athena_archive_read_all(AthenaArchive *archive, void **out_data, size_t *out_size)
{
    unsigned char *out = NULL;
    size_t capacity = 0;
    size_t total = 0;
    int read;

    if (!archive || !out_data || !out_size)
        return ATHENA_ARCHIVE_ERR_ARGUMENT;
    if (archive->type != ATHENA_ARCHIVE_GZ)
        return ATHENA_ARCHIVE_ERR_TYPE;
    *out_data = NULL;
    *out_size = 0;

    /* Rewind so the stream can be read more than once. */
    if (gzrewind(archive->handle.gz) != 0)
        return ATHENA_ARCHIVE_ERR_IO;

    for (;;) {
        if (capacity - total < ARCHIVE_CHUNK) {
            size_t next = capacity ? capacity * 2 : ARCHIVE_CHUNK * 4;
            unsigned char *grown = realloc(out, next);
            if (!grown) {
                free(out);
                return ATHENA_ARCHIVE_ERR_MEMORY;
            }
            out = grown;
            capacity = next;
        }
        read = gzread(archive->handle.gz, out + total, ARCHIVE_CHUNK);
        if (read < 0) {
            free(out);
            return ATHENA_ARCHIVE_ERR_FORMAT;
        }
        if (read == 0)
            break;
        total += (size_t)read;
    }

    *out_data = out;
    *out_size = total;
    return ATHENA_ARCHIVE_OK;
}

/* ------------------------------------------------------------------------ */
/* Tar                                                                      */
/* ------------------------------------------------------------------------ */

static uint64_t tar_parse_oct(const char *p, size_t n)
{
    uint64_t value = 0;

    while (n > 0 && (*p < '0' || *p > '7')) {
        p++;
        n--;
    }
    while (n > 0 && *p >= '0' && *p <= '7') {
        value = value * 8 + (uint64_t)(*p - '0');
        p++;
        n--;
    }
    return value;
}

static int tar_is_zero_block(const char *block)
{
    for (int i = 0; i < TAR_BLOCK; i++)
        if (block[i])
            return 0;
    return 1;
}

static int tar_checksum_ok(const char *block)
{
    unsigned int sum = 0;
    for (int i = 0; i < TAR_BLOCK; i++)
        sum += (i >= 148 && i < 156) ? 0x20 : (unsigned char)block[i];
    return sum == tar_parse_oct(block + 148, 8);
}

/* Returns bytes read: TAR_BLOCK, 0 on clean EOF or -1 on error/short read. */
static int tar_read_block(gzFile gz, char *block)
{
    int read = gzread(gz, block, TAR_BLOCK);
    if (read == TAR_BLOCK || read == 0)
        return read;
    return -1;
}

static void tar_entry_name(const char *block, const char *long_name, char *out, size_t size)
{
    char name[101];
    char prefix[156];

    if (long_name[0]) {
        snprintf(out, size, "%s", long_name);
        return;
    }
    memcpy(name, block, 100);
    name[100] = '\0';
    if (!memcmp(block + 257, "ustar", 5) && block[345]) {
        memcpy(prefix, block + 345, 155);
        prefix[155] = '\0';
        snprintf(out, size, "%s/%s", prefix, name);
    } else {
        snprintf(out, size, "%s", name);
    }
}

int athena_archive_untar(const char *path, const char *dest_dir)
{
    char resolved[ARCHIVE_PATH_MAX];
    char out_path[ARCHIVE_PATH_MAX];
    char name[ARCHIVE_PATH_MAX];
    char long_name[ARCHIVE_PATH_MAX];
    char block[TAR_BLOCK];
    gzFile gz;
    int ret;

    if (!path || !path[0])
        return ATHENA_ARCHIVE_ERR_ARGUMENT;
    ret = path_resolve(path, resolved, sizeof(resolved));
    if (ret < 0)
        return ret;

    /* gzread passes uncompressed files through, so .tar and .tar.gz share a path. */
    gz = gzopen(resolved, "rb");
    if (!gz)
        return ATHENA_ARCHIVE_ERR_IO;

    if (dest_dir && dest_dir[0]) {
        if (strlen(dest_dir) >= sizeof(out_path)) {
            gzclose(gz);
            return ATHENA_ARCHIVE_ERR_ARGUMENT;
        }
        strcpy(out_path, dest_dir);
        make_dirs(out_path);
    }

    long_name[0] = '\0';
    ret = ATHENA_ARCHIVE_OK;
    for (;;) {
        uint64_t remaining;
        FILE *fp = NULL;
        char type;
        int read = tar_read_block(gz, block);

        if (read == 0)
            break;
        if (read < 0) {
            ret = ATHENA_ARCHIVE_ERR_FORMAT;
            break;
        }
        if (tar_is_zero_block(block))
            break;
        if (!tar_checksum_ok(block)) {
            ret = ATHENA_ARCHIVE_ERR_FORMAT;
            break;
        }

        remaining = tar_parse_oct(block + 124, 12);
        type = block[156];

        if (type == 'L') {
            /* GNU long name: the payload is the next entry's name. */
            size_t copied = 0;
            if (remaining >= sizeof(long_name)) {
                ret = ATHENA_ARCHIVE_ERR_UNSAFE;
                break;
            }
            while (remaining > 0) {
                size_t take = remaining < TAR_BLOCK ? (size_t)remaining : TAR_BLOCK;
                if (tar_read_block(gz, block) != TAR_BLOCK) {
                    ret = ATHENA_ARCHIVE_ERR_FORMAT;
                    break;
                }
                memcpy(long_name + copied, block, take);
                copied += take;
                remaining -= take;
            }
            long_name[copied] = '\0';
            if (ret < 0)
                break;
            continue;
        }

        if (type == '0' || type == '\0' || type == '5') {
            tar_entry_name(block, long_name, name, sizeof(name));
            long_name[0] = '\0';
            if (!entry_name_is_safe(name)) {
                ret = ATHENA_ARCHIVE_ERR_UNSAFE;
                break;
            }
            ret = path_join(dest_dir, name, out_path, sizeof(out_path));
            if (ret < 0)
                break;
            if (type == '5') {
                make_dirs(out_path);
                remaining = 0;
            } else {
                fp = create_file(out_path);
                if (!fp) {
                    dbgprintf("[Archive] cannot create %s\n", out_path);
                    ret = ATHENA_ARCHIVE_ERR_IO;
                    break;
                }
            }
        } else {
            /* Links, devices, pax headers: skip their payload. */
            long_name[0] = '\0';
        }

        while (remaining > 0) {
            size_t take = remaining < TAR_BLOCK ? (size_t)remaining : TAR_BLOCK;
            if (tar_read_block(gz, block) != TAR_BLOCK) {
                ret = ATHENA_ARCHIVE_ERR_FORMAT;
                break;
            }
            if (fp && fwrite(block, 1, take, fp) != take) {
                ret = ATHENA_ARCHIVE_ERR_IO;
                break;
            }
            remaining -= take;
        }
        if (fp && fclose(fp) != 0 && ret == ATHENA_ARCHIVE_OK)
            ret = ATHENA_ARCHIVE_ERR_IO;
        if (ret < 0)
            break;
    }

    gzclose(gz);
    return ret;
}
