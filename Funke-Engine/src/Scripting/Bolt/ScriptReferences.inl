// Included alongside the host's per-module script and attachment registry.
uint64_t ScriptReference(uint64_t owner, uint64_t operation) {
    if (!s_Frame) return 0;
    Scene* scene = nullptr;
    EntityHandle handle = entt::null;
    if (!ResolveEntityReference(owner, scene, handle)) return 0;
    const auto* component = scene->GetRegistry().try_get<ScriptComponent>(handle);
    if (!component || s_Frame->Text.empty()) return 0;

    const auto& wanted = s_Frame->Text;
    const ScriptInstance* shortNameMatch = nullptr;
    bool ambiguous = false;
    for (const auto& attached : component->Scripts) {
        const auto& identity = attached.GetClassName();
        std::string name;
        if (attached.GetType() == ScriptType::Bolt) {
            const auto module = s_Programs.find(identity);
            if (module == s_Programs.end() || !module->second.Active || !module->second.EntityScript) continue;
            const auto slot = module->second.Attachments.find({scene, static_cast<uint32_t>(handle)});
            if (slot == module->second.Attachments.end()) continue;
            name = module->second.ScriptClassName;
        } else if (CanBeManagedScript(attached.GetType()) && attached.HasManagedInstance()) {
            name = ScriptEngine::ResolveManagedTypeName(identity);
            if (name.empty()) name = identity;
            if (name.starts_with("type:")) name.erase(0, 5);
        } else continue;

        if (wanted == identity || wanted == name) {
            s_Frame->Text = operation == 2 ? name : identity;
            return s_Frame->Text.size();
        }
        if (operation != 1 || name.substr(name.find_last_of('.') + 1) != wanted) continue;
        ambiguous = shortNameMatch != nullptr;
        shortNameMatch = &attached;
    }
    if (!shortNameMatch || ambiguous) return 0;
    s_Frame->Text = shortNameMatch->GetClassName();
    return s_Frame->Text.size();
}
