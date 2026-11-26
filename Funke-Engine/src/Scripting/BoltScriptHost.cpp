#include "pch.hpp"
#include "Scripting/BoltScriptHost.hpp"
#include "Scripting/Bolt/CompilerHost.h"
#include "Scripting/Bolt/EmbeddedSources.hpp"
#include "Scripting/Bolt/NativeApi.hpp"
#include "DevTools/DevConsole.hpp"
#include "Scripting/ScriptPreferences.hpp"
#include "Scripting/Bolt/AssetLoads.hpp"
#include "Scripting/Bolt/SceneLoadOwner.hpp"
#include "Scripting/Bolt/Locale.hpp"
#include "Scripting/Bolt/RuntimeTextures.hpp"
#include "Scripting/Bolt/ServiceBindings.hpp"
#include "Scripting/Bolt/SceneQueryBorrow.hpp"
#include "Scripting/Bolt/EntityScriptSource.hpp"
#include "Scripting/Bolt/ConsoleSource.hpp"
#include "Scripting/Bolt/PlayResetSource.hpp"
#include "Scripting/Bolt/InspectorSource.hpp"
#include "Scripting/Bolt/ComponentSource.hpp"
#include "Scripting/Bolt/NativeStructSource.hpp"
#include "Scripting/Bolt/JobQuerySource.hpp"
#include "Scripting/Bolt/JobModule.hpp"
#include "Scripting/Bolt/CommandBuffer.hpp"
#include "Scripting/Bolt/CoroutineSource.hpp"
#include <set>
#include "Jobs/ParallelFor.hpp"
#include "Scene/Entity.hpp"
#include "Scene/DynamicComponentStorage.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptEntityReference.hpp"
#include "Scripting/ScriptTransform2D.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/SceneExecutionContext.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptSystem.hpp"
#include <deque>
#include <map>
#include "Scripting/NativeEngineAPI.hpp"
#include "Scripting/NativeScriptHost.hpp"
#include "Scripting/ScriptInstance.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Core/Platform.hpp"
#include "Core/Application.hpp"
#include "Core/Engine.hpp"
#if FUNKE_WITH_2D
#include "Components/Graphics/SpriteRendererComponent.hpp"
#endif
#include "Project/ProjectManager.hpp"
#if FUNKE_WITH_RENDER
#include "Core/Input.hpp"
#endif
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

#include "Scripting/Bolt/AotExport.hpp"

namespace Funke::BoltScriptHost {
	namespace {
		const std::string_view k_CoroutineNamespace = EmbeddedSources::Coroutines;
		const std::string_view k_ComponentNamespace = EmbeddedSources::ComponentNamespace;
        struct CompilerAPI {
			void* Library = nullptr;
            std::filesystem::path LibraryPath;
            decltype(&bolt_compiler_bind_submit) BindSubmit = nullptr;
            decltype(&bolt_module_visit_layout) VisitLayout = nullptr;
			decltype(&bolt_compiler_create) Create = nullptr;
            decltype(&bolt_compiler_create_with_llvm) CreateWithLlvm = nullptr;
            bool LoggedProfile = false;
			decltype(&bolt_compiler_bind_pointer) BindPointer = nullptr;
            decltype(&bolt_compiler_destroy) Destroy = nullptr;
			decltype(&bolt_compiler_compile) Compile = nullptr;
			decltype(&bolt_compiler_compile_sources) CompileSources = nullptr;
			decltype(&bolt_compiler_visit_fields) VisitFields = nullptr;
			decltype(&bolt_compiler_load) Load = nullptr;
			decltype(&bolt_module_release) Release = nullptr;
			decltype(&bolt_compiler_run) Run = nullptr;
			decltype(&bolt_compiler_run_persistent) RunPersistent = nullptr;
            decltype(&bolt_module_run_persistent_batch) RunBatch = nullptr;
            decltype(&bolt_compiler_version) Version = nullptr;

			bool Open(std::string& error) {
				if (Library) return true;
				const auto path = FindCompilerLibrary();
				if (path.empty()) {
					error = "Install the Bolt desktop SDK or set BOLT_SDK_ROOT to its directory";
					return false;
				}
				LibraryPath=path;
                Library = Platform::LoadSharedLibrary(path, &error);
				if (!Library) return false;
				auto bind = [this](auto& function, const char* name) {
					function = reinterpret_cast<std::decay_t<decltype(function)>>(Platform::GetSharedLibrarySymbol(Library, name));
					return function != nullptr;
				};
				bind(Version,"bolt_compiler_version");
                bind(CreateWithLlvm, "bolt_compiler_create_with_llvm");
                bind(VisitLayout,"bolt_module_visit_layout");
                bind(BindSubmit,"bolt_compiler_bind_submit");
                bind(BindPointer, "bolt_compiler_bind_pointer");
                bind(RunPersistent, "bolt_compiler_run_persistent");
                bind(RunBatch,"bolt_module_run_persistent_batch");
				bind(CompileSources, "bolt_compiler_compile_sources");
				bind(VisitFields, "bolt_compiler_visit_fields");
				decltype(&bolt_compiler_host_abi) abi = nullptr;
				if (bind(abi, "bolt_compiler_host_abi") && abi() == BOLT_COMPILER_HOST_ABI
					&& bind(Create, "bolt_compiler_create") && bind(Destroy, "bolt_compiler_destroy")
					&& bind(Compile, "bolt_compiler_compile") && bind(Load, "bolt_compiler_load")
					&& bind(Release, "bolt_module_release") && bind(Run, "bolt_compiler_run")) return true;
				error = "Incompatible Bolt compiler host ABI at " + Path::ToUtf8(path);
				Platform::UnloadSharedLibrary(Library);
				Library = nullptr;
				return false;
			}
		};

		struct AttachmentHash {
			size_t operator()(const std::pair<const Scene*, uint32_t>& key) const noexcept {
				return std::hash<const Scene*>{}(key.first) ^ std::hash<uint32_t>{}(key.second);
			}
		};

		struct Program {
#ifdef FUNKE_BOLT_AOT
            int (*AotRun)() = nullptr;
#endif
            std::shared_ptr<JobImage> Jobs;
			BoltCompiler* Compiler = nullptr;
			std::string Path;
			std::string Error;
            std::string ScriptClassName;
			std::filesystem::file_time_type Modified{};
			bool EntityScript = false;
            bool HotReloadState = false;
            bool HasPlayReset = false;
            uint64_t ResetEpoch = 0;
            std::unordered_map<uint64_t, std::string> ReloadStates;
			bool ComponentOnly = false;
            bool UiEvents = false;
            bool SceneLoads = false;
            bool InheritsEmptyUpdate = false;
			std::vector<Field> Fields;
			std::unordered_map<std::pair<const Scene*, uint32_t>, uint64_t, AttachmentHash> Attachments;
			std::vector<uint64_t> FreeAttachments;
			std::vector<void*> UpdateSlots;
            std::vector<uint8_t> ReadySlots, WatchedSlots;
            std::vector<uint32_t> DispatchTokens;
			uint64_t NextAttachment = 1;
			bool Active = false;
			bool ReportedRunError = false;
		};

		struct ConsoleRegistration { Program* Module; std::string Description, Usage = "[args...]"; };
        std::unordered_map<std::string, ConsoleRegistration> s_ConsoleCommands, s_ConsoleAttributes;
        thread_local void** s_ActiveConsoleBridge = nullptr;
        uint64_t ConsoleIo(uint64_t, void*, void*) noexcept;
        uint64_t ConsoleExecute(uint64_t, void*, void*) noexcept;
        void ReleaseConsoleCommands(Program* program) {
            auto owned = [=](const auto& item) { return item.second.Module == program; };
            std::erase_if(s_ConsoleCommands, owned);
            std::erase_if(s_ConsoleAttributes, owned);
        }
        struct DispatchSlot {
            Scene* Owner;
            uint32_t Entity;
            const std::string* Identity;
            Program* Module;
            uint64_t Attachment;
        };
        std::vector<DispatchSlot> s_DispatchSlots;
        std::vector<uint32_t> s_FreeDispatchSlots;

        struct CachedSchema {
			std::string Path;
			std::filesystem::file_time_type Modified{};
			std::vector<Field> Fields;
		};
        struct DataInstance {
            std::string Script;
            std::unordered_map<std::string, std::string> Values;
            std::unordered_map<std::string, Json::Value> EnumOptions;
        };
        std::unordered_map<uint64_t, DataInstance> s_DataAssets;
        uint64_t DataAssetAccess(uint64_t operation, uint64_t guid);
		std::unordered_map<std::string, CachedSchema> s_Schemas;
		CompilerAPI s_API;
		std::unordered_map<std::string, Program> s_Programs;
        uint64_t s_PlayResetEpoch = 0;
        bool s_PlayResetActive = false;
		std::chrono::steady_clock::time_point s_NextPoll;
		std::map<std::string, std::string> s_ComponentSources;
		std::optional<ScriptFileStamps> s_LastScriptFiles;
		std::set<std::string> s_QueuedScripts;
		std::map<std::string, std::string> s_ValidationErrors;

        struct PumpSlot { void* Slot; uint64_t Epoch; float Delta; uint32_t EventId, Watched, Scheduled; };
        static_assert(sizeof(void*)!=8 || (offsetof(PumpSlot,Epoch)==8 && offsetof(PumpSlot,Delta)==16 && offsetof(PumpSlot,EventId)==20));
		struct Frame {
			uint32_t Entity;
			bool Started;
			uint64_t State;
			Event Callback;
			uint64_t Attachment;
			Contact Collision;
			std::string Text;
			const std::string& Path;
			const std::string& Identity;
			const std::vector<Field>& Fields;
			std::vector<uint64_t> QueryResults;
			uint32_t SystemHandle = 0;
			Program* EntityProgram = nullptr;
			void* UpdateSlot = nullptr;
            bool PumpingActive = false;
            PumpSlot Pump{};
            EventData Notification;
            std::string LogFile;
            SceneQueryBorrow QueryBorrows;
		};
		thread_local Frame* s_Frame = nullptr;
		uint64_t RuntimeTextureCall(uint64_t operation, void* request, void* bytes) noexcept {
			return s_Frame ? RuntimeTextures::Call(&s_Frame->Path, operation, request, bytes) : 0;
		}
		uint64_t WriteImage(uint64_t length, void* path, void* bytes) noexcept {
            if (!path || !bytes || !length || length > INT_MAX) return 0;
            try {
                const auto target = Path::FromUtf8(static_cast<const char*>(path));
                if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path());
                const auto* data = static_cast<const uint8_t*>(bytes);
                return File::WriteAllBytes(Path::ToUtf8(target), std::vector<uint8_t>(data, data + length));
            } catch (...) { return 0; }
        }
        uint64_t AssetLoadCall(uint64_t operation, void* args, void*) noexcept {
            const auto* words = static_cast<const uint64_t*>(args);
            return s_Frame ? AssetLoads::Call(&s_Frame->Path, operation, words ? words[0] : 0, words ? static_cast<int>(words[1]) : 0) : 0;
        }
#include "Scripting/Bolt/ScriptReferences.inl"

        struct PendingCall {
			Scene* Owner;
			const std::string& Identity;
			Program& Script;
			Frame Context;
			Scene* EventScene = nullptr;
		};
		thread_local std::deque<PendingCall> s_Pending;

		uint64_t EntityId(uint64_t, uint64_t) noexcept { return s_Frame ? s_Frame->Entity : 0; }
		uint64_t FirstUpdate(uint64_t, uint64_t) noexcept { return s_Frame && !s_Frame->Started; }
		uint64_t ReadState(uint64_t, uint64_t) noexcept { return s_Frame ? s_Frame->State : 0; }
		uint64_t WriteState(uint64_t, uint64_t value) noexcept {
			if (!s_Frame) return 0;
			s_Frame->State = value;
			return value;
		}
		uint64_t DeltaMicros(uint64_t, uint64_t) noexcept {
			if (!s_Frame) return 0;
			const double micros = NativeScriptHost::GetEngineAPI().GetDeltaTime() * 1000000.0;
			return std::isfinite(micros) ? static_cast<uint64_t>(std::round(std::clamp(micros, 0.0, double(UINT32_MAX)))) : 0;
		}
		uint64_t MoveRight(uint64_t id, uint64_t milliUnits) noexcept {
			if (!s_Frame || id != s_Frame->Entity || milliUnits > UINT32_MAX) return 0;
			const auto& api = NativeScriptHost::GetEngineAPI();
			float x, y;
			api.GetPosition(s_Frame->Entity, &x, &y);
			api.SetPosition(s_Frame->Entity, x + static_cast<float>(milliUnits) / 1000.0f, y);
			return 1;
		}
		uint64_t Translate(uint64_t dx, uint64_t dy) noexcept {
			if (!s_Frame) return 0;
			const auto& api = NativeScriptHost::GetEngineAPI();
			float x, y;
			api.GetPosition(s_Frame->Entity, &x, &y);
			api.SetPosition(s_Frame->Entity, x + static_cast<float>(static_cast<int64_t>(dx)) / 1000.0f,
				y + static_cast<float>(static_cast<int64_t>(dy)) / 1000.0f);
			return 1;
		}
		uint64_t GetKey(uint64_t code, uint64_t) noexcept {
#if FUNKE_WITH_RENDER
			return s_Frame && code <= UINT16_MAX && Input::GetKey(static_cast<KeyCode>(code));
#else
			return 0;
#endif
		}
		uint64_t LogValue(uint64_t, uint64_t value) noexcept {
			try { FNK_INFO_TAG("Bolt", "{}", value); }
			catch (...) { return 0; }
			return 1;
		}

		void* CurrentUpdateSlot(uint64_t anyEvent, uint64_t) noexcept {
            if (!s_Frame) return nullptr;
            if (anyEvent==2) return sizeof(void*)==8 && s_Frame->Pump.Slot ? &s_Frame->Pump : nullptr;
            return anyEvent || s_Frame->Callback==Event::Update ? s_Frame->UpdateSlot : nullptr;
		}
		uint64_t RememberUpdateSlot(uint64_t key, void* slot, void*) noexcept {
			if (s_Frame && s_Frame->EntityProgram && key == s_Frame->Attachment &&
				key < s_Frame->EntityProgram->UpdateSlots.size())
				s_Frame->EntityProgram->UpdateSlots[key] = slot;
			return 0;
		}

		uint64_t Attachment(uint64_t, uint64_t) noexcept { return s_Frame ? (s_Frame->SystemHandle ? s_Frame->SystemHandle : s_Frame->Attachment) : 0; }
		uint64_t Callback(uint64_t, uint64_t) noexcept { return s_Frame ? static_cast<uint64_t>(s_Frame->Callback) : 0; }
		uint64_t PersistentEntityId(uint64_t, uint64_t) noexcept {
			auto* scene = ScriptEngine::GetScene();
			return scene && s_Frame ? GetEntityScriptId(*scene, static_cast<EntityHandle>(s_Frame->Entity)) : 0;
		}

		uint64_t TouchValue(uint64_t index, uint64_t part) noexcept {
			if (!s_Frame || index > INT_MAX) return 0;
			const auto get = ScriptApi().Input_GetTouch;
			int64_t id{}; int phase{}; float xy[4]{};
			if (!get || !get(static_cast<int>(index), &id, &phase, xy)) return 0;
			switch (part) {
				case 0: return 1;
				case 1: return static_cast<uint64_t>(id);
				case 2: return static_cast<uint64_t>(phase);
				case 3: return Pack(xy[0], xy[1]);
				case 4: return Pack(xy[2], xy[3]);
				default: return 0;
			}
		}
		uint64_t ContactValue(uint64_t index, uint64_t) noexcept {
			if (!s_Frame) return 0;
			const auto& c = s_Frame->Collision;
			switch (index) {
				case 0: return c.Other;
				case 1: return c.A;
				case 2: return c.B;
				case 3: return Pack(c.X, c.Y);
				case 4: return Encode(c.Z);
				case 5: return Pack(c.NormalX, c.NormalY);
				case 6: return Encode(c.NormalZ);
				default: return 0;
			}
		}
		uint64_t CreateUnnamedEntity(uint64_t, uint64_t) noexcept {
			return s_Frame ? ScriptApi().Entity_Create(nullptr) : 0;
		}
        uint64_t TakeReloadState(uint64_t, uint64_t) noexcept {
            if (!s_Frame || !s_Frame->EntityProgram) return 0;
            auto saved = s_Frame->EntityProgram->ReloadStates.extract(s_Frame->Attachment);
            if (saved.empty()) return 0;
            s_Frame->Text = std::move(saved.mapped());
            return s_Frame->Text.size();
        }

