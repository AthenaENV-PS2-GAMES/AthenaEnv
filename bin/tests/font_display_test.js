const font = new Font();
const mode = Screen.getMode();
const white = Color.new(255, 255, 255);
const cyan = Color.new(96, 224, 255);
const background = Color.new(18, 24, 40);

font.scale = 1;
font.color = white;
font.align = Font.ALIGN_NONE;

let frame = 0;

while (true) {
    const text = `AthenaEnv Font  frame ${frame}`;
    const size = font.getTextSize(text);
    const x = (mode.width - size.width) / 2;
    const y = mode.height / 2 - size.height / 2;

    Screen.clear(background);

    font.color = cyan;
    font.print(x, y - 32, "Font display test");

    font.color = white;
    font.print(x, y, text);

    Screen.flip();
    frame++;
}
