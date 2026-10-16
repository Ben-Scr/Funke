#include "pch.hpp"
#include "Graphics/SkyAmbient.hpp"

#include "Assets/AssetRegistry.hpp"
#include "Graphics/CustomShaderLibrary.hpp"
#include "Graphics/ImageProcessing.hpp"
#include "Graphics/Material.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace Funke::SkyAmbient {

	namespace {
		// Linear RGB averages of one texture: whole image plus the three equirect bands.
		struct TextureAverages {
			glm::vec3 All{ 1.0f };
			glm::vec3 Top{ 1.0f };
			glm::vec3 Horizon{ 1.0f };
			glm::vec3 Ground{ 1.0f };
		};

		float ToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
		float ToSrgb(float c) {
			c = std::max(c, 0.0f);
			return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
		}
		glm::vec3 ToLinear(const Color& c) { return { ToLinear(c.r), ToLinear(c.g), ToLinear(c.b) }; }
		Color ToSrgb(const glm::vec3& c) { return Color(ToSrgb(c.r), ToSrgb(c.g), ToSrgb(c.b), 1.0f); }

		const std::array<float, 256>& LinearTable() {
			static const std::array<float, 256> table = [] {
				std::array<float, 256> t{};
				for (int i = 0; i < 256; ++i) t[static_cast<size_t>(i)] = ToLinear(static_cast<float>(i) / 255.0f);
				return t;
			}();
			return table;
		}

		// Rows whose centre lies in [v0, v1) of the image height, weighted by cos(latitude) (equal solid angle per texel).
		glm::vec3 AverageBand(const ImageData& image, float v0, float v1) {
			const auto& lut = LinearTable();
			glm::dvec3 sum(0.0);
			double weight = 0.0;
			for (int y = 0; y < image.Height; ++y) {
				const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(image.Height);
				if (v < v0 || v >= v1) continue;
				const double w = std::max(std::cos((0.5 - static_cast<double>(v)) * 3.14159265358979323846), 1e-3);
				const unsigned char* row = image.Pixels + static_cast<size_t>(y) * image.Width * 4;
				for (int x = 0; x < image.Width; ++x) {
					sum += glm::dvec3(lut[row[x * 4]], lut[row[x * 4 + 1]], lut[row[x * 4 + 2]]) * w;
					weight += w;
				}
			}
			return weight > 0.0 ? glm::vec3(sum / weight) : glm::vec3(0.0f);
		}

		glm::vec3 DefaultTextureColor(ShaderDefaultTexture texture) {
			switch (texture) {
			case ShaderDefaultTexture::Black: return glm::vec3(0.0f);
			case ShaderDefaultTexture::Gray: return glm::vec3(ToLinear(0.5f));
			case ShaderDefaultTexture::Normal: return { 0.5f, 0.5f, 1.0f };
			case ShaderDefaultTexture::White: break;
			}
			return glm::vec3(1.0f);
		}

		std::unordered_map<uint64_t, std::optional<TextureAverages>>& Cache() {
			static std::unordered_map<uint64_t, std::optional<TextureAverages>> cache;
			return cache;
		}

		std::optional<TextureAverages> LoadAverages(uint64_t textureGuid) {
			const std::string path = AssetRegistry::ResolvePath(textureGuid);
			if (path.empty()) return std::nullopt;
			std::unique_ptr<ImageData> image = ImageProcessing::LoadFile(path);
			if (!image || image->Width <= 0 || image->Height <= 0) return std::nullopt;
			// Area-averaged thumbnail: the ambient only needs a handful of colors.
			if (image->Width > 128 || image->Height > 64) {
				if (auto thumbnail = ImageProcessing::Resize(*image, std::min(image->Width, 128), std::min(image->Height, 64))) image = std::move(thumbnail);
			}
			const Colors bands = FromEquirect(*image);
			TextureAverages result;
			result.Top = ToLinear(bands.Top);
			result.Horizon = ToLinear(bands.Horizon);
			result.Ground = ToLinear(bands.Ground);
			result.All = AverageBand(*image, 0.0f, 1.01f);
			return result;
		}

		TextureAverages TextureOrDefault(const MaterialPropertyValue& value, const ShaderProperty& property) {
			if (value.Texture != 0) {
				auto& cache = Cache();
				auto it = cache.find(value.Texture);
				if (it == cache.end()) it = cache.emplace(value.Texture, LoadAverages(value.Texture)).first;
				if (it->second) return *it->second;
			}
			const glm::vec3 c = DefaultTextureColor(property.DefaultTexture);
			return { c, c, c, c };
		}
	}

	Colors FromEquirect(const ImageData& image) {
		if (!image.Pixels || image.Width <= 0 || image.Height <= 0) return { Color(0, 0, 0, 1), Color(0, 0, 0, 1), Color(0, 0, 0, 1) };
		return {
			ToSrgb(AverageBand(image, 0.0f, 0.4f)),
			ToSrgb(AverageBand(image, 0.4f, 0.6f)),
			ToSrgb(AverageBand(image, 0.6f, 1.01f)),
		};
	}

	std::optional<Colors> FromMaterial(uint64_t materialGuid) {
		const Material* material = MaterialLibrary::Get(materialGuid);
		if (!material || material->Shader == 0) return std::nullopt;
		const ShaderDescription* description = CustomShaderLibrary::GetDescription(material->Shader);
		if (!description) return std::nullopt;

		auto find = [&](std::string_view name, ShaderPropertyType type) -> const ShaderProperty* {
			const ShaderProperty* property = description->FindProperty(name);
			return property && property->Type == type ? property : nullptr;
		};
		auto color = [&](std::string_view name) -> std::optional<glm::vec3> {
			const ShaderProperty* property = find(name, ShaderPropertyType::Color);
			if (!property) return std::nullopt;
			const glm::vec4 v = ResolveMaterialProperty(*material, *property).Value;
			return ToLinear(Color(v.r, v.g, v.b, v.a));
		};
		auto texture = [&](std::string_view name) -> std::optional<TextureAverages> {
			const ShaderProperty* property = find(name, ShaderPropertyType::Texture);
			if (!property) return std::nullopt;
			return TextureOrDefault(ResolveMaterialProperty(*material, *property), *property);
		};
		float exposure = 1.0f;
		if (const ShaderProperty* property = find("exposure", ShaderPropertyType::Float)) {
			exposure = std::max(ResolveMaterialProperty(*material, *property).Value.x, 0.0f);
		}

		if (auto top = color("top"), horizon = color("horizon"), ground = color("ground"); top && horizon && ground) {
			return Colors{ ToSrgb(*top), ToSrgb(*horizon), ToSrgb(*ground) };
		}
		if (auto panorama = texture("panorama")) {
			const glm::vec3 tint = color("tint").value_or(glm::vec3(1.0f)) * exposure;
			return Colors{ ToSrgb(panorama->Top * tint), ToSrgb(panorama->Horizon * tint), ToSrgb(panorama->Ground * tint) };
		}
		auto face = [&](std::string_view name) -> std::optional<glm::vec3> {
			const auto averages = texture(name);
			if (!averages) return std::nullopt;
			const std::string colorName = std::string(name) + "Color";
			return averages->All * color(colorName).value_or(glm::vec3(1.0f)) * exposure;
		};
		if (auto up = face("up"), down = face("down"); up && down) {
			glm::vec3 sides(0.0f);
			int sideCount = 0;
			for (const char* name : { "front", "back", "left", "right" }) {
				if (auto side = face(name)) { sides += *side; ++sideCount; }
			}
			const glm::vec3 horizon = sideCount > 0 ? sides / static_cast<float>(sideCount) : (*up + *down) * 0.5f;
			return Colors{ ToSrgb(*up), ToSrgb(horizon), ToSrgb(*down) };
		}
		return std::nullopt;
	}

	void ClearCache() {
		Cache().clear();
	}

}