		uint64_t TextByte(uint64_t byte, uint64_t reset) noexcept {
			if (!s_Frame) return 0;
			try {
				if (reset == 2) return byte < s_Frame->Text.size() ? static_cast<unsigned char>(s_Frame->Text[byte]) : 0;
				if (reset) s_Frame->Text.clear();
				else s_Frame->Text.push_back(static_cast<char>(byte));
			} catch (...) { return 0; }
			return 1;
		}
		ScriptComponent* ComponentStore(uint64_t id, bool create) {
			Scene* scene = nullptr;
			EntityHandle handle = entt::null;
			if (!ResolveEntityReference(id, scene, handle)) return nullptr;
			auto entity = scene->GetEntity(handle);
			if (create) return &entity.GetOrAddComponent<ScriptComponent>();
			return entity.HasComponent<ScriptComponent>() ? &entity.GetComponent<ScriptComponent>() : nullptr;
		}
		uint64_t CustomHas(uint64_t id, uint64_t guid) {
			auto* store = ComponentStore(id, false);
			return store && store->HasScript(std::to_string(guid), ScriptType::Bolt);
		}
		uint64_t CustomAdd(uint64_t id, uint64_t guid) {
			auto* store = ComponentStore(id, true);
			return store && store->AddScript(std::to_string(guid), ScriptType::Bolt);
		}
		uint64_t CustomRemove(uint64_t id, uint64_t guid) {
			Scene* scene = nullptr;
			EntityHandle handle = entt::null;
			if (!ResolveEntityReference(id, scene, handle) || !scene->HasComponent<ScriptComponent>(handle)) return 0;
			auto& scripts = scene->GetComponent<ScriptComponent>(handle).Scripts;
			const auto identity = std::to_string(guid);
			for (size_t i=0; i<scripts.size(); ++i)
				if (scripts[i].GetType()==ScriptType::Bolt && scripts[i].GetClassName()==identity)
					return ScriptSystem::RemoveScript(scene->GetEntity(handle), i);
			return 0;
		}
		uint64_t CustomRead(uint64_t id, uint64_t) {
			auto* store = ComponentStore(id, false);
			if (!s_Frame || !store) return 0;
			auto it = store->PendingFieldValues.find(s_Frame->Text);
			if (it == store->PendingFieldValues.end()) return 0;
			s_Frame->Text = it->second;
			return s_Frame->Text.size() + 1;
		}
		uint64_t CustomWrite(uint64_t id, uint64_t) {
			auto* store = ComponentStore(id, false);
			if (!s_Frame || !store) return 0;
			const auto& text = s_Frame->Text;
			auto split = text.find('=');
			auto dot = text.find('.');
			if (split == text.npos || dot >= split || !store->HasScript(text.substr(0, dot), ScriptType::Bolt)) return 0;
			store->PendingFieldValues[text.substr(0,split)] = text.substr(split+1);
			return 1;
		}

		ManagedFieldAccess s_ManagedFieldAccess = nullptr;
		template<auto Member> uint64_t ManagedMembership(uint64_t id, uint64_t) {
			if (!s_Frame) return 0;
			return (ScriptApi().*Member)(id, s_Frame->Text.c_str());
		}
		template<bool Write> uint64_t ManagedField(uint64_t id, uint64_t) {
			if (!s_Frame || !s_ManagedFieldAccess) return 0;
			const auto message = s_Frame->Text;
			const auto end = Write ? message.find('=') : message.size();
			if (end == message.npos) return 0;
			const auto split = message.rfind('.', end);
			if (split == message.npos || split >= end) return 0;
			const auto type = message.substr(0, split), field = message.substr(split + 1, end - split - 1);
			auto* result = s_ManagedFieldAccess(id, type.c_str(), field.c_str(), Write ? message.c_str() + end + 1 : nullptr);
			if (!result) return 0;
			if constexpr (Write) return 1;
			s_Frame->Text = result;
			return s_Frame->Text.size() + 1;
		}
        uint64_t ManagedTaskState(uint64_t id,uint64_t) {
            const char* value=s_ManagedFieldAccess?s_ManagedFieldAccess(id,"<Task>","state",nullptr):nullptr;
            return value && value[0]>='0' && value[0]<='3' ? value[0]-'0' : 3;
        }
        uint64_t ManagedTaskText(uint64_t id,uint64_t fault) {
            if(!s_Frame) return 0;
            const char* value=s_ManagedFieldAccess?s_ManagedFieldAccess(id,"<Task>",fault?"error":"result",nullptr):nullptr;
            s_Frame->Text=value?value:"Managed task is unavailable";
            return s_Frame->Text.size();
        }
        uint64_t ManagedToken(uint64_t handle,uint64_t release) {
            const char* value=s_ManagedFieldAccess?s_ManagedFieldAccess(handle,"<CancellationToken>",release?"release":"state",nullptr):nullptr;
            return !value || *value!='0';
        }
        uint64_t ManagedTaskRelease(uint64_t id,uint64_t) {
            if(s_ManagedFieldAccess) s_ManagedFieldAccess(id,"<Task>","release",nullptr);
            return 0;
        }
		struct System;
		struct SystemProgram : Program {
			std::deque<std::tuple<std::shared_ptr<System>, Event, Scene*, EventData>> Pending;
			bool Running = false;
			~SystemProgram() {
				ReleaseConsoleCommands(this);
				SceneLoads::ReleaseGeneration(&Path);
				AssetLoads::ReleaseGeneration(&Path);
				RuntimeTextures::ReleaseGeneration(&Path);
				if (Compiler) s_API.Destroy(Compiler);
			}
		};
		std::unordered_map<std::string, std::weak_ptr<SystemProgram>> s_SystemPrograms;
		struct System {
			std::string Identity;
			Scene* Owner = nullptr;
			std::shared_ptr<SystemProgram> Module;
			std::unordered_map<std::string,std::string> Overrides;
			bool Awake = false, Enabled = false, Started = false;
			bool Global = false;
			uint32_t Handle = 0;
		};
		std::map<uint32_t, std::shared_ptr<System>> s_Systems;
		uint32_t s_NextSystem = 0x80000000u;
		thread_local System* s_CurrentSystem = nullptr;

		struct CoroutineOwner {
			Scene* Owner;
			uint32_t Entity;
			std::string Identity;
			uint32_t SystemHandle;
			Program* Module = nullptr;
            uint64_t Attachment = 0;
            auto operator<=>(const CoroutineOwner& rhs) const {
                return std::tie(Owner,Entity,Identity,SystemHandle)<=>std::tie(rhs.Owner,rhs.Entity,rhs.Identity,rhs.SystemHandle);
            }
		};
		class CoroutineOwners {
            std::set<CoroutineOwner> members;
            std::shared_ptr<const std::vector<CoroutineOwner>> snapshot;
        public:
            bool empty() const { return members.empty(); }
            bool contains(const CoroutineOwner& key) const { return members.contains(key); }
            void insert(const CoroutineOwner& key) { if (members.insert(key).second) snapshot.reset(); }
            void erase(const CoroutineOwner& key) { if (members.erase(key)) snapshot.reset(); }
            template<class F> void EraseIf(F match) { if (std::erase_if(members,match)) snapshot.reset(); }
            auto Snapshot() {
                if (!snapshot) snapshot=std::make_shared<std::vector<CoroutineOwner>>(members.begin(),members.end());
                return snapshot;
            }
            bool Current(const std::shared_ptr<const std::vector<CoroutineOwner>>& value) const { return value==snapshot; }
        };
        CoroutineOwners s_Coroutines;
		uint64_t s_Epoch = 0;
		float s_CoroutineDelta = 0;
		uint64_t CoroutineClock(uint64_t delta, uint64_t) {
			return delta ? Encode(s_CoroutineDelta) : s_Epoch;
		}
		uint64_t ScheduleCoroutine(uint64_t active, uint64_t) {
			if (!s_Frame) return 0;
            if (active==2 || active==3) {
                s_Frame->Pump.Watched=active==2;
                if (s_Frame->EntityProgram && s_Frame->Attachment<s_Frame->EntityProgram->WatchedSlots.size())
                    s_Frame->EntityProgram->WatchedSlots[s_Frame->Attachment]=active==2;
                if (active==3) return 0;
                active=1;
            }
            if (s_Frame->Callback == Event::Destroy || s_Frame->Callback==Event::CancelCoroutines || s_Frame->Callback==Event::ReleaseSystemState) return 0;
			if (active && (s_Frame->Callback==Event::PumpUpdate || s_Frame->Callback==Event::PumpFixed) && s_Frame->PumpingActive) return 0;
            const bool system = s_CurrentSystem && !s_Frame->Attachment && s_CurrentSystem->Identity == s_Frame->Identity;
			CoroutineOwner key{system ? nullptr : ScriptEngine::GetScene(), s_Frame->Entity, s_Frame->Identity, system ? s_CurrentSystem->Handle : 0,s_Frame->EntityProgram,s_Frame->Attachment};
			if (active) s_Coroutines.insert(key); else s_Coroutines.erase(key);
            s_Frame->PumpingActive=active!=0;
            s_Frame->Pump.Scheduled=active!=0;
			return 0;
		}
		Scene* QueryScene(std::string_view name) {
			auto* current = ScriptEngine::GetScene();
			if (current && current->GetName() == name) return current;
			return SceneManager::GetLoadedScene(std::string(name)).lock().get();
		}
		uint64_t SceneInfo(uint64_t, uint64_t count) {
			auto* scene = s_Frame ? QueryScene(s_Frame->Text) : nullptr;
			if (!scene) return 0;
			if (!count) return scene->IsLoaded();
			return scene->IsLoaded() ? scene->GetEntityCount() : 0;
		}
		struct QueryRequirement { uint64_t Id; bool Custom; std::string SerializedName; };
		std::vector<QueryRequirement> QueryRequirements(std::string_view text) {
			std::vector<QueryRequirement> result;
			while (!text.empty()) {
				auto end = text.find('|');
				auto key = text.substr(0,end);
				if (key.starts_with("@")) {
					result.push_back({0, false, "type:" + std::string(key.substr(1))});
				} else if (key.starts_with("bolt:") || key.starts_with("type:")) {
					result.push_back({0, false, std::string(key)});
				} else if (!key.empty()) {
					const bool custom=key.front()=='#';
					if (custom) key.remove_prefix(1);
					uint64_t id=UINT64_MAX;
					auto parsed=std::from_chars(key.data(),key.data()+key.size(),id);
					result.push_back({parsed.ec==std::errc{} && parsed.ptr==key.data()+key.size() ? id : UINT64_MAX,custom});
				}
				if (end==text.npos) break;
				text.remove_prefix(end+1);
			}
			return result;
		}
		uint64_t SceneQuery(uint64_t, uint64_t filter) {
			if (!s_Frame) return 0;
			auto& ids=s_Frame->QueryResults;
			ids.clear();
			std::string_view text=s_Frame->Text;
			const auto first = text.find('\n');
			if (first == text.npos) return 0;
			const auto second = text.find('\n', first + 1);
			if (second == text.npos) return 0;
			auto* scene=QueryScene(text.substr(0,first));
			if (!scene) return 0;
			const auto required=QueryRequirements(text.substr(first+1,second-first-1));
			const auto excluded=QueryRequirements(text.substr(second+1));
			for (auto h: scene->GetRegistry().view<entt::entity>()) {
				const auto id=GetEntityScriptId(*scene,h);
				if (!id) continue;
				if (filter && (ScriptApi().Entity_GetIsEnabledInHierarchy(id)!=0)!=(filter==1)) continue;
				auto has=[id](const QueryRequirement& key) {
					if (!key.SerializedName.empty()) return ScriptApi().Entity_HasComponent(id, key.SerializedName.c_str()) != 0;
					return key.Custom ? CustomHas(id,key.Id)!=0 : ComponentCall<&NativeBindings::Entity_HasComponent>(id,key.Id)!=0;
				};
				if (std::all_of(required.begin(),required.end(),has) && std::none_of(excluded.begin(),excluded.end(),has)) ids.push_back(id);
			}
			return ids.size();
		}
		uint64_t QueryResult(uint64_t index, uint64_t) {
			return s_Frame && index<s_Frame->QueryResults.size() ? s_Frame->QueryResults[index] : 0;
		}
		uint64_t ToggleSceneSystem(uint64_t, uint64_t mode) {
			if (!s_Frame) return 0;
			const auto split = s_Frame->Text.find('\n');
			if (split == std::string::npos) return 0;
			const auto name = s_Frame->Text.substr(0, split), identity = s_Frame->Text.substr(split + 1);
			auto* scene = QueryScene(name);
			if (!scene) return 0;
			return mode ? scene->SetSceneSystemEnabled(identity, mode == 1) : scene->IsSceneSystemEnabled(identity);
		}
		uint64_t ToggleGlobalSystem(uint64_t, uint64_t enabled) {
			if (s_Frame) {
				const std::string identity = s_Frame->Text;
				ScriptEngine::SetGlobalSystemEnabled(identity, enabled != 0);
			}
			return 0;
		}
		uint64_t SystemSceneName(uint64_t, uint64_t) {
			if (!s_Frame || !s_CurrentSystem || !s_CurrentSystem->Owner) return 0;
			s_Frame->Text = s_CurrentSystem->Owner->GetName();
			return s_Frame->Text.size();
		}
		uint64_t SystemEnabled(uint64_t write, uint64_t value) {
			if (!s_CurrentSystem || !s_CurrentSystem->Owner) return 0;
			auto& scene = *s_CurrentSystem->Owner;
			if (write) scene.SetSceneSystemEnabled(s_CurrentSystem->Identity, value != 0);
			return scene.IsSceneSystemEnabled(s_CurrentSystem->Identity);
		}
		struct Inspection {
			const std::string* Filter;
			std::vector<Field>* Snapshot;
		};
		thread_local Inspection s_Inspection{};
		uint64_t FieldRead(uint64_t index, uint64_t) noexcept {
			if (!s_Frame || index >= s_Frame->Fields.size()) return 0;
			try {
				const auto& name = s_Frame->Fields[index].Name;
				if (s_Inspection.Filter && name != *s_Inspection.Filter) return 0;
				if (s_CurrentSystem && s_Frame->Attachment == 0 && s_Frame->Identity == s_CurrentSystem->Identity) {
					auto it = s_CurrentSystem->Overrides.find(name);
					if (it == s_CurrentSystem->Overrides.end()) return 0;
					s_Frame->Text = it->second;
					return s_Frame->Text.size() + 1;
				}
				auto* scene = ScriptEngine::GetScene();
				auto entity = static_cast<EntityHandle>(s_Frame->Entity);
				if (!scene || !scene->IsValid(entity) || !scene->HasComponent<ScriptComponent>(entity)) return 0;
				const auto& values = scene->GetComponent<ScriptComponent>(entity).PendingFieldValues;
				auto value = values.find(s_Frame->Identity + "." + name);
				if (value == values.end()) return 0;
				s_Frame->Text = value->second;
				return s_Frame->Text.size() + 1;
			} catch (...) { return 0; }
		}
		uint64_t FieldWrite(uint64_t index, uint64_t) noexcept {
			auto* fields = s_Inspection.Snapshot;
			if (!s_Frame || !fields || index >= fields->size()) return 0;
			try { (*fields)[index].Value = s_Frame->Text; } catch (...) { return 0; }
			return 1;
		}
		struct CompilerMode {
            int Profile = BOLT_HOST_DEBUG;
            std::filesystem::path Llvm;
        };

