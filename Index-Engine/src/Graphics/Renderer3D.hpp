#pragma once
#include "Core/Export.hpp"
#include "Core/GraphicsSettings.hpp"
#include "Collections/Color.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Graphics/PosColorVertex.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Index {
	class Scene;
	class Shader;

	// Background and sky-ambient settings of the camera a view renders through.
	struct INDEX_API Camera3DEnvironment {
		CameraBackground Background = CameraBackground::Skybox;
		Color ClearColor = Camera3DComponent{}.ClearColor;
		Color SkyTop = Camera3DComponent{}.SkyTopColor;
		Color SkyHorizon = Camera3DComponent{}.SkyHorizonColor;
		Color SkyGround = Camera3DComponent{}.SkyGroundColor;
		float SunSize = Camera3DComponent{}.SunSize;
		float SkyAmbient = Camera3DComponent{}.SkyAmbient;

		static Camera3DEnvironment FromCamera(const Camera3DComponent& camera);
	};

	struct Camera3DView {
		glm::mat4 View{ 1.0f };
		glm::mat4 Projection{ 1.0f };
		glm::vec3 Position{ 0.0f };
		Camera3DEnvironment Environment;
	};

	// Forward renderer for MeshRendererComponent: procedural sky, up to GraphicsSettings::MaxLights directional/point/spot
	// lights, GGX shading with .material assets, one shadowed directional light. Draws are instanced per (mesh, texture).
	// ParticleSystem3D billboards follow the meshes in their own unlit, depth-tested pass.
	// Draws into whatever target RenderApi has bound; the swap chain gets a renderer-owned depth buffer.
	class INDEX_API Renderer3D {
	public:
		using SceneProvider = std::function<void(const std::function<void(Scene&)>&)>;

		Renderer3D();
		~Renderer3D();
		Renderer3D(const Renderer3D&) = delete;
		Renderer3D& operator=(const Renderer3D&) = delete;

		bool Initialize();
		void Shutdown();
		bool IsInitialized() const { return m_IsInitialized; }

		void SetGraphicsSettings(const GraphicsSettings& settings) { m_Settings = ClampGraphicsSettings(settings); }
		const GraphicsSettings& GetGraphicsSettings() const { return m_Settings; }

		// Runtime path: clears the swap chain to the main Camera3D's background and draws every loaded scene through it.
		// Returns false (and touches nothing) when no loaded scene has a Camera3D.
		bool BeginFrame();
		bool HasRenderedThisFrame() const { return m_RenderedThisFrame; }
		void SetSceneProvider(SceneProvider provider) { m_SceneProvider = std::move(provider); }
		// The editor renders its own views from OnPreRender.
		void SetSkipBeginFrameRender(bool skip) { m_SkipBeginFrameRender = skip; }

		// Fills the bound target with the view's procedural sky when its background is Skybox; the sun disc follows
		// the main DirectionalLight of lightScene. Call after the clear and before RenderScene.
		void RenderSky(const Camera3DView& view, Scene* lightScene);
		// Meshes, then particles.
		void RenderScene(Scene& scene, const Camera3DView& view);

		// Queued world-space lines, drawn and cleared by FlushLines (editor grid, selection bounds).
		void DrawLine(const glm::vec3& a, const glm::vec3& b, const Color& color);
		void DrawWireBox(const glm::mat4& transform, const glm::vec3& halfExtents, const Color& color);
		void DrawWireCircle(const glm::vec3& center, const glm::vec3& axisA, const glm::vec3& axisB, float radius, const Color& color, int segments = 48);
		void DrawGrid(const glm::vec3& center, int halfLineCount, float spacing, const Color& color);
		// Lines queued while on stay visible behind meshes: full color where visible, faded where hidden (collider gizmos).
		void SetLinesSeeThrough(bool seeThrough) { m_LinesSeeThrough = seeThrough; }
		void FlushLines(const Camera3DView& view, bool depthTested);

		uint32_t GetDrawCallsCount() const { return m_DrawCalls; }
		uint32_t GetRenderedMeshCount() const { return m_RenderedMeshes; }
		uint32_t GetLightCount() const { return m_LightCount; }
		uint32_t GetRenderedParticleCount() const { return m_RenderedParticles; }

		static Camera3DView MakeView(const glm::mat4& cameraWorld, const Camera3DComponent& camera, float aspect);
		// Aspect of the viewport passes are clipped to (letterbox-aware), falling back to the bound target.
		static float GetCurrentTargetAspect();
		static uint32_t GetShadowMapResolution(ShadowQualityLevel quality);

	private:
		struct GpuState;

		void RenderMeshes(Scene& scene, const Camera3DView& view);
		void RenderParticles(Scene& scene, const Camera3DView& view);

		std::unique_ptr<GpuState> m_Gpu;
		std::unique_ptr<Shader> m_MeshShader;
		std::unique_ptr<Shader> m_SkyShader;
		std::unique_ptr<Shader> m_ShadowShader;
		std::unique_ptr<Shader> m_LineShader;
		// Optional: without it particles are skipped but meshes still draw.
		std::unique_ptr<Shader> m_ParticleShader;
		SceneProvider m_SceneProvider;
		std::vector<PosColorVertex> m_Lines;
		std::vector<PosColorVertex> m_SeeThroughLines;
		std::vector<PosColorVertex> m_LineUpload;
		GraphicsSettings m_Settings;

		uint32_t m_DrawCalls = 0;
		uint32_t m_RenderedMeshes = 0;
		uint32_t m_LightCount = 0;
		uint32_t m_RenderedParticles = 0;
		bool m_IsInitialized = false;
		bool m_SkipBeginFrameRender = false;
		bool m_LinesSeeThrough = false;
		bool m_RenderedThisFrame = false;
		bool m_OwnDepthNeedsClear = true;
	};

}
