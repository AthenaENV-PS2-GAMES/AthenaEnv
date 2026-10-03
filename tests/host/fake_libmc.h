/*
 * Fake libmc for host tests: a PS2 card in RAM on port 0 and an empty slot
 * on port 1. It follows the driver as read from the PS2SDK sources
 * (mcman/mcserv/libmc): one command in flight (a second one gets the busy
 * command number), -1 once after a card swap, mkdir of an existing name
 * gives "no entry", three file handles, create replaces, listings continue
 * with mode 1. Also provides the memcard_internal.h driver hooks.
 * Include once per program.
 */
#ifndef ATHENA_FAKE_LIBMC_H
#define ATHENA_FAKE_LIBMC_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libmc.h>
#include <athena/memcard.h>

/* ------------------------------------------------------------------------ */
/* Fake driver state                                                        */
/* ------------------------------------------------------------------------ */

static int fake_status = ATHENA_MEMCARD_OK;
static uint32_t fake_generation = 1;

int memcard_driver_status(void) { return fake_status; }
uint32_t memcard_driver_generation(void) { return fake_generation; }
int athena_memcard_prepare(void) { return fake_status; }

/* ------------------------------------------------------------------------ */
/* Fake libmc                                                               */
/* ------------------------------------------------------------------------ */

#define FAKE_ENTRIES 256
#define FAKE_FDS 3

typedef struct {
    bool used;
    char path[1024];
    bool dir;
    uint8_t *data;
    size_t size;
    uint16_t attr;
    sceMcStDateTime created, modified;
} FakeEntry;

typedef struct {
    bool open;
    int entry;
    int pos;
    int flags;
} FakeFd;

static FakeEntry fake_fs[FAKE_ENTRIES];
static FakeFd fake_fds[FAKE_FDS];
static size_t fake_capacity = 1 << 20;      /* bytes the card can hold */
static bool fake_formatted = true;
static bool fake_swapped = true;            /* the first detection after boot */
static bool fake_present = true;            /* a card in slot 1 */
/* Commands only run a light probe, which fails until mcGetInfo authenticated the card. */
static bool fake_authed = false;
static bool fake_info_new = true;
static int fake_pending;                    /* command in flight, 0 = none */
static int fake_result;
static int fake_polls;                      /* mcSync(NOWAIT) calls before it completes */
static int fake_commands;
static int fake_closes;
static int fake_in_command;                 /* concurrent entries, must stay 0/1 */
/* Listing in progress. */
static int fake_list[FAKE_ENTRIES + 2];
static int fake_list_count, fake_list_pos;

static pthread_mutex_t fake_mutex = PTHREAD_MUTEX_INITIALIZER;

static size_t fake_used(void) {
    size_t used = 0;
    for (int i = 0; i < FAKE_ENTRIES; i++)
        if (fake_fs[i].used)
            used += fake_fs[i].size;
    return used;
}

static int fake_find(const char *path) {
    for (int i = 0; i < FAKE_ENTRIES; i++)
        if (fake_fs[i].used && !strcmp(fake_fs[i].path, path))
            return i;
    return -1;
}

/* Parent directory of `path` ("/" for top-level names). */
static void fake_parent(const char *path, char *out) {
    const char *slash = strrchr(path, '/');
    size_t length = (size_t)(slash - path);
    if (length == 0) {
        strcpy(out, "/");
        return;
    }
    memcpy(out, path, length);
    out[length] = '\0';
}

static bool fake_is_dir(const char *path) {
    int index;
    if (!strcmp(path, "/"))
        return true;
    index = fake_find(path);
    return index >= 0 && fake_fs[index].dir;
}

static int fake_add(const char *path, bool dir) {
    for (int i = 0; i < FAKE_ENTRIES; i++) {
        if (!fake_fs[i].used) {
            memset(&fake_fs[i], 0, sizeof(fake_fs[i]));
            fake_fs[i].used = true;
            strcpy(fake_fs[i].path, path);
            fake_fs[i].dir = dir;
            fake_fs[i].attr = dir ? 0x8427 : 0x8417;
            fake_fs[i].created = (sceMcStDateTime){ 0, 5, 4, 3, 2, 1, 2026 };
            fake_fs[i].modified = fake_fs[i].created;
            return i;
        }
    }
    return -1;
}

static void fake_free_entry(int index) {
    free(fake_fs[index].data);
    fake_fs[index].data = NULL;
    fake_fs[index].used = false;
}