        CompilerMode CurrentCompilerMode() {
            if (Application::IsEditor()) return {};
            if (const auto* project = ProjectManager::GetCurrentProject()) {
                std::error_code ec;
                if (std::filesystem::equivalent(Path::FromUtf8(project->RootDirectory), Path::FromUtf8(Path::ExecutableDir()), ec)) {
                    if (project->ActiveBuildProfile == FunkeProject::BuildProfile::Release)
                        return {BOLT_HOST_RELEASE, Platform::GetExecutablePath().parent_path() / DesktopLlvmRelativePath()};
                    return {};
                }
            }
            const char* profile = std::getenv("FUNKE_BOLT_PROFILE");
            return {profile && std::string_view(profile) == "release" ? BOLT_HOST_RELEASE : BOLT_HOST_DEBUG, {}};
        }
		uint64_t LogText(uint64_t level, uint64_t line) noexcept {
			if (!s_Frame || level > 3) return 0;
			const auto& api = ScriptApi();
			const auto fn = level == 0 ? api.Log_Trace : level == 1 ? api.Log_Info : level == 2 ? api.Log_Warn : api.Log_Error;
			fn(s_Frame->Text.c_str(), s_Frame->LogFile.empty() ? s_Frame->Path.c_str() : s_Frame->LogFile.c_str(), static_cast<int>(line));
            if (s_Frame->Callback != Event::LogMessage) {
                try { BroadcastApplicationEvent(Event::LogMessage, nullptr, {0, s_Frame->Text}); } catch (...) {}
            }
			return 1;
		}
        thread_local uint64_t s_QueryPartition=0;
        uint64_t RunJobRanges(uint64_t parts,void* entry,void* data) noexcept {
            if (!parts || parts>INT_MAX || !entry || !data) return 0;
            auto run=reinterpret_cast<int32_t (*)(void*)>(entry);
            std::atomic<bool> completed{true};
            try {
                auto execute=[&](size_t i) {
                    const auto previous=std::exchange(s_QueryPartition,i);
                    if (!run(data)) completed.store(false,std::memory_order_relaxed);
                    s_QueryPartition=previous;
                };
                if (parts==1) execute(0);
                else ParallelFor(0,static_cast<size_t>(parts),execute,1);
                return completed.load(std::memory_order_relaxed);
            } catch (...) { return 0; }
        }
        struct JobView { std::vector<void*> Cells; int Rows = 0; void* Dense = nullptr; };
        std::mutex s_JobViewMutex;
        std::map<uint64_t,JobView> s_JobViews;
        std::vector<void*> s_FreeJobCells;
        struct QuerySpec {
            std::array<std::string,5> Names;
            uint64_t Generation = 0, Token = 0;
        };
        std::array<QuerySpec,256> s_QueryPlans{};
        uint64_t s_NextQueryPlan=1;
        uint64_t RememberQueryPlan(uint64_t, uint64_t) {
            if (!s_Frame) return 0;
            QuerySpec spec{{},SceneManager::GetComponentRegistry().GetGeneration()};
            std::string_view rest=s_Frame->Text;
            for (size_t i=0;i<4;i++) {
                auto split=rest.find('\n');
                if (split==rest.npos) return 0;
                spec.Names[i]=rest.substr(0,split);
                rest.remove_prefix(split+1);
            }
            spec.Names[4]=rest;
            const auto token=s_NextQueryPlan++;
            spec.Token=token;
            s_QueryPlans[token%s_QueryPlans.size()]=std::move(spec);
            return token;
        }
        uint64_t OpenJobView(uint64_t enable, uint64_t plan) noexcept {
            if (!s_Frame || enable>2) return 0;
            uint64_t borrow=0;
            try {
                if (!plan) plan=RememberQueryPlan(0,0);
                if (!plan) return 0;
                const auto& spec=s_QueryPlans[plan%s_QueryPlans.size()];
                if (spec.Token!=plan || spec.Generation!=SceneManager::GetComponentRegistry().GetGeneration()) return UINT64_MAX;
                const auto& [scene,writes,reads,required,excluded]=spec.Names;
                auto columns=[](const std::string& names)->size_t{return names.empty()?0:1+std::count(names.begin(),names.end(),'|');};
                size_t stride=columns(writes)+columns(reads);
                if(!stride) return 0;
                const auto& api=ScriptApi();
                borrow=api.Scene_BeginJobQueryBorrow(scene.c_str(),writes.c_str(),reads.c_str());
                if(!borrow) return 0;
                JobView view{};
                const auto& column=writes.empty()?reads:writes;
                if (stride==1 && excluded.empty() && (required.empty() || required==column)) {
                    auto owner=SceneManager::GetLoadedScene(scene).lock();
                    const auto* info=SceneManager::GetComponentRegistry().FindBySerializedName(column);
                    if (owner && owner->IsLoaded() && info && info->IsModuleActive() && info->dynamicStorage) {
                        auto& registry=owner->GetRegistry();
                        const auto* disabled=registry.storage(entt::type_hash<DisabledTag>::value());
                        if (!enable || !disabled || disabled->empty()) {
                            auto pool=info->dynamicStorage->GetSceneView(registry);
                            if (pool.Size()>INT_MAX) throw std::length_error("ECS job view too large");
                            if (enable!=2 && pool.Size()) {
                                view.Rows=static_cast<int>(pool.Size());
                                view.Dense=pool.Get(pool.Entities().front());
                            }
                            std::lock_guard lock(s_JobViewMutex);
                            s_JobViews.emplace(borrow,std::move(view));
                            return borrow;
                        }
                    }
                }
                { std::lock_guard lock(s_JobViewMutex); view.Cells.swap(s_FreeJobCells); }
                int capacity=static_cast<int>(std::min<size_t>(INT_MAX,view.Cells.size()/stride));
                for(;;) {
                    view.Rows=api.Scene_OpenQueryView(scene.c_str(),writes.c_str(),reads.c_str(),required.c_str(),excluded.c_str(),static_cast<int>(enable),view.Cells.data(),capacity);
                    if(view.Rows<0) throw std::runtime_error("ECS job view failed");
                    if(view.Rows<=capacity) break;
                    capacity=view.Rows;
                    if(static_cast<size_t>(capacity)>view.Cells.max_size()/stride) throw std::length_error("ECS job view too large");
                    view.Cells.resize(stride*capacity);
                }
                std::lock_guard lock(s_JobViewMutex);
                s_JobViews.emplace(borrow,std::move(view));
                return borrow;
            } catch(...) { if(borrow) ScriptApi().Scene_EndJobQueryBorrow(borrow); return 0; }
        }
        void* JobRows(uint64_t borrow,uint64_t dense) {
            std::lock_guard lock(s_JobViewMutex);
            const auto view=s_JobViews.find(borrow);
            return view==s_JobViews.end()?nullptr:(dense?view->second.Dense:view->second.Cells.data());
        }
        uint64_t JobRowCount(uint64_t borrow,uint64_t) {
            std::lock_guard lock(s_JobViewMutex);
            const auto view=s_JobViews.find(borrow);
            return view==s_JobViews.end()?0:view->second.Rows;
        }
        uint64_t CloseJobView(uint64_t borrow,uint64_t) {
            {
                std::lock_guard lock(s_JobViewMutex);
                auto view=s_JobViews.find(borrow);
                if (view==s_JobViews.end()) return 0;
                if (s_FreeJobCells.size()<view->second.Cells.size()) s_FreeJobCells.swap(view->second.Cells);
                s_JobViews.erase(view);
            }
            ScriptApi().Scene_EndJobQueryBorrow(borrow);
            return 1;
        }
        struct LayoutKey {
            std::string BoltName, ManagedName;
            uint64_t Size;
            uint64_t Generation = 0;
            const ComponentInfo* Info = nullptr;
        };
        std::vector<LayoutKey> s_LayoutKeys;
        void* NativeStructAddress(uint64_t entity, uint64_t request) {
            const auto token=request >> 1;
            const bool readOnly=(request & 1)!=0;
            if (s_Frame && token && token<=s_LayoutKeys.size()) {
                auto& layout=s_LayoutKeys[token-1];
                const auto& registry=SceneManager::GetComponentRegistry();
                if (layout.Generation != registry.GetGeneration()) {
                    layout.Info = NativeLayout(layout.BoltName, layout.Size);
                    if (!layout.Info) layout.Info = NativeLayout(layout.ManagedName, layout.Size);
                    layout.Generation=registry.GetGeneration();
                }
                const auto* info=layout.Info;
                if (!info || !info->IsModuleActive() || !info->getRaw) return nullptr;
                if (!info->isDynamic) {
                    const auto& name=info->serializedName;
                    return readOnly ? ScriptApi().Entity_GetComponentPtrReadOnly(entity,name.c_str()) : ScriptApi().Entity_GetComponentPtr(entity,name.c_str());
                }
                Scene* scene=nullptr;
                EntityHandle handle=entt::null;
                if (!ResolveEntityReference(entity,scene,handle)) return nullptr;
                return info->getRaw(scene->GetEntity(handle));
            }
            if (token) return nullptr;
            if (!s_Frame) return nullptr;
            const auto name=s_Frame->Text.c_str();
            return readOnly ? ScriptApi().Entity_GetComponentPtrReadOnly(entity,name) : ScriptApi().Entity_GetComponentPtr(entity,name);
        }
        uint64_t NativeStructId(uint64_t size, uint64_t cache) {
            if (!s_Frame) return 0;
            if (cache) {
                const auto bolt=cache == 2 ? s_Frame->Text : "bolt:"+s_Frame->Text;
                for (size_t i=0;i<s_LayoutKeys.size();++i)
                    if (s_LayoutKeys[i].Size==size && s_LayoutKeys[i].BoltName==bolt) return i+1;
                s_LayoutKeys.push_back({bolt,cache == 2 ? s_Frame->Text : "type:"+s_Frame->Text,size});
                return s_LayoutKeys.size();
            }
            const auto name=s_Frame->Text.c_str();
            return NativeLayout(name, size) ? ScriptApi().Component_GetTypeId(name) : 0;
        }
		std::string DataPathSegment(std::string value, const char* fallback) {
			const auto first = value.find_first_not_of(" \t\r\n");
			if (first == value.npos) return fallback;
			value = value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
			for (char& c : value) {
				if (c == '/' || c == '\\'
#ifdef FNK_PLATFORM_WINDOWS
					|| static_cast<unsigned char>(c) < 32 || std::string_view("<>:\"|?*").find(c) != std::string_view::npos
#endif
				) c = '_';
			}
			value.erase(value.find_last_not_of(". ") + 1);
			return value.empty() ? fallback : value;
		}

        std::filesystem::path GameDataDirectory(bool temporary) {
            const auto root = temporary ? std::filesystem::temp_directory_path() : Platform::GetUserDataRoot();
            if (root.empty()) return {};
            const auto& api = ScriptApi();
            auto read = [](auto get) -> std::string {
                if (!get) return {};
                const int bytes = get(nullptr, 0);
                if (bytes <= 0) return {};
                std::string text(static_cast<size_t>(bytes) + 1, '\0');
                get(text.data(), static_cast<int>(text.size()));
                text.resize(std::strlen(text.c_str()));
                return text;
            };
            auto directory = root / Path::FromUtf8(DataPathSegment(read(api.Application_GetCompanyBuffer), "DefaultCompany"))
                / Path::FromUtf8(DataPathSegment(read(api.Application_GetNameBuffer), "UntitledGame"));
            const auto profile = temporary ? std::string{} : read(api.Application_GetPrefsProfileBuffer);
            if (profile.find_first_not_of(" \t\r\n") != profile.npos)
                directory = directory / "Profiles" / Path::FromUtf8(DataPathSegment(profile, "Test"));
            return directory;
        }
        uint64_t ApplicationDataDirectory(uint64_t temporary, uint64_t) noexcept {
            if (!s_Frame) return 0;
            try {
                const auto directory = GameDataDirectory(temporary != 0);
                if (directory.empty()) return 0;
                std::filesystem::create_directories(directory);
                s_Frame->Text = Path::ToUtf8(directory);
                return s_Frame->Text.size();
            } catch (const std::exception& error) {
                FNK_CORE_ERROR_TAG("Bolt", "Could not open game data directory: {}", error.what());
                return 0;
            }
        }
        uint64_t SaveFile(uint64_t op, uint64_t keyBytes) noexcept {
            if (!s_Frame || keyBytes > s_Frame->Text.size()) return uint64_t(-2);
            try {
                const auto input = s_Frame->Text;
                if (!ApplicationDataDirectory(0, 0)) return uint64_t(-3);
                const auto folder = Path::FromUtf8(s_Frame->Text) / "Saves";
                if (op == 4) {
                    std::vector<std::string> names;
                    if (std::filesystem::exists(folder)) {
                        for (const auto& entry : std::filesystem::directory_iterator(folder))
                            if (entry.is_regular_file() && entry.path().extension() == ".json")
                                names.push_back(Path::ToUtf8(entry.path().stem()));
                    }
                    auto fold = [](std::string str) {
                        for (char& c : str) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
                        return str;
                    };
                    std::sort(names.begin(), names.end(), [&](const auto& a, const auto& b) {
                        return std::pair{fold(a), a} < std::pair{fold(b), b};
                    });
                    auto list = Json::Value::MakeArray();
                    for (auto& name : names) list.GetArray().emplace_back(std::move(name));
                    s_Frame->Text = Json::Stringify(list);
                    return s_Frame->Text.size();
                }
                const auto raw = input.substr(0, keyBytes);
                const auto start = raw.find_first_not_of(" \t\r\n");
                if (start == raw.npos) return uint64_t(-2);
                const auto key = raw.substr(start, raw.find_last_not_of(" \t\r\n") - start + 1);
                if (key.empty() || DataPathSegment(key, "") != key) return uint64_t(-2);
                const auto path = folder / Path::FromUtf8(key + ".json");
                switch (op) {
                case 0: return std::filesystem::is_regular_file(path);
                case 1:
                    if (!std::filesystem::is_regular_file(path)) return uint64_t(-1);
                    s_Frame->Text = File::ReadAllText(Path::ToUtf8(path));
                    return s_Frame->Text.size();
                case 2:
                    std::filesystem::create_directories(folder);
                    return File::WriteAllText(Path::ToUtf8(path), input.substr(keyBytes)) ? 0 : uint64_t(-3);
                case 3:
                    std::filesystem::remove(path);
                    return 0;
                default: return uint64_t(-2);
                }
            } catch (...) { return uint64_t(-3); }
        }


		template<auto Member>
		constexpr BoltHostFunction SlotBinding = ApiCall<Member>;
#if FUNKE_WITH_2D
		template<bool Set>
		uint64_t WorldPosition(uint64_t id, uint64_t bits) noexcept {
			if (!s_Frame || !id) return 0;
			try {
				Scene* scene = GetCurrentExecutionScene();
				if (!scene) scene = ScriptEngine::GetScene();
				EntityHandle entity = static_cast<EntityHandle>(s_Frame->Entity);
				const auto* meta = scene && s_Frame->EntityProgram
					? scene->GetRegistry().try_get<EntityMetaDataComponent>(entity) : nullptr;
				if (!meta || !meta->MetaData.RuntimeID || scene->TranslateEntityRef(id) != meta->MetaData.RuntimeID) {
					if (!ResolveEntityReference(id, scene, entity)) return 0;
				}
				auto* transform = scene->GetRegistry().try_get<Transform2DComponent>(entity);
				if (!transform) return 0;
				if constexpr (Set) {
					ScriptTransform2D::SetWorldPosition(*scene, entity, *transform,
						{ Decode<float>(bits), Decode<float>(bits >> 32) });
					return 0;
				}
				return Pack(transform->Position.x, transform->Position.y);
			} catch (...) { return 0; }
		}
		template<> constexpr BoltHostFunction SlotBinding<&NativeBindings::Transform2D_GetPosition> = WorldPosition<false>;
		template<> constexpr BoltHostFunction SlotBinding<&NativeBindings::Transform2D_SetPosition> = WorldPosition<true>;
#endif
		enum class SpriteField { PixelsPerUnit, FlipX, FlipY };
		template<SpriteField Field, bool Write>
		uint64_t SpriteValue(uint64_t id, uint64_t bits) noexcept {
#if FUNKE_WITH_2D
			if (!s_Frame) return 0;
			Scene* scene = nullptr;
			EntityHandle handle = entt::null;
			if (!ResolveEntityReference(id, scene, handle)) return 0;
			auto* sprite = scene->GetRegistry().try_get<SpriteRendererComponent>(handle);
			if (!sprite) return 0;
			if constexpr (Field == SpriteField::PixelsPerUnit) {
				if constexpr (Write) sprite->PixelsPerUnit = std::max(Decode<float>(bits), 0.0f);
				return Encode(sprite->PixelsPerUnit);
			} else {
				bool& flip = Field == SpriteField::FlipX ? sprite->FlipX : sprite->FlipY;
				if constexpr (Write) flip = bits != 0;
				return flip;
			}
#else
			(void)id;
			(void)bits;
			return 0;
#endif
		}

