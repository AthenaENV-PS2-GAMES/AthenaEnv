.SILENT:
include Makefile.const

define HEADER

==================================================
           AthenaEnv - Minimal Core
==================================================

endef
export HEADER

EE_EXT = .elf

EE_BIN_PREF ?= athena
EE_BIN_PKD = $(EE_BIN_PREF)_pkd

DEBUG ?= 0
EE_SIO ?= 0

EE_LIBS = -L$(PS2SDK)/ports/lib -lmc -lpad -lpatches -lz -llzma -lzip -lfileXio -lelf-loader-nocolour -lerl -ldebug

EE_INCS += -I$(PS2SDK)/ports/include -I$(PS2SDK)/ports/include/zlib -Isrc/readini/include -Isrc/include

EE_CFLAGS += -Wall -fpermissive -DCONFIG_BIGNUM -DCONFIG_VERSION=\"$(shell cat VERSION 2>/dev/null || echo 2.0.0)\" -D__TM_GMTOFF=tm_gmtoff -DPATH_MAX=256 -DPS2

ifeq ($(DEBUG),1)
  EE_CFLAGS += -DDEBUG
endif

ifneq ($(EE_SIO), 0)
  EE_BIN_PREF := $(EE_BIN_PREF)_eesio
  EE_BIN_PKD := $(EE_BIN_PKD)_eesio
  EE_CFLAGS += -D__EESIO_PRINTF
  EE_LIBS += -lsiocookie
endif

JS_CORE = quickjs/cutils.o quickjs/libbf.o quickjs/libregexp.o quickjs/libunicode.o \
          quickjs/realpath.o quickjs/quickjs.o quickjs/quickjs-libc.o

-include Makefile.modules

# Modules may ship C sources and prebuilt VU microprograms (.vsm).
MODULE_OBJS = $(patsubst src/%.vsm,%.o,$(patsubst src/%.c,%.o,$(MODULE_SRCS)))
EE_LIBS += $(MODULE_LIBS)
EE_INCS += $(MODULE_INCS)

APP_CORE = main.o memory.o ee_tools.o module_system.o iop_manager.o strUtils.o system.o excepHandler.o exceptions.o sioprintf.o athena_math.o

INI_READER = readini/src/readini.o

JS_ENGINE = js_api/ath_env.o js_api/ath_gil.o

IOP_MODULES = iomanx.o filexio.o sio2man.o mcman.o mcserv.o padman.o \
              usbd.o bdm.o bdmfs_fatfs.o usbmass_bd.o cdfs.o \
              freeram.o poweroff.o

# IOP modules embedded for the selected modules (see embed_irx in module.json).
IOP_MODULES += $(MODULE_IRX:%=%.o)
MODULE_IRX_BUILD_DIRS = $(strip $(foreach irx,$(MODULE_IRX),$(MODULE_IRX_BUILD_$(irx))))

EMBEDDED_ELFS = loader_elf.o
EMBEDDED_FONTS = quicksand_regular.o

EE_OBJS = $(APP_CORE) $(INI_READER) $(JS_CORE) $(JS_ENGINE) $(MODULE_OBJS) $(IOP_MODULES) $(EMBEDDED_ELFS) $(EMBEDDED_FONTS)
EE_OBJS := $(EE_OBJS:%=$(EE_OBJ_DIR)%)

EE_BIN := $(EE_BIN_DIR)$(EE_BIN_PREF)$(EE_EXT)
EE_BIN_PKD := $(EE_BIN_DIR)$(EE_BIN_PKD)$(EE_EXT)

all: $(DIR_GUARD) $(EE_OBJS)
	$(EE_CXX) -T$(EE_LINKFILE) $(EE_OPTFLAGS) -o $(EE_BIN_DIR)tmp.elf $(EE_OBJS) $(EE_LDFLAGS) $(EXTRA_LDFLAGS) -Wno-write-strings $(EE_LIBS) $(EE_SRC_DIR)dummy-exports.c
	sh ./build-exports.sh
	$(EE_CXX) -T$(EE_LINKFILE) $(EE_OPTFLAGS) -o $(EE_BIN) $(EE_OBJS) $(EE_LDFLAGS) $(EXTRA_LDFLAGS) -fpermissive -Wno-write-strings $(EE_LIBS) $(EE_SRC_DIR)exports.c
	rm -f $(EE_BIN_DIR)tmp.elf
	@echo "$$HEADER"
	echo "Building $(EE_BIN)..."
	$(EE_STRIP) $(EE_BIN)
	ps2-packer $(EE_BIN) $(EE_BIN_PKD) > /dev/null 2>&1 || true

debug: EE_CFLAGS += -DDEBUG
debug: $(DIR_GUARD) $(EE_OBJS)
	$(EE_CXX) -T$(EE_LINKFILE) $(EE_OPTFLAGS) -o $(EE_BIN_DIR)tmp.elf $(EE_OBJS) $(EE_LDFLAGS) $(EXTRA_LDFLAGS) -Wno-write-strings $(EE_LIBS) $(EE_SRC_DIR)dummy-exports.c
	sh ./build-exports.sh
	$(EE_CXX) -T$(EE_LINKFILE) $(EE_OPTFLAGS) -o bin/athena_debug.elf $(EE_OBJS) $(EE_LDFLAGS) $(EXTRA_LDFLAGS) -fpermissive -Wno-write-strings $(EE_LIBS) $(EE_SRC_DIR)exports.c
	rm -f $(EE_BIN_DIR)tmp.elf
	echo "Building bin/athena_debug.elf with debug symbols..."

clean:
	echo Cleaning executables...
	rm -f bin/*.elf
	rm -rf $(EE_OBJ_DIR)
	rm -rf $(EE_EMBED_DIR)
	$(MAKE) -C ee_modules/loader clean
	$(foreach dir,$(MODULE_IRX_BUILD_DIRS),$(MAKE) -C $(dir) clean;)

rebuild: clean all

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
include Makefile.embed

$(EE_EMBED_DIR):
	@mkdir -p $@

$(EE_OBJ_DIR):
	@mkdir -p $@

$(EE_OBJ_DIR)%.o: $(EE_SRC_DIR)%.c | $(EE_OBJ_DIR)
	@echo CC - $<
	$(DIR_GUARD)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

$(EE_OBJ_DIR)%.o: $(EE_SRC_DIR)%.s | $(EE_OBJ_DIR)
	@echo AS - $<
	$(DIR_GUARD)
	$(EE_AS) $(EE_ASFLAGS) $(EE_INCS) $< -o $@

$(EE_OBJ_DIR)%.o: $(EE_SRC_DIR)%.S | $(EE_OBJ_DIR)
	@echo AS - $<
	$(DIR_GUARD)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

# Prebuilt VU microprograms; vcl is not part of the toolchain image.
$(EE_OBJ_DIR)%.o: $(EE_SRC_DIR)%.vsm | $(EE_OBJ_DIR)
	@echo DVP - $<
	$(DIR_GUARD)
	$(EE_DVP) $< -o $@

$(EE_OBJ_DIR)%.o: $(EE_EMBED_DIR)%.c | $(EE_OBJ_DIR)
	@echo BIN2C - $<
	$(DIR_GUARD)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