static void fake_fill(int index, sceMcTblGetDir *table, const char *name) {
    const FakeEntry *e = &fake_fs[index];
    memset(table, 0, sizeof(*table));
    table->_Create = e->created;
    table->_Modify = e->modified;
    table->FileSizeByte = (u32)e->size;
    table->AttrFile = e->attr;
    snprintf((char *)table->EntryName, sizeof(table->EntryName), "%s", name ? name : strrchr(e->path, '/') + 1);
}

/* Starts a command; the caller fills fake_result. Returns what libmc would. */
static int fake_begin(int command) {
    int ret = 0;

    pthread_mutex_lock(&fake_mutex);
    if (fake_pending) {
        ret = fake_pending;
    } else {
        fake_pending = command;
        fake_polls = 2;
        fake_commands++;
        if (++fake_in_command > 1) {
            printf("  FATAL: two commands in flight\n");
            abort();
        }
    }
    pthread_mutex_unlock(&fake_mutex);
    return ret;
}

/* A path command on port 1 (empty slot) or right after a swap. */
/* No card: mcman drops its descriptors, and the next card is a new one. */
static void fake_no_card(void) {
    for (int i = 0; i < FAKE_FDS; i++)
        fake_fds[i].open = false;
    fake_swapped = true;
    fake_authed = false;
}

static bool fake_detect(int port) {
    if (port != 0 || !fake_present) {
        if (port == 0)
            fake_no_card();
        fake_result = -10;
        return false;
    }
    if (!fake_authed) {
        fake_result = -12;
        return false;
    }
    if (fake_swapped) {
        fake_swapped = false;
        fake_result = sceMcResChangedCard;
        for (int i = 0; i < FAKE_FDS; i++)
            fake_fds[i].open = false;
        return false;
    }
    if (!fake_formatted) {
        fake_result = sceMcResNoFormat;
        return false;
    }
    return true;
}

int mcInit(int type) { (void)type; return 0; }

int mcGetInfo(int port, int slot, int *type, int *free_clusters, int *format) {
    int ret = fake_begin(1);
    if (ret)
        return ret;
    if (port != 0 || !fake_present) {
        if (port == 0)
            fake_no_card();
        *type = 0;
        *free_clusters = 0;
        *format = 0;
        fake_result = -10;
        return 0;
    }
    fake_result = 0;
    fake_authed = true;
    if (fake_swapped || fake_info_new) {
        fake_result = fake_formatted ? sceMcResChangedCard : sceMcResNoFormat;
        fake_swapped = false;
        fake_info_new = false;
    }
    *type = 2;
    *format = fake_formatted;
    *free_clusters = (int)((fake_capacity - fake_used()) / 1024);
    return 0;
}

int mcGetDir(int port, int slot, const char *name, unsigned mode, int maxent, sceMcTblGetDir *table) {
    int ret = fake_begin(0x0D);
    size_t length = strlen(name);

    if (ret)
        return ret;
    if (mode == 0) {
        if (!fake_detect(port))
            return 0;
        fake_list_count = fake_list_pos = 0;
        if (length >= 2 && !strcmp(name + length - 2, "/*")) {
            char dir[1024], parent[1024];
            memcpy(dir, name, length - 2);
            dir[length - 2] = '\0';
            if (!dir[0])
                strcpy(dir, "/");
            if (!fake_is_dir(dir)) {
                fake_result = sceMcResNoEntry;
                return 0;
            }
            fake_list[fake_list_count++] = -1;  /* "." */
            fake_list[fake_list_count++] = -2;  /* ".." */
            for (int i = 0; i < FAKE_ENTRIES; i++) {
                if (!fake_fs[i].used)
                    continue;
                fake_parent(fake_fs[i].path, parent);
                if (!strcmp(parent, dir))
                    fake_list[fake_list_count++] = i;
            }
        } else {
            char parent[1024];
            int index = fake_find(name);
            fake_parent(name, parent);
            if (!fake_is_dir(parent)) {
                fake_result = sceMcResNoEntry;
                return 0;
            }
            if (index >= 0)
                fake_list[fake_list_count++] = index;
        }
    }
    fake_result = 0;
    while (fake_result < maxent && fake_list_pos < fake_list_count) {
        int index = fake_list[fake_list_pos++];
        sceMcTblGetDir *slot_table = &table[fake_result++];
        if (index < 0) {
            memset(slot_table, 0, sizeof(*slot_table));
            strcpy((char *)slot_table->EntryName, index == -1 ? "." : "..");
            slot_table->AttrFile = 0x8427;
        } else {
            fake_fill(index, slot_table, NULL);
        }
    }
    return 0;
}

