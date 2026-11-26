#pragma once
#include "Editor/EditorCamera3D.hpp"
#include "Graphics/Framebuffer.hpp"
#include "Scene/EntityHandle.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace Funke {
	class Scene;
	struct MeshAsset;

	class AssetPreview3D {
	public:
		AssetPreview3D();
		~AssetPreview3D();
		void Open(const std::string& path);
		void Close();
		void Render();
		bool IsOpen() const { return !m_Path.empty(); }
		bool IsFocused() const { return IsOpen() && m_Focused; }

	private:
		void EnsureScene();
		bool UpdateModel(const MeshAsset& asset);
		void Frame(float aspect);
		void DrawPreview(float width, float height);

		std::string m_Path;
		std::string m_Error;
		std::unique_ptr<Scene> m_Scene;
		Framebuffer m_Target;
		EditorCamera3D m_Camera;
		EntityHandle m_Entity = entt::null;
		uint64_t m_Asset = 0;
		uint64_t m_Load = 0;
		uint32_t m_Revision = 0;
		float m_Radius = 1.0f;
		bool m_IsMaterial = false;
		bool m_FramePending = false;
		bool m_FocusPending = false;
		bool m_Focused = false;
		bool m_Look = false;
		bool m_Pan = false;
	};
}
