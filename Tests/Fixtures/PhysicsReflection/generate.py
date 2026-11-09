import bpy
import json
import struct
from pathlib import Path
root = Path(__file__).resolve().parent
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
mesh = bpy.data.meshes.new("Asymmetric tetrahedron")
mesh.from_pydata([(0,0,0),(2,0,0),(0,3,0),(0,0,4)], [], [(0,2,1),(0,1,3),(0,3,2),(1,2,3)])
mesh.update()
obj = bpy.data.objects.new("Reflection regression", mesh)
bpy.context.collection.objects.link(obj)
bpy.context.view_layer.objects.active = obj
obj.select_set(True)
bpy.ops.wm.save_as_mainfile(filepath=str(root / "asymmetric.blend"))
with (root / "asymmetric.obj").open("w", encoding="utf-8") as output:
    for vertex in mesh.vertices:
        output.write("v %g %g %g\n" % tuple(vertex.co))
    for face in mesh.polygons:
        output.write("f " + " ".join(str(index + 1) for index in face.vertices) + "\n")
bpy.ops.export_scene.gltf(filepath=str(root / "asymmetric external.gltf"), export_format="GLTF_SEPARATE", use_selection=True)
document = json.loads((root / "asymmetric external.gltf").read_text(encoding="utf-8"))
chunk = json.dumps(document, separators=(",", ":")).encode("utf-8")
chunk += b" " * (-len(chunk) % 4)
(root / "external.glb").write_bytes(struct.pack("<IIIII", 0x46546C67, 2, 20 + len(chunk), len(chunk), 0x4E4F534A) + chunk)
