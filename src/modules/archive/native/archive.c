#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <unzip.h>
#include <zlib.h>

#include <athena/archive.h>
#include <athena/debug.h>

#define ARCHIVE_PATH_MAX 512
/* Large blocks: each fread/fwrite on host:, mc0: or mass: is an IOP RPC. */
#define ARCHIVE_CHUNK (64 * 1024)
#define TAR_BLOCK 512
#define TAR_PAX_MAX (64 * 1024)

/* Where an entry's data starts: a zip directory position or a tar offset. */
typedef struct ArchiveLocation {
    uint64_t offset;            /* zip: directory position; tar: data offset in the stream */
    uint64_t index;             /* zip: entry number */
} ArchiveLocation;

struct AthenaArchive {
    AthenaArchiveType type;
    bool compressed;
    unzFile zip;
    gzFile gz;                  /* tar (plain or compressed) and gzip */
    AthenaArchiveEntry *entries;
    ArchiveLocation *locations;
    int count;
    int capacity;
    bool indexed;
    char detail[ARCHIVE_PATH_MAX];
};

/* ------------------------------------------------------------------------ */
/* Errors                                                                   */
/* ------------------------------------------------------------------------ */

const char *athena_archive_strerror(int code)
{
    switch (code) {
    case ATHENA_ARCHIVE_OK: return "success";
    case ATHENA_ARCHIVE_ERR_ARGUMENT: return "invalid argument";
    case ATHENA_ARCHIVE_ERR_IO: return "I/O error";
    case ATHENA_ARCHIVE_ERR_FORMAT: return "unsupported or corrupted archive";
    case ATHENA_ARCHIVE_ERR_MEMORY: return "out of memory";
    case ATHENA_ARCHIVE_ERR_UNSUPPORTED: return "operation not supported for this entry or archive";
    case ATHENA_ARCHIVE_ERR_UNSAFE: return "entry path escapes the destination directory";
    case ATHENA_ARCHIVE_ERR_TOO_LARGE: return "data exceeds the size limit";
    case ATHENA_ARCHIVE_ERR_NOT_FOUND: return "entry not found";
    case ATHENA_ARCHIVE_ERR_EXISTS: return "destination file already exists";
    case ATHENA_ARCHIVE_ERR_ENCRYPTED: return "encrypted entries are not supported";
    case ATHENA_ARCHIVE_ERR_ABORTED: return "extraction aborted";
    default: return "unknown error";
    }
}

const char *athena_archive_error_code(int code)
{
    switch (code) {
    case ATHENA_ARCHIVE_ERR_ARGUMENT: return "INVALID_ARGUMENT";
    case ATHENA_ARCHIVE_ERR_IO: return "IO";
    case ATHENA_ARCHIVE_ERR_FORMAT: return "BAD_FORMAT";
    case ATHENA_ARCHIVE_ERR_MEMORY: return "NO_MEMORY";
    case ATHENA_ARCHIVE_ERR_UNSUPPORTED: return "UNSUPPORTED";
    case ATHENA_ARCHIVE_ERR_UNSAFE: return "UNSAFE_PATH";
    case ATHENA_ARCHIVE_ERR_TOO_LARGE: return "TOO_LARGE";
    case ATHENA_ARCHIVE_ERR_NOT_FOUND: return "NOT_FOUND";
    case ATHENA_ARCHIVE_ERR_EXISTS: return "EXISTS";
    case ATHENA_ARCHIVE_ERR_ENCRYPTED: return "ENCRYPTED";
    case ATHENA_ARCHIVE_ERR_ABORTED: return "ABORTED";
    default: return "UNKNOWN";
    }
}

const char *athena_archive_error_detail(const AthenaArchive *archive)
{
    return archive ? archive->detail : "";
}

static int archive_fail(AthenaArchive *archive, int code, const char *fmt, ...)
{
    va_list args;

    if (archive) {
        va_start(args, fmt);
        vsnprintf(archive->detail, sizeof(archive->detail), fmt, args);
        va_end(args);
    }
    return code;
}

/* ------------------------------------------------------------------------ */
/* Paths                                                                    */
/* ------------------------------------------------------------------------ */

/* Absolute paths keep error messages and reopened handles unambiguous. */
static int path_resolve(const char *path, char *out, size_t size)
{
    char cwd[ARCHIVE_PATH_MAX];
    int written;

    if (strchr(path, ':') || path[0] == '/') {
        written = snprintf(out, size, "%s", path);
    } else {
        size_t len;
        if (!getcwd(cwd, sizeof(cwd)))
            return ATHENA_ARCHIVE_ERR_IO;
        while (path[0] == '.' && path[1] == '/')
            path += 2;
        len = strlen(cwd);
        written = snprintf(out, size, "%s%s%s", cwd,
            len && cwd[len - 1] != '/' ? "/" : "", path);
    }
    return written < 0 || (size_t)written >= size ? ATHENA_ARCHIVE_ERR_ARGUMENT : ATHENA_ARCHIVE_OK;
}

static int path_join(const char *dir, const char *name, char *out, size_t size)
{
    size_t len = strlen(dir);
    int written = snprintf(out, size, "%s%s%s", dir,
        len && dir[len - 1] != '/' && dir[len - 1] != ':' ? "/" : "", name);
    return written < 0 || (size_t)written >= size ? ATHENA_ARCHIVE_ERR_ARGUMENT : ATHENA_ARCHIVE_OK;
}

static const char *path_basename(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\' || *p == ':')
            base = p + 1;
    return base;
}

/* Rejects names that would leave the destination directory. */
static bool entry_name_is_safe(const char *name)
{
    const char *segment = name;

    if (!name[0] || name[0] == '/' || name[0] == '\\' || strchr(name, ':'))
        return false;
    while (*segment) {
        const char *end = segment;
        while (*end && *end != '/' && *end != '\\')
            end++;
        if (end - segment == 2 && segment[0] == '.' && segment[1] == '.')
            return false;
        segment = *end ? end + 1 : end;
    }
    return true;
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

static bool file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return false;
    fclose(fp);
    return true;
}