static int fake_open_fd(int index, int flags) {
    int open_count = 0;

    for (int i = 0; i < FAKE_FDS; i++) {
        if (!fake_fds[i].open)
            continue;
        open_count++;
        if (fake_fds[i].entry == index && (flags & 2) && (fake_fds[i].flags & 2))
            return sceMcResDeniedPermit;
    }
    if (open_count == FAKE_FDS)
        return sceMcResUpLimitHandle;
    for (int i = 0; i < FAKE_FDS; i++) {
        if (!fake_fds[i].open) {
            fake_fds[i] = (FakeFd){ true, index, 0, flags };
            return i;
        }
    }
    return sceMcResUpLimitHandle;
}

int mcOpen(int port, int slot, const char *name, int mode) {
    int ret = fake_begin(2), index;
    char parent[1024];

    if (ret)
        return ret;
    if (!fake_detect(port))
        return 0;
    fake_parent(name, parent);
    index = fake_find(name);
    if (!fake_is_dir(parent)) {
        fake_result = sceMcResNoEntry;
        return 0;
    }
    if (mode & 0x40) {
        fake_result = index >= 0 ? sceMcResNoEntry : (fake_add(name, true) >= 0 ? 0 : -3);
        return 0;
    }
    if (index >= 0 && (mode & 0x202) && !(fake_fs[index].attr & 2)) {
        fake_result = sceMcResDeniedPermit;
        return 0;
    }
    if (index < 0) {
        if (!(mode & 0x200)) {
            fake_result = sceMcResNoEntry;
            return 0;
        }
        index = fake_add(name, false);
    } else if (mode & 0x200) {
        free(fake_fs[index].data);
        fake_fs[index].data = NULL;
        fake_fs[index].size = 0;
    }
    fake_result = fake_open_fd(index, mode | ((mode & 0x200) ? 2 : 0));
    return 0;
}

int mcMkDir(int port, int slot, const char *name) {
    return mcOpen(port, slot, name, 0x40);
}

static FakeFd *fake_fd(int fd) {
    if (fd < 0 || fd >= FAKE_FDS || !fake_fds[fd].open) {
        fake_result = sceMcResDeniedPermit;
        return NULL;
    }
    if (fake_swapped) {
        fake_swapped = false;
        fake_result = sceMcResChangedCard;
        for (int i = 0; i < FAKE_FDS; i++)
            fake_fds[i].open = false;
        return NULL;
    }
    return &fake_fds[fd];
}

int mcClose(int fd) {
    int ret = fake_begin(3);
    FakeFd *f;
    if (ret)
        return ret;
    fake_closes++;
    if ((f = fake_fd(fd))) {
        f->open = false;
        fake_result = 0;
    }
    return 0;
}

int mcSeek(int fd, int offset, int origin) {
    int ret = fake_begin(4);
    FakeFd *f;
    if (ret)
        return ret;
    if ((f = fake_fd(fd))) {
        int base = origin == 0 ? 0 : origin == 1 ? f->pos : (int)fake_fs[f->entry].size;
        f->pos = base + offset < 0 ? 0 : base + offset;
        fake_result = f->pos;
    }
    return 0;
}

int mcRead(int fd, void *buffer, int size) {
    int ret = fake_begin(5);
    FakeFd *f;
    if (ret)
        return ret;
    if ((f = fake_fd(fd))) {
        FakeEntry *e = &fake_fs[f->entry];
        int available = f->pos < (int)e->size ? (int)e->size - f->pos : 0;
        int count = size < available ? size : available;
        if (!(f->flags & 1)) {
            fake_result = sceMcResDeniedPermit;
            return 0;
        }
        memcpy(buffer, e->data + f->pos, (size_t)count);
        f->pos += count;
        fake_result = count;
    }
    return 0;
}

int mcWrite(int fd, const void *buffer, int size) {
    int ret = fake_begin(6);
    FakeFd *f;
    if (ret)
        return ret;
    if ((f = fake_fd(fd))) {
        FakeEntry *e = &fake_fs[f->entry];
        size_t end = (size_t)f->pos + (size_t)size;
        if (!(f->flags & 2)) {
            fake_result = sceMcResDeniedPermit;
            return 0;
        }
        if (end > e->size && fake_used() + (end - e->size) > fake_capacity) {
            fake_result = sceMcResFullDevice;
            return 0;
        }
        if (end > e->size) {
            e->data = realloc(e->data, end);
            memset(e->data + e->size, 0, end - e->size);
            e->size = end;
        }
        memcpy(e->data + f->pos, buffer, (size_t)size);
        f->pos += size;
        fake_result = size;
    }
    return 0;
}

int mcFlush(int fd) {
    int ret = fake_begin(0x0A);
    if (ret)
        return ret;
    if (fake_fd(fd))
        fake_result = 0;
    return 0;
}

