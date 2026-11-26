"""Generate Bolt component properties that delegate directly to ScriptCore's native slots."""
from pathlib import Path
import json
import re

root = Path(__file__).resolve().parents[2]
core = root / "Funke-ScriptCore/Source/Funke"
out = root / "Funke-Engine/src/Scripting/Bolt"
glue = (root / "Funke-Engine/src/Scripting/ScriptGlue.hpp").read_text(encoding="utf-8")
slots = set(re.findall(r"\(\*(\w+)\)\(", glue))
sources = "\n".join(p.read_text(encoding="utf-8-sig") for p in sorted(core.rglob("*.cs")))
sources = re.sub(r"//[^\n]*|/\*.*?\*/", "", sources, flags=re.S)
enum_bodies = dict(re.findall(r"public enum (\w+)(?:\s*:\s*\w+)?\s*\{([^{}]*)\}", sources))
used_slots, used_enums = set(), set()
classes = {}
native_names = dict(re.findall(r'typeof\((?:Funke\.UI\.)?(\w+)\),\s*"([^"]+)"', sources))
widths = {"Vector2": 2, "Vector3": 3, "Vector4": 4, "Color": 4}
slot_args = dict(re.findall(r"\(\*(\w+)\)\(([^)]*)\);", glue))

def native(name, a, b="0ul"):
    used_slots.add(name)
    return f"Fk{name}({a}, {b})"

def getter(t, expr):
    if t == "string":
        return f"FunkeText.Read({expr})"
    if t == "int":
        return f"Reinterpret<int>((uint)({expr} & 4294967295ul))"
    if t == "float":
        return f"Reinterpret<float>((uint){expr})"
    if t == "bool":
        return f"({expr} != 0ul)"
    if t == "Vector2":
        return f"Vector2.FromBits({expr})"
    if t in ("Vector3", "Vector4", "Color"):
        return f"FunkeApiTypes.Decode{t}({expr}, FkReadExtra(0ul, 0ul))"
    return f"({t}){expr}"

def setter(t, name):
    if t == "string":
        return native(name, "ID", "FunkeText.Put(value)")
    if t == "float":
        return native(name, "ID", "(ulong)Reinterpret<uint>(value)")
    if t == "int":
        return native(name, "ID", "(ulong)Reinterpret<uint>(value)")
    if t == "bool":
        return native(name, "ID", "(value ? 1ul : 0ul)")
    if t == "Vector2":
        return native(name, "ID", "value.Bits()")
    if t in ("Vector3", "Vector4", "Color"):
        return f"FkWriteExtra(value.HighBits(), 0ul); {native(name, 'ID', 'value.LowBits()')}"
    return native(name, "ID", "(ulong)value")

