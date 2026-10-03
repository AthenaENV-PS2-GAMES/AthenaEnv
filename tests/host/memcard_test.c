/* Host test of the memcard module against the fake libmc of fake_libmc.h. */
#include "host_runtime.h"

#include "memcard.c"
#include "job.c"
#include "memcard_job.c"

#include "fake_libmc.h"

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static uint8_t *pattern(size_t size, unsigned seed) {
    uint8_t *data = malloc(size ? size : 1);
    for (size_t i = 0; i < size; i++)
        data[i] = (uint8_t)(i * 31 + seed);
    return data;
}

static int cancel_after;
static int cancel_progress(uint64_t done, uint64_t total, void *user) {
    (void)total; (void)user;
    return done >= (uint64_t)cancel_after ? -1 : 0;
}

static bool open_fds(int count) {
    int open = 0;
    for (int i = 0; i < FAKE_FDS; i++)
        open += fake_fds[i].open;
    return open == count;
}

/* ------------------------------------------------------------------------ */
/* Tests                                                                    */
/* ------------------------------------------------------------------------ */

static void test_paths(void) {
    char out[ATHENA_MEMCARD_PATH_MAX + 1];
    int port = -1;

    CHECK(athena_memcard_parse_path("mc0:/SAVE/data.bin", &port, out, sizeof(out)) == 0 &&
        port == 0 && !strcmp(out, "/SAVE/data.bin"), "plain path: %s", out);
    CHECK(athena_memcard_parse_path("mc1:", &port, out, sizeof(out)) == 0 && port == 1 &&
        !strcmp(out, "/"), "root of mc1: %s", out);
    CHECK(athena_memcard_parse_path("MC0:SAVE//a/./b/../c/", &port, out, sizeof(out)) == 0 &&
        !strcmp(out, "/SAVE/a/c"), "normalized: %s", out);
    CHECK(athena_memcard_parse_path("mc0:/A/..", &port, out, sizeof(out)) == 0 &&
        !strcmp(out, "/"), "back to root: %s", out);
    CHECK(athena_memcard_parse_path("mc0:/..", &port, out, sizeof(out)) == ATHENA_MEMCARD_ERR_ARGUMENT,
        "above the root");
    CHECK(athena_memcard_parse_path("mc2:/A", &port, out, sizeof(out)) == ATHENA_MEMCARD_ERR_ARGUMENT,
        "port 2");
    CHECK(athena_memcard_parse_path("mass:/A", &port, out, sizeof(out)) == ATHENA_MEMCARD_ERR_ARGUMENT,
        "other device");
    CHECK(athena_memcard_parse_path("mc0:/A*", &port, out, sizeof(out)) == ATHENA_MEMCARD_ERR_ARGUMENT,
        "wildcard");
    CHECK(athena_memcard_parse_path("mc0:/A\tB", &port, out, sizeof(out)) == ATHENA_MEMCARD_ERR_ARGUMENT,
        "control character");
    CHECK(athena_memcard_parse_path("mc0:/0123456789012345678901234567890", &port, out, sizeof(out)) == 0,
        "31-byte name");
    CHECK(athena_memcard_parse_path("mc0:/01234567890123456789012345678901", &port, out, sizeof(out)) ==
        ATHENA_MEMCARD_ERR_ARGUMENT, "32-byte name");
    CHECK(athena_memcard_normalize("/A/B", out, 4) == ATHENA_MEMCARD_ERR_ARGUMENT, "output too small");
}

static void test_time(void) {
    AthenaMemcardTime t;

    athena_memcard_time_from_unix(0, &t);
    CHECK(t.year == 1970 && t.month == 1 && t.day == 1 && t.hour == 9 && t.min == 0,
        "epoch in JST: %d-%d-%d %d:%d", t.year, t.month, t.day, t.hour, t.min);
    CHECK(athena_memcard_time_to_unix(&t) == 0, "epoch back");
    athena_memcard_time_from_unix(1790000000, &t);
    CHECK(athena_memcard_time_to_unix(&t) == 1790000000, "round trip");
    t = (AthenaMemcardTime){ 2024, 2, 29, 23, 59, 59 };
    CHECK(athena_memcard_time_to_unix(&t) == 1709218799, "leap day: %lld",
        (long long)athena_memcard_time_to_unix(&t));
    t.month = 0;
    CHECK(athena_memcard_time_to_unix(&t) == 0, "invalid date");
}

