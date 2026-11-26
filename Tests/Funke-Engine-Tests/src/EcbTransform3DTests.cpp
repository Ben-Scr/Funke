#include <doctest/doctest.h>
#include "Scripting/EntityCommandBufferParallel.hpp"
#include "Core/Application.hpp"
#include "Scripting/NativeEntityCommandBuffer.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include <chrono>
#include <iostream>

using namespace Funke;
namespace {
	std::vector<uint8_t> TransformCommands(const std::vector<EntityHandle>& handles, uint32_t type, uint8_t opcode) {
		std::vector<uint8_t> bytes(handles.size() * 19);
		for (size_t index = 0; index < handles.size(); ++index) {
			auto* command = bytes.data() + index * 19;
			command[0] = opcode;
			const uint32_t target = kEcbNoName; const uint16_t payload = 8; const uint64_t identity = entt::to_integral(handles[index]);
			std::memcpy(command + 1, &target, 4); std::memcpy(command + 5, &type, 4);
			std::memcpy(command + 9, &payload, 2); std::memcpy(command + 11, &identity, 8);
		}
		return bytes;
	}
	void CustomTransformHook(entt::registry&, EntityHandle) {}
}
TEST_CASE("Native Transform3D ECB kernel matches ordered add remove and refuses dependent batches") {
	Application app;
	auto& components = SceneManager::GetComponentRegistry(); RegisterBuiltInComponents(components);
	const auto* info = components.GetInfo<Transform3DComponent>(); REQUIRE(info);
	auto scene = Scene::CreateDetachedScene("Transform3D batch");
	std::vector<EntityHandle> handles;
	for (int i = 0; i < 8193; ++i) handles.push_back(scene->CreateEmptyEntity().GetHandle());
	const EcbHeader header{0, static_cast<uint32_t>(handles.size())};
	auto add = TransformCommands(handles, info->typeIdU32, Ecb_DefaultConstructComponent);
	CHECK_FALSE(EcbTryParallelPlayback<true>(*scene, header, add.data(), nullptr, 0));
	CHECK_FALSE(scene->GetRegistry().any_of<Transform3DComponent>(handles.front()));
	JobSystemSpec spec; spec.WorkerCount = 4; JobSystem::Initialize(spec);
	REQUIRE(EcbTryParallelPlayback<true>(*scene, header, add.data(), nullptr, 2));
	for (auto handle : handles) {
		const auto& value = scene->GetRegistry().get<Transform3DComponent>(handle);
		CHECK(value.Position.x == 0); CHECK(value.Scale.x == 1); CHECK_FALSE(value.CachedLocalValid);
	}
	auto remove = TransformCommands(handles, info->typeIdU32, Ecb_RemoveComponent);
	REQUIRE(EcbTryParallelPlayback<true>(*scene, header, remove.data(), nullptr, 2));
	CHECK(scene->GetRegistry().storage<Transform3DComponent>().empty());
	// No partial commit when a late command introduces a hierarchy or duplicate target.
	scene->GetRegistry().emplace<HierarchyComponent>(handles.back());
	CHECK_FALSE(EcbTryParallelPlayback<true>(*scene, header, add.data(), nullptr, 2));
	CHECK(scene->GetRegistry().storage<Transform3DComponent>().empty());
	scene->GetRegistry().remove<HierarchyComponent>(handles.back());
	auto duplicate = add; std::memcpy(duplicate.data() + 19 + 11, duplicate.data() + 11, 8);
	CHECK_FALSE(EcbTryParallelPlayback<true>(*scene, header, duplicate.data(), nullptr, 2));
	auto& storage = scene->GetRegistry().storage<Transform3DComponent>();
	storage.on_construct().connect<&CustomTransformHook>();
	CHECK_FALSE(EcbTryParallelPlayback<true>(*scene, header, add.data(), nullptr, 2));
	storage.on_construct().disconnect<&CustomTransformHook>();
	Transform3DComponent authored; authored.Position = Vec3(2, 3, 4); authored.Rotation = Vec3(10, 20, 30); authored.Scale = Vec3(3, 4, 5);
	auto payload = TransformCommands({handles[1]}, info->typeIdU32, Ecb_AddComponent);
	payload.resize(19 + sizeof(authored)); const uint16_t payloadSize = 8 + sizeof(authored);
	std::memcpy(payload.data() + 9, &payloadSize, sizeof(payloadSize)); std::memcpy(payload.data() + 19, &authored, sizeof(authored));
	REQUIRE(EcbTryParallelPlayback<true>(*scene, EcbHeader{0, 1}, payload.data(), nullptr, 2));
	info->emplaceFromBytes(scene->GetRegistry(), handles[2], reinterpret_cast<const uint8_t*>(&authored), sizeof(authored));
	const auto& staged = storage.get(handles[1]); const auto& ordered = storage.get(handles[2]);
	CHECK((staged.Position == ordered.Position)); CHECK((staged.Rotation == ordered.Rotation)); CHECK((staged.Scale == ordered.Scale));
	// The public API actually completes the rejected mixed batch through ordered playback.
	NativeEntityCommandBuffer mixed;
	mixed.AddComponent<Transform3DComponent>(NativeEntityCommandBuffer::Existing(scene->GetEntity(handles.front())));
	mixed.Destroy(NativeEntityCommandBuffer::Existing(scene->GetEntity(handles.back())));
	CHECK_NOTHROW(mixed.Playback(*scene, EntityCommandBufferPlaybackMode::Parallel));
	CHECK(scene->GetRegistry().all_of<Transform3DComponent>(handles.front()));
	CHECK_FALSE(scene->IsValid(handles.back()));
	JobSystem::Shutdown();
}
TEST_CASE("Transform3D ECB total planning and storage cost comparison") {
	if (!std::getenv("FUNKE_ECB_3D_BENCH")) return;
	Application app; auto& components = SceneManager::GetComponentRegistry(); RegisterBuiltInComponents(components);
	const auto* info = components.GetInfo<Transform3DComponent>(); REQUIRE(info);
	JobSystemSpec spec; spec.WorkerCount = 4; JobSystem::Initialize(spec);
	for (int count : {512, 8192, 32768}) for (bool parallel : {false, true}) {
		std::vector<double> times;
		for (int iteration = 0; iteration < 9; ++iteration) {
			auto scene = Scene::CreateDetachedScene("3D workload"); std::vector<EntityHandle> handles;
			for (int i = 0; i < count; ++i) handles.push_back(scene->CreateEmptyEntity().GetHandle());
			NativeEntityCommandBuffer commands;
			for (auto handle : handles) commands.AddComponent<Transform3DComponent>(NativeEntityCommandBuffer::Existing(scene->GetEntity(handle)));
			const auto start = std::chrono::steady_clock::now();
			commands.Playback(*scene, parallel ? EntityCommandBufferPlaybackMode::Parallel : EntityCommandBufferPlaybackMode::Sequential);
			const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			if (iteration) times.push_back(elapsed);
		}
		std::sort(times.begin(), times.end());
		std::cout << "Transform3D count=" << count << " parallel=" << parallel << " median_ms=" << times[times.size()/2] << '\n';
	}
	JobSystem::Shutdown();
}
