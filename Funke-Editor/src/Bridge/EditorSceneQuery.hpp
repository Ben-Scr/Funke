#pragma once
#include "Serialization/Json.hpp"

#include <map>
#include <chrono>
#include <memory>
#include <string>

namespace Funke {
	class Scene;
	class ImGuiEditorLayer;

	class EditorSceneQuery {
	public:
		EditorSceneQuery();
		void Refresh();
		Json::Value Execute(std::string_view command, const Json::Value& args);
		Json::Value Apply(const Json::Value& args, ImGuiEditorLayer& editor);

	private:
		struct Instance {
			std::string Id;
			std::string Generation;
			uint64_t ContentGeneration = 0;
		};
		Json::Value Describe(Scene& scene, const Instance& instance) const;
		std::string SchemaRevision() const;
		struct RequestResult {
			std::string Payload;
			Json::Value Result;
			std::chrono::steady_clock::time_point Completed;
		};
		std::map<std::string, RequestResult> m_Requests;
		size_t m_RequestBytes = 0;
		std::string m_SessionId;
		uint64_t m_NextId = 1;
		bool m_Playing = false;
		std::map<std::weak_ptr<Scene>, Instance, std::owner_less<std::weak_ptr<Scene>>> m_Instances;
	};
}