static void test_not_ready(void) {
    AthenaMemcardInfo info;

    CHECK(athena_memcard_get_info(0, &info) == ATHENA_MEMCARD_ERR_NOT_READY, "no lock before init");
    athena_memcard_module_init();
    fake_status = ATHENA_MEMCARD_ERR_NOT_READY;
    CHECK(athena_memcard_get_info(0, &info) == ATHENA_MEMCARD_ERR_NOT_READY, "driver down");
    fake_status = ATHENA_MEMCARD_OK;
    CHECK(athena_memcard_get_info(2, &info) == ATHENA_MEMCARD_ERR_ARGUMENT, "bad port");
}

static void test_info(void) {
    AthenaMemcardInfo info;
    int type = -1, free_clusters = -1, formatted = -1;

    CHECK(athena_memcard_get_info(0, &info) == 0 && info.type == ATHENA_MEMCARD_TYPE_PS2 &&
        info.formatted && info.changed && info.free_clusters == 1024,
        "first detection: type %d formatted %d changed %d free %u",
        info.type, info.formatted, info.changed, info.free_clusters);
    CHECK(athena_memcard_get_info(0, &info) == 0 && !info.changed, "same card");
    fake_swapped = true;
    CHECK(athena_memcard_get_info(0, &info) == 0 && info.changed, "swapped card");
    CHECK(athena_memcard_get_info(1, &info) == 0 && info.type == ATHENA_MEMCARD_TYPE_NONE &&
        !info.formatted && info.free_clusters == 0, "empty slot is not an error");
    CHECK(athena_memcard_get_info_raw(0, &type, &free_clusters, &formatted) == 0 &&
        type == 2 && formatted == 1 && free_clusters == 1024, "raw info for System");

    /* A swap seen by another command still shows in the next get_info. */
    fake_swapped = true;
    AthenaMemcardEntry entry;
    CHECK(athena_memcard_stat(0, "/", &entry) == 0, "stat after swap retries");
    CHECK(athena_memcard_get_info(0, &info) == 0 && info.changed, "change seen by stat reported");

    fake_formatted = false;
    CHECK(athena_memcard_get_info(0, &info) == 0 && !info.formatted, "unformatted card");
    CHECK(athena_memcard_stat(0, "/", &entry) == ATHENA_MEMCARD_ERR_UNFORMATTED, "unformatted stat");
    fake_formatted = true;
    CHECK(athena_memcard_stat(1, "/", &entry) == ATHENA_MEMCARD_ERR_NO_CARD, "no card stat");
}

static void test_directories(void) {
    AthenaMemcardEntry entry, *entries = NULL;
    int count = -1;

    /* fake_reset() left a card nobody authenticated: the first command detects it. */
    CHECK(!fake_authed, "card not authenticated yet");
    CHECK(athena_memcard_mkdir(0, "/GAME", false) == 1 && fake_authed, "mkdir as the first command");
    CHECK(athena_memcard_mkdir(0, "/GAME", false) == ATHENA_MEMCARD_ERR_EXISTS, "mkdir existing");
    CHECK(athena_memcard_mkdir(0, "/GAME", true) == 0, "mkdir -p existing");
    CHECK(athena_memcard_mkdir(0, "/NOPE/SUB", false) == ATHENA_MEMCARD_ERR_NOT_FOUND, "missing parent");
    CHECK(athena_memcard_mkdir(0, "/A/B/C", true) == 1 && fake_is_dir("/A/B/C") && fake_is_dir("/A/B"),
        "mkdir -p");
    CHECK(athena_memcard_mkdir(0, "/", true) == 0, "mkdir -p root");
    CHECK(athena_memcard_stat(0, "/A/B", &entry) == 0 && athena_memcard_entry_is_dir(&entry) &&
        !strcmp(entry.name, "B") && entry.created.year == 2026, "stat dir");
    CHECK(athena_memcard_stat(0, "/A/X", &entry) == ATHENA_MEMCARD_ERR_NOT_FOUND, "stat missing");
    CHECK(athena_memcard_stat(0, "/Q/X", &entry) == ATHENA_MEMCARD_ERR_NOT_FOUND, "stat missing parent");

    /* More entries than one mcGetDir batch. */
    for (int i = 0; i < 45; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/GAME/f%02d", i);
        CHECK(athena_memcard_write_file(0, path, "x", 1, NULL) == 1, "write %s", path);
    }
    CHECK(athena_memcard_list(0, "/GAME", &entries, &count) == 0 && count == 45, "list %d", count);
    if (entries && count == 45) {
        bool dots = false;
        for (int i = 0; i < count; i++)
            dots |= mc_is_dot_entry(entries[i].name);
        CHECK(!dots && !strcmp(entries[44].name, "f44") && entries[0].size == 1, "entries");
    }
    free(entries);
    CHECK(athena_memcard_list(0, "/", &entries, &count) == 0 && count == 2, "root %d", count);
    free(entries);
    CHECK(athena_memcard_list(0, "/GAME/f00", &entries, &count) == ATHENA_MEMCARD_ERR_NOT_DIR, "list a file");
    CHECK(athena_memcard_list(0, "/NONE", &entries, &count) == ATHENA_MEMCARD_ERR_NOT_FOUND, "list missing");
    CHECK(athena_memcard_free_entries(0, "/GAME") == 13, "free entries");

    CHECK(athena_memcard_remove(0, "/A", false, NULL, NULL) == ATHENA_MEMCARD_ERR_NOT_EMPTY, "rm non-empty");
    CHECK(athena_memcard_remove(0, "/GAME", true, NULL, NULL) == 0 && fake_find("/GAME") < 0 &&
        fake_find("/GAME/f10") < 0, "rm -r");
    CHECK(athena_memcard_remove(0, "/A", true, NULL, NULL) == 0 && fake_find("/A/B/C") < 0, "rm -r nested");
    CHECK(athena_memcard_remove(0, "/", true, NULL, NULL) == ATHENA_MEMCARD_ERR_ARGUMENT, "rm root");
    CHECK(athena_memcard_remove(0, "/A", false, NULL, NULL) == ATHENA_MEMCARD_ERR_NOT_FOUND, "rm missing");
}

