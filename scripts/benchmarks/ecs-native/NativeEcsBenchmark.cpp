#include "Scripting/NativeScript.hpp"

#include "Components/General/Transform2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Core/Application.hpp"
#include "Scripting/EntityCommandBufferWire.hpp"
#include "Scripting/NativeEntityCommandBuffer.hpp"
#include "Scripting/ScriptBindings.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

	using namespace Funke;
	using Clock = std::chrono::steady_clock;

	void Require(bool condition, const char* message) {
		if (!condition) throw std::runtime_error(message);
	}

	std::string Environment(const char* name, const std::string& fallback = {}) {
#ifdef _WIN32
		char* value = nullptr;
		size_t length = 0;
		_dupenv_s(&value, &length, name);
		const std::string result = value ? value : fallback;
		std::free(value);
		return result;
#else
		const char* value = std::getenv(name);
		return value ? value : fallback;
#endif
	}

	int PositiveEnvironment(const char* name, int fallback, bool allowZero = false) {
		const int value = std::stoi(Environment(name, std::to_string(fallback)));
		Require(value >= (allowZero ? 0 : 1), "Invalid benchmark size/sample setting");
		return value;
	}

	template<typename Fn>
	double Measure(Fn&& action) {
		const auto begin = Clock::now();
		action();
		return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
	}

	class WireBuffer {
	public:
		void Reserve(size_t capacity) { m_Bytes.reserve(capacity); }
		void Begin(uint32_t entities, uint32_t commands) {
			m_Bytes.clear();
			Append(entities);
			Append(commands);
			for (uint32_t i = 0; i < entities; ++i) Append(kEcbNoName);
		}

		void DefaultComponent(uint32_t entity, uint32_t type) {
			Command(Ecb_DefaultConstructComponent, entity, type, 0);
		}

		void Live(EcbOpcode opcode, uint32_t type, uint64_t identity) {
			Command(opcode, kEcbNoName, type, sizeof(identity));
			Append(identity);
		}

		int Playback(const NativeBindings& bindings, std::vector<uint64_t>& identities) const {
			Require(m_Bytes.size() <= static_cast<size_t>(std::numeric_limits<int>::max()), "ECB exceeds wire size limit");
			return bindings.Ecb_PlaybackWithMode(m_Bytes.data(), static_cast<int>(m_Bytes.size()),
				identities.data(), static_cast<int>(identities.size()), 2);
		}

	private:
		template<typename T>
		void Append(T value) {
			const size_t offset = m_Bytes.size();
			m_Bytes.resize(offset + sizeof(value));
			std::memcpy(m_Bytes.data() + offset, &value, sizeof(value));
		}

		void Command(EcbOpcode opcode, uint32_t entity, uint32_t type, uint16_t payloadSize) {
			Append(static_cast<uint8_t>(opcode));
			Append(entity);
			Append(type);
			Append(payloadSize);
		}

		std::vector<uint8_t> m_Bytes;
	};

	enum class Operation { Create2, AddSprite, RemoveSprite, Destroy };
	enum class Mode { NativeDirect, NativeECB, NativeWireParallel };

	struct BenchmarkCase {
		Operation Action;
		Mode Execution;
		const char* OperationName;
		const char* ModeName;
	};

}

