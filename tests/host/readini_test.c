/* Host test of src/readini: line endings, quotes, keys without a value. */
#include <stdio.h>
#include <string.h>
#include <readini.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
    const char *text =
        "# AthenaEnv Configuration File\r\n"
        "default_script=tests/sound_test.js  \r\n"
        "\r\n"
        "   \n"
        "audsrv = true\n"
        "keyonly\n"
        "quoted = \"dir with spaces/a.js\"\n"
        "count = 42";
    FILE *f = fopen("/tmp/readini_test.ini", "wb"); fputs(text, f); fclose(f);

    IniReader ini;
    char value[256];
    bool flag = false;
    int number = 0, empties = 0, lines = 0;
    bool got_script = false, got_quoted = false;
    readini_open(&ini, "/tmp/readini_test.ini");
    while (readini_getline(&ini)) {
        lines++;
        if (readini_emptyline(&ini)) { empties++; continue; }
        memset(value, 'X', sizeof value);
        if (readini_string(&ini, "default_script", value)) {
            got_script = true;
            CHECK(!strcmp(value, "tests/sound_test.js"), "script '%s'", value);
        } else if (readini_string(&ini, "quoted", value)) {
            got_quoted = true;
            CHECK(!strcmp(value, "dir with spaces/a.js"), "quoted '%s'", value);
        }
        readini_bool(&ini, "audsrv", &flag);
        readini_int(&ini, "count", &number);
        CHECK(!readini_bool(&ini, "keyonly", &flag), "key without value matched");
    }
    readini_close(&ini);
    CHECK(got_script && got_quoted, "script %d quoted %d", got_script, got_quoted);
    CHECK(flag, "audsrv = true");
    CHECK(number == 42, "count %d (last line without newline)", number);
    CHECK(empties == 2, "empty lines %d", empties);
    CHECK(lines == 8, "lines %d", lines);
    printf("readini: %d failures\n", failures);
    return failures != 0;
}
