namespace {
    std::map<std::string, SystemProgram> s_DataModules;

    uint64_t DataAssetAccess(uint64_t operation, uint64_t guid) {
        if (!s_Frame) return 0;
        auto found = s_DataAssets.find(guid);
        if (found == s_DataAssets.end()) return 0;
        auto& instance = found->second;
        auto& text = s_Frame->Text;
        if (operation == 2) return text == instance.Script;
        if (operation == 0) {
            const auto value = instance.Values.find(text);
            if (value == instance.Values.end()) return 0;
            text = value->second;
            return text.size() + 1;
        }
        if (operation == 3) {
            const auto colon = text.find(':');
            const auto equals = text.find('=', colon);
            if (colon == text.npos || equals == text.npos) return 0;
            Json::Value number;
            if (!Json::TryParse(text.substr(equals + 1), number, nullptr) || !number.IsNumber()) return 0;
            auto option = Json::Value::MakeObject();
            option.AddMember("name", text.substr(colon + 1, equals - colon - 1));
            option.AddMember("value", std::move(number));
            instance.EnumOptions[text.substr(0, colon)].EnsureArray().push_back(std::move(option));
            return 1;
        }
        if (operation != 1) return 0;
        const auto split = text.find('=');
        if (split == text.npos) return 0;
        instance.Values[text.substr(0, split)] = text.substr(split + 1);
        return 1;
    }

    bool ResolveDataType(const std::string& type, std::string& identity, std::string& name) {
        if (!type.starts_with("bolt:")) return false;
        identity = type.substr(5);
        auto path = ResolveAsset(identity);
        if (path.empty()) return false;
        ScriptBase kind{};
        name = ScriptClass(File::ReadAllText(path), kind);
        return kind == ScriptBase::DataAsset;
    }
}

bool HasDataAsset(uint64_t guid) { return s_DataAssets.contains(guid); }

bool HasDataAssetClass(const std::string& type) {
    std::string identity, name;
    return ResolveDataType(type, identity, name);
}

bool CreateDataAsset(const std::string& type, uint64_t guid) {
    std::string identity, name;
    if (!guid || !ResolveDataType(type, identity, name)) return false;
    auto& module = s_DataModules[name];
    Refresh(identity, module, false, ScriptBase::DataAsset);
    if (!module.Active || !module.Error.empty()) return false;
    s_DataAssets.insert_or_assign(guid, DataInstance{name, {}});
    if (std::any_of(module.Fields.begin(), module.Fields.end(), [](const auto& field) {
        return field.Type == "enum" || field.Type == "flagenum";
    }) && !InvokeToolScript(identity, module, Event::Inspect, guid)) {
        s_DataAssets.erase(guid);
        return false;
    }
    return true;
}

void DestroyDataAsset(uint64_t guid) { s_DataAssets.erase(guid); }

std::string GetDataAssetFields(uint64_t guid) {
    const auto found = s_DataAssets.find(guid);
    if (found == s_DataAssets.end()) return "[]";
    auto fields = s_DataModules.at(found->second.Script).Fields;
    for (auto& field : fields) {
        auto stored = found->second.Values.find(field.Name);
        if (stored != found->second.Values.end()) field.Value = stored->second;
    }
    return FieldJson(fields, &found->second.EnumOptions);
}

void SetDataAssetField(uint64_t guid, const char* key, const char* value) {
    if (!key || !value) return;
    auto found = s_DataAssets.find(guid);
    if (found == s_DataAssets.end()) return;
    auto& instance = found->second;
    instance.Values[key] = value;
    auto& module = s_DataModules.at(instance.Script);
    if (module.Running) return;
    module.Running = true;
    InvokeToolScript(instance.Script, module, Event::ApplyField, guid);
    module.Running = false;
}

std::string AppendDataAssetTypes(const std::string& managed) {
    Json::Value result;
    if (!Json::TryParse(managed, result) || !result.IsArray()) result = Json::Value::MakeArray();
    for (const auto& unit : ComponentUnits()) {
        if (unit.Kind != ScriptBase::DataAsset) continue;
        auto row = Json::Value::MakeObject();
        std::string menu = unit.Name.substr(unit.Name.find_last_of('.')+1);
        std::string_view prefix(unit.Source.data(), unit.Body);
        while (!prefix.empty()) {
            auto token = NextBoltToken(prefix);
            if (token != "CreateDataAsset" && token != "CreateDataAssetAttribute") continue;
            if (NextBoltToken(prefix) != "(") continue;
            Json::Value arg;
            if (Json::TryParse(NextBoltToken(prefix), arg) && arg.IsString() && !arg.AsStringOr().empty()) menu = arg.AsStringOr();
            break;
        }
        row.AddMember("type", "bolt:" + std::to_string(unit.Guid));
        row.AddMember("menu", std::move(menu));
        result.Append(std::move(row));
    }
    return Json::Stringify(result);
}

