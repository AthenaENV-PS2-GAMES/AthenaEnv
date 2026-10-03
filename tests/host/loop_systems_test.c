/* Host test of the Loop systems registry: order, phases, real time, errors and changes while running. */
#include <stdio.h>
#include <string.h>

#include "loop_systems.c"

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct {
    const char *tag;
    int fail_phase;         /* phase that returns -1, or -1 */
    int remove_id;          /* system removed on the first call, or 0 */
    AthenaLoopSystemDesc add;   /* system added on the first call when add.func is set */
    int added_id;
    float last_value;
    int calls;
    int released;
} Probe;

static char trace[256];

static int probe_func(void *opaque, AthenaLoopPhase phase, float value) {
    Probe *probe = opaque;

    strcat(trace, probe->tag);
    probe->last_value = value;
    if (probe->calls++ == 0) {
        if (probe->remove_id)
            athena_loop_system_remove(probe->remove_id);
        if (probe->add.func)
            probe->added_id = athena_loop_system_add(&probe->add);
    }
    return (int)phase == probe->fail_phase ? -1 : 0;
}

static void probe_release(void *opaque) {
    ((Probe *)opaque)->released++;
}

static AthenaLoopSystemDesc desc_of(Probe *probe, const char *name, int priority, uint32_t phases) {
    AthenaLoopSystemDesc desc = {
        .name = name, .priority = priority, .phases = phases,
        .func = probe_func, .release = probe_release, .opaque = probe,
    };
    return desc;
}

static void run(AthenaLoopPhase phase, float value, float real_value) {
    trace[0] = '\0';
    athena_loop_systems_run(phase, value, real_value, NULL);
}

#define ALL_PHASES (ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_PHASE_COUNT) - 1)

