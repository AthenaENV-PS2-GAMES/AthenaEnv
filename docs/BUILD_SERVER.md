# Build server

The module picker on the project site (`public/index.html`) lists every module
from `catalog.json`. With a build server configured, it can also build a binary
for the selected modules on demand. `tools/build-server/server.mjs` is that
server: a dependency-free Node.js program that runs `tools/modules.js configure`
and `make` for each request.

## Running it

The server builds inside its checkout, rewriting `Makefile.modules` and
`src/generated/` there, so give it a **dedicated clone**, never a working copy:

```shell
git clone https://github.com/GibranKhalil/AthenaEnv ../athena-build
ATHENA_CHECKOUT=../athena-build docker compose up build-server
```

The `build-server` service uses `Dockerfile.server` (the toolchain image plus
Node.js and git), mounts the clone at `/athena`, keeps results in the
`build-cache` volume and listens on port 8080.

The server builds the commit its clone is on when it starts. To serve a newer
version, update the clone and restart the service.

### Environment

| Variable | Default | Meaning |
|---|---|---|
| `PORT` | `8080` | Listen port. |
| `ATHENA_ROOT` | repository root (`/athena` in Docker) | AthenaEnv checkout used for builds. |
| `CACHE_DIR` | `<ATHENA_ROOT>/.build-cache` (`/cache` in Docker) | Where build results are kept. |
| `ALLOWED_ORIGIN` | `*` | CORS origin allowed to call the API; set it to the site's origin. |
| `MAX_QUEUE` | `16` | Pending builds accepted before answering 503. |
| `BUILD_TIMEOUT` | `900` | Seconds before a build is killed. |

## Connecting the site

Set the server's base URL in `public/index.html`:

```html
<meta name="athena-build-api" content="https://build.example.com">
```

With an empty value, the picker only shows the commands to build locally.

## API

| Request | Response |
|---|---|
| `POST /api/builds` with `{ "modules": ["gamepad", "font"], "runtime": "quickjs" }` | A build job. `runtime` is `quickjs` or `native`. |
| `GET /api/builds/:id` | Status, resolved modules and artifacts of a job. |
| `GET /api/builds/:id/:file` | Downloads an artifact, or `build.log`. |
| `GET /api/catalog` | The modules this server can build (`catalog.json` plus the commit). |

Response types are declared in `tools/athena-api.d.ts`, published on the site
as `/athena-api.d.ts`.

Artifacts:

- `quickjs`: `athena.elf`, `athena_pkd.elf` and `athena.d.ts`;
- `native`: `athena-sdk.tar.gz`, the C library and headers.

Builds run one at a time. Identical requests (same commit, runtime and resolved
modules) share one cached result.
