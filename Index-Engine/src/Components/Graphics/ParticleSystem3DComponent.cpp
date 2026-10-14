#include "pch.hpp"
#include "Components/Graphics/ParticleSystem3DComponent.hpp"

#include "Graphics/ParticleQualityPolicy.hpp"
#include "Math/Random.hpp"

#if INDEX_WITH_APPLICATION
#include "Core/Application.hpp"
#endif

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace Index {
	namespace {
		ParticleQualityLevel GetActiveParticleQuality() {
#if INDEX_WITH_APPLICATION
			if (const Application* application = Application::GetInstance()) {
				return application->GetEffectiveGraphicsSettings().ParticleQuality;
			}
#endif
			return ParticleQualityLevel::High;
		}

		float Vary(float value, float variation) {
			const float v = std::clamp(variation, 0.0f, 1.0f);
			return v > 0.0f ? value * (1.0f + Random::NextFloat(-v, v)) : value;
		}

		Vec3 RandomUnitVector() {
			const float z = Random::NextFloat(-1.0f, 1.0f);
			const float phi = Random::NextFloat(glm::two_pi<float>());
			const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
			return { r * std::cos(phi), z, r * std::sin(phi) };
		}
	}

	void ParticleSystem3DComponent::ResetSimulation() {
		m_Particles.clear();
		m_EmitAccumulator = 0.0f;
		for (Burst& burst : m_Bursts) {
			burst.TimeUntilNext = 0.0f;
		}
	}

	void ParticleSystem3DComponent::Emit(size_t count) {
		const ParticleQualityLevel quality = GetActiveParticleQuality();
		// Emitted particles must move and expire even while the system is stopped.
		m_IsSimulating = true;
		EmitInternal(ParticleQualityPolicy::ScaleCount(count, quality), quality);
	}

	void ParticleSystem3DComponent::Simulate(float deltaTime, const glm::mat4& emitterWorld, ParticleQualityLevel quality) {
		m_EmitterWorld = emitterWorld;
		if (quality == ParticleQualityLevel::Off) {
			m_Particles.clear();
			m_EmitAccumulator = 0.0f;
			return;
		}

		const uint32_t maxParticles = ParticleQualityPolicy::EffectiveLimit(MaxParticles, quality);
		if (m_Particles.size() > maxParticles) {
			m_Particles.erase(m_Particles.begin(), m_Particles.begin() + static_cast<std::ptrdiff_t>(m_Particles.size() - maxParticles));
		}
		if (deltaTime <= 0.0f) return;

		if (m_IsEmitting) {
			const float toEmit = std::max(EmitOverTime, 0.0f) * ParticleQualityPolicy::Density(quality) * deltaTime + m_EmitAccumulator;
			const float whole = std::floor(toEmit);
			m_EmitAccumulator = toEmit - whole;
			EmitInternal(static_cast<size_t>(whole), quality);

			for (Burst& burst : m_Bursts) {
				// Starts at 0, so a burst fires on the first update after Play, then every Interval.
				burst.TimeUntilNext -= deltaTime;
				if (burst.TimeUntilNext <= 0.0f) {
					EmitInternal(ParticleQualityPolicy::ScaleCount(burst.Count, quality), quality);
					burst.TimeUntilNext = std::max(burst.Interval, 0.001f);
				}
			}
		}

		if (!m_IsSimulating) return;

		Vec3 gravity(0.0f);
		if (UseGravity) {
			gravity = Gravity;
			// Gravity is authored in world space; local-space particles need it in emitter axes.
			const glm::mat3 basis(emitterWorld);
			if (SimulationSpace == ParticleSpace3D::Local && std::abs(glm::determinant(basis)) > 1e-12f) {
				gravity = glm::inverse(basis) * Gravity;
			}
		}
		const float damping = std::max(0.0f, 1.0f - std::max(Drag, 0.0f) * deltaTime);
		for (Particle& particle : m_Particles) {
			particle.Velocity = (particle.Velocity + gravity * deltaTime) * damping;
			particle.Position += particle.Velocity * deltaTime;
			particle.Rotation += particle.AngularVelocity * deltaTime;
			particle.LifeTime -= deltaTime;

			if (ScaleOverLifetimeEnabled || ColorOverLifetimeEnabled) {
				const float age01 = particle.StartLifeTime > 0.0f
					? std::clamp(1.0f - particle.LifeTime / particle.StartLifeTime, 0.0f, 1.0f)
					: 1.0f;
				if (ScaleOverLifetimeEnabled) {
					particle.Size = particle.StartSize * ScaleOverLifetime.Evaluate(age01);
				}
				if (ColorOverLifetimeEnabled) {
					particle.Color = particle.StartColor * ColorOverLifetime.Evaluate(age01);
				}
			}
		}

		m_Particles.erase(std::remove_if(m_Particles.begin(), m_Particles.end(),
			[](const Particle& p) { return p.LifeTime <= 0.0f; }), m_Particles.end());
	}

	void ParticleSystem3DComponent::EmitInternal(size_t count, ParticleQualityLevel quality) {
		const uint32_t maxParticles = ParticleQualityPolicy::EffectiveLimit(MaxParticles, quality);
		if (count == 0 || m_Particles.size() >= maxParticles) return;
		count = std::min<size_t>(count, maxParticles - m_Particles.size());
		m_Particles.reserve(m_Particles.size() + count);

		const float radius = std::max(Radius, 0.0f);
		const float coneAngle = glm::radians(std::clamp(ConeAngle, 0.0f, 89.9f));
		const glm::mat3 basis(m_EmitterWorld);
		const bool world = SimulationSpace == ParticleSpace3D::World;

		for (size_t i = 0; i < count; ++i) {
			Vec3 position(0.0f);
			Vec3 direction(0.0f, 1.0f, 0.0f);
			switch (Shape) {
				case ParticleShape3D::Sphere:
				case ParticleShape3D::Hemisphere: {
					direction = RandomUnitVector();
					if (Shape == ParticleShape3D::Hemisphere) direction.y = std::abs(direction.y);
					// cbrt keeps the volume density uniform.
					position = direction * (EmitFromSurface ? radius : radius * std::cbrt(Random::NextFloat()));
					break;
				}
				case ParticleShape3D::Cone: {
					const float phi = Random::NextFloat(glm::two_pi<float>());
					const float t = EmitFromSurface ? 1.0f : std::sqrt(Random::NextFloat());
					position = Vec3(std::cos(phi), 0.0f, std::sin(phi)) * (radius * t);
					// Particles further out lean further, so the spray fills the cone like Unity's cone shape.
					const float tilt = coneAngle * t;
					direction = { std::sin(tilt) * std::cos(phi), std::cos(tilt), std::sin(tilt) * std::sin(phi) };
					break;
				}
				case ParticleShape3D::Box: {
					const Vec3 half = glm::abs(BoxSize) * 0.5f;
					position = {
						half.x > 0.0f ? Random::NextFloat(-half.x, half.x) : 0.0f,
						half.y > 0.0f ? Random::NextFloat(-half.y, half.y) : 0.0f,
						half.z > 0.0f ? Random::NextFloat(-half.z, half.z) : 0.0f,
					};
					break;
				}
			}

			if (world) {
				position = Vec3(m_EmitterWorld * glm::vec4(position, 1.0f));
				const Vec3 worldDirection = basis * direction;
				const float length = glm::length(worldDirection);
				direction = length > 1e-6f ? worldDirection / length : Vec3(0.0f, 1.0f, 0.0f);
			}

			Particle particle;
			particle.Position = position;
			particle.Velocity = direction * Vary(Speed, SpeedVariation);
			particle.StartLifeTime = std::max(Vary(LifeTime, LifeTimeVariation), 0.001f);
			particle.LifeTime = particle.StartLifeTime;
			particle.StartSize = std::max(Vary(Size, SizeVariation), 0.0f);
			particle.Size = particle.StartSize;
			particle.Rotation = RandomRotation ? Random::NextFloat(360.0f) : 0.0f;
			particle.AngularVelocity = AngularVelocity;
			particle.StartColor = UseRandomColors ? Random::NextColor() : Color;
			particle.Color = particle.StartColor;
			if (ScaleOverLifetimeEnabled) particle.Size = particle.StartSize * ScaleOverLifetime.Evaluate(0.0f);
			if (ColorOverLifetimeEnabled) particle.Color = particle.StartColor * ColorOverLifetime.Evaluate(0.0f);
			m_Particles.push_back(particle);
		}
	}

}
