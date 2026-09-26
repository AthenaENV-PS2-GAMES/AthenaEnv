#include <stdlib.h>
#include <string.h>

#include <athena/loop.h>

/*
 * Systems are kept in run order: ascending priority, then insertion order.
 * A phase runs over a snapshot of that array, so systems added or removed by
 * a system take effect with the next phase. Removed entries are freed once no
 * phase is running, since a snapshot may still point at them.
 */
typedef struct LoopSystem {
    AthenaLoopSystemDesc desc;
    int id;
    bool removed;
    struct LoopSystem *next_free;   /* removed while a phase ran */
} LoopSystem;

static LoopSystem **systems;
static int system_count;
static int system_capacity;
static int next_id = 1;

static int running;                 /* nesting depth of athena_loop_systems_run() */
static LoopSystem *pending_free;

static LoopSystem **snapshot;
static int snapshot_capacity;

static void system_free(LoopSystem *system) {
    if (system->desc.release)
        system->desc.release(system->desc.opaque);
    free((char *)system->desc.name);
    free(system);
}

static void free_pending(void) {
    while (pending_free) {
        LoopSystem *system = pending_free;
        pending_free = system->next_free;
        system_free(system);
    }
}

static int system_index(int id) {
    for (int i = 0; i < system_count; i++) {
        if (systems[i]->id == id)
            return i;
    }
    return -1;
}

int athena_loop_system_add(const AthenaLoopSystemDesc *desc) {
    LoopSystem *system;
    int at;

    if (!desc || !desc->func || !(desc->phases & (ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_PHASE_COUNT) - 1)))
        return ATHENA_LOOP_SYSTEM_EINVAL;
    if (desc->name && athena_loop_system_find(desc->name))
        return ATHENA_LOOP_SYSTEM_EEXIST;

    if (system_count == system_capacity) {
        int capacity = system_capacity ? system_capacity * 2 : 8;
        LoopSystem **grown = realloc(systems, capacity * sizeof(*grown));
        if (!grown)
            return ATHENA_LOOP_SYSTEM_ENOMEM;
        systems = grown;
        system_capacity = capacity;
    }

    system = calloc(1, sizeof(*system));
    if (!system)
        return ATHENA_LOOP_SYSTEM_ENOMEM;
    system->desc = *desc;
    if (desc->name) {
        system->desc.name = strdup(desc->name);
        if (!system->desc.name) {
            free(system);
            return ATHENA_LOOP_SYSTEM_ENOMEM;
        }
    }
    system->id = next_id++;

    /* After every system of the same or a lower priority. */
    at = system_count;
    while (at > 0 && systems[at - 1]->desc.priority > desc->priority)
        at--;
    memmove(&systems[at + 1], &systems[at], (system_count - at) * sizeof(*systems));
    systems[at] = system;
    system_count++;
    return system->id;
}

bool athena_loop_system_remove(int id) {
    int index = system_index(id);
    LoopSystem *system;

    if (index < 0)
        return false;
    system = systems[index];
    memmove(&systems[index], &systems[index + 1],
        (system_count - index - 1) * sizeof(*systems));
    system_count--;

    system->removed = true;
    if (running) {
        system->next_free = pending_free;
        pending_free = system;
    } else {
        system_free(system);
    }
    return true;
}

int athena_loop_system_find(const char *name) {
    if (!name)
        return 0;
    for (int i = 0; i < system_count; i++) {
        if (systems[i]->desc.name && strcmp(systems[i]->desc.name, name) == 0)
            return systems[i]->id;
    }
    return 0;
}

int athena_loop_system_list(int *ids, int max) {
    for (int i = 0; i < system_count && i < max; i++)
        ids[i] = systems[i]->id;
    return system_count;
}

const AthenaLoopSystemDesc *athena_loop_system_get(int id) {
    int index = system_index(id);
    return index < 0 ? NULL : &systems[index]->desc;
}

int athena_loop_systems_run(AthenaLoopPhase phase, float value, float real_value,
    int *failed_id) {
    uint32_t bit = ATHENA_LOOP_PHASE_BIT(phase);
    bool use_real = phase == ATHENA_LOOP_PRE_UPDATE || phase == ATHENA_LOOP_POST_UPDATE;
    LoopSystem **list = snapshot;
    int count = system_count;
    int result = 0;

    if (failed_id)
        *failed_id = 0;
    if (count == 0 || (int)phase < 0 || (int)phase >= ATHENA_LOOP_PHASE_COUNT)
        return 0;

    /* A nested run (a system running phases itself) gets its own snapshot. */
    if (running) {
        list = malloc(count * sizeof(*list));
        if (!list)
            return ATHENA_LOOP_SYSTEM_ENOMEM;
    } else if (count > snapshot_capacity) {
        LoopSystem **grown = realloc(snapshot, count * sizeof(*grown));
        if (!grown)
            return ATHENA_LOOP_SYSTEM_ENOMEM;
        snapshot = list = grown;
        snapshot_capacity = count;
    }
    memcpy(list, systems, count * sizeof(*list));

    running++;
    for (int i = 0; i < count; i++) {
        LoopSystem *system = list[i];
        if (system->removed || !(system->desc.phases & bit))
            continue;
        result = system->desc.func(system->desc.opaque, phase,
            use_real && system->desc.real_time ? real_value : value);
        if (result < 0) {
            if (failed_id)
                *failed_id = system->id;
            break;
        }
        result = 0;
    }
    running--;

    if (list != snapshot)
        free(list);
    if (!running)
        free_pending();
    return result;
}

void athena_loop_systems_clear(void) {
    while (system_count > 0)
        athena_loop_system_remove(systems[system_count - 1]->id);
    if (!running) {
        free(systems);
        systems = NULL;
        system_capacity = 0;
        free(snapshot);
        snapshot = NULL;
        snapshot_capacity = 0;
    }
}
