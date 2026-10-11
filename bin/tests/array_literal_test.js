// Regression for the transition from stack-built arrays to indexed fields.
let checks = 0;
function assert(ok, label) { checks++; if (!ok) throw new Error(label); }
const large = [
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
    16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
    32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,
    48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64
];
assert(large.length === 65, "literal length beyond 32 and 64");
for (let i = 0; i < 65; i++) assert(large[i] === i, "literal element " + i);
for (const Type of [Uint32Array, Float32Array]) {
    const copied = new Type(large);
    assert(copied.length === 65 && copied.byteLength === 260, "typed array retains literal length");
    for (let i = 0; i < 65; i++) assert(copied[i] === i, "typed element " + i);
}
let setterCalls = 0;
Object.defineProperty(Array.prototype, "33", {configurable: true, set() { setterCalls++; }});
try {
    const own = [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35];
    assert(own.length === 36 && own[33] === 33 && setterCalls === 0, "literal defines own elements");
    const descriptor = Object.getOwnPropertyDescriptor(own, "33");
    assert(descriptor.writable && descriptor.enumerable && descriptor.configurable, "element attributes");
} finally { delete Array.prototype[33]; }
const holes = [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,,,34,,];
assert(holes.length === 36 && !(32 in holes) && !(33 in holes) && holes[34] === 34 && !(35 in holes), "holes beyond 32");
const spread = [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,...[33,34,35]];
assert(spread.length === 36 && spread[35] === 35, "spread beyond 32");
std.gc();
console.log("QuickJS array literal tests passed (" + checks + " checks)");
