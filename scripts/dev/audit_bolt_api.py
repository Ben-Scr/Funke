"""Compare a ScriptCore public metadata export against the Bolt SDK declarations.

Export metadata with Export-ScriptApi.ps1. The CSV retains every overload and
separates absence from signatures that need a language-specific review.
"""
import argparse
import csv
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Funke-BoltScriptCore"))
import embed

OPERATORS = dict(Addition="+", Subtraction="-", Multiply="*", Division="/", Equality="==",
                 Inequality="!=", UnaryNegation="-", LessThan="<", GreaterThan=">",
                 LessThanOrEqual="<=", GreaterThanOrEqual=">=", Modulus="%", Implicit="implicit",
                 Explicit="explicit", OnesComplement="~", BitwiseAnd="&", BitwiseOr="|", ExclusiveOr="^")
ALIASES = dict(Int32="int", UInt32="uint", Single="float", Double="double", Int64="long",
               UInt64="ulong", Boolean="bool", String="string", Byte="byte", SByte="sbyte",
               Int16="short", UInt16="ushort", Char="char", Void="void", Object="object")


def normal(value):
    value = re.sub(r"\bSystem\.(\w+)", lambda m: ALIASES.get(m[1], m[1]), value or "")
    return re.sub(r"(?:\b[A-Z]\w*\.)+(\w+)", r"\1", value).replace(" ", "")


def split(value):
    parts, start, depth = [], 0, 0
    for i, c in enumerate(value):
        if c in "<([": depth += 1
        if c in ">)]": depth -= 1
        if c == "," and depth == 0:
            parts.append(value[start:i].strip())
            start = i + 1
    return [p for p in [*parts, value[start:].strip()] if p]


