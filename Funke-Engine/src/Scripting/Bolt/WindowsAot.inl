// Included inside the Bolt host so linked modules use the same bindings as editor scripts.
bool IsWindowsAotPlayer() {
    if (Application::IsEditor()) return false;
    const auto* project = ProjectManager::GetCurrentProject();
    if (!project || project->ActiveBuildProfile != FunkeProject::BuildProfile::Release) return false;
    static const auto executableRoot = Platform::GetExecutablePath().parent_path();
    static thread_local std::string lastRoot;
    static thread_local bool packaged = false;
    if (lastRoot != project->RootDirectory) {
        lastRoot = project->RootDirectory;
        std::error_code ec;
        packaged = false;
        if (std::filesystem::equivalent(Path::FromUtf8(lastRoot), executableRoot, ec)) {
            Json::Value snapshot;
            if (Json::TryParse(File::ReadAllText(Path::ToUtf8(executableRoot / "project.json")), snapshot)) {
                const auto* aot = snapshot.FindMember("windowsBoltAot");
                packaged = aot && aot->AsBoolOr(false);
            }
        }
    }
    return packaged;
}

const auto& WindowsModuleDirectory() {
    static const auto path = Platform::GetExecutablePath().parent_path() / "BoltModules";
    return path;
}

const Json::Value& WindowsCatalog() {
    static const auto catalog = [] {
        Json::Value value;
        const auto path = Path::ToUtf8(WindowsModuleDirectory() / "catalog.json");
        std::string error;
        if (File::Exists(path) && !Json::TryParse(File::ReadAllText(path), value, &error))
            FNK_ERROR_TAG("Bolt", "Invalid AOT catalog {}: {}", path, error);
        return value;
    }();
    return catalog;
}

bool WindowsFields(const std::string& identity, std::vector<Field>& fields) {
    const auto* modules = WindowsCatalog().FindMember("modules");
    const auto* rows = modules ? modules->FindMember(identity) : nullptr;
    if (!rows || !rows->IsArray()) return false;
    fields.clear();
    for (const auto& row : rows->GetArray()) {
        if (!row.IsArray() || row.GetArray().size() != 8) return false;
        const auto& v = row.GetArray();
        fields.push_back({v[0].AsStringOr(), v[1].AsStringOr(), v[2].AsStringOr(),
            v[3].AsStringOr(), v[4].AsStringOr(), v[5].AsBoolOr(false), v[6].AsStringOr(), v[7].AsBoolOr(false)});
    }
    return true;
}

bool InstallWindowsLayouts(std::string& error) {
    const auto* schemas = WindowsCatalog().FindMember("layouts");
    if (!schemas) return true;
    std::vector<NativeStructLayout> layouts;
    for (const auto& [name, row] : schemas->GetMembers()) {
        const auto* fields = row.FindMember("fields");
        const auto* size = row.FindMember("size");
        const auto* alignment = row.FindMember("alignment");
        if (!fields || !size || !alignment || !fields->IsArray()) {
            error = "Invalid Windows AOT component layout for " + name;
            return false;
        }
        layouts.push_back({name, static_cast<uint32_t>(size->AsUInt64Or(0)),
            static_cast<uint32_t>(alignment->AsUInt64Or(0)), *fields});
    }
    return InstallNativeStructs(layouts, error);
}

void* ResolveWindowsBinding(const char* name) {
    for (const auto& binding : s_Bindings)
        if (std::string_view(name) == binding.name) return reinterpret_cast<void*>(binding.function);
    const std::pair<std::string_view, void*> extra[] = {
        {"FkCurrentUpdateSlot", reinterpret_cast<void*>(CurrentUpdateSlot)},
        {"FkJobRows", reinterpret_cast<void*>(JobRows)},
        {"FkNativeStructAddress", reinterpret_cast<void*>(NativeStructAddress)},
        {"FkRunJobRanges", reinterpret_cast<void*>(RunJobRanges)},
        {"FkSubmitJob", reinterpret_cast<void*>(SubmitJob)},
        {"FkRememberUpdateSlot", reinterpret_cast<void*>(RememberUpdateSlot)},
        {"FkNativeInvoke", reinterpret_cast<void*>(Services::Call)},
        {"FkPolygonPoints", reinterpret_cast<void*>(PolygonPoints)},
        {"FkConsoleIo", reinterpret_cast<void*>(ConsoleIo)},
        {"FkConsoleExecute", reinterpret_cast<void*>(ConsoleExecute)},
        {"FkLocale", reinterpret_cast<void*>(Locales::Call)},
        {"FkWriteImage", reinterpret_cast<void*>(WriteImage)},
        {"FkAssetOperation", reinterpret_cast<void*>(AssetLoadCall)},
        {"FkRuntimeTexture", reinterpret_cast<void*>(RuntimeTextureCall)},
        {"FkEcb", reinterpret_cast<void*>(Commands::Call)}
    };
    for (const auto& [symbol, address] : extra) if (symbol == name) return address;
    return nullptr;
}

bool LoadWindowsModule(const std::string& identity, Program& program, std::string& error) {
    uint64_t guid = 0;
    const auto end = identity.data() + identity.size();
    const auto parsed = std::from_chars(identity.data(), end, guid);
    if (parsed.ec != std::errc{} || parsed.ptr != end || !guid || !WindowsFields(identity, program.Fields)) {
        error = "Missing Windows AOT catalog entry for " + identity;
        return false;
    }
    auto image = std::make_shared<JobImage>();
    image->Id = guid;
    const auto path = WindowsModuleDirectory() / (identity + ".dll");
    image->Library = Platform::LoadSharedLibrary(path, &error);
    if (!image->Library) {
        error = "Cannot load Windows AOT module " + Path::ToUtf8(path) + ": " + error;
        return false;
    }
    using Bind = const char* (*)(void* (*)(const char*));
    auto bind = reinterpret_cast<Bind>(Platform::GetSharedLibrarySymbol(image->Library, "FunkeBoltBindV1"));
    image->RunAot = reinterpret_cast<decltype(image->RunAot)>(Platform::GetSharedLibrarySymbol(image->Library, "FunkeBoltRunV1"));
    image->CloseAot = reinterpret_cast<decltype(image->CloseAot)>(Platform::GetSharedLibrarySymbol(image->Library, "FunkeBoltCloseV1"));
    if (!bind || !image->RunAot || !image->CloseAot) {
        error = "Incompatible Windows AOT module " + Path::ToUtf8(path);
        return false;
    }
    if (const char* missing = bind(ResolveWindowsBinding)) {
        error = "Unknown Windows AOT host function " + std::string(missing);
        return false;
    }
    { std::lock_guard lock(s_JobImagesMutex); s_JobImages[guid] = image; }
    program.Jobs = std::move(image);
    FNK_INFO_TAG("Bolt", "Loaded Windows AOT module {}", identity);
    return true;
}
