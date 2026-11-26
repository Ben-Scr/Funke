#pragma once

namespace Funke {
	inline constexpr const char* k_NativeScriptSnippets = R"json({
    "Funke C++ script": {
        "scope": "cpp",
        "prefix": "fnkscript",
        "description": "Native script with lifecycle callbacks and registration",
        "body": [
            "#include <Funke/Scripting.hpp>",
            "",
            "using namespace Funke;",
            "",
            "class ${1:Player} : public NativeScript {",
            "public:",
            "\tvoid Start() override {",
            "\t\t$0",
            "\t}",
            "",
            "\tvoid Update() override {}",
            "\tvoid FixedUpdate() override {}",
            "\tvoid OnDestroy() override {}",
            "};",
            "",
            "REGISTER_SCRIPT($1)"
        ]
    },
    "Funke lifecycle callback": {
        "scope": "cpp",
        "prefix": "fnklifecycle",
        "description": "Insert a native lifecycle override inside a script",
        "body": ["void ${1|Start,Update,FixedUpdate,OnDestroy|}() override {", "\t$0", "}"]
    },
    "Funke optional component or script": {
        "scope": "cpp",
        "prefix": "fnktry",
        "description": "Borrow an optional component or native script for the current operation",
        "body": ["if (auto* ${1:component} = TryGetComponent<${2:Transform2DComponent}>()) {", "\t$0", "}"]
    },
    "Funke get or add component": {
        "scope": "cpp",
        "prefix": "fnkensure",
        "description": "Reuse an ECS component or add it if absent",
        "body": ["auto& ${1:component} = GetOrAddComponent<${2:Transform2DComponent}>($0);"]
    },
    "Funke get or add native script": {
        "scope": "cpp",
        "prefix": "fnkensurescript",
        "description": "Reuse or attach a registered native script; check for failure",
        "body": ["if (auto* ${1:script} = GetOrAddComponent<${2:Health}>()) {", "\t$0", "}"]
    },
    "Funke retained component handle": {
        "scope": "cpp",
        "prefix": "fnkref",
        "description": "Keep a ComponentRef across frames instead of a borrowed component address",
        "body": ["auto ${1:transform} = GetComponentRef<${2:Transform2DComponent}>();$0"]
    },
    "Funke Inspector fields": {
        "scope": "cpp",
        "prefix": "fnkfields",
        "description": "Expose an existing member; preserve the saved key when renaming the C++ member",
        "body": [
            "FNK_NATIVE_FIELDS(",
            "\tFunke::NativeFields::Make<&${1:Player}::${2:Speed}>(\"${3:Speed}\", \"${4:Movement Speed}\")$0",
            ")"
        ]
    },
    "Funke Inspector field descriptor": {
        "scope": "cpp",
        "prefix": "fnkfield",
        "description": "Add inside FNK_NATIVE_FIELDS; the saved key is independent of the member name",
        "body": ["Funke::NativeFields::Make<&${1:Player}::${2:Speed}>(\"${3:Speed}\", \"${4:Movement Speed}\")$0"]
    }
})json";
}
