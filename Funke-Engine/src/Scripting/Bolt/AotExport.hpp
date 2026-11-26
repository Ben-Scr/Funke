#pragma once

#include "Scripting/Bolt/CompilerHost.h"
#include "Scripting/BoltScriptHost.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include <charconv>

namespace Funke::BoltScriptHost {
    struct AotExport {
        uint64_t Guid = 0;
        std::filesystem::path Directory;
        std::string AssetName;

        bool Open(const std::filesystem::path& root, const std::string& path, std::string& error) {
            Json::Value meta;
            if (!Json::TryParse(File::ReadAllText(path + ".meta"), meta, &error)) return false;
            auto id = meta.FindMember("AssetGUID");
            if (!id) id = meta.FindMember("uuid");
            if (id && id->IsString()) {
                const auto raw = id->AsStringOr();
                const auto result = std::from_chars(raw.data(), raw.data() + raw.size(), Guid);
                if (result.ec != std::errc{} || result.ptr != raw.data() + raw.size()) Guid = 0;
            } else if (id) Guid = id->AsUInt64Or(0);
            if (!Guid) { error = path + ": missing Bolt asset GUID"; return false; }
            AssetName = Path::ToUtf8(Path::FromUtf8(path).filename());
            Directory = root / std::to_string(Guid);
            std::error_code ec;
            if (std::filesystem::exists(Directory, ec) ||
                !std::filesystem::create_directories(Directory / "src", ec)) {
                error = path + ": duplicate Bolt GUID or inaccessible staging directory";
                return false;
            }
            return true;
        }

        bool Write(const std::vector<BoltSource>& units, const char* entry, const std::vector<Field>& fields,
                   std::string& error) const {
            auto write = [&](const std::filesystem::path& file, const std::string& data) {
                if (File::WriteAllText(Path::ToUtf8(Directory / file), data)) return true;
                error = "Cannot stage Bolt source for " + AssetName;
                return false;
            };
            for (size_t i = 0; i < units.size(); ++i) {
                const auto& unit = units[i];
                const auto filename = Path::ToUtf8(Path::FromUtf8(unit.name).filename());
                if (!write(std::filesystem::path("src") / Path::FromUtf8(std::to_string(i) + "_" + filename),
                           std::string(unit.source, unit.length))) return false;
            }
            const auto manifest = std::string("format = 1\n[project]\nname = \"FunkeScript\"\nkind = \"executable\"\nentry = \"")
                + entry + "\"\n[sources]\nroots = [\"src\"]\n";
            if (!write("bolt.proj", manifest)) return false;
            auto module = Json::Value::MakeObject();
            module.AddMember("asset", AssetName);
            auto schema = Json::Value::MakeArray();
            for (const auto& field : fields) {
                auto row = Json::Value::MakeArray();
                for (const auto* value : {&field.Name, &field.Type, &field.DisplayName, &field.Value, &field.Tooltip})
                    row.Append(Json::Value(*value));
                row.Append(Json::Value(field.ReadOnly));
                row.Append(Json::Value(field.ReferenceType));
                row.Append(Json::Value(field.Optional));
                schema.Append(std::move(row));
            }
            module.AddMember("fields", std::move(schema));
            return write("module.json", Json::Stringify(module));
        }
    };
}
