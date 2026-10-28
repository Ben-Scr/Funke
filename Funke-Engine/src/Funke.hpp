#pragma once

#include "Funke/Core.hpp"

#include "Events/Events.hpp"
#include "Math/Common.hpp"
#include "Utils/StringHelper.hpp"

#if FUNKE_WITH_APPLICATION
#include "Funke/App.hpp"

#include "Core/Input.hpp"
#include "Core/KeyCodes.hpp"
#include "Core/Layer.hpp"
#include "Core/MouseButton.hpp"
#include "Core/Window.hpp"

#include "Components/General/AnimatorComponent.hpp"
#include "Components/General/General.hpp"
#include "Components/Tags.hpp"

#include "Scene/Entity.hpp"
#include "Scene/EntityHelper.hpp"
#include "Scene/EntityHandle.hpp"
#include "Scene/SceneSystem.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneManager.hpp"
#endif

#if FUNKE_WITH_RENDER
#if FUNKE_WITH_APPLICATION
#include "Graphics/RenderSettings.hpp"
#endif
#include "Components/Graphics/ImageComponent.hpp"
#if FUNKE_WITH_2D
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Graphics/TerrainRenderer2DComponent.hpp"
#include "Components/Graphics/PostProcessing2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#endif
#if FUNKE_WITH_3D
#include "Components/Graphics/ParticleSystem3DComponent.hpp"
#endif
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/UI/UI.hpp"

#include "Graphics/DefaultTexture.hpp"
#include "Graphics/Gizmo.hpp"
#include "Graphics/Instance44.hpp"
#if FUNKE_WITH_2D
#include "Graphics/Renderer2D.hpp"
#endif
#include "Graphics/Texture2D.hpp"
#include "Graphics/TextureHandle.hpp"
#include "Graphics/TextureManager.hpp"
#endif

#if FUNKE_WITH_AUDIO
#include "Audio/Audio.hpp"
#include "Audio/AudioHandle.hpp"
#include "Audio/AudioManager.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#endif

#if FUNKE_WITH_PHYSICS && FUNKE_WITH_2D
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#include "Components/Physics/Joint2DComponents.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"

#include "Physics/Collision2D.hpp"
#include "Physics/Physics2D.hpp"
#include "Physics/PhysicsUtility.hpp"
#endif

#if FUNKE_WITH_SCRIPTING
#include "Scripting/NativeScript.hpp"
#endif
