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

<a id="wsl-com-docker-e-inspecao-do-toolchain"></a>

### WSL com Docker e inspeção do toolchain

No Windows, temos WSL disponível. Se o Podman não sustentar o build, use o
Docker Engine na distribuição Ubuntu. Abra um shell WSL e entre no checkout:

```powershell
wsl -d Ubuntu
```

Dentro do WSL:

```sh
cd /mnt/c/Users/User/ath
docker compose run --rm build
docker compose run --rm host-tests
docker compose run --rm js-tests
docker compose run --rm shell
```

O serviço `shell` monta o checkout em `/src` e dá acesso às dependências
instaladas na imagem. **Se precisar procurar uma definição nas dependências,
use esse shell para inspecionar ps2dev, PS2SDK, gsKit, OpenVCL/masp e outros**:

```sh
printenv PS2DEV PS2SDK
command -v mips64r5900el-ps2-elf-gcc
command -v openvcl
command -v masp
grep -R -n 'GS_SETREG_TEST' "$PS2DEV/gsKit/include"
grep -R -n 'dmaKit_wait' "$PS2DEV/gsKit/include"
grep -R -n 'SyncDCache' "$PS2SDK/ee/include"
find "$PS2DEV" -iname '*openvcl*' -o -iname '*masp*'
```

Prefira `rg` quando instalado; `grep` e `find` são o fallback no container.
Confirme os headers e ferramentas da imagem efetivamente usada antes de
adaptar chamadas do legado. Não é necessário instalar o SDK no Windows.

Para localizar um PC de erro do PS2/PCSX2, preserve símbolos no ELF da mesma
versão, seleção de módulos, runtime e flags do executável que falhou. Dentro
do shell do toolchain, por exemplo:

```sh
make RUNTIME=quickjs EE_BIN_PREF=athena_symbols EE_STRIP=true
PS2_CRASH_PC=0x15fadc # substituir pelo PC do log correspondente
mips64r5900el-ps2-elf-addr2line -i -f -C -e bin/athena_symbols.elf "$PS2_CRASH_PC"
```

Um ELF recompilado após mudanças pode ter endereços diferentes; mantenha a
cópia com símbolos de cada build que estiver validando.

Neste ambiente também há a imagem local `localhost/ps2swf-dev:m0`, com
ps2dev/PS2SDK e OpenVCL/masp. Ela foi usada para os builds iniciais da migração
3D. Para reutilizá-la, se ainda estiver disponível, a partir do WSL:

```sh
docker run --rm -it --entrypoint /bin/sh -v "$PWD:/src" -w /src localhost/ps2swf-dev:m0
# Dentro do container, após selecionar módulos no host:
make -j4 RUNTIME=quickjs EE_BIN_PREF=athena_3d_js
make -j4 RUNTIME=native APP_SRCS=samples/native/3d/main.c EE_BIN_PREF=athena_3d_native
sh tests/host/run.sh
```

A imagem local é uma alternativa disponível neste ambiente; o Dockerfile do
projeto continua definindo a imagem fixada para o fluxo padrão. Os testes
QuickJS precisam do userland **i386** de `Dockerfile.jstests`; o ambiente
64 bits do toolchain não o substitui. Na imagem local baseada em Alpine,
a biblioteca ASan de host apresentou erro de linkedição; por isso os testes
host usam UBSan, e a execução adicional com ASan ocorre na imagem i386.

Para configurar a seleção e usar os exemplos 3D, veja [3D.md](3D.md).

---

## Building Locally (Advanced)

If you prefer to compile on your host machine without Docker:

### Essential components
* [Personal Computer](https://en.wikipedia.org/wiki/Personal_computer)
* [ps2dev](https://github.com/ps2dev/ps2dev)
* [ps2-packer](https://github.com/ps2dev/ps2-packer)

### Optional components
* [OpenVCL](https://github.com/ps2dev/openvcl) and masp - compile the VU microprograms (`src/modules/*/vu1/*.vcl`) into the `.vsm` the build assembles. Both come with ps2dev and are in the Docker image; OpenVCL runs masp in place of GASP (`--gasp masp -g`, see `Makefile.const`). The generated `.vsm` files are committed, so a build without them works as long as no `.vcl` changed.

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
