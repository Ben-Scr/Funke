#include "pch.hpp"
#include "Audio/AudioHrtf.hpp"
#include "Audio/HrtfKemarData.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <vector>

namespace Funke {
	namespace {
		constexpr float k_Pi = 3.14159265358979323846f;
		constexpr uint32_t k_Pending = 4;
		struct Tap { float Left = 0.0f; float Right = 0.0f; };
		using Kernel = std::vector<Tap>;

		float Sinc(float x) {
			return std::abs(x) < 1e-6f ? 1.0f : std::sin(k_Pi * x) / (k_Pi * x);
		}

		struct FilterBank {
			struct Ring { int Elevation; size_t Start; size_t Count; };
			std::vector<Kernel> Filters;
			std::vector<Ring> Rings;
			size_t TapCount;

			explicit FilterBank(uint32_t sampleRate) {
				const float ratio = 44100.0f / sampleRate;
				const float cutoff = std::min(1.0f, 1.0f / ratio);
				// Padding preserves the sinc's pre/post ringing and gives both ears the same delay.
				const float radius = 16.0f / cutoff;
				TapCount = static_cast<size_t>(std::ceil((128.0f + 2.0f * radius) / ratio));
				for (const auto& measurement : KemarData::Measurements) {
					if (Rings.empty() || Rings.back().Elevation != measurement.Elevation)
						Rings.push_back({ measurement.Elevation, Filters.size(), 0 });
					++Rings.back().Count;
					Filters.emplace_back(TapCount);
				}
				// Every direction uses the same resampling weights for this sample rate.
				for (size_t tap = 0; tap < TapCount; ++tap) {
					const float time = static_cast<float>(tap) * ratio - radius;
					const int first = std::max(0, static_cast<int>(std::ceil(time - radius)));
					const int last = std::min(127, static_cast<int>(std::floor(time + radius)));
					std::array<float, 128> weights;
					for (int i = first; i <= last; ++i) {
						const float offset = (time - i) * cutoff;
						weights[i] = Sinc(offset) * (0.5f + 0.5f * std::cos(k_Pi * offset / 16.0f))
							* cutoff * ratio / 32768.0f;
					}
					for (size_t direction = 0; direction < Filters.size(); ++direction) {
						const auto& measurement = KemarData::Measurements[direction];
						Tap& output = Filters[direction][tap];
						for (int i = first; i <= last; ++i) {
							output.Left += measurement.Samples[i * 2] * weights[i];
							output.Right += measurement.Samples[i * 2 + 1] * weights[i];
						}
					}
				}
			}

			void AddRing(Kernel& output, const Ring& ring, float azimuth, float weight, bool swapEars) const {
				size_t high = ring.Start;
				const size_t end = ring.Start + ring.Count - 1;
				while (high < end && KemarData::Measurements[high].Azimuth < azimuth) ++high;
				const size_t low = high > ring.Start ? high - 1 : high;
				const float span = static_cast<float>(KemarData::Measurements[high].Azimuth - KemarData::Measurements[low].Azimuth);
				const float blend = span > 0 ? std::clamp((azimuth - KemarData::Measurements[low].Azimuth) / span, 0.0f, 1.0f) : 0;
				for (size_t i = 0; i < TapCount; ++i) {
					const float left = std::lerp(Filters[low][i].Left, Filters[high][i].Left, blend) * weight;
					const float right = std::lerp(Filters[low][i].Right, Filters[high][i].Right, blend) * weight;
					output[i].Left += swapEars ? right : left;
					output[i].Right += swapEars ? left : right;
				}
			}

			void Interpolate(Kernel& output, const glm::vec2& angles) const {
				std::fill(output.begin(), output.end(), Tap{});
				const float elevation = std::clamp(angles.y, -40.0f, 90.0f);
				size_t high = 0;
				while (high + 1 < Rings.size() && Rings[high].Elevation < elevation) ++high;
				const size_t low = high > 0 ? high - 1 : high;
				const float span = static_cast<float>(Rings[high].Elevation - Rings[low].Elevation);
				const float blend = span > 0 ? (elevation - Rings[low].Elevation) / span : 0;
				AddRing(output, Rings[low], std::abs(angles.x), 1.0f - blend, angles.x < 0);
				AddRing(output, Rings[high], std::abs(angles.x), blend, angles.x < 0);
			}
		};

		std::shared_ptr<const FilterBank> GetBank(uint32_t sampleRate) {
			// Retain banks across recycled one-shots; only the engine thread accesses this cache.
			static std::map<uint32_t, std::shared_ptr<const FilterBank>> banks;
			auto& bank = banks[sampleRate];
			if (!bank) bank = std::make_shared<FilterBank>(sampleRate);
			return bank;
		}

