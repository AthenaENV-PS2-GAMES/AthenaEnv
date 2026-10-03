/**
 * PS2-aligned vector types.
 *
 * `Vector2`, `Vector3` and `Vector4` are separate JavaScript classes exposed
 * by the `Vector` module. Arithmetic methods return new vectors and do not
 * mutate their operands. `div()` rejects zero components.
 *
 * Example:
 * ```js
 * import * as Vector from 'Vector';
 * const direction = new Vector.Vector3(3, 4, 0);
 * console.log(direction.norm());
 * const right = direction.cross(new Vector.Vector3(0, 0, 1));
 * ```
 */
declare class Vector2 {
    /** Creates a two-component vector. */
    constructor(x: number, y: number);
    /** Horizontal component. */
    x: number;
    /** Vertical component. */
    y: number;
    /** Returns Euclidean length. */
    norm(): number;
    /** Returns the dot product. */
    dot(value: Vector2): number;
    /** Returns Euclidean distance to another vector. */
    distance(value: Vector2): number;
    /** Returns squared distance without taking a square root. */
    distance2(value: Vector2): number;
    /** Returns the component-wise sum. */
    add(value: Vector2): Vector2;
    /** Returns the component-wise difference. */
    sub(value: Vector2): Vector2;
    /** Returns the component-wise product. */
    mul(value: Vector2): Vector2;
    /** Returns the component-wise quotient; zero divisors throw. */
    div(value: Vector2): Vector2;
    /** Returns a readable component representation. */
    toString(): string;
}

declare class Vector3 {
    /** Creates a three-component vector. */
    constructor(x: number, y: number, z: number);
    /** X component. */
    x: number;
    /** Y component. */
    y: number;
    /** Z component. */
    z: number;
    /** Returns Euclidean length. */
    norm(): number;
    /** Returns the dot product. */
    dot(value: Vector3): number;
    /** Returns the 3D cross product. */
    cross(value: Vector3): Vector3;
    /** Returns Euclidean distance to another vector. */
    distance(value: Vector3): number;
    /** Returns squared distance without taking a square root. */
    distance2(value: Vector3): number;
    /** Returns the component-wise sum. */
    add(value: Vector3): Vector3;
    /** Returns the component-wise difference. */
    sub(value: Vector3): Vector3;
    /** Returns the component-wise product. */
    mul(value: Vector3): Vector3;
    /** Returns the component-wise quotient; zero divisors throw. */
    div(value: Vector3): Vector3;
    /** Returns a readable component representation. */
    toString(): string;
}

declare class Vector4 {
    /** Creates a homogeneous four-component vector. */
    constructor(x: number, y: number, z: number, w: number);
    /** X component. */
    x: number;
    /** Y component. */
    y: number;
    /** Z component. */
    z: number;
    /** Homogeneous component: commonly 1 for points and 0 for directions. */
    w: number;
    /** Returns four-dimensional Euclidean length. */
    norm(): number;
    /** Returns the four-component dot product. */
    dot(value: Vector4): number;
    /** Returns the cross product with homogeneous component cleared. */
    cross(value: Vector4): Vector4;
    /** Returns Euclidean distance to another vector. */
    distance(value: Vector4): number;
    /** Returns squared distance without taking a square root. */
    distance2(value: Vector4): number;
    /** Returns the component-wise sum. */
    add(value: Vector4): Vector4;
    /** Returns the component-wise difference. */
    sub(value: Vector4): Vector4;
    /** Returns the component-wise product. */
    mul(value: Vector4): Vector4;
    /** Returns the component-wise quotient; zero divisors throw. */
    div(value: Vector4): Vector4;
    /** Returns a readable component representation. */
    toString(): string;
}
