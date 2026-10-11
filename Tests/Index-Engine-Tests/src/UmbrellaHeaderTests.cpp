#include <Index.hpp>

#include <type_traits>

namespace {

class UmbrellaSystem final : public Index::SceneSystem {
public:
	void Update(Index::Scene&) override {}
};

class UmbrellaScript final : public Index::NativeScript {};

static_assert(std::is_base_of_v<Index::SceneSystem, UmbrellaSystem>);
static_assert(std::is_base_of_v<Index::NativeScript, UmbrellaScript>);
static_assert(std::is_same_v<decltype(Index::Transform2DComponent{}.Up()), Index::Vec2>);
static_assert(std::is_default_constructible_v<Index::SpriteRendererComponent>);
static_assert(std::is_default_constructible_v<Index::Camera2DComponent>);
static_assert(std::is_default_constructible_v<Index::ParticleSystem2DComponent>);
static_assert(std::is_default_constructible_v<Index::AudioSourceComponent>);
static_assert(std::is_default_constructible_v<Index::Rigidbody2DComponent>);
static_assert(std::is_default_constructible_v<Index::ButtonComponent>);
static_assert(std::is_same_v<decltype(Index::Input::GetKey(Index::KeyCode::A)), bool>);
static_assert(std::is_same_v<decltype(Index::Input::GetMousePosition()), Index::Vec2>);
static_assert(!std::is_member_function_pointer_v<decltype(&Index::Input::GetKey)>);
static_assert(std::is_same_v<decltype(Index::Time::GetDeltaTime()), float>);
static_assert(!std::is_member_function_pointer_v<decltype(&Index::Time::GetDeltaTime)>);

}
