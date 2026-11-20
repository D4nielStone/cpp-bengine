"""Run inside Blender: blender --python blender_import.py -- scene.glb [scene.blend]."""
import json
from pathlib import Path
import struct
import sys


def main():
    import bpy
    args=sys.argv[sys.argv.index('--')+1:]
    if not 1<=len(args)<=2:raise ValueError('Expected scene.glb and optional output.blend')
    path=Path(args[0]).resolve();data=path.read_bytes()
    if struct.unpack_from('<I',data)[0]!=0x46546c67:raise ValueError('Expected GLB')
    length=struct.unpack_from('<I',data,12)[0];document=json.loads(data[20:20+length])
    bpy.ops.import_scene.gltf(filepath=str(path))
    original=document.get('extras',{}).get('bubbleScene',{})
    ambient=[.15,.15,.15]
    for entity in original.get('entities',[]):
        light=entity.get('components',{}).get('directional_light')
        if light:ambient=light.get('ambient',ambient);break
    world=bpy.context.scene.world
    if world is None:world=bpy.data.worlds.new('Bubble environment');bpy.context.scene.world=world
    world.use_nodes=True
    background=world.node_tree.nodes.get('Background')
    if background:
        background.inputs['Color'].default_value=(*ambient,1)
        background.inputs['Strength'].default_value=1
    bpy.context.scene['bubble_scene_metadata']=json.dumps(original,ensure_ascii=False)
    bpy.ops.file.pack_all()
    if len(args)==2:
        output=Path(args[1]).resolve()
        if output.exists():raise ValueError('Output already exists; choose a new name')
        bpy.ops.wm.save_as_mainfile(filepath=str(output))


if __name__=='__main__':main()
