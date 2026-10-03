import * as FontModule from "Font";

const FontClass = FontModule.Font;

if (typeof FontClass !== "function") {
    throw new Error("Font constructor is not exported");
}

const constants = [
    "ALIGN_TOP",
    "ALIGN_BOTTOM",
    "ALIGN_VCENTER",
    "ALIGN_LEFT",
    "ALIGN_RIGHT",
    "ALIGN_HCENTER",
    "ALIGN_NONE",
    "ALIGN_CENTER",
];

for (const name of constants) {
    if (!Number.isInteger(FontClass[name])) {
        throw new Error(`Font.${name} is missing`);
    }
}

const defaultFont = new FontClass();
const defaultSize = defaultFont.getTextSize("AthenaEnv");
if (!Number.isFinite(defaultSize.width) || !Number.isFinite(defaultSize.height) ||
    defaultSize.width <= 0 || defaultSize.height <= 0) {
    throw new Error("Embedded default font returned an invalid text size");
}

let rejected = false;
try {
    new FontClass("host:/missing-font.ttf");
} catch (error) {
    rejected = true;
}
if (!rejected) {
    throw new Error("Font must report a load failure");
}

console.log("Font module API validation passed");
