import * as VectorModule from 'Vector';
import * as Matrix4Module from 'Matrix4';

const Vector2 = VectorModule.Vector2;
const Vector3 = VectorModule.Vector3;
const Vector4 = VectorModule.Vector4;
const Matrix4 = Matrix4Module.Matrix4;

let passed = 0;
let failed = 0;

function check(condition, message) {
    if (!condition) {
        failed++;
        throw new Error(message);
    }
    passed++;
    console.log(`[PASS] ${message}`);
}

const v2 = new Vector2(3, 4);
check(v2.norm() === 5, 'Vector2 computes norm');

const v3 = new Vector3(1, 0, 0).cross(new Vector3(0, 1, 0));
check(v3.x === 0 && v3.y === 0 && v3.z === 1,
    'Vector3 computes cross product');

const v4 = new Vector4(1, 2, 3, 1);
check(v4.w === 1 && v4.dot(v4) === 15,
    'Vector4 preserves homogeneous component');

const identity = new Matrix4();
check(identity.get(0) === 1 && identity.get(5) === 1 &&
    identity.get(10) === 1 && identity.get(15) === 1,
    'Matrix4 defaults to identity');

const matrix = new Matrix4();
matrix.set(12, 10);
matrix.set(13, 20);
matrix.set(14, 30);
matrix.invert();
check(matrix.get(12) === -10 && matrix.get(13) === -20 &&
    matrix.get(14) === -30,
    'Matrix4 inverts affine translation');

const nearIdentity = new Matrix4();
const perturbed = new Matrix4();
perturbed.set(0, 1.00001);
check(nearIdentity.equalsEpsilon(perturbed, 0.0001),
    'Matrix4 compares with epsilon');

let divisionRejected = false;
try {
    new Vector3(1, 2, 3).div(new Vector3(1, 0, 1));
} catch (error) {
    divisionRejected = true;
}
check(divisionRejected, 'Vector division rejects zero components');

for (let i = 0; i < 64; i++) {
    const stressMatrix = new Matrix4();
    stressMatrix.set(12, i);
    stressMatrix.set(13, i * 2);
    stressMatrix.set(14, i * 3);
    stressMatrix.invert();
    const stressVector = new Vector4(i, i + 1, i + 2, 1);
    stressVector.add(new Vector4(1, 1, 1, 0));
}
check(true, 'Aligned vector and matrix allocation stress');

console.log(`Result: ${passed} passed, ${failed} failed`);
