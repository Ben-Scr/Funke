#include <doctest/doctest.h>
#include "Editor/AnimationPreview.hpp"
#include "Animation/AnimationClip.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scripting/StructFieldBytes.hpp"

using namespace Funke;

TEST_CASE("Dynamic fields appear in track selection and preview survives removal and schema reload") {
    auto scene = Scene::CreateDetachedScene("Dynamic preview");
    auto entity = scene->CreateEmptyEntity();
    auto& registry = SceneManager::GetComponentRegistryOrBuiltIn();
    const auto id = registry.RegisterDynamic("Preview", "type:Preview", "Test", ComponentCategory::Component, 4, 4);
    REQUIRE(id != 0);
    constexpr uint64_t clipId = 98712365;
    struct Cleanup {
        ComponentRegistry& Registry;
        uint64_t Clip;
        ~Cleanup() { AnimationClipLibrary::Invalidate(Clip); Registry.UnregisterAllDynamic(); }
    } cleanup{registry, clipId};
    registry.SetDynamicProperties(id, BuildDynamicFieldProperties(id, 4,
        R"([{"name":"Nested.x","type":"float","offset":0,"size":4},{"name":"ReadOnly","type":"float","offset":0,"size":4,"readOnly":true},{"name":"OutOfBounds","type":"float","offset":4,"size":4}])"));
    const auto* info = registry.GetByTypeId(id);
    REQUIRE(info->properties.size() == 1);
    info->add(entity);
    REQUIRE(WriteStructField(static_cast<uint8_t*>(info->getRaw(entity)), 4, {"float", 0, 4}, "7"));
    auto choices = CollectAnimatableProperties(*scene, entity.GetHandle());
    REQUIRE(choices.size() == 1);
    CHECK(choices[0].Descriptor->Name == "Nested.x");
    AnimationClip clip;
    AnimationTrack track;
    track.Component = "type:Preview";
    track.Property = "Nested.x";
    track.Curve.SetKey(0, 42);
    clip.Tracks.push_back(track);
    AnimationClipLibrary::Set(clipId, clip);
    AnimationPreview preview;
    preview.SetRecording(true);
    preview.SetClipPose(scene.get(), entity.GetHandle(), clipId, 0);
    preview.BeginFrame(scene.get());
    REQUIRE(preview.GetClipErrors().empty());
    CHECK(ReadStructField(static_cast<uint8_t*>(info->getRaw(entity)), 4, {"float", 0, 4}).value() == "42");
    SUBCASE("End of frame restores authored ECS data") {
        preview.EndFrame();
        CHECK(ReadStructField(static_cast<uint8_t*>(info->getRaw(entity)), 4, {"float", 0, 4}).value() == "7");
    }
    SUBCASE("Component removed during recording") {
        info->remove(entity);
        CHECK_NOTHROW(preview.EndFrame());
    }
    SUBCASE("Entity removed during recording") {
        scene->DestroyEntity(entity);
        CHECK_NOTHROW(preview.EndFrame());
    }
    SUBCASE("Registry reload during recording") {
        registry.UnregisterAllDynamic();
        CHECK_NOTHROW(preview.EndFrame());
        CHECK(preview.TakeRecordedChanges().empty());
    }
}
