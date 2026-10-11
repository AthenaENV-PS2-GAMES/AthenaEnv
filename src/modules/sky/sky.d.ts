/**
 * Sky and time of day for outdoor scenes.
 *
 * `draw(camera)` paints the sky in screen space before the 3D scene: a
 * vertical gradient from the ground colour through the horizon to the
 * zenith, by the elevation of the camera ray through each band (camera
 * roll is ignored), and the sun as a disc with a halo. There is no geometry
 * around the camera, so nothing goes to the clipper. `setTime(hours)`
 * blends built-in keyframes (night, dawn, day, dusk) into the colors and
 * the sun, and `apply(lights)` writes the matching ambient, the sun (or
 * moon) as directional slot 0 and the horizon as fog colour. Colors are
 * linear [r, g, b] in [0, 1].
 *
 * `tint()` is the matching day/night RGB multiplier (white at noon, dark blue
 * at night). Lights only darken DIFFUSE materials; for UNLIT meshes and Voxel
 * baked light call `Render3D.setTint(...Sky.tint())` before drawing them (and
 * restore white after): Sky does not touch Render3D itself. Not in the default build: `node tools/modules.js configure --modules=sky,...`
 *
 * Example:
 * ```js
 * let hours = 6;
 * Loop.run({
 *     update(dt) { hours += dt / 10; Sky.setTime(hours); Sky.apply(lights); },
 *     draw() {
 *         Screen.clear(Sky.clearColor());
 *         Sky.draw(camera);
 *         Render3D.setTint(...Sky.tint());
 *         scene.draw(camera, Render3D.CULL_BACK, lights);
 *         Render3D.setTint(1, 1, 1);
 *     },
 * });
 * ```
 */
declare namespace Sky {
    type RGB = [number, number, number];
    function setColors(colors: { zenith?: RGB; horizon?: RGB; ground?: RGB }): void;
    /** Direction toward the sun (normalized); color black hides the disc; size: radius in pixels (default 18). */
    function setSun(dx: number, dy: number, dz: number, options?: { color?: RGB; size?: number }): void;
    /** Hours (wrapped to 0..24): colors, sun direction and color, light colors. */
    function setTime(hours: number): void;
    /** Ambient, directional slot 0 and (with fog on in lights, unless fog: false) the fog colour. */
    function apply(lights: Lights.Set, options?: { fog?: boolean }): void;
    /** Draws the gradient (1-64 bands, default 16) and the sun; returns the bands drawn. */
    function draw(camera: Camera3D.Camera, bands?: number): number;
    /** Sky color of a ray at an elevation in radians. */
    function colorAt(elevation: number): RGB;
    /** The horizon as a Color.new() value, for Screen.clear(). */
    function clearColor(): number;
    /** Day/night RGB multiplier in [0, 1] for Render3D.setTint(). */
    function tint(): RGB;
    function state(): { time: number; zenith: RGB; horizon: RGB; ground: RGB; sunDirection: RGB; sunColor: RGB;
        ambient: RGB; light: RGB; lightDirection: RGB; tint: RGB };
}
