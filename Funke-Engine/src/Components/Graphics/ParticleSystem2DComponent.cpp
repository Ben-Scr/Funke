#include "pch.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"

#include "Math/Random.hpp"
#include "Math/Math.hpp"
#include "Math/VectorMath.hpp"
#include "Scene/Scene.hpp"

#include <Core/Time.hpp>
#include "Graphics/ParticleQualityPolicy.hpp"

#if FUNKE_WITH_APPLICATION
#include "Core/Application.hpp"
#endif

#include <cmath>

namespace Funke {
	namespace {
		Vec2 DirectionOrUp(const Vec2& direction) {
			return Vector::LengthSquared(direction) > 0.0001f ? Vector::Normalized(direction) : Vector::Up();
		}

		ParticleQualityLevel GetActiveParticleQuality() {
#if FUNKE_WITH_APPLICATION
			if (const Application* application = Application::GetInstance()) {
				return application->GetEffectiveGraphicsSettings().ParticleQuality;
			}
#endif
			return ParticleQualityLevel::High;
		}
	}

	Transform2DComponent& ParticleSystem2DComponent::GetTransform2D() {
		Transform2DComponent* transform = const_cast<Transform2DComponent*>(TryGetEmitterTransform());
		FNK_ASSERT(transform != nullptr, FunkeErrorCode::InvalidHandle, "Particle system emitter transform is no longer available");
		return *transform;
	}

	const Transform2DComponent& ParticleSystem2DComponent::GetTransform2D() const {
		const Transform2DComponent* transform = TryGetEmitterTransform();
		FNK_ASSERT(transform != nullptr, FunkeErrorCode::InvalidHandle, "Particle system emitter transform is no longer available");
		return *transform;
	}

	const Transform2DComponent* ParticleSystem2DComponent::TryGetEmitterTransform() const
	{
		if (!m_EmitterScene || m_EmitterEntity == entt::null || !m_EmitterScene->IsValid(m_EmitterEntity)) {
			return nullptr;
		}

		if (!m_EmitterScene->HasComponent<Transform2DComponent>(m_EmitterEntity)) {
			return nullptr;
		}

		return &m_EmitterScene->GetComponent<Transform2DComponent>(m_EmitterEntity);
	}

	void ParticleSystem2DComponent::ResetSimulation() {
		m_Particles.clear();
		m_EmitAccumulator = 0.0f;
		for (auto& burst : m_Bursts) {
			burst.TimeUntilNext = 0.0f;
		}
	}

	void ParticleSystem2DComponent::Update() {
		Update(Time::GetDeltaTime(), GetActiveParticleQuality());
	}

	void ParticleSystem2DComponent::Update(float deltaTime, ParticleQualityLevel quality) {
		if (quality == ParticleQualityLevel::Off) {
			m_Particles.clear();
			m_EmitAccumulator = 0.0f;
			return;
		}

		const uint32_t maxParticles = ParticleQualityPolicy::EffectiveLimit(
			RenderingSettings.MaxParticles, quality);
		if (m_Particles.size() > maxParticles) {
			m_Particles.erase(m_Particles.begin(),
				m_Particles.begin() + static_cast<std::ptrdiff_t>(m_Particles.size() - maxParticles));
		}
		if (deltaTime == 0.f) return;

		if (m_IsEmitting) {
			float toEmit = EmissionSettings.EmitOverTime
				* ParticleQualityPolicy::Density(quality) * deltaTime + m_EmitAccumulator;
			int emitCount = static_cast<int>(toEmit);
			m_EmitAccumulator = toEmit - emitCount;
			Emit(static_cast<size_t>(emitCount), quality);

			for (auto& burst : m_Bursts) {
				// Counts down from 0, so the burst fires on the first update after Play (instant
				// on spawn), then every Interval — instead of waiting one full Interval first.
				burst.TimeUntilNext -= deltaTime;
				if (burst.TimeUntilNext <= 0.f) {
					Emit(ParticleQualityPolicy::ScaleCount(burst.Count, quality), quality);
					burst.TimeUntilNext = burst.Interval;
				}
			}
		}
		if (m_IsSimulating) {
			const bool scaleOverLife = ParticleSettings.ScaleOverLifetime.Enabled;
			const bool colorOverLife = ParticleSettings.ColorOverLifetime.Enabled;
			for (auto& particle : m_Particles) {
				if (ParticleSettings.UseGravity) {
					particle.Velocity += ParticleSettings.Gravity * deltaTime;
				}

				particle.Transform.Position += particle.Velocity * deltaTime;

				if (scaleOverLife || colorOverLife) {
					const float age01 = particle.StartLifeTime > 0.f
						? std::clamp(1.0f - particle.LifeTime / particle.StartLifeTime, 0.0f, 1.0f)
						: 1.0f;
					if (scaleOverLife) {
						particle.Transform.Scale = particle.StartScale * ParticleSettings.ScaleOverLifetime.Evaluate(age01);
					}
					if (colorOverLife) {
						particle.Color = particle.StartColor * ParticleSettings.ColorOverLifetime.Gradient.Evaluate(age01);
					}
				}
			}

			m_Particles.erase(std::remove_if(m_Particles.begin(), m_Particles.end(),
				[deltaTime](Particle& p) {
					return (p.LifeTime -= deltaTime) <= 0.f;
				}), m_Particles.end());
		}
	}

