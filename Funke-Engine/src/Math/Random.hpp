#pragma once
#include "Core/Export.hpp"
#include <concepts>
#include <cstdint>

namespace Funke {
	struct Color;

	template<typename T>
	concept Randomable = std::same_as<T, int> ||
		std::same_as<T, double> ||
		std::same_as<T, float> ||
		std::same_as<T, std::uint8_t>;

	class FUNKE_API Random {
	public:
		Random() = delete;

		static bool NextBool();
		static Color NextColor();

		// Bounded overloads use non-empty half-open ranges and reject invalid bounds.
		static std::uint8_t NextByte();
		static std::uint8_t NextByte(std::uint8_t min, std::uint8_t max);
		static std::uint8_t NextByte(std::uint8_t max);

		static float NextFloat();
		static float NextFloat(float max);
		static float NextFloat(float min, float max);

		static double NextDouble();
		static double NextDouble(double max);
		static double NextDouble(double min, double max);

		static int NextInt();
		static int NextInt(int max);
		static int NextInt(int min, int max);

		template<Randomable T>
		static T Next(T a) {
			if constexpr (std::same_as<T, int>)
				return NextInt(a);
			else if constexpr (std::same_as<T, double>)
				return NextDouble(a);
			else if constexpr (std::same_as<T, float>)
				return NextFloat(a);
			else if constexpr (std::same_as<T, std::uint8_t>)
				return NextByte(a);
		}

		// Seeds the calling thread's generator only — each thread has its own.
		static void SetSeed(std::uint32_t seed);
	};

}
