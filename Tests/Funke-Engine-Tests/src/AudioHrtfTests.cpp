#include <doctest/doctest.h>

#include "Audio/AudioHrtf.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <thread>
#include <vector>

using namespace Funke;

namespace {
	struct HrtfMix {
		std::unique_ptr<AudioHrtf> Hrtf;
		std::vector<float> Signal;
		size_t Cursor = 0;
		uint32_t Channels = 1;

		HrtfMix(uint32_t rate, const glm::vec3& position, const AudioListenerState& listener = {}, uint32_t frames = 4096, uint32_t channels = 1) {
			Channels = channels;
			Signal.resize(static_cast<size_t>(frames) * channels);
			for (uint32_t i = 0; i < frames; ++i) {
				const float value = i % 1024 == 31 ? 0.5f : 0.0f;
				for (uint32_t ch = 0; ch < channels; ++ch) Signal[i * channels + ch] = value;
			}
			Hrtf = std::make_unique<AudioHrtf>();
			REQUIRE(Hrtf->Initialize(rate, listener, position));
		}

		std::vector<float> Read(uint32_t frames) {
			std::vector<float> output(static_cast<size_t>(frames) * 2);
			std::vector<float> input(output.size());
			for (uint32_t i = 0; i < frames && Cursor < Signal.size() / Channels; ++i, ++Cursor) {
				input[i * 2] = Signal[Cursor * Channels];
				input[i * 2 + 1] = Signal[Cursor * Channels + Channels - 1];
			}
			Hrtf->Process(input.data(), output.data(), frames);
			for (float value : output) REQUIRE(std::isfinite(value));
			return output;
		}
	};

	double Energy(const std::vector<float>& samples, size_t channel) {
		double energy = 0;
		for (size_t i = channel; i < samples.size(); i += 2) energy += samples[i] * samples[i];
		return energy;
	}

	size_t Peak(const std::vector<float>& samples, size_t channel) {
		size_t peak = channel;
		for (size_t i = channel; i < samples.size(); i += 2)
			if (std::abs(samples[i]) > std::abs(samples[peak])) peak = i;
		return peak / 2;
	}

	std::vector<float> Render(const glm::vec3& position, uint32_t rate = 48000, const AudioListenerState& listener = {}) {
		HrtfMix mix(rate, position, listener);
		return mix.Read(1024);
	}

	double SpectralDifference(const std::vector<float>& a, const std::vector<float>& b) {
		const auto magnitude = [](const std::vector<float>& samples, double frequency) {
			double real = 0, imag = 0;
			for (size_t i = 0; i < samples.size() / 2; ++i) {
				const double phase = 6.283185307179586 * frequency * i / 48000.0;
				real += samples[i * 2] * std::cos(phase);
				imag += samples[i * 2] * std::sin(phase);
			}
			return std::hypot(real, imag);
		};
		double difference = 0;
		for (double frequency : { 1000.0, 3000.0, 6000.0, 9000.0, 12000.0 })
			difference += std::abs(magnitude(a, frequency) / magnitude(a, 1000) - magnitude(b, frequency) / magnitude(b, 1000));
		return difference;
	}
}

TEST_CASE("HRTF distinguishes front rear and elevation through spectral cues") {
	const auto front = Render({ 0, 0, -1 });
	const auto rear = Render({ 0, 0, 1 });
	const auto above = Render({ 0, 1, 0 });
	CHECK(Energy(front, 0) > 0.001);
	CHECK(SpectralDifference(front, rear) > 0.5);
	CHECK(SpectralDifference(front, above) > 0.5);
	CHECK(SpectralDifference(rear, above) > 0.5);
	for (size_t i = 0; i < front.size(); i += 2) CHECK(front[i] == doctest::Approx(front[i + 1]).epsilon(0.0001));
}

TEST_CASE("HRTF preserves ear delay and level and mirrors left and right") {
	const auto right = Render({ 1, 0, 0 });
	const auto left = Render({ -1, 0, 0 });
	CHECK(Peak(right, 1) < Peak(right, 0));
	CHECK(Energy(right, 1) > Energy(right, 0) * 2);
	for (size_t i = 0; i < right.size(); i += 2) {
		CHECK(right[i] == doctest::Approx(left[i + 1]).epsilon(0.0001));
		CHECK(right[i + 1] == doctest::Approx(left[i]).epsilon(0.0001));
	}
}

