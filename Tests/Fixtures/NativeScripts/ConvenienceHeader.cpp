#include <Funke/Scripting.hpp>

struct Input {};
static_assert(sizeof(Input) > 0);
#if FUNKE_WITH_APPLICATION
static_assert(sizeof(Funke::Input) > 0);
static_assert(sizeof(Funke::SceneManager) > 0);
static_assert(sizeof(Funke::RectTransform2DComponent) > 0);
#endif
#if FUNKE_WITH_SCRIPTING
static_assert(sizeof(Funke::NativeScript) > 0);
static_assert(sizeof(Funke::ScriptComponent) > 0);
struct HeaderValue { int Value = 0; };
struct HeaderTag {};
class HeaderScript : public Funke::NativeScript {};
void CompileConvenienceAccess(Funke::NativeScript& script, Funke::Entity& entity) {
    (void)script.GetEntity();
    (void)script.TryGetComponent<HeaderValue>();
    (void)std::as_const(script).TryGetComponent<HeaderValue>();
    (void)entity.TryGetComponent<HeaderValue>();
    (void)std::as_const(entity).TryGetComponent<HeaderValue>();
    (void)script.GetOrAddComponent<HeaderValue>();
    entity.GetOrAddComponent<HeaderTag>();
    (void)script.GetOrAddComponent<HeaderScript>();
    (void)entity.GetOrAddComponent<HeaderScript>();
    script.RemoveComponent<HeaderScript>();
}
#endif
#if FUNKE_WITH_RENDER
static_assert(sizeof(Funke::ButtonComponent) > 0);
static_assert(sizeof(Funke::PostProcessing2DComponent) > 0);
#if FUNKE_WITH_2D
static_assert(sizeof(Funke::SpriteRendererComponent) > 0);
#endif
#if FUNKE_WITH_3D
static_assert(sizeof(Funke::SkinnedMeshRendererComponent) > 0);
#endif
#endif
#if FUNKE_WITH_PHYSICS2D
static_assert(sizeof(Funke::Rigidbody2DComponent) > 0);
#endif
#if FUNKE_WITH_PHYSICS3D
static_assert(sizeof(Funke::Rigidbody3DComponent) > 0);
#endif
#if FUNKE_WITH_AUDIO
static_assert(sizeof(Funke::AudioSourceComponent) > 0);
#endif
