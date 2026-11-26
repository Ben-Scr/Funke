namespace {
    struct EditorWindowInfo {
        std::string Path;
        std::filesystem::file_time_type Modified{};
        Json::Value Description;
    };

    struct EditorWindowInstance {
        SystemProgram Module;
        bool Opened = false;
    };

    std::map<std::string, EditorWindowInfo> s_EditorCatalog;
    std::map<std::string, EditorWindowInstance> s_EditorWindows;

    Json::Value DescribeEditorWindow(const std::string& identity, const std::string& source) {
        ScriptBase kind{};
        size_t body = 0;
        const auto name = ScriptClass(source, kind, &body);
        if (kind != ScriptBase::EditorWindow) return {};
        auto title = name.substr(name.find_last_of('.') + 1);
        std::string menu;
        auto description = Json::Value::MakeObject();
        std::string_view declaration(source.data(), body);
        while (!declaration.empty()) {
            const auto token = NextBoltToken(declaration);
            if (token != "EditorWindow" && token != "EditorWindowAttribute") continue;
            if (NextBoltToken(declaration) != "(") continue;
            const auto arg = NextBoltToken(declaration);
            Json::Value value;
            if (Json::TryParse(arg, value) && value.IsString() && !value.AsStringOr().empty()) title = value.AsStringOr();
            if (NextBoltToken(declaration) == "," && Json::TryParse(NextBoltToken(declaration), value) && value.IsString()) {
                menu = value.AsStringOr();
                for (const char* key : {"width", "height"}) {
                    if (NextBoltToken(declaration) != ",") break;
                    const auto end = declaration.find_first_of(",)");
                    if (end == declaration.npos) break;
                    const auto text = declaration.substr(0, end);
                    declaration.remove_prefix(end);
                    const auto first = text.find_first_not_of(" \t\n\r");
                    if (first == text.npos) break;
                    float pixels{};
                    const auto number = std::from_chars(text.data()+first, text.data()+text.size(), pixels);
                    if (number.ec == std::errc{} && std::isfinite(pixels) && pixels > 0)
                        description.AddMember(key, static_cast<double>(pixels));
                }
            }
            break;
        }
        description.AddMember("type", "bolt:" + identity);
        description.AddMember("title", title);
        description.AddMember("menu", menu.empty() ? title : menu);
        return description;
    }

    bool InvokeToolScript(const std::string& identity, SystemProgram& module, Event event, uint64_t argument = 0) {
        Frame frame{0, true, 0, event, 0, {}, {}, module.Path, identity, module.Fields};
        frame.Notification.Argument = argument;
        auto* previous = std::exchange(s_Frame, &frame);
        int32_t code = 0;
        char diagnostic[2048]{};
        const int status = RunProgram(module, true, code, diagnostic, sizeof(diagnostic));
        s_Frame = previous;
        if (!previous) DrainPendingEntities();
        if (status == BOLT_HOST_OK && code == 0) return true;
        if (!module.ReportedRunError) FNK_ERROR_TAG("Bolt", "Script '{}' returned {}: {}", module.Path, code, diagnostic);
        module.ReportedRunError = true;
        return false;
    }
}

std::string AppendEditorWindowTypes(const std::string& managed) {
    Json::Value result;
    if (!Json::TryParse(managed, result) || !result.IsArray()) result = Json::Value::MakeArray();
    std::set<std::string> present;
    for (const auto& asset : AssetRegistry::GetAssetsByKind(AssetKind::Script)) {
        const auto path = Path::FromUtf8(asset.Path);
        if (!ScriptTypeEqualsIgnoreCase(path.extension().string(), ".bolt")) continue;
        std::error_code ec;
        const auto modified = std::filesystem::last_write_time(path, ec);
        if (ec) continue;
        const auto identity = std::to_string(asset.Id);
        present.insert(identity);
        auto& item = s_EditorCatalog[identity];
        if (item.Path != asset.Path || item.Modified != modified) {
            item = {asset.Path, modified, DescribeEditorWindow(identity, File::ReadAllText(asset.Path))};
        }
        if (item.Description.IsObject()) result.Append(item.Description);
    }
    std::erase_if(s_EditorCatalog, [&](const auto& pair) { return !present.contains(pair.first); });
    return Json::Stringify(result);
}

int DrawEditorWindow(const std::string& identity) {
    const auto info = s_EditorCatalog.find(identity);
    if (info == s_EditorCatalog.end() || !info->second.Description.IsObject()) return 0;
    auto& window = s_EditorWindows[identity];
    auto& module = window.Module;
    if (module.Path != info->second.Path || module.Modified != info->second.Modified) {
        if (window.Opened) InvokeToolScript(identity, module, Event::EditorWindowClose);
        Refresh(identity, module, true, ScriptBase::EditorWindow);
        window.Opened = false;
    }
    if (!module.Active || !module.Error.empty()) return -1;
    if (!window.Opened) {
        if (!InvokeToolScript(identity, module, Event::EditorWindowOpen)) return -1;
        window.Opened = true;
    }
    return InvokeToolScript(identity, module, Event::EditorWindowDraw) ? 1 : -1;
}

void CloseEditorWindow(const std::string& identity) {
    auto found = s_EditorWindows.find(identity);
    if (found == s_EditorWindows.end()) return;
    if (found->second.Opened) InvokeToolScript(found->first, found->second.Module, Event::EditorWindowClose);
    s_EditorWindows.erase(found);
}