TEST_CASE("HRTF rotates with camera yaw pitch and roll") {
	const auto front = Render({ 0, 0, -1 });
	AudioListenerState yaw;
	yaw.Position = { 5, 6, 7 };
	yaw.Forward = { 1, 0, 0 };
	const auto yawed = Render({ 6, 6, 7 }, 48000, yaw);
	AudioListenerState pitch;
	pitch.Forward = { 0, 1, 0 };
	pitch.Up = { 0, 0, 1 };
	const auto pitched = Render({ 0, 1, 0 }, 48000, pitch);
	AudioListenerState roll;
	roll.Up = { 1, 0, 0 };
	const auto rolled = Render({ 0, 1, 0 }, 48000, roll);
	const auto left = Render({ -1, 0, 0 });
	for (size_t i = 0; i < front.size(); ++i) {
		CHECK(yawed[i] == doctest::Approx(front[i]).epsilon(0.0001));
		CHECK(pitched[i] == doctest::Approx(front[i]).epsilon(0.0001));
		CHECK(rolled[i] == doctest::Approx(left[i]).epsilon(0.0001));
	}
}

TEST_CASE("HRTF resampling preserves physical ear delay and gain") {
	double referenceDelay = 0, referenceGain = 0;
	for (uint32_t rate : { 44100u, 32000u, 48000u, 96000u, 192000u }) {
		const auto right = Render({ 1, 0, 0 }, rate);
		const double delay = (static_cast<double>(Peak(right, 0)) - Peak(right, 1)) / rate;
		double gain = 0;
		for (size_t i = 1; i < right.size(); i += 2) gain += right[i];
		if (rate == 44100) { referenceDelay = delay; referenceGain = gain; }
		CHECK(delay == doctest::Approx(referenceDelay).epsilon(0.08));
		CHECK(gain == doctest::Approx(referenceGain).epsilon(0.03));
	}
}

TEST_CASE("HRTF processing is independent of callback block size and drains short clip tails") {
	HrtfMix whole(48000, { 1, 0, 0 }, {}, 32);
	const auto expected = whole.Read(1024);
	HrtfMix split(48000, { 1, 0, 0 }, {}, 32);
	std::vector<float> actual;
	for (uint32_t frames : { 7u, 25u, 13u, 211u, 768u }) {
		const auto block = split.Read(frames);
		actual.insert(actual.end(), block.begin(), block.end());
	}
	REQUIRE(actual.size() == expected.size());
	CHECK(Energy(expected, 1) > 0.001);
	CHECK(Peak(expected, 1) >= 32);
	CHECK_FALSE(split.Hrtf->HasTail());
	for (size_t i = 0; i < actual.size(); ++i) CHECK(actual[i] == doctest::Approx(expected[i]).epsilon(0.0001));
}

TEST_CASE("HRTF can switch direction and bypass while retaining stereo dry output") {
	HrtfMix mix(48000, { 1, 0, 0 }, {}, 6144, 2);
	for (size_t i = 1; i < mix.Signal.size(); i += 2) mix.Signal[i] *= 0.5f;
	const auto right = mix.Read(1024);
	mix.Hrtf->Update({}, { -1, 0, 0 }, true);
	mix.Read(1024);
	const auto left = mix.Read(1024);
	CHECK(Energy(right, 1) > Energy(right, 0));
	CHECK(Energy(left, 0) > Energy(left, 1));
	mix.Hrtf->Update({}, { -1, 0, 0 }, false);
	mix.Read(1024);
	const auto dry = mix.Read(1024);
	CHECK(dry[31 * 2] == doctest::Approx(0.5f));
	CHECK(dry[31 * 2 + 1] == doctest::Approx(0.25f));
	CHECK_FALSE(mix.Hrtf->HasTail());
}

TEST_CASE("HRTF interpolation is continuous across measured directions and the rear seam") {
	for (float angle : { 44.99f, 179.99f }) {
		const float a = angle * 3.14159265358979323846f / 180;
		const float b = (angle + 0.02f) * 3.14159265358979323846f / 180;
		const auto first = Render({ std::sin(a), 0.2f, -std::cos(a) });
		const auto second = Render({ std::sin(b), 0.2f, -std::cos(b) });
		double difference = 0;
		for (size_t i = 0; i < first.size(); ++i) difference += std::pow(first[i] - second[i], 2);
		CHECK(difference < (Energy(first, 0) + Energy(first, 1)) * 0.0001);
	}
}

TEST_CASE("HRTF exchanges moving directions safely with the mixing thread") {
	HrtfMix mix(48000, { 0, 0, -1 }, {}, 65536);
	std::fill(mix.Signal.begin(), mix.Signal.end(), 0.125f);
	std::thread producer([&] {
		for (int i = 0; i < 2000; ++i) {
			const float angle = i * 0.01f;
			mix.Hrtf->Update({}, { std::sin(angle), 0.4f, -std::cos(angle) }, true);
		}
	});
	std::vector<float> output(128);
	std::vector<float> input(128, 0.125f);
	bool finite = true;
	for (int i = 0; i < 1000; ++i) {
		mix.Hrtf->Process(input.data(), output.data(), 64);
		for (float value : output) finite &= std::isfinite(value) && std::abs(value) < 10;
	}
	producer.join();
	CHECK(finite);
}
