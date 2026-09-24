/**
 * AthenaEnv catalog and build API types.
 *
 * Published with the site as /athena-api.d.ts (GitHub Pages). Describes:
 *   - catalog.json (GitHub Pages)
 *   - the build server (tools/build-server/server.mjs): GET /api/catalog,
 *     POST /api/builds, GET /api/builds/:id
 *
 * Keep in sync with tools/modules.js (buildCatalog) and the server. Breaking
 * changes bump `schemaVersion`.
 */

export type AthenaSchemaVersion = 1;

export type AthenaRuntime = "quickjs" | "native";

export interface AthenaModuleDependencies {
    /** Other module ids, pulled into the build automatically. */
    modules: string[];
    /** IOP (co-processor) drivers the module embeds. Informational. */
    iop: string[];
    /** Linker libraries. Informational. */
    ee_libs: string[];
}

export interface AthenaCatalogModule {
    /** Stable identifier, e.g. "gamepad". */
    id: string;
    name: string;
    description: string;
    /** "Core" | "Graphics" | "Input" | "Storage" | "Math" | "System" today; treat as open-ended. */
    category: string;
    version: string;
    /** Always part of a build; cannot be deselected. */
    required: boolean;
    /** Part of the default selection. */
    default: boolean;
    dependencies: AthenaModuleDependencies;
    /** JavaScript global installed by the module, e.g. "Gamepad". */
    global_alias: string | null;
    /** Which APIs the module offers: C (`#include <athena/<id>.h>`) and/or JavaScript. */
    api: { native: boolean; quickjs: boolean };
}

/** catalog.json on GitHub Pages. */
export interface AthenaCatalog {
    schemaVersion: AthenaSchemaVersion;
    name: string;
    version: string;
    /** ISO 8601. */
    generatedAt: string;
    modules: AthenaCatalogModule[];
}

/**
 * GET /api/catalog: the same shape as catalog.json, for the commit the
 * server builds. Use it, not catalog.json, to decide what can be built.
 */
export interface AthenaServerCatalog extends AthenaCatalog {
    /** Git commit of the server's checkout. */
    commit: string;
    runtimes: AthenaRuntime[];
    /** Artifact names each runtime produces. */
    artifacts: Record<AthenaRuntime, string[]>;
}

/** POST /api/builds body. Send the ids the user selected; the server resolves dependencies. */
export interface AthenaBuildRequest {
    modules: string[];
    runtime?: AthenaRuntime; // default "quickjs"
}

export type AthenaBuildStatus = "queued" | "building" | "done" | "failed";

export interface AthenaBuildArtifact {
    name: string;
    /** Relative to the build server base URL. */
    url: string;
}

/**
 * POST /api/builds (202 queued, 200 cached) and GET /api/builds/:id (200).
 * Poll while status is "queued" or "building".
 */
export interface AthenaBuildJob {
    schemaVersion: AthenaSchemaVersion;
    /** sha256 of commit + runtime + resolved modules; identical requests share it. */
    id: string;
    status: AthenaBuildStatus;
    /** 1-based place in the queue while "queued", otherwise 0. */
    position: number;
    runtime: AthenaRuntime;
    /** Resolved module list that is (or was) built. */
    modules: string[];
    /** Ids as requested. */
    requested: string[];
    commit: string;
    /** ISO 8601. */
    created: string;
    /** ISO 8601, set once "done" or "failed". */
    finished: string | null;
    error: string | null;
    artifacts: AthenaBuildArtifact[];
    /** Relative URL of the build log. */
    log: string;
}

/** Error body of 400 (bad request), 404 (unknown build/artifact) and 503 (queue full). */
export interface AthenaApiError {
    error: string;
}