for m in re.finditer(r"public (?:unsafe |sealed |partial )*class (\w+)\s*:\s*(?:Component|Collider[23]D|Joint2D)\b", sources):
    name = m[1]
    if name not in native_names or name == "Transform2D":
        continue
    end = sources.find("\n}", m.end())
    body = sources[m.end():end]
    members = []
    for prop in re.finditer(r"public (\w+) (\w+)\s*\{\s*get\s*=>\s*(?:\(\w+\))?InternalCalls\.(\w+)\(RequireComponent<"+name+r">\(\)\);\s*(?:set\s*=>\s*InternalCalls\.(\w+)\(RequireComponent<"+name+r">\(\),\s*(?:\(int\))?value(?: \?\? \"\")?\);\s*)?\}", body):
        t, field, read, write = prop.groups()
        if t == "string":
            read += "Buffer"
        if t not in ("string", "float", "int", "bool", "uint", "ulong", "Vector2", "Vector3", "Vector4", "Color") and t not in enum_bodies:
            continue
        if read not in slots or (write and write not in slots):
            continue
        # Compound buffer APIs such as Camera's packed settings need dedicated wrappers.
        declaration = next(l for l in glue.splitlines() if "(*"+read+")(" in l)
        if t in ("Vector2", "Vector3", "Vector4", "Color"):
            width = {"Vector2":2,"Vector3":3,"Vector4":4,"Color":4}[t]
            if declaration.count("float*") != width:
                continue
        elif t == "string":
            if not re.fullmatch(r"uint64_t \w+, char\* \w+, int \w+", slot_args[read]): continue
            if write and not re.fullmatch(r"uint64_t \w+, const char\* \w+", slot_args[write]): continue
        elif not re.search(r"\(\*"+read+r"\)\(uint64_t \w+\);", declaration) or "void" in declaration:
            continue
        if write and t not in ("string", "Vector2", "Vector3", "Vector4", "Color"):
            if not re.search(r"\(\*"+write+r"\)\(uint64_t \w+, (?:int|float|uint32_t|uint64_t) \w+\);", glue): continue
        if t in enum_bodies: used_enums.add(t)
        value = getter(t, native(read, "ID"))
        members.append(f"    {t} {field} {{ get {{ return {value}; }} set {{ {setter(t, write)}; }} }}" if write else f"    {t} {field} => {value};")
    for prop in re.finditer(r"public (Vector[234]|Color) (\w+)\s*\{\s*get\s*\{"
                            r"\s*ulong (\w+)\s*=\s*RequireComponent<" + name + r">\(\);"
                            r"\s*InternalCalls\.(\w+)\(\3, ((?:out float \w+(?:, )?)+)\);"
                            r"\s*return new \w+\(([\w, ]+)\);\s*\}"
                            r"\s*(?:set\s*=>\s*InternalCalls\.(\w+)\(RequireComponent<" + name
                            + r">\(\), ([\w., ]+)\);\s*)?\}", body):
        t, field, _, read, outputs, result, write, inputs = prop.groups()
        if slot_args.get(read, "").count("float*") != widths[t]: continue
        if result.split(", ") != re.findall(r"out float (\w+)", outputs): continue
        if write and (write not in slots or inputs != ", ".join("value." + c for c in "XYZW"[:widths[t]])): continue
        value = getter(t, native(read, "ID"))
        members.append(f"    {t} {field} {{ get {{ return {value}; }} set {{ {setter(t, write)}; }} }}" if write else f"    {t} {field} => {value};")

    for t, field, read in re.findall(r"public (Vector[234]|Color|float|int|bool|ulong) (\w+)\s*=>\s*InternalCalls\.(\w+)\(RequireComponent<"+name+r">\(\)\);", body):
        if read not in slots: continue
        if t in widths and slot_args[read].count("float*") != widths[t]: continue
        if t not in widths and not re.fullmatch(r"uint64_t \w+", slot_args[read]): continue
        members.append(f"    {t} {field} => {getter(t, native(read, 'ID'))};")

    for field, action in re.findall(r"public void (\w+)\(\)\s*=>\s*InternalCalls\.(\w+)\(RequireComponent<"+name+r">\(\)\);", body):
        if re.fullmatch(r"uint64_t \w+", slot_args.get(action, "")):
            members.append(f"    void {field}() {{ {native(action, 'ID')}; }}")
    for t, field, write in re.findall(r"public (bool|float|int) (\w+)\s*\{\s*set\s*=>\s*InternalCalls\.(\w+)\(RequireComponent<"+name+r">\(\), value\);\s*\}", body):
        if re.fullmatch(r"uint64_t \w+, (?:int|float) \w+", slot_args.get(write, "")):
            members.append(f"    {t} {field} {{ set {{ {setter(t, write)}; }} }}")

    for field, parameter, action in re.findall(r"public void (\w+)\(int (\w+)\)\s*=>\s*InternalCalls\.(\w+)\(RequireComponent<"+name+r">\(\), \2\);", body):
        if re.fullmatch(r"uint64_t \w+, int \w+", slot_args.get(action, "")):
            members.append(f"    void {field}(int {parameter}) {{ {native(action, 'ID', f'(ulong)Reinterpret<uint>({parameter})')}; }}")

    for field, action, opcode in re.findall(r"public void (\w+)\(\)\s*=>\s*InternalCalls\.(\w+)\(RequireComponent<"+name+r">\(\), (\d+)\);", body):
        if re.fullmatch(r"uint64_t \w+, int \w+", slot_args.get(action, "")):
            members.append(f"    void {field}() {{ {native(action, 'ID', opcode + 'ul')}; }}")
    for field, read, write in re.findall(r"public Entity\? (\w+)\s*\{\s*get\s*\{\s*ulong id = InternalCalls\.(\w+)\(RequireComponent<"
                                          + name + r">\(\)\);\s*return id == 0 \? null : new Entity\(id\);\s*\}\s*set => InternalCalls\.(\w+)\(RequireComponent<"
                                          + name + r">\(\), value\?\.ID \?\? 0\);\s*\}", body):
        if read not in slots or write not in slots: continue
        members.append(f"    Funke.Entity? {field} {{ get {{ return Funke.Entity.FromId({native(read, 'ID')}); }} "
                       f"set {{ ulong id = 0ul; if (value != null) id = value.Value.ID; {native(write, 'ID', 'id')}; }} }}")
    asset_types = r"Texture|Audio|Font|Material|Mesh|AnimationClip|AnimatorController"
    for prop in re.finditer(r"public (" + asset_types + r")\?? (\w+)\s*\{\s*get\s*(?:=>\s*(?:Funke\.)?\1\.FromAssetUUID\(InternalCalls\.(\w+)\(RequireComponent<"
                            + name + r">\(\)\)\);|\{\s*ulong (\w+) = InternalCalls\.(\w+)\(RequireComponent<"
                            + name + r">\(\)\);\s*return (?:Funke\.)?\1\.FromAssetUUID\(\4\)!?;\s*\})\s*set\s*=>\s*InternalCalls\.(\w+)\(RequireComponent<"
                            + name + r">\(\), value\?\.UUID \?\? 0\);\s*\}", body):
        t, field, direct_read, _, block_read, write = prop.groups()
        read = direct_read or block_read
        if not re.fullmatch(r"uint64_t \w+", slot_args.get(read, "")): continue
        if not re.fullmatch(r"uint64_t \w+, uint64_t \w+", slot_args.get(write, "")): continue
        members.append(f"    Funke.{t}? {field} {{ get {{ return Funke.{t}.FromAssetUUID({native(read, 'ID')}); }} "
                       f"set {{ ulong guid = 0ul; if (value != null) guid = value.Value.UUID; {native(write, 'ID', 'guid')}; }} }}")
    if name == "Transform3D":
        members.append("    Vector3 LocalPosition { get { return Position; } set { Position = value; } }")
    classes[name] = members

for component in ("MeshRenderer", "MeshCollider3D"):
    read = native(component + "_GetCustomMesh", "ID")
    write = native(component + "_SetCustomMesh", "ID", "guid")
    classes[component].append(f"""    Funke.Mesh? CustomMesh {{
        get {{ return Funke.Mesh.FromAssetUUID({read}); }}
        set {{
            ulong guid = 0ul;
            if (value != null) guid = value.Value.UUID;
            {write};
        }}
    }}""")

