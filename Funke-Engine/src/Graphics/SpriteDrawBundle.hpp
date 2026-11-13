#pragma once
#include <webgpu/webgpu_cpp.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Funke {

	class SpriteDrawBundle {
	public:
		struct Draw {
			wgpu::RenderPipeline Pipeline;
			wgpu::BindGroup Textures;
			wgpu::BindGroup Material;
			uint32_t Count = 0;
			uint32_t First = 0;
			bool operator==(const Draw& other) const {
				return Pipeline.Get() == other.Pipeline.Get() && Textures.Get() == other.Textures.Get()
					&& Material.Get() == other.Material.Get() && Count == other.Count && First == other.First;
			}
		};

		struct PassKey {
			wgpu::Buffer Vertices;
			wgpu::Buffer Instances;
			wgpu::Buffer Indices;
			uint32_t IndexCount = 0;
			wgpu::TextureFormat ColorFormat = wgpu::TextureFormat::Undefined;
			wgpu::TextureFormat DepthFormat = wgpu::TextureFormat::Undefined;
			uint32_t SampleCount = 1;
			bool operator==(const PassKey& other) const {
				return Vertices.Get() == other.Vertices.Get() && Instances.Get() == other.Instances.Get()
					&& Indices.Get() == other.Indices.Get() && IndexCount == other.IndexCount
					&& ColorFormat == other.ColorFormat && DepthFormat == other.DepthFormat && SampleCount == other.SampleCount;
			}
		};

		void Reset() {
			m_Bundle = nullptr;
			m_Key = {};
			m_Signature.clear();
			m_Attempted = false;
		}

		bool Submit(wgpu::Device device, wgpu::RenderPassEncoder pass, const PassKey& key, const std::vector<Draw>& draws) {
			if (draws.size() < 64 || draws.size() > 65536) {
				Reset();
				Encode(pass, key, draws);
				return false;
			}
			const bool same = m_Key == key && m_Signature == draws;
			if (!same) {
				m_Bundle = nullptr;
				m_Attempted = false;
			}
			if (same && !m_Bundle && !m_Attempted) {
				m_Attempted = true;
				wgpu::RenderBundleEncoderDescriptor descriptor{};
				descriptor.label = "renderer2d-stable-draws";
				descriptor.colorFormatCount = 1;
				descriptor.colorFormats = &key.ColorFormat;
				descriptor.depthStencilFormat = key.DepthFormat;
				descriptor.sampleCount = key.SampleCount;
				auto encoder = device.CreateRenderBundleEncoder(&descriptor);
				if (encoder) {
					Encode(encoder, key, draws);
					m_Bundle = encoder.Finish();
				}
			}
			if (m_Bundle) pass.ExecuteBundles(1, &m_Bundle);
			else Encode(pass, key, draws);
			if (!same) {
				m_Key = key;
				// Own handles so a reload cannot recycle an address in the comparison signature.
				m_Signature = draws;
			}
			return static_cast<bool>(m_Bundle);
		}

		template<class Encoder>
		static void Encode(const Encoder& encoder, const PassKey& key, const std::vector<Draw>& draws) {
			if (draws.empty()) return;
			encoder.SetVertexBuffer(0, key.Vertices);
			encoder.SetVertexBuffer(1, key.Instances);
			encoder.SetIndexBuffer(key.Indices, wgpu::IndexFormat::Uint16);
			WGPURenderPipeline pipeline = nullptr;
			WGPUBindGroup textures = nullptr, material = nullptr;
			for (const Draw& draw : draws) {
				if (pipeline != draw.Pipeline.Get()) {
					encoder.SetPipeline(draw.Pipeline);
					pipeline = draw.Pipeline.Get();
					textures = material = nullptr;
				}
				if (draw.Material && material != draw.Material.Get()) {
					encoder.SetBindGroup(1, draw.Material);
					material = draw.Material.Get();
				}
				if (textures != draw.Textures.Get()) {
					encoder.SetBindGroup(0, draw.Textures);
					textures = draw.Textures.Get();
				}
				encoder.DrawIndexed(key.IndexCount, draw.Count, 0, 0, draw.First);
			}
		}

	private:
		wgpu::RenderBundle m_Bundle;
		PassKey m_Key;
		std::vector<Draw> m_Signature;
		bool m_Attempted = false;
	};

}
