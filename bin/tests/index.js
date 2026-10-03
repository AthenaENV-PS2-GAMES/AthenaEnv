/*
 * Test launcher: lists the scripts in tests/ and runs the selected one with
 * std.reload(). Every script comes back here when it ends, throws, or when
 * SELECT+START is held for a second; what it printed (console.log) and its
 * error are shown here, since a console has no terminal.
 *
 * Use it with default_script=tests/index.js in athena.ini.
 */
const DIR = "tests";
const SELF = "tests/index.js";

const WHITE = Color.new(255, 255, 255);
const GREY = Color.new(150, 150, 160);
const DIM = Color.new(100, 100, 110);
const ACCENT = Color.new(90, 170, 255);
const GREEN = Color.new(110, 220, 120);
const RED = Color.new(255, 110, 100);
const YELLOW = Color.new(240, 200, 90);
const BAR = Color.new(40, 60, 100);

const font = new Font();
const pad = Gamepad.player(0);
const { width: SCREEN_W, height: SCREEN_H } = Screen.getMode();
const LINE = Math.max(16, Math.ceil(font.getTextSize("Ag").height) + 4);
const MARGIN = 24;
const TITLE = "AthenaEnv tests";
const TITLE_W = Math.ceil(font.getTextSize(TITLE).width) + 32;
const COLUMNS = Math.max(20, Math.floor((SCREEN_W - 2 * MARGIN) / (font.getTextSize("M").width || 10)));
const ROWS = Math.max(3, Math.floor((SCREEN_H - 4 * LINE - 2 * MARGIN) / LINE));

const scripts = System.listDir(DIR)
    .filter(entry => !entry.dir && entry.name.endsWith(".js") && `${DIR}/${entry.name}` !== SELF)
    .map(entry => entry.name)
    .sort();

const baseName = path => path.slice(path.lastIndexOf("/") + 1);

const last = std.lastRun();
const STATUS = {
    finished: { text: "finished", color: GREEN },
    exited: { text: "left with SELECT+START", color: YELLOW },
    error: { text: "error", color: RED },
    reloaded: { text: "came back with std.reload()", color: GREEN },
};
const status = last && STATUS[last.status] ?
    { text: `${baseName(last.script)}: ${STATUS[last.status].text}`, color: STATUS[last.status].color } : null;

let selected = 0;
let top = 0;
let view = null;   // { title, color, lines, scroll } while the output is shown

/* Splits text into lines that fit the screen, keeping its line breaks. */
function wrap(text, columns) {
    const lines = [];
    for (const paragraph of text.replace(/\n+$/, "").split("\n")) {
        let rest = paragraph.replace(/\t/g, "    ");
        do {
            lines.push(rest.slice(0, columns));
            rest = rest.slice(columns);
        } while (rest.length);
    }
    return lines;
}

/* Output of the last run, then its error; scrolled to the end, where summaries are. */
function outputView() {
    if (!status) return null;
    let text = last.output || "";
    if (last.error) text += (text ? "\n" : "") + last.error;
    const lines = text.trim() ? wrap(text, COLUMNS) : ["(no output)"];
    return { title: status.text, color: status.color, lines, scroll: Math.max(0, lines.length - (ROWS - 1)) };
}

if (last) {
    const index = scripts.indexOf(baseName(last.script));
    if (index >= 0) selected = index;
    if ((last.output && last.output.trim()) || last.error) view = outputView();
}

function lineColor(text) {
    if (text.startsWith("[PASS]")) return GREEN;
    if (/FAIL|Error|Uncaught|\b[1-9]\d* failed/.test(text)) return RED;
    if (/PASS|passed/.test(text)) return GREEN;
    return WHITE;
}

function print(x, y, text, color) {
    font.color = color;
    font.print(x, y, text);
}

function run(name) {
    try {
        std.reload(`${DIR}/${name}`, { returnTo: SELF });
    } catch (error) {
        view = { title: `Could not start ${name}`, color: RED, lines: wrap(String(error), COLUMNS), scroll: 0 };
    }
}

function drawList() {
    if (selected < top) top = selected;
    if (selected >= top + ROWS) top = selected - ROWS + 1;

    for (let row = 0; row < ROWS && top + row < scripts.length; row++) {
        const index = top + row;
        const y = MARGIN + 2 * LINE + row * LINE;
        const name = scripts[index];
        if (index === selected) Draw.rect(MARGIN - 6, y - 2, SCREEN_W - 2 * MARGIN + 12, LINE, BAR);
        print(MARGIN, y, name, index === selected ? WHITE : name.endsWith("_test.js") ? GREY : DIM);
    }
    if (!scripts.length) print(MARGIN, MARGIN + 2 * LINE, `No scripts in ${DIR}/`, GREY);
    if (scripts.length > ROWS) print(SCREEN_W - MARGIN - 80, MARGIN, `${selected + 1}/${scripts.length}`, DIM);
}

function drawView() {
    const visible = ROWS - 1;
    const maxScroll = Math.max(0, view.lines.length - visible);
    view.scroll = Math.max(0, Math.min(view.scroll, maxScroll));

    print(MARGIN, MARGIN + 2 * LINE, view.title, view.color);
    for (let row = 0; row < visible && view.scroll + row < view.lines.length; row++) {
        const text = view.lines[view.scroll + row];
        print(MARGIN, MARGIN + 3 * LINE + row * LINE, text, lineColor(text));
    }
    if (view.lines.length > visible) {
        const end = Math.min(view.lines.length, view.scroll + visible);
        print(SCREEN_W - MARGIN - 120, MARGIN, `${view.scroll + 1}-${end}/${view.lines.length}`, DIM);
    }
}

Loop.run(() => {
    Gamepad.update();
    print(MARGIN, MARGIN, TITLE, ACCENT);

    if (view) {
        drawView();
        if (pad.repeatPressed(Gamepad.UP)) view.scroll--;
        if (pad.repeatPressed(Gamepad.DOWN)) view.scroll++;
        if (pad.repeatPressed(Gamepad.LEFT)) view.scroll -= ROWS - 1;
        if (pad.repeatPressed(Gamepad.RIGHT)) view.scroll += ROWS - 1;
        if (pad.anyJustPressed(Gamepad.CROSS | Gamepad.CIRCLE)) view = null;
        print(MARGIN, SCREEN_H - MARGIN - LINE, "UP/DOWN scroll   LEFT/RIGHT page   X/O back to the list", DIM);
        return;
    }

    if (status) print(MARGIN + TITLE_W, MARGIN, status.text, status.color);
    drawList();
    if (scripts.length) {
        if (pad.repeatPressed(Gamepad.UP)) selected = (selected + scripts.length - 1) % scripts.length;
        if (pad.repeatPressed(Gamepad.DOWN)) selected = (selected + 1) % scripts.length;
        if (pad.repeatPressed(Gamepad.LEFT)) selected = Math.max(0, selected - ROWS);
        if (pad.repeatPressed(Gamepad.RIGHT)) selected = Math.min(scripts.length - 1, selected + ROWS);
        if (pad.justPressed(Gamepad.CROSS)) run(scripts[selected]);
    }
    if (status && pad.justPressed(Gamepad.TRIANGLE)) view = outputView();
    print(MARGIN, SCREEN_H - MARGIN - LINE,
        status ? "X run   TRIANGLE last output   In a script: hold SELECT+START to come back"
            : "X run   LEFT/RIGHT page   In a script: hold SELECT+START to come back", DIM);
}, { clearColor: Color.new(12, 14, 22) });
