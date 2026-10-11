#include <doctest/doctest.h>

#include "Math/Random.hpp"
#include "Collections/Color.hpp"
#include "Core/Exceptions.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <thread>
#include <utility>

using namespace Index;

namespace {
	template<typename Callable>
	void CheckRandomError(Callable&& callable, const IndexErrorCode expectedCode) {
		bool caught = false;
		try {
			std::forward<Callable>(callable)();
		}
		catch (const IndexException& exception) {
			caught = true;
			CHECK(exception.Code() == expectedCode);
		}
		catch (...) {
			caught = true;
			FAIL_CHECK("Expected IndexException");
		}
		CHECK(caught);
	}

	std::array<int, 64> DrawInts() {
		std::array<int, 64> values{};
		for (int& value : values)
			value = Random::NextInt(-1'000'000, 1'000'000);
		return values;
	}
}

TEST_CASE("Random bounded integer overloads use exclusive upper bounds") {
	Random::SetSeed(0x51A7E5u);

	for (int i = 0; i < 2048; ++i) {
		const int positive = Random::NextInt(37);
		CHECK(positive >= 0);
		CHECK(positive < 37);

		const int signedValue = Random::NextInt(-23, 41);
		CHECK(signedValue >= -23);
		CHECK(signedValue < 41);

		const std::uint8_t byte = Random::NextByte(std::uint8_t{ 17 }, std::uint8_t{ 203 });
		CHECK(byte >= 17);
		CHECK(byte < 203);
	}

	CHECK(Random::NextInt(1) == 0);
	CHECK(Random::NextInt(-7, -6) == -7);
	CHECK(Random::NextByte(std::uint8_t{ 1 }) == 0);
	CHECK(Random::NextByte(std::uint8_t{ 254 }, std::uint8_t{ 255 }) == 254);
}

TEST_CASE("Random unbounded integer overloads match the managed non-negative domains") {
	Random::SetSeed(0x9D02C3u);

	for (int i = 0; i < 2048; ++i) {
		CHECK(Random::NextInt() >= 0);
		(void)Random::NextByte();
	}
}

TEST_CASE("Random floating-point overloads stay inside finite half-open ranges") {
	Random::SetSeed(0x771CE5u);

	for (int i = 0; i < 2048; ++i) {
		const float unitFloat = Random::NextFloat();
		CHECK(std::isfinite(unitFloat));
		CHECK(unitFloat >= 0.0f);
		CHECK(unitFloat < 1.0f);

		const float rangedFloat = Random::NextFloat(-8.5f, 13.25f);
		CHECK(std::isfinite(rangedFloat));
		CHECK(rangedFloat >= -8.5f);
		CHECK(rangedFloat < 13.25f);

		const double rangedDouble = Random::NextDouble(-300.0, -12.0);
		CHECK(std::isfinite(rangedDouble));
		CHECK(rangedDouble >= -300.0);
		CHECK(rangedDouble < -12.0);
	}

	CHECK(Random::NextFloat(std::numeric_limits<float>::denorm_min()) == 0.0f);
	CHECK(Random::NextDouble(std::numeric_limits<double>::denorm_min()) == 0.0);
}

