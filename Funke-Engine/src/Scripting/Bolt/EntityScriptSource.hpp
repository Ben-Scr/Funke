#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace Funke::BoltScriptHost {
	inline std::string_view NextBoltToken(std::string_view& text) {
		while (!text.empty()) {
			if (std::isspace(static_cast<unsigned char>(text.front()))) { text.remove_prefix(1); continue; }
			if (text.starts_with("//") || text.starts_with("/*")) {
				const bool block = text[1] == '*';
				const auto end = text.find(block ? "*/" : "\n", 2);
				text.remove_prefix(end == text.npos ? text.size() : end + (block ? 2 : 1));
				continue;
			}
			size_t n = 1;
			if (text.front() == '"' || text.front() == '\'') {
				while (n < text.size()) {
					const char c = text[n++];
					if (c == text.front()) break;
					if (c == '\\' && n < text.size()) ++n;
				}
			} else if (std::isalnum(static_cast<unsigned char>(text.front())) || text.front() == '_') {
				while (n < text.size() && (std::isalnum(static_cast<unsigned char>(text[n])) || text[n] == '_')) ++n;
			}
			auto token = text.substr(0, n);
			text.remove_prefix(n);
			return token;
		}
		return {};
	}

	inline bool ImportsFunke(std::string_view source) {
		while (!source.empty()) {
			if (NextBoltToken(source) == "import" && NextBoltToken(source) == "Funke") return true;
		}
		return false;
	}

    struct NativeCollectionImports {
        bool Collections = false;
        bool Interop = false;

        void Inspect(std::string_view source) {
            if ((Collections && Interop) || (source.find("Native") == source.npos && source.find("Interop") == source.npos)) return;
            std::string_view previous, beforePrevious;
            while (!source.empty()) {
                const auto token = NextBoltToken(source);
                if (token == "NativeInterop" || (token == "Interop" && previous == "." && beforePrevious == "Funke")) {
                    Interop = true;
                    Collections = true; // NativeInterop allocates its UTF-8 buffers with NativeMemoryPool.
                    return;
                }
                if (!Collections && ((token == "Native" && previous == "." && beforePrevious == "Collections") ||
                    token == "NativeArray" || token == "NativeBitArray" || token == "NativeBool" || token == "NativeBuffer" ||
                    token == "NativeDeque" || token == "NativeDictionary" || token == "NativeHashset" || token == "NativeList" ||
                    token == "NativeMemoryPool" || token == "NativePriorityQueue" || token == "NativeQueue" ||
                    token == "NativeStack" || token == "NativeString"))
                    Collections = true;
                beforePrevious = previous;
                previous = token;
            }
        }
    };

	enum class ScriptBase { Procedural, Entity, Scene, Global, Component, NativeStruct, Job, EditorWindow, DataAsset, EnumLibrary };

    struct ScriptDeclaration { std::string Name; ScriptBase Kind; size_t Body; bool HotReloadState = false; };

    inline std::vector<ScriptDeclaration> ScriptDeclarations(std::string_view source) {
        const char* begin=source.data();
        std::vector<ScriptDeclaration> result;
        std::string ns;
        while (!source.empty()) {
            const auto token=NextBoltToken(source);
            if(token=="namespace"){
                ns.clear();
                for(auto part=NextBoltToken(source);!part.empty() && part!=";";part=NextBoltToken(source))ns+=part;
                continue;
            }
            if(token!="class" && token!="struct")continue;
            auto tail=source;
            const auto name=NextBoltToken(tail);
            if(NextBoltToken(tail)!=":")continue;
            auto kind=ScriptBase::Procedural;
            bool hotReloadState = false;
            for(auto part=NextBoltToken(tail);!part.empty() && part!="{";part=NextBoltToken(tail)){
                std::string qualified(part);
                auto peek=tail;
                while(NextBoltToken(peek)=="."){qualified+=".";qualified+=NextBoltToken(peek);tail=peek;}
                auto is=[&](std::string_view id,std::string_view prefix) {
                    return qualified==id || (qualified.starts_with(prefix) && std::string_view(qualified).substr(prefix.size())==id);
                };
                if(token=="struct" && is("IComponent","Funke.Components."))kind=ScriptBase::NativeStruct;
                else if(token=="struct" && kind!=ScriptBase::NativeStruct &&
                    (is("IJobQuery","Funke.Jobs.") || is("IJob","Funke.Jobs.") || is("IJobParallelFor","Funke.Jobs.") || is("IJobFor","Funke.Jobs.")))kind=ScriptBase::Job;
                else if(token=="class"){
                    if(is("EntityScript","Funke."))kind=ScriptBase::Entity;
                    if(is("IHotReloadState","Funke."))hotReloadState=true;
                    if(is("Component","Funke."))kind=ScriptBase::Component;
                    if(is("SceneSystem","Funke."))kind=ScriptBase::Scene;
                    if(is("GlobalSystem","Funke."))kind=ScriptBase::Global;
                    if(is("DataAsset","Funke."))kind=ScriptBase::DataAsset;
                    if(is("EditorWindow","Funke.Editor."))kind=ScriptBase::EditorWindow;
                }
            }
            if(kind!=ScriptBase::Procedural)
                result.push_back({ns.empty()?std::string(name):ns+"."+std::string(name),kind,static_cast<size_t>(tail.data()-begin),hotReloadState});
        }
        return result;
    }

    // A standalone enum declaration is a shared gameplay dependency; it has no entry point of its own.
    inline std::string EnumLibraryName(std::string_view source) {
        std::string name, ns;
        int depth = 0, attribute = 0;
        while (!source.empty()) {
            const auto part = NextBoltToken(source);
            if (part == "[") { ++attribute; continue; }
            if (part == "]" && attribute) { --attribute; continue; }
            if (attribute) continue;
            if (part == "{") { ++depth; continue; }
            if (part == "}") { --depth; continue; }
            if (depth) continue;
            if (part == "class" || part == "struct" || part == "interface" || part == "(") return {};
            if (part == "namespace") {
                for (auto word = NextBoltToken(source); !word.empty() && word != ";"; word = NextBoltToken(source)) ns += word;
            }
            if (part == "enum" && name.empty()) name = NextBoltToken(source);
        }
        return name.empty() || ns.empty() ? name : ns + "." + name;
    }

    inline std::string ScriptClass(std::string_view source,ScriptBase& kind,size_t* bodyOffset=nullptr,bool* hotReloadState=nullptr) {
        auto declarations=ScriptDeclarations(source);
        kind=ScriptBase::Procedural;
        const ScriptDeclaration* selected=nullptr;
        for(const auto& type:declarations) {
            if(!selected || (selected->Kind==ScriptBase::Job && type.Kind==ScriptBase::NativeStruct))selected=&type;
            if(type.Kind!=ScriptBase::NativeStruct && type.Kind!=ScriptBase::Job){selected=&type;break;}
        }
        if (!selected) {
            auto library = source.find("enum") == source.npos ? std::string{} : EnumLibraryName(source);
            if (!library.empty()) kind = ScriptBase::EnumLibrary;
            return library;
        }
        kind=selected->Kind;
        if(bodyOffset)*bodyOffset=selected->Body;
        if(hotReloadState)*hotReloadState=selected->HotReloadState;
        return selected->Name;
    }

	inline std::string EntityScriptClass(std::string_view source) {
		ScriptBase kind = ScriptBase::Procedural;
		auto name = ScriptClass(source, kind);
		return kind == ScriptBase::Entity ? name : std::string{};
	}

    inline std::string EditorWindowEntryPoint(const std::string& name) {
        return "public static class FunkeScriptEntry { private: static " + name +
            " Window; public: int Main() { ulong e = FunkeApiTypes.EventId(); "
            "if (e == 59ul) Window.OnOpen(); if (e == 60ul) Window.OnGUI(); "
            "if (e == 61ul) Window.OnClose(); return 0; } }";
    }

	inline std::string SystemEntryPoint(const std::string& name, ScriptBase kind) {
		const bool scene = kind == ScriptBase::Scene;
		std::string entry = "public static class FunkeScriptEntry { private: static Dictionary<ulong," + name
			+ "> Instances; public: int Main() { ulong eventId=FunkeApiTypes.EventId(); ulong key=FunkeApiTypes.Attachment();"
			"if (eventId >= 30ul && (eventId <= 58ul || eventId == 62ul)) { Funke.FkNotifications.Dispatch(eventId); return 0; }"
			"if (eventId==28ul) { ManagedTasks.Pump(true);if(Instances.ContainsKey(key)) Instances.GetRef(key).FkCancel(); ManagedTasks.Pump(true); Instances.Remove(key); return 0; } if (!Instances.ContainsKey(key)) {" + name
			+ " fresh; Instances.Add(key,fresh); } " + name + "& Instance=Instances.GetRef(key);";
		entry += "if(eventId==29ul){ManagedTasks.Pump(true);Instance.FkCancel();ManagedTasks.Pump(true);return 0;}";
        if (scene) entry += "if (Instance.Scene.Name.Length==0) { Instance.Scene.Name=FunkeApiTypes.SystemSceneName(); Instance.Scene.Guid=FunkeApiTypes.SystemSceneGuid(); } if (eventId == 24ul) Instance.OnAwake(); if (eventId == 25ul) Instance.OnStart(); if (eventId == 4ul) {ManagedTasks.Pump(true);Instance.FkCancel(); Instance.OnDestroy();ManagedTasks.Pump(true);}";
		else entry += "if (eventId == 25ul) Instance.OnInitialize();";
		return entry + "if (eventId==26ul || eventId==27ul) {ManagedTasks.Pump(false);FunkeApiTypes.KeepCoroutine(Instance.FkPump(eventId==27ul,FunkeApiTypes.CoroutineDelta(),FunkeApiTypes.CoroutineEpoch()) || ManagedTasks.HasPending()); return 0;}"
			"if (eventId == 0ul) Instance.OnUpdate(); if (eventId == 1ul) Instance.OnFixedUpdate();"
			"if (eventId == 2ul) Instance.OnEnable(); if (eventId == 3ul) Instance.OnDisable();"
			"if (eventId == 18ul) Instance.OnApplicationPaused(); if (eventId == 19ul) Instance.OnApplicationQuit();"
			"if (eventId == 20ul || eventId == 21ul) Instance.OnFocusChanged(eventId == 20ul);"
			"if (eventId == 22ul) Instance.FkReadFields(); if (eventId == 23ul) Instance.FkApplyFields(); return 0; } }";
	}

	inline std::string EntityEntryPoint(const std::string& name, bool hotReloadState = false) {
		std::string entry = R"bolt(
extern "C" void* FkCurrentUpdateSlot(ulong a, ulong b);
extern "C" ulong FkRememberUpdateSlot(ulong key, void* slot, void* reserved);
struct FunkePumpSlot { public: void* Slot; ulong Epoch; float Delta; uint EventId, Watched, Scheduled; }
class FunkeAttachment {
public:
	SCRIPT Behaviour;
	bool Enabled = false;
	bool Started = false;
}
public static class FunkeScriptEntry {
private:
	static Bolt.Runtime.Buffer<FunkeAttachment[]> Instances;
	int Create(int key, ulong eventId) {
		if (eventId == 3ul || eventId == 4ul || eventId >= 22ul) return 0;
		while (Instances.Length <= key) {
			FunkeAttachment[] empty;
			Instances.Add(empty);
		}
		Instances[key] = new FunkeAttachment[1];
		FunkeAttachment& slot = Instances[key][0];
		slot.Behaviour.FkBind(FunkeApiTypes.EntityId());
		slot.Behaviour.FkApplyFields();
		slot.Behaviour.OnAwake();
		FkRememberUpdateSlot((ulong)key,(void*)&slot,null);
		return Dispatch(slot, key, eventId);
	}
	int Dispatch(FunkeAttachment& slot, int key, ulong eventId) {
        CAPTURE_STATE
        if(eventId==29ul){ManagedTasks.Pump(true);slot.Behaviour.FkCancel();ManagedTasks.Pump(true);return 0;}
		if (eventId==26ul || eventId==27ul) { ManagedTasks.Pump(false);FunkeApiTypes.KeepCoroutine(slot.Behaviour.FkPump(eventId==27ul,FunkeApiTypes.CoroutineDelta(),FunkeApiTypes.CoroutineEpoch()) || ManagedTasks.HasPending()); return 0; }
		if (eventId == 0ul && slot.Enabled && slot.Started) { slot.Behaviour.OnUpdate(); return 0; }
		if (eventId == 22ul) { slot.Behaviour.FkReadFields(); return 0; }
		if (eventId == 23ul) { slot.Behaviour.FkApplyFields(); return 0; }
		if (eventId == 3ul || eventId == 4ul) {
			if (slot.Enabled) { slot.Enabled = false; slot.Behaviour.OnDisable(); }
			if (eventId == 4ul) { ManagedTasks.Pump(true);slot.Behaviour.FkCancel(); slot.Behaviour.OnDestroy(); ManagedTasks.Pump(true); FunkeAttachment[] empty; Instances[key] = empty; }
			return 0;
		}
		if (!slot.Enabled) { slot.Enabled = true; slot.Behaviour.OnEnable(); }
		if (eventId == 2ul) return 0;
        if (eventId == 5ul) { slot.Behaviour.OnCollisionEnter2D(Collision2D(false)); return 0; }
        if (eventId == 6ul) { slot.Behaviour.OnCollisionStay2D(Collision2D(false)); return 0; }
        if (eventId == 7ul) { slot.Behaviour.OnCollisionExit2D(Collision2D(false)); return 0; }
        if (eventId == 8ul) { slot.Behaviour.OnTriggerEnter2D(Collision2D(true)); return 0; }
        if (eventId == 9ul) { slot.Behaviour.OnTriggerStay2D(Collision2D(true)); return 0; }
        if (eventId == 10ul) { slot.Behaviour.OnTriggerExit2D(Collision2D(true)); return 0; }
        if (eventId == 11ul) { slot.Behaviour.OnCollisionEnter3D(Collision3D(false)); return 0; }
        if (eventId == 12ul) { slot.Behaviour.OnCollisionStay3D(Collision3D(false)); return 0; }
        if (eventId == 13ul) { slot.Behaviour.OnCollisionExit3D(Collision3D(false)); return 0; }
        if (eventId == 14ul) { slot.Behaviour.OnTriggerEnter3D(Collision3D(true)); return 0; }
        if (eventId == 15ul) { slot.Behaviour.OnTriggerStay3D(Collision3D(true)); return 0; }
        if (eventId == 16ul) { slot.Behaviour.OnTriggerExit3D(Collision3D(true)); return 0; }
        if (eventId == 17ul) { slot.Behaviour.OnApplicationStart(); return 0; }
        if (eventId == 18ul) { slot.Behaviour.OnApplicationPaused(); return 0; }
        if (eventId == 19ul) { slot.Behaviour.OnApplicationQuit(); return 0; }
        if (eventId == 20ul || eventId == 21ul) { slot.Behaviour.OnFocusChanged(eventId == 20ul); return 0; }
		if (!slot.Started) { slot.Started = true; slot.Behaviour.OnStart(); RESTORE_STATE }
		if (eventId == 1ul) slot.Behaviour.OnFixedUpdate();
		else slot.Behaviour.OnUpdate();
		return 0;
	}
	int FromTable() {
		int key = (int)FunkeApiTypes.Attachment();
		ulong eventId = FunkeApiTypes.EventId();
		if (eventId >= 30ul && (eventId <= 58ul || eventId == 62ul)) { Funke.FkNotifications.Dispatch(eventId); return 0; }
		FunkeAttachment* existing = (FunkeAttachment*)FkCurrentUpdateSlot(1ul,0ul);
        if (existing != null) return Dispatch(*existing,key,eventId);
        if (key >= Instances.Length || Instances[key].Length == 0) return Create(key, eventId);
		FunkeAttachment& slot = Instances[key][0];
		if (eventId == 0ul && slot.Enabled && slot.Started) { slot.Behaviour.OnUpdate(); return 0; }
		return Dispatch(slot, key, eventId);
	}
public:
	int Main() {
        FunkePumpSlot* pump=(FunkePumpSlot*)FkCurrentUpdateSlot(2ul,0ul);
        if (pump!=null) {
            FunkeAttachment* current=(FunkeAttachment*)pump->Slot;
            if (pump->EventId==0u) {
                if(current->Enabled && current->Started) {current->Behaviour.OnUpdate();return 0;}
                return FromTable();
            }
            if (pump->Watched!=0u) ManagedTasks.Pump(false);
            bool active=current->Behaviour.FkPump(pump->EventId==27u,pump->Delta,pump->Epoch) || pump->Watched!=0u;
            if (active!=(pump->Scheduled!=0u)) FunkeApiTypes.KeepCoroutine(active);
            return 0;
        }
        FunkeAttachment* update=(FunkeAttachment*)FkCurrentUpdateSlot(0ul,0ul);
        if(update!=null && update->Enabled && update->Started){update->Behaviour.OnUpdate();return 0;}
		return FromTable();
	}
}
)bolt";
		const std::string capture = hotReloadState ? R"bolt(
        if (eventId == 63ul) {
            ReloadState state;
            slot.Behaviour.CaptureReloadState(state);
            Funke.FunkeText.Put(state.FkSerialize());
            return 0;
        })bolt" : "";
        const std::string restore = hotReloadState ? R"bolt(
            ulong size = Funke.FkTakeReloadState(0ul,0ul);
            if (size != 0ul) {
                try {
                    ReloadState state = ReloadState.FkDeserialize(Funke.FunkeText.Read(size));
                    slot.Behaviour.RestoreReloadState(state);
                    string unread = state.FkUnreadKeys();
                    if (unread.Length != 0) Log.Warn("Incomplete hot reload restore; unread or incompatible values: " + unread);
                } catch (System.Exception& error) {
                    Log.Error("Incomplete hot reload restore: " + error.Message);
                }
            })bolt" : "";
        entry.replace(entry.find("CAPTURE_STATE"), 13, capture);
        entry.replace(entry.find("RESTORE_STATE"), 13, restore);
        entry.replace(entry.find("SCRIPT"), 6, name);
		return entry;
	}
}

