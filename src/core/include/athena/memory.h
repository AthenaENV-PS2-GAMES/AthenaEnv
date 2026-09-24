#ifndef ATHENA_MEMORY_H
#define ATHENA_MEMORY_H

#include <stddef.h>


extern char __start;
extern char _end;

void init_memory_manager();

size_t get_binary_size();
size_t get_allocs_size();
size_t get_stack_size();
size_t get_used_memory();
#endif /* ATHENA_MEMORY_H */
