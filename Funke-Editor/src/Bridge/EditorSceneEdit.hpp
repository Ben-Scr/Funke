#pragma once
#include "Serialization/Json.hpp"
#include <functional>

namespace Funke {
	class Scene;
	class UndoStack;
	namespace EditorSceneEdit {
		Json::Value Apply(Scene& scene, UndoStack& history, const Json::Value& request, const std::function<void()>& beforeApply = {});
	}
}