/* ------------------------------------------------------------------------ */
/* Entry index                                                              */
/* ------------------------------------------------------------------------ */

static void archive_clear_entries(AthenaArchive *archive)
{
    for (int i = 0; i < archive->count; i++)
        free((char *)archive->entries[i].name);
    free(archive->entries);
    free(archive->locations);
    archive->entries = NULL;
    archive->locations = NULL;
    archive->count = 0;
    archive->capacity = 0;
    archive->indexed = false;
}

/* Takes ownership of name. */
static int archive_add_entry(AthenaArchive *archive, char *name, const AthenaArchiveEntry *info,
    ArchiveLocation location)
{
    if (archive->count == archive->capacity) {
        int next = archive->capacity ? archive->capacity * 2 : 16;
        AthenaArchiveEntry *entries = realloc(archive->entries, (size_t)next * sizeof(*entries));
        ArchiveLocation *locations;
        if (!entries) {
            free(name);
            return ATHENA_ARCHIVE_ERR_MEMORY;
        }
        archive->entries = entries;
        locations = realloc(archive->locations, (size_t)next * sizeof(*locations));
        if (!locations) {
            free(name);
            return ATHENA_ARCHIVE_ERR_MEMORY;
        }
        archive->locations = locations;
        archive->capacity = next;
    }
    archive->entries[archive->count] = *info;
    archive->entries[archive->count].name = name;
    archive->locations[archive->count] = location;
    archive->count++;
    return ATHENA_ARCHIVE_OK;
}

/* DOS date fields as Unix seconds (UTC); 0 for an invalid date. */
static uint32_t zip_unix_time(const tm_unz *t)
{
    /* Days from civil, Howard Hinnant's algorithm. */
    int y = (int)t->tm_year, m = (int)t->tm_mon + 1, d = (int)t->tm_mday;
    int era, yoe, doy, doe;
    long days;

    if (y < 1980 || m < 1 || m > 12 || d < 1 || d > 31)
        return 0;
    y -= m <= 2;
    era = y / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    days = (long)era * 146097 + doe - 719468;
    return (uint32_t)(days * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec);
}

static int zip_build_index(AthenaArchive *archive)
{
    int ret = unzGoToFirstFile(archive->zip);

    for (uint64_t i = 0; ret == UNZ_OK; i++, ret = unzGoToNextFile(archive->zip)) {
        unz_file_info64 file;
        unz64_file_pos pos;
        AthenaArchiveEntry info = { 0 };
        char *name;
        size_t len;

        if (archive->count >= INT_MAX / 2)
            return archive_fail(archive, ATHENA_ARCHIVE_ERR_TOO_LARGE, "too many entries");
        if (unzGetCurrentFileInfo64(archive->zip, &file, NULL, 0, NULL, 0, NULL, 0) != UNZ_OK ||
            unzGetFilePos64(archive->zip, &pos) != UNZ_OK)
            return archive_fail(archive, ATHENA_ARCHIVE_ERR_FORMAT, "cannot read zip entry %lu", (unsigned long)i);
        name = malloc(file.size_filename + 1);
        if (!name)
            return ATHENA_ARCHIVE_ERR_MEMORY;
        if (unzGetCurrentFileInfo64(archive->zip, NULL, name, file.size_filename + 1, NULL, 0, NULL, 0) != UNZ_OK) {
            free(name);
            return archive_fail(archive, ATHENA_ARCHIVE_ERR_FORMAT, "cannot read zip entry %lu", (unsigned long)i);
        }
        name[file.size_filename] = '\0';
        len = strlen(name);
        info.dir = len > 0 && name[len - 1] == '/';
        info.size = file.uncompressed_size;
        info.compressed_size = file.compressed_size;
        info.mtime = zip_unix_time(&file.tmu_date);
        info.encrypted = (file.flag & 0x1) != 0;
        /* Stored and deflate are the methods zlib can read. */
        info.unsupported = !info.dir && file.compression_method != 0 && file.compression_method != Z_DEFLATED;
        ret = archive_add_entry(archive, name, &info,
            (ArchiveLocation){ pos.pos_in_zip_directory, pos.num_of_file });
        if (ret < 0)
            return ret;
    }
    if (ret != UNZ_END_OF_LIST_OF_FILE)
        return archive_fail(archive, ATHENA_ARCHIVE_ERR_FORMAT, "cannot read the zip directory");
    return ATHENA_ARCHIVE_OK;
}

/* ---- tar ---------------------------------------------------------------- */

/* Octal, or GNU base-256 when the high bit is set. UINT64_MAX on overflow. */
static uint64_t tar_parse_number(const char *p, size_t n)
{
    uint64_t value = 0;

    if ((unsigned char)p[0] & 0x80) {
        value = (unsigned char)p[0] & 0x7f;
        for (size_t i = 1; i < n; i++) {
            if (value >> 56)
                return UINT64_MAX;
            value = (value << 8) | (unsigned char)p[i];
        }
        return value;
    }
    while (n > 0 && (*p < '0' || *p > '7')) {
        p++;
        n--;
    }
    while (n > 0 && *p >= '0' && *p <= '7') {
        if (value >> 60)
            return UINT64_MAX;
        value = value * 8 + (uint64_t)(*p - '0');
        p++;
        n--;
    }
    return value;
}

static bool tar_is_zero_block(const char *block)
{
    for (int i = 0; i < TAR_BLOCK; i++)
        if (block[i])
            return false;
    return true;
}

