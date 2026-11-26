// Included beside RunProgram so command execution can enter the registering script image.
struct ConsoleInvocation {
    std::string Name;
    const std::vector<std::string>& Arguments;
    DevCommandResult Result = DevCommandResult::Ok();
};
struct ConsoleBridge {
    void* Entry;
    void* Capture;
};
struct ConsoleCaller {
    Frame* Context;
    ConsoleBridge& Bridge;
    ConsoleCaller* Previous;
};
thread_local ConsoleInvocation* s_ConsoleInvocation = nullptr;
thread_local ConsoleCaller* s_ConsoleCaller = nullptr;

uint64_t ConsoleIo(uint64_t operation, void* first, void* second) noexcept {
    if (!s_Frame) return 0;
    try {
        const auto text = [](const void* value) { return value ? static_cast<const char*>(value) : ""; };
        auto setText = [](std::string value) -> uint64_t {
            s_Frame->Text = std::move(value);
            return s_Frame->Text.size();
        };
        switch (operation) {
        case 0:
        case 9: {
            Program* module = s_Frame->EntityProgram;
            if (!module && s_CurrentSystem) module = s_CurrentSystem->Module.get();
            if (!module || text(first)[0] == 0) return 0;
            if (operation == 9) s_ConsoleAttributes.try_emplace(text(first), module, text(second));
            else s_ConsoleCommands[text(first)] = {module, text(second)};
            return 1;
        }
        case 1: {
            const auto it = s_ConsoleCommands.find(text(first));
            if (it == s_ConsoleCommands.end()) return 0;
            s_ConsoleCommands.erase(it);
            return 1;
        }
        case 8: {
            const auto it = s_ConsoleAttributes.find(text(first));
            if (it != s_ConsoleAttributes.end()) it->second.Usage = text(second);
            return 0;
        }
        case 7: if (s_ActiveConsoleBridge) *s_ActiveConsoleBridge = first; return 0;
        case 2: return s_ConsoleInvocation ? s_ConsoleInvocation->Arguments.size() : 0;
        case 3: {
            if (!s_ConsoleInvocation || !first) return 0;
            const auto i = *static_cast<const int*>(first);
            return i >= 0 && static_cast<size_t>(i) < s_ConsoleInvocation->Arguments.size()
                ? setText(s_ConsoleInvocation->Arguments[i]) : 0;
        }
        case 4: return s_ConsoleInvocation ? setText(s_ConsoleInvocation->Name) : 0;
        case 5:
        case 6:
            if (s_ConsoleInvocation) s_ConsoleInvocation->Result = operation == 5
                ? DevCommandResult::Ok(text(first)) : DevCommandResult::Error(text(first));
            return 0;
        }
    } catch (...) {}
    return 0;
}

uint64_t ConsoleExecute(uint64_t, void* line, void* args) noexcept {
    if (!s_Frame || !line || !args) return 0;
    ConsoleCaller caller{s_Frame, *static_cast<ConsoleBridge*>(args), s_ConsoleCaller};
    s_ConsoleCaller = &caller;
    try {
        s_Frame->Text = DevConsole::Execute(static_cast<const char*>(line)).Output;
    } catch (...) { s_Frame->Text.clear(); }
    s_ConsoleCaller = caller.Previous;
    return s_Frame->Text.size();
}

DevCommandResult InvokeConsoleCommand(const std::string& name, const std::vector<std::string>& args) {
    Program* module = nullptr;
    if (const auto found = s_ConsoleCommands.find(name); found != s_ConsoleCommands.end()) module = found->second.Module;
    else if (const auto found = s_ConsoleAttributes.find(name); found != s_ConsoleAttributes.end()) module = found->second.Module;
    if (!module) return {DevCommandResult::Status::NotFound, {}};
    Program& program = *module;
    ConsoleInvocation invocation{name, args};
    auto previousInvocation = std::exchange(s_ConsoleInvocation, &invocation);
    Frame* previousFrame = s_Frame;
    try {
        // An Execute from the same image already owns the VM's execution lock. Its short-lived
        // callback uses the active context without entering RunProgram a second time.
        ConsoleCaller* caller = s_ConsoleCaller;
        while (caller && &caller->Context->Path != &program.Path) caller = caller->Previous;
        if (caller) {
            s_Frame = caller->Context;
            void* capture = std::exchange(caller->Bridge.Capture, nullptr);
            if (!capture || !caller->Bridge.Entry) invocation.Result = DevCommandResult::Error("Developer console callback unavailable.");
            else {
                auto previous = s_ActiveConsoleBridge;
                s_ActiveConsoleBridge = &caller->Bridge.Capture;
                reinterpret_cast<void*(*)(void*)>(caller->Bridge.Entry)(capture);
                s_ActiveConsoleBridge = previous;
            }
        } else {
            Frame frame{0, true, 0, Event::DevCommand, 0, {}, {}, program.Path, program.Path, program.Fields};
            frame.EntityProgram = &program;
            s_Frame = &frame;
            int32_t code = 0;
            char diagnostic[2048]{};
            if (RunProgram(program, true, code, diagnostic, sizeof(diagnostic)) != BOLT_HOST_OK || code != 0)
                invocation.Result = DevCommandResult::Error(diagnostic[0] ? diagnostic : "Developer console callback failed.");
        }
    } catch (const std::exception& error) { invocation.Result = DevCommandResult::Error(error.what()); }
    s_Frame = previousFrame;
    s_ConsoleInvocation = previousInvocation;
    return invocation.Result;
}
