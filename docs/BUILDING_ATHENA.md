# Building an AthenaEnv binary

AthenaEnv is normally distributed on Releases tab or GitHub artifacts for dev binaries, however, there's some relevance to mantain a build instruction manual for customization and preservation purposes.

## Building with Docker (Recommended)

The easiest and most reliable way to compile AthenaEnv is using Docker, as it eliminates the need to install or compile toolchains locally.

### Prerequisites
* [Docker Desktop](https://www.docker.com/) (Windows / macOS) or Docker Engine (Linux)

### Build command
Simply run the helper script or docker compose:

**On Linux / macOS:**
```shell
./docker-build.sh
# or
docker compose run --rm build
```

**On Windows:**
```shell
docker-build.bat
# or
docker compose run --rm build
```

The compiled binaries will be output to the `bin/` directory (`athena.elf` and `athena_pkd.elf`).

---

## Building Locally (Advanced)

If you prefer to compile on your host machine without Docker:

### Essential components
* [Personal Computer](https://en.wikipedia.org/wiki/Personal_computer)
* [ps2dev](https://github.com/ps2dev/ps2dev)
* [ps2-packer](https://github.com/ps2dev/ps2-packer)

### Optional components
* [Vector Unit Command Line](https://ps2linux.no-ip.info/playstation2-linux.com/projects/vcl.html) - P.S.: VCL is a 32bit binary and depends on [GASP](https://github.com/matrach/gasp). It is used to compile AthenaEnv VU1 microprograms. It can compile without VCL, but you can't edit AthenaEnv VU1 microprograms without it.

_P.S.: Install and usage instructions are inside their pages._

## Compiling locally
Once you have PS2DEV environment working on your computer, you can compile AthenaEnv.  
  
AthenaEnv is easily compilable with a single command ```make```. Two AthenaEnv binary variants will be generated at bin folder.  
* **athena.elf** - Athena _default_ binary, with all specified functions
* **athena_pkd.elf** - Athena binary _compressed_ variant. Proper to be used inside Memory Card or other low storage devices. 
  
In addition, you can just type ```make clean``` to remove compilation cache resources.

## Choosing modules

Every feature lives in a module under `src/modules/<id>/`, described by its `module.json`. Only the modules you select (plus the ones they depend on and the `required` ones) are compiled, linked and embedded, and the linker drops any code they do not reference (`--gc-sections`).

```shell
node tools/modules.js list                                  # what is available
node tools/modules.js configure --defaults                  # modules marked "default"
node tools/modules.js configure --modules=graphics,gamepad  # a custom selection
node tools/modules.js configure --all                       # everything
make clean all
```

`configure` writes `Makefile.modules`, `src/generated/*` (`athena_config.h`, `native_registry.c`, `js_registry.c`) and `bin/athena.d.ts`. Do not edit them by hand.

Boot devices are modules too: `memcard` (mc0:/mc1:), `usbmass` (mass:/) and `cdrom` (cdrom0:/cdfs:). The core only embeds the file I/O drivers (`iomanX`, `fileXio`), so a build meant to run from USB can drop `memcard` and `cdrom` and save about 96 KB of EE RAM. A build that lacks the driver of the device it is started from cannot read its `main.js`/`athena.ini`.

The `erl` module (loading `.erl` native modules at runtime) is not a default: it exports every symbol of the binary, which costs RAM and disables dead-code removal. Select it only if you load ERL modules.

## Runtimes

| Command | Output | Contents |
|---|---|---|
| `make` (`RUNTIME=quickjs`) | `bin/athena.elf` | QuickJS, runs `main.js` / `athena.ini` |
| `make RUNTIME=native APP_SRCS=my/app.c` | `bin/athena_native.elf` | No script engine; your C code implements `int athena_main(int argc, char **argv)` |
| `make lib RUNTIME=native` | `lib/libathena.a`, `lib/athena.mk` | Core + selected modules as a library, used from this tree |
| `make sdk RUNTIME=native` | `dist/athena-sdk.tar.gz` | Self-contained SDK: library, headers, `athena.mk`, samples |

The native runtime boots exactly like the JavaScript one (IOP reset, boot device, `athena.ini`, exception handlers, module `init` hooks) and then calls `athena_main()`. Include `<athena.h>` plus the headers of the modules you use (`<athena/graphics.h>`, `<athena/gamepad.h>`, `<athena/screen.h>`, …); `ATHENA_MODULE_<ID>` macros from `athena_config.h` tell which modules the build contains. See `samples/native/hello/main.c` and `samples/native/move/main.c` (screen, draw and gamepad from C).

Linking against the library from another project:

```make
ATHENA_ROOT = path/to/AthenaEnv
include $(ATHENA_ROOT)/lib/athena.mk
EE_BIN = hello.elf
EE_OBJS = main.o
EE_INCS = $(ATHENA_INCS)
EE_LIBS = -L$(ATHENA_ROOT)/lib -lathena $(ATHENA_LIBS)
EE_LDFLAGS = $(ATHENA_LDFLAGS)
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
```

The same builds are available on demand from the module picker on the site when a build server is configured; see [BUILD_SERVER.md](BUILD_SERVER.md).

## Customizing compilation
AthenaEnv has some flags and variables that can be changed when compiling as command line arguments.
* RUNTIME - `quickjs` or `native`. Default: quickjs
* APP_SRCS - Application sources for `RUNTIME=native`.
* EE_BIN_PREF - Binary name prefix. Default: athena (athena_native for the native runtime)
* DEBUG - Enable debug level logging; outputs `<prefix>_debug.elf`, not stripped. Default: 0
* EE_SIO - Redirect all print calls to EE Serial. Default: 0

Objects are kept per configuration in `obj/<runtime>[-debug][-eesio]/`, so switching options never mixes builds.

### Usage

Debug on EE serial with a small module set:
```shell
node tools/modules.js configure --modules=graphics,gamepad
make DEBUG=1 EE_SIO=1
```