/* Accepts the POSIX (unsigned) and the historic signed checksum. */
static bool tar_header_is_valid(const char *block)
{
    unsigned int unsigned_sum = 0;
    int signed_sum = 0;
    uint64_t stored;

    if (tar_is_zero_block(block))
        return false;
    for (int i = 0; i < TAR_BLOCK; i++) {
        bool in_field = i >= 148 && i < 156;
        unsigned_sum += in_field ? 0x20 : (unsigned char)block[i];
        signed_sum += in_field ? 0x20 : (signed char)block[i];
    }
    stored = tar_parse_number(block + 148, 8);
    return stored == unsigned_sum || stored == (uint64_t)(unsigned int)signed_sum;
}

static uint64_t tar_padded(uint64_t size)
{
    return (size + TAR_BLOCK - 1) & ~(uint64_t)(TAR_BLOCK - 1);
}

typedef struct TarStream {
    gzFile gz;
    uint64_t pos;
} TarStream;

/* TAR_BLOCK on success, 0 on clean EOF, -1 on a short or failed read. */
static int tar_read_block(TarStream *stream, char *block)
{
    int read = gzread(stream->gz, block, TAR_BLOCK);
    if (read == TAR_BLOCK) {
        stream->pos += TAR_BLOCK;
        return TAR_BLOCK;
    }
    return read == 0 ? 0 : -1;
}

static int tar_skip(TarStream *stream, uint64_t bytes)
{
    uint64_t target = stream->pos + bytes;
    if (target > (uint64_t)LONG_MAX)
        return ATHENA_ARCHIVE_ERR_TOO_LARGE;
    if (bytes && gzseek(stream->gz, (z_off_t)target, SEEK_SET) < 0)
        return ATHENA_ARCHIVE_ERR_FORMAT;
    stream->pos = target;
    return ATHENA_ARCHIVE_OK;
}

/* Reads a small payload (long name, pax header) into a NUL-terminated buffer. */
static int tar_read_payload(TarStream *stream, uint64_t size, char **out)
{
    uint64_t padded = tar_padded(size);
    char *data;

    if (size > TAR_PAX_MAX)
        return ATHENA_ARCHIVE_ERR_TOO_LARGE;
    data = malloc((size_t)padded + 1);
    if (!data)
        return ATHENA_ARCHIVE_ERR_MEMORY;
    for (uint64_t done = 0; done < padded; done += TAR_BLOCK) {
        if (tar_read_block(stream, data + done) != TAR_BLOCK) {
            free(data);
            return ATHENA_ARCHIVE_ERR_FORMAT;
        }
    }
    data[size] = '\0';
    *out = data;
    return ATHENA_ARCHIVE_OK;
}

/* Extracts "path" and "size" from pax records ("<len> key=value\n"). */
static void tar_parse_pax(const char *data, char **path, uint64_t *size, bool *has_size)
{
    const char *p = data;

    while (*p) {
        char *end;
        unsigned long len = strtoul(p, &end, 10);
        const char *key, *value, *record_end;

        if (end == p || *end != ' ' || len == 0)
            return;
        record_end = p + len;
        if (memchr(p, '\0', len))
            return;
        key = end + 1;
        value = memchr(key, '=', (size_t)(record_end - key));
        if (!value || record_end[-1] != '\n')
            return;
        value++;
        if ((size_t)(value - key) == 5 && !strncmp(key, "path=", 5)) {
            size_t value_len = (size_t)(record_end - 1 - value);
            char *copy = malloc(value_len + 1);
            if (copy) {
                memcpy(copy, value, value_len);
                copy[value_len] = '\0';
                free(*path);
                *path = copy;
            }
        } else if ((size_t)(value - key) == 5 && !strncmp(key, "size=", 5)) {
            *size = strtoull(value, NULL, 10);
            *has_size = true;
        }
        p = record_end;
    }
}

static char *tar_header_name(const char *block)
{
    char name[101];
    char prefix[156];
    char *out;

    memcpy(name, block, 100);
    name[100] = '\0';
    if (!memcmp(block + 257, "ustar", 5) && block[345]) {
        memcpy(prefix, block + 345, 155);
        prefix[155] = '\0';
        out = malloc(strlen(prefix) + strlen(name) + 2);
        if (out)
            sprintf(out, "%s/%s", prefix, name);
        return out;
    }
    return strdup(name);
}

static int tar_build_index(AthenaArchive *archive)
{
    TarStream stream = { archive->gz, 0 };
    char block[TAR_BLOCK];
    char *pending_name = NULL;
    uint64_t pending_size = 0;
    bool has_pending_size = false;
    int ret = ATHENA_ARCHIVE_OK;

    if (gzrewind(archive->gz) != 0)
        return archive_fail(archive, ATHENA_ARCHIVE_ERR_IO, "cannot rewind the tar stream");

    for (;;) {
        AthenaArchiveEntry info = { 0 };
        uint64_t size;
        char type;
        int read = tar_read_block(&stream, block);

        if (read == 0 || (read > 0 && tar_is_zero_block(block)))
            break;
        if (read < 0) {
            ret = archive_fail(archive, ATHENA_ARCHIVE_ERR_FORMAT, "truncated tar header");
            break;
        }
        if (!tar_header_is_valid(block)) {
            ret = archive_fail(archive, ATHENA_ARCHIVE_ERR_FORMAT,
                "bad tar header checksum at offset %lu", (unsigned long)(stream.pos - TAR_BLOCK));
            break;
        }

        size = tar_parse_number(block + 124, 12);
        if (size == UINT64_MAX) {
            ret = archive_fail(archive, ATHENA_ARCHIVE_ERR_FORMAT, "bad tar entry size");
            break;
        }
        type = block[156];

        if (type == 'L' || type == 'x') {
            char *payload = NULL;
            ret = tar_read_payload(&stream, size, &payload);
            if (ret < 0) {
                archive_fail(archive, ret, "bad tar extended header");
                break;
            }
            if (type == 'L') {
                free(pending_name);
                pending_name = payload;
            } else {
                tar_parse_pax(payload, &pending_name, &pending_size, &has_pending_size);
                free(payload);
            }
            continue;
        }

        if (has_pending_size)
            size = pending_size;

        if (type == '0' || type == '\0' || type == '7' || type == '5') {
            char *name = pending_name ? pending_name : tar_header_name(block);
            size_t len;

            pending_name = NULL;
            if (!name) {
                ret = ATHENA_ARCHIVE_ERR_MEMORY;
                break;
            }
            info.dir = type == '5';
            len = strlen(name);
            if (info.dir && (len == 0 || name[len - 1] != '/')) {
                char *with_slash = realloc(name, len + 2);
                if (!with_slash) {
                    free(name);
                    ret = ATHENA_ARCHIVE_ERR_MEMORY;
                    break;
                }
                name = with_slash;
                strcpy(name + len, "/");
            }
            info.size = info.dir ? 0 : size;
            info.compressed_size = info.size;
            info.mtime = (uint32_t)tar_parse_number(block + 136, 12);
            ret = archive_add_entry(archive, name, &info, (ArchiveLocation){ stream.pos, 0 });
            if (ret < 0)
                break;
        } else {
            /* Links, devices, FIFOs and global pax headers are not extracted. */
            free(pending_name);
            pending_name = NULL;
        }

        has_pending_size = false;
        ret = tar_skip(&stream, tar_padded(size));
        if (ret < 0) {
            archive_fail(archive, ret, "cannot skip tar entry data");
            break;
        }
    }

    free(pending_name);
    return ret;
}

