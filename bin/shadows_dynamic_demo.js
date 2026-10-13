// Asset-free silhouette capture demo. Modules: shadows,screen,loop,color.
// Render order: capture caster -> opaque receiver -> projector -> caster.
import * as Screen from 'Screen';
import * as Loop from 'Loop';
import * as Color from 'Color';
import * as Model3D from 'Model3D';
import * as Camera3D from 'Camera3D';
import * as Render3D from 'Render3D';
import * as Collision3D from 'Collision3D';
import * as Shadows from 'Shadows';
const mode=Screen.getMode();mode.zbuffering=true;mode.psmz=Screen.Z16S;Screen.setMode(mode);
const camera=new Camera3D.Camera({aspect:mode.width/mode.height})
    .setViewport(mode.width,mode.height).setPosition(0,5,8).lookAt(0,0,0);
const lightCamera=new Camera3D.Camera({fovYDegrees:40,aspect:1,near:.1,far:20})
    .setViewport(128,128).setPosition(0,6,1).setUp(0,0,-1).setPosition(0,6,0);
const groundPositions=new Float32Array([-5,0,-4,-5,0,4,5,0,-4,5,0,-4,-5,0,4,5,0,4]);
const groundMesh=Model3D.Mesh.fromGeometry({positions:groundPositions,
    material:{baseColor:new Float32Array([.75,.8,.85,1])}});
const ground=groundMesh.createInstance();
const cubeMesh=Model3D.Mesh.fromGeometry({
    positions:new Float32Array([-.5,-.5,-.5,.5,-.5,-.5,.5,.5,-.5,-.5,.5,-.5,
        -.5,-.5,.5,.5,-.5,.5,.5,.5,.5,-.5,.5,.5]),
    indices:new Uint32Array([0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,
        3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5]),
    material:{baseColor:new Float32Array([.9,.3,.15,1])}
});
const cube=cubeMesh.createInstance();
const world=new Collision3D.World();world.addTriangles(groundPositions);
const map=new Shadows.ShadowMap(128,128);
const projector=new Shadows.Projector(map.texture)
    .setSize(2*6*Math.tan(20*Math.PI/180),2*6*Math.tan(20*Math.PI/180))
    .setGrid(6,6).setBias(.02).enableRaycast(world,8).setColor(0,0,0,.65);
let time=0;
Loop.run({
    update(dt){
        time+=dt;const x=Math.sin(time)*2,z=Math.cos(time)*.8;
        cube.setPosition(x,1.2+Math.sin(time*2)*.3,z).setRotationEuler(time*.7,time,time*.4);
        lightCamera.setPosition(x,6,z).lookAt(x,0,z);
        projector.position={x,y:0,z};
    },
    draw(){
        map.capture(lightCamera,cube);
        Render3D.draw(ground,camera,Render3D.CULL_NONE,undefined,null);
        projector.render(camera);
        Render3D.draw(cube,camera,Render3D.CULL_NONE,undefined,null);
    }
},{clearColor:Color.new(40,45,60)});