		glm::vec3 UnitOr(const glm::vec3& value, const glm::vec3& fallback) {
			const float length = glm::length(value);
			return std::isfinite(length) && length > 1e-6f ? value / length : fallback;
		}

		glm::vec2 ListenerAngles(const AudioListenerState& listener, const glm::vec3& position) {
			const glm::vec3 forward = UnitOr(listener.Forward, { 0, 0, -1 });
			const glm::vec3 fallbackUp = std::abs(forward.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
			const glm::vec3 right = UnitOr(glm::cross(forward, listener.Up), glm::normalize(glm::cross(forward, fallbackUp)));
			const glm::vec3 up = glm::cross(right, forward);
			const glm::vec3 direction = UnitOr(position - listener.Position, forward);
			return { std::atan2(glm::dot(direction, right), glm::dot(direction, forward)) * 180.0f / k_Pi,
				std::asin(std::clamp(glm::dot(direction, up), -1.0f, 1.0f)) * 180.0f / k_Pi };
		}
	}

	struct AudioHrtf::Impl {
		ma_node_base Node{};
		struct Snapshot { Kernel Filter; float Wet = 1.0f; };
		std::shared_ptr<const FilterBank> Bank;
		std::array<Snapshot, 3> Snapshots;
		std::atomic<uint32_t> Middle{ 1 };
		uint32_t WriteIndex = 2;
		uint32_t ReadIndex = 0;
		Kernel Current;
		Kernel Step;
		std::vector<float> History;
		size_t Cursor = 0;
		uint32_t RampFrames = 0;
		uint32_t RemainingRamp = 0;
		uint32_t TailFrames = 0;
		std::atomic<uint32_t> PublishedTail{ 0 };
		float Wet = 1.0f;
		float WetStep = 0.0f;
		glm::vec2 LastAngles{};
		bool Enabled = true;
		bool Initialized = false;

		static void Process(ma_node* node, const float** input, ma_uint32* inputCount, float** output, ma_uint32* outputCount) {
			auto& self = *static_cast<Impl*>(node);
			const size_t taps = self.Current.size();
			// Each side owns one snapshot; the atomic exchange transfers the third without blocking.
			if (self.Middle.load(std::memory_order_acquire) & k_Pending) {
				self.ReadIndex = self.Middle.exchange(self.ReadIndex, std::memory_order_acq_rel) & ~k_Pending;
				const auto& target = self.Snapshots[self.ReadIndex];
				for (size_t i = 0; i < taps; ++i) {
					self.Step[i].Left = (target.Filter[i].Left - self.Current[i].Left) / self.RampFrames;
					self.Step[i].Right = (target.Filter[i].Right - self.Current[i].Right) / self.RampFrames;
				}
				self.WetStep = (target.Wet - self.Wet) / self.RampFrames;
				self.RemainingRamp = self.RampFrames;
			}
			const uint32_t available = std::min(*inputCount, *outputCount);
			for (uint32_t frame = 0; frame < *outputCount; ++frame) {
				const float left = input && input[0] && frame < available ? input[0][frame * 2] : 0;
				const float right = input && input[0] && frame < available ? input[0][frame * 2 + 1] : 0;
				const float mono = (left + right) * 0.5f;
				self.Cursor = self.Cursor == 0 ? taps - 1 : self.Cursor - 1;
				self.History[self.Cursor] = self.History[self.Cursor + taps] = mono;
				if (std::abs(mono) > 1e-15f) self.TailFrames = static_cast<uint32_t>(taps);
				else if (self.TailFrames) --self.TailFrames;
				if (self.RemainingRamp) {
					for (size_t i = 0; i < taps; ++i) {
						self.Current[i].Left += self.Step[i].Left;
						self.Current[i].Right += self.Step[i].Right;
					}
					self.Wet += self.WetStep;
					if (--self.RemainingRamp == 0) {
						std::copy(self.Snapshots[self.ReadIndex].Filter.begin(), self.Snapshots[self.ReadIndex].Filter.end(), self.Current.begin());
						self.Wet = self.Snapshots[self.ReadIndex].Wet;
					}
				}
				float filteredLeft = 0, filteredRight = 0;
				if (self.TailFrames && self.Wet > 0) {
					const float* history = self.History.data() + self.Cursor;
					for (size_t i = 0; i < taps; ++i) {
						filteredLeft += history[i] * self.Current[i].Left;
						filteredRight += history[i] * self.Current[i].Right;
					}
				}
				output[0][frame * 2] = std::lerp(left, filteredLeft, self.Wet);
				output[0][frame * 2 + 1] = std::lerp(right, filteredRight, self.Wet);
			}
			*inputCount = available;
			self.PublishedTail.store(self.Wet > 0 ? self.TailFrames : 0, std::memory_order_release);
		}
	};