class NativeEcsBenchmark final : public Funke::NativeScript {
public:
	void Update() override {
		if (m_Finished) return;
		try {
			if (!m_Initialized) Initialize();
			if (m_IdleFrames-- > 0) return;
			if (m_CaseIndex == m_Cases.size()) {
				Complete();
				return;
			}
			RunSample(m_Cases[m_CaseIndex]);
			if (++m_Iteration == m_Warmups + m_Samples) {
				++m_CaseIndex;
				m_Iteration = 0;
			}
		} catch (const std::exception& error) {
			Fail(error.what());
		} catch (...) {
			Fail("Unknown native benchmark exception");
		}
	}

private:
	void Initialize() {
		m_Output = Environment("ECS_NATIVE_OUTPUT");
		Require(!m_Output.empty(), "ECS_NATIVE_OUTPUT must be set");
		const auto directory = std::filesystem::path(m_Output).parent_path();
		if (!directory.empty()) std::filesystem::create_directories(directory);
		m_Count = PositiveEnvironment("ECS_NATIVE_SIZE", 10000);
		m_Warmups = PositiveEnvironment("ECS_NATIVE_WARMUPS", 3, true);
		m_Samples = PositiveEnvironment("ECS_NATIVE_SAMPLES", 9);
		Require(m_Count <= 10000000, "Benchmark size exceeds supported safety bound");
		Require(m_Scene != nullptr && m_Scene->IsValid(GetEntityHandle()), "Native benchmark has no live owning scene");
		m_Baseline = m_Scene->GetEntityCount();
		Require(m_Baseline == 1, "Benchmark scene must contain only its controller entity");
		Funke::ScriptBindings::PopulateNativeBindings(m_Bindings);
		m_TransformType = m_Bindings.Component_GetTypeId("Transform2D");
		m_SpriteType = m_Bindings.Component_GetTypeId("SpriteRenderer");
		Require(m_TransformType != 0 && m_SpriteType != 0, "Native component type lookup failed");
		m_Identities.resize(m_Count);
		m_Handles.resize(m_Count, entt::null);
		m_Entities.assign(m_Count, Funke::Entity::Null);
		m_SortedIds.reserve(m_Count);
		m_Wire.Reserve(8 + static_cast<size_t>(m_Count) * 26);
		m_SetupWire.Reserve(8 + static_cast<size_t>(m_Count) * 26);
		m_CleanupWire.Reserve(8 + static_cast<size_t>(m_Count) * 19);

		const std::string filter = Environment("ECS_NATIVE_FILTER");
		const std::regex selected(filter);
		for (const auto execution : { Mode::NativeDirect, Mode::NativeECB, Mode::NativeWireParallel }) {
			for (const auto action : { Operation::Create2, Operation::AddSprite, Operation::RemoveSprite, Operation::Destroy }) {
				if (execution == Mode::NativeECB && action != Operation::Create2) continue;
				const char* operationName = action == Operation::Create2 ? "Create2" : action == Operation::AddSprite ? "AddSprite"
					: action == Operation::RemoveSprite ? "RemoveSprite" : "Destroy";
				const char* modeName = execution == Mode::NativeDirect ? "NativeDirect" : execution == Mode::NativeECB ? "NativeECB" : "NativeWireParallel";
				if (filter.empty() || std::regex_search(std::string(operationName) + "/" + modeName, selected)) {
					m_Cases.push_back({ action, execution, operationName, modeName });
				}
			}
		}
		Require(!m_Cases.empty(), "No benchmark cases match ECS_NATIVE_FILTER");
		m_Csv.open(m_Output + ".csv", std::ios::trunc);
		Require(m_Csv.is_open(), "Cannot open native benchmark CSV");
		m_Csv.imbue(std::locale::classic());
		m_Csv << "Operation,Mode,Count,Sample,Warmup,RecordMs,PlaybackMs,TotalMs\n" << std::fixed << std::setprecision(6);
		m_Initialized = true;
	}

	void RecordCreate(WireBuffer& wire, bool sprite) {
		wire.Begin(m_Count, m_Count * (sprite ? 2 : 1));
		for (int i = 0; i < m_Count; ++i) {
			wire.DefaultComponent(i, m_TransformType);
			if (sprite) wire.DefaultComponent(i, m_SpriteType);
		}
	}

	void RecordMutation(WireBuffer& wire, Operation action) {
		wire.Begin(0, m_Count);
		const auto opcode = action == Operation::AddSprite ? Ecb_DefaultConstructComponent
			: action == Operation::RemoveSprite ? Ecb_RemoveComponent : Ecb_DestroyEntity;
		const uint32_t type = action == Operation::Destroy ? 0 : m_SpriteType;
		for (const auto identity : m_Identities) wire.Live(opcode, type, identity);
	}

	void ResolveCreated() {
		for (int i = 0; i < m_Count; ++i) {
			Require(m_Identities[i] != 0 && m_Scene->TryResolveEntityRef(m_Identities[i], m_Handles[i]), "Created identity does not resolve");
			Require(m_Handles[i] != GetEntityHandle(), "Benchmark created controller alias");
			m_Entities[i] = m_Scene->GetEntity(m_Handles[i]);
		}
	}