static void test_files(void) {
    const size_t size = 40000; /* three read/write blocks */
    uint8_t *data = pattern(size, 7);
    void *back = NULL;
    size_t back_size = 0;
    AthenaMemcardWriteOptions options = { .create_dirs = true };

    CHECK(athena_memcard_write_file(0, "/SAVE/data.bin", data, size, NULL) == ATHENA_MEMCARD_ERR_NOT_FOUND,
        "no parent without create_dirs");
    CHECK(athena_memcard_write_file(0, "/SAVE/data.bin", data, size, &options) == (int)size, "write");
    CHECK(athena_memcard_read_file(0, "/SAVE/data.bin", &back, &back_size, NULL, NULL) == 0 &&
        back_size == size && !memcmp(back, data, size), "read back");
    CHECK(((uintptr_t)back & 63) == 0, "read buffer aligned for DMA");
    free(back);
    CHECK(open_fds(0), "handles released");

    CHECK(athena_memcard_write_file(0, "/SAVE/empty", "", 0, NULL) == 0, "empty file");
    CHECK(athena_memcard_read_file(0, "/SAVE/empty", &back, &back_size, NULL, NULL) == 0 && back_size == 0,
        "read empty");
    free(back);
    CHECK(athena_memcard_read_file(0, "/SAVE", &back, &back_size, NULL, NULL) == ATHENA_MEMCARD_ERR_IS_DIR,
        "read a directory");
    CHECK(athena_memcard_write_file(0, "/SAVE", "x", 1, NULL) == ATHENA_MEMCARD_ERR_IS_DIR, "write a directory");
    CHECK(athena_memcard_read_file(0, "/SAVE/none", &back, &back_size, NULL, NULL) == ATHENA_MEMCARD_ERR_NOT_FOUND,
        "read missing");

    /* Atomic replace leaves only the new file. */
    options.atomic = true;
    CHECK(athena_memcard_write_file(0, "/SAVE/data.bin", "new", 3, &options) == 3, "atomic write");
    CHECK(fake_find("/SAVE/data.bin~") < 0 && fake_fs[fake_find("/SAVE/data.bin")].size == 3,
        "atomic swap done");
    CHECK(athena_memcard_write_file(0, "/SAVE/fresh", "abc", 3, &options) == 3 && fake_find("/SAVE/fresh") >= 0,
        "atomic write of a new file");

    /* Out of space: the old file survives an atomic write, a plain write removes the partial file. */
    fake_capacity = fake_used() + 20000;
    CHECK(athena_memcard_write_file(0, "/SAVE/data.bin", data, size, &options) == ATHENA_MEMCARD_ERR_FULL,
        "atomic write on a full card");
    CHECK(fake_find("/SAVE/data.bin~") < 0 && fake_fs[fake_find("/SAVE/data.bin")].size == 3, "old file kept");
    options.atomic = false;
    CHECK(athena_memcard_write_file(0, "/SAVE/big", data, size, &options) == ATHENA_MEMCARD_ERR_FULL, "full");
    CHECK(fake_find("/SAVE/big") < 0 && open_fds(0), "partial file removed");
    fake_capacity = 1 << 20;

    /* Cancel through the progress hook. */
    cancel_after = 16 * 1024;
    options.progress = cancel_progress;
    CHECK(athena_memcard_write_file(0, "/SAVE/big", data, size, &options) == ATHENA_MEMCARD_ERR_CANCELLED,
        "cancelled write");
    CHECK(fake_find("/SAVE/big") < 0, "cancelled file removed");
    options.progress = NULL;

    /* Rename and attributes. */
    CHECK(athena_memcard_rename(0, "/SAVE/fresh", "data.bin") == ATHENA_MEMCARD_ERR_EXISTS, "rename onto existing");
    CHECK(athena_memcard_rename(0, "/SAVE/fresh", "a/b") == ATHENA_MEMCARD_ERR_ARGUMENT, "rename to a path");
    CHECK(athena_memcard_rename(0, "/SAVE/fresh", "renamed") == 0 && fake_find("/SAVE/renamed") >= 0,
        "rename");
    CHECK(athena_memcard_rename(0, "/SAVE", "SAVE2") == 0 && fake_find("/SAVE2/renamed") >= 0,
        "rename a directory");

    AthenaMemcardSetInfo info = {
        .fields = ATHENA_MEMCARD_SET_ATTRIBUTES | ATHENA_MEMCARD_SET_MODIFIED,
        .attributes = ATHENA_MEMCARD_ATTR_READABLE | ATHENA_MEMCARD_ATTR_HIDDEN,
        .modified = { 2030, 6, 7, 8, 9, 10 },
    };
    AthenaMemcardEntry entry;
    CHECK(athena_memcard_set_info(0, "/SAVE2/renamed", &info) == 0, "set info");
    CHECK(athena_memcard_stat(0, "/SAVE2/renamed", &entry) == 0 &&
        entry.attributes == (ATHENA_MEMCARD_ATTR_READABLE | ATHENA_MEMCARD_ATTR_HIDDEN |
        ATHENA_MEMCARD_ATTR_FILE | 0x0400 | ATHENA_MEMCARD_ATTR_EXISTS) &&
        entry.modified.year == 2030 && entry.created.year == 2026,
        "attributes 0x%x, other bits kept", entry.attributes);
    CHECK(athena_memcard_write_file(0, "/SAVE2/renamed", "x", 1, NULL) == ATHENA_MEMCARD_ERR_DENIED,
        "read-only file cannot be replaced");
    info.fields = 0x80;
    CHECK(athena_memcard_set_info(0, "/SAVE2/renamed", &info) == ATHENA_MEMCARD_ERR_ARGUMENT, "unknown field");
    /* As on the console: the driver deletes read-only entries. */
    CHECK(athena_memcard_remove(0, "/SAVE2/renamed", false, NULL, NULL) == 0, "read-only file removed");
    free(data);
}

