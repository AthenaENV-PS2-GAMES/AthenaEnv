import * as Model3D from 'Model3D';
import * as Render3D from 'Render3D';
export const SHADOW_BLEND_DARKEN = 0;
export const SHADOW_BLEND_ALPHA = 1;
export const SHADOW_BLEND_ADD = 2;

function finite(v, name) {
    if (typeof v !== 'number' || !Number.isFinite(v)) throw new TypeError(`${name} must be finite`);
    return v;
}
function vec(v, n, name) {
    const a = Array.isArray(v) || v instanceof Float32Array ? Array.from(v) :
        n === 4 ? [v.x, v.y, v.z, v.w] : [v.x, v.y, v.z];
    if (a.length !== n) throw new RangeError(`${name} needs ${n} components`);
    a.forEach(x => finite(x, name));
    return a;
}
function same(a, b) {
    if (!a || a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
    return true;
}
/** A static or captured texture decal. Draw after opaque receivers, before translucent objects.
 * Rays travel along lightDir; misses and excessive slopes omit whole triangles.
 * Geometry is cached until it changes; terrain queries run on every render. */
export class Projector {
    constructor(texture) {
        this._dead = false;
        this._ownedTexture = texture === undefined;
        if (texture === undefined) {
            const pixels = new Uint32Array(32 * 32);
            for (let y = 0; y < 32; y++) for (let x = 0; x < 32; x++) {
                const r = Math.hypot((x + .5 - 16) / 16, (y + .5 - 16) / 16);
                pixels[y * 32 + x] = (Math.round(255 * Math.max(0, 1 - r) ** 2) << 24 | 0xffffff) >>> 0;
            }
            texture = Model3D.Texture.fromPixels({width: 32, height: 32, pixels, filter: 1});
        }
        if (!(texture instanceof Model3D.Texture)) throw new TypeError('Projector expects Model3D.Texture');
        this._texture = texture;
        this._position = [0, 0, 0]; this._rotation = [0, 0, 0, 1]; this._scale = [1, 1, 1];
        this._matrix = null;
        this._width = this._height = 1; this._gx = this._gz = 2;
        this._light = [0, -1, 0]; this._bias = .01; this._offset = 0; this._slope = -1;
        this._color = [0, 0, 0, .75]; this._blend = SHADOW_BLEND_DARKEN;
        this._uv = [0, 0, 1, 1]; this._world = null; this._length = 2;
        this._mesh = this._instance = this._snapshot = null;
        this._origin = new Float32Array(3); this._hit = {};
    }
    _live() { if (this._dead) throw new TypeError('Projector is disposed'); }
    _invalidate() { this._snapshot = null; }
    get position() { return {x: this._position[0], y: this._position[1], z: this._position[2]}; }
    set position(v) { this._live(); this._position = vec(v, 3, 'position'); this._matrix = null; }
    get rotation() { return {x: this._rotation[0], y: this._rotation[1], z: this._rotation[2], w: this._rotation[3]}; }
    set rotation(v) {
        this._live(); const q = vec(v, 4, 'rotation'); const n = Math.hypot(...q);
        if (!n || !Number.isFinite(n)) throw new RangeError('rotation must be a nonzero quaternion');
        this._rotation = q.map(x => x / n); this._matrix = null;
    }
    get scale() { return {x: this._scale[0], y: this._scale[1], z: this._scale[2]}; }
    set scale(v) { this._live(); this._scale = vec(v, 3, 'scale'); this._matrix = null; }
    setTransform(matrix) {
        this._live(); const a = matrix.toArray();
        if (a.length !== 16) throw new RangeError('transform needs 16 components');
        a.forEach(v => finite(v, 'transform'));
        if (a[3] || a[7] || a[11] || a[15] !== 1) throw new RangeError('transform must be affine');
        this._matrix = a; return this;
    }
    setSize(width, height) {
        this._live(); finite(width, 'width'); finite(height, 'height');
        if (width <= 0 || height <= 0) throw new RangeError('size must be positive');
        this._width = width; this._height = height; return this;
    }
    setGrid(x, z) {
        this._live();
        if (!Number.isInteger(x) || !Number.isInteger(z) || x < 2 || z < 2 || x > 64 || z > 64)
            throw new RangeError('grid dimensions must be integers in 2..64');
        this._gx = x; this._gz = z; return this;
    }
    setLightDir(x, y, z) {
        this._live(); const d = [x, y, z]; d.forEach(v => finite(v, 'lightDir'));
        const n = Math.hypot(...d);
        if (!n || !Number.isFinite(n)) throw new RangeError('lightDir must be nonzero');
        this._light = d.map(v => v / n); return this;
    }
    setBias(v) { this._live(); this._bias = finite(v, 'bias'); return this; }
    setLightOffset(v) { this._live(); this._offset = finite(v, 'lightOffset'); return this; }
    setSlopeLimit(v) {
        this._live(); finite(v, 'slope');
        if (v < -1 || v > 1) throw new RangeError('slope cosine must be in [-1,1]');
        this._slope = v; return this;
    }
    setColor(r, g, b, a) {
        this._live(); const c = [r, g, b, a]; c.forEach(v => finite(v, 'color'));
        if (c.some(v => v < 0 || v > 1)) throw new RangeError('color must be in [0,1]');
        this._color = c; this._invalidate(); return this;
    }
    setBlend(mode) {
        this._live();
        if (![0, 1, 2].includes(mode)) throw new RangeError('invalid shadow blend mode');
        this._blend = mode; return this;
    }
    setUVRect(u0, v0, u1, v1) {
        this._live(); const uv = [u0, v0, u1, v1]; uv.forEach(v => finite(v, 'UV'));
        if (uv.some(v => Math.abs(v) > Model3D.UV_LIMIT)) throw new RangeError('UV exceeds Model3D.UV_LIMIT');
        this._uv = uv; this._invalidate(); return this;
    }
    enableRaycast(world, rayLength = 2, enable = true) {
        this._live(); finite(rayLength, 'rayLength');
        if (rayLength <= 0) throw new RangeError('rayLength must be positive');
        if (enable && (!world || typeof world.raycast !== 'function')) throw new TypeError('expected Collision3D.World');
        this._world = enable ? world : null; this._length = rayLength; return this;
    }
    _transform() {
        if (this._matrix) return this._matrix;
        const [x, y, z, w] = this._rotation, [sx, sy, sz] = this._scale, [px, py, pz] = this._position;
        return [(1-2*y*y-2*z*z)*sx, (2*x*y+2*w*z)*sx, (2*x*z-2*w*y)*sx, 0,
            (2*x*y-2*w*z)*sy, (1-2*x*x-2*z*z)*sy, (2*y*z+2*w*x)*sy, 0,
            (2*x*z+2*w*y)*sz, (2*y*z-2*w*x)*sz, (1-2*x*x-2*y*y)*sz, 0, px, py, pz, 1];
    }
    render(camera, stats = null) {
        this._live();
        const m = this._transform(), nodes = [], valid = [], positions = [], texcoords = [];
        const [dx, dy, dz] = this._light, [u0, v0, u1, v1] = this._uv;
        for (let j = 0; j < this._gz; j++) for (let i = 0; i < this._gx; i++) {
            const x = (i/(this._gx-1)-.5)*this._width, z = (j/(this._gz-1)-.5)*this._height;
            let p = [m[0]*x+m[8]*z+m[12]-dx*this._offset,
                m[1]*x+m[9]*z+m[13]-dy*this._offset, m[2]*x+m[10]*z+m[14]-dz*this._offset];
            let ok = true;
            if (this._world) {
                this._origin.set([p[0]-dx*this._length*.5, p[1]-dy*this._length*.5, p[2]-dz*this._length*.5]);
                const hit = this._world.raycast(this._origin, this._light, this._length, undefined, this._hit);
                ok = !!hit && (this._slope === -1 || hit.ny >= this._slope);
                if (ok) p = [hit.x, hit.y, hit.z];
            }
            p = [p[0]-dx*this._bias, p[1]-dy*this._bias, p[2]-dz*this._bias];
            p.forEach(v => finite(v, 'projected position')); nodes.push(p); valid.push(ok);
        }
        for (let j = 0; j < this._gz-1; j++) for (let i = 0; i < this._gx-1; i++) {
            const a = j*this._gx+i;
            for (const tri of [[a,a+this._gx,a+1],[a+1,a+this._gx,a+this._gx+1]]) {
                if (tri.some(n => !valid[n])) continue;
                for (const n of tri) {
                    positions.push(...nodes[n]);
                    texcoords.push(u0+(u1-u0)*(n%this._gx)/(this._gx-1), v0+(v1-v0)*Math.floor(n/this._gx)/(this._gz-1));
                }
            }
        }
        const snapshot = positions.concat(texcoords);
        if (!same(this._snapshot, snapshot)) {
            let mesh = null, instance = null;
            if (positions.length) {
                mesh = Model3D.Mesh.fromGeometry({positions: new Float32Array(positions), texcoords: new Float32Array(texcoords),
                    material: {shading: Model3D.UNLIT, baseColor: new Float32Array(this._color), texture: this._texture}});
                try { instance = mesh.createInstance(); } catch (e) { mesh.dispose(); throw e; }
            }
            if (this._instance) this._instance.dispose();
            if (this._mesh) this._mesh.dispose();
            this._mesh = mesh; this._instance = instance; this._snapshot = snapshot;
        }
        if (!this._instance) return undefined;
        return Render3D.withBlend(this._blend, () => Render3D.draw(this._instance, camera, Render3D.CULL_NONE, undefined, stats));
    }
    dispose() {
        if (this._dead) return;
        if (this._instance) this._instance.dispose();
        if (this._mesh) this._mesh.dispose();
        if (this._ownedTexture) this._texture.dispose();
        this._instance = this._mesh = this._texture = this._world = this._snapshot = null; this._dead = true;
    }
}

/** Owns a GPU silhouette texture. A separate light camera keeps the main
 * camera untouched. Draw a Model3D.Instance or a callback of 3D draws. */
export class ShadowMap {
    constructor(width = 256, height = width, filter = 1) {
        this._texture = Model3D.Texture.renderTarget(width, height, filter);
        this._capturing = false;
    }
    get texture() {
        if (!this._texture) throw new TypeError('ShadowMap is disposed');
        return this._texture;
    }
    capture(camera, caster) {
        const texture = this.texture;
        if (this._capturing) throw new TypeError('ShadowMap capture cannot nest');
        this._capturing = true;
        try {
            return Render3D.captureSilhouette(texture, () => {
                if (typeof caster === 'function') return caster(camera);
                return Render3D.draw(caster, camera, Render3D.CULL_NONE, undefined, null);
            });
        } finally { this._capturing = false; }
    }
    dispose() {
        if (this._capturing) throw new TypeError('Cannot dispose ShadowMap during capture');
        if (this._texture) this._texture.dispose();
        this._texture = null;
    }
}
