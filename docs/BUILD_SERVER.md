# Build server

`tools/build-server/server.mjs` builds AthenaEnv for a module selection. The module picker on the site (`public/index.html`) calls it when `<meta name="athena-build-api">` holds its URL. With that meta tag empty, the site only shows the equivalent local commands.

## Running it

The server builds inside `ATHENA_ROOT` and rewrites `Makefile.modules` and `src/generated/` there, so give it a dedicated clone:

```shell
git clone https://github.com/GibranKhalil/AthenaEnv ../athena-build
ATHENA_CHECKOUT=../athena-build ALLOWED_ORIGIN=https://<user>.github.io docker compose up build-server
```

`Dockerfile.server` is the toolchain image plus Node.js and git. Without Docker, run `node tools/build-server/server.mjs` from a clone on a machine with the PS2 toolchain.

| Variable | Default | |
|---|---|---|
| `PORT` | 8080 | |
| `ATHENA_ROOT` | repository root | checkout used for builds |
| `CACHE_DIR` | `<ATHENA_ROOT>/.build-cache` | finished builds (status, log, artifacts) |
| `ALLOWED_ORIGIN` | `*` | CORS origin of the site |
| `MAX_QUEUE` | 16 | pending builds before answering 503 |
| `BUILD_TIMEOUT` | 900 | seconds before a build is killed |

The build key is the commit, the runtime and the resolved module list. Identical requests share one result, and a module list that resolves to the same set (for example `draw` versus `draw,graphics,color`) reuses it too. After updating the checkout (`git pull`), restart the server so new builds use the new commit.

Builds run one at a time. Incremental compilation (`obj/<runtime>/`) keeps a build of a new selection to roughly the files that include `<athena/config.h>` plus linking.

## API

TypeScript types for every request and response are in [`tools/athena-api.d.ts`](../tools/athena-api.d.ts), published with the site as `/athena-api.d.ts`. Front ends (the picker in `public/`, an external React site) should use them instead of redeclaring the shapes.

**Versioning:** `catalog.json`, `GET /api/catalog` and every build job carry `schemaVersion` (currently `1`). It is bumped only on incompatible changes, such as a removed or renamed field or a changed meaning; new optional fields do not bump it. Clients should refuse, or degrade to local-build instructions, when they see a version they do not know.

```http
POST /api/builds
Content-Type: application/json

{ "modules": ["gamepad", "font"], "runtime": "quickjs" }
```

`runtime` is `quickjs` (artifacts: `athena.elf`, `athena_pkd.elf`, `athena.d.ts`) or `native` (artifact: `athena-sdk.tar.gz`, see below). The answer is `202` for a queued build or `200` for a cached one:

```json
{
  "schemaVersion": 1,
  "id": "<sha256>",
  "status": "queued | building | done | failed",
  "position": 1,
  "runtime": "quickjs",
  "modules": ["color", "font", "gamepad", "graphics", "system"],
  "requested": ["font", "gamepad"],
  "commit": "…",
  "created": "2026-09-24T12:00:00.000Z",
  "finished": null,
  "error": null,
  "artifacts": [{ "name": "athena.elf", "url": "/api/builds/<id>/athena.elf" }],
  "log": "/api/builds/<id>/build.log"
}
```

- `GET /api/builds/<id>`: status (poll it until `done` or `failed`).
- `GET /api/builds/<id>/<artifact>`: download an artifact, or `build.log`.
- `GET /api/catalog`: the same document as `catalog.json`, generated from the server's checkout, plus `commit`, `runtimes` and `artifacts` (artifact names per runtime). Use it, rather than the `catalog.json` on GitHub Pages, to decide which modules can be requested: the server may be on a different commit.

Unknown module ids, a malformed body and a full queue answer `400` and `503` with `{ "error": "…" }`. Module ids are checked against `src/modules/*/module.json` and passed to child processes as arguments, never through a shell.

## Native SDK

`make sdk RUNTIME=native` (what the server runs for `native`) produces `dist/athena-sdk.tar.gz`:

```
athena-sdk/
  lib/libathena.a       core + selected modules + native runtime (main → athena_main)
  include/              <athena.h>, <athena/*.h> of the core and of each selected module, <athena/config.h>
  athena.mk             ATHENA_INCS / ATHENA_LIBS / ATHENA_LDFLAGS, relative to ATHENA_SDK
  samples/              the native samples
```

```make
ATHENA_SDK = path/to/athena-sdk
include $(ATHENA_SDK)/athena.mk
EE_BIN = game.elf
EE_OBJS = main.o
EE_INCS = $(ATHENA_INCS)
EE_LIBS = $(ATHENA_LIBS)
EE_LDFLAGS = $(ATHENA_LDFLAGS)
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
```