int mcDelete(int port, int slot, const char *name) {
    int ret = fake_begin(0x0F), index;
    char parent[1024];

    if (ret)
        return ret;
    if (!fake_detect(port))
        return 0;
    index = fake_find(name);
    if (index < 0) {
        fake_result = sceMcResNoEntry;
        return 0;
    }
    /* No writable check: mcman skips it while PS1CardFlag is set (the default). */
    if (fake_fs[index].dir) {
        for (int i = 0; i < FAKE_ENTRIES; i++) {
            if (!fake_fs[i].used)
                continue;
            fake_parent(fake_fs[i].path, parent);
            if (!strcmp(parent, name)) {
                fake_result = sceMcResNotEmpty;
                return 0;
            }
        }
    }
    fake_free_entry(index);
    fake_result = 0;
    return 0;
}

int mcSetFileInfo(int port, int slot, const char *name, const sceMcTblGetDir *info, unsigned flags) {
    int ret = fake_begin(0x0E), index;

    if (ret)
        return ret;
    if (!fake_detect(port))
        return 0;
    index = fake_find(name);
    if (index < 0) {
        fake_result = sceMcResNoEntry;
        return 0;
    }
    if (flags & 0x10) {
        char target[1100], parent[1024];
        size_t old_length = strlen(name);
        fake_parent(name, parent);
        snprintf(target, sizeof(target), "%s/%s", strcmp(parent, "/") ? parent : "",
            (const char *)info->EntryName);
        if (fake_find(target) >= 0) {
            fake_result = sceMcResDeniedPermit;
            return 0;
        }
        /* The entry and everything below it. */
        for (int i = 0; i < FAKE_ENTRIES; i++) {
            char moved[2200];
            if (!fake_fs[i].used || strncmp(fake_fs[i].path, name, old_length) ||
                (fake_fs[i].path[old_length] && fake_fs[i].path[old_length] != '/'))
                continue;
            snprintf(moved, sizeof(moved), "%s%s", target, fake_fs[i].path + old_length);
            snprintf(fake_fs[i].path, sizeof(fake_fs[i].path), "%.1023s", moved);
        }
    }
    /* mcman with PS1CardFlag set copies every mode bit but these. */
    if (flags & 0x04)
        fake_fs[index].attr = (uint16_t)((fake_fs[index].attr & 0x8030) | (info->AttrFile & ~0x8030));
    if (flags & 0x01)
        fake_fs[index].created = info->_Create;
    if (flags & 0x02)
        fake_fs[index].modified = info->_Modify;
    fake_result = 0;
    return 0;
}

int mcRename(int port, int slot, const char *oldName, const char *newName) {
    sceMcTblGetDir info;
    memset(&info, 0, sizeof(info));
    snprintf((char *)info.EntryName, sizeof(info.EntryName), "%s", newName);
    return mcSetFileInfo(port, slot, oldName, &info, 0x10);
}

int mcGetEntSpace(int port, int slot, const char *path) {
    int ret = fake_begin(0x12);
    if (ret)
        return ret;
    if (fake_detect(port))
        fake_result = fake_is_dir(path) ? 13 : sceMcResNoEntry;
    return 0;
}

int mcFormat(int port, int slot) {
    int ret = fake_begin(0x10);
    if (ret)
        return ret;
    if (port != 0) {
        fake_result = -10;
        return 0;
    }
    for (int i = 0; i < FAKE_ENTRIES; i++)
        if (fake_fs[i].used)
            fake_free_entry(i);
    fake_formatted = true;
    fake_result = 0;
    return 0;
}

int mcUnformat(int port, int slot) {
    int ret = fake_begin(0x11);
    if (ret)
        return ret;
    fake_formatted = false;
    fake_result = 0;
    return 0;
}

int mcSync(int mode, int *cmd, int *result) {
    int ret;

    pthread_mutex_lock(&fake_mutex);
    if (!fake_pending) {
        ret = -1;
    } else if (mode == MC_NOWAIT && --fake_polls > 0) {
        ret = 0;
    } else {
        if (cmd)
            *cmd = fake_pending;
        if (result)
            *result = fake_result;
        fake_pending = 0;
        fake_in_command--;
        ret = 1;
    }
    pthread_mutex_unlock(&fake_mutex);
    return ret;
}

/* Empty formatted card, nothing open, nothing in flight. */
static void fake_reset(void) {
    for (int i = 0; i < FAKE_ENTRIES; i++)
        if (fake_fs[i].used)
            fake_free_entry(i);
    memset(fake_fds, 0, sizeof(fake_fds));
    fake_capacity = 1 << 20;
    fake_formatted = true;
    fake_swapped = false;
    fake_authed = false;
    fake_pending = 0;
}

#endif /* ATHENA_FAKE_LIBMC_H */
