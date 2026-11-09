#pragma once

#include "Core/Export.hpp"

#if FUNKE_WITH_APPLICATION
#include "Components/General/General.hpp"
#include "Components/General/AnimatorComponent.hpp"
#include "Components/General/ComponentOrderComponent.hpp"
#include "Components/Tags.hpp"
#if FUNKE_WITH_3D
#include "Components/General/Transform3DComponent.hpp"
#endif
#endif

#if FUNKE_WITH_RENDER
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/Graphics/PostProcessing2DComponent.hpp"
#include "Components/UI/UI.hpp"
#if FUNKE_WITH_2D
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Graphics/TerrainRenderer2DComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#endif
#if FUNKE_WITH_3D
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Components/Graphics/SkinnedMeshRendererComponent.hpp"
#include "Components/Graphics/DirectionalLightComponent.hpp"
#include "Components/Graphics/PointLightComponent.hpp"
#include "Components/Graphics/SpotLightComponent.hpp"
#include "Components/Graphics/ParticleSystem3DComponent.hpp"
#endif
#endif

#if FUNKE_WITH_PHYSICS2D
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Physics/Joint2DComponents.hpp"
#endif

#if FUNKE_WITH_PHYSICS3D
#include "Components/Physics3D/Rigidbody3DComponent.hpp"
#include "Components/Physics3D/Collider3DComponents.hpp"
#endif

#if FUNKE_WITH_AUDIO
#include "Components/Audio/AudioSourceComponent.hpp"
#endif

#if FUNKE_WITH_SCRIPTING
#include "Scripting/ScriptComponent.hpp"
#endif
