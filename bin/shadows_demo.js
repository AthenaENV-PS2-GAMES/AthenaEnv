// Build with modules: shadows,screen,loop,color (dependencies auto-resolve).
import * as Screen from 'Screen';
import * as Loop from 'Loop';
import * as Color from 'Color';
import * as Model3D from 'Model3D';
import * as Camera3D from 'Camera3D';
import * as Render3D from 'Render3D';
import * as Collision3D from 'Collision3D';
import * as Shadows from 'Shadows';

const mode=Screen.getMode(); mode.zbuffering=true; mode.psmz=Screen.Z16S; Screen.setMode(mode);
const camera=new Camera3D.Camera({aspect:mode.width/mode.height})
    .setViewport(mode.width,mode.height).setPosition(0,5,7).lookAt(0,0,0);
// Sloped receiver: the grid conforms to the rise from y=0 to y=1.
const positions=new Float32Array([-4,0,-3, -4,0,3, 4,1,-3, 4,1,-3, -4,0,3, 4,1,3]);
const mesh=Model3D.Mesh.fromGeometry({positions,material:{baseColor:new Float32Array([.75,.8,.85,1])}});
const receiver=mesh.createInstance();
const world=new Collision3D.World(); world.addTriangles(positions);
const shadow=new Shadows.Projector().setGrid(6,6).setSize(2,2).setBias(.02).enableRaycast(world,6);
let time=0;
Loop.run({
    update(dt){time+=dt;shadow.position={x:Math.sin(time)*2.5,y:1,z:Math.cos(time)*1.2};},
    draw(){Render3D.draw(receiver,camera,Render3D.CULL_NONE,undefined,null);shadow.render(camera);}
},{clearColor:Color.new(40,45,60)});
