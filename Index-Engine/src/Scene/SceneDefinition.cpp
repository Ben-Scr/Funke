#include "pch.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/Scene.hpp"
#include "Core/Exceptions.hpp"


namespace Index {
	std::shared_ptr<Scene> SceneDefinition::Instantiate() const {
		std::shared_ptr<Scene> scene(new Scene(m_Name, this, m_IsPersistent));
		const auto systemFactories = m_SystemFactories;
		const auto initializeCallbacks = m_InitializeCallbacks;

		for (const auto& factory : systemFactories) {
			auto system = factory();

			if (system) {
				scene->m_Systems.push_back(std::move(system));
			}
			else {
				IDX_THROW(IndexErrorCode::LoadFailed, "Failed creating system for scene with name '" +
					m_Name + "'");
			}
		}

		for (const auto& callback : initializeCallbacks) {
			try {
				callback(*scene);
			}
			catch (const std::exception& e) {
				IDX_LOG_ERROR(IndexErrorCode::Undefined, "Exception in initialize callback for scene with name '" +
					m_Name + "': " + e.what());
				throw;
			}
			catch (...) {
				IDX_LOG_ERROR(IndexErrorCode::Undefined, "Unknown Exception in initialize callback for scene with name '" +
					m_Name + "'");
				throw;
			}
		}

		return scene;
	}
}