		constexpr BoltHostBinding s_Bindings[] = {
			{"FunkeEntity", EntityId}, {"FunkeDeltaMicros", DeltaMicros}, {"FunkeMoveRight", MoveRight},
			{"FunkeFirstUpdate", FirstUpdate}, {"FunkeReadState", ReadState}, {"FunkeWriteState", WriteState},
			{"FunkeLogValue", LogValue}, {"FunkeTranslate", Translate}, {"FunkeGetKey", GetKey},
			{"FkCoroutineClock", CoroutineClock}, {"FkCoroutineSchedule", ScheduleCoroutine},
			{"FkCustomHas", CustomHas}, {"FkCustomAdd", CustomAdd}, {"FkCustomRemove", CustomRemove},
			{"FkCustomRead", CustomRead}, {"FkCustomWrite", CustomWrite},
			{"FkManagedHas", ManagedMembership<&NativeBindings::Entity_HasComponent>},
			{"FkManagedAdd", ManagedMembership<&NativeBindings::Entity_AddComponent>},
			{"FkManagedRemove", ManagedMembership<&NativeBindings::Entity_RemoveComponent>},
			{"FkManagedToken",ManagedToken}, {"FkManagedTaskState",ManagedTaskState}, {"FkManagedTaskText",ManagedTaskText}, {"FkManagedTaskRelease",ManagedTaskRelease},
            {"FkManagedRead", ManagedField<false>}, {"FkManagedWrite", ManagedField<true>},
			{"FkWorkerCount", [](uint64_t,uint64_t)->uint64_t{return ScriptApi().JobSystem_GetWorkerCount();}},
            {"FkWorkerIndex", [](uint64_t,uint64_t)->uint64_t{return static_cast<uint64_t>(ScriptApi().JobSystem_GetCallerWorkerIndex()+1);}},
            {"FkWorkerConfigure", [](uint64_t count,uint64_t)->uint64_t{return ScriptApi().JobSystem_Reconfigure(static_cast<int>(count));}},
            {"FkWaitJob", [](uint64_t job,uint64_t)->uint64_t{ScriptApi().JobSystem_Wait(job);return 0;}},
            {"FkRememberQueryPlan",RememberQueryPlan}, {"FkQueryPartition", [](uint64_t,uint64_t)->uint64_t{return s_QueryPartition;}}, {"FkNativeStructId", NativeStructId}, {"FkOpenJobView",OpenJobView}, {"FkJobRowCount",JobRowCount}, {"FkCloseJobView",CloseJobView},
            {"FkNativeAdd", [](uint64_t e,uint64_t id)->uint64_t{return ScriptApi().Entity_AddComponentByTypeId(e,static_cast<uint32_t>(id));}},
            {"FkNativeHas", [](uint64_t e,uint64_t id)->uint64_t{return ScriptApi().Entity_HasComponentByTypeId(e,static_cast<uint32_t>(id));}},
            {"FkNativeRemove", [](uint64_t e,uint64_t id)->uint64_t{return ScriptApi().Entity_RemoveComponentByTypeId(e,static_cast<uint32_t>(id));}},
            {"FkDataAsset", DataAssetAccess},
            {"FkScriptReference", ScriptReference},
            {"FkSceneQueryBorrow", [](uint64_t op, uint64_t token) -> uint64_t {
                return s_Frame ? s_Frame->QueryBorrows.Call(op, token, s_Frame->Text) : 0;
            }},
            {"FkSceneQuery", SceneQuery}, {"FkQueryResult", QueryResult}, {"FkSceneInfo", SceneInfo},
			{"FkToggleSceneSystem", ToggleSceneSystem}, {"FkToggleGlobalSystem", ToggleGlobalSystem},
			{"FkSystemSceneName", SystemSceneName}, {"FkSystemEnabled", SystemEnabled},
			{"FkTouch", TouchValue}, {"FkFieldRead", FieldRead}, {"FkFieldWrite", FieldWrite},
			{"FkTakeReloadState", TakeReloadState}, {"FkContact", ContactValue}, {"FkTextByte", TextByte}, {"FkLogText", LogText},
			{"FkHasComponent", ComponentCall<&NativeBindings::Entity_HasComponent>},
			{"FkAddComponent", ComponentCall<&NativeBindings::Entity_AddComponent>},
			{"FkRemoveComponent", ComponentCall<&NativeBindings::Entity_RemoveComponent>},
			{"FkSpriteRenderer_GetPixelsPerUnit", SpriteValue<SpriteField::PixelsPerUnit, false>},
			{"FkSpriteRenderer_SetPixelsPerUnit", SpriteValue<SpriteField::PixelsPerUnit, true>},
			{"FkSpriteRenderer_GetFlipX", SpriteValue<SpriteField::FlipX, false>},
			{"FkSpriteRenderer_SetFlipX", SpriteValue<SpriteField::FlipX, true>},
			{"FkSpriteRenderer_GetFlipY", SpriteValue<SpriteField::FlipY, false>},
			{"FkSpriteRenderer_SetFlipY", SpriteValue<SpriteField::FlipY, true>},
			{"FkSaveFile", SaveFile},
            {"FkAndroidSimulate", [](uint64_t, uint64_t length) -> uint64_t {
                if (!s_Frame || length > s_Frame->Text.size()) return 0;
                ScriptEngine::DispatchAndroidMessage(s_Frame->Text.substr(0, length), s_Frame->Text.substr(length));
                return 1;
            }},
            {"FkLogLocation", [](uint64_t, uint64_t) -> uint64_t {
                if (s_Frame) s_Frame->LogFile = s_Frame->Text;
                return 0;
            }},
            {"FkOwnSceneLoad", [](uint64_t id, uint64_t keep) -> uint64_t {
                return s_Frame ? SceneLoads::Track(&s_Frame->Path, id, keep) : 0;
            }},
            {"FkWatchSceneLoads", [](uint64_t watch, uint64_t) -> uint64_t {
                if (!s_Frame) return 0;
                if (s_Frame->EntityProgram) s_Frame->EntityProgram->SceneLoads = watch != 0;
                if (s_CurrentSystem) s_CurrentSystem->Module->SceneLoads = watch != 0;
                return 0;
            }},
            {"FkEventWord", [](uint64_t index, uint64_t) -> uint64_t {
                return s_Frame ? (index == 1 ? s_Frame->Notification.Extra : s_Frame->Notification.Argument) : 0;
            }},
            {"FkEventText", [](uint64_t index, uint64_t) -> uint64_t {
                if (!s_Frame) return 0;
                s_Frame->Text = index == 1 ? s_Frame->Notification.Channel : s_Frame->Notification.Text;
                return s_Frame->Text.size();
            }},
            {"FkUiScene", [](uint64_t subscribe, uint64_t) -> uint64_t {
                if (!s_Frame) return 0;
                if (subscribe) {
                    if (s_Frame->EntityProgram) s_Frame->EntityProgram->UiEvents = true;
                    if (s_CurrentSystem) s_CurrentSystem->Module->UiEvents = true;
                }
                return reinterpret_cast<uintptr_t>(ScriptEngine::GetScene());
            }},
            {"FkApplicationDataDirectory", ApplicationDataDirectory},
            {"FkEngine_GraphicsApi", [](uint64_t, uint64_t) -> uint64_t { return static_cast<uint64_t>(Engine::GetGraphicsApi()); }},
            {"FkEngine_GpuVendor", [](uint64_t, uint64_t) -> uint64_t { return static_cast<uint64_t>(Engine::GetGpuVendor()); }},
            {"FkEngine_CpuVendor", [](uint64_t, uint64_t) -> uint64_t { return static_cast<uint64_t>(Engine::GetCpuVendor()); }},
            {"FkEngine_CpuVendorName", [](uint64_t, uint64_t) -> uint64_t {
                if (!s_Frame) return 0;
                s_Frame->Text = Engine::GetCpuVendorName();
                return s_Frame->Text.size();
            }},
			{"FkReadExtra", ReadExtra}, {"FkWriteExtra", WriteExtra},
			{"FkCreateUnnamedEntity", CreateUnnamedEntity},
			{"FkAttachment", Attachment}, {"FkEvent", Callback}, {"FkEntityId", PersistentEntityId},
#define FUNKE_BOLT_SLOT(name) {"Fk" #name, SlotBinding<&NativeBindings::name>},
#include "Scripting/Bolt/NativeSlots.inc"
#include "Scripting/Bolt/ComponentSlots.inc"
#undef FUNKE_BOLT_SLOT
#define FUNKE_BOLT_PACKED(access, name, read, write, count, index, width, kind) \
			{"Fk" #access #name, PackedField<FieldAccess::access, &NativeBindings::read, write, count, index, width, kind>},
#include "Scripting/Bolt/PackedFields.inc"
#undef FUNKE_BOLT_PACKED
		};

		struct ScriptLibrary { std::string Core, UI; std::vector<std::string> Additional; };

		const ScriptLibrary& GetScriptLibrary() {
			static const ScriptLibrary sources = [] {
				std::string declarations = "extern \"C\" ulong FkNativeInvoke(ulong slot, void* arguments, void* unused);\n";
				for (const auto& binding : s_Bindings) {
					if (std::string_view(binding.name).starts_with("Fk"))
						declarations += "extern \"C\" ulong " + std::string(binding.name) + "(ulong a, ulong b);\n";
				}
				ScriptLibrary library{
					std::string(EmbeddedSources::CorePreamble) +
						"extern \"C\" void* FkNativeStructAddress(ulong entity, ulong readOnly);\n"
						"extern \"C\" void* FkJobRows(ulong handle,ulong unused);\n" + declarations + EmbeddedSources::Core,
					EmbeddedSources::UIPreamble + declarations + EmbeddedSources::UI, {}
				};
                for (auto* module = EmbeddedSources::ExtraModules; module->Filename; ++module)
                    library.Additional.emplace_back(module->Preamble + declarations + module->Body);
                return library;
			}();
			return sources;
		}

		void CancelProgramCoroutines(Program& program);
        bool CaptureProgramReloadState(Program& program, std::unordered_map<uint64_t, std::string>& captured, std::string& error);

		bool Compile(CompilerAPI& api, BoltCompiler*& compiler, const std::string& path, bool& entityScript, std::vector<Field>& fields, std::string& error, const std::vector<ComponentUnit>* projectUnits = nullptr, Program* replacing=nullptr,bool registerLayouts=false, AotExport* aot=nullptr, const CompilerMode& mode=CurrentCompilerMode()) {
			char diagnostic[4096]{};
			const int backend = mode.Profile;
            if (!compiler) {
                int status;
                if (mode.Llvm.empty()) {
                    status = api.Create(BOLT_COMPILER_HOST_ABI, backend, s_Bindings, std::size(s_Bindings),
                        &compiler, diagnostic, sizeof(diagnostic));
                } else {
                    if (!api.CreateWithLlvm) {
                        error = "The Bolt compiler host lacks desktop Release LLVM path support: " + Path::ToUtf8(api.LibraryPath);
                        return false;
                    }
                    status = api.CreateWithLlvm(BOLT_COMPILER_HOST_ABI, backend, s_Bindings, std::size(s_Bindings),
                        Path::ToUtf8(mode.Llvm).c_str(), &compiler, diagnostic, sizeof(diagnostic));
                }
                if (status != BOLT_HOST_OK) { error = diagnostic; return false; }
                if (!api.LoggedProfile) {
                    FNK_INFO_TAG("Bolt", "Compiler profile: {}{}", backend == BOLT_HOST_RELEASE ? "LLVM Release" : "Fast-JIT",
                        mode.Llvm.empty() ? std::string{} : " using " + Path::ToUtf8(mode.Llvm));
                    api.LoggedProfile = true;
                }
            }
			if(api.BindSubmit && (api.BindSubmit(compiler,"FkSubmitJob",SubmitJob,diagnostic,sizeof(diagnostic))!=BOLT_HOST_OK
                || api.BindSubmit(compiler,"FkRunJobRanges",RunJobRanges,diagnostic,sizeof(diagnostic))!=BOLT_HOST_OK
                || api.BindSubmit(compiler,"FkNativeInvoke",Services::Call,diagnostic,sizeof(diagnostic))!=BOLT_HOST_OK
                || api.BindSubmit(compiler,"FkPolygonPoints",PolygonPoints,diagnostic,sizeof(diagnostic))!=BOLT_HOST_OK
                || api.BindSubmit(compiler,"FkConsoleIo",ConsoleIo,diagnostic,sizeof(diagnostic))!=BOLT_HOST_OK
                || api.BindSubmit(compiler,"FkConsoleExecute",ConsoleExecute,diagnostic,sizeof(diagnostic))!=BOLT_HOST_OK
                || api.BindSubmit(compiler,"FkLocale",Locales::Call,diagnostic,sizeof(diagnostic))!=BOLT_HOST_OK
                || api.BindSubmit(compiler,"FkWriteImage",WriteImage,diagnostic,sizeof(diagnostic))!=BOLT_HOST_OK
                || api.BindSubmit(compiler,"FkAssetOperation",AssetLoadCall,diagnostic,sizeof(diagnostic))!=BOLT_HOST_OK
                || api.BindSubmit(compiler,"FkRuntimeTexture",RuntimeTextureCall,diagnostic,sizeof(diagnostic))!=BOLT_HOST_OK
                || api.BindSubmit(compiler,"FkEcb",Commands::Call,diagnostic,sizeof(diagnostic))!=BOLT_HOST_OK
                || api.BindSubmit(compiler,"FkRememberUpdateSlot",RememberUpdateSlot,diagnostic,sizeof(diagnostic))!=BOLT_HOST_OK)) {
                error=diagnostic;return false;
            }
            const uint64_t jobImage = aot ? aot->Guid : s_NextJobImage.fetch_add(1);
            if (api.BindPointer && (api.BindPointer(compiler,"FkNativeStructAddress",NativeStructAddress,diagnostic,sizeof(diagnostic))!=BOLT_HOST_OK
                || api.BindPointer(compiler,"FkJobRows",JobRows,diagnostic,sizeof(diagnostic))!=BOLT_HOST_OK
                || api.BindPointer(compiler,"FkCurrentUpdateSlot",CurrentUpdateSlot,diagnostic,sizeof(diagnostic))!=BOLT_HOST_OK)) {
                error=diagnostic; return false;
            }
            std::string source = File::ReadAllText(path);
            BindNativeQueryKeys(source, false);
            const bool inheritsEmptyUpdate=source.find("OnUpdate")==source.npos;
            std::string consoleSetup;
            std::vector<std::pair<std::string, std::string>> resetCalls;
            try { BindPlayResetMethods(source, path, resetCalls); consoleSetup = BindConsoleAttributes(source, EmbeddedSources::Core); source=BindJobQueries(std::move(source)); }
            catch (const std::exception& ex) {error=path+": "+ex.what(); return false;}
            size_t body = 0;
			ScriptBase kind = ScriptBase::Procedural;
			bool hotReloadState = false;
            const auto name = ScriptClass(source, kind, &body, &hotReloadState);
			auto components = projectUnits ? *projectUnits : ComponentUnits();
			FieldList schema{name};
			AddReferences(schema, components);
            if (kind == ScriptBase::DataAsset) AddDataEnums(schema, components, source);
			BoltCompiledModule* next = nullptr;
			int status;
			const bool standaloneCommands = name.empty() && (!consoleSetup.empty() || !resetCalls.empty()) && source.find("int Main(") == source.npos;
            NativeCollectionImports nativeImports;
            nativeImports.Inspect(source);
			if (name.empty() && !ImportsFunke(source) && !nativeImports.Collections && consoleSetup.empty() && resetCalls.empty()) {
				if (aot) return aot->Write({{path.c_str(), source.data(), source.size()}}, "Program", schema.Fields, error);
				status = api.Compile(compiler, path.c_str(), source.data(), source.size(), &next, diagnostic, sizeof(diagnostic));
			} else {
				if (!api.Version || !api.CompileSources || !api.BindSubmit || !api.BindPointer || !api.VisitLayout || !api.RunPersistent || !api.VisitFields) {
					error = "Funke scripts require the Bolt desktop SDK with Funke host extensions. Loaded " + Path::ToUtf8(api.LibraryPath);
					return false;
				}
				const BoltSource asset{path.c_str(), source.data(), source.size()};
				if (!name.empty() && api.VisitFields(&asset, backend, CollectField, &schema, diagnostic, sizeof(diagnostic)) != BOLT_HOST_OK) {
					error = diagnostic;
					return false;
				}
				if (kind == ScriptBase::Component || kind == ScriptBase::DataAsset) {
					const auto found = std::find_if(components.begin(),components.end(),[&](const auto& unit){return unit.Name == name;});
					const auto guid = found != components.end() ? found->Guid : AssetRegistry::GetGUID(path);
					ComponentUnit self{guid, path, name, source, body, false, {}, kind};
					if (!BindComponent(self, schema, error)) return false;
					source = std::move(self.Source);
				} else if(kind!=ScriptBase::Job && kind!=ScriptBase::NativeStruct && kind!=ScriptBase::EnumLibrary && kind!=ScriptBase::EditorWindow) {
					if (body && source.find("async") != source.npos) {
						try { source = CoroutineLowering(source).Lower(body, name); }
						catch (const std::exception& ex) { error = path + ": " + ex.what(); return false; }
					}
					std::string accessors = FieldAccessors(schema.Fields);
					if (kind == ScriptBase::Scene || kind == ScriptBase::Global) {
						const auto match = std::find_if(components.begin(), components.end(), [&](const auto& unit) {
							return unit.Kind == kind && !unit.Managed && unit.Name == name;
						});
						if (match != components.end()) accessors += " public override string FkSystemName() {return \"" + std::to_string(match->Guid) + "\";}";
					}
					source.insert(body, accessors);
				}
				const auto& library = GetScriptLibrary();
				std::string entry = name.empty() ? std::string{} : "import Funke\nimport System.Collections.Generic\n" + (kind == ScriptBase::Entity ? EntityEntryPoint(name, hotReloadState) : kind == ScriptBase::EditorWindow ? EditorWindowEntryPoint(name) : kind == ScriptBase::DataAsset ? "public static class FunkeScriptEntry { public: int Main() {" + name + " asset; asset.FkSetGuid(Funke.FkDataStorage.Current()); if (Funke.FunkeApiTypes.EventId() == 22ul) asset.FkDescribeEnums(); else asset.OnValidate(); return 0;} }" : (kind == ScriptBase::Component || kind == ScriptBase::NativeStruct || kind==ScriptBase::Job || kind==ScriptBase::EnumLibrary) ? "public static class FunkeScriptEntry { public: int Main() { return 0; } }" : SystemEntryPoint(name, kind));

				if (standaloneCommands)
                    entry = consoleSetup.empty()
                        ? "import Funke\npublic static class FunkeScriptEntry { public: int Main() { return 0; } }"
                        : "import Funke\npublic static class FunkeScriptEntry { public: int Main() { Funke.DevConsole.FkDispatch(); return 0; } }";
                if (!consoleSetup.empty()) {
                    auto& programSource = entry.empty() ? source : entry;
                    const auto main = programSource.find("int Main()");
                    if (main != std::string::npos) programSource.insert(programSource.find('{', main) + 1, consoleSetup + "if(Funke.FunkeApiTypes.EventId()==62ul){Funke.DevConsole.FkDispatch();return 0;}");
                }

                std::string jobs(EmbeddedSources::Jobs);
                if (api.RunBatch) jobs.insert(0, "#define FunkeBoltRepeatCallbacks\n");
                constexpr std::string_view marker="/*module*/0ul";
                jobs.replace(jobs.find(marker),marker.size(),std::to_string(jobImage)+"ul");
                std::vector<BoltSource> units = {
					{"<Funke>/Jobs.bolt", jobs.data(), jobs.size()},
                    {"<Funke>/Components.bolt", k_ComponentNamespace.data(), k_ComponentNamespace.size()},
                    {"<Funke>/Coroutines.bolt", k_CoroutineNamespace.data(), k_CoroutineNamespace.size()},
					{path.c_str(), source.data(), source.size()},
					{"<Funke>/ScriptCore.bolt", library.Core.data(), library.Core.size()},
					{"<Funke>/UI.bolt", library.UI.data(), library.UI.size()},
					{"<Funke>/Entry.bolt", entry.data(), entry.size()}
				};
				if (entry.empty()) units.pop_back();
                for (size_t i = 0; i < library.Additional.size(); ++i)
                    units.push_back({EmbeddedSources::ExtraModules[i].RuntimePath, library.Additional[i].data(), library.Additional[i].size()});
				for (auto& unit : components) {
					if (unit.Name == name && unit.Kind == kind && !unit.Managed) continue;
					BindNativeQueryKeys(unit.Source, unit.Managed);
					if(unit.Source.find("IJobQuery")!=unit.Source.npos) {
                        try {unit.Source=BindJobQueries(std::move(unit.Source));}
                        catch(const std::exception& ex){error=unit.Path+": "+ex.what(); return false;}
                    }
                    FieldList componentFields{unit.Name};
					AddReferences(componentFields, components);
					if (unit.Kind == ScriptBase::DataAsset) AddDataEnums(componentFields, components, unit.Source);
					if (!BindComponent(unit, componentFields, error)) return false;
                    try { if (!unit.Managed) BindPlayResetMethods(unit.Source, unit.Path, resetCalls); }
                    catch (const std::exception& ex) { error = unit.Path + ": " + ex.what(); return false; }
                    units.push_back({unit.Path.c_str(), unit.Source.data(), unit.Source.size()});
                    nativeImports.Inspect(unit.Source);
				}
                // The SDK eagerly specializes native container implementations even for scripts that never reference them.
                std::erase_if(units, [&](const BoltSource& unit) {
                    const std::string_view path = unit.name;
                    return (path == "<Funke>/NativeCollections.bolt" && !nativeImports.Collections) ||
                        (path == "<Funke>/Interop.bolt" && !nativeImports.Interop);
                });
                if (!resetCalls.empty()) {
                    std::sort(resetCalls.begin(), resetCalls.end());
                    std::string invoke = "if(Funke.FunkeApiTypes.EventId()==64ul){";
                    for (const auto& [method, call] : resetCalls) invoke += call;
                    invoke += "return 0;}";
                    auto& mainSource = entry.empty() ? source : entry;
                    const auto main = mainSource.find("int Main()");
                    if (main == std::string::npos) { error = path + ": cannot locate the play reset entry point"; return false; }
                    mainSource.insert(mainSource.find('{', main)+1, invoke);
                    if (entry.empty()) units[3] = {path.c_str(), source.data(), source.size()};
                    else units[6] = {"<Funke>/Entry.bolt", entry.data(), entry.size()};
                }
				if (aot) {
                    if (!aot->Write(units, name.empty() && !standaloneCommands ? "Program" : "FunkeScriptEntry", schema.Fields, error))
                        return false;
                    auto hasStruct = [](std::string_view contents) {
                        const auto declarations = ScriptDeclarations(contents);
                        return std::any_of(declarations.begin(), declarations.end(), [](const auto& decl) {
                            return decl.Kind == ScriptBase::NativeStruct;
                        });
                    };
                    if (!hasStruct(source) && std::none_of(components.begin(), components.end(),
                        [&](const auto& unit) { return hasStruct(unit.Source); })) return true;
                }
                status = api.CompileSources(compiler, units.data(), units.size(), name.empty() && !standaloneCommands ? nullptr : "FunkeScriptEntry", &next, diagnostic, sizeof(diagnostic));
			}
			if (status == BOLT_HOST_OK) {
				std::vector<NativeStructLayout> layouts;
                std::set<std::string> visited;
                auto collect=[&](const std::string& unitPath,const std::string& contents) {
                    if(contents.find("IComponent")==contents.npos)return true;
                    for(const auto& decl:ScriptDeclarations(contents)) {
                        if(decl.Kind!=ScriptBase::NativeStruct || !visited.insert(decl.Name).second)continue;
                        ComponentUnit unit{}; unit.Path=unitPath; unit.Name=decl.Name;
                        if(!ReadNativeStruct(unit,api.VisitLayout,next,layouts,error))return false;
                    }
                    return true;
                };
                for(const auto& unit:components) {
                    const bool ok=unit.Managed ? unit.Kind!=ScriptBase::NativeStruct || ReadNativeStruct(unit,api.VisitLayout,next,layouts,error,aot != nullptr)
                        : collect(unit.Path,unit.Source);
                    if(!ok){api.Release(next);return false;}
                }
                if(!collect(path,source)){api.Release(next);return false;}
                if (aot) {
                    api.Release(next);
                    auto catalog = Json::Value::MakeArray();
                    for (const auto& layout : layouts) {
                        auto row = Json::Value::MakeObject();
                        row.AddMember("name", layout.Name);
                        row.AddMember("size", static_cast<uint64_t>(layout.Size));
                        row.AddMember("alignment", static_cast<uint64_t>(layout.Alignment));
                        row.AddMember("fields", layout.Fields);
                        row.AddMember("managed", std::any_of(components.begin(), components.end(),
                            [&](const auto& unit) { return unit.Managed && unit.Name == layout.Name; }));
                        catalog.Append(std::move(row));
                    }
                    if (File::WriteAllText(Path::ToUtf8(aot->Directory / "layouts.json"), Json::Stringify(catalog))) return true;
                    error = "Cannot stage Bolt component layouts for " + path;
                    return false;
                }
                std::unordered_map<uint64_t, std::string> captured;
                if (replacing && replacing->Active && replacing->HotReloadState && hotReloadState &&
                    !CaptureProgramReloadState(*replacing, captured, error)) {
                    api.Release(next);
                    return false;
                }
                if((replacing || registerLayouts) && !InstallNativeStructs(layouts,error)){api.Release(next);return false;}
                if (replacing && replacing->Active) CancelProgramCoroutines(*replacing);
				status = api.Load(compiler, next, diagnostic, sizeof(diagnostic));
				if(status==BOLT_HOST_OK && replacing) {
					if (replacing->Active) {
                        ReleaseConsoleCommands(replacing);
                        SceneLoads::ReleaseGeneration(&replacing->Path);
                        AssetLoads::ReleaseGeneration(&replacing->Path);
                        RuntimeTextures::ReleaseGeneration(&replacing->Path);
                    }
                    replacing->HasPlayReset = !resetCalls.empty();
                    replacing->HotReloadState = hotReloadState && kind == ScriptBase::Entity;
                    replacing->ReloadStates = std::move(captured);
                    replacing->InheritsEmptyUpdate=kind==ScriptBase::Entity && inheritsEmptyUpdate;
                    std::fill(replacing->UpdateSlots.begin(), replacing->UpdateSlots.end(), nullptr);
                    std::fill(replacing->ReadySlots.begin(),replacing->ReadySlots.end(),0);
                    std::fill(replacing->WatchedSlots.begin(),replacing->WatchedSlots.end(),0);
                    auto owner=std::make_shared<JobImage>();
                    owner->Id=jobImage;owner->Release=api.Release;owner->Image=next;
                    owner->Library=Platform::LoadSharedLibrary(api.LibraryPath);
                    {std::lock_guard lock(s_JobImagesMutex);s_JobImages.emplace(jobImage,owner);}
                    replacing->Jobs=std::move(owner);
                } else api.Release(next);
			}
			if (status != BOLT_HOST_OK) {
				error = diagnostic;
				return false;
			}
			entityScript = kind == ScriptBase::Entity;
			fields = std::move(schema.Fields);
			return true;
		}

#ifdef FUNKE_BOLT_AOT
        struct AotModule {
            std::string_view Identity;
            uint64_t Guid;
            int (*Run)();
            std::vector<Field> Fields;
        };
#include "FunkeBoltModules.inc"
#endif

#ifdef FNK_PLATFORM_WINDOWS
#include "Scripting/Bolt/WindowsAot.inl"
#endif

        int RunProgram(Program& program, bool persistent, int32_t& code, char* diagnostic, size_t capacity) {
#ifdef FUNKE_BOLT_AOT
            diagnostic[0] = 0;
            code = program.AotRun();
            return BOLT_HOST_OK;
#else
#ifdef FNK_PLATFORM_WINDOWS
            if (program.Jobs && program.Jobs->RunAot) {
                if (!program.Jobs->RunAot(&program.Jobs->Session, persistent, &code)) {
                    snprintf(diagnostic, capacity, "Bolt AOT execution failed in %s", program.Path.c_str());
                    return BOLT_HOST_ERROR;
                }
                return BOLT_HOST_OK;
            }
#endif
            const auto run = persistent ? s_API.RunPersistent : s_API.Run;
            return run(program.Compiler, &code, diagnostic, capacity);
#endif
        }

        bool ResetModulePlayState(const std::string& identity, Program& program) {
            static_assert(static_cast<unsigned>(Event::ResetPlayState) == 64);
            if (!s_PlayResetActive || !program.HasPlayReset || program.ResetEpoch == s_PlayResetEpoch) return true;
            Frame frame{0, false, 0, Event::ResetPlayState, 0, {}, {}, program.Path, identity, program.Fields};
            auto* previous = std::exchange(s_Frame, &frame);
            frame.EntityProgram = &program;
            char diagnostic[2048]{};
            int32_t code = 0;
            const int status = RunProgram(program, true, code, diagnostic, sizeof(diagnostic));
            s_Frame = previous;
            if (status != BOLT_HOST_OK || code) {
                program.Error = "ResetOnPlay failed for " + identity + ": " + diagnostic;
                FNK_ERROR_TAG("Bolt", "{}", program.Error);
                return false;
            }
            program.ResetEpoch = s_PlayResetEpoch;
            return true;
        }

        bool CaptureProgramReloadState(Program& program, std::unordered_map<uint64_t, std::string>& captured, std::string& error) {
            static_assert(static_cast<unsigned>(Event::CaptureReloadState) == 63);
            // A disabled attachment may still be waiting for its first update after a previous reload.
            captured = program.ReloadStates;
            const auto attachments = program.Attachments;
            for (const auto& [owner, attachment] : attachments) {
                if (captured.contains(attachment) || attachment >= program.UpdateSlots.size() || !program.UpdateSlots[attachment])
                    continue;
                auto* scene = const_cast<Scene*>(owner.first);
                const auto entity = static_cast<EntityHandle>(owner.second);
                if (!scene || !scene->IsValid(entity)) continue;
                const auto token = program.DispatchTokens[attachment];
                if (token >= s_DispatchSlots.size() || !s_DispatchSlots[token].Identity) continue;
                const auto& identity = *s_DispatchSlots[token].Identity;
                Frame frame{owner.second, true, 0, Event::CaptureReloadState, attachment, {}, {}, program.Path, identity, program.Fields};
                frame.EntityProgram = &program;
                frame.UpdateSlot = program.UpdateSlots[attachment];
                SceneExecutionScope scope(*scene);
                auto* previousScene = ScriptEngine::GetScene();
                auto* previousFrame = s_Frame;
                ScriptEngine::SetScene(scene);
                s_Frame = &frame;
                char diagnostic[2048]{};
                int32_t code = 0;
                const int status = RunProgram(program, true, code, diagnostic, sizeof(diagnostic));
                s_Frame = previousFrame;
                ScriptEngine::SetScene(previousScene);
                if (status != BOLT_HOST_OK || code != 0) {
                    error = "Could not capture reload state for " + identity + ": " + diagnostic;
                    return false;
                }
                captured.emplace(attachment, std::move(frame.Text));
            }
            return true;
        }

		#include "Scripting/Bolt/ConsoleCommands.inl"

		bool Refresh(const std::string& identity, Program& program, bool force, ScriptBase expected = ScriptBase::Procedural) {
			const auto path = ResolveAsset(identity);
			std::error_code ec;
			const auto modified = std::filesystem::last_write_time(Path::FromUtf8(path), ec);
			if (!force && path == program.Path && modified == program.Modified) return false;
			program.Path = path;
			program.Modified = modified;
			if (path.empty()) {
                SceneLoads::ReleaseGeneration(&program.Path);
                AssetLoads::ReleaseGeneration(&program.Path);
                RuntimeTextures::ReleaseGeneration(&program.Path);
                ReleaseConsoleCommands(&program);
				program.Error = "Missing Bolt asset " + identity;
				FNK_ERROR_TAG("Bolt", "{}", program.Error);
				program.Active = false;
				return true;
			}
			ScriptBase kind = ScriptBase::Procedural;
			const auto className = ScriptClass(File::ReadAllText(path), kind);
			std::string error;
			if (expected != ScriptBase::Procedural && kind != expected) error = "registered Bolt system changed its base class";
#ifdef FUNKE_BOLT_AOT
            program.EntityScript = kind == ScriptBase::Entity;
            if (const auto* module = FindAotModule(identity)) {
                program.AotRun = module->Run;
                program.Fields = module->Fields;
                auto image = std::make_shared<JobImage>();
                image->Id = module->Guid;
                { std::lock_guard lock(s_JobImagesMutex); s_JobImages[image->Id] = image; }
                program.Jobs = std::move(image);
            } else error = "Missing linked Bolt AOT module";
            if (!error.empty()) {
#else
            bool loaded = false;
            if (error.empty()) {
#ifdef FNK_PLATFORM_WINDOWS
                if (IsWindowsAotPlayer()) {
                    program.EntityScript = kind == ScriptBase::Entity;
                    loaded = LoadWindowsModule(identity, program, error);
                } else
#endif
                loaded = s_API.Open(error) && Compile(s_API, program.Compiler, path, program.EntityScript,
                    program.Fields, error, nullptr, &program);
            }
            if (!loaded) {
#endif
				program.Error = error.starts_with(path) ? error : path + ": " + error;
				FNK_ERROR_TAG("Bolt", "{}", program.Error);
				return true;
			}
			program.ComponentOnly = kind != ScriptBase::Entity && kind != ScriptBase::Procedural;
            program.ScriptClassName = className;
			program.Error.clear();
			program.Active = ResetModulePlayState(identity, program);
			program.ReportedRunError = false;
			return true;
		}
	}

#ifdef FUNKE_BOLT_AOT
    // The target linker resolves the C imports emitted by Bolt.
#include "FunkeBoltBindings.inc"
    extern "C" void* FkCurrentUpdateSlot(uint64_t a, uint64_t b) { return CurrentUpdateSlot(a, b); }
    extern "C" void* FkJobRows(uint64_t a, uint64_t b) { return JobRows(a, b); }
    extern "C" void* FkNativeStructAddress(uint64_t a, uint64_t b) { return NativeStructAddress(a, b); }
    extern "C" uint64_t FkRunJobRanges(uint64_t n, void* e, void* c) { return RunJobRanges(n,e,c); }
    extern "C" uint64_t FkSubmitJob(uint64_t a, void* b, void* c) { return SubmitJob(a, b, c); }
    extern "C" uint64_t FkRememberUpdateSlot(uint64_t a, void* b, void* c) { return RememberUpdateSlot(a, b, c); }
    extern "C" uint64_t FkNativeInvoke(uint64_t a, void* b, void* c) { return Services::Call(a,b,c); }
    extern "C" uint64_t FkPolygonPoints(uint64_t a, void* b, void* c) { return PolygonPoints(a, b, c); }
    extern "C" uint64_t FkConsoleIo(uint64_t a, void* b, void* c) { return ConsoleIo(a, b, c); }
    extern "C" uint64_t FkConsoleExecute(uint64_t a, void* b, void* c) { return ConsoleExecute(a, b, c); }
    extern "C" uint64_t FkLocale(uint64_t a, void* b, void* c) { return Locales::Call(a, b, c); }
    extern "C" uint64_t FkWriteImage(uint64_t a, void* b, void* c) { return WriteImage(a, b, c); }
    extern "C" uint64_t FkAssetOperation(uint64_t a, void* b, void* c) { return AssetLoadCall(a, b, c); }
    extern "C" uint64_t FkRuntimeTexture(uint64_t a, void* b, void* c) { return RuntimeTextureCall(a, b, c); }
    extern "C" uint64_t FkEcb(uint64_t a, void* b, void* c) { return Commands::Call(a, b, c); }
#endif

	std::string GetSavedLanguage() {
        try {
            const auto directory = GameDataDirectory(false);
            if (directory.empty()) return {};
            const auto path = Path::ToUtf8(directory / "prefs.json");
            constexpr char key[] = "Funke.Language";
            uint64_t unused = 0;
            const int bytes = ScriptPreferences::Access(path.c_str(), ScriptPreferences::GetString, key, sizeof(key)-1, &unused, nullptr, 0);
            if (bytes <= 0) return {};
            std::string language(static_cast<size_t>(bytes), '\0');
            if (ScriptPreferences::Access(path.c_str(), ScriptPreferences::GetString, key, sizeof(key)-1, &unused, language.data(), bytes) != bytes) return {};
            return language;
        } catch (...) { return {}; }
    }
    namespace {
        struct CommandAsset {
            std::string Path;
            std::filesystem::file_time_type Stamp{};
            std::vector<DevCommandInfo> Commands;
        };
        std::map<uint64_t, CommandAsset> s_CommandAssets;
        uint64_t s_CommandCatalogVersion = UINT64_MAX;
        std::chrono::steady_clock::time_point s_NextCommandScan;
        void RefreshCommandCatalog() {
            const auto now = std::chrono::steady_clock::now();
            const auto version = AssetRegistry::GetChangeVersion();
            if (version == s_CommandCatalogVersion && now < s_NextCommandScan) return;
            s_CommandCatalogVersion = version;
            s_NextCommandScan = now + std::chrono::milliseconds(250);
            std::unordered_set<uint64_t> present;
            for (const auto& asset : AssetRegistry::GetAssetsByKind(AssetKind::Script)) {
                auto path = Path::FromUtf8(asset.Path);
                if (!ScriptTypeEqualsIgnoreCase(path.extension().string(), ".bolt")) continue;
                present.insert(asset.Id);
                std::error_code ec;
                auto stamp = std::filesystem::last_write_time(path, ec);
                if (ec) continue;
                auto& entry = s_CommandAssets[asset.Id];
                if (entry.Path == asset.Path && entry.Stamp == stamp) continue;
                entry = {asset.Path, stamp};
                auto source = File::ReadAllText(asset.Path);
                if (source.find("DevCommand") == source.npos) continue;
                try {
                    std::vector<ConsoleDeclaration> declarations;
                    BindConsoleAttributes(source, {}, &declarations);
                    for (const auto& declaration : declarations) {
                        Json::Value key, description;
                        if (!Json::TryParse(declaration.NameLiteral, key) || !key.IsString()) continue;
                        auto name = key.AsStringOr();
                        name.erase(0, name.find_first_not_of(" \t\r\n"));
                        name.erase(name.find_last_not_of(" \t\r\n") + 1);
                        if (name.empty()) continue;
                        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        Json::TryParse(declaration.DescriptionLiteral, description);
                        entry.Commands.push_back({std::move(name), declaration.Usage, description.AsStringOr(), true});
                    }
                } catch (const std::exception& error) { FNK_WARN_TAG("Bolt", "{}: {}", asset.Path, error.what()); }
            }
            std::erase_if(s_CommandAssets, [&](const auto& item) { return !present.contains(item.first); });
        }
    }

    DevCommandResult ExecuteDevCommand(const std::string& name, const std::vector<std::string>& args) {
        if (!s_ConsoleCommands.contains(name) && !s_ConsoleAttributes.contains(name)) {
            RefreshCommandCatalog();
            for (const auto& [guid, asset] : s_CommandAssets) {
                if (std::none_of(asset.Commands.begin(), asset.Commands.end(), [&](const auto& info) { return info.Name == name; })) continue;
                const auto identity = std::to_string(guid);
                auto& program = s_Programs[identity];
                Refresh(identity, program, false);
                if (!program.Active) return DevCommandResult::Error(program.Error);
                // Entering a previously unused image runs its generated attribute registrations
                // before dispatching the first command.
                ConsoleInvocation invocation{name, args};
                auto previousInvocation = std::exchange(s_ConsoleInvocation, &invocation);
                Frame frame{0, true, 0, Event::DevCommand, 0, {}, {}, program.Path, identity, program.Fields};
                frame.EntityProgram = &program;
                auto previousFrame = std::exchange(s_Frame, &frame);
                char diagnostic[2048]{};
                int32_t code = 0;
                const int status = RunProgram(program, true, code, diagnostic, sizeof(diagnostic));
                s_Frame = previousFrame;
                s_ConsoleInvocation = previousInvocation;
                if (status != BOLT_HOST_OK || code != 0) return DevCommandResult::Error(diagnostic);
                return invocation.Result;
            }
        }
        return InvokeConsoleCommand(name, args);
    }
    void AppendDevCommands(std::vector<DevCommandInfo>& commands) {
        auto append = [&](const DevCommandInfo& command) {
            if (std::none_of(commands.begin(), commands.end(), [&](const auto& info) { return info.Name == command.Name; })) commands.push_back(command);
        };
        for (const auto& [name, registration] : s_ConsoleCommands)
            append({name, registration.Usage, registration.Description, true});
        for (const auto& [name, registration] : s_ConsoleAttributes)
            append({name, registration.Usage, registration.Description, true});
        RefreshCommandCatalog();
        for (const auto& [guid, asset] : s_CommandAssets)
            for (const auto& command : asset.Commands) append(command);
    }
    std::string* FrameText() noexcept { return s_Frame ? &s_Frame->Text : nullptr; }


    static void ConfigureBoltLanguageServer(const std::filesystem::path& root) {
        auto compiler=FindCompilerLibrary();
        if(compiler.empty())return;
#if defined(FNK_PLATFORM_WINDOWS)
        const auto executable="bolt.exe";
#else
        const auto executable="bolt";
#endif
        compiler=compiler.parent_path()/executable;
        std::error_code ec;
        if(!std::filesystem::is_regular_file(compiler,ec))return;
        const auto directory=root/".vscode";
        std::filesystem::create_directories(directory,ec);
        if(ec)return;
        const auto path=Path::ToUtf8(directory/"settings.json");
        auto body=File::Exists(path)?File::ReadAllText(path):std::string("{}");
        if(body.find("\"bolt.compilerPath\"")!=body.npos)return;
        std::string_view tokens=body;
        const auto open=NextBoltToken(tokens);
        if(open!="{")return;
        const bool empty=NextBoltToken(tokens)=="}";
        const auto pos=static_cast<size_t>(open.data()-body.data())+1;
        body.insert(pos,"\n    \"bolt.compilerPath\": "+Json::Stringify(Json::Value(Path::ToUtf8(compiler)))+(empty?"\n":","));
        (void)File::WriteAllText(path,body);
    }

    void EnsureEditorProject(const std::filesystem::path& root) {
		const auto directory = root / ".funke" / "Bolt";
		std::error_code ec;
		std::filesystem::create_directories(directory, ec);
		if (ec) {
			FNK_CORE_WARN_TAG("Bolt", "Could not create Bolt tooling directory '{}': {}", Path::ToUtf8(directory), ec.message());
			return;
		}
		const auto writeChanged = [](const std::filesystem::path& path, const std::string& contents) {
			const auto file = Path::ToUtf8(path);
			return (File::Exists(file) && File::ReadAllText(file) == contents) || File::WriteAllText(file, contents);
		};
		if (!writeChanged(directory / "ScriptCore.bolt", EmbeddedSources::EditorCore)
			|| !writeChanged(directory / "UI.bolt", EmbeddedSources::EditorUI)
			|| !writeChanged(directory / "Jobs.bolt", EmbeddedSources::EditorJobs)
			|| !writeChanged(directory / "Components.bolt", EmbeddedSources::EditorComponentNamespace)
			|| !writeChanged(directory / "Coroutines.bolt", EmbeddedSources::EditorCoroutines)) return;
		for (auto* module = EmbeddedSources::ExtraModules; module->Filename; ++module)
			if (!writeChanged(directory / module->Filename, module->Editor)) return;
		ConfigureBoltLanguageServer(root);
        const auto manifest = Path::ToUtf8(root / "bolt.proj");
		if (!File::Exists(manifest)) {
			(void)File::WriteAllText(manifest, R"(format = 1
[project]
name = "FunkeScripts"
kind = "static-library"
[sources]
roots = ["Assets", ".funke/Bolt"]
)");
		}
	}

	std::filesystem::path DesktopLlvmRelativePath() {
#ifdef FNK_PLATFORM_WINDOWS
        return "LLVM-C.dll";
#else
        return std::filesystem::path("llvm/lib") / Platform::SharedLibraryFileName("LLVM");
#endif
    }

    std::filesystem::path FindCompilerLibrary() {
		const auto name = Platform::SharedLibraryFileName("BoltCompiler");
		auto findIn = [&](const std::filesystem::path& root) -> std::filesystem::path {
			for (const auto& dir : {root / "libexec" / "Funke", root, root / "bin", root / "libexec"}) {
				std::error_code ec;
				if (std::filesystem::is_regular_file(dir / name, ec)) return dir / name;
			}
			return {};
		};
		const auto executable = Platform::GetExecutablePath();
		if (!executable.empty()) {
			if (auto bundled = findIn(executable.parent_path()); !bundled.empty()) return bundled;
		}
		if (const char* root = std::getenv("BOLT_SDK_ROOT"); root && *root) return findIn(Path::FromUtf8(root));
#if defined(FNK_PLATFORM_WINDOWS)
		if (const char* local = std::getenv("LOCALAPPDATA")) return findIn(Path::FromUtf8(local) / "Programs" / "Bolt");
#else
		if (const char* home = std::getenv("HOME")) return findIn(Path::FromUtf8(home) / ".local/share/bolt");
#endif
		return {};
	}

	std::string ResolveAsset(const std::string& identity) {
		uint64_t id = 0;
		const auto end = identity.data() + identity.size();
		const auto parsed = std::from_chars(identity.data(), end, id);
		if (!id || parsed.ec != std::errc{} || parsed.ptr != end) return {};
		const std::string path = AssetRegistry::ResolvePath(id);
		return ScriptTypeEqualsIgnoreCase(Path::FromUtf8(path).extension().string(), ".bolt") ? path : std::string{};
	}

	static bool ValidateUnits(const std::vector<std::string>& paths, std::string& error, bool standaloneSet, std::vector<ComponentUnit> components, const std::filesystem::path& aotDirectory, const CompilerMode& mode=CurrentCompilerMode()) {
		if (standaloneSet) {
			for (const auto& path : paths) {
				auto source = File::ReadAllText(path);
				auto kind = ScriptBase::Procedural;
				size_t body = 0;
				auto name = ScriptClass(source,kind,&body);
				const bool library = kind == ScriptBase::Component || kind == ScriptBase::NativeStruct || kind == ScriptBase::Job || kind == ScriptBase::DataAsset || kind == ScriptBase::EnumLibrary;
				if (!library && kind != ScriptBase::Scene && kind != ScriptBase::Global) continue;
				Json::Value meta;
				std::string diagnostic;
				if (!Json::TryParse(File::ReadAllText(path+".meta"),meta,&diagnostic)) { error=path+": "+diagnostic; return false; }
				auto id = meta.FindMember("AssetGUID");
				if (!id) id = meta.FindMember("uuid");
				uint64_t guid = id ? id->AsUInt64Or(0) : 0;
				if (id && id->IsString()) {
					const auto raw=id->AsStringOr();
					auto parsed=std::from_chars(raw.data(),raw.data()+raw.size(),guid);
					if (parsed.ec!=std::errc{} || parsed.ptr!=raw.data()+raw.size()) guid=0;
				}
				if (!guid) {error = path + ": missing component asset GUID"; return false;}
				if (library) components.push_back({guid,path,std::move(name),std::move(source),body,false,{},kind});
				else components.push_back(SystemDeclaration(guid,path,name,kind));
			}
		}
        if (!aotDirectory.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(aotDirectory, ec);
            const auto toolPath = FindCompilerLibrary().parent_path() /
#ifdef FNK_PLATFORM_WINDOWS
                "bolt.exe";
#else
                "bolt";
#endif
            if (!File::WriteAllText(Path::ToUtf8(aotDirectory / "compiler.json"),
                                    Json::Stringify(Json::Value(Path::ToUtf8(toolPath))))) {
                error = "Cannot stage Bolt compiler path";
                return false;
            }
            auto bindings = Json::Value::MakeArray();
            for (const auto& binding : s_Bindings) bindings.Append(Json::Value(std::string(binding.name)));
            if (ec || !File::WriteAllText(Path::ToUtf8(aotDirectory / "bindings.json"), Json::Stringify(bindings))) {
                error = "Cannot stage Bolt host bindings";
                return false;
            }
        }
		CompilerAPI api;
		if (!api.Open(error)) return false;
		bool valid = true;
		for (const std::string& path : paths) {
			BoltCompiler* compiler = nullptr;
			bool entityScript = false;
			std::vector<Field> fields;
			AotExport aot;
            if (!aotDirectory.empty() && !aot.Open(aotDirectory, path, error)) { valid = false; break; }
            valid = Compile(api, compiler, path, entityScript, fields, error, standaloneSet ? &components : nullptr,
                nullptr, false, aotDirectory.empty() ? nullptr : &aot, mode);
			if (compiler) api.Destroy(compiler);
			if (!valid) {
				if (!error.starts_with(path)) error = path + ": " + error;
				break;
			}
		}
		Platform::UnloadSharedLibrary(api.Library);
		return valid;
	}

	bool ValidateFiles(const std::vector<std::string>& paths, std::string& error, bool standaloneSet) {
        return ValidateUnits(paths, error, standaloneSet, {}, {});
    }

    bool ValidateFiles(const std::vector<std::string>& paths, std::string& error, bool standaloneSet, const std::filesystem::path& aotDirectory) {
		return ValidateUnits(paths, error, standaloneSet, {}, aotDirectory);
	}

	static bool ValidateMixedUnits(const std::vector<std::string>& paths, const std::filesystem::path& core,
		const std::filesystem::path& assembly, std::string& error, const std::filesystem::path& aotDirectory, const CompilerMode& mode) {
        if (aotDirectory.empty() && ValidateUnits(paths, error, true, {}, {}, mode)) return true;
		using Describe = const char* (*)(const char*);
		auto describe = reinterpret_cast<Describe>(ScriptEngine::GetCoreEntryPoint(core,
			"Funke.Interop.ScriptInstanceManager", "DescribeBoltAssembly"));
		if (!describe) return !aotDirectory.empty() && ValidateUnits(paths, error, true, {}, aotDirectory, mode);
		const auto* json = describe(Path::ToUtf8(assembly).c_str());
		std::vector<ComponentUnit> components;
		if (!json || !ReadManagedComponentSchema(json, components) || components.empty())
            return !aotDirectory.empty() && ValidateUnits(paths, error, true, {}, aotDirectory, mode);
		error.clear();
		return ValidateUnits(paths, error, true, std::move(components), aotDirectory, mode);
	}

    bool ValidateMixedFiles(const std::vector<std::string>& paths, const std::filesystem::path& core,
		const std::filesystem::path& assembly, std::string& error, const std::filesystem::path& aotDirectory) {
        return ValidateMixedUnits(paths, core, assembly, error, aotDirectory, CurrentCompilerMode());
    }

    bool ValidateBuildFiles(const std::vector<std::string>& paths, const std::filesystem::path& core,
        const std::filesystem::path& assembly, const std::filesystem::path& aotDirectory,
        const std::filesystem::path& llvmLibrary, std::string& error) {
        const CompilerMode mode{llvmLibrary.empty() ? BOLT_HOST_DEBUG : BOLT_HOST_RELEASE, llvmLibrary};
        if (assembly.empty()) return ValidateUnits(paths, error, true, {}, aotDirectory, mode);
        return ValidateMixedUnits(paths, core, assembly, error, aotDirectory, mode);
    }

    bool ValidateMixedFiles(const std::vector<std::string>& paths, const std::filesystem::path& core,
        const std::filesystem::path& assembly, std::string& error) {
        return ValidateMixedFiles(paths, core, assembly, error, {});
    }

	void UpdateManagedComponents(const char* schema, ManagedFieldAccess access, const std::filesystem::path& root) {
		std::vector<ComponentUnit> next;
		if (schema && access) ReadManagedComponentSchema(schema, next);
		s_ManagedComponentUnits.swap(next);
		s_ManagedFieldAccess = access;
		s_Schemas.clear();
		if (access && !s_ManagedComponentUnits.empty()) {
			for (auto& [identity, program] : s_Programs)
				if (!program.Active && !program.Error.empty()) Refresh(identity, program, true);
		}
		if (root.empty()) return;
		const auto directory = root / ".funke" / "Bolt";
		std::error_code ec;
		if (!std::filesystem::is_directory(directory, ec)) return;
		std::unordered_set<std::string> expected;
		constexpr std::string_view marker = "// Generated by Funke from the project C# assembly.\n";
		std::vector<ComponentUnit> references;
		if (schema && access) ReadManagedComponentSchema(schema, references, true);
		for (const auto& unit : references) {
			const auto filename = "CSharp." + unit.Name + ".bolt";
			expected.insert(filename);
			const auto path = Path::ToUtf8(directory / filename);
			const auto content = std::string(marker) + unit.Source;
			if (!File::Exists(path) || File::ReadAllText(path) != content) File::WriteAllText(path, content);
		}
		for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
			const auto name = Path::ToUtf8(it->path().filename());
			if (name.starts_with("CSharp.") && name.ends_with(".bolt") && !expected.contains(name)
				&& File::ReadAllText(Path::ToUtf8(it->path())).starts_with(marker))
				std::filesystem::remove(it->path(), ec);
		}
	}

