"""Original vector drawings extruded in Blender; exports editable and runtime art.
Run: blender --background --factory-startup --python tools/create_life_fx.py
"""
import bpy, math, json, os
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'assets/life_fx'
OUT.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
def mat(name,color):
 m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
 bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(*color,1);bs.inputs['Roughness'].default_value=.55
 return m
white=mat('Ghost porcelain white',(1,1,1));ink=mat('Deep shadow eyes',(.012,.018,.035))
blue=mat('Jester cobalt',(.06,.28,1));ice=mat('Jester icy blue',(.35,.72,1));navy=mat('Jester hat navy',(.025,.075,.3))
pearl=mat('Jester bells',(.6,.9,1));orange=mat('Player spirit orange',(1,.15,.015))
shapes={}
def shape(name,points,material,depth=.09,z=0,bevel=.022):
 c=bpy.data.curves.new(name,'CURVE');c.dimensions='2D';c.fill_mode='BOTH';c.extrude=depth;c.bevel_depth=bevel;c.bevel_resolution=1
 spline=c.splines.new('POLY');spline.points.add(len(points)-1)
 for p,xy in zip(spline.points,points):p.co=(*xy,0,1)
 spline.use_cyclic_u=True
 obj=bpy.data.objects.new(name,c);bpy.context.collection.objects.link(obj);obj.rotation_euler[0]=math.pi/2;obj.location.y=-z;obj.data.materials.append(material)
 bpy.context.view_layer.objects.active=obj;obj.select_set(True);bpy.ops.object.convert(target='MESH');obj=bpy.context.object
 bpy.ops.object.select_all(action='DESELECT')
 shapes[name]=points
 return obj

def oval(cx,cy,rx,ry,angle=0,n=22):
 return [(cx+rx*math.cos(t)*math.cos(angle)-ry*math.sin(t)*math.sin(angle),cy+rx*math.cos(t)*math.sin(angle)+ry*math.sin(t)*math.cos(angle)) for t in [i*2*math.pi/n for i in range(n)]]
head=[(.7*math.cos(i*math.pi/24),.48+.73*math.sin(i*math.pi/24)) for i in range(25)]
side=[(.7,-.82),(.77,-.24),(.98,-.12),(.78,.1)]
outline=side+head+[(-x,y) for x,y in reversed(side)]+[(-.46,-.55),(-.23,-.93),(0,-.66),(.23,-.93),(.46,-.55)]
ghost=[shape('Ghost drawn silhouette',outline,white),shape('Ghost left eye',oval(-.26,.48,.14,.22,-.19),ink,.025,.12,.01),shape('Ghost right eye',oval(.26,.48,.14,.22,.19),ink,.025,.12,.01),shape('Ghost open wail',oval(0,-.02,.19,.27),ink,.025,.12,.012)]
jester=[shape('Jester drawn face',oval(0,0,.62,.66),blue),shape('Three point cap',[(-.6,.35),(-.97,1.08),(-.54,.91),(-.3,.64),(0,1.55),(.3,.64),(.55,.92),(.97,1.08),(.59,.34)],navy,.11,.03),shape('Cobalt center cap',[(-.28,.62),(0,1.55),(.28,.62)],blue,.025,.17),shape('Hat band',[(-.65,.37),(-.63,.6),(.63,.6),(.65,.37)],ice,.025,.18)]
for name,x,y in [('left',-.97,1.08),('center',0,1.55),('right',.97,1.08)]: jester.append(shape('Jester '+name+' bell',oval(x,y,.105,.105,n=16),pearl,.05,.08,.02))
jester += [shape('Jester left mask',[(-.47,.26),(-.12,.17),(-.18,-.005),(-.39,.055)],ice,.025,.12,.01),shape('Jester right mask',[(.12,.17),(.47,.26),(.39,.055),(.18,-.005)],ice,.025,.12,.01),shape('Jester left pupil',oval(-.28,.12,.06,.11,-.25,n=16),ink,.015,.17,.008),shape('Jester right pupil',oval(.28,.12,.06,.11,.25,n=16),ink,.015,.17,.008),shape('Jester wicked smile',[(-.46,-.15),(-.2,-.24),(0,-.26),(.2,-.24),(.46,-.15),(.29,-.45),(0,-.55),(-.29,-.45)],ink,.025,.13,.015)]
for i in range(5):
 x=(i-2)*.12;jester.append(shape('Jester tooth '+str(i),[(x-.045,-.26),(x+.045,-.26),(x+.035,-.36),(x-.035,-.36)],pearl,.012,.18,.004))