static int archive_ensure_index(AthenaArchive *archive)
{
    int ret = ATHENA_ARCHIVE_OK;

    if (archive->indexed)
        return ATHENA_ARCHIVE_OK;
    if (archive->type == ATHENA_ARCHIVE_ZIP)
        ret = zip_build_index(archive);
    else if (archive->type == ATHENA_ARCHIVE_TAR)
        ret = tar_build_index(archive);
    if (ret < 0) {
        archive_clear_entries(archive);
        return ret;
    }
    archive->indexed = true;
    return ATHENA_ARCHIVE_OK;
}

/* ------------------------------------------------------------------------ */
/* Open / close                                                             */
/* ------------------------------------------------------------------------ */

/* Builds the single entry of a gzip file from its header and trailer. */
static int gz_single_entry(AthenaArchive *archive, const char *path, const unsigned char *head,
    size_t head_size, FILE *fp)
{
    AthenaArchiveEntry info = { 0 };
    unsigned char trailer[4];
    char *name = NULL;
    long file_size;

    if (head_size >= 8)
        info.mtime = head[4] | head[5] << 8 | head[6] << 16 | (uint32_t)head[7] << 24;

    /* FNAME (flag 0x08) after the optional FEXTRA field (flag 0x04). */
    if (head_size > 10 && (head[3] & 0x08)) {
        size_t pos = 10;
        if (head[3] & 0x04) {
            if (head_size >= 12)
                pos = 12 + (head[10] | head[11] << 8);
            else
                pos = head_size;
        }
        if (pos < head_size) {
            const char *start = (const char *)head + pos;
            const char *end = memchr(start, '\0', head_size - pos);
            if (end) {
                const char *base = path_basename(start);
                if (entry_name_is_safe(base) && strcmp(base, ".."))
                    name = strdup(base);
            }
        }
    }
    if (!name) {
        const char *base = path_basename(path);
        size_t len = strlen(base);
        if (len > 3 && !strcmp(base + len - 3, ".gz")) {
            name = malloc(len - 2);
            if (name) {
                memcpy(name, base, len - 3);
                name[len - 3] = '\0';
            }
        } else {
            name = malloc(len + 5);
            if (name)
                sprintf(name, "%s.out", base);
        }
    }
    if (!name)
        return ATHENA_ARCHIVE_ERR_MEMORY;

    if (fseek(fp, 0, SEEK_END) == 0 && (file_size = ftell(fp)) >= 0) {
        info.compressed_size = (uint64_t)file_size;
        if (file_size >= 18 && fseek(fp, -4, SEEK_END) == 0 && fread(trailer, 1, 4, fp) == 4)
            info.size = trailer[0] | trailer[1] << 8 | trailer[2] << 16 | (uint32_t)trailer[3] << 24;
    }
    return archive_add_entry(archive, name, &info, (ArchiveLocation){ 0, 0 });
}

static int archive_open_gz(AthenaArchive *archive, const char *resolved)
{
    archive->gz = gzopen(resolved, "rb");
    if (!archive->gz)
        return ATHENA_ARCHIVE_ERR_IO;
    gzbuffer(archive->gz, ARCHIVE_CHUNK);
    return ATHENA_ARCHIVE_OK;
}