	std::string ComponentIdentity(const std::string& name) {
		for (const auto& unit : ComponentUnits()) if (!unit.Managed && unit.Kind == ScriptBase::Component && unit.Name == name) return std::to_string(unit.Guid);
		return {};
	}

	bool IsSystemAsset(const std::string& identity, bool global) {
		const auto path = ResolveAsset(identity);
		if (path.empty()) return false;
		ScriptBase kind = ScriptBase::Procedural;
		ScriptClass(File::ReadAllText(path), kind);
		return kind == (global ? ScriptBase::Global : ScriptBase::Scene);
	}

	uint32_t CreateSystem(const std::string& identity, Scene* owner, bool global) {
		if (!IsSystemAsset(identity, global) || (!global && !owner)) return 0;
		auto system = std::make_shared<System>();
		system->Owner = owner;
		system->Global = global;
		system->Identity = identity;
		auto& cached = s_SystemPrograms[identity];
		system->Module = cached.lock();
		if (!system->Module) {
			system->Module = std::make_shared<SystemProgram>();
			Refresh(identity, *system->Module, true, global ? ScriptBase::Global : ScriptBase::Scene);
			if (!system->Module->Active) return 0;
			cached = system->Module;
		}
		while (s_Systems.contains(s_NextSystem)) s_NextSystem = 0x80000000u | (s_NextSystem + 1);
		const auto handle = s_NextSystem;
		s_NextSystem = 0x80000000u | (s_NextSystem + 1);
		system->Handle = handle;
		s_Systems.emplace(handle, std::move(system));
		return handle;
	}