used_slots.update(("Animator_SetParameter", "Animator_GetParameter", "Animator_HasParameter", "Animator_Play", "Animator_GetStateBuffer"))
classes["Animator"].append("""    private void SetParameter(string name, int kind, float value) {
        FunkeText.Put(name);
        FkWriteExtra((ulong)kind, 0ul);
        FkAnimator_SetParameter(ID, (ulong)Reinterpret<uint>(value));
    }
    private float GetParameter(string name, int kind) {
        FunkeText.Put(name);
        return Reinterpret<float>((uint)FkAnimator_GetParameter(ID, (ulong)kind));
    }
    void SetFloat(string name, float value) { SetParameter(name, 0, value); }
    float GetFloat(string name) { return GetParameter(name, 0); }
    void SetInteger(string name, int value) { SetParameter(name, 1, (float)value); }
    int GetInteger(string name) { return (int)GetParameter(name, 1); }
    void SetBool(string name, bool value) { SetParameter(name, 2, value ? 1.0f : 0.0f); }
    bool GetBool(string name) { return GetParameter(name, 2) != 0.0f; }
    void SetTrigger(string name) { SetParameter(name, 3, 1.0f); }
    void ResetTrigger(string name) { SetParameter(name, 4, 0.0f); }
    bool HasParameter(string name) { return FkAnimator_HasParameter(ID, FunkeText.Put(name)) != 0ul; }
    bool Play(string state, float normalizedTime = 0.0f) {
        FunkeText.Put(state);
        return FkAnimator_Play(ID, Vector2(normalizedTime, -1.0f).Bits()) != 0ul;
    }
    bool CrossFade(string state, float duration) {
        FunkeText.Put(state);
        return FkAnimator_Play(ID, Vector2(0.0f, Mathf.Max(0.0f, duration)).Bits()) != 0ul;
    }
    string CurrentStateName => FunkeText.Read(FkAnimator_GetStateBuffer(ID, 0ul));
    string NextStateName => FunkeText.Read(FkAnimator_GetStateBuffer(ID, 1ul));""")

for t, field in (("float", "PixelsPerUnit"), ("bool", "FlipX"), ("bool", "FlipY")):
    read = getter(t, f"FkSpriteRenderer_Get{field}(ID, 0ul)")
    encoded = "(ulong)Reinterpret<uint>(value)" if t == "float" else "value ? 1ul : 0ul"
    classes["SpriteRenderer"].append(f"    {t} {field} {{ get {{ return {read}; }} set {{ FkSpriteRenderer_Set{field}(ID, {encoded}); }} }}")

used_slots.update(("Dropdown_GetOptionBuffer", "Dropdown_SetOption", "Dropdown_AddOption", "Dropdown_RemoveOption", "Dropdown_ClearOptions"))
classes["Dropdown"].append("""    string GetOption(int index) { return FunkeText.Read(FkDropdown_GetOptionBuffer(ID, (ulong)Reinterpret<uint>(index))); }
    string SelectedOption {
        get {
            int count = OptionCount;
            return count == 0 ? "" : GetOption(Mathf.Clamp(SelectedIndex, 0, count - 1));
        }
    }
    void SetOption(int index, string text, bool ignoreEvent = true) {
        FunkeText.Put(text);
        FkDropdown_SetOption(ID, (ulong)Reinterpret<uint>(index));
    }
    void AddOption(string text) { FkDropdown_AddOption(ID, FunkeText.Put(text)); }
    void RemoveOption(int index) { FkDropdown_RemoveOption(ID, (ulong)Reinterpret<uint>(index)); }
    void ClearOptions() { FkDropdown_ClearOptions(ID, 0ul); }
    void SetOptions(string... options) {
        ClearOptions();
        for (int i = 0; i < options.Length; i++) AddOption(options[i]);
    }""")
for slider in ("Slider", "CircularSlider"):
    classes[slider].append("""    float NormalizedValue {
        get {
            float min = MinValue;
            float range = MaxValue - min;
            return range != 0.0f ? (Value - min) / range : 0.0f;
        }
    }""")
classes["Button"].append("""    bool WasClicked {
        get {
            Funke.Entity owner = FunkeApiTypes.MakeEntity(ID);
            Interactable? target = owner.GetComponent<Interactable>();
            if (target == null) return false;
            return target.Value.IsClicked;
        }
    }""")
for field in ("Rotation", "LocalRotation"):
    read = getter("float", native("RectTransform_Get" + field, "ID"))
    write = native("RectTransform_Set" + field, "ID", "(ulong)Reinterpret<uint>(value * Mathf.Deg2Rad)")
    classes["RectTransform"].append(f"    float {field} {{ get {{ return {read} * Mathf.Rad2Deg; }} set {{ {write}; }} }}")
used_slots.update(("Camera3D_ScreenPointToRay", "Camera3D_WorldToScreenPoint"))
classes["Camera3D"].append("""    private static bool IsFinite(float value) { return Mathf.Abs(value) < Mathf.Infinity; }
    Ray ScreenPointToRay(Vector2 point) {
        Ray ray;
        if (TryScreenPointToRay(point, ray)) return ray;
        return Ray(Vector3.Zero, Vector3.Forward);
    }
    bool TryScreenPointToRay(Vector2 point, Ray& ray) {
        ray = default;
        if (!IsFinite(point.X) || !IsFinite(point.Y)) return false;
        if (FkCamera3D_ScreenPointToRay(ID, point.Bits()) == 0ul) return false;
        Vector2 a = Vector2.FromBits(FkReadExtra(0ul, 0ul));
        Vector2 b = Vector2.FromBits(FkReadExtra(1ul, 0ul));
        Vector2 c = Vector2.FromBits(FkReadExtra(2ul, 0ul));
        Vector3 direction = Vector3(b.Y, c.X, c.Y);
        if (!IsFinite(a.X) || !IsFinite(a.Y) || !IsFinite(b.X) ||
            !IsFinite(direction.LengthSquared()) || direction.LengthSquared() <= Mathf.Epsilon * Mathf.Epsilon) return false;
        ray = Ray(Vector3(a.X, a.Y, b.X), direction);
        return true;
    }
    bool WorldToScreenPoint(Vector3 world, Vector3& screen) {
        FkWriteExtra(world.HighBits(), 0ul);
        bool visible = FkCamera3D_WorldToScreenPoint(ID, world.LowBits()) != 0ul;
        screen = Vector3.FromParts(FkReadExtra(0ul, 0ul), FkReadExtra(1ul, 0ul));
        return visible;
    }""")