int athena_archive_open(const char *path, AthenaArchive **out_archive)
{
    char resolved[ARCHIVE_PATH_MAX];
    unsigned char head[TAR_BLOCK];
    AthenaArchive *archive;
    FILE *fp;
    size_t read;
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
    read = fread(head, 1, sizeof(head), fp);

    archive = calloc(1, sizeof(*archive));
    if (!archive) {
        fclose(fp);
        return ATHENA_ARCHIVE_ERR_MEMORY;
    }

    if (read >= 4 && head[0] == 'P' && head[1] == 'K' &&
        ((head[2] == 3 && head[3] == 4) || (head[2] == 5 && head[3] == 6))) {
        fclose(fp);
        archive->type = ATHENA_ARCHIVE_ZIP;
        archive->zip = unzOpen64(resolved);
        if (!archive->zip) {
            /* The file opened above, so a failure here is a bad central directory. */
            dbgprintf("[Archive] unzOpen64(%s) failed\n", resolved);
            free(archive);
            return ATHENA_ARCHIVE_ERR_FORMAT;
        }
    } else if (read >= 2 && head[0] == 0x1f && head[1] == 0x8b) {
        char block[TAR_BLOCK];

        archive->compressed = true;
        ret = archive_open_gz(archive, resolved);
        if (ret < 0) {
            fclose(fp);
            free(archive);
            return ret;
        }
        /* A gzip whose payload starts with a tar header is a .tar.gz. */
        if (gzread(archive->gz, block, TAR_BLOCK) == TAR_BLOCK && tar_header_is_valid(block)) {
            archive->type = ATHENA_ARCHIVE_TAR;
            fclose(fp);
        } else {
            archive->type = ATHENA_ARCHIVE_GZ;
            ret = gz_single_entry(archive, resolved, head, read, fp);
            fclose(fp);
            if (ret < 0) {
                athena_archive_close(archive);
                return ret;
            }
            archive->indexed = true;
        }
    } else if (read == TAR_BLOCK && tar_header_is_valid((const char *)head)) {
        fclose(fp);
        archive->type = ATHENA_ARCHIVE_TAR;
        /* gzread passes plain files through, so both tar flavours share code. */
        ret = archive_open_gz(archive, resolved);
        if (ret < 0) {
            free(archive);
            return ret;
        }
    } else {
        fclose(fp);
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

bool athena_archive_is_compressed(const AthenaArchive *archive)
{
    return archive->compressed;
}

int athena_archive_close_detail(AthenaArchive *archive, char *detail, size_t size)
{
    int ret = ATHENA_ARCHIVE_OK;
    int rc;

    if (detail && size)
        detail[0] = '\0';
    if (!archive)
        return ATHENA_ARCHIVE_OK;
    if (archive->zip) {
        errno = 0;
        if ((rc = unzClose(archive->zip)) != UNZ_OK) {
            if (detail && size)
                snprintf(detail, size, "unzClose returned %d, errno %d", rc, errno);
            ret = ATHENA_ARCHIVE_ERR_IO;
        }
    }
    if (archive->gz) {
        errno = 0;
        if ((rc = gzclose(archive->gz)) != Z_OK) {
            if (detail && size)
                snprintf(detail, size, "gzclose returned %d, errno %d (%s)", rc, errno,
                    archive->type == ATHENA_ARCHIVE_TAR ? "tar" : "gz");
            ret = ATHENA_ARCHIVE_ERR_IO;
        }
    }
    if (ret < 0 && detail && size)
        dbgprintf("[Archive] close failed: %s\n", detail);
    archive_clear_entries(archive);
    free(archive);
    return ret;
}

int athena_archive_close(AthenaArchive *archive)
{
    return athena_archive_close_detail(archive, NULL, 0);
}

int athena_archive_entries(AthenaArchive *archive, const AthenaArchiveEntry **out_entries,
    int *out_count)
{
    int ret;

    if (!archive || !out_entries || !out_count)
        return ATHENA_ARCHIVE_ERR_ARGUMENT;
    archive->detail[0] = '\0';
    ret = archive_ensure_index(archive);
    if (ret < 0)
        return ret;
    *out_entries = archive->entries;
    *out_count = archive->count;
    return ATHENA_ARCHIVE_OK;
}

/* ------------------------------------------------------------------------ */
/* Entry streams                                                            */
/* ------------------------------------------------------------------------ */

/*
 * Sequential reader over one entry. read() returns bytes (0 at the end) or a
 * negative code; the stream verifies zip CRCs and truncated tar data.
 */
typedef struct EntryStream {
    AthenaArchive *archive;
    const AthenaArchiveEntry *entry;
    bool zip_open;              /* zip: the current file is open in archive->zip */
    uint64_t remaining;         /* tar: bytes left in the entry */
} EntryStream;

static int entry_stream_open(AthenaArchive *archive, int index, EntryStream *stream)
{
    const AthenaArchiveEntry *entry = &archive->entries[index];

    memset(stream, 0, sizeof(*stream));
    stream->archive = archive;
    stream->entry = entry;

    switch (archive->type) {
    case ATHENA_ARCHIVE_ZIP: {
        unz64_file_pos pos = {
            archive->locations[index].offset,
            archive->locations[index].index,
        };
        if (unzGoToFilePos64(archive->zip, &pos) != UNZ_OK || unzOpenCurrentFile(archive->zip) != UNZ_OK)
            return archive_fail(archive, ATHENA_ARCHIVE_ERR_FORMAT, "cannot open '%s'", entry->name);
        stream->zip_open = true;
        stream->remaining = entry->size;
        return ATHENA_ARCHIVE_OK;
    }
    case ATHENA_ARCHIVE_TAR:
        if (archive->locations[index].offset > (uint64_t)LONG_MAX)
            return archive_fail(archive, ATHENA_ARCHIVE_ERR_TOO_LARGE, "'%s' is beyond 2 GiB", entry->name);
        if (gzseek(archive->gz, (z_off_t)archive->locations[index].offset, SEEK_SET) < 0)
            return archive_fail(archive, ATHENA_ARCHIVE_ERR_FORMAT, "cannot seek to '%s'", entry->name);
        stream->remaining = entry->size;
        return ATHENA_ARCHIVE_OK;
    case ATHENA_ARCHIVE_GZ:
        if (gzrewind(archive->gz) != 0)
            return archive_fail(archive, ATHENA_ARCHIVE_ERR_IO, "cannot rewind '%s'", entry->name);
        return ATHENA_ARCHIVE_OK;
    }
    return ATHENA_ARCHIVE_ERR_ARGUMENT;
}

static long entry_stream_read(EntryStream *stream, void *buf, size_t size)
{
    AthenaArchive *archive = stream->archive;
    const char *name = stream->entry->name;

    switch (archive->type) {
    case ATHENA_ARCHIVE_ZIP: {
        int read;
        if (size > INT_MAX)
            size = INT_MAX;
        read = unzReadCurrentFile(archive->zip, buf, (unsigned)size);
        if (read < 0)
            return archive_fail(archive, ATHENA_ARCHIVE_ERR_FORMAT, "'%s' is corrupted (%d)", name, read);
        if (read == 0) {
            /* End of data: the CRC is checked when the file is closed. */
            int closed = unzCloseCurrentFile(archive->zip);
            stream->zip_open = false;
            if (stream->remaining != 0 || closed != UNZ_OK)
                return archive_fail(archive, ATHENA_ARCHIVE_ERR_FORMAT, "'%s' is corrupted (%s)", name,
                    closed == UNZ_CRCERROR ? "CRC mismatch" : "size mismatch");
            return 0;
        }
        if ((uint64_t)read > stream->remaining)
            return archive_fail(archive, ATHENA_ARCHIVE_ERR_FORMAT, "'%s' is larger than declared", name);
        stream->remaining -= (uint64_t)read;
        return read;
    }
    case ATHENA_ARCHIVE_TAR: {
        int read;
        if (stream->remaining == 0)
            return 0;
        if (size > stream->remaining)
            size = (size_t)stream->remaining;
        read = gzread(archive->gz, buf, (unsigned)size);
        if (read != (int)size)
            return archive_fail(archive, ATHENA_ARCHIVE_ERR_FORMAT, "'%s' is truncated", name);
        stream->remaining -= (uint64_t)read;
        return read;
    }
    case ATHENA_ARCHIVE_GZ: {
        int read = gzread(archive->gz, buf, (unsigned)size);
        if (read < 0)
            return archive_fail(archive, ATHENA_ARCHIVE_ERR_FORMAT, "'%s' is corrupted", name);
        return read;
    }
    }
    return ATHENA_ARCHIVE_ERR_ARGUMENT;
}

static void entry_stream_close(EntryStream *stream)
{
    if (stream->zip_open)
        unzCloseCurrentFile(stream->archive->zip);
    stream->zip_open = false;
}

static int archive_find(AthenaArchive *archive, const char *name)
{
    if (archive->type == ATHENA_ARCHIVE_GZ && (!name || !strcmp(name, archive->entries[0].name)))
        return 0;
    if (!name)
        return -1;
    /* Last match wins, as when a tar is extracted. */
    for (int i = archive->count - 1; i >= 0; i--)
        if (!strcmp(archive->entries[i].name, name))
            return i;
    return -1;
}

/* ------------------------------------------------------------------------ */
/* Read into memory                                                         */
/* ------------------------------------------------------------------------ */

int athena_archive_read(AthenaArchive *archive, const char *name, size_t max_size,
    void **out_data, size_t *out_size)
{
    const AthenaArchiveEntry *entry;
    EntryStream stream;
    unsigned char *out = NULL;
    size_t capacity, total = 0;
    int index, ret;

    if (!archive || !out_data || !out_size)
        return ATHENA_ARCHIVE_ERR_ARGUMENT;
    *out_data = NULL;
    *out_size = 0;
    archive->detail[0] = '\0';
    if (!max_size)
        max_size = ATHENA_ARCHIVE_DEFAULT_MAX_MEMORY;
    if (!name && archive->type != ATHENA_ARCHIVE_GZ)
        return archive_fail(archive, ATHENA_ARCHIVE_ERR_ARGUMENT, "an entry name is required");

    ret = archive_ensure_index(archive);
    if (ret < 0)
        return ret;
    index = archive_find(archive, name);
    if (index < 0)
        return archive_fail(archive, ATHENA_ARCHIVE_ERR_NOT_FOUND, "'%s'", name);
    entry = &archive->entries[index];
    if (entry->dir)
        return archive_fail(archive, ATHENA_ARCHIVE_ERR_UNSUPPORTED, "'%s' is a directory", entry->name);
    if (entry->encrypted)
        return archive_fail(archive, ATHENA_ARCHIVE_ERR_ENCRYPTED, "'%s'", entry->name);
    if (entry->unsupported)
        return archive_fail(archive, ATHENA_ARCHIVE_ERR_UNSUPPORTED,
            "'%s' uses a compression method other than store/deflate", entry->name);
    /*
     * gzip sizes come from the unverified trailer: a larger value rejects
     * early, a smaller (or wrapped) one is still caught while streaming.
     */
    if (entry->size > max_size)
        return archive_fail(archive, ATHENA_ARCHIVE_ERR_TOO_LARGE, "'%s' has %lu bytes (limit %lu)",
            entry->name, (unsigned long)entry->size, (unsigned long)max_size);

    /* Exact allocation when the size is known; +1 detects a lying gzip trailer. */
    capacity = entry->size && entry->size < max_size ? (size_t)entry->size + 1 : ARCHIVE_CHUNK;
    if (capacity > max_size + 1)
        capacity = max_size + 1;

    ret = entry_stream_open(archive, index, &stream);
    if (ret < 0)
        return ret;
    out = malloc(capacity);
    if (!out) {
        entry_stream_close(&stream);
        return ATHENA_ARCHIVE_ERR_MEMORY;
    }

    for (;;) {
        long read;
        if (total == capacity) {
            size_t next = capacity * 2 > max_size + 1 ? max_size + 1 : capacity * 2;
            unsigned char *grown;
            if (next <= capacity) {
                ret = archive_fail(archive, ATHENA_ARCHIVE_ERR_TOO_LARGE,
                    "'%s' exceeds %lu bytes", entry->name, (unsigned long)max_size);
                break;
            }
            grown = realloc(out, next);
            if (!grown) {
                ret = ATHENA_ARCHIVE_ERR_MEMORY;
                break;
            }
            out = grown;
            capacity = next;
        }
        read = entry_stream_read(&stream, out + total, capacity - total);
        if (read < 0) {
            ret = (int)read;
            break;
        }
        if (read == 0)
            break;
        total += (size_t)read;
    }
    entry_stream_close(&stream);

    if (ret == ATHENA_ARCHIVE_OK && total > max_size)
        ret = archive_fail(archive, ATHENA_ARCHIVE_ERR_TOO_LARGE,
            "'%s' exceeds %lu bytes", entry->name, (unsigned long)max_size);
    if (ret < 0) {
        free(out);
        return ret;
    }
    if (total + 1 < capacity) {
        unsigned char *shrunk = realloc(out, total ? total : 1);
        if (shrunk)
            out = shrunk;
    }
    *out_data = out;
    *out_size = total;
    return ATHENA_ARCHIVE_OK;
}

/* ------------------------------------------------------------------------ */
/* Extract to disk                                                          */
/* ------------------------------------------------------------------------ */

static int extract_entry(AthenaArchive *archive, int index, char *out_path, unsigned char *buf,
    uint64_t *written, const AthenaArchiveExtractOptions *options)
{
    uint64_t max_size = options->max_size;
    const AthenaArchiveEntry *entry = &archive->entries[index];
    EntryStream stream;
    FILE *fp;
    int ret;

    if (entry->dir) {
        make_dirs(out_path);
        return ATHENA_ARCHIVE_OK;
    }

    ret = entry_stream_open(archive, index, &stream);
    if (ret < 0)
        return ret;
    make_parent_dirs(out_path);
    fp = fopen(out_path, "wb");
    if (!fp) {
        entry_stream_close(&stream);
        return archive_fail(archive, ATHENA_ARCHIVE_ERR_IO, "cannot create '%s'", out_path);
    }

    for (;;) {
        long read = entry_stream_read(&stream, buf, ARCHIVE_CHUNK);
        if (read < 0) {
            ret = (int)read;
            break;
        }
        if (read == 0)
            break;
        *written += (uint64_t)read;
        if (max_size && *written > max_size) {
            ret = archive_fail(archive, ATHENA_ARCHIVE_ERR_TOO_LARGE,
                "'%s' goes past the %lu byte limit", entry->name, (unsigned long)max_size);
            break;
        }
        if (fwrite(buf, 1, (size_t)read, fp) != (size_t)read) {
            ret = archive_fail(archive, ATHENA_ARCHIVE_ERR_IO, "cannot write '%s'", out_path);
            break;
        }
        if (options->written && options->written(*written, options->user) < 0) {
            ret = archive_fail(archive, ATHENA_ARCHIVE_ERR_ABORTED, "cancelled while writing '%s'",
                entry->name);
            break;
        }
    }
    entry_stream_close(&stream);
    if (fclose(fp) != 0 && ret == ATHENA_ARCHIVE_OK)
        ret = archive_fail(archive, ATHENA_ARCHIVE_ERR_IO, "cannot write '%s'", out_path);
    if (ret < 0)
        remove(out_path);
    return ret;
}

int athena_archive_extract(AthenaArchive *archive, const char *dest_dir,
    const AthenaArchiveExtractOptions *options)
{
    static const AthenaArchiveExtractOptions defaults = { .overwrite = true };
    char base[ARCHIVE_PATH_MAX];
    char out_path[ARCHIVE_PATH_MAX];
    unsigned char *buf = NULL;
    bool *selected = NULL;
    uint64_t declared = 0, written = 0;
    int selected_count = 0, done = 0;
    int ret;

    if (!archive)
        return ATHENA_ARCHIVE_ERR_ARGUMENT;
    if (!options)
        options = &defaults;
    archive->detail[0] = '\0';

    if (dest_dir && dest_dir[0]) {
        ret = path_resolve(dest_dir, base, sizeof(base));
    } else {
        ret = getcwd(base, sizeof(base)) ? ATHENA_ARCHIVE_OK : ATHENA_ARCHIVE_ERR_IO;
    }
    if (ret < 0)
        return archive_fail(archive, ret, "bad destination directory");

    ret = archive_ensure_index(archive);
    if (ret < 0)
        return ret;

    selected = calloc((size_t)archive->count + 1, sizeof(*selected));
    if (!selected)
        return ATHENA_ARCHIVE_ERR_MEMORY;

    /* Plan: select, then validate everything before the first write. */
    for (int i = 0; i < archive->count; i++) {
        if (options->filter) {
            int keep = options->filter(&archive->entries[i], options->user);
            if (keep < 0) {
                ret = archive_fail(archive, ATHENA_ARCHIVE_ERR_ABORTED, "filter aborted");
                goto out;
            }
            if (!keep)
                continue;
        }
        selected[i] = true;
        selected_count++;
    }

    for (int i = 0; i < archive->count; i++) {
        const AthenaArchiveEntry *entry = &archive->entries[i];
        if (!selected[i])
            continue;
        if (!entry_name_is_safe(entry->name)) {
            ret = archive_fail(archive, ATHENA_ARCHIVE_ERR_UNSAFE, "'%s'", entry->name);
            goto out;
        }
        if (entry->encrypted) {
            ret = archive_fail(archive, ATHENA_ARCHIVE_ERR_ENCRYPTED, "'%s'", entry->name);
            goto out;
        }
        if (entry->unsupported) {
            ret = archive_fail(archive, ATHENA_ARCHIVE_ERR_UNSUPPORTED,
                "'%s' uses a compression method other than store/deflate", entry->name);
            goto out;
        }
        if (path_join(base, entry->name, out_path, sizeof(out_path)) < 0) {
            ret = archive_fail(archive, ATHENA_ARCHIVE_ERR_ARGUMENT, "path too long for '%s'", entry->name);
            goto out;
        }
        if (!options->overwrite && !entry->dir && file_exists(out_path)) {
            ret = archive_fail(archive, ATHENA_ARCHIVE_ERR_EXISTS, "'%s'", out_path);
            goto out;
        }
        declared += entry->size;
    }
    /* Declared sizes: exact for zip/tar, the gzip trailer otherwise (also enforced while writing). */
    if (options->max_size && declared > options->max_size) {
        ret = archive_fail(archive, ATHENA_ARCHIVE_ERR_TOO_LARGE, "%lu bytes (limit %lu)",
            (unsigned long)declared, (unsigned long)options->max_size);
        goto out;
    }

    buf = malloc(ARCHIVE_CHUNK);
    if (!buf) {
        ret = ATHENA_ARCHIVE_ERR_MEMORY;
        goto out;
    }
    strcpy(out_path, base);
    make_dirs(out_path);

    for (int i = 0; i < archive->count; i++) {
        if (!selected[i])
            continue;
        if (options->progress &&
            options->progress(&archive->entries[i], done, selected_count, options->user) < 0) {
            ret = archive_fail(archive, ATHENA_ARCHIVE_ERR_ABORTED, "cancelled before '%s'",
                archive->entries[i].name);
            goto out;
        }
        path_join(base, archive->entries[i].name, out_path, sizeof(out_path));
        ret = extract_entry(archive, i, out_path, buf, &written, options);
        if (ret < 0)
            goto out;
        done++;
    }
    ret = done;

out:
    free(buf);
    free(selected);
    return ret;
}

/* ------------------------------------------------------------------------ */
/* In-memory gzip                                                           */
/* ------------------------------------------------------------------------ */

int athena_archive_gunzip(const void *data, size_t size, size_t max_size,
    void **out_data, size_t *out_size)
{
    const unsigned char *in = data;
    unsigned char *out;
    size_t capacity, total = 0;
    z_stream zs;
    int ret = ATHENA_ARCHIVE_OK;
    int zret;

    if (!data || !out_data || !out_size || size > UINT_MAX)
        return ATHENA_ARCHIVE_ERR_ARGUMENT;
    *out_data = NULL;
    *out_size = 0;
    if (!max_size)
        max_size = ATHENA_ARCHIVE_DEFAULT_MAX_MEMORY;
    if (size < 18 || in[0] != 0x1f || in[1] != 0x8b)
        return ATHENA_ARCHIVE_ERR_FORMAT;

    /* ISIZE: rejects early when it is over the limit, else sizes one allocation. */
    capacity = in[size - 4] | in[size - 3] << 8 | in[size - 2] << 16 | (size_t)in[size - 1] << 24;
    if (capacity > max_size)
        return ATHENA_ARCHIVE_ERR_TOO_LARGE;
    capacity = capacity ? capacity + 1 : ARCHIVE_CHUNK;
    if (capacity > max_size + 1)
        capacity = max_size + 1;
    out = malloc(capacity);
    if (!out)
        return ATHENA_ARCHIVE_ERR_MEMORY;

    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, 15 + 16) != Z_OK) {
        free(out);
        return ATHENA_ARCHIVE_ERR_MEMORY;
    }
    zs.next_in = (Bytef *)in;
    zs.avail_in = (uInt)size;

    for (;;) {
        if (total == capacity) {
            size_t next = capacity * 2 > max_size + 1 ? max_size + 1 : capacity * 2;
            unsigned char *grown;
            if (next <= capacity) {
                ret = ATHENA_ARCHIVE_ERR_TOO_LARGE;
                break;
            }
            grown = realloc(out, next);
            if (!grown) {
                ret = ATHENA_ARCHIVE_ERR_MEMORY;
                break;
            }
            out = grown;
            capacity = next;
        }
        zs.next_out = out + total;
        zs.avail_out = (uInt)(capacity - total);
        zret = inflate(&zs, Z_NO_FLUSH);
        total = capacity - zs.avail_out;
        if (zret == Z_STREAM_END) {
            /* Concatenated members are part of the same file. */
            if (zs.avail_in >= 2 && zs.next_in[0] == 0x1f && zs.next_in[1] == 0x8b) {
                inflateReset(&zs);
                continue;
            }
            break;
        }
        if (zret == Z_MEM_ERROR) {
            ret = ATHENA_ARCHIVE_ERR_MEMORY;
            break;
        }
        if (zret != Z_OK && !(zret == Z_BUF_ERROR && zs.avail_out == 0)) {
            ret = ATHENA_ARCHIVE_ERR_FORMAT;
            break;
        }
    }
    inflateEnd(&zs);

    if (ret == ATHENA_ARCHIVE_OK && total > max_size)
        ret = ATHENA_ARCHIVE_ERR_TOO_LARGE;
    if (ret < 0) {
        free(out);
        return ret;
    }
    if (total + 1 < capacity) {
        unsigned char *shrunk = realloc(out, total ? total : 1);
        if (shrunk)
            out = shrunk;
    }
    *out_data = out;
    *out_size = total;
    return ATHENA_ARCHIVE_OK;
}