	static void InvokeEntity(PendingCall& call);
	static void DrainPendingEntities();

	void InvokeSystem(uint32_t handle, Event event, Scene* source, const EventData& data) {
		auto found = s_Systems.find(handle);
		if (found == s_Systems.end()) return;
		auto program = found->second->Module;
		program->Pending.emplace_back(found->second, event, source, data);
		if (program->Running) return;
		program->Running = true;
		auto& module = *program;
		while (!program->Pending.empty()) {
			auto [system, next, eventScene, notification] = std::move(program->Pending.front());
			program->Pending.pop_front();
			Scene* owner = eventScene ? eventScene : (system->Global ? SceneManager::GetActiveScene() : system->Owner);
			std::optional<SceneExecutionScope> scope;
			if (owner) scope.emplace(*owner);
			auto previousScene = ScriptEngine::GetScene();
			ScriptEngine::SetScene(owner);
			if (next == Event::Awake) system->Awake = true;
			if (next == Event::Enable || next == Event::Disable) system->Enabled = next == Event::Enable;
			if (next == Event::Start) system->Started = true;
			Frame frame{0, true, 0, next, 0, {}, {}, module.Path, system->Identity, module.Fields};
			frame.SystemHandle = system->Handle;
            frame.Notification = std::move(notification);
			auto previousFrame = std::exchange(s_Frame, &frame);
			auto previousSystem = std::exchange(s_CurrentSystem, system.get());
			char diagnostic[2048]{};
			int32_t code = 0;
			const int status = RunProgram(module, true, code, diagnostic, sizeof(diagnostic));
			s_CurrentSystem = previousSystem;
			s_Frame = previousFrame;
			if ((status != BOLT_HOST_OK || code != 0) && !module.ReportedRunError) {
				module.ReportedRunError = true;
				FNK_ERROR_TAG("Bolt", "System '{}' returned {}: {}", module.Path, code, diagnostic);
			}
			ScriptEngine::SetScene(previousScene);
		}
		program->Running = false;
		if (!s_Frame) DrainPendingEntities();
	}

#include "Scripting/Bolt/EditorWindows.inl"

