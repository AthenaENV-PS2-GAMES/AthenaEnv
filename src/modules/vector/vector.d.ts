declare class Vector2 {
    constructor(x: number, y: number);
    x: number; y: number;
    norm(): number; dot(value: Vector2): number;
    distance(value: Vector2): number; distance2(value: Vector2): number;
    add(value: Vector2): Vector2; sub(value: Vector2): Vector2;
    mul(value: Vector2): Vector2; div(value: Vector2): Vector2;
    toString(): string;
}

declare class Vector3 {
    constructor(x: number, y: number, z: number);
    x: number; y: number; z: number;
    norm(): number; dot(value: Vector3): number; cross(value: Vector3): Vector3;
    distance(value: Vector3): number; distance2(value: Vector3): number;
    add(value: Vector3): Vector3; sub(value: Vector3): Vector3;
    mul(value: Vector3): Vector3; div(value: Vector3): Vector3;
    toString(): string;
}

declare class Vector4 {
    constructor(x: number, y: number, z: number, w: number);
    x: number; y: number; z: number; w: number;
    norm(): number; dot(value: Vector4): number; cross(value: Vector4): Vector4;
    distance(value: Vector4): number; distance2(value: Vector4): number;
    add(value: Vector4): Vector4; sub(value: Vector4): Vector4;
    mul(value: Vector4): Vector4; div(value: Vector4): Vector4;
    toString(): string;
}