TEST_CASE("Random rejects empty reversed and non-finite ranges") {
	CheckRandomError([] { (void)Random::NextByte(std::uint8_t{ 0 }); }, IndexErrorCode::InvalidArgument);
	CheckRandomError([] { (void)Random::NextByte(std::uint8_t{ 7 }, std::uint8_t{ 7 }); }, IndexErrorCode::InvalidArgument);
	CheckRandomError([] { (void)Random::NextByte(std::uint8_t{ 8 }, std::uint8_t{ 7 }); }, IndexErrorCode::InvalidArgument);

	CheckRandomError([] { (void)Random::NextInt(0); }, IndexErrorCode::InvalidArgument);
	CheckRandomError([] { (void)Random::NextInt(-1); }, IndexErrorCode::InvalidArgument);
	CheckRandomError([] { (void)Random::NextInt(4, 4); }, IndexErrorCode::InvalidArgument);
	CheckRandomError([] { (void)Random::NextInt(5, 4); }, IndexErrorCode::InvalidArgument);

	const float floatNaN = std::numeric_limits<float>::quiet_NaN();
	const float floatInfinity = std::numeric_limits<float>::infinity();
	CheckRandomError([=] { (void)Random::NextFloat(floatNaN); }, IndexErrorCode::InvalidArgument);
	CheckRandomError([=] { (void)Random::NextFloat(-floatInfinity, 1.0f); }, IndexErrorCode::InvalidArgument);
	CheckRandomError([=] { (void)Random::NextFloat(0.0f, floatInfinity); }, IndexErrorCode::InvalidArgument);
	CheckRandomError([] { (void)Random::NextFloat(1.0f, 1.0f); }, IndexErrorCode::InvalidArgument);
	CheckRandomError([] { (void)Random::NextFloat(2.0f, 1.0f); }, IndexErrorCode::InvalidArgument);
	CheckRandomError([] {
		(void)Random::NextFloat(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max());
	}, IndexErrorCode::Overflow);

	const double doubleNaN = std::numeric_limits<double>::quiet_NaN();
	const double doubleInfinity = std::numeric_limits<double>::infinity();
	CheckRandomError([=] { (void)Random::NextDouble(doubleNaN); }, IndexErrorCode::InvalidArgument);
	CheckRandomError([=] { (void)Random::NextDouble(-doubleInfinity, 1.0); }, IndexErrorCode::InvalidArgument);
	CheckRandomError([=] { (void)Random::NextDouble(0.0, doubleInfinity); }, IndexErrorCode::InvalidArgument);
	CheckRandomError([] { (void)Random::NextDouble(1.0, 1.0); }, IndexErrorCode::InvalidArgument);
	CheckRandomError([] { (void)Random::NextDouble(2.0, 1.0); }, IndexErrorCode::InvalidArgument);
	CheckRandomError([] {
		(void)Random::NextDouble(std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max());
	}, IndexErrorCode::Overflow);
}

TEST_CASE("Random invalid ranges do not advance the calling thread generator") {
	constexpr std::uint32_t seed = 0xC01DBEEFu;
	Random::SetSeed(seed);
	const int expected = Random::NextInt();

	Random::SetSeed(seed);
	CheckRandomError([] { (void)Random::NextInt(0); }, IndexErrorCode::InvalidArgument);
	CHECK(Random::NextInt() == expected);
}

TEST_CASE("Random SetSeed reproduces mixed draws") {
	constexpr std::uint32_t seed = 0xA11CEu;

	Random::SetSeed(seed);
	const bool expectedBool = Random::NextBool();
	const std::uint8_t expectedByte = Random::NextByte();
	const int expectedInt = Random::NextInt(-1000, 1000);
	const float expectedFloat = Random::NextFloat(-1.0f, 1.0f);
	const double expectedDouble = Random::NextDouble(-1.0, 1.0);

	Random::SetSeed(seed);
	CHECK(Random::NextBool() == expectedBool);
	CHECK(Random::NextByte() == expectedByte);
	CHECK(Random::NextInt(-1000, 1000) == expectedInt);
	CHECK(Random::NextFloat(-1.0f, 1.0f) == expectedFloat);
	CHECK(Random::NextDouble(-1.0, 1.0) == expectedDouble);
}

TEST_CASE("Random seeds and state are local to the calling thread") {
	constexpr std::uint32_t seed = 0xC0FFEEu;
	Random::SetSeed(seed);
	const auto expected = DrawInts();

	Random::SetSeed(seed);
	std::array<int, 64> workerValues{};
	std::thread worker([&] {
		Random::SetSeed(seed);
		workerValues = DrawInts();
	});
	worker.join();

	CHECK(workerValues == expected);
	CHECK(DrawInts() == expected);
}

TEST_CASE("Random NextColor maps sequential unit draws to RGB") {
	constexpr std::uint32_t seed = 0xC0104u;
	Random::SetSeed(seed);
	const Color color = Random::NextColor();

	CHECK(std::isfinite(color.r));
	CHECK(std::isfinite(color.g));
	CHECK(std::isfinite(color.b));
	CHECK(color.r >= 0.0f);
	CHECK(color.r < 1.0f);
	CHECK(color.g >= 0.0f);
	CHECK(color.g < 1.0f);
	CHECK(color.b >= 0.0f);
	CHECK(color.b < 1.0f);
	CHECK(color.a == 1.0f);

	Random::SetSeed(seed);
	CHECK(color.r == Random::NextFloat());
	CHECK(color.g == Random::NextFloat());
	CHECK(color.b == Random::NextFloat());
}
