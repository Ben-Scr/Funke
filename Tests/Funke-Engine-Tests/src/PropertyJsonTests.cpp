#include <doctest/doctest.h>
#include "Inspector/PropertyJson.hpp"
#include "Scene/Scene.hpp"

#include <limits>

using namespace Funke;

TEST_CASE("Property JSON preserves 64 bit identities and reference qualifiers") {
	for (auto type : { PropertyType::UInt64, PropertyType::TextureRef, PropertyType::EntityRef, PropertyType::ComponentRef }) {
		PropertyValue original;
		original.Type = type;
		original.UIntValue = UINT64_MAX;
		if (type == PropertyType::TextureRef) original.StringValue = "coin";
		if (type == PropertyType::EntityRef) original.StringValue = "prefab";
		if (type == PropertyType::ComponentRef) original.StringValue = "Transform2D";
		Json::Value wire, parsed;
		std::string error;
		REQUIRE(PropertyJson::Encode(original, wire, error));
		REQUIRE(Json::TryParse(Json::Stringify(wire), parsed, &error));
		PropertyValue restored;
		REQUIRE(PropertyJson::Decode(type, parsed, restored, error));
		CHECK(restored == original);
		CHECK(Json::Stringify(wire).find("\"18446744073709551615\"") != std::string::npos);
	}
}

TEST_CASE("Property JSON rejects lossy types overflow and nonfinite values") {
	PropertyValue out;
	std::string error;
	CHECK_FALSE(PropertyJson::Decode(PropertyType::Int8, Json::Value(128), out, error));
	CHECK_FALSE(PropertyJson::Decode(PropertyType::UInt32, Json::Value(-1), out, error));
	CHECK_FALSE(PropertyJson::Decode(PropertyType::Int32, Json::Value(1.5), out, error));
	CHECK_FALSE(PropertyJson::Decode(PropertyType::Int64, Json::Value(9007199254740992.0), out, error));
	CHECK_FALSE(PropertyJson::Decode(PropertyType::UInt64, Json::Value("18446744073709551616"), out, error));
	CHECK_FALSE(PropertyJson::Decode(PropertyType::UInt64, Json::Value("-1"), out, error));
	CHECK_FALSE(PropertyJson::Decode(PropertyType::Bool, Json::Value("true"), out, error));
	CHECK(PropertyJson::Decode(PropertyType::Int64, Json::Value("-9223372036854775808"), out, error));
	CHECK(out.IntValue == INT64_MIN);
	CHECK_FALSE(PropertyJson::Decode(PropertyType::Float, Json::Value(std::numeric_limits<double>::max()), out, error));
	out.Type = PropertyType::Vec2;
	out.FloatVec[1] = std::numeric_limits<float>::infinity();
	Json::Value encoded;
	CHECK_FALSE(PropertyJson::Encode(out, encoded, error));
	CHECK(error == "non_finite_value");
	out.Type = PropertyType::Graph;
	CHECK_FALSE(PropertyJson::Encode(out, encoded, error));
	CHECK(error == "unsupported_field_type");
}

TEST_CASE("Scene revisions advance for repeated dirty edits and never reset on save") {
	auto scene = Scene::CreateDetachedScene("Revision test");
	const auto initial = scene->GetRevision();
	auto entity = scene->CreateEntity("first");
	CHECK(scene->GetRevision() > initial);
	const auto created = scene->GetRevision();
	scene->MarkDirty();
	CHECK(scene->GetRevision() > created);
	const auto changed = scene->GetRevision();
	scene->ClearDirty();
	CHECK(scene->GetRevision() == changed);
	scene->DestroyEntity(entity);
	CHECK(scene->GetRevision() > changed);
}
