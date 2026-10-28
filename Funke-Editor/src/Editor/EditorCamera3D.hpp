#pragma once
#include "Editor/EditorCamera3DSettings.hpp"
#include "Graphics/Renderer3D.hpp"

#include <glm/glm.hpp>

namespace Funke {

	class EditorCamera3D {
	public:
		struct Input {
			glm::vec2 MouseDelta{ 0.0f };
			float Scroll = 0.0f;
			bool Look = false;
			bool Pan = false;
			// x = right, y = up, z = forward; settings select local or world up.
			glm::vec3 Fly{ 0.0f };
			bool Fast = false;
		};

		void SetSettings(const EditorCamera3DSettings& settings);
		void Update(float deltaTime, const Input& input);
		void Focus(const glm::vec3& target, float radius);

		Camera3DView GetView(float aspect) const;
		glm::mat4 GetWorldMatrix() const;
		glm::vec3 GetPosition() const;
		glm::vec3 GetFocus() const { return m_Focus; }
		float GetDistance() const { return m_Distance; }

	private:
		glm::vec3 m_Focus{ 0.0f, 0.5f, 0.0f };
		float m_YawDegrees = -30.0f;
		float m_PitchDegrees = -25.0f;
		float m_Distance = 10.0f;
		EditorCamera3DSettings m_Settings;
	};

}