used_enums.update(("RigidbodyConstraints2D", "ForceMode3D"))
classes["Rigidbody2D"].append("""    RigidbodyConstraints2D Constraints {
        get {
            int mask = (FreezePositionX ? 1 : 0) | (FreezePositionY ? 2 : 0) | (FreezeRotation ? 4 : 0);
            return (RigidbodyConstraints2D)mask;
        }
        set {
            int mask = (int)value;
            FreezePositionX = (mask & 1) != 0;
            FreezePositionY = (mask & 2) != 0;
            FreezeRotation = (mask & 4) != 0;
        }
    }""")
for kind, (action, angular, param) in enumerate((("ApplyForce", "ApplyTorque", "force"), ("ApplyImpulse", "ApplyAngularImpulse", "impulse"))):
    used_slots.update(("Rigidbody2D_" + action, "Rigidbody2D_ApplyAtPoint", "Rigidbody2D_ApplyAngular"))
    classes["Rigidbody2D"].append(f"""    void {action}(Vector2 {param}, bool wake = true) {{
        FkWriteExtra(wake ? 1ul : 0ul, 0ul);
        FkRigidbody2D_{action}(ID, {param}.Bits());
    }}
    void {action}AtPoint(Vector2 {param}, Vector2 worldPoint, bool wake = true) {{
        FkWriteExtra(worldPoint.Bits(), 0ul);
        FkWriteExtra({kind}ul, 1ul);
        FkWriteExtra(wake ? 1ul : 0ul, 2ul);
        FkRigidbody2D_ApplyAtPoint(ID, {param}.Bits());
    }}
    void {angular}(float amount, bool wake = true) {{
        FkWriteExtra({kind}ul, 0ul);
        FkWriteExtra(wake ? 1ul : 0ul, 1ul);
        FkRigidbody2D_ApplyAngular(ID, (ulong)Reinterpret<uint>(amount));
    }}""")

used_slots.update(("Rigidbody3D_GetVelocity", "Rigidbody3D_SetVelocity", "Rigidbody3D_IsSleeping", "Rigidbody3D_SetSleeping", "Rigidbody3D_AddForce"))
for kind, field in enumerate(("LinearVelocity", "AngularVelocity")):
    classes["Rigidbody3D"].append(f"""    Vector3 {field} {{
        get {{ return Vector3.FromParts(FkRigidbody3D_GetVelocity(ID, {kind}ul), FkReadExtra(0ul, 0ul)); }}
        set {{
            FkWriteExtra(value.HighBits(), 0ul);
            FkWriteExtra({kind}ul, 1ul);
            FkRigidbody3D_SetVelocity(ID, value.LowBits());
        }}
    }}""")
classes["Rigidbody3D"].append("""    bool IsSleeping => FkRigidbody3D_IsSleeping(ID, 0ul) != 0ul;
    void WakeUp() { FkRigidbody3D_SetSleeping(ID, 0ul); }
    void Sleep() { FkRigidbody3D_SetSleeping(ID, 1ul); }
    private void Apply(int kind, ForceMode3D mode, Vector3 value, Vector3 point) {
        FkWriteExtra(value.LowBits(), 0ul);
        FkWriteExtra(Vector2(value.Z, point.X).Bits(), 1ul);
        FkWriteExtra(Vector2(point.Y, point.Z).Bits(), 2ul);
        FkRigidbody3D_AddForce(ID, (ulong)kind | ((ulong)mode << 32));
    }
    void AddForce(Vector3 force, ForceMode3D mode = ForceMode3D.Force) { Apply(0, mode, force, Vector3.Zero); }
    void AddForceAtPosition(Vector3 force, Vector3 position, ForceMode3D mode = ForceMode3D.Force) { Apply(1, mode, force, position); }
    void AddTorque(Vector3 torque, ForceMode3D mode = ForceMode3D.Force) { Apply(2, mode, torque, Vector3.Zero); }""")

used_slots.add("ParticleSystem3D_GetState")
classes["ParticleSystem3D"].append("""    bool IsEmitting => (FkParticleSystem3D_GetState(ID, 0ul) & 1ul) != 0ul;
    bool IsPlaying => IsEmitting;
    int ParticleCount => (int)(FkParticleSystem3D_GetState(ID, 0ul) >> 32);""")