static void test_open_files(void) {
    AthenaMemcardFile *files[4] = { 0 };
    char buffer[16];

    CHECK(athena_memcard_mkdir(0, "/F", false) == 1, "mkdir");
    CHECK(athena_memcard_open(0, "/F", ATHENA_MEMCARD_OPEN_READ, &files[0]) == ATHENA_MEMCARD_ERR_IS_DIR, "open dir");
    CHECK(athena_memcard_open(0, "/F/x", ATHENA_MEMCARD_OPEN_READ, &files[0]) == ATHENA_MEMCARD_ERR_NOT_FOUND,
        "open missing");
    CHECK(athena_memcard_open(0, "/F/x", ATHENA_MEMCARD_OPEN_CREATE, &files[0]) == ATHENA_MEMCARD_ERR_ARGUMENT,
        "create without write");
    CHECK(athena_memcard_open(0, "/F/x", ATHENA_MEMCARD_OPEN_WRITE | ATHENA_MEMCARD_OPEN_CREATE, &files[0]) == 0,
        "create");
    CHECK(athena_memcard_write(files[0], "hello world", 11) == 11 && athena_memcard_tell(files[0]) == 11, "write");
    CHECK(athena_memcard_read(files[0], buffer, 4) == ATHENA_MEMCARD_ERR_DENIED, "read a write-only file");
    CHECK(athena_memcard_seek(files[0], 6, ATHENA_MEMCARD_SEEK_SET) == 6, "seek");
    CHECK(athena_memcard_write(files[0], "WORLD", 5) == 5, "overwrite");
    CHECK(athena_memcard_flush(files[0]) == 0, "flush");
    CHECK(athena_memcard_close(files[0]) == 0, "close");

    CHECK(athena_memcard_open(0, "/F/x", ATHENA_MEMCARD_OPEN_READ, &files[0]) == 0 &&
        athena_memcard_read(files[0], buffer, sizeof(buffer)) == 11 && !memcmp(buffer, "hello WORLD", 11),
        "read back");
    CHECK(athena_memcard_seek(files[0], -5, ATHENA_MEMCARD_SEEK_END) == 6, "seek from end");
    CHECK(athena_memcard_read(files[0], buffer, 16) == 5 && athena_memcard_read(files[0], buffer, 16) == 0,
        "end of file");
    CHECK(athena_memcard_seek(files[0], 0, 7) == ATHENA_MEMCARD_ERR_ARGUMENT, "bad whence");

    CHECK(athena_memcard_open(0, "/F/y", ATHENA_MEMCARD_OPEN_WRITE | ATHENA_MEMCARD_OPEN_CREATE, &files[1]) == 0 &&
        athena_memcard_open(0, "/F/z", ATHENA_MEMCARD_OPEN_WRITE | ATHENA_MEMCARD_OPEN_CREATE, &files[2]) == 0,
        "three handles");
    CHECK(athena_memcard_open(0, "/F/w", ATHENA_MEMCARD_OPEN_WRITE | ATHENA_MEMCARD_OPEN_CREATE, &files[3]) ==
        ATHENA_MEMCARD_ERR_TOO_MANY_OPEN && !files[3], "fourth handle");
    CHECK(athena_memcard_write_file(0, "/F/w", "x", 1, NULL) == ATHENA_MEMCARD_ERR_TOO_MANY_OPEN,
        "whole-file ops share the limit");
    athena_memcard_close(files[2]);

    /* A swapped card kills the descriptors. */
    fake_swapped = true;
    CHECK(athena_memcard_read(files[0], buffer, 4) == ATHENA_MEMCARD_ERR_CHANGED, "read after swap");
    athena_memcard_close(files[0]);
    athena_memcard_close(files[1]);
    CHECK(open_fds(0), "handles gone with the card");

    /* A quick swap acknowledged by get_info: the old handle must not reach the new card. */
    AthenaMemcardInfo info;
    CHECK(athena_memcard_open(0, "/F/x", ATHENA_MEMCARD_OPEN_READ, &files[0]) == 0, "open before a swap");
    fake_swapped = true;
    CHECK(athena_memcard_get_info(0, &info) == 0 && info.changed, "swap acknowledged");
    int commands = fake_commands;
    CHECK(athena_memcard_read(files[0], buffer, 4) == ATHENA_MEMCARD_ERR_CHANGED && fake_commands == commands,
        "read refused without a driver command");
    CHECK(athena_memcard_close(files[0]) == ATHENA_MEMCARD_ERR_CHANGED && fake_commands == commands,
        "not closed on the new card");
    memset(fake_fds, 0, sizeof(fake_fds));

    /* The card left: the handle is refused too. */
    CHECK(athena_memcard_open(0, "/F/x", ATHENA_MEMCARD_OPEN_READ, &files[0]) == 0, "open before removal");
    fake_present = false;
    CHECK(athena_memcard_get_info(0, &info) == 0 && info.type == ATHENA_MEMCARD_TYPE_NONE, "card removed");
    fake_present = true;
    CHECK(athena_memcard_read(files[0], buffer, 4) == ATHENA_MEMCARD_ERR_CHANGED, "read after removal");
    athena_memcard_close(files[0]);
    memset(fake_fds, 0, sizeof(fake_fds));

    /* An IOP reset: the old number must not be closed (it may be reused). */
    CHECK(athena_memcard_open(0, "/F/x", ATHENA_MEMCARD_OPEN_READ, &files[0]) == 0, "reopen");
    fake_generation++;
    int closes = fake_closes;
    CHECK(athena_memcard_read(files[0], buffer, 4) == ATHENA_MEMCARD_ERR_CLOSED, "stale descriptor");
    CHECK(athena_memcard_close(files[0]) == ATHENA_MEMCARD_ERR_CLOSED && fake_closes == closes,
        "stale descriptor not closed");
    memset(fake_fds, 0, sizeof(fake_fds));
    CHECK(athena_memcard_close(NULL) == ATHENA_MEMCARD_ERR_CLOSED, "close NULL");
}

