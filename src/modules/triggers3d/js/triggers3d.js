/*
 * Triggers3D: zones (boxes, spheres) that report bodies entering and
 * leaving them, tested in C (Triggers3DNative) each update(); JavaScript
 * only runs for the events. Zones and bodies can follow Scene3D nodes.
 */
import * as Native from "Triggers3DNative";

export const MAX = Native.MAX;
const ALL = 0xFFFFFFFF;

function checkFn(f, name) {
    if (f !== undefined && typeof f !== "function") throw new TypeError(`Triggers3D ${name} must be a function`);
    return f;
}

class Zone {
    constructor(world, id, options) {
        this._world = world; this._id = id;
        this.onEnter = checkFn(options.onEnter, "onEnter");
        this.onExit = checkFn(options.onExit, "onExit");
        this.data = options.data;
        this._enabled = true; this._mask = options.mask ?? ALL;
    }
    get id() { return this._id; }
    get enabled() { return this._enabled; }
    set enabled(on) { this._live(); this._enabled = !!on; this._world._native.setZoneEnabled(this._id, this._enabled); }
    get mask() { return this._mask; }
    set mask(m) { this._live(); this._world._native.setMask(this._id, m >>> 0); this._mask = m >>> 0; }
    setBox(minX, minY, minZ, maxX, maxY, maxZ) { this._live(); this._world._native.setZone(this._id, 0, minX, minY, minZ, maxX, maxY, maxZ); return this; }
    setSphere(x, y, z, radius) { this._live(); this._world._native.setZone(this._id, 1, x, y, z, radius, 0, 0); return this; }
    /** Moves with node (its world position of the last scene update) plus the offset; null stops. */
    follow(node, ox = 0, oy = 0, oz = 0) { this._live(); this._world._native.zoneFollow(this._id, node, ox, oy, oz); return this; }
    contains(body) { this._live(); return body instanceof Body && body._world === this._world && body._id >= 0 && this._world._native.inside(this._id, body._id); }
    occupants() { this._live(); return this._world._native.occupants(this._id).map(id => this._world._bodies[id]); }
    /** Removes the zone; onExit runs for the bodies inside. */
    dispose() {
        if (this._id < 0) return;
        const inside = this.occupants();
        this._world._native.removeZone(this._id);
        this._world._zones[this._id] = undefined;
        this._id = -1;
        for (const body of inside) this._world._exit(this, body);
    }
    _live() { if (this._id < 0) throw new TypeError("Triggers3D zone was disposed"); }
}

class Body {
    constructor(world, id, options) {
        this._world = world; this._id = id; this.data = options.data;
        this._layers = options.layers ?? 1; this._radius = options.radius ?? 0;
    }
    get id() { return this._id; }
    get layers() { return this._layers; }
    set layers(l) { this._live(); this._world._native.setLayers(this._id, l >>> 0); this._layers = l >>> 0; }
    get radius() { return this._radius; }
    /** Moves the body (and optionally changes its radius). */
    setPosition(x, y, z, radius = this._radius) { this._live(); this._world._native.setBody(this._id, x, y, z, radius); this._radius = radius; return this; }
    follow(node, ox = 0, oy = 0, oz = 0) { this._live(); this._world._native.bodyFollow(this._id, node, ox, oy, oz); return this; }
    /** Removes the body; onExit runs for the zones it was in. */
    dispose() {
        if (this._id < 0) return;
        const w = this._world, zones = [];
        for (const zone of w._zones) if (zone && w._native.inside(zone._id, this._id)) zones.push(zone);
        w._native.removeBody(this._id);
        w._bodies[this._id] = undefined;
        this._id = -1;
        for (const zone of zones) w._exit(zone, this);
    }
    _live() { if (this._id < 0) throw new TypeError("Triggers3D body was disposed"); }
}

export class World {
    constructor() {
        this._native = new Native.World();
        this._zones = []; this._bodies = [];
        this._events = new Int32Array(96);
        this._dispatchZones = []; this._dispatchBodies = [];
        this._updating = false;
        this.onEnter = undefined; this.onExit = undefined;   // world-wide callbacks (zone, body)
    }
    /** A box zone. options: mask (layers it reacts to, default all), onEnter(body, zone), onExit(body, zone), data. */
    box(minX, minY, minZ, maxX, maxY, maxZ, options = {}) {
        const zone = new Zone(this, -1, options);
        const id = this._native.addZone(0, minX, minY, minZ, maxX, maxY, maxZ, zone._mask >>> 0);
        zone._id = id;
        return (this._zones[id] = zone);
    }
    sphere(x, y, z, radius, options = {}) {
        const zone = new Zone(this, -1, options);
        const id = this._native.addZone(1, x, y, z, radius, 0, 0, zone._mask >>> 0);
        zone._id = id;
        return (this._zones[id] = zone);
    }
    /** A body: a sphere of options.radius (default 0, a point) on options.layers (default 1). */
    body(x, y, z, options = {}) {
        const body = new Body(this, -1, options);
        const id = this._native.addBody(x, y, z, body._radius, body._layers >>> 0);
        body._id = id;
        return (this._bodies[id] = body);
    }
    _enter(zone, body) {
        if (zone.onEnter) zone.onEnter(body, zone);
        if (this.onEnter) this.onEnter(zone, body);
    }
    _exit(zone, body) {
        if (zone.onExit) zone.onExit(body, zone);
        if (this.onExit) this.onExit(zone, body);
    }
    /** Tests every pair in C and runs the callbacks of the changes; returns the number of events. */
    update() {
        if (this._updating) throw new TypeError("Triggers3D.update cannot run recursively");
        this._updating = true;
        let n = 0;
        const zones = this._dispatchZones, bodies = this._dispatchBodies;
        try {
            n = this._native.update(this._events);
            if (n * 3 > this._events.length) {
                this._events = new Int32Array(n * 3 * 2);
                this._native.readEvents(this._events);
            }
            const e = this._events;
            // Capture identities before callbacks: a removed id may be reused
            // by a new zone/body during dispatch. Reuse the arrays each frame.
            for (let i = 0; i < n; i++) {
                zones[i] = this._zones[e[i * 3 + 1]];
                bodies[i] = this._bodies[e[i * 3 + 2]];
            }
            for (let i = 0; i < n; i++) {
                const zone = zones[i], body = bodies[i];
                if (!zone || !body || zone._id < 0 || body._id < 0) continue;
                if (e[i * 3] === Native.ENTER) this._enter(zone, body); else this._exit(zone, body);
            }
            return n;
        } finally {
            for (let i = 0; i < n; i++) { zones[i] = undefined; bodies[i] = undefined; }
            this._updating = false;
        }
    }
    zones() { return this._zones.filter(Boolean); }
    bodies() { return this._bodies.filter(Boolean); }
    dispose() {
        this._native.dispose();
        for (const zone of this._zones) if (zone) zone._id = -1;
        for (const body of this._bodies) if (body) body._id = -1;
        this._zones = []; this._bodies = [];
    }
}
