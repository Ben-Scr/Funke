"""Embed the ordered Bolt modules in the native host at build time."""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent


# Only the file header is shared between types. Keep each declaration verbatim.
HEADER_TOKEN = re.compile(r"\s+|//[^\n]*|/\*.*?\*/|"
                          r"import[ \t]+([\w.]+)[ \t]*(?=\n)|namespace\s+([\w.]+)\s*;", re.S)


def assemble(files):
    imports = []
    bodies = []
    namespace = None
    for name in files:
        source = (ROOT / "Source/Funke" / name).read_text(encoding="utf-8-sig")
        pos = 0
        comments = []
        while token := HEADER_TOKEN.match(source, pos):
            pos = token.end()
            if token[2]:
                if namespace and namespace != token[2]:
                    raise ValueError(f"{name}: expected namespace {namespace}, found {token[2]}")
                namespace = token[2]
                break
            if token[1] and token[1] not in imports:
                imports.append(token[1])
            if token[0].startswith("/"):
                comments.append(token[0])
        else:
            raise ValueError(f"{name}: missing file namespace")
        bodies.append("\n".join([*comments, source[pos:].strip("\n")]))
    preamble = "".join(f"import {name}\n" for name in imports) + f"namespace {namespace};\n"
    return preamble, "\n\n".join(bodies) + "\n"


# Project tooling type-checks against signatures. The host compiles the full definitions
# separately, including the dispatch methods injected into user components.
TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|=>|\w+|[^\s]', re.S)
HOST_NAME = re.compile(r'\b(?:Fk[A-Z]\w*|fkOwnerCanceled)\b')


def editor_declarations(source):
    tokens = [t for t in TOKEN.finditer(source) if not t[0].startswith(("//", "/*"))]
    closing = {}
    stack = []
    for i, token in enumerate(tokens):
        if token[0] in ("{", "(", "["):
            stack.append(i)
        elif token[0] in ("}", ")", "]"):
            closing[stack.pop()] = i

    def span(first, end):
        return source[tokens[first].start():tokens[end - 1].end()].strip()

    def declarations(first, end, access="public"):
        lines = []
        i = first
        while i < end:
            if tokens[i][0] == ";":
                i += 1
                continue
            if i + 1 < end and tokens[i + 1][0] == ":":
                access = tokens[i][0]
                lines.append(span(i, i + 2))
                i += 2
                continue
            start = i
            while tokens[i][0] not in ("{", ";", "=>"):
                i = closing.get(i, i) + 1
            header = span(start, i)
            symbol = tokens[i][0]
            if symbol == "{":
                finish = closing[i] + 1
            else:
                finish = i
                while tokens[finish][0] != ";":
                    finish = closing.get(finish, finish) + 1
                finish += 1
            explicit = re.search(r"\b(public|protected|private|internal)\b", header)
            visibility = explicit[1] if explicit else access
            if visibility in ("private", "internal") or HOST_NAME.search(header) or header.startswith('extern '):
                i = finish
                continue

            body = '{ throw System.InvalidOperationException("Editor API reference"); }'
            if symbol == ";":
                lines.append(span(start, finish))
            elif re.search(r'\benum\s+\w+', header):
                lines.append(span(start, finish))
            elif re.search(r'\b(?:class|struct|interface)\s+\w+', header):
                lines.append(header + " {\n" + declarations(i + 1, finish - 1, "public" if re.search(r"\binterface\b", header) else "private") + "\n}")
            elif symbol == "=>" or (tokens[i + 1][0] in ("get", "set") and "(" not in header):
                accessors = ["get"] if symbol == "=>" else []
                j = i + 1
                while symbol == "{" and j < finish - 1:
                    accessors.append(tokens[j][0])
                    j = closing[j + 1] + 1
                lines.append(header + " { " + " ".join(a + " " + body for a in accessors) + " }")
            else:
                # Constructor initializers reference implementation fields.
                signature = re.sub(r'\)\s*:\s*.*$', ")", header, flags=re.S)
                lines.append(signature + " " + body)
            i = finish
        return "\n".join(lines)

    return declarations(0, len(tokens)) + "\n"


def definition(name, source):
    # MSVC limits the size of an individual raw string literal.
    delimiter = "bolt"
    while f'){delimiter}"' in source:
        delimiter += "_"
    literals = [f'R"{delimiter}({source[i:i + 4000]}){delimiter}"' for i in range(0, len(source), 4000)]
    return f"const char {name}[] =\n" + "\n".join(literals) + ";\n"


def public_file(path):
    return (not path.startswith("Interop/") or Path(path).stem == "NativeInterop") and Path(path).stem not in {
        "ManagedTasks", "CoroutineContext", "JobBindings"
    }


def main(output):
    modules = json.loads((ROOT / "modules.json").read_text(encoding="utf-8"))
    definitions = []
    extra = []
    for name, files in modules.items():
        preamble, body = assemble(files)
        public_files = [path for path in files if public_file(path)]
        _, public_body = assemble(public_files)
        definitions.append(definition("Editor" + name, preamble + editor_declarations(public_body)))
        additional = name not in ("Core", "UI", "Jobs", "ComponentNamespace", "Coroutines")
        if additional:
            extra.append(name)
        if name in ("Core", "UI") or additional:
            definitions.append(definition(name + "Preamble", preamble))
            definitions.append(definition(name, body))
        else:
            definitions.append(definition(name, preamble + body))
    definitions.append("const ExtraModule ExtraModules[] = {\n" + "".join(
        f'    {{"<Funke>/{name}.bolt", "{name}.bolt", {name}Preamble, {name}, Editor{name}}},\n' for name in extra)
        + "    {}\n};\n")
    source = ('#include "pch.hpp"\n#include "Scripting/Bolt/EmbeddedSources.hpp"\n\n'
              'namespace Funke::BoltScriptHost::EmbeddedSources {\n' + "\n".join(definitions) + "}\n")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(source.encode("utf-8"))


if __name__ == "__main__":
    main(Path(sys.argv[1]))