	void ReleaseSystem(uint32_t handle) {
		InvokeSystem(handle, Event::ReleaseSystemState);
		s_Coroutines.EraseIf([handle](const auto& key) { return key.SystemHandle == handle; });
		s_Systems.erase(handle);
		std::erase_if(s_SystemPrograms, [](const auto& item) { return item.second.expired(); });
	}

	void PumpCoroutines(bool fixed, float deltaTime) {
		if (s_Coroutines.empty()) return;
		++s_Epoch;
		s_CoroutineDelta = deltaTime;
		const auto owners = s_Coroutines.Snapshot();
		const auto event = fixed ? Event::PumpFixed : Event::PumpUpdate;
		for (size_t index=0;index<owners->size();++index) {
            const auto& key=(*owners)[index];
            if (!s_Coroutines.Current(owners) && !s_Coroutines.contains(key)) continue;
			if (key.SystemHandle) { InvokeSystem(key.SystemHandle, event); continue; }
			const auto entity = static_cast<EntityHandle>(key.Entity);
			auto* scripts=key.Owner ? key.Owner->GetRegistry().try_get<ScriptComponent>(entity) : nullptr;
            if (!scripts || !scripts->HasScript(key.Identity,ScriptType::Bolt)) {
                s_Coroutines.erase(key);
                continue;
            }
            SceneExecutionScope scope(*key.Owner);
            auto previous=ScriptEngine::GetScene();
            ScriptEngine::SetScene(key.Owner);
            if (key.Module && key.Module->Active && s_Coroutines.Current(owners) && !s_Frame) {
                auto& program=*key.Module;
                PendingCall call{key.Owner,key.Identity,program,
                    {key.Entity,false,0,event,key.Attachment,{}, {},program.Path,key.Identity,program.Fields}};
                const bool batch=s_API.RunBatch && program.Jobs && program.Jobs->Image &&
                    index+1<owners->size() && (*owners)[index+1].Module==&program;
                if (!batch) {
                    InvokeEntity(call);
                } else {
                    struct Cursor {
                        const std::shared_ptr<const std::vector<CoroutineOwner>>& Owners;
                        size_t Index;
                        Frame& Context;
                        Program& Module;
                        Scene* ScenePtr;
                        void Bind(const CoroutineOwner& owner) {
                            Context.QueryBorrows.CloseAll();
                            Context.Entity=owner.Entity;
                            Context.Attachment=owner.Attachment;
                            Context.EntityProgram=&Module;
                            Context.UpdateSlot=Module.UpdateSlots[owner.Attachment];
                            Context.PumpingActive=true;
                            Context.Pump={Context.UpdateSlot,s_Epoch,s_CoroutineDelta,static_cast<uint32_t>(Context.Callback),Module.WatchedSlots[owner.Attachment],1};
                        }
                        static int Advance(void* data) {
                            auto& c=*static_cast<Cursor*>(data);
                            if (c.Context.State || !s_Pending.empty() || !s_Coroutines.Current(c.Owners) || c.Index+1>=c.Owners->size()) return 0;
                            const auto& next=(*c.Owners)[c.Index+1];
                            if (next.Module!=&c.Module || next.Owner!=c.ScenePtr || next.SystemHandle) return 0;
                            const auto entity=static_cast<EntityHandle>(next.Entity);
                            auto* scripts=c.ScenePtr->GetRegistry().try_get<ScriptComponent>(entity);
                            if (!scripts || !scripts->HasScript(next.Identity,ScriptType::Bolt)) return 0;
                            ++c.Index;
                            c.Context.Text.clear();
                            c.Context.QueryResults.clear();
                            c.Bind(next);
                            return 1;
                        }
                    };
                    auto& frame=call.Context;
                    Cursor cursor{owners,index,frame,program,key.Owner};
                    cursor.Bind(key);
                    s_Frame=&frame;
                    char diagnostic[2048]{};
                    int32_t code=0;
                    const int status=s_API.RunBatch(program.Jobs->Image,Cursor::Advance,&cursor,&code,diagnostic,sizeof(diagnostic));
                    s_Frame=nullptr;
                    index=cursor.Index;
                    if (status!=BOLT_HOST_OK || code) {
                        if (!program.ReportedRunError) FNK_ERROR_TAG("Bolt","{} on entity {} returned {}: {}",program.Path,frame.Entity,code,diagnostic);
                        program.ReportedRunError=true;
                    } else if (frame.State) {
                        if (auto* store=key.Owner->GetRegistry().try_get<ScriptComponent>(static_cast<EntityHandle>(frame.Entity)))
                            for (auto& live:store->Scripts) if (live.GetType()==ScriptType::Bolt && live.GetClassName()==key.Identity) {
                                live.SetBoltState(frame.State);
                                break;
                            }
                    }
                }
                if (!s_Pending.empty()) DrainPendingEntities();
            } else {
                ScriptInstance instance(key.Identity,ScriptType::Bolt);
                Dispatch(instance,entity,event);
            }
            ScriptEngine::SetScene(previous);
		}
	}

    namespace {
        void CancelProgramCoroutines(Program& program) {
            const auto owners=s_Coroutines.Snapshot();
            for (const auto& owner:*owners) {
                if(!s_Coroutines.contains(owner)) continue;
                if(owner.SystemHandle) {
                    auto it=s_Systems.find(owner.SystemHandle);
                    if(it==s_Systems.end() || it->second->Module.get()!=&program)continue;
                    InvokeSystem(owner.SystemHandle,Event::CancelCoroutines);
                } else {
                    auto it=s_Programs.find(owner.Identity);
                    if(it==s_Programs.end() || &it->second!=&program)continue;
                    const auto entity=static_cast<EntityHandle>(owner.Entity);
                    if(owner.Owner && owner.Owner->IsValid(entity)) {
                        SceneExecutionScope scope(*owner.Owner);
                        auto* previous=ScriptEngine::GetScene();
                        ScriptEngine::SetScene(owner.Owner);
                        ScriptInstance instance(owner.Identity,ScriptType::Bolt);
                        Dispatch(instance,entity,Event::CancelCoroutines);
                        ScriptEngine::SetScene(previous);
                    }
                }
                s_Coroutines.erase(owner);
            }
        }
    }

    void RegisterNativeStructs() {
        if (!Application::IsMainThread()) return;
        const auto* app = Application::GetInstance();
        if (!app || !app->GetSceneManager()) return;
        static uint64_t lastAssets=UINT64_MAX, lastRegistry=UINT64_MAX;
        auto& registry=SceneManager::GetComponentRegistry();
        const auto version=AssetRegistry::GetChangeVersion();
        if(lastAssets==version && lastRegistry==registry.GetGeneration())return;
#ifdef FUNKE_BOLT_AOT
        std::string error;
        if (!InstallAotLayouts(error)) { FNK_ERROR_TAG("Bolt", "{}", error); return; }
#else
#ifdef FNK_PLATFORM_WINDOWS
        if (IsWindowsAotPlayer()) {
            std::string error;
            if (!InstallWindowsLayouts(error)) { FNK_ERROR_TAG("Bolt", "{}", error); return; }
        } else
#endif
        {
            const auto units = ComponentUnits();
            auto first = std::find_if(units.begin(), units.end(), [](const auto& unit) {
                return !unit.Managed && unit.Kind == ScriptBase::NativeStruct;
            });
            if (first != units.end()) {
                std::string diagnostic;
                BoltCompiler* compiler = nullptr;
                bool entity = false;
                std::vector<Field> unused;
                if (!s_API.Open(diagnostic) || !Compile(s_API, compiler, first->Path, entity, unused, diagnostic, &units, nullptr, true))
                    FNK_ERROR_TAG("Bolt", "Cannot register ECS structs: {}", diagnostic);
                if (compiler) s_API.Destroy(compiler);
            }
        }
#endif
        lastAssets=version;lastRegistry=registry.GetGeneration();
    }

	void BroadcastApplicationEvent(Event event, Scene* source, const EventData& data) {
        const bool sceneLoad = event >= Event::SceneLoadSucceeded && event <= Event::PumpSceneLoads;
		for (auto& [identity, program] : s_Programs) {
			if (!program.Active || !program.EntityScript || (event == Event::UiDispatch && !program.UiEvents) || (sceneLoad && !program.SceneLoads)) continue;
			s_Pending.push_back({nullptr, identity, program,
				{0, true, 0, event, 0, {}, {}, program.Path, identity, program.Fields}, source});
            s_Pending.back().Context.Notification = data;
		}
		std::unordered_set<SystemProgram*> modules;
		std::vector<uint32_t> handles;
		for (const auto& [handle, system] : s_Systems) {
			if (sceneLoad && !system->Module->SceneLoads) continue;
			if (event == Event::UiDispatch && (!system->Module->UiEvents || (!system->Global && system->Owner != source))) continue;
			if (modules.insert(system->Module.get()).second) handles.push_back(handle);
		}
		for (const auto handle : handles) InvokeSystem(handle, event, source, data);
		if (!s_Frame) DrainPendingEntities();
		if (event == Event::NotifyPlayModeExited) {
            s_PlayResetActive = false;
            RuntimeTextures::ExitPlay();
        }
	}

    bool ResetRegisteredPlayState() {
        s_PlayResetActive = true;
        ++s_PlayResetEpoch;
        std::vector<std::string> candidates;
        for (const auto& [identity, program] : s_Programs)
            if (program.Active && program.HasPlayReset) candidates.push_back(identity);
        for (const auto& asset : AssetRegistry::GetAssetsByKind(AssetKind::Script)) {
            const auto identity = std::to_string(asset.Id);
            if (s_Programs.contains(identity) || !ScriptTypeEqualsIgnoreCase(Path::FromUtf8(asset.Path).extension().string(), ".bolt")) continue;
            auto source = File::ReadAllText(asset.Path);
            if (source.find("ResetOnPlay") == std::string::npos) continue;
            ScriptBase kind;
            ScriptClass(source, kind);
            if (kind != ScriptBase::Procedural && kind != ScriptBase::Entity) continue;
            std::vector<std::pair<std::string, std::string>> calls;
            try { BindPlayResetMethods(source, asset.Path, calls); }
            catch (const std::exception& error) { FNK_ERROR_TAG("Bolt", "{}: {}", asset.Path, error.what()); return false; }
            if (!calls.empty()) candidates.push_back(identity);
        }
        std::sort(candidates.begin(), candidates.end());
        for (const auto& identity : candidates) {
            auto [entry, inserted] = s_Programs.try_emplace(identity);
            auto& program = entry->second;
            if (inserted) Refresh(identity, program, true);
            if (!program.Active || !ResetModulePlayState(identity, program)) return false;
        }
        std::vector<std::pair<std::string, std::shared_ptr<SystemProgram>>> systems;
        for (const auto& [identity, weak] : s_SystemPrograms)
            if (auto program = weak.lock(); program && program->Active && program->HasPlayReset)
                systems.emplace_back(identity, std::move(program));
        std::sort(systems.begin(), systems.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [identity, program] : systems)
            if (!ResetModulePlayState(identity, *program)) return false;
        return true;
    }

	void BroadcastSystems(Event event) {
		std::vector<uint32_t> handles;
		for (const auto& [handle, system] : s_Systems) handles.push_back(handle);
		for (const auto handle : handles) InvokeSystem(handle, event);
	}

	void SetSystemField(uint32_t handle, const char* name, const char* value) {
		auto found = s_Systems.find(handle);
		if (found == s_Systems.end() || !name || !value) return;
		found->second->Overrides[name] = value;
		auto previous = s_Inspection;
		std::string filter(name);
		s_Inspection = {&filter, nullptr};
		InvokeSystem(handle, Event::ApplyField);
		s_Inspection = previous;
	}

	static std::string FieldJson(const std::vector<Field>& fields, const std::unordered_map<std::string,Json::Value>* enumOptions = nullptr) {
		auto array = Json::Value::MakeArray();
		for (const auto& f : fields) {
			auto row = Json::Value::MakeObject();
			std::string tag = f.Type;
			if (tag.starts_with("Vector") || tag == "Color") tag.front() = static_cast<char>(std::tolower(tag.front()));
			row.AddMember("name", f.Name);
			row.AddMember("type", tag);
            if (enumOptions && (tag == "enum" || tag == "flagenum")) {
                auto options = enumOptions->find(f.Name);
                if (options != enumOptions->end()) row.AddMember("enumOptions", options->second);
                row.AddMember("enumIsFlags", tag == "flagenum");
            }
			row.AddMember("displayName", f.DisplayName);
			row.AddMember("value", f.Value);
			row.AddMember("readOnly", f.ReadOnly);
			row.AddMember("tooltip", f.Tooltip);
            row.AddMember("headerContent", f.HeaderContent);
            row.AddMember("headerSize", f.HeaderSize);
            row.AddMember("spaceHeight", f.SpaceHeight);
            row.AddMember("clampMin", f.ClampMin);
            row.AddMember("clampMax", f.ClampMax);
            row.AddMember("hasSpace", f.HasSpace);
            row.AddMember("hasClamp", f.HasClamp);
            row.AddMember("hasEnabledIf", f.HasEnabledIf);
            row.AddMember("enabledIfAny", f.EnabledIfAny);
            row.AddMember("enabledIfField", f.EnabledIfField);
            row.AddMember("enabledIfValue", f.EnabledIfValue);
			array.Append(std::move(row));
		}
		return Json::Stringify(array);
	}

#include "Scripting/Bolt/DataAssets.inl"

	std::string SystemFieldDefinitions(const std::string& identity) {
		auto path = ResolveAsset(identity);
		if (path.empty()) return "[]";
		auto source = File::ReadAllText(path);
		ScriptBase kind = ScriptBase::Procedural;
		FieldList list{ScriptClass(source, kind)};
		if (kind != ScriptBase::Scene) return "[]";
#ifdef FUNKE_BOLT_AOT
        const auto* module = FindAotModule(identity);
        return module ? FieldJson(module->Fields) : "[]";
#endif
#ifdef FNK_PLATFORM_WINDOWS
        if (IsWindowsAotPlayer()) {
            WindowsFields(identity, list.Fields);
            return FieldJson(list.Fields);
        }
#endif
		AddReferences(list, ComponentUnits());
		std::string error;
		if (!s_API.Open(error) || !s_API.VisitFields) return "[]";
		BoltSource unit{path.c_str(), source.data(), source.size()};
		char diagnostic[1024]{};
		if (s_API.VisitFields(&unit, CurrentCompilerMode().Profile, CollectField, &list, diagnostic, sizeof(diagnostic)) != BOLT_HOST_OK) return "[]";
		return FieldJson(list.Fields);
	}

