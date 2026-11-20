#!/usr/bin/env python3
"""Export a saved Bubble scene as self-contained GLB, optionally bundled with sources."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import tempfile
import zipfile


def vector(value, size):
    if not isinstance(value, list) or len(value) != size or any(
        isinstance(x, bool) or not isinstance(x, (float, int)) or not math.isfinite(x) for x in value
    ):
        raise ValueError('Expected a finite vector of length ' + str(size))
    return value


def qmul(a, b):
    x,y,z,w=a; X,Y,Z,W=b
    return [w*X+x*W+y*Z-z*Y, w*Y-x*Z+y*W+z*X, w*Z+x*Y-y*X+z*W, w*W-x*X-y*Y-z*Z]


def axis(axis, angle):
    a=math.radians(angle)/2; q=[0.,0.,0.,math.cos(a)]; q[axis]=math.sin(a); return q


def rotation(euler):
    return qmul(qmul(axis(0,euler[0]),axis(1,euler[1])),axis(2,euler[2]))


def rotate(q, v):
    return qmul(qmul(q,list(v)+[0]),[-q[0],-q[1],-q[2],q[3]])[:3]


def trs(record):
    return dict(translation=vector(record.get('position',[0,0,0]),3),
                rotation=rotation(vector(record.get('rotation',[0,0,0]),3)),
                scale=vector(record.get('scale',[1,1,1]),3))


def aim(direction):
    d=vector(direction,3); length=math.sqrt(sum(x*x for x in d))
    if length < 1e-9: raise ValueError('A light direction cannot be zero')
    x,y,z=[v/length for v in d]
    return qmul(axis(1,math.degrees(math.atan2(-x,-z))),axis(0,math.degrees(math.asin(max(-1,min(1,y))))))


class Exporter:
    def __init__(self, scene, root):
        self.scene=scene; self.root=Path(root).resolve(); self.binary=bytearray(); self.files=set()
        self.doc=dict(asset=dict(version='2.0',generator='Bubble Engine scene export'),scene=0,
                      scenes=[dict(nodes=[])],nodes=[],meshes=[],materials=[],bufferViews=[],accessors=[],
                      extras=dict(bubbleScene=scene,notes=[
                          'Static saved scene; scripts, physics, animation clips, HUD and GLSL remain engine metadata.',
                          'Light intensities preserve engine numbers, not a calibrated photometric match.',
                          'Instanced nodes share mesh data; Blender can join selected objects when desired.']))
        self.entities={e['id']:e for e in scene['entities']}
        if len(self.entities)!=len(scene['entities']): raise ValueError('Duplicate entity IDs')
        self.materials={};self.meshes={};self.textures={}
        for e in scene['entities']:
            seen=set();p=e
            while p.get('parent'):
                if p['id'] in seen: raise ValueError('Hierarchy cycle')
                seen.add(p['id']);p=self.entities[p['parent']]

    def path(self, name, base=None):
        p=Path(name);p=(base or self.root)/p if not p.is_absolute() else p;p=p.resolve()
        if not p.is_file(): raise ValueError('Missing resource: '+str(p))
        self.files.add(p);return p

    def blob(self, data, target=None):
        self.binary.extend(b'\0'*(-len(self.binary)%4)); offset=len(self.binary);self.binary.extend(data)
        entry=dict(buffer=0,byteOffset=offset,byteLength=len(data))
        if target:entry['target']=target
        self.doc['bufferViews'].append(entry);return len(self.doc['bufferViews'])-1

    def accessor(self, values, size, integer=False, bounds=False):
        flat=[x for v in values for x in (v if isinstance(v,list) else [v])]
        data=struct.pack('<'+('I' if integer else 'f')*len(flat),*flat)
        a=dict(bufferView=self.blob(data,34963 if integer else 34962),componentType=5125 if integer else 5126,
               count=len(values),type={1:'SCALAR',2:'VEC2',3:'VEC3'}[size])
        if bounds:a.update(min=[min(v[i] for v in values) for i in range(size)],max=[max(v[i] for v in values) for i in range(size)])
        self.doc['accessors'].append(a);return len(self.doc['accessors'])-1

    def texture(self, path):
        if path in self.textures:return self.textures[path]
        data=path.read_bytes()
        mime='image/png' if data.startswith(b'\x89PNG') else 'image/jpeg' if data.startswith(b'\xff\xd8') else None
        if not mime:raise ValueError('GLB texture must be PNG/JPEG: '+str(path))
        images=self.doc.setdefault('images',[]);textures=self.doc.setdefault('textures',[])
        images.append(dict(bufferView=self.blob(data),mimeType=mime,name=path.name))
        textures.append(dict(source=len(images)-1));self.textures[path]=len(textures)-1;return len(textures)-1

    def material(self, source, native, model_path):
        source=dict(source); base=self.root; asset={}
        if source.get('file'):
            path=self.path(source['file']);asset=json.loads(path.read_text());base=path.parent
        elif native:
            path=self.path(native,model_path.parent);asset=json.loads(path.read_text());base=path.parent
        for key in ('vertex_shader','fragment_shader'):
            if asset.get(key):self.path(asset[key],base)
        for texture in asset.get('textures',{}).values():
            if texture.get('path'):self.path(texture['path'],base)
        uniforms=asset.get('uniforms',{})
        def uniform(name,default):return uniforms.get(name,{}).get('value',default)
        albedo=uniform('material.albedo',source.get('albedo',[1,1,1,1]));vector(albedo,4)
        pbr=dict(baseColorFactor=[max(0,min(1,x)) for x in albedo],
                 metallicFactor=max(0,min(1,uniform('material.metallic',[source.get('metallic',0)])[0])),
                 roughnessFactor=max(0,min(1,uniform('material.roughness',[source.get('roughness',1)])[0])))
        m=dict(pbrMetallicRoughness=pbr,doubleSided=True,extras=dict(bubbleMaterial=source,bubbleAsset=asset))
        if albedo[3]<1:m['alphaMode']='BLEND'
        maps=source.get('maps',{})
        for slot,key in [('tex_albedo','baseColorTexture'),('tex_normal','normalTexture'),('tex_ao','occlusionTexture')]:
            ref=asset.get('textures',{}).get(slot,{}).get('path') if asset else maps.get(slot)
            if ref:
                texture=dict(index=self.texture(self.path(ref,base)))
                (pbr if slot=='tex_albedo' else m)[key]=texture
        if 'emissionColor' in uniforms:
            rgb=vector(uniform('emissionColor',[1,1,1]),3);strength=uniform('emissionStrength',[1])[0]
            m['emissiveFactor']=[max(0,min(1,v*strength)) for v in rgb]
        signature=json.dumps(m,sort_keys=True)
        if signature not in self.materials:
            self.materials[signature]=len(self.doc['materials']);self.doc['materials'].append(m)
        return self.materials[signature]

    def mesh(self, record):
        path=self.path(record['model']);signature=(path,json.dumps(record.get('materials',[]),sort_keys=True),bool(record.get('mesh_instances')))
        if signature in self.meshes:return self.meshes[signature]
        source=json.loads(path.read_text())
        if source.get('format')!='bengine-model':raise ValueError('Convert imported geometry to .bmodel first: '+str(path))
        if source.get('rig') or record.get('rig_pose'):raise ValueError('Rig/posed export requires baking first: '+str(path))
        primitives=[]
        for i,mesh in enumerate(source['meshes']):
            vertices=[vector(v,5) for v in mesh['vertices']]
            positions=[v[:3] for v in vertices];uv=[[v[3],1-v[4]] for v in vertices]
            normals=[[0.,0.,0.] for _ in vertices];indices=[]
            for face in mesh['faces']:
                if len(face)<3 or any(type(k)!=int or k<0 or k>=len(vertices) for k in face):raise ValueError('Invalid face')
                for j in range(1,len(face)-1):
                    ids=[face[0],face[j],face[j+1]];a,b,c=[vertices[k] for k in ids]
                    u=[b[n]-a[n] for n in range(3)];v=[c[n]-a[n] for n in range(3)]
                    normal=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
                    if sum(x*x for x in normal)<1e-24:continue
                    indices.extend(ids)
                    for k in ids:
                        for n in range(3):normals[k][n]+=normal[n]
            if not indices:
                primitives.append(None)
                continue
            for k,normal in enumerate(normals):
                length=math.sqrt(sum(x*x for x in normal))
                normals[k]=[x/length for x in normal] if length>1e-12 else [0,1,0]
            materials=record.get('materials',[]);material=self.material(materials[i] if i<len(materials) else {},mesh.get('material'),path)
            primitives.append(dict(attributes=dict(POSITION=self.accessor(positions,3,bounds=True),NORMAL=self.accessor(normals,3),TEXCOORD_0=self.accessor(uv,2)),indices=self.accessor(indices,1,integer=True),material=material))
        ids=[]
        if not record.get('mesh_instances'):
            visible=[p for p in primitives if p is not None]
            if not visible:raise ValueError('Model has no nondegenerate triangles: '+str(path))
            ids=[len(self.doc['meshes'])];self.doc['meshes'].append(dict(primitives=visible,name=path.stem))
        else:
            for primitive in primitives:
                if primitive is None:ids.append(None);continue
                ids.append(len(self.doc['meshes']));self.doc['meshes'].append(dict(primitives=[primitive],name=path.stem))
        self.meshes[signature]=ids;return ids

    def node(self, data, parent=None):
        index=len(self.doc['nodes']);self.doc['nodes'].append(data)
        if parent is None:self.doc['scenes'][0]['nodes'].append(index)
        else:self.doc['nodes'][parent].setdefault('children',[]).append(index)
        return index

    def world_point(self, entity, point):
        seen=set()
        while entity:
            if entity['id'] in seen:raise ValueError('Hierarchy cycle')
            seen.add(entity['id']);t=trs(entity['components'].get('transform',{}))
            point=rotate(t['rotation'],[a*b for a,b in zip(point,t['scale'])]);point=[a+b for a,b in zip(point,t['translation'])]
            entity=self.entities.get(entity.get('parent'))
        return point

    def world_rotation(self, entity, q):
        while entity:
            q=qmul(rotation(entity['components'].get('transform',{}).get('rotation',[0,0,0])),q)
            entity=self.entities.get(entity.get('parent'))
        return q

    def collect_references(self, obj):
        if isinstance(obj,dict):
            for key,value in obj.items():
                if key in ('clip','directory','font','image','shader','fragment_shader','vertex_shader') and isinstance(value,str) and value:
                    # Missing optional references remain in metadata, never disappear silently.
                    path=(self.root/value).resolve()
                    if path.is_file():self.files.add(path)
                if key == "fragment_shaders" and isinstance(value,list):
                    for path in value:self.path(path)
                self.collect_references(value)
        elif isinstance(obj,list):
            for item in obj:self.collect_references(item)

    def build(self):
        nodes={}
        for e in self.scene['entities']:
            nodes[e['id']]=self.node(dict(name=e.get('name','Entity '+str(e['id'])),**trs(e['components'].get('transform',{})),extras=dict(bubbleEntity=e['id'])))
        roots=self.doc['scenes'][0]['nodes'];roots.clear()
        for e in self.scene['entities']:
            n=nodes[e['id']];parent=e.get('parent')
            if parent:self.doc['nodes'][nodes[parent]].setdefault('children',[]).append(n)
            else:roots.append(n)
            components=e['components'];renderer=components.get('renderer')
            if renderer and renderer.get('visible',True) and renderer.get('model'):
                meshes=self.mesh(renderer);instances=renderer.get('mesh_instances',[])
                if instances and len(instances)!=len(meshes):raise ValueError('Instance mesh count mismatch')
                for i,mesh in enumerate(meshes):
                    if mesh is None:continue
                    records=instances[i] if instances else []
                    if records:
                        for k,pose in enumerate(records):
                            self.node(dict(name=f"{e.get('name',e['id'])}/{i}/{k}",mesh=mesh,**trs(pose)),
                                n if renderer.get("local_instances") else None)
                    else:self.node(dict(name=f"mesh-{i}",mesh=mesh),n)
            for component,kind in [('directional_light','directional'),('point_light','point'),('spot_light','spot')]:
                if component not in components:continue
                light=components[component];entry=dict(type=kind,color=vector(light.get('color',[1,1,1]),3),intensity=max(0,light.get('intensity',1)),extras=dict(bubbleLight=light))
                if kind=='spot':entry['spot']=dict(innerConeAngle=math.radians(light.get('inner_cutoff',12.5)),outerConeAngle=math.radians(light.get('outer_cutoff',17.5)))
                lights=self.doc.setdefault('extensions',{}).setdefault('KHR_lights_punctual',dict(lights=[]))['lights'];index=len(lights);lights.append(entry)
                q=aim(light.get('direction',[0,-1,0])) if kind!='point' else [0,0,0,1]
                position=light.get('position',[0,0,0])
                self.node(dict(name=e.get('name',str(e['id']))+'/light',translation=position if kind=='point' else self.world_point(e,position),rotation=q if kind=='point' else self.world_rotation(e,q),extensions=dict(KHR_lights_punctual=dict(light=index))))
            if 'camera' in components:
                camera=components['camera'];perspective=dict(yfov=math.radians(camera.get('fov',75)),znear=camera.get('znear',.1),zfar=camera.get('zfar',300))
                entry=dict(type='perspective',perspective=perspective,extras=dict(bubbleCamera=camera))
                if camera.get('ortho'):
                    entry=dict(type='orthographic',orthographic=dict(xmag=camera.get('scale',5),ymag=camera.get('scale',5),znear=perspective['znear'],zfar=perspective['zfar']),extras=dict(bubbleCamera=camera))
                cameras=self.doc.setdefault('cameras',[]);index=len(cameras);cameras.append(entry)
                r=components.get('transform',{}).get('rotation',[0,0,0]);q=qmul(axis(1,-r[1]-90),axis(0,r[0]));q=self.world_rotation(self.entities.get(e.get('parent')),q)
                self.node(dict(name=e.get('name',str(e['id']))+'/camera',translation=self.world_point(e,[0,0,0]),rotation=q,camera=index))
        if 'extensions' in self.doc:self.doc['extensionsUsed']=['KHR_lights_punctual']
        self.collect_references(self.scene)
        if not self.binary:raise ValueError('No visible geometry to export')
        self.doc['buffers']=[dict(byteLength=len(self.binary))]
        encoded=json.dumps(self.doc,ensure_ascii=False,allow_nan=False,separators=(',',':')).encode();encoded+=b' '*(-len(encoded)%4)
        binary=bytes(self.binary);binary+=b'\0'*(-len(binary)%4)
        return struct.pack('<III',0x46546c67,2,12+8+len(encoded)+8+len(binary))+struct.pack('<I4s',len(encoded),b'JSON')+encoded+struct.pack('<I4s',len(binary),b'BIN\0')+binary


def export_scene(scene_path, output):
    scene_path=Path(scene_path).resolve();output=Path(output).resolve()
    if output.exists():raise ValueError('Output already exists; choose a new name')
    if output.suffix.lower() not in ('.glb','.zip'):raise ValueError('Output must be .glb or .zip')
    scene=json.loads(scene_path.read_text())
    if scene.get('format')!='bubble-engine-scene' or scene.get('version')!=1:raise ValueError('Unsupported scene format')
    exporter=Exporter(scene,scene_path.parent);glb=exporter.build()
    output.parent.mkdir(parents=True,exist_ok=True)
    fd,tmp=tempfile.mkstemp(prefix='.'+output.name,dir=output.parent)
    try:
        with os.fdopen(fd,'wb') as stream:
            if output.suffix.lower()=='.glb':stream.write(glb)
            else:
                with zipfile.ZipFile(stream,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as package:
                    package.writestr('scene.glb',glb);package.writestr('engine-scene.json',json.dumps(scene,ensure_ascii=False,indent=2))
                    mapping={}
                    for path in sorted(exporter.files):
                        data=path.read_bytes();name='resources/'+hashlib.sha256(data).hexdigest()[:16]+'/'+path.name
                        if name not in mapping.values():package.writestr(name,data)
                        mapping[str(path)]=name
                    helper=Path(__file__).with_name('blender_import.py')
                    package.writestr('blender_import.py',helper.read_bytes())
                    package.writestr('README.txt','Import scene.glb in Blender (glTF 2.0). Geometry and punctual lights are included.\nOptional helper: blender --python blender_import.py -- scene.glb scene.blend\nThe helper also sets an approximate ambient world, preserves engine metadata and packs images.\nEngine GLSL, fog, post-processing, physics, audio and scripted animation require separate recreation.\nObjects share mesh data. Use Blender Join on selected objects if you want a single editable object.\nresources/ contains source assets; resource-index.json maps original paths.\n')
                    package.writestr('resource-index.json' ,json.dumps(mapping,ensure_ascii=False,indent=2))
            stream.flush();os.fsync(stream.fileno())
        # Publish without replacing an existing file created by another writer.
        os.link(tmp,output)
    finally:
        Path(tmp).unlink(missing_ok=True)
    return dict(output=str(output),nodes=len(exporter.doc['nodes']),meshes=len(exporter.doc['meshes']),materials=len(exporter.doc['materials']),lights=len(exporter.doc.get('extensions',{}).get('KHR_lights_punctual',{}).get('lights',[])))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('scene',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args()
    try:print(json.dumps(export_scene(args.scene,args.output),ensure_ascii=False))
    except (ValueError,OSError,KeyError,TypeError,IndexError) as error:parser.exit(1,'Export failed: '+str(error)+'\n')