	void CaptureCreated() {
		for (int i = 0; i < m_Count; ++i) {
			m_Handles[i] = m_Entities[i].GetHandle();
			m_Identities[i] = m_Scene->GetEntityPersistentID(m_Handles[i]);
		}
	}

	void ValidateLive(bool sprite) {
		Require(m_Scene->GetEntityCount() == m_Baseline + m_Count, "Unexpected live entity count");
		m_SortedIds.assign(m_Identities.begin(), m_Identities.end());
		std::sort(m_SortedIds.begin(), m_SortedIds.end());
		Require(m_SortedIds.front() != 0 && std::adjacent_find(m_SortedIds.begin(), m_SortedIds.end()) == m_SortedIds.end(), "Nonunique or zero entity identities");
		for (int i = 0; i < m_Count; ++i) {
			Funke::EntityHandle resolved = entt::null;
			Require(m_Scene->TryResolveEntityRef(m_Identities[i], resolved) && resolved == m_Handles[i], "Entity identity changed");
			Require(resolved != GetEntityHandle() && m_Entities[i].IsValid(), "Invalid fixture entity");
			Require(m_Entities[i].IsRuntime(), "Created entity has incorrect origin");
			Require(m_Entities[i].HasComponent<Funke::Transform2DComponent>(), "Missing Transform2D");
			const auto& transform = m_Entities[i].GetComponent<Funke::Transform2DComponent>();
			Require(transform.Position.x == 0 && transform.Position.y == 0 && transform.Scale.x == 1 && transform.Scale.y == 1
				&& transform.LocalPosition.x == 0 && transform.LocalPosition.y == 0 && transform.LocalScale.x == 1 && transform.LocalScale.y == 1
				&& transform.Rotation == 0 && transform.LocalRotation == 0, "Transform defaults differ");
			Require(m_Entities[i].HasComponent<Funke::SpriteRendererComponent>() == sprite, "Sprite presence differs");
			if (sprite) {
				const auto& renderer = m_Entities[i].GetComponent<Funke::SpriteRendererComponent>();
				Require(renderer.Color.r == 1 && renderer.Color.g == 1 && renderer.Color.b == 1 && renderer.Color.a == 1
					&& renderer.SortingOrder == 0 && renderer.SortingLayer == 0 && !renderer.TextureHandle.IsValid() && renderer.TextureHandle.generation == 0
					&& static_cast<uint64_t>(renderer.TextureAssetId) == 0 && renderer.FilterMode == Funke::Filter::Default
					&& renderer.Material == 0 && renderer.PixelsPerUnit == 0 && !renderer.FlipX && !renderer.FlipY, "Sprite defaults differ");
			}
		}
	}

	void ValidateDestroyed() {
		Require(m_Scene->GetEntityCount() == m_Baseline, "Destroyed fixture left entities behind");
		Require(m_Scene->IsValid(GetEntityHandle()), "Benchmark destroyed its controller");
		for (int i = 0; i < m_Count; ++i) {
			Funke::EntityHandle resolved = entt::null;
			Require(!m_Scene->IsValid(m_Handles[i]) && !m_Scene->TryResolveEntityRef(m_Identities[i], resolved), "Destroyed identity or handle is still alive");
		}
	}