	const char* SystemFields(uint32_t handle) {
		auto found = s_Systems.find(handle);
		if (found == s_Systems.end() || found->second->Module->Running) return "[]";
		auto fields = found->second->Module->Fields;
		auto previous = s_Inspection;
		s_Inspection = {nullptr, &fields};
		InvokeSystem(handle, Event::Inspect);
		s_Inspection = previous;
		static thread_local std::string result;
		result = FieldJson(fields);
		return result.c_str();
	}

	void Dispatch(ScriptInstance& instance, EntityHandle entity, Event event, const Contact& contact) {
		if (event==Event::Update && !s_Frame && instance.GetCallbackMask()<s_DispatchSlots.size()) {
            const auto& cached=s_DispatchSlots[instance.GetCallbackMask()];
            if (cached.Owner==ScriptEngine::GetScene() && cached.Entity==static_cast<uint32_t>(entity) &&
                cached.Module && *cached.Identity==instance.GetClassName()) {
                auto& program=*cached.Module;
                if (!program.Active || program.ComponentOnly) return;
                if (program.EntityScript) {
                    if (program.InheritsEmptyUpdate && instance.HasStarted() && program.ReadySlots[cached.Attachment]) return;
                    PendingCall call{cached.Owner,*cached.Identity,program,
                        {cached.Entity,instance.HasStarted(),instance.GetBoltState(),event,cached.Attachment,contact,{},program.Path,*cached.Identity,program.Fields}};
                    InvokeEntity(call);
                    if (!s_Pending.empty()) DrainPendingEntities();
                    return;
                }
            }
        }
        if (event != Event::Update && event != Event::Enable && !s_Programs.contains(instance.GetClassName())) return;
		auto [it, inserted] = s_Programs.try_emplace(instance.GetClassName());
		Program& program = it->second;
		if (inserted) Refresh(it->first, program, true);
		if (!program.Active || program.ComponentOnly) return;
		if (!program.EntityScript && event != Event::Update) return;
		const auto key = std::make_pair(static_cast<const Scene*>(ScriptEngine::GetScene()), static_cast<uint32_t>(entity));
		uint64_t attachment = 0;
		if (program.EntityScript) {
			auto found = program.Attachments.find(key);
			if (found == program.Attachments.end()) {
				if (event != Event::Update && event != Event::Enable) return;
				const bool reuse = !program.FreeAttachments.empty();
				const auto index = reuse ? program.FreeAttachments.back() : program.NextAttachment;
				if (index >= INT32_MAX) return;
				if (!reuse) { program.UpdateSlots.resize(index + 1); program.ReadySlots.resize(index + 1); program.WatchedSlots.resize(index+1); program.DispatchTokens.resize(index+1,UINT32_MAX); }
				found = program.Attachments.emplace(key, index).first;
				if (reuse) program.FreeAttachments.pop_back();
				else ++program.NextAttachment;
			}
			attachment = found->second;
            auto& token=program.DispatchTokens[attachment];
            if (event==Event::Update && !s_Frame) {
                if (token==UINT32_MAX) {
                    if (!s_FreeDispatchSlots.empty()) {
                        token=s_FreeDispatchSlots.back(); s_FreeDispatchSlots.pop_back();
                    } else if (s_DispatchSlots.size()<UINT32_MAX) {
                        token=static_cast<uint32_t>(s_DispatchSlots.size()); s_DispatchSlots.emplace_back();
                    }
                    if (token!=UINT32_MAX) s_DispatchSlots[token]={ScriptEngine::GetScene(),static_cast<uint32_t>(entity),&it->first,&program,attachment};
                }
                instance.SetCallbackMask(token);
            }
            if (event==Event::Destroy && token!=UINT32_MAX) {
                s_DispatchSlots[token]={}; s_FreeDispatchSlots.push_back(token); token=UINT32_MAX;
            }
            if (event==Event::Update && program.InheritsEmptyUpdate && instance.HasStarted()
                && !s_Frame && program.ReadySlots[attachment]) return;
            if (event == Event::Destroy) {
				s_Coroutines.erase({ScriptEngine::GetScene(), static_cast<uint32_t>(entity), it->first, 0});
				program.Attachments.erase(found);
                program.ReloadStates.erase(attachment);
			}
		}
		PendingCall call{ScriptEngine::GetScene(), it->first, program,
			{static_cast<uint32_t>(entity), instance.HasStarted(), instance.GetBoltState(), event, attachment, contact, {}, program.Path, it->first, program.Fields}};
		// Nested notifications run after the callback releases its Bolt object and execution lock.
		if (s_Frame) { s_Pending.push_back(std::move(call)); return; }
		InvokeEntity(call);
		DrainPendingEntities();
	}

	static void InvokeEntity(PendingCall& call) {
		Frame& frame = call.Context;
		Program& program = call.Script;
        frame.PumpingActive=frame.Callback==Event::PumpUpdate || frame.Callback==Event::PumpFixed;
		const auto entity = static_cast<EntityHandle>(frame.Entity);
		if (frame.Callback != Event::Destroy && (!call.Owner || !call.Owner->IsValid(entity))) return;
		const auto previousState = frame.State;
		frame.EntityProgram = &program;
		if (frame.Attachment && frame.Attachment < program.UpdateSlots.size()) {
			auto& slot = program.UpdateSlots[frame.Attachment];
			frame.UpdateSlot = slot;
            if (frame.Callback==Event::Update) frame.Pump={slot,0,0,static_cast<uint32_t>(Event::Update),0,0};
            else if (frame.PumpingActive) frame.Pump={slot,s_Epoch,s_CoroutineDelta,static_cast<uint32_t>(frame.Callback),program.WatchedSlots[frame.Attachment],1};
            if (frame.Callback == Event::Destroy || frame.Callback == Event::Disable) program.ReadySlots[frame.Attachment]=0;
            if (frame.Callback == Event::Destroy) { slot=nullptr; program.WatchedSlots[frame.Attachment]=0; }
		}
		s_Frame = &frame;
		int32_t code = 0;
		char diagnostic[2048];
		const int status = RunProgram(program, program.EntityScript, code, diagnostic, sizeof(diagnostic));
		s_Frame = nullptr;
		if (frame.Callback == Event::Destroy && status == BOLT_HOST_OK && code == 0 && frame.Attachment)
			program.FreeAttachments.push_back(frame.Attachment);
		if (status != BOLT_HOST_OK || code != 0) {
			if (!program.ReportedRunError) {
				program.ReportedRunError = true;
				FNK_ERROR_TAG("Bolt", "{} on entity {} returned {}: {}", program.Path, frame.Entity, code, diagnostic);
			}
			return;
		}
		if (frame.Callback==Event::Update && frame.Attachment) program.ReadySlots[frame.Attachment]=1;
        if ((frame.Started || frame.Callback != Event::Update) && frame.State == previousState) return;
		if (!call.Owner->IsValid(entity) || !call.Owner->HasComponent<ScriptComponent>(entity)) return;
		for (auto& live : call.Owner->GetComponent<ScriptComponent>(entity).Scripts) {
			if (live.GetType() != ScriptType::Bolt || live.GetClassName() != call.Identity) continue;
			if (frame.Callback == Event::Update) live.MarkStarted();
			if (frame.State != previousState) live.SetBoltState(frame.State);
			break;
		}
	}

	static void DrainPendingEntities() {
		while (!s_Pending.empty()) {
			auto next = std::move(s_Pending.front());
			s_Pending.pop_front();
			if (!next.Owner) {
                std::optional<SceneExecutionScope> scope;
                auto* previousScene = ScriptEngine::GetScene();
                if (next.EventScene) {
                    scope.emplace(*next.EventScene);
                    ScriptEngine::SetScene(next.EventScene);
                }
				next.Context.EntityProgram = &next.Script;
				s_Frame = &next.Context;
				int32_t code = 0;
				char diagnostic[2048]{};
				const int status = RunProgram(next.Script, true, code, diagnostic, sizeof(diagnostic));
				s_Frame = nullptr;
                ScriptEngine::SetScene(previousScene);
				if (status != BOLT_HOST_OK || code != 0)
					FNK_ERROR_TAG("Bolt", "{} application event returned {}: {}", next.Identity, code, diagnostic);
				continue;
			}
			SceneExecutionScope scope(*next.Owner);
			Scene* previous = ScriptEngine::GetScene();
			ScriptEngine::SetScene(next.Owner);
			InvokeEntity(next);
			ScriptEngine::SetScene(previous);
		}
	}

	static void InspectAttachment(Entity entity, const std::string& identity, Event event, Inspection request) {
		if (s_Frame || !entity.HasComponent<ScriptComponent>()) return;
		auto program = s_Programs.find(identity);
		if (program == s_Programs.end() || !program->second.EntityScript) return;
		Scene* scene = entity.GetScene();
		if (!scene || !program->second.Attachments.contains({scene, static_cast<uint32_t>(entity.GetHandle())})) return;
		SceneExecutionScope scope(*scene);
		Scene* previous = ScriptEngine::GetScene();
		ScriptEngine::SetScene(scene);
		s_Inspection = request;
		ScriptInstance attachment(identity, ScriptType::Bolt);
		Dispatch(attachment, entity.GetHandle(), event);
		s_Inspection = {};
		ScriptEngine::SetScene(previous);
	}

	std::vector<Field> GetFields(Entity entity, const std::string& identity) {
		if (!entity.HasComponent<ScriptComponent>()) return {};
		std::vector<Field> result;
		auto program = s_Programs.find(identity);
		if (program != s_Programs.end() && program->second.Active) {
			result = program->second.Fields;
#ifdef FUNKE_BOLT_AOT
        } else if (const auto* module = FindAotModule(identity)) {
            result = module->Fields;
#elif defined(FNK_PLATFORM_WINDOWS)
        } else if (IsWindowsAotPlayer()) {
            WindowsFields(identity, result);
#endif
        } else {
			const auto path = ResolveAsset(identity);
			if (path.empty()) return {};
			std::error_code ec;
			const auto time = std::filesystem::last_write_time(Path::FromUtf8(path), ec);
			auto& cached = s_Schemas[identity];
			if (ec) return {};
			if (cached.Path != path || cached.Modified != time) {
				cached = {path, time};
				std::string error;
				if (s_API.Open(error) && s_API.VisitFields) {
					const auto source = File::ReadAllText(path);
					ScriptBase kind = ScriptBase::Procedural;
					FieldList list{ScriptClass(source, kind)};
					AddReferences(list, ComponentUnits());
					BoltSource unit{path.c_str(), source.data(), source.size()};
					char diagnostic[1024]{};
					if (!list.Owner.empty() && s_API.VisitFields(&unit, CurrentCompilerMode().Profile, CollectField, &list,
						diagnostic, sizeof(diagnostic)) == BOLT_HOST_OK) cached.Fields = std::move(list.Fields);
				}
			}
			result = cached.Fields;
		}
		if (result.empty()) return result;
		const auto& values = entity.GetComponent<ScriptComponent>().PendingFieldValues;
		for (auto& field : result) {
			if (auto value = values.find(identity + "." + field.Name); value != values.end()) field.Value = value->second;
		}
		InspectAttachment(entity, identity, Event::Inspect, {nullptr, &result});
		return result;
	}

	void ApplyField(Entity entity, const std::string& identity, const std::string& name) {
		InspectAttachment(entity, identity, Event::ApplyField, {&name, nullptr});
	}

	std::string RebuildErrors() {
		std::string errors;
		for (const auto& [id, error] : s_ValidationErrors) {
			if (!errors.empty()) errors += '\n';
			errors += error;
		}
		for (const auto& [id, program] : s_Programs) {
			if (program.Error.empty()) continue;
			if (!errors.empty()) errors += '\n';
			errors += program.Error;
		}
		for (const auto& [handle, system] : s_Systems) {
			const auto& message = system->Module->Error;
			if (!message.empty()) { if (!errors.empty()) errors += '\n'; errors += message; }
		}
		return errors;
	}

	bool QueueChangedScripts(bool force) {
#ifdef FUNKE_BOLT_AOT
        return false;
#elif defined(FNK_PLATFORM_WINDOWS)
        if (IsWindowsAotPlayer()) return false;
#endif
		const auto now = std::chrono::steady_clock::now();
		if (!force && now < s_NextPoll) return !s_QueuedScripts.empty();
		s_NextPoll = now + std::chrono::seconds(1);
		std::map<std::string,std::string> sources;
		ScriptFileStamps files;
		for (auto& unit : ComponentUnits(&files)) sources.emplace(unit.Managed ? unit.Path : std::to_string(unit.Guid),std::move(unit.Source));
		if (s_LastScriptFiles) {
			for (const auto& [id, stamp] : files) {
				auto previous = s_LastScriptFiles->find(id);
				if (previous == s_LastScriptFiles->end() || previous->second != stamp) s_QueuedScripts.insert(id);
			}
		}
		std::erase_if(s_ValidationErrors, [&](const auto& entry) { return !files.contains(entry.first); });
		if (sources != s_ComponentSources) {
			s_ComponentSources.swap(sources);
			force = true;
			s_Schemas.clear();
		}
		auto queueLive = [&](const std::string& id, const Program& program) {
			const auto file = files.find(id);
			if (file != files.end()) {
				if (force || file->second.first != program.Path || file->second.second != program.Modified)
					s_QueuedScripts.insert(id);
				return;
			}
			const auto path = ResolveAsset(id);
			std::error_code ec;
			const auto modified = std::filesystem::last_write_time(Path::FromUtf8(path), ec);
			if (force || path != program.Path || modified != program.Modified) s_QueuedScripts.insert(id);
		};
		for (const auto& [id, program] : s_Programs) queueLive(id, program);
		for (const auto& [id, weak] : s_SystemPrograms)
			if (auto program = weak.lock()) queueLive(id, *program);
		if (force) for (const auto& [id, error] : s_ValidationErrors) s_QueuedScripts.insert(id);
		s_LastScriptFiles = std::move(files);
		if (s_QueuedScripts.empty()) RegisterNativeStructs();
		return !s_QueuedScripts.empty();
	}

	bool ReloadQueuedScripts() {
		auto pending = std::exchange(s_QueuedScripts, {});
		if (pending.empty()) return false;
		RegisterNativeStructs();
		for (const auto& id : pending) s_ValidationErrors.erase(id);
		for (auto& [id, program] : s_Programs) {
			if (!pending.contains(id)) continue;
			Refresh(id, program, true);
			if (program.Error.empty())
				s_Coroutines.EraseIf([&](const auto& key) { return !key.SystemHandle && key.Identity == id; });
		}
		std::unordered_set<std::string> reloaded;
		for (const auto& [handle, system] : s_Systems) {
			if (!pending.contains(system->Identity) || reloaded.contains(system->Identity) || system->Module->Running) continue;
			Refresh(system->Identity, *system->Module, true, system->Global ? ScriptBase::Global : ScriptBase::Scene);
			reloaded.insert(system->Identity);
		}
		std::vector<uint32_t> handles;
		for (const auto& [handle, system] : s_Systems)
			if (reloaded.contains(system->Identity) && system->Module->Error.empty()) handles.push_back(handle);
		for (auto handle : handles) {
			const auto found = s_Systems.find(handle);
			if (found == s_Systems.end()) continue;
			auto system = found->second;
			s_Coroutines.EraseIf([handle](const auto& key) { return key.SystemHandle == handle; });
			InvokeSystem(handle, Event::ApplyField);
			if (system->Awake) InvokeSystem(handle, Event::Awake);
			if (system->Enabled) InvokeSystem(handle, Event::Enable);
			if (system->Started) InvokeSystem(handle, Event::Start);
		}
		for (const auto& id : pending) {
			if (s_Programs.contains(id) || reloaded.contains(id)) continue;
			const auto path = ResolveAsset(id);
			std::string error;
			if (!path.empty() && !ValidateFiles({path}, error)) {
				s_ValidationErrors[id] = error;
				FNK_ERROR_TAG("Bolt", "{}", error);
			}
		}
		return true;
	}

	bool PollChanges(bool force) {
		return QueueChangedScripts(force) && ReloadQueuedScripts();
	}

	void Shutdown() {
        s_PlayResetActive = false;
        while (!s_EditorWindows.empty()) CloseEditorWindow(s_EditorWindows.begin()->first);
        s_EditorCatalog.clear();
        s_DataAssets.clear();
        s_DataModules.clear();
		for(auto& [id,program]:s_Programs) CancelProgramCoroutines(program);
		for (auto& [id, program] : s_Programs) {
			if (program.Compiler) s_API.Destroy(program.Compiler);
		}
		s_DispatchSlots.clear();
        s_FreeDispatchSlots.clear();
        s_Programs.clear();
		s_ConsoleCommands.clear();
        s_ConsoleAttributes.clear();
        s_CommandAssets.clear();
        s_CommandCatalogVersion = UINT64_MAX;
		SceneLoads::ReleaseGeneration(nullptr);
		AssetLoads::ReleaseGeneration(nullptr);
		RuntimeTextures::Shutdown();
		s_Schemas.clear();
		s_ComponentSources.clear();
		s_LastScriptFiles.reset();
		s_QueuedScripts.clear();
		s_ValidationErrors.clear();
		s_NextPoll = {};
		if (!s_Systems.empty()) return;
		if (s_API.Library) Platform::UnloadSharedLibrary(s_API.Library);
		s_API = {};
	}
}
