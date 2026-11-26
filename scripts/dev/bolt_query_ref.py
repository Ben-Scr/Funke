"""Emit the strongly typed borrowed-query rows and enumerators through arity eight."""


def types(count, readonly):
    return [f"W{i + 1}Type" for i in range(count)] + (["RType"] if readonly else [])


def builder(count, readonly, entity):
    params = types(count, readonly)
    arguments = ", ".join(params)
    suffix = f"{count}{'_RO1' if readonly else ''}"
    name = f"QueryRef{'Entity' if entity else ''}Builder{suffix}"
    result = f"{name}<{arguments}>"
    single = not entity and count == 1 and not readonly
    width = len(params)
    declarations = [f"public class {name}<{arguments}> {{", "public:", "    FkQueryRefSpec FkSpec;"]
    if not entity:
        for method, field in (("With", "Required"), ("Without", "Excluded")):
            declarations.append(f"    {result} {method}<T>() {{ {result} copy = this; copy.FkSpec.{field} += FkQueryRefSpec.Key<T>(); return copy; }}")
        for method, mode in (("EnabledOnly", 1), ("DisabledOnly", 2), ("IncludeDisabled", 0)):
            declarations.append(f"    {result} {method}() {{ {result} copy = this; copy.FkSpec.EnableFilter = {mode}; return copy; }}")
        with_entity = f"QueryRefEntityBuilder{suffix}<{arguments}>"
        declarations.append(f"    {with_entity} WithEntity() {{ {with_entity} copy; copy.FkSpec = FkSpec; return copy; }}")
        if count == 1 and not readonly:
            kind = "QueryRefBuilder1_RO1<W1Type, RType>"
            declarations.append(f"    {kind} Readonly<RType>() {{ {kind} copy; copy.FkSpec = FkSpec; copy.FkSpec.Reads = FkQueryRefSpec.Key<RType>(); return copy; }}")
    if not single:
        declarations.extend(["    public class Row {", "    public:", "        FkQueryRefCursor FkCursor;"])
        deconstruct, assignments = [], []
        if entity:
            declarations.append("        EntityRef Entity => FkCursor.Data.Get().EntityAt(FkCursor.Index);")
            deconstruct.append("EntityRef& entity")
            assignments.append("entity = Entity;")
        for i, param in enumerate(params):
            read = readonly and i == len(params) - 1
            prop = "R" if read else f"W{i+1}"
            wrapper = f"Funke.Components.{'RefRO' if read else 'Ref'}<{param}>"
            pointer = f"({param}*)FkCursor.Data.Get().Cell(FkCursor.Index, {i})"
            declarations.append(f"        {param}{'' if read else '&'} {prop} {{ get {{ return *({pointer}); }} }}")
            if read and entity:
                declarations.append(f"        {param} R1 {{ get {{ return *({pointer}); }} }}")
            if i == 0 and count == 1:
                declarations.append(f"        {param}& W {{ get {{ return *({pointer}); }} }}")
            deconstruct.append(f"{wrapper}& {prop.lower()}")
            assignments.append(f"{prop.lower()} = {wrapper}.FkBorrow({pointer});")
        declarations.append("        void Deconstruct(" + ", ".join(deconstruct) + ") {")
        declarations.extend(f"            {value}" for value in assignments)
        declarations.extend(["        }", "    }"])
    declarations += ["    public class Enumerator {", "    public:", "        FkQueryRefCursor FkCursor;",
                     "        bool MoveNext() { return FkCursor.MoveNext(); }", "        void Dispose() { FkCursor.Dispose(); }"]
    if single:
        declarations.append("        W1Type& Current { get { return *((W1Type*)FkCursor.Data.Get().Cell(FkCursor.Index, 0)); } }")
    else:
        declarations.append("        Row Current { get { Row row; row.FkCursor = FkCursor; return row; } }")
    declarations.extend(["    }", "    Enumerator GetEnumerator() {", "        Enumerator result;",
                         f"        result.FkCursor.Open(FkSpec, {width}, {str(entity).lower()});",
                         "        return result;", "    }", "}"])
    return "\n".join(declarations)


def generate():
    groups = [builder(n, readonly, entity) for n in range(1, 9)
              for readonly in ([False, True] if n == 1 else [False]) for entity in (False, True)]
    extensions = []
    with_entity = []
    for n in range(1, 9):
        arguments = ", ".join(types(n, False))
        extensions.append(f"    static QueryRefBuilder{n}<{arguments}> QueryRef<{arguments}>(Scene scene) {{ return scene.QueryRef<{arguments}>(); }}")
        for readonly in ([False, True] if n == 1 else [False]):
            arguments = ", ".join(types(n, readonly))
            suffix = f"{n}{'_RO1' if readonly else ''}"
            with_entity.append(f"    static QueryRefEntityBuilder{suffix}<{arguments}> WithEntity<{arguments}>(QueryRefBuilder{suffix}<{arguments}> query) {{ return query.WithEntity(); }}")
    rows = []
    for count in range(2, 9):
        params = [f"T{i + 1}" for i in range(count)]
        arguments = ", ".join(params)
        outputs = ", ".join(f"{param}& c{i+1}" for i, param in enumerate(params))
        row = f"this QueryRow<{arguments}> row"
        assignments = " ".join(f"c{i+1} = row.Get<{param}>();" for i, param in enumerate(params))
        rows.append(f"    static void Deconstruct<{arguments}>({row}, Entity& entity, {outputs}) {{ entity = row.Entity; {assignments} }}")
        rows.append(f"    static void Deconstruct<{arguments}>({row}, {outputs}) {{ {assignments} }}")
    groups.append("public static class SceneQueryRows {\npublic:\n" + "\n".join(rows) + "\n}")
    groups.append("public static class SceneQueryRefExtensions {\npublic:\n" + "\n".join(extensions) + "\n}")
    groups.append("public static class QueryRefWithEntityExtensions {\npublic:\n" + "\n".join(with_entity) + "\n}")
    return "namespace Funke;\n\n" + "\n\n".join(groups) + "\n"
