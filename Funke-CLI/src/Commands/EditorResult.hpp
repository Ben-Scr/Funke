#pragma once
#include "../Cli.hpp"
#include "Serialization/Json.hpp"

namespace Funke::Cli {

	inline int EvaluateEditorResult(std::string_view command, const Json::Value& result) {
		auto isTrue = [](const Json::Value& object, std::string_view key) {
			const Json::Value* value = object.FindMember(key);
			return value && value->IsBool() && value->AsBoolOr(false);
		};
		if (command == "exec") return isTrue(result, "ok") ? k_ExitOk : k_ExitFailure;
		if (command == "scenes" || command == "entities" || command == "inspect" || command == "schema" || command == "apply" || command == "apply-status") {
			return isTrue(result, "ok") ? k_ExitOk : k_ExitFailure;
		}
		if (command == "recompile") {
			return isTrue(result, "started") || isTrue(result, "succeeded") ? k_ExitOk : k_ExitFailure;
		}
		if (command == "input") {
			const Json::Value* commands = result.FindMember("commands");
			const Json::Value* screenshots = result.FindMember("screenshots");
			if (!commands || !commands->IsArray() || !screenshots || !screenshots->IsArray()) return k_ExitFailure;
			for (const Json::Value& entry : commands->GetArray()) {
				if (!isTrue(entry, "ok")) return k_ExitFailure;
			}
			for (const Json::Value& entry : screenshots->GetArray()) {
				const Json::Value* error = entry.FindMember("error");
				if (error && (!error->IsString() || !error->AsStringOr().empty())) return k_ExitFailure;
			}
		}
		return k_ExitOk;
	}

}
