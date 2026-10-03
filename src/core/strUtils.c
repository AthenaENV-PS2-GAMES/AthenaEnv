
#include <athena/str_utils.h>

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <assert.h>
#include <stdarg.h>

char* strpre(const char *pre, const char *str)
{
    if (strncmp(pre, str, strlen(pre)) == 0)
        return (char*)(str + strlen(pre));
    return NULL;
}

char* s_sprintf(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    // Determine o tamanho necess�rio da string
    int size = vsnprintf(NULL, 0, format, args);
    va_end(args);

    // Aloque mem�ria para a string
    char* str = (char*)malloc(size + 1);
    if (!str) {
        return NULL;
    }

    // Formate a string
    va_start(args, format);
    vsnprintf(str, size + 1, format, args);
    va_end(args);

    return str;
}

char** str_split(char* a_str, const char a_delim)
{
    char** result    = 0;
    size_t count     = 0;
    char* tmp        = a_str;
    char* last_comma = 0;
    char delim[2];
    delim[0] = a_delim;
    delim[1] = 0;

    /* Count how many elements will be extracted. */
    while (*tmp)
    {
        if (a_delim == *tmp)
        {
            count++;
            last_comma = tmp;
        }
        tmp++;
    }

    /* Add space for trailing token. */
    count += last_comma < (a_str + strlen(a_str) - 1);

    /* Add space for terminating null string so caller
       knows where the list of returned strings ends. */
    count++;

    result = malloc(sizeof(char*) * count);

    if (result)
    {
        size_t idx  = 0;
        char* token = strtok(a_str, delim);

        while (token)
        {
            assert(idx < count);
            *(result + idx++) = strdup(token);
            token = strtok(0, delim);
        }
        assert(idx == count - 1);
        *(result + idx) = 0;
    }

    return result;
}

int count_nonascii(const char *str) {
    int count = 0;

    while (*str) {
        if ((*str & ~0x7f)) {
            count++;
        }
        str++;
    }

    return count/2;
}

int count_spaces(const char *str, const char *chars) {
    int count = 0;

    while (*chars) {
        const char *tmp_str = str;

        while (*tmp_str) {
            if (*tmp_str == *chars) {
                count++;
            }
            tmp_str++;
        }

        chars++;
    }

    return count;
}

