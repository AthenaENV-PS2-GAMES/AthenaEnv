// AthenaEnv default script: replace it with your own, or point
// default_script in athena.ini to another file.

const font = new Font();
const WHITE = Color.new(255, 255, 255);
const GRAY = Color.new(160, 160, 160);

console.log(`[AthenaEnv] Boot path: ${System.bootPath}`);

Loop.run(() => {
    font.color = WHITE;
    font.print(40, 40, "Hello from AthenaEnv!");
    font.color = GRAY;
    font.print(40, 80, "Boot path: " + System.bootPath);
    font.print(40, 110, "Free memory: " + (System.getFreeMemory() / 1048576).toFixed(2) + " MB");
    font.print(40, 140, "FPS: " + Loop.getStats().fps.toFixed(1));
}, { clearColor: Color.new(20, 20, 40) });