post_processing = (core / "Graphics/PostProcessing2D.cs").read_text(encoding="utf-8-sig")
effects = re.findall(r"public struct (\w+)Settings\s*\{(.*?)internal PostProcessingInterop\.", post_processing, re.S)
effect_ids = dict(re.findall(r"\b(\w+) = (\d+),", (core / "Interop/PostProcessingInterop.cs").read_text()))
for effect, settings in effects:
    declarations = []
    for t, field in re.findall(r"public (bool|int|float|Color|Vector2) (\w+);", settings):
        if t == "bool":
            backing = field[0].lower() + field[1:]
            declarations.append(f"        private int {backing};\n        bool {field} {{ get {{ return {backing} != 0; }} set {{ {backing} = value ? 1 : 0; }} }}")
        else:
            declarations.append(f"        {t} {field};")
    default = re.search(r"Default => new\(\)\s*\{(.*?)\};", settings, re.S)[1]
    defaults = re.findall(r"(\w+) = (new (?:Color|Vector2)\([^)]*\)|[\d.f]+)", default)
    initializers = "\n".join(f"                result.{field} = {value.replace('new ', 'Funke.')};" for field, value in defaults)
    name = effect + "Settings"
    classes["PostProcessing2D"].append(f"""    struct {name} {{
    public:
{chr(10).join(declarations)}
        static {name} Default {{
            get {{
                {name} result;
{initializers}
                return result;
            }}
        }}
    }}
    {name} {effect} {{
        get {{ return ReadEffect<{name}>({effect_ids[effect]}ul); }}
        set {{ WriteEffect<{name}>({effect_ids[effect]}ul, value); }}
    }}""")
used_slots.update(("PostProcessing2D_GetEffect", "PostProcessing2D_SetEffect"))
classes["PostProcessing2D"].append("""    private T ReadEffect<T>(ulong effect) {
        T result;
        if (FkPostProcessing2D_GetEffect(ID, effect | ((ulong)sizeof(T) << 32)) == 0ul) return default;
        uint* words = (uint*)&result;
        for (int i = 0; i < (int)sizeof(T) / 4; i++) {
            ulong pair = FkReadExtra((ulong)(i / 2), 0ul);
            words[i] = (uint)((pair >> ((i % 2) * 32)) & 4294967295ul);
        }
        return result;
    }
    private void WriteEffect<T>(ulong effect, T value) {
        uint* words = (uint*)&value;
        int count = (int)sizeof(T) / 4;
        for (int i = 0; i < count; i += 2) {
            ulong pair = (ulong)words[i];
            if (i + 1 < count) pair = pair | ((ulong)words[i + 1] << 32);
            FkWriteExtra(pair, (ulong)(i / 2));
        }
        FkPostProcessing2D_SetEffect(ID, effect | ((ulong)sizeof(T) << 32));
    }""")

for camera in ("Camera2D", "Camera3D"):
    main = native(camera + "_GetMainEntity", "0ul")
    classes[camera].append(f"""    static {camera}? Main {{
        get {{
            ulong id = {main};
            if (id == 0ul) return null;
            return FunkeApiTypes.MakeEntity(id).GetComponent<{camera}>();
        }}
    }}""")

used_slots.update(("Camera2D_GetFrustum", "Camera2D_ScreenToWorld"))
classes["Camera2D"].append("""    Vector2 ScreenToWorld(Vector2 screenPos) {
        return Vector2.FromBits(FkCamera2D_ScreenToWorld(ID, screenPos.Bits()));
    }
    Vector2 WorldToScreen(Vector2 worldPos) {
        float width = ViewportWidth;
        float height = ViewportHeight;
        if (width <= 0.0f || height <= 0.0f) return Vector2.Zero;
        float halfHeight = OrthographicSize * Zoom;
        float halfWidth = halfHeight * (width / height);
        if (halfWidth <= Mathf.Epsilon || halfHeight <= Mathf.Epsilon) return Vector2.Zero;
        Funke.Entity owner = FunkeApiTypes.MakeEntity(ID);
        Transform2D? transform = owner.GetComponent<Transform2D>();
        if (transform == null) throw System.InvalidOperationException("Camera2D requires a Transform2D.");
        Vector2 delta = worldPos - transform.Value.Position;
        float angle = transform.Value.Rotation * Mathf.Deg2Rad;
        float c = Mathf.Cos(angle);
        float s = Mathf.Sin(angle);
        return Vector2((1.0f + (c * delta.X + s * delta.Y) / halfWidth) * 0.5f * width,
                       (1.0f - (c * delta.Y - s * delta.X) / halfHeight) * 0.5f * height);
    }
    AABB Frustum {
        get {
            Vector2 min = Vector2.FromBits(FkCamera2D_GetFrustum(ID, 0ul));
            return AABB(min, Vector2.FromBits(FkReadExtra(0ul, 0ul)));
        }
    }
    Vector2 FrustumSize => Frustum.Size;""")