/* Commands each common operation sends to the driver: a regression guard for performance. */
static void test_costs(void) {
    AthenaMemcardWriteOptions options = { .create_dirs = true };
    char buffer[16];
    int before;

    CHECK(athena_memcard_mkdir(0, "/COST/A", true) == 1, "setup");
    before = fake_commands;
    CHECK(athena_memcard_write_file(0, "/COST/A/save", "x", 1, &options) == 1, "save");
    CHECK(fake_commands - before == 4, "save in an existing directory: %d commands", fake_commands - before);
    before = fake_commands;
    CHECK(athena_memcard_mkdir(0, "/COST/A", true) == 0, "mkdir -p existing");
    CHECK(fake_commands - before == 2, "mkdir -p of an existing tree: %d commands", fake_commands - before);
    before = fake_commands;
    CHECK(athena_memcard_mkdir(0, "/COST/A/B", true) == 1, "mkdir -p leaf");
    CHECK(fake_commands - before == 1, "mkdir -p of a new leaf: %d commands", fake_commands - before);
    options.atomic = true;
    before = fake_commands;
    CHECK(athena_memcard_write_file(0, "/COST/A/save", "yz", 2, &options) == 2, "atomic save");
    CHECK(fake_commands - before == 6, "atomic replace: %d commands", fake_commands - before);
    options.atomic = false;
    CHECK(athena_memcard_write_file(0, "/COST/N1/N2/save", "x", 1, &options) == 1 && fake_is_dir("/COST/N1/N2"),
        "missing parents created on demand");
    options.create_dirs = false;
    CHECK(athena_memcard_write_file(0, "/COST/N3/save", "x", 1, &options) == ATHENA_MEMCARD_ERR_NOT_FOUND,
        "no parents without create_dirs");

    /* Append and size. */
    AthenaMemcardFile *file = NULL;
    CHECK(athena_memcard_open(0, "/COST/log", ATHENA_MEMCARD_OPEN_WRITE | ATHENA_MEMCARD_OPEN_APPEND, &file) == 0 &&
        athena_memcard_size(file) == 0, "append creates");
    CHECK(athena_memcard_write(file, "one", 3) == 3 && athena_memcard_size(file) == 3, "size grows");
    athena_memcard_close(file);
    CHECK(athena_memcard_open(0, "/COST/log", ATHENA_MEMCARD_OPEN_READ | ATHENA_MEMCARD_OPEN_WRITE |
        ATHENA_MEMCARD_OPEN_APPEND, &file) == 0 && athena_memcard_size(file) == 3 &&
        athena_memcard_tell(file) == 3, "append keeps the file and starts at its end");
    CHECK(athena_memcard_write(file, "two", 3) == 3 && athena_memcard_size(file) == 6, "appended");
    CHECK(athena_memcard_seek(file, 0, ATHENA_MEMCARD_SEEK_SET) == 0 &&
        athena_memcard_read(file, buffer, sizeof(buffer)) == 6 && !memcmp(buffer, "onetwo", 6), "content");
    athena_memcard_close(file);
    CHECK(athena_memcard_open(0, "/COST/log", ATHENA_MEMCARD_OPEN_APPEND, &file) == ATHENA_MEMCARD_ERR_ARGUMENT,
        "append needs write");
    CHECK(athena_memcard_open(0, "/COST/log", ATHENA_MEMCARD_OPEN_READ, &file) == 0 &&
        athena_memcard_size(file) == 6, "size of an existing file");
    athena_memcard_close(file);
    CHECK(athena_memcard_open(0, "/COST/log", ATHENA_MEMCARD_OPEN_WRITE | ATHENA_MEMCARD_OPEN_CREATE, &file) == 0 &&
        athena_memcard_size(file) == 0, "create truncates");
    athena_memcard_close(file);

    /* readFile leaves a terminator after the data. */
    void *data = NULL;
    size_t size = 0;
    CHECK(athena_memcard_write_file(0, "/COST/text", "{\"a\":1}", 7, NULL) == 7 &&
        athena_memcard_read_file(0, "/COST/text", &data, &size, NULL, NULL) == 0 && size == 7 &&
        ((char *)data)[7] == '\0', "terminated");
    free(data);
    CHECK(athena_memcard_remove(0, "/COST", true, NULL, NULL) == 0, "cleanup");
}

