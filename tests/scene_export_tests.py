import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile

spec=importlib.util.spec_from_file_location('scene_export',Path(__file__).resolve().parents[1]/'tools/scene_export.py')
export=importlib.util.module_from_spec(spec);spec.loader.exec_module(export)

class SceneExportTests(unittest.TestCase):
    def setUp(self):
        self.folder=tempfile.TemporaryDirectory();self.addCleanup(self.folder.cleanup);self.root=Path(self.folder.name)
        (self.root/'triangle.bmodel').write_text(json.dumps(dict(format='bengine-model',version=1,meshes=[dict(vertices=[[0,0,0,0,0],[1,0,0,1,0],[0,1,0,0,1]],faces=[[0,1,2]])])))
        self.scene=dict(format='bubble-engine-scene',version=1,entities=[dict(id=2,name='Parent',components=dict(transform=dict(position=[3,0,0],rotation=[0,90,0],scale=[2,1,1]))),dict(id=3,name='Plant',parent=2,components=dict(renderer=dict(model='triangle.bmodel',materials=[dict(albedo=[.2,.7,.1,1])]))),dict(id=4,components=dict(directional_light=dict(direction=[0,-1,0],color=[1,.8,.5],intensity=2),camera=dict(fov=65),transform=dict(position=[1,2,3],rotation=[0,0,0],scale=[1,1,1])))])
        self.path=self.root/'test.scene';self.save()
    def save(self):self.path.write_text(json.dumps(self.scene))
    def decode(self,path):
        data=path.read_bytes();magic,version,total=struct.unpack_from('<III',data);self.assertEqual((magic,version,total),(0x46546c67,2,len(data)))
        length,kind=struct.unpack_from('<I4s',data,12);self.assertEqual(kind,b'JSON');self.assertEqual(length%4,0)
        return json.loads(data[20:20+length])
    def test_geometry_hierarchy_camera_and_lights_survive_export(self):
        output=self.root/'test.glb';export.export_scene(self.path,output);d=self.decode(output)
        self.assertEqual(d['nodes'][0]['translation'],[3,0,0]);self.assertIn(1,d['nodes'][0]['children'])
        self.assertEqual(d['extensions']['KHR_lights_punctual']['lights'][0]['type'],'directional')
        self.assertAlmostEqual(d['cameras'][0]['perspective']['yfov'],65*export.math.pi/180)
        self.assertEqual(d['accessors'][0]['max'],[1,1,0]);self.assertIn('indices',d['meshes'][0]['primitives'][0])
    def test_instance_nodes_share_mesh_and_keep_world_transforms(self):
        self.scene['entities'][1]['components']['renderer']['mesh_instances']=[[dict(position=[8,1,0],rotation=[0,0,0],scale=[1,1,1]),dict(position=[10,1,0],rotation=[0,20,0],scale=[1,1,1])]];self.save()
        out=self.root/'instanced.glb';export.export_scene(self.path,out);d=self.decode(out)
        instances=[n for n in d['nodes'] if 'mesh' in n];self.assertEqual(len(instances),2);self.assertEqual(instances[0]['mesh'],instances[1]['mesh']);self.assertEqual(instances[0]['translation'],[8,1,0])
    def test_local_instances_are_children_of_the_renderer_node(self):
        renderer = self.scene['entities'][1]['components']['renderer']
        renderer.update(local_instances=True, mesh_instances=[[
            dict(position=[1, 0, 0], rotation=[0, 0, 0], scale=[1, 1, 1])
        ]])
        self.save()
        output = self.root / 'local.glb'
        export.export_scene(self.path, output)
        document = self.decode(output)
        child, = document['nodes'][1]['children']
        self.assertEqual(document['nodes'][child]['translation'], [1, 0, 0])
        self.assertNotIn(child, document['scenes'][0]['nodes'])

    def test_zip_contains_glb_resources_and_metadata_without_overwriting(self):
        output=self.root/'test.zip';export.export_scene(self.path,output)
        with zipfile.ZipFile(output) as z:
            self.assertIn('scene.glb',z.namelist());self.assertIn('engine-scene.json',z.namelist());self.assertTrue(json.loads(z.read('resource-index.json')))
        original=output.read_bytes()
        with self.assertRaises(ValueError):export.export_scene(self.path,output)
        self.assertEqual(output.read_bytes(),original)
    def test_invalid_scene_does_not_publish_partial_output(self):
        self.scene['entities'][1]['parent']=3;self.save();output=self.root/'invalid.glb'
        with self.assertRaises(ValueError):export.export_scene(self.path,output)
        self.assertFalse(output.exists())
    def test_visible_rig_is_rejected_instead_of_silently_discarding_pose(self):
        model=json.loads((self.root/'triangle.bmodel').read_text());model['rig']=dict(joints=[{}]);(self.root/'triangle.bmodel').write_text(json.dumps(model))
        with self.assertRaisesRegex(ValueError,'baking'):export.export_scene(self.path,self.root/'rig.glb')

if __name__=='__main__':unittest.main()
