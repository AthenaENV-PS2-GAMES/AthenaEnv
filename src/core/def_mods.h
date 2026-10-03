#ifndef DEF_MODS_H
#define DEF_MODS_H

#include <sifrpc.h>
#include <loadfile.h>
#include <libmc.h>
#include <iopheap.h>
#include <iopcontrol.h>
#include <smod.h>
#include <sbv_patches.h>
#include <smem.h>
#include <libpwroff.h>

#include <athena/iop_manager.h>

extern bool HDD_USABLE;

iopman_define_module(iomanX);
iopman_define_module(fileXio);

void register_iop_modules();
char *get_boot_device(const char* path);
char *get_block_device(const char* path);
int load_default_module(int id);
bool wait_device(char *path);
void prepare_IOP();

#endif
