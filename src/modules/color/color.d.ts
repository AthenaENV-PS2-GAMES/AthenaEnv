declare namespace Color {
    type Value = number;

    function new(r: number, g: number, b: number, a?: number): Value;
    function getR(color: Value): number;
    function getG(color: Value): number;
    function getB(color: Value): number;
    function getA(color: Value): number;
    function setR(color: Value, value: number): Value;
    function setG(color: Value, value: number): Value;
    function setB(color: Value, value: number): Value;
    function setA(color: Value, value: number): Value;
}
