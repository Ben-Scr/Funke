#pragma once
#include "Core/Export.hpp"
#include "Graphics/SpriteShaderProgram.hpp"
#include "Graphics/QuadMesh.hpp"
#include "Graphics/Instance44.hpp"
#include "Graphics/Text/TextRenderer.hpp"
#include "Gui/UICanvasDrawOrder.hpp"
#include "Scene/EntityHandle.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Funke {
	class Scene;
	class SceneManager;

	class FUNKE_API GuiRenderer {
	public:
		GuiRenderer() = default;
		void Initialize();
		void Shutdown();

		void BeginFrame();
		void EndFrame();

		void RenderScene(const Scene& scene, bool renderWorld = true);
		void RenderScene(const Scene& scene, const glm::mat4& worldVP, float pixelToWorldScale, bool renderWorld = true);
		void RenderWorldScene(const Scene& scene, const glm::mat4& vp);
		static float ComputeWorldUIPixelScale();
		void SetSkipBeginFrameRender(bool skip) { m_SkipBeginFrameRender = skip; }

	private:
		void CollectAndDraw(const Scene& scene, const glm::mat4& mvp,
			float halfW, float halfH, EntityHandle worldRoot,
			const std::vector<std::pair<EntityHandle, int>>& drawOrder);
		void DrawCanvases(const Scene& scene, const glm::mat4& screenMVP,
			const glm::mat4* worldVP, float halfW, float halfH);

		SpriteShaderProgram m_SpriteShader;
		QuadMesh m_QuadMesh;

		// Owned text renderer so UI can render labels in screen-space
		// without going through Renderer2D's world-space text pass.
		std::unique_ptr<TextRenderer> m_TextRenderer;

		// Reused across frames so RenderScene doesn't heap-allocate per frame.
		std::vector<Instance44> m_InstancesScratch;
		std::vector<TextDrawCmd> m_TextScratch;
		UIDrawOrder::CanvasOrders m_CanvasOrders;

		bool m_IsInitialized = false;
		bool m_SkipBeginFrameRender = false;
	};
}