# The C# wrappers read the entire native settings array before changing a field.
# Each tuple follows its binding's float layout; compound values occupy consecutive slots.
classes["PolygonCollider2D"].append("""    static const int MinVertices = 3;
    static const int MaxVertices = 8;
    static const int MaxPathPoints = 16384;
    void SetPoints(Vector2[] points) { SetPoints(System.ReadOnlySpan<Vector2>(points)); }
    void SetPoints(System.ReadOnlySpan<Vector2> points) {
        if (points.Length < MinVertices || points.Length > MaxPathPoints) {
            Log.Error("PolygonCollider2D.SetPoints requires 3..16384 points.");
            return;
        }
        FkPolygonRequest req;
        req.Operation = 0;
        req.Count = points.Length;
        Vector2[] owned = points.ToArray();
        FkPolygonPoints(ID, &req, (void*)&owned[0]);
    }
    Vector2[] GetPath(int index) {
        FkPolygonRequest req;
        req.Operation = 1;
        req.Index = index;
        int count = (int)FkPolygonPoints(ID, &req, null);
        Vector2[] result = new Vector2[count];
        if (count > 0) {
            req.Count = count;
            FkPolygonPoints(ID, &req, (void*)&result[0]);
        }
        return result;
    }
    void SetPaths(Vector2[]... paths) {
        int total = 0;
        int[] counts = new int[paths.Length];
        for (int i = 0; i < paths.Length; i++) {
            counts[i] = paths[i].Length;
            total += counts[i];
        }
        Vector2[] points = new Vector2[total];
        int offset = 0;
        for (int i = 0; i < paths.Length; i++)
            for (int j = 0; j < counts[i]; j++) points[offset++] = paths[i][j];
        FkPolygonRequest req;
        req.Operation = 2;
        req.Count = total;
        req.Paths = paths.Length;
        if (paths.Length > 0) req.Counts = &counts[0];
        void* data = null;
        if (total > 0) data = &points[0];
        FkPolygonPoints(ID, &req, data);
    }
    int GetWorldPoints(Vector2[]& destination) { return GetWorldPoints(System.Span<Vector2>(destination)); }
    int GetWorldPoints(System.Span<Vector2> destination) {
        if (destination.Length == 0) return 0;
        FkPolygonRequest req;
        req.Operation = 3;
        req.Count = destination.Length < MaxVertices ? destination.Length : MaxVertices;
        return (int)FkPolygonPoints(ID, &req, (void*)&destination[0]);
    }""")

from bolt_scene_api import extend as extend_scene_api, component_declaration
extend_scene_api(classes, used_slots)
from bolt_ui_events import extend as extend_ui_events
extend_ui_events(classes, used_slots, root / "Funke-BoltScriptCore/Source/Funke")

packed_bindings = []
def packed(component, read, write, count, fields, kind=-1):
    index = 0
    for spec in fields.split():
        t, field = spec.split(":")
        writable = not field.endswith("?")
        field = field.rstrip("?")
        width = widths.get(t, 1)
        if t in enum_bodies:
            used_enums.add(t)
        prefix = component + "_"
        args = f"{count}, {index}, {width}, {kind}"
        packed_bindings.append(f"FUNKE_BOLT_PACKED(Get, {prefix}{field}, {read}, nullptr, {args})")
        expr = f"FkGet{prefix}{field}(ID, 0ul)"
        if t in widths or t == "float":
            value = getter(t, expr)
        else:
            scalar = getter("float", expr)
            value = scalar + " != 0.0f" if t == "bool" else f"({t})(int){scalar}"
        if writable:
            assert write, (component, field)
            packed_bindings.append(f"FUNKE_BOLT_PACKED(Set, {prefix}{field}, {read}, &NativeBindings::{write}, {args})")
            if width > 1:
                bits = "value.Bits()" if width == 2 else "value.LowBits()"
            else:
                number = "value" if t == "float" else "(value ? 1.0f : 0.0f)" if t == "bool" else "(float)(int)value"
                bits = f"(ulong)Reinterpret<uint>({number})"
            extra = "FkWriteExtra(value.HighBits(), 0ul); " if width > 2 else ""
            classes[component].append(f"    {t} {field} {{ get {{ return {value}; }} set {{ {extra}FkSet{prefix}{field}(ID, {bits}); }} }}")
        else:
            classes[component].append(f"    {t} {field} => {value};")
        index += width
    assert index <= count, (component, index, count)

packed("Camera3D", "Camera3D_GetSky", "Camera3D_SetSky", 23,
       "CameraBackground:Background Color:SkyTopColor Color:SkyHorizonColor Color:SkyGroundColor float:SunSize "
       "float:SkyAmbient bool:AmbientFromSkybox bool:FogEnabled Color:FogColor float:FogStart float:FogEnd")
packed("PointLight", "PointLight_Get", "PointLight_Set", 6, "Color:Color float:Intensity float:Range")
packed("SpotLight", "SpotLight_Get", "SpotLight_Set", 8, "Color:Color float:Intensity float:Range float:InnerAngle float:OuterAngle")
packed("TextRenderer", "TextRenderer_GetEffects", "TextRenderer_SetEffects", 11,
       "float:OutlineWidth Color:OutlineColor Vector2:ShadowOffset Color:ShadowColor")
packed("ParticleSystem3D", "ParticleSystem3D_Get", "ParticleSystem3D_Set", 21,
       "float:LifeTime float:Speed float:Size float:EmitOverTime Color:Color bool:UseGravity Vector3:Gravity "
       "int:MaxParticles bool:PlayOnAwake ParticleBlendMode3D:BlendMode ParticleSpace3D:SimulationSpace "
       "ParticleShape3D:Shape float:Radius float:ConeAngle float:Drag float:AngularVelocity")
packed("Rigidbody2D", "Rigidbody2D_GetDynamics", "Rigidbody2D_SetDynamics", 8,
       "float:LinearDamping float:AngularDamping bool:IsBullet float:Inertia? Vector2:LocalCenterOfMass? Vector2:WorldCenterOfMass?")
packed("Rigidbody3D", "Rigidbody3D_Get", "Rigidbody3D_Set", 7,
       "BodyType:BodyType float:Mass float:LinearDrag float:AngularDrag float:GravityScale bool:ContinuousCollision RigidbodyConstraints3D:Constraints")
packed("Image", "Image_GetSlicing", "Image_SetSlicing", 6, "ImageType:Type Vector4:Border float:BorderScale")
packed("AudioSource", "AudioSource_GetSpatial", "AudioSource_SetSpatial", 7,
       "bool:Spatial float:MinDistance float:MaxDistance AudioRolloff:Rolloff float:Panning float:DopplerLevel bool:Hrtf")
