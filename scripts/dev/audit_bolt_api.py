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
                    params = split(after.rsplit(")", 1)[0])
                    params = tuple(normal(re.sub(r"\s+\w+$", "", a.split("=")[0].strip())) for a in params)
                    ctor = name == owner.split(".")[-1].split(chr(96))[0]
                    args = split(method[2]) if not op and method[2] else []
                    result[owner][".ctor" if ctor else name].append(
                        ("Constructor" if ctor else "Method", params, normal(ret), static, "", len(args)))
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
    return result, generic_forms


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("metadata", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    bolt, generic_forms = declarations()
    with args.output.open("w", encoding="utf-8", newline="") as out:
        rows = csv.writer(out)
        rows.writerow(["C# type", "Member", "C# signature", "Comparison", "Bolt declarations"])
        for t in json.loads(args.metadata.read_text(encoding="utf-8-sig")):
            typ = t["type"]
            canonical = typ
            if typ not in bolt:
                match = re.fullmatch(r"(.+?)(?:`(\d+))?", typ)
                form = generic_forms.get(match[1])
                arity = int(match[2] or 0)
                if form and arity >= form[1] and (form[2] is None or arity <= form[2]):
                    canonical = form[0]
            rows.writerow([typ, "", "", "type found" if typ in bolt else "generic equivalent" if canonical in bolt else "missing type", canonical if canonical in bolt else ""])
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
                forms = [" ".join(filter(None, ("static" if st else "", k, r, key + ("<"+str(g)+">" if g else "") +
                          "("+", ".join(args)+")", acc))) for k, args, r, st, acc, g in options]
                rows.writerow([typ, name, member["signature"], status, " | ".join(forms)])


if __name__ == "__main__":
    main()