	AudioHrtf::AudioHrtf() = default;
	AudioHrtf::~AudioHrtf() {
		if (m_Impl && m_Impl->Initialized) ma_node_uninit(&m_Impl->Node, nullptr);
	}

	bool AudioHrtf::Initialize(ma_engine* engine, ma_sound* sound, const AudioListenerState& listener, const glm::vec3& position) {
		if (m_Impl || !engine || !sound || ma_engine_get_channels(engine) != 2) return false;
		const uint32_t rate = ma_engine_get_sample_rate(engine);
		if (!Initialize(rate, listener, position)) return false;
		auto& impl = m_Impl;
		if (ma_sound_is_playing(sound)) {
			impl->Wet = 0;
			impl->WetStep = 1.0f / impl->RampFrames;
			impl->RemainingRamp = impl->RampFrames;
		}
		static const ma_node_vtable vtable{ Impl::Process, nullptr, 1, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING };
		ma_node_config config = ma_node_config_init();
		const ma_uint32 channels = 2;
		config.vtable = &vtable;
		config.pInputChannels = &channels;
		config.pOutputChannels = &channels;
		if (ma_node_init(ma_engine_get_node_graph(engine), &config, nullptr, &impl->Node) != MA_SUCCESS) {
			m_Impl.reset();
			return false;
		}
		if (ma_node_attach_output_bus(&impl->Node, 0, ma_engine_get_endpoint(engine), 0) != MA_SUCCESS
			|| ma_node_attach_output_bus(sound, 0, &impl->Node, 0) != MA_SUCCESS) {
			ma_node_uninit(&impl->Node, nullptr);
			m_Impl.reset();
			return false;
		}
		impl->Initialized = true;
		return true;
	}

	bool AudioHrtf::Initialize(uint32_t rate, const AudioListenerState& listener, const glm::vec3& position) {
		if (m_Impl) return false;
		if (rate < 8000 || rate > 192000) return false;
		auto impl = std::make_unique<Impl>();
		impl->Bank = GetBank(rate);
		const size_t taps = impl->Bank->TapCount;
		impl->Current.resize(taps);
		impl->Step.resize(taps);
		impl->History.resize(taps * 2);
		impl->LastAngles = ListenerAngles(listener, position);
		impl->Bank->Interpolate(impl->Current, impl->LastAngles);
		for (auto& snapshot : impl->Snapshots) snapshot.Filter = impl->Current;
		impl->RampFrames = std::max(1u, rate / 100);
		m_Impl = std::move(impl);
		return true;
	}

	void AudioHrtf::Process(const float* input, float* output, uint32_t frames) {
		if (!m_Impl || m_Impl->Initialized || !output) return;
		ma_uint32 inputFrames = frames, outputFrames = frames;
		Impl::Process(m_Impl.get(), &input, &inputFrames, &output, &outputFrames);
	}

	void AudioHrtf::Update(const AudioListenerState& listener, const glm::vec3& position, bool enabled) {
		if (!m_Impl) return;
		auto& self = *m_Impl;
		const glm::vec2 angles = ListenerAngles(listener, position);
		if (self.Enabled == enabled && glm::length(angles - self.LastAngles) < 0.01f) return;
		auto& snapshot = self.Snapshots[self.WriteIndex];
		self.Bank->Interpolate(snapshot.Filter, angles);
		snapshot.Wet = enabled ? 1.0f : 0.0f;
		self.WriteIndex = self.Middle.exchange(self.WriteIndex | k_Pending, std::memory_order_acq_rel) & ~k_Pending;
		self.LastAngles = angles;
		self.Enabled = enabled;
	}

	bool AudioHrtf::HasTail() const {
		return m_Impl && m_Impl->PublishedTail.load(std::memory_order_acquire) != 0;
	}

	void AudioHrtf::SetPaused(bool paused) {
		if (m_Impl && m_Impl->Initialized) ma_node_set_state(&m_Impl->Node, paused ? ma_node_state_stopped : ma_node_state_started);
	}

	uint32_t AudioHrtf::GetTailFrameCount() const {
		return m_Impl ? static_cast<uint32_t>(m_Impl->Bank->TapCount) : 0;
	}
}