packed("Animator", "Animator_GetCulling", "Animator_SetCulling", 4,
       "AnimationCullingMode:CullingMode AnimationDistanceRate:DistanceRate float:HalfRateDistance float:QuarterRateDistance")
packed("Animator", "Animator_GetPlayback", None, 3,
       "float:NormalizedTime? bool:IsInTransition? float:TransitionProgress?")
packed("TerrainRenderer2D", "TerrainRenderer2D_Get", "TerrainRenderer2D_Set", 19,
       "Color:FillColor Vector2:FillTileSize TerrainFillMapping:FillMapping float:FillDepth Color:EdgeColor "
       "float:EdgeThickness float:EdgeOffset float:EdgeTileLength int:SortingLayer int:SortingOrder int:EdgeSortingOrder float:EdgeTextureOffset")
packed("SkinnedMeshRenderer", "SkinnedMeshRenderer_Get", "SkinnedMeshRenderer_Set", 15,
       "float:Speed bool:Loop bool:PlayOnAwake float:Time bool:IsPlaying? bool:Lit bool:CastShadows Color:Color "
       "AnimationCullingMode:CullingMode AnimationDistanceRate:DistanceRate float:HalfRateDistance float:QuarterRateDistance")
for kind, (component, count, settings, state) in enumerate((
    ("RevoluteJoint2D", 15, "bool:EnableLimit float:LowerAngle float:UpperAngle bool:EnableMotor float:MotorSpeed "
     "float:MaxMotorTorque bool:EnableSpring float:SpringHertz float:SpringDampingRatio",
     "float:JointAngle? float:JointSpeed? float:MotorTorque?"),
    ("PrismaticJoint2D", 17, "Vector2:Axis bool:EnableLimit float:LowerTranslation float:UpperTranslation bool:EnableMotor "
     "float:MotorSpeed float:MaxMotorForce bool:EnableSpring float:SpringHertz float:SpringDampingRatio",
     "float:JointTranslation? float:JointSpeed? float:MotorForce?"),
    ("WeldJoint2D", 10, "float:LinearHertz float:AngularHertz float:LinearDampingRatio float:AngularDampingRatio", "")
)):
    packed(component, "Joint2D_Get", "Joint2D_Set", count,
           "Vector2:Anchor Vector2:ConnectedAnchor bool:AutoConfigureConnectedAnchor bool:CollideConnected " + settings, kind)
    packed(component, "Joint2D_GetState", None, 7,
           "bool:IsCreated? Vector2:ReactionForce? float:ReactionTorque? " + state, kind)
    used_slots.update(("Joint2D_GetConnectedBody", "Joint2D_SetConnectedBody"))
    classes[component].append(f"""    Entity? ConnectedBody {{
        get {{ return Funke.Entity.FromId(FkJoint2D_GetConnectedBody(ID, {kind}ul)); }}
        set {{
            ulong body = 0ul;
            if (value != null) body = value.Value.ID;
            FkWriteExtra({kind}ul, 0ul);
            FkJoint2D_SetConnectedBody(ID, body);
        }}
    }}""")

for kind, component in enumerate(("BoxCollider2D", "CircleCollider2D", "PolygonCollider2D")):
    for t, field, slot in (("bool", "IsSensor", "Sensor"), ("ulong", "Layer", "Layer"),
                           ("ulong", "CollisionMask", "CollisionMask"), ("float", "Friction", "Friction"),
                           ("float", "Bounciness", "Bounciness"), ("int", "GroupIndex", "GroupIndex"),
                           ("float", "Density", "Density")):
        read = getter(t, native("Collider2D_Get" + slot, "ID", f"{kind}ul"))
        write = setter(t, "Collider2D_Set" + slot)
        classes[component].append(f"    {t} {field} {{ get {{ return {read}; }} "
                                  f"set {{ FkWriteExtra({kind}ul, 0ul); {write}; }} }}")

for kind, (component, shape) in enumerate((
    ("BoxCollider3D", "Vector3:Size"), ("SphereCollider3D", "float:Radius"),
    ("CapsuleCollider3D", "float:Radius float:Height CapsuleDirection:Direction"),
    ("MeshCollider3D", "bool:UseMeshRenderer PrimitiveMesh:Mesh bool:Convex")
)):
    packed(component, "Collider3D_Get", "Collider3D_Set", 9,
           "Vector3:Center bool:IsTrigger float:Friction float:Bounciness " + shape, kind)

for component, base, assets in (
    ("Animator", "Animator", (("AnimatorController", "Controller"), ("AnimationClip", "Clip"))),
    ("TerrainRenderer2D", "TerrainRenderer2D", (("Texture", "FillTexture"), ("Texture", "EdgeTexture"))),
    ("SkinnedMeshRenderer", "SkinnedMeshRenderer", (("Mesh", "Model"), ("Material", "Material"))),
):
    suffix = "Texture" if component == "TerrainRenderer2D" else "Asset"
    for index, (t, field) in enumerate(assets):
        read = native(base + "_Get" + suffix, "ID", f"{index}ul")
        write = native(base + "_Set" + suffix, "ID", "guid")
        classes[component].append(f"""    Funke.{t}? {field} {{
        get {{ return Funke.{t}.FromAssetUUID({read}); }}
        set {{
            ulong guid = 0ul;
            if (value != null) guid = value.Value.UUID;
            FkWriteExtra({index}ul, 0ul);
            {write};
        }}
    }}""")

def save(name, content):
    path = out / name
    if not path.exists() or path.read_text(encoding="utf-8") != content:
        path.write_text(content, encoding="utf-8")

