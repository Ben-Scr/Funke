#pragma once
#include "Core/Export.hpp"
#include "Core/GraphicsSettings.hpp"
#include "Collections/Color.hpp"
#include "Collections/Curve.hpp"
#include "Collections/Gradient.hpp"
#include "Collections/Vec2.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Index {

	// Cone and Hemisphere open along the emitter's local +Y; Box emits along local +Y.
	enum class ParticleShape3D : uint8_t {
		Sphere,
		Hemisphere,
		Cone,
		Box
	};

	enum class ParticleSpace3D : uint8_t {
		Local,
		World
	};

	enum class ParticleBlendMode3D : uint8_t {
		Alpha,
		Additive
	};

	// Camera-facing billboard particles emitted around the entity's Transform3D and drawn by Renderer3D.
	// Holds no scene pointers: the caller passes the emitter's world matrix to Simulate, so value copies stay valid.
	class INDEX_API ParticleSystem3DComponent {
	public:
		struct Particle {
			// World space, or emitter-local when SimulationSpace is Local.
			Vec3 Position{ 0.0f };
			Vec3 Velocity{ 0.0f };
			Index::Color Color{ Index::Color::White() };
			Index::Color StartColor{ Index::Color::White() };
			float Size = 1.0f;
			float StartSize = 1.0f;
			// Degrees around the view axis.
			float Rotation = 0.0f;
			float AngularVelocity = 0.0f;
			float LifeTime = 1.0f;
			float StartLifeTime = 1.0f;
		};

		struct Burst {
			uint32_t Count{ 10 };
			float Interval{ 1.0f };
			float TimeUntilNext{ 0.0f };
		};

		bool PlayOnAwake = true;

		float LifeTime = 2.0f;
		float Speed = 2.0f;
		float Size = 0.25f;
		// Fractions in [0, 1]: each particle draws its value from base * (1 +- variation).
		float LifeTimeVariation = 0.0f;
		float SpeedVariation = 0.0f;
		float SizeVariation = 0.0f;
		bool RandomRotation = false;
		// Degrees per second around the view axis.
		float AngularVelocity = 0.0f;
		bool UseGravity = false;
		Vec3 Gravity{ 0.0f, -9.81f, 0.0f };
		// Fraction of velocity lost per second.
		float Drag = 0.0f;
		bool UseRandomColors = false;

		// Particles per second.
		float EmitOverTime = 10.0f;
		ParticleSpace3D SimulationSpace = ParticleSpace3D::World;

		ParticleShape3D Shape = ParticleShape3D::Cone;
		// Sphere/Hemisphere radius, or the cone's base radius.
		float Radius = 0.25f;
		// Half angle in degrees.
		float ConeAngle = 25.0f;
		Vec3 BoxSize{ 1.0f, 1.0f, 1.0f };
		// Sphere/Hemisphere: emit from the shell only; Cone: from the base circle's edge only.
		bool EmitFromSurface = false;

		bool ScaleOverLifetimeEnabled = false;
		Curve ScaleOverLifetime;
		// Multiplies each particle's start color over its normalized age; a white gradient is a no-op.
		bool ColorOverLifetimeEnabled = false;
		Gradient ColorOverLifetime;

		// Texture asset GUID; 0 draws soft round dots.
		uint64_t Texture = 0;
		Index::Color Color{ Index::Color::White() };
		ParticleBlendMode3D BlendMode = ParticleBlendMode3D::Alpha;
		uint32_t MaxParticles = 1000;

		void Play() { m_IsEmitting = true; m_IsSimulating = true; }
		// Freezes emission and simulation; live particles stay visible.
		void Pause() { m_IsEmitting = false; m_IsSimulating = false; }
		// Stops emission; live particles keep simulating until they expire.
		void Stop() { m_IsEmitting = false; }
		void StopAndReset() { m_IsEmitting = false; m_IsSimulating = false; ResetSimulation(); }
		void Restart() { ResetSimulation(); Play(); }
		void PlayOnAwakeIfEnabled() { if (PlayOnAwake) Play(); }

		bool IsEmitting() const { return m_IsEmitting; }
		bool IsSimulating() const { return m_IsSimulating; }
		bool IsPlaying() const { return m_IsEmitting; }

		// Emits immediately from the emitter pose of the last Simulate, even while stopped.
		void Emit(size_t count);

		// Advances emission and simulation by deltaTime; emitterWorld is the entity's Transform3D world matrix.
		void Simulate(float deltaTime, const glm::mat4& emitterWorld, ParticleQualityLevel quality = ParticleQualityLevel::High);

		void AddBurst(const Burst& burst) { m_Bursts.push_back(burst); }
		void RemoveBurst(size_t index) { if (index < m_Bursts.size()) m_Bursts.erase(m_Bursts.begin() + static_cast<std::ptrdiff_t>(index)); }
		void ClearBursts() { m_Bursts.clear(); }
		std::span<Burst> GetBursts() noexcept { return m_Bursts; }
		std::span<const Burst> GetBursts() const noexcept { return m_Bursts; }

		std::span<const Particle> GetParticles() const noexcept { return m_Particles; }
		size_t GetParticleCount() const noexcept { return m_Particles.size(); }

	private:
		void EmitInternal(size_t count, ParticleQualityLevel quality);
		void ResetSimulation();

		std::vector<Particle> m_Particles;
		std::vector<Burst> m_Bursts;
		glm::mat4 m_EmitterWorld{ 1.0f };
		float m_EmitAccumulator = 0.0f;
		bool m_IsEmitting = false;
		bool m_IsSimulating = false;
	};

}
