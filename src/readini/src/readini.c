#include <readini.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool readini_open(IniReader* ini, const char* fname) {
    ini->handle = fopen(fname, "r");
    return ini->handle;
}

bool readini_close(IniReader* ini) {
    return fclose(ini->handle);
}

/*
 * Reads the next line with control characters (\r, \n, \t...) and commas
 * turned into spaces, and without leading or trailing spaces, so values do
 * not carry the line ending ("tests/a.js\n" used to become "tests/a.js ").
 */
bool readini_getline(IniReader* ini) {
    char* start = ini->cur_line;
    size_t length;

    if( !fgets(ini->cur_line, LINE_BUFSIZE, ini->handle) ) {
        if(!feof(ini->handle)) {
            ini->cur_line[0] = '\0';
            return true;
        }
        return false;
    }

    for (char* c = ini->cur_line; *c; c++) {
        if ( *c < ' ' || *c == ',' )
            *c = ' ';
    }

    while (*start == ' ')
        start++;
    length = strlen(start);
    while (length > 0 && start[length - 1] == ' ')
        length--;

    memmove(ini->cur_line, start, length);
    ini->cur_line[length] = '\0';

    return true;
}

bool readini_comment(IniReader* ini, char* value_ptr) {
    if (ini->cur_line[0] == '#') {
        strcpy(value_ptr, ini->cur_line+1);
        return true;
    } else if (ini->cur_line[0] == '/' && ini->cur_line[1] == '/') {
        strcpy(value_ptr, ini->cur_line+2);
        return true;
    }

    return false;
}

/* readini_getline() leaves blank lines empty. */
bool readini_emptyline(IniReader* ini) {
    return ini->cur_line[0] == '\0';
}

/*
 * Splits "key = value" into `tmp_str`. Returns false when the line has no
 * value or the key is not `key`.
 */
static bool readini_match(IniReader* ini, const char* key, char* tmp_str, char** value_str) {
    char* key_str;

    strcpy(tmp_str, ini->cur_line);
    key_str = strtok(tmp_str, " ,\t=");
    *value_str = strtok(NULL, " ,\t=");

    return key_str && *value_str && !strcasecmp(key_str, key);
}

bool readini_bool(IniReader* ini, const char* key, bool* value_ptr) {
    char tmp_str[LINE_BUFSIZE];
    char* value_str;

    if (!readini_match(ini, key, tmp_str, &value_str))
        return false;

    if (!strcasecmp("false", value_str)) {
        *value_ptr = false;
        return true;
    }
    if (!strcasecmp("true", value_str)) {
        *value_ptr = true;
        return true;
    }

    return false;
}

bool readini_int(IniReader* ini, const char* key, int* value_ptr) {
    char tmp_str[LINE_BUFSIZE];
    char* value_str;

    if (!readini_match(ini, key, tmp_str, &value_str))
        return false;

    *value_ptr = atoi(value_str);
    return true;
}

bool readini_float(IniReader* ini, const char* key, float* value_ptr) {
    char tmp_str[LINE_BUFSIZE];
    char* value_str;

    if (!readini_match(ini, key, tmp_str, &value_str))
        return false;

    *value_ptr = strtof(value_str, NULL);
    return true;
}

/*
 * The value runs to the end of the line; a value in quotes ("a b" or 'a b')
 * keeps its spaces and stops at the closing quote.
 */
bool readini_string(IniReader* ini, const char* key, char* value_ptr) {
    char tmp_str[LINE_BUFSIZE];
    char* value_str;
    const char* value;

    if (!readini_match(ini, key, tmp_str, &value_str))
        return false;

    value = ini->cur_line + (value_str - tmp_str);
    if (*value == '"' || *value == '\'') {
        char quote = *value++;

        while (*value && *value != quote)
            *value_ptr++ = *value++;
        *value_ptr = '\0';
    } else {
        strcpy(value_ptr, value);
    }

    return true;
}
