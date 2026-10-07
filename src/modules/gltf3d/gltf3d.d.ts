/** glTF 2.0 / GLB scenes with their node hierarchy and animations.
 * Model3D.load() keeps reading single meshes; GLTF3D.load() reads whole
 * scenes: nodes become Scene3D nodes (TRS, or a matrix decomposed into TRS),
 * every triangle primitive a Model3D mesh (extra primitives of a mesh become
 * child nodes), and every animation an Animation3D clip whose targets are
 * node indices. Skins deform their meshes every draw (in C) from the joint
 * nodes, which the clips animate. Morph targets are not supported;
 * CUBICSPLINE samplers keep their keyframe values and play linearly.
 * ```js
 * const hero = GLTF3D.load("models/hero.glb");
 * scene.root.add(hero.root);
 * const walk = new Animation3D.Player(hero.clips.walk, hero.nodes);
 * walk.loop = true; walk.play();
 * ``` */
declare namespace GLTF3D {
    interface Asset {
        /** Group holding the default scene's root nodes. */
        root: Scene3D.Node;
        /** Every node in file order: the clips' target indices. */
        nodes: Scene3D.Node[];
        /** Node names, "" when unnamed. */
        names: string[];
        /** Clips by animation name (the index for unnamed ones). */
        clips: { [name: string]: Animation3D.Clip };
    }
    /** material, as in Model3D.load(), replaces every primitive's material. */
    function load(path: string, material?: Model3D.Material): Asset;
}