int athena_archive_gzip(const void *data, size_t size, int level,
    void **out_data, size_t *out_size)
{
    unsigned char *out;
    uLong bound;
    z_stream zs;
    int zret;

    if ((!data && size) || !out_data || !out_size || size > UINT_MAX || level < -1 || level > 9)
        return ATHENA_ARCHIVE_ERR_ARGUMENT;
    *out_data = NULL;
    *out_size = 0;

    memset(&zs, 0, sizeof(zs));
    if (deflateInit2(&zs, level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return ATHENA_ARCHIVE_ERR_MEMORY;
    /* deflateBound includes the gzip wrapper once the stream is initialized. */
    bound = deflateBound(&zs, (uLong)size);
    out = malloc(bound);
    if (!out) {
        deflateEnd(&zs);
        return ATHENA_ARCHIVE_ERR_MEMORY;
    }
    zs.next_in = (Bytef *)data;
    zs.avail_in = (uInt)size;
    zs.next_out = out;
    zs.avail_out = (uInt)bound;
    zret = deflate(&zs, Z_FINISH);
    deflateEnd(&zs);
    if (zret != Z_STREAM_END) {
        free(out);
        return ATHENA_ARCHIVE_ERR_MEMORY;
    }

    *out_size = bound - zs.avail_out;
    {
        unsigned char *shrunk = realloc(out, *out_size);
        if (shrunk)
            out = shrunk;
    }
    *out_data = out;
    return ATHENA_ARCHIVE_OK;
}
