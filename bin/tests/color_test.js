import * as Color from "Color";

function assertEqual(name, actual, expected) {
    if (actual !== expected) {
        throw new Error(`${name}: expected ${expected}, got ${actual}`);
    }
}

const color = Color.new(0x12, 0x34, 0x56, 0x78);
assertEqual("red", Color.getR(color), 0x12);
assertEqual("green", Color.getG(color), 0x34);
assertEqual("blue", Color.getB(color), 0x56);
assertEqual("alpha", Color.getA(color), 0x78);
assertEqual("set red", Color.getR(Color.setR(color, 0xaa)), 0xaa);
assertEqual("set green", Color.getG(Color.setG(color, 0xbb)), 0xbb);
assertEqual("set blue", Color.getB(Color.setB(color, 0xcc)), 0xcc);
assertEqual("set alpha", Color.getA(Color.setA(color, 0xdd)), 0xdd);

console.log("Color module test passed");