bolt_root = root / "Funke-BoltScriptCore"
bolt_source = bolt_root / "Source/Funke"
core_header = "import System\nimport Math\nimport Type\nimport System.Text\nnamespace Funke;"
ui_header = "import Funke\nnamespace Funke.UI;"
jobs_header = "import System\nimport System.Threading\nimport System.Threading.Tasks\nnamespace Funke.Jobs;"

def save_type(relative, header, declaration):
    path = bolt_source / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    save(path, header + "\n\n" + declaration.rstrip() + "\n")

ui_sources = "\n".join(p.read_text(encoding="utf-8-sig") for p in sorted((core / "UI").rglob("*.cs")))
ui_types = set(re.findall(r"public (?:sealed |partial )*(?:enum|class) (\w+)", ui_sources))
component_files, ui_files = [], []

def save_component(name, declaration, is_enum=False):
    ui = name in ui_types
    directory = "UI" if ui else "Components"
    if is_enum:
        directory += "/Enums"
    relative = f"{directory}/{name}.bolt"
    save_type(relative, ui_header if ui else core_header, declaration)
    (ui_files if ui else component_files).append(relative)

for e in sorted(used_enums):
    save_component(e, "public enum "+e+" {"+enum_bodies[e]+"}", is_enum=True)
names = ["Transform2D"] + sorted(classes)
for i, name in enumerate(names[1:], 1):
    save_component(name, component_declaration(name, i, classes[name]))

manifest = bolt_root / "modules.json"
modules = json.loads(manifest.read_text(encoding="utf-8"))
modules["Core"] = [p for p in modules["Core"] if not p.startswith("Components/")] + component_files
modules["UI"] = ["UI/WidgetEvent.bolt", "UI/FkUiStore.bolt", "UI/WidgetEvents.bolt", "UI/UIPointer.bolt"] + ui_files
from bolt_enums import extend as extend_enums
extend_enums(core, bolt_source, modules, save)
from bolt_query_ref import generate as generate_query_ref
save(bolt_source / "Scene/QueryRef.bolt", generate_query_ref())
from generate_bolt_service_calls import generate as generate_service_calls
generate_service_calls()
from bolt_native_slots import generate as generate_native_slots
generate_native_slots(bolt_source, modules, glue, save)
save(manifest, json.dumps(modules, indent=2) + "\n")
save("ComponentNames.inc", "".join(f'"{native_names[name]}",\n' for name in names))
save("ComponentReferenceNames.inc", "".join(f'{{"{name}", "{native_names[name]}"}},\n' for name in names))
save("PackedFields.inc", "\n".join(packed_bindings) + "\n")
base_slots=set(re.findall(r"FUNKE_BOLT_SLOT\((\w+)\)",(out/"NativeSlots.inc").read_text()))
save("ComponentSlots.inc", "".join(f"FUNKE_BOLT_SLOT({n})\n" for n in sorted(used_slots-base_slots)))
print(f"{len(names)} component types, {sum(map(len,classes.values()))} forwarded members")


def generate_atomic_types():
    for suffix, value, word in (("Bool","bool","int"),("Int","int","int"),("Long","long","long"),
                                ("Float","float","uint"),("Double","double","ulong")):
        name = "Atomic" + suffix
        encode = "v ? 1 : 0" if value == "bool" else "v" if value == word else f"Reinterpret<{word}>(v)"
        decode = "v != 0" if value == "bool" else "v" if value == word else f"Reinterpret<{value}>(v)"
        methods = [
            f"private: System.Threading.Atomic<{word}> bits;",
            f"static {word} Encode({value} v) {{ return {encode}; }}",
            f"static {value} Decode({word} v) {{ return {decode}; }}",
            f"public: {name}() {{}}",
            f"{name}({value} initialValue) {{ Store(initialValue); }}",
            f"{value} Value {{get {{ return Load(); }} set {{ Store(value); }} }}",
            f"{value} Load() {{return Decode(bits.Load());}}",
            f"void Store({value} v) {{bits.Store(Encode(v));}}",
            f"{value} Exchange({value} v) {{return Decode(bits.Exchange(Encode(v)));}}",
            f"{value} CompareExchange({value} v,{value} comparand) {{ {word} old=Encode(comparand); bits.CompareExchange(old,Encode(v));return Decode(old); }}",
        ]
        if value == "bool":
            methods += ["bool TrySet() {return !CompareExchange(true,false);}",
                        "bool TryClear() {return CompareExchange(false,true);}"]
        else:
            for method, operation, result in (("Add","current + operand","next"),("FetchMin","operand < current ? operand : current","current"),
                                              ("FetchMax","operand > current ? operand : current","current"),
                                              ("Or","current | operand","current"),("And","current & operand","current")):
                if method in ("Or","And") and word != value:
                    continue
                methods.append(f"{value} {method}({value} operand) {{ {word} old=bits.Load(); while(true) {{ {value} current=Decode(old); "
                               f"{value} next={operation}; if(bits.CompareExchange(old,Encode(next))) return {result}; }}return Decode(old); }}")
            methods.append(f"{value} Sub({value} operand) {{return Add(-operand);}}")
            if value == word:
                methods += [f"{value} Increment() {{return Add(1);}}",f"{value} Decrement() {{return Add(-1);}}"]
        display = 'Load() ? "true" : "false"' if value == "bool" else "(string)Load()"
        methods.append(f"string ToString() {{ return {display}; }}")
        save_type(f"Jobs/{name}.bolt", jobs_header, "public class " + name + " {\n    " + "\n    ".join(methods) + "\n}\n")

generate_atomic_types()
