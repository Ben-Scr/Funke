#include "pch.hpp"
#include "Random.hpp"
#include "Collections/Color.hpp"
#include "Core/Exceptions.hpp"

#include <cmath>
#include <limits>
#include <optional>
#include <random>

namespace Index {
	namespace {
		std::optional<std::mt19937>& GeneratorStorage() {
			thread_local std::optional<std::mt19937> generator;
			return generator;
		}

		std::mt19937& Generator() {
			auto& generator = GeneratorStorage();
			if (!generator) generator.emplace(std::random_device{}());
			return *generator;
		}

		template<typename T>
		void ValidateRealRange(const T min, const T max) {
			if (!std::isfinite(min) || !std::isfinite(max))
				IDX_THROW(IndexErrorCode::InvalidArgument, "Random range bounds must be finite");
			if (!(min < max))
				IDX_THROW(IndexErrorCode::InvalidArgument, "Random range requires min < max");
			if (min < T{ 0 } && max > std::numeric_limits<T>::max() + min)
				IDX_THROW(IndexErrorCode::Overflow, "Random range span is not representable");
		}

		template<typename T>
		T NextReal(const T min, const T max) {
			ValidateRealRange(min, max);
			std::uniform_real_distribution<T> distribution(min, max);
			const T value = distribution(Generator());
			return value < max ? value : std::nextafter(max, min);
		}
	}

	void Random::SetSeed(const std::uint32_t seed) {
		GeneratorStorage().emplace(seed);
	}

	Color Random::NextColor() {
		const float red = NextFloat();
		const float green = NextFloat();
		const float blue = NextFloat();
		return Color(red, green, blue);
	}
	bool Random::NextBool() {
		std::bernoulli_distribution distribution(0.5);
		return distribution(Generator());
	}
	std::uint8_t Random::NextByte() {
		std::uniform_int_distribution<int> distribution(0, std::numeric_limits<std::uint8_t>::max());
		return static_cast<std::uint8_t>(distribution(Generator()));
	}
	std::uint8_t Random::NextByte(const std::uint8_t max) {
		if (max == 0)
			IDX_THROW(IndexErrorCode::InvalidArgument, "Random byte max must be greater than zero");

		std::uniform_int_distribution<int> distribution(0, static_cast<int>(max) - 1);
		return static_cast<std::uint8_t>(distribution(Generator()));
	}
	std::uint8_t Random::NextByte(const std::uint8_t min, const std::uint8_t max) {
		if (min >= max)
			IDX_THROW(IndexErrorCode::InvalidArgument, "Random byte range requires min < max");

		std::uniform_int_distribution<int> distribution(static_cast<int>(min), static_cast<int>(max) - 1);
		return static_cast<std::uint8_t>(distribution(Generator()));
	}

	double Random::NextDouble() {
		return NextReal(0.0, 1.0);
	}
	double Random::NextDouble(const double max) {
		return NextReal(0.0, max);
	}
	double Random::NextDouble(const double min, const double max) {
		return NextReal(min, max);
	}

	float Random::NextFloat() {
		return NextReal(0.0f, 1.0f);
	}
	float Random::NextFloat(const float max) {
		return NextReal(0.0f, max);
	}
	float Random::NextFloat(const float min, const float max) {
		return NextReal(min, max);
	}

	int Random::NextInt() {
		std::uniform_int_distribution<int> distribution(0, std::numeric_limits<int>::max());
		return distribution(Generator());
	}
	int Random::NextInt(const int max) {
		if (max <= 0)
			IDX_THROW(IndexErrorCode::InvalidArgument, "Random integer max must be greater than zero");

		std::uniform_int_distribution<int> distribution(0, max - 1);
		return distribution(Generator());
	}
	int Random::NextInt(const int min, const int max) {
		if (min >= max)
			IDX_THROW(IndexErrorCode::InvalidArgument, "Random integer range requires min < max");

		std::uniform_int_distribution<int> distribution(min, max - 1);
		return distribution(Generator());
	}
}