static void test_busy_and_format(void) {
    AthenaMemcardInfo info;
    AthenaMemcardEntry entry;

    /* Someone bypassing the lock left a command in flight. */
    fake_pending = 6;
    fake_in_command = 1;
    CHECK(athena_memcard_get_info(0, &info) == ATHENA_MEMCARD_ERR_BUSY, "busy driver");
    fake_pending = 0;
    fake_in_command = 0;

    CHECK(athena_memcard_unformat(0) == 0 && athena_memcard_stat(0, "/", &entry) == ATHENA_MEMCARD_ERR_UNFORMATTED,
        "unformat");
    CHECK(athena_memcard_format(0) == 0 && athena_memcard_stat(0, "/", &entry) == 0 &&
        fake_find("/F/x") < 0, "format");
    CHECK(athena_memcard_format(1) == ATHENA_MEMCARD_ERR_NO_CARD, "format empty slot");
    CHECK(athena_memcard_format(3) == ATHENA_MEMCARD_ERR_ARGUMENT, "format bad port");
}

static void test_icon_sys(void) {
    uint8_t out[ATHENA_MEMCARD_ICON_SYS_SIZE];
    AthenaMemcardIconSys icon;
    uint32_t alpha, color;
    float light;

    athena_memcard_icon_sys_defaults(&icon);
    icon.title = "Ab 1\nZ!";
    icon.icon = "icon.ico";
    icon.delete_icon = "del.ico";
    CHECK(athena_memcard_build_icon_sys(&icon, out) == 0, "build");
    CHECK(!memcmp(out, "PS2D", 4), "magic");
    CHECK(out[6] == 8 && out[7] == 0, "line break at byte 8: %d", out[6]);
    memcpy(&alpha, out + 12, 4);
    memcpy(&color, out + 16, 4);
    memcpy(&light, out + 176, 4);
    CHECK(alpha == 0x60 && color == 68 && light == 0.5f, "background and lights");
    CHECK(out[192] == 0x82 && out[193] == 0x60 && out[194] == 0x82 && out[195] == 0x82 &&
        out[196] == 0x81 && out[197] == 0x40 && out[198] == 0x82 && out[199] == 0x50 &&
        out[200] == 0x82 && out[201] == 0x79 && out[202] == 0x81 && out[203] == 0x49 &&
        out[204] == 0 && out[205] == 0, "Shift-JIS title");
    CHECK(!strcmp((char *)out + 260, "icon.ico") && !strcmp((char *)out + 324, "icon.ico") &&
        !strcmp((char *)out + 388, "del.ico"), "icon names");

    icon.title = "One line";
    CHECK(athena_memcard_build_icon_sys(&icon, out) == 0 && out[6] == 16, "no break: offset %d", out[6]);
    icon.title = "123456789012345678901234567890123";
    CHECK(athena_memcard_build_icon_sys(&icon, out) == 0, "33 characters");
    icon.title = "1234567890123456789012345678901234";
    CHECK(athena_memcard_build_icon_sys(&icon, out) == ATHENA_MEMCARD_ERR_ARGUMENT, "34 characters");
    icon.title = "a\nb\nc";
    CHECK(athena_memcard_build_icon_sys(&icon, out) == ATHENA_MEMCARD_ERR_ARGUMENT, "two breaks");
    icon.title = "caf\xc3\xa9";
    CHECK(athena_memcard_build_icon_sys(&icon, out) == ATHENA_MEMCARD_ERR_ARGUMENT, "non-ASCII");
    icon.title = "ok";
    icon.icon = "dir/icon.ico";
    CHECK(athena_memcard_build_icon_sys(&icon, out) == ATHENA_MEMCARD_ERR_ARGUMENT, "icon path");
    icon.icon = "icon.ico";
    icon.background[1][2] = 256;
    CHECK(athena_memcard_build_icon_sys(&icon, out) == ATHENA_MEMCARD_ERR_ARGUMENT, "color range");
}

