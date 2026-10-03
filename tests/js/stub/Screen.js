/* Host stand-in for Screen: NTSC 640x448 until setMode(). */
export const NTSC = 2, PAL = 3, DTV_480p = 0x50, DTV_576p = 0x53;
let mode = { mode: NTSC, width: 640, height: 448, psm: 0 };
export function getMode() { return Object.assign({}, mode); }
export function setMode(next) { mode = Object.assign({}, next); }
export function getFreeVRAM() { return 1536 * 1024; }
