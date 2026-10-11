#pragma once

#include "Index/Core.hpp"

#include "Events/Events.hpp"
#include "Math/Common.hpp"
#include "Utils/StringHelper.hpp"

#if INDEX_WITH_APPLICATION
#include "Index/App.hpp"

#include "Core/Input.hpp"
#include "Core/KeyCodes.hpp"
#include "Core/Layer.hpp"
#include "Core/MouseButton.hpp"
#include "Core/Window.hpp"

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

#if INDEX_WITH_RENDER
#if INDEX_WITH_APPLICATION
#include "Graphics/RenderSettings.hpp"
#endif
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Graphics/SpriteAnimator2DComponent.hpp"
#include "Components/Graphics/PostProcessing2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/UI/UI.hpp"

#include "Graphics/DefaultTexture.hpp"
#include "Graphics/Gizmo.hpp"
#include "Graphics/Instance44.hpp"
#include "Graphics/Renderer2D.hpp"
#include "Graphics/Texture2D.hpp"
#include "Graphics/TextureHandle.hpp"
#include "Graphics/TextureManager.hpp"
#endif

#if INDEX_WITH_AUDIO
#include "Audio/Audio.hpp"
#include "Audio/AudioHandle.hpp"
#include "Audio/AudioManager.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#endif

#if INDEX_WITH_PHYSICS
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/FastBody2DComponent.hpp"
#include "Components/Physics/FastBoxCollider2DComponent.hpp"
#include "Components/Physics/FastCircleCollider2DComponent.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"

#include "Physics/Collision2D.hpp"
#include "Physics/Physics2D.hpp"
#include "Physics/PhysicsUtility.hpp"
#endif

#if INDEX_WITH_SCRIPTING
#include "Scripting/NativeScript.hpp"
#endif