static AthenaMemcardJobStatus wait_job(AthenaMemcardJob *job) {
    AthenaMemcardJobStatus status;
    CHECK(athena_memcard_job_wait(job, 5000), "job settles");
    athena_memcard_job_status(job, &status);
    return status;
}

static void test_jobs(void) {
    const size_t size = 50000;
    uint8_t *data = pattern(size, 3);
    AthenaMemcardWriteOptions options = { .create_dirs = true };
    AthenaMemcardJobStatus status;
    AthenaMemcardJob *job;
    void *back = NULL;
    size_t back_size = 0;

    job = athena_memcard_job_write(0, "/JOB/data", data, size, &options);
    memset(data, 0, size); /* the job owns a copy */
    status = wait_job(job);
    CHECK(status.state == ATHENA_MEMCARD_JOB_DONE && status.result == (int)size &&
        status.bytes_done == size && status.bytes_total == size, "write job %d", status.result);
    athena_memcard_job_destroy(job);

    job = athena_memcard_job_read(0, "/JOB/data");
    status = wait_job(job);
    CHECK(status.state == ATHENA_MEMCARD_JOB_DONE && status.bytes_total == size, "read job");
    CHECK(athena_memcard_job_take_data(job, &back, &back_size) == 0 && back_size == size &&
        ((uint8_t *)back)[1] == (uint8_t)(31 + 3), "read job data");
    void *again = NULL;
    size_t again_size = 1;
    athena_memcard_job_take_data(job, &again, &again_size);
    CHECK(!again && again_size == 0, "data handed over once");
    athena_memcard_job_destroy(job);
    free(back);

    job = athena_memcard_job_read(0, "/JOB/none");
    status = wait_job(job);
    CHECK(status.state == ATHENA_MEMCARD_JOB_FAILED && status.result == ATHENA_MEMCARD_ERR_NOT_FOUND,
        "failed job");
    athena_memcard_job_destroy(job);

    /* Cancelled before the first block goes out. */
    job = athena_memcard_job_write(0, "/JOB/cancel", data, size, &options);
    athena_memcard_job_cancel(job);
    status = wait_job(job);
    CHECK(status.state == ATHENA_MEMCARD_JOB_CANCELLED || status.state == ATHENA_MEMCARD_JOB_DONE,
        "cancel state %d", status.state);
    if (status.state == ATHENA_MEMCARD_JOB_CANCELLED)
        CHECK(fake_find("/JOB/cancel") < 0, "cancelled file removed");
    athena_memcard_job_destroy(job);

    job = athena_memcard_job_remove(0, "/JOB", true);
    status = wait_job(job);
    CHECK(status.state == ATHENA_MEMCARD_JOB_DONE && fake_find("/JOB") < 0, "remove job");
    athena_memcard_job_destroy(job);

    job = athena_memcard_job_format(0);
    CHECK(wait_job(job).state == ATHENA_MEMCARD_JOB_DONE, "format job");
    athena_memcard_job_destroy(job);

    CHECK(athena_memcard_job_read(2, "/x") == NULL, "job on a bad port");
    /* Destroying a running job waits for it: no card handle is left open. */
    job = athena_memcard_job_write(0, "/drop", data, size, &options);
    athena_memcard_job_destroy(job);
    CHECK(open_fds(0), "handles released");
    /* Jobs run on the shared pool, whose workers stop with the runtime. */
    CHECK(threads_alive <= ATHENA_JOB_WORKERS, "pool workers: %d", threads_alive);
    athena_job_pool_stop();
    CHECK(threads_alive == 0, "workers joined: %d", threads_alive);
    free(data);
}