int main(void) {
    Probe a = { .tag = "a", .fail_phase = -1 }, b = { .tag = "b", .fail_phase = -1 };
    Probe c = { .tag = "c", .fail_phase = -1 }, d = { .tag = "d", .fail_phase = -1 };
    AthenaLoopSystemDesc desc;
    int ids[8];
    int id_a, id_b, id_c, failed, result;

    /* Invalid descriptions. */
    desc = desc_of(&a, NULL, 0, 0);
    CHECK(athena_loop_system_add(&desc) == ATHENA_LOOP_SYSTEM_EINVAL, "no phases");
    desc = desc_of(&a, NULL, 0, ALL_PHASES);
    desc.func = NULL;
    CHECK(athena_loop_system_add(&desc) == ATHENA_LOOP_SYSTEM_EINVAL, "no func");
    CHECK(athena_loop_system_add(NULL) == ATHENA_LOOP_SYSTEM_EINVAL, "NULL desc");

    /* Order: priority, then insertion. */
    desc = desc_of(&a, "a", 10, ALL_PHASES);
    id_a = athena_loop_system_add(&desc);
    desc = desc_of(&b, "b", -5, ALL_PHASES);
    id_b = athena_loop_system_add(&desc);
    desc = desc_of(&c, NULL, 10, ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_POST_DRAW));
    id_c = athena_loop_system_add(&desc);
    CHECK(id_a > 0 && id_b > 0 && id_c > 0 && id_a != id_b && id_b != id_c, "ids %d %d %d", id_a, id_b, id_c);
    desc = desc_of(&d, "a", 0, ALL_PHASES);
    CHECK(athena_loop_system_add(&desc) == ATHENA_LOOP_SYSTEM_EEXIST, "duplicate name");

    CHECK(athena_loop_system_list(ids, 8) == 3, "count");
    CHECK(ids[0] == id_b && ids[1] == id_a && ids[2] == id_c, "list order");
    CHECK(athena_loop_system_find("a") == id_a && athena_loop_system_find("zz") == 0, "find");
    CHECK(athena_loop_system_get(id_b) && athena_loop_system_get(id_b)->priority == -5, "get");
    CHECK(strcmp(athena_loop_system_get(id_a)->name, "a") == 0, "name copied");

    run(ATHENA_LOOP_UPDATE, 0.5f, 1.0f);
    CHECK(strcmp(trace, "ba") == 0, "update trace '%s'", trace);
    run(ATHENA_LOOP_POST_DRAW, 0.25f, 0.25f);
    CHECK(strcmp(trace, "bac") == 0, "post draw trace '%s'", trace);
    CHECK(c.last_value == 0.25f, "alpha %f", c.last_value);

    /* Real time applies to PRE/POST_UPDATE only. */
    athena_loop_system_remove(id_c);
    CHECK(c.released == 1, "released on remove");
    CHECK(!athena_loop_system_remove(id_c), "removed twice");
    desc = desc_of(&c, "real", 0, ALL_PHASES);
    desc.real_time = true;
    id_c = athena_loop_system_add(&desc);
    run(ATHENA_LOOP_PRE_UPDATE, 0.0f, 0.016f);
    CHECK(c.last_value == 0.016f && a.last_value == 0.0f, "real %f scaled %f", c.last_value, a.last_value);
    run(ATHENA_LOOP_UPDATE, 0.0f, 0.016f);
    CHECK(c.last_value == 0.0f, "update ignores real time: %f", c.last_value);

    /* A failure stops the phase and reports the system. */
    a.fail_phase = ATHENA_LOOP_POST_UPDATE;
    trace[0] = '\0';
    result = athena_loop_systems_run(ATHENA_LOOP_POST_UPDATE, 0.1f, 0.1f, &failed);
    CHECK(result == -1 && failed == id_a, "failure %d id %d", result, failed);
    CHECK(strcmp(trace, "bca") == 0, "stopped after the failure: '%s'", trace);
    a.fail_phase = -1;

    /* Removing a later system while running: skipped now, released after the phase. */
    athena_loop_systems_clear();
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b)); memset(&c, 0, sizeof(c));
    a.tag = "a"; b.tag = "b"; c.tag = "c";
    a.fail_phase = b.fail_phase = c.fail_phase = -1;
    desc = desc_of(&a, NULL, 0, ALL_PHASES);
    id_a = athena_loop_system_add(&desc);
    desc = desc_of(&b, NULL, 1, ALL_PHASES);
    id_b = athena_loop_system_add(&desc);
    a.remove_id = id_b;
    b.remove_id = 0;
    run(ATHENA_LOOP_UPDATE, 0, 0);
    CHECK(strcmp(trace, "a") == 0, "removed system skipped: '%s'", trace);
    CHECK(b.released == 1, "release deferred to the end of the phase");

    /* Removing itself while running. */
    a.calls = 0;
    a.remove_id = id_a;
    run(ATHENA_LOOP_UPDATE, 0, 0);
    CHECK(strcmp(trace, "a") == 0 && a.released == 1, "self removal '%s' %d", trace, a.released);
    CHECK(athena_loop_system_list(ids, 8) == 0, "empty");

    /* Adding while running: starts with the next phase. */
    memset(&c, 0, sizeof(c));
    c.tag = "c"; c.fail_phase = -1;
    memset(&d, 0, sizeof(d));
    d.tag = "d"; d.fail_phase = -1;
    c.add = desc_of(&d, "late", -100, ALL_PHASES);
    desc = desc_of(&c, NULL, 0, ALL_PHASES);
    id_c = athena_loop_system_add(&desc);
    run(ATHENA_LOOP_UPDATE, 0, 0);
    CHECK(strcmp(trace, "c") == 0 && c.added_id > 0, "added system waits: '%s'", trace);
    run(ATHENA_LOOP_UPDATE, 0, 0);
    CHECK(strcmp(trace, "dc") == 0, "added system runs next phase by priority: '%s'", trace);

    /* Clear releases everything. */
    athena_loop_systems_clear();
    CHECK(c.released == 1 && d.released == 1, "clear releases %d %d", c.released, d.released);
    CHECK(athena_loop_system_list(ids, 8) == 0, "clear empties");
    CHECK(athena_loop_systems_run(ATHENA_LOOP_UPDATE, 0, 0, &failed) == 0 && failed == 0, "empty run");

    if (failures) {
        printf("loop_systems_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("loop_systems_test: all checks passed\n");
    return 0;
}
