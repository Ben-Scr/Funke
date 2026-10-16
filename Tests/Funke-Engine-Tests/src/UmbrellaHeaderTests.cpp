#include <Funke.hpp>

#include <type_traits>

namespace {

class UmbrellaSystem final : public Funke::SceneSystem {
public:
	void Update(Funke::Scene&) override {}
};

class UmbrellaScript final : public Funke::NativeScript {};

static_assert(std::is_base_of_v<Funke::SceneSystem, UmbrellaSystem>);
static_assert(std::is_base_of_v<Funke::NativeScript, UmbrellaScript>);
static_assert(std::is_same_v<decltype(Funke::Transform2DComponent{}.Up()), Funke::Vec2>);
static_assert(std::is_default_constructible_v<Funke::SpriteRendererComponent>);
static_assert(std::is_default_constructible_v<Funke::AnimatorComponent>);
static_assert(std::is_default_constructible_v<Funke::Camera2DComponent>);
static_assert(std::is_default_constructible_v<Funke::ParticleSystem2DComponent>);
static_assert(std::is_default_constructible_v<Funke::ParticleSystem3DComponent>);
static_assert(std::is_default_constructible_v<Funke::AudioSourceComponent>);
static_assert(std::is_default_constructible_v<Funke::Rigidbody2DComponent>);
static_assert(std::is_default_constructible_v<Funke::ButtonComponent>);
static_assert(std::is_default_constructible_v<Funke::LocalizedTextComponent>);
static_assert(std::is_same_v<decltype(Funke::Input::GetKey(Funke::KeyCode::A)), bool>);
static_assert(std::is_same_v<decltype(Funke::Input::GetMousePosition()), Funke::Vec2>);
static_assert(!std::is_member_function_pointer_v<decltype(&Funke::Input::GetKey)>);
static_assert(std::is_same_v<decltype(Funke::Time::GetDeltaTime()), float>);
static_assert(!std::is_member_function_pointer_v<decltype(&Funke::Time::GetDeltaTime)>);

}