/* Several threads at once: the fake aborts if two commands overlap. */
static void *hammer(void *arg) {
    int id = (int)(intptr_t)arg;
    char path[32];
    uint8_t data[3000];
    AthenaMemcardInfo info;

    memset(data, id, sizeof(data));
    snprintf(path, sizeof(path), "/T/t%d", id);
    for (int i = 0; i < 20; i++) {
        void *back = NULL;
        size_t size = 0;
        if (athena_memcard_write_file(0, path, data, sizeof(data), NULL) != (int)sizeof(data) ||
            athena_memcard_read_file(0, path, &back, &size, NULL, NULL) != 0 ||
            size != sizeof(data) || ((uint8_t *)back)[size - 1] != (uint8_t)id ||
            athena_memcard_get_info(0, &info) != 0)
            __atomic_add_fetch(&failures, 1, __ATOMIC_SEQ_CST);
        free(back);
    }
    return NULL;
}

static void test_threads(void) {
    pthread_t threads[4];

    CHECK(athena_memcard_mkdir(0, "/T", false) == 1, "mkdir");
    for (int i = 0; i < 4; i++)
        pthread_create(&threads[i], NULL, hammer, (void *)(intptr_t)(i + 1));
    for (int i = 0; i < 4; i++)
        pthread_join(threads[i], NULL);
    CHECK(fake_in_command == 0, "no command left in flight");
}

int main(void) {
    test_paths();
    test_time();
    test_not_ready();
    test_info();
    fake_reset();
    test_directories();
    test_files();
    test_open_files();
    test_costs();
    test_busy_and_format();
    test_icon_sys();
    fake_reset();
    test_jobs();
    fake_reset();
    test_threads();
    printf("memcard: %d checks, %d failures (%d driver commands)\n", checks, failures, fake_commands);
    return failures != 0;
}