	void ParticleSystem2DComponent::Emit(size_t count) {
		const ParticleQualityLevel quality = GetActiveParticleQuality();
		Emit(ParticleQualityPolicy::ScaleCount(count, quality), quality);
	}

	void ParticleSystem2DComponent::Emit(size_t count, ParticleQualityLevel quality) {
		if(!m_IsEmitting || count == 0)
			return;

		const uint32_t maxParticles = ParticleQualityPolicy::EffectiveLimit(
			RenderingSettings.MaxParticles, quality);

		if (m_Particles.size() >= maxParticles)
			return;

		if (m_Particles.capacity() < maxParticles) {
			m_Particles.reserve(maxParticles);
		}

		while (count > 0) {
			Particle particle;
			particle.LifeTime = ParticleSettings.LifeTime;
			particle.StartLifeTime = ParticleSettings.LifeTime;

			Vec2 position{ 0 };
			Vec2 scale = ParticleSettings.Scale;
			float rot{ 0.f };
			Vec2 velocityDirection = DirectionOrUp(ParticleSettings.MoveDirection);
			Vec2 velocity = velocityDirection * ParticleSettings.Speed;

			std::visit([&](auto const& s) {
				using T = std::decay_t<decltype(s)>;
				if constexpr (std::is_same_v<T, CircleParams>) {
					// Arc-restricted emission: angle in [0, Arc) degrees; Arc==360 reproduces
					// the full-circle distribution. In-circle uses r=R*sqrt(u) for uniform area.
					const float arcRad = std::clamp(s.Arc, 0.0f, 360.0f) * 0.01745329252f;
					const float theta = arcRad > 0.0f ? Random::NextFloat(arcRad) : 0.0f;
					// Safety net: a zero radius spawns everything at the centre (all particles
					// would then emit straight up). The editor clamps this too.
					const float safeRadius = std::max(s.Radius, 0.0001f);
					const float r = s.IsOnCircle ? safeRadius : safeRadius * std::sqrt(Random::NextFloat(0.0f, 1.0f));
					position = Vector::FromAngle(theta) * r;
					velocityDirection = DirectionOrUp(position);
					velocity = velocityDirection * ParticleSettings.Speed;
				}
				else if constexpr (std::is_same_v<T, SquareParams>) {
					const float halfWidth = std::abs(s.HalfExtends.x);
					const float halfHeight = std::abs(s.HalfExtends.y);
					position = Vec2(
						halfWidth > 0.0f ? Random::NextFloat(-halfWidth, halfWidth) : 0.0f,
						halfHeight > 0.0f ? Random::NextFloat(-halfHeight, halfHeight) : 0.0f);
				}
				else if constexpr (std::is_same_v<T, EdgeParams>) {
					const float halfLength = std::abs(s.Length) * 0.5f;
					position = Vec2(halfLength > 0.0f ? Random::NextFloat(-halfLength, halfLength) : 0.0f, 0.0f);
					velocityDirection = DirectionOrUp(ParticleSettings.MoveDirection);
					velocity = velocityDirection * ParticleSettings.Speed;
				}
				}, Shape);

			if (EmissionSettings.EmissionSpace == Space::World) {
				if (const Transform2DComponent* emitterTransform = TryGetEmitterTransform()) {
					const Vec2 worldPosition = emitterTransform->TransformPoint(position);
					if (std::holds_alternative<CircleParams>(Shape)) {
						velocityDirection = DirectionOrUp(worldPosition - emitterTransform->Position);
					}
					else {
						velocityDirection = DirectionOrUp(Vector::Rotated(velocityDirection, emitterTransform->Rotation));
					}
					position = worldPosition;
					velocity = velocityDirection * ParticleSettings.Speed;
				}
			}

			particle.Velocity = velocity;
			particle.Transform.Position = position;
			particle.Transform.Rotation = rot;
			particle.Transform.Scale = scale;
			particle.StartScale = scale;
			particle.Color = ParticleSettings.UseRandomColors ? Random::NextColor() : RenderingSettings.Color;
			particle.StartColor = particle.Color;
			m_Particles.push_back(particle);

			if (m_Particles.size() >= maxParticles)
				break;

			count--;
		}
	}
}