	void RunSample(const BenchmarkCase& sample) {
		Require(m_Scene->GetEntityCount() == m_Baseline, "Sample started with leftover entities");
		if (sample.Action != Operation::Create2) {
			const bool sprite = sample.Action != Operation::AddSprite;
			RecordCreate(m_SetupWire, sprite);
			Require(m_SetupWire.Playback(m_Bindings, m_Identities) == m_Count, "Fixture creation failed");
			ResolveCreated();
			ValidateLive(sprite);
		}

		double recordMs = 0;
		double playbackMs = 0;
		double totalMs = 0;
		int result = 0;
		if (sample.Execution == Mode::NativeDirect) {
			totalMs = Measure([&] {
				for (int i = 0; i < m_Count; ++i) {
					switch (sample.Action) {
					case Operation::Create2:
						m_Entities[i] = Funke::Entity::Create();
						m_Entities[i].AddComponent<Funke::SpriteRendererComponent>();
						break;
					case Operation::AddSprite: m_Entities[i].AddComponent<Funke::SpriteRendererComponent>(); break;
					case Operation::RemoveSprite: m_Entities[i].RemoveComponent<Funke::SpriteRendererComponent>(); break;
					case Operation::Destroy: m_Entities[i].Destroy(); break;
					}
				}
			});
			if (sample.Action == Operation::Create2) CaptureCreated();
		} else if (sample.Execution == Mode::NativeECB) {
			recordMs = Measure([&] {
				m_NativeEcb.Clear();
				for (int i = 0; i < m_Count; ++i) m_NativeEcb.CreateWith<Funke::Transform2DComponent, Funke::SpriteRendererComponent>();
			});
			playbackMs = Measure([&] { result = m_NativeEcb.Playback(*m_Scene); });
			Require(result == m_Count, "Native public ECB playback failed");
			for (int i = 0; i < m_Count; ++i) m_Entities[i] = m_Scene->GetEntity(m_NativeEcb.GetCreatedEntity(i));
			CaptureCreated();
			totalMs = recordMs + playbackMs;
		} else {
			recordMs = Measure([&] {
				if (sample.Action == Operation::Create2) RecordCreate(m_Wire, true);
				else RecordMutation(m_Wire, sample.Action);
			});
			playbackMs = Measure([&] { result = m_Wire.Playback(m_Bindings, m_Identities); });
			Require(result == (sample.Action == Operation::Create2 ? m_Count : 0), "Native wire ECB playback failed");
			if (sample.Action == Operation::Create2) ResolveCreated();
			totalMs = recordMs + playbackMs;
		}

		if (sample.Action == Operation::Destroy) ValidateDestroyed();
		else {
			ValidateLive(sample.Action != Operation::RemoveSprite);
			RecordMutation(m_CleanupWire, Operation::Destroy);
			Require(m_CleanupWire.Playback(m_Bindings, m_Identities) == 0, "Fixture cleanup failed");
			ValidateDestroyed();
		}
		const bool warmup = m_Iteration < m_Warmups;
		m_Csv << sample.OperationName << ',' << sample.ModeName << ',' << m_Count << ','
			<< (warmup ? m_Iteration : m_Iteration - m_Warmups) << ',' << (warmup ? 1 : 0) << ','
			<< recordMs << ',' << playbackMs << ',' << totalMs << '\n';
		m_Csv.flush();
		Require(m_Csv.good(), "Writing native benchmark CSV failed");
	}

	void Complete() {
		Require(m_Scene->GetEntityCount() == m_Baseline, "Completion left benchmark entities alive");
		m_Csv.close();
		std::ofstream marker(m_Output + ".done");
		marker << "PASS\n";
		marker.close();
		Require(marker.good(), "Could not write completion marker");
		m_Finished = true;
		Print("Native ECS comparison PASS");
		Funke::Application::Quit();
	}

	void Fail(const std::string& message) {
		m_Finished = true;
		m_Csv.close();
		if (!m_Output.empty()) {
			std::ofstream marker(m_Output + ".failed");
			marker << message << '\n';
		}
		FNK_NATIVE_LOG_ERROR(message.c_str());
		Funke::Application::SetExitCode(1);
		Funke::Application::Quit();
	}

	bool m_Initialized = false;
	bool m_Finished = false;
	int m_IdleFrames = 3;
	int m_Count = 10000;
	int m_Warmups = 3;
	int m_Samples = 9;
	int m_Iteration = 0;
	size_t m_Baseline = 0;
	size_t m_CaseIndex = 0;
	uint32_t m_TransformType = 0;
	uint32_t m_SpriteType = 0;
	std::string m_Output;
	std::ofstream m_Csv;
	Funke::NativeBindings m_Bindings{};
	Funke::NativeEntityCommandBuffer m_NativeEcb;
	WireBuffer m_Wire;
	WireBuffer m_SetupWire;
	WireBuffer m_CleanupWire;
	std::vector<BenchmarkCase> m_Cases;
	std::vector<Funke::Entity> m_Entities;
	std::vector<Funke::EntityHandle> m_Handles;
	std::vector<uint64_t> m_Identities;
	std::vector<uint64_t> m_SortedIds;
};

REGISTER_SCRIPT(NativeEcsBenchmark)