def export(name,objects):
 bpy.ops.object.select_all(action='DESELECT')
 for obj in objects:obj.select_set(True)
 bpy.context.view_layer.objects.active=objects[0]
 bpy.ops.export_scene.gltf(filepath=str(OUT/(name+'.glb')),use_selection=True,export_format='GLB')
 bpy.ops.wm.obj_export(filepath=str(OUT/(name+'.obj')),export_selected_objects=True,export_materials=True)
 # Triangulated vertices with baked studio shading for the fixed-function renderer.
 verts=[];light=Vector((-.35,-.65,.8)).normalized()
 for obj in objects:
  mesh=obj.data;mesh.calc_loop_triangles();material=obj.data.materials[0];color=material.diffuse_color[:3]
  for tri in mesh.loop_triangles:
   normal=(obj.matrix_world.to_3x3()@tri.normal).normalized();shade=.9+.1*max(0,normal.dot(light))
   packed=0xff000000
   for shift,c in zip((16,8,0),color):packed|=max(0,min(255,round(c*shade*255)))<<shift
   for index in tri.vertices:
    v=obj.matrix_world@mesh.vertices[index].co;verts.append([*map(float,v),packed])
 return verts
meshes={'Ghost':export('spooky_ghost',ghost),'Jester':export('blue_jester',jester)}
header=['// Generated from original Blender-extruded artwork by tools/create_life_fx.py.','// Do not hand edit. Editable assets: assets/life_fx.','#pragma once','#include <array>','#include <cstdint>','namespace gusto::client::rof2::life_art {','struct Vertex { float x,y,z; std::uint32_t color; };']
for name,vertices in meshes.items():
 header.append('inline constexpr std::array<Vertex,%d> %s = {{'%(len(vertices),name))
 for x,y,z,c in vertices:header.append('  {%.7ff,%.7ff,%.7ff,0x%08xu},'%(x,y,z,c))
 header.append('}};')
header.append('}')
(ROOT/'include/gusto/client/rof2/life_art.hpp').write_text('\n'.join(header)+'\n')
(OUT/'mesh_stats.json').write_text(json.dumps({k:{'vertices':len(v),'triangles':len(v)//3,'bounds':[[min(p[a] for p in v),max(p[a] for p in v)] for a in range(3)]} for k,v in meshes.items()},indent=2)+'\n')
for name,objects in [('ghost_outline',ghost),('jester_outline',jester)]:
 elements=[]
 for obj in objects:
  points=shapes[obj.name]; coords=' '.join('%.2f,%.2f'%(200+x*90,190-y*90) for x,y in points)
  color=obj.data.materials[0].diffuse_color[:3]; fill='#'+''.join('%02x'%round(c*255) for c in color)
  elements.append('<polygon points="'+coords+'" fill="'+fill+'"/>')
 (OUT/(name+'.svg')).write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 340">'+''.join(elements)+'</svg>\n')
player=[]
for obj in ghost:
 copy=obj.copy();copy.data=obj.data.copy();bpy.context.collection.objects.link(copy);copy.scale=(1.6,1.6,1.6);copy.location.y*=1.6
 if copy.data.materials[0]==white:copy.data.materials[0]=orange
 copy.name='Player '+copy.name;player.append(copy)
export('player_ghost',player)
# Three-model gallery in an editable blend file: small white NPC, large orange player, blue FD.
for obj in ghost:obj.location.x=-2.6
for obj in jester:obj.location.x=2.7
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=24;scene.render.threads_mode='FIXED';scene.render.threads=4
scene.render.resolution_x=1200;scene.render.resolution_y=650;scene.render.resolution_percentage=100
scene.world.color=(.04,.04,.04)
world=scene.world;world.use_nodes=True;world.node_tree.nodes['Background'].inputs[0].default_value=(.018,.027,.055,1);world.node_tree.nodes['Background'].inputs[1].default_value=.5
for location,power,size in [((1,-5,6),900,6),((-5,-2,2),500,4),((3,3,5),1100,4)]:
 bpy.ops.object.light_add(type='AREA',location=location);light=bpy.context.object;light.data.energy=power;light.data.shape='DISK';light.data.size=size;light.rotation_euler=(Vector((0,0,.2))-light.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(1,-12,4.2));camera=bpy.context.object;camera.rotation_euler=(Vector((0,0,.35))-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=9.4;scene.camera=camera
scene.view_settings.view_transform='Standard'
scene.render.image_settings.file_format='PNG';scene.render.filepath=str(OUT/'blender_preview.png')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'gusto_life_effects.blend'))
if not os.environ.get('GUSTO_SKIP_ART_RENDER'):bpy.ops.render.render(write_still=True)
print('GUSTO original meshes exported:',json.dumps({k:len(v)//3 for k,v in meshes.items()}))
