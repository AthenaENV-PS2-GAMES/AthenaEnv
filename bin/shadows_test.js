import * as Shadows from 'Shadows';
import * as Model3D from 'Model3D';
import * as Render3D from 'Render3D';
import * as Camera3D from 'Camera3D';
import * as Collision3D from 'Collision3D';
function assert(ok, message) { if (!ok) throw new Error(message); }
function throws(fn) { let threw=false; try { fn(); } catch (_) { threw=true; } assert(threw, 'expected exception'); }
const camera = new Camera3D.Camera().setPosition(0, 4, 5).lookAt(0, 0, 0);
const p = new Shadows.Projector();
p.setSize(2, 4); p.position = {x: 2, y: 0, z: 0};
p.render(camera);
let bounds=p._mesh.getBounds();
assert(Math.abs(bounds[0]-1)<1e-5 && Math.abs(bounds[3]-3)<1e-5 && Math.abs(bounds[1]-.01)<1e-5, 'transformed footprint and bias');
const first=p._mesh;
p.render(camera); assert(first===p._mesh, 'static geometry must be reused');
p.setColor(0,0,0,.3); p.render(camera); assert(first!==p._mesh, 'color must update geometry');
p.position=[0,0,0]; p.setGrid(4,4).setSize(2,2);
const world=new Collision3D.World(); world.addBox([-10,-1,-10],[10,0,10]);
p.enableRaycast(world,4).setSlopeLimit(.8);
p.render(camera); bounds=p._mesh.getBounds();
assert(Math.abs(bounds[1]-.01)<1e-5 && Math.abs(bounds[4]-.01)<1e-5, 'terrain projection');
const terrain=p._mesh;
p.render(camera); assert(terrain===p._mesh, 'unchanged terrain reuses geometry');
p.position=[0,10,0]; assert(p.render(camera)===undefined && p._mesh===null, 'ray misses omit triangles');
p.enableRaycast(null,2,false); p.position=[0,0,0];
for(let mode=0;mode<3;mode++) { p.setBlend(mode); p.render(camera); }
throws(()=>p.setGrid(65,2)); throws(()=>p.setLightDir(0,0,0)); throws(()=>p.setColor(0,0,0,2));
throws(()=>p.setSlopeLimit(2)); throws(()=>p.setBias(NaN)); throws(()=>p.setBlend(3));
throws(()=>Render3D.withBlend(3,()=>{}));
throws(()=>Render3D.group(()=>p.render(camera)));
throws(()=>Render3D.withBlend(0,()=>{ throw new Error('test'); }));
// A following opaque draw and nested scopes must still succeed.
Render3D.withBlend(0,()=>Render3D.withBlend(2,()=>p.render(camera)));
p.render(camera);
const map=new Shadows.ShadowMap(128,64);
throws(()=>map.texture.upload());
map.capture(camera,p._instance);map.texture.upload();
const dynamic=new Shadows.Projector(map.texture);
dynamic.render(camera);
map.capture(camera,cam=>{ assert(cam===camera,'light camera forwarded'); Render3D.draw(p._instance,cam,Render3D.CULL_NONE,undefined,null); });
throws(()=>map.capture(camera,()=>map.capture(camera,p._instance)));
throws(()=>map.texture.upload()); // failed capture invalidates partial output
map.capture(camera,p._instance);dynamic.render(camera);
throws(()=>map.capture(camera,()=>map.dispose()));
map.capture(camera,p._instance);
dynamic.dispose();map.dispose();map.dispose();throws(()=>map.texture);
throws(()=>new Shadows.ShadowMap(96,64));
p.dispose(); p.dispose(); throws(()=>p.render(camera));
world.dispose(); camera.dispose();
console.log('Shadows tests passed');
