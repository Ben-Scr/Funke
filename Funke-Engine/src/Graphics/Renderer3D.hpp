#pragma once
#include "Core/Export.hpp"
#include "Core/GraphicsSettings.hpp"
#include "Collections/Color.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Graphics/PosColorVertex.hpp"
#include "Graphics/RenderStatistics.hpp"

#include <glm/glm.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Funke {
	class Scene;
	class Shader;
	struct PostProcessing2DComponent;

	// Background and sky-ambient settings of the camera a view renders through.
	struct FUNKE_API Camera3DEnvironment {
		CameraBackground Background = CameraBackground::Skybox;
		Color ClearColor = Camera3DComponent{}.ClearColor;
		Color SkyTop = Camera3DComponent{}.SkyTopColor;
		Color SkyHorizon = Camera3DComponent{}.SkyHorizonColor;
		Color SkyGround = Camera3DComponent{}.SkyGroundColor;
		float SunSize = Camera3DComponent{}.SunSize;
		float SkyAmbient = Camera3DComponent{}.SkyAmbient;
		uint64_t SkyboxMaterial = 0;
		bool FogEnabled = false;
		Color FogColor = Camera3DComponent{}.FogColor;
		float FogStart = Camera3DComponent{}.FogStart;
		float FogEnd = Camera3DComponent{}.FogEnd;

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
	class FUNKE_API Renderer3D {
	public:
		using SceneProvider = std::function<void(const std::function<void(Scene&)>&)>;

		Renderer3D();
		~Renderer3D();
		Renderer3D(const Renderer3D&) = delete;
		Renderer3D& operator=(const Renderer3D&) = delete;

		bool Initialize();
		void Shutdown();
		bool IsInitialized() const { return m_IsInitialized; }
		// Call after frame submission, when no render pass still reads cached mesh pointers.
		size_t PurgeUnusedMeshes(std::chrono::milliseconds maxIdle = std::chrono::seconds(2));

		// Rebuilds the material samplers when the texture quality or anisotropy changed.
		void SetGraphicsSettings(const GraphicsSettings& settings);
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
		// Meshes, then particles. Under RenderApi's PolygonMode::Wireframe: black mesh edges only (editor Triangle/Mixed).
		void RenderScene(Scene& scene, const Camera3DView& view);

		// Editor View toggle (also gates anti-aliasing); the Game View and the runtime keep it on.
		void SetPostProcessingEnabled(bool enabled) { m_PostProcessingEnabled = enabled; }
		// With an effect enabled in `settings` (the camera's Post Processing) or anti-aliasing on, redirects drawing
		// into an intermediate target cleared to clearColor and returns true; EndPostProcess then runs anti-aliasing
		// and the effects into the original target. False = draw straight to the target and skip EndPostProcess.
		// MSAA4x draws into a 4x target that EndPostProcess resolves (straight into the original target without effects).
		bool BeginPostProcess(const PostProcessing2DComponent* settings, const Color& clearColor);
		void EndPostProcess();

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
		// Running totals since startup, not reset per frame (see CaptureRenderGeometryTotals).
		const RenderGeometryStats& GetGeometryTotals() const { return m_GeometryTotals; }

		static Camera3DView MakeView(const glm::mat4& cameraWorld, const Camera3DComponent& camera, float aspect);
		// Aspect of the viewport passes are clipped to (letterbox-aware), falling back to the bound target.
		static float GetCurrentTargetAspect();
		static uint32_t GetShadowMapResolution(ShadowQualityLevel quality);

	private:
		struct GpuState;
		struct PostProcessState;

		void RenderMeshes(Scene& scene, const Camera3DView& view);
		void RenderParticles(Scene& scene, const Camera3DView& view);

		std::unique_ptr<GpuState> m_Gpu;
		std::unique_ptr<PostProcessState> m_PostProcess;
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
		RenderGeometryStats m_GeometryTotals;
		bool m_IsInitialized = false;
		bool m_SkipBeginFrameRender = false;
		bool m_LinesSeeThrough = false;
		bool m_RenderedThisFrame = false;
		bool m_OwnDepthNeedsClear = true;
		bool m_PostProcessingEnabled = true;
	};

}
