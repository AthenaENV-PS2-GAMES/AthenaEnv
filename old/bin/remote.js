// {"name": "DVD Remote demo", "author": "Daniel Santos", "version": "08012026", "file": "remote.js"}

IOP.loadModule("rmman");

Remote.init();

// The remote control shows up on controller port 2 (index 1), whether it's
// the external IR dongle on a fat PS2 or the built-in receiver on a slim.
let remote = Remote.get(1);

const buttonNames = {
    [Remote.DVD_PLAY]: "PLAY",
    [Remote.DVD_PAUSE]: "PAUSE",
    [Remote.DVD_STOP]: "STOP",
    [Remote.DVD_NEXT]: "NEXT",
    [Remote.DVD_PREV]: "PREV",
    [Remote.DVD_SCAN_FORW]: "SCAN_FORW",
    [Remote.DVD_SCAN_BACK]: "SCAN_BACK",
};

while (true) {
    remote.update();

    if (!remote.released() && remote.button !== remote.old_button) {
        const name = buttonNames[remote.button] || ("0x" + remote.button.toString(16));
        console.log(`remote button: ${name}`);
    }

    if (remote.pressed(Remote.DVD_PLAY)) {
        console.log("-> play requested");
    }

    if (remote.pressed(Remote.DVD_STOP)) {
        break;
    }
}
