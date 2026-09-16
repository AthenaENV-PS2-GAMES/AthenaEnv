declare class Matrix4 {
    constructor(...values: number[]);
    readonly length: 16;
    [index: number]: number;
    get(index: number): number;
    set(index: number, value: number): this;
    equals(value: Matrix4): boolean;
    equalsEpsilon(value: Matrix4, epsilon: number): boolean;
    toArray(): number[];
    fromArray(values: ArrayLike<number>): this;
    clone(): Matrix4;
    copy(value: Matrix4): this;
    multiply(value: Matrix4): Matrix4;
    identity(): this;
    transpose(): this;
    invert(): this;
    toString(): string;
}