def declarations():
    generic_forms = {}
    formals = {}
    packs = {}
    result = defaultdict(lambda: defaultdict(list))
    modules = json.loads((embed.ROOT / "modules.json").read_text())
    for files in modules.values():
        files = [path for path in files if embed.public_file(path)]
        preamble, body = embed.assemble(files)
        namespace = re.search(r"namespace ([\w.]+);", preamble)[1]
        cleaned = re.sub(r"//[^\n]*|/\*[\s\S]*?\*/", "", embed.editor_declarations(body))
        tokens = list(embed.TOKEN.finditer(cleaned))
        pairs, stack = {}, []
        for i, token in enumerate(tokens):
            if token[0] in ("{", "(", "["): stack.append(i)
            if token[0] in ("}", ")", "]"): pairs[stack.pop()] = i

        def walk(first, end, owner, public=True, enum=False, static_class=False):
            i = first
            while i < end:
                if tokens[i][0] in (";", ","):
                    i += 1
                    continue
                if tokens[i][0] == "[":
                    i = pairs[i] + 1
                    continue
                if i + 1 < end and tokens[i + 1][0] == ":":
                    public = tokens[i][0] == "public"
                    i += 2
                    continue
                start = i
                while i < end and tokens[i][0] not in ("{", ";") and not (enum and tokens[i][0] == ","):
                    i = pairs.get(i, i) + 1
                header = cleaned[tokens[start].start():tokens[i - 1].end()]
                stop = pairs[i] + 1 if i in pairs else i + 1
                begin = i
                i = stop
                if re.search(r"\b(?:private|protected|internal)\b", header) or not (public or header.startswith("public ")):
                    continue
                static = static_class or bool(re.search(r"\bstatic\b", header))
                header = re.sub(r"\b(?:public|override|abstract|virtual|const|readonly|sealed|static)\s+", "", header)
                type_match = re.match(r"(class|struct|enum|interface)\s+(\w+)(?:<([^>]+)>)?", header)
                if type_match:
                    kind, name, args = type_match.groups()
                    suffix = chr(96) + str(len(split(args))) if args else ""
                    key = owner + "." + name + suffix
                    result[key]
                    formals[key] = formals.get(owner, []) + [re.match(r"\w+", v)[0] for v in split(args or "")]
                    if args and ("..." in args or "=" in args):
                        arguments = split(args)
                        minimum = sum("..." not in arg and "=" not in arg for arg in arguments)
                        maximum = None if "..." in args else len(arguments)
                        generic_forms[owner + "." + name] = (key, minimum, maximum)
                    if begin in pairs: walk(begin + 1, stop - 1, key, kind in ("enum","interface"), kind == "enum", static and kind == "class")
                    continue
                if enum:
                    result[owner][header.split("=")[0].strip()].append(("Field", (), "", True, "", 0))
                    continue
                indexer = re.fullmatch(r"(.+?)\s+this\[(.+)\]", header)
                if indexer and begin in pairs:
                    element_type, index = indexer.groups()
                    params = tuple(normal(re.sub(r"\s+\w+$", "", part)) for part in split(index))
                    inside = cleaned[tokens[begin].end():tokens[stop - 1].start()]
                    access = "".join(k[0] for k in ("get", "set") if re.search(r"\b"+k+r"\s*\{", inside))
                    result[owner]["Item"].append(("Property", params, normal(element_type), static, access, 0))
                    continue
                conversion = re.fullmatch(r"(implicit|explicit)\s+([\w.<>]+)\(\s*\)", header)
                if conversion:
                    source_type = owner.rsplit(".", 1)[-1].split(chr(96))[0]
                    result[owner][conversion[1]].append(("Method", (source_type,), normal(conversion[2]), True, "", 0))
                    continue
                if "(" in header and ("operator" in header or "=" not in header.split("(", 1)[0]):
                    before, after = header.split("(", 1)
                    op = re.search(r"operator\s*([^\s]+)$", before)
                    method = re.search(r"(\w+)(?:<([^>]*)>)?$", before)
                    if not op and not method: continue
                    name = op[1] if op else method[1]
                    ret = before[:(op or method).start()].strip()
                    args_text = after.rsplit(")", 1)[0]
                    defaults = sum("=" in p for p in split(args_text))
                    params = split(args_text)
                    params = tuple(normal(re.sub(r"\s+\w+$", "", a.split("=")[0].strip())) for a in params)
                    ctor = name == owner.split(".")[-1].split(chr(96))[0]
                    args = split(method[2]) if not op and method[2] else []
                    if any("..." in item for item in args):
                        packs[owner, name] = len(args) - 1
                    for count in range(len(params) - defaults, len(params) + 1):
                        result[owner][".ctor" if ctor else name].append(
                            ("Constructor" if ctor else "Method", params[:count], normal(ret), static, "", len(args)))
                else:
                    header = header.split("=")[0].strip()
                    fields = re.match(r"(event\s+)?(.+?)\s+([\w,\s]+)$", header)
                    if not fields: continue
                    kind = "Event" if fields[1] else "Property" if begin in pairs else "Field"
                    acc = ""
                    if kind == "Property":
                        inside = cleaned[tokens[begin].end():tokens[stop-1].start()]
                        acc = "".join(n[0] for n in ("get","set") if re.search(r"\b"+n+r"\s*\{", inside))
                    for n in fields[3].split(","):
                        result[owner][n.strip()].append((kind, (), normal(fields[2]), static, acc, 0))

        walk(0, len(tokens), namespace)
    return result, generic_forms, formals, packs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("metadata", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    bolt, generic_forms, formals, packs = declarations()
    with args.output.open("w", encoding="utf-8", newline="") as out:
        rows = csv.writer(out)
        rows.writerow(["C# type", "Member", "C# signature", "Comparison", "Bolt declarations", "C# parameters"])
        for t in json.loads(args.metadata.read_text(encoding="utf-8-sig")):
            typ = t["type"]
            canonical = typ
            if typ not in bolt:
                match = re.fullmatch(r"(.+?)(?:`(\d+))?", typ)
                form = generic_forms.get(match[1])
                arity = int(match[2] or 0)
                if form and arity >= form[1] and (form[2] is None or arity <= form[2]):
                    canonical = form[0]
            rows.writerow([typ, "", "", "type found" if typ in bolt else "generic equivalent" if canonical in bolt else "missing type", canonical if canonical in bolt else "", ""])
            for member in t["members"]:
                name = member["name"]
                key = OPERATORS.get(name[3:], name) if name.startswith("op_") else name
                options = bolt.get(canonical, {}).get(key, [])
                signature = (member["kind"], tuple(map(normal, member["parameters"])), normal(member["result"]),
                             member["static"], member["access"], len(member["generic"]))
                if not options and name == ".ctor" and not member["parameters"] and canonical in bolt:
                    status = "check implicit constructor"
                elif not options: status = "missing member" if canonical in bolt else "missing type"
                elif signature in options or (member["kind"] == "Field" and member["result"] == typ and
                                               any(k[0] == "Field" for k in options)): status = "candidate match"
                else: status = "review signature"
                if status == "review signature":
                    csharp_vars = t.get("generic", [])
                    bolt_vars = formals.get(canonical, [])
                    def canonical_type(shape, variables):
                        for idx, var in enumerate(variables):
                            shape = re.sub(r"\b" + re.escape(var) + r"\b", f"@{idx}", shape)
                        return shape.replace("?", "").replace("...", "[]")
                    # A readonly C# input can be passed by value in Bolt. Keep writable ref/out parameters distinct.
                    args = tuple(canonical_type(p.removesuffix("&") if arg["passing"] == "in" else p, csharp_vars)
                                 for p, arg in zip(signature[1], member.get("arguments", [])))
                    ret = canonical_type(signature[2], csharp_vars)
                    for kind, params, return_type, static, access, arity in options:
                        if (kind, static, access) != (signature[0], signature[3], signature[4]): continue
                        pack = packs.get((canonical, key))
                        if arity != signature[5] and (pack is None or signature[5] < pack): continue
                        if len(params) != len(signature[1]) or len(args) != len(params): continue
                        if tuple(canonical_type(p, bolt_vars) for p in params) != args: continue
                        if canonical_type(return_type, bolt_vars) != ret: continue
                        status = "Bolt signature equivalent"
                        break
                if status == "review signature" and signature[0] == "Event":
                    widget = signature[2].replace("Action<", "WidgetEvent<", 1)
                    if any(o[0] == "Property" and o[2] == widget for o in options):
                        status = "widget event equivalent"
                if status == "review signature" and any(
                    (o[0], o[1], o[2].removesuffix("?"), *o[3:]) == signature and o[2].endswith("?")
                    for o in options
                ):
                    status = "optional return"
                if status == "missing member" and name == "GetEnumerator" and canonical.startswith("Funke.QueryBuilder"):
                    options = bolt.get("Funke.SceneQueryIteration", {}).get(name, [])
                    if options:
                        status = "extension method"
                if status == "review signature" and key == "Equals" and signature[1] == ("object",):
                    if any(o[0] == "Method" and o[2] == "bool" and len(o[1]) == 1 for o in options):
                        status = "typed equality"
                if status == "review signature" and member["kind"] == "Field":
                    access = "g" if member.get("readOnly") else "gs"
                    if any(o[:4] == ("Property", (), signature[2], signature[3]) and all(k in o[4] for k in access) for o in options):
                        status = "property equivalent"
                if status == "review signature" and member["kind"] == "Property" and not signature[1]:
                    if any(o[:4] == ("Field", (), signature[2], signature[3]) for o in options):
                        status = "field equivalent"
                if status == "missing member" and name in ("op_Explicit", "op_Implicit") and len(signature[1]) == 1:
                    conversions = [o for members in bolt.values() for o in members.get(key, []) if o[1:3] == signature[1:3]]
                    if conversions:
                        options = conversions
                        status = "conversion on source or destination"
                if status == "missing member" and name in ("op_Explicit", "op_Implicit") and len(signature[1]) == 1:
                    destination = next((members for type_name, members in bolt.items() if type_name == member["result"]), {})
                    constructors = [o for o in destination.get(".ctor", []) if o[1] == signature[1]]
                    explicit = [o for members in bolt.values() for o in members.get("explicit", []) if o[1:3] == signature[1:3]]
                    if constructors or explicit:
                        options = constructors or explicit
                        status = "constructor cast" if name == "op_Explicit" else "explicit conversion required"
                if status == "missing member" and name == "Entity" and typ in {"Funke.Transform2D", "Funke.UI.RectTransform"}:
                    options = bolt["Funke.Component"].get("Entity", [])
                    if options: status = "inherited member"
                forms = [" ".join(filter(None, ("static" if st else "", k, r, key + ("<"+str(g)+">" if g else "") +
                          "("+", ".join(args)+")", acc))) for k, args, r, st, acc, g in options]
                arguments = []
                for arg in member.get("arguments", []):
                    descriptor = " ".join(filter(None, ("params" if arg["variadic"] else arg["passing"], arg["type"], arg["name"])))
                    if arg["optional"]:
                        descriptor += " = " + json.dumps(arg["default"], ensure_ascii=False)
                    arguments.append(descriptor)
                rows.writerow([typ, name, member["signature"], status, " | ".join(forms), ", ".join(arguments)])


if __name__ == "__main__":
    main()

