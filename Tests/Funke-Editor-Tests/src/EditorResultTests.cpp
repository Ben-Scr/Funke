#include <doctest/doctest.h>
#include "../../../Funke-CLI/src/Commands/EditorResult.hpp"

using namespace Funke;

namespace {
	int Evaluate(std::string_view command, std::string_view json) {
		Json::Value result;
		std::string error;
		REQUIRE(Json::TryParse(json, result, &error));
		return Cli::EvaluateEditorResult(command, result);
	}
}

TEST_CASE("Editor results distinguish command failure from transport success") {
	CHECK(Evaluate("exec", R"({"ok":false,"status":"not-found"})") == 1);
	CHECK(Evaluate("exec", R"({"ok":true})") == 0);
	CHECK(Evaluate("exec", R"({"ok":"true"})") == 1);
	CHECK(Evaluate("exec", R"({})") == 1);
	CHECK(Evaluate("recompile", R"({"succeeded":false})") == 1);
	CHECK(Evaluate("recompile", R"({"succeeded":true})") == 0);
	CHECK(Evaluate("recompile", R"({"started":true,"completion":"accepted"})") == 0);
	CHECK(Evaluate("recompile", R"({})") == 1);
	CHECK(Evaluate("status", R"({"scripts":{"lastBuildSucceeded":false}})") == 0);
}

TEST_CASE("Editor input results aggregate command and screenshot failures") {
	CHECK(Evaluate("input", R"({"commands":[],"screenshots":[]})") == 0);
	CHECK(Evaluate("input", R"({"commands":[{"ok":true},{"ok":false}],"screenshots":[]})") == 1);
	CHECK(Evaluate("input", R"({"commands":[{}],"screenshots":[]})") == 1);
	CHECK(Evaluate("input", R"({"commands":[],"screenshots":[{"error":"cannot write"}]})") == 1);
	CHECK(Evaluate("input", R"({"commands":[{"ok":true}],"screenshots":[{"path":"a.png","error":""}]})") == 0);
	CHECK(Evaluate("input", R"({"commands":{},"screenshots":[]})") == 1);
	CHECK(Evaluate("input", R"({})") == 1);
}
