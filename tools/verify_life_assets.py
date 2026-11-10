#!/usr/bin/env python3
"""Audit original GLB mesh files and their nonzero extrusion; no game assets."""
import json,struct,sys,re
from pathlib import Path
root=Path(__file__).resolve().parents[1]/'assets/life_fx'
failures=0
for name in ('spooky_ghost','player_ghost','blue_jester'):
 try:
  data=(root/(name+'.glb')).read_bytes()
  magic,version,total=struct.unpack_from('<III',data)
  length,kind=struct.unpack_from('<II',data,12)
  assert magic==0x46546c67 and version==2 and total==len(data) and kind==0x4e4f534a
  gltf=json.loads(data[20:20+length]);count=0;volume=False
  for mesh in gltf['meshes']:
   for primitive in mesh['primitives']:
    a=gltf['accessors'][primitive['attributes']['POSITION']]
    count+=gltf['accessors'][primitive['indices']]['count']//3
    volume |= all(hi-lo>.05 for lo,hi in zip(a['min'],a['max']))
  assert count>100 and volume and gltf.get('materials')
  print(f'[PASS] {name}: {count} triangles, materials, nonzero extrusion')
 except (OSError,ValueError,KeyError,AssertionError,struct.error) as exc:
  failures+=1;print(f'[FAIL] {name}: {exc}')
try:
 header=(root.parents[1]/'include/gusto/client/rof2/life_art.hpp').read_text().split(' Ghost = {{')[1].split('}};')[0]
 vertices=[tuple(float(v) for v in match) for match in re.findall(r'\{(-?[0-9.]+)f,(-?[0-9.]+)f,(-?[0-9.]+)f,',header)]
 assert vertices
 # Blender bevel tessellation can differ by a few float ulps at mirrored joins.
 assert all(any(abs(x+xx)<2e-5 and abs(y-yy)<2e-5 and abs(z-zz)<2e-5 for xx,yy,zz in vertices) for x,y,z in vertices)
 assert 'fang' not in (root/'ghost_outline.svg').read_text().lower()
 print('[PASS] Ghost runtime mesh is mirror-symmetric about X=0')
except (OSError,IndexError,AssertionError) as exc:
 failures+=1;print(f'[FAIL] Ghost symmetry: {exc}')
print(f'{failures} failure(s)');sys.exit(bool(failures))
