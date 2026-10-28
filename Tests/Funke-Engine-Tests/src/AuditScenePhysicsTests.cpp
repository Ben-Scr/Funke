#include <doctest/doctest.h>

#include "Components/General/Transform2DComponent.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Core/Application.hpp"
#include "Jobs/JobSystem.hpp"
#include "Physics/PhysicsSystem2D.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/SceneSystem.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/PrefabTemplateCache.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Systems/TransformHierarchySystem.hpp"

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Funke;

namespace {
	ApplicationConfig AuditConfiguration(bool physics) {
		ApplicationConfig config = ApplicationConfig::Headless();
		config.EnablePhysics2D = physics;
		config.EnableAudio = false;
		config.EnableScripting = false;
		config.EnablePackageHost = false;
		config.JobSystemWorkerCount = 1;
		return config;
	}

	class PhysicsAuditApplication final : public Application {
	public:
		explicit PhysicsAuditApplication(std::function<void(Scene&)> test) : m_Test(std::move(test)) {}
		ApplicationConfig GetConfiguration() const override { return AuditConfiguration(true); }
		void ConfigureScenes() override {
			SceneManager::RegisterScene("Physics Audit").WithoutDefaultCamera2D().SetAsStartupScene();
		}
		void Start() override {
			struct QuitOnExit { ~QuitOnExit() { Application::Quit(); } } quit;
			Application::SetIsPlaying(false);
			Scene* scene = SceneManager::GetActiveScene();
			REQUIRE(scene != nullptr);
			m_Test(*scene);
		}
	private:
		std::function<void(Scene&)> m_Test;
	};

	template <typename TCollider>
	void CheckColliderReset(Scene& scene, const char* name) {
		Entity entity = scene.CreateEntity("Collider Reset");
		auto& collider = entity.AddComponent<TCollider>();
		collider.SetFriction(0.85f);
		collider.SetBounciness(0.75f);
		collider.SetDensity(5.0f);
		collider.SetLayer(8);
		collider.SetCollisionMask(16);
		collider.SetGroupIndex(-7);
		collider.SetRegisterContacts(true);
		REQUIRE(SceneSerializer::ResetComponent(scene, entity.GetHandle(), name));
		const TCollider defaults;
		CHECK(collider.GetFriction() == doctest::Approx(defaults.GetFriction()));
		CHECK(collider.GetBounciness() == doctest::Approx(defaults.GetBounciness()));
		CHECK(collider.GetDensity() == doctest::Approx(defaults.GetDensity()));
		CHECK(collider.GetLayer() == defaults.GetLayer());
		CHECK(collider.GetCollisionMask() == defaults.GetCollisionMask());
		CHECK(collider.GetGroupIndex() == defaults.GetGroupIndex());
		CHECK(collider.CanRegisterContacts() == defaults.CanRegisterContacts());
	}

	enum class AuditPhase { Update, FixedUpdate, PreRender };
	struct LifecycleAuditState {
		AuditPhase Phase = AuditPhase::Update;
		bool Suspend = false;
		bool Interrupted = false;
		bool Destroyed = false;
		int CallsAfterDestroy = 0;
	};

	class InterruptingAuditSystem final : public SceneSystem {
	public:
		explicit InterruptingAuditSystem(LifecycleAuditState* state) : m_State(state) {}
		void Update(Scene& scene) override { Interrupt(scene, AuditPhase::Update); }
		void FixedUpdate(Scene& scene) override { Interrupt(scene, AuditPhase::FixedUpdate); }
		void OnPreRender(Scene& scene) override { Interrupt(scene, AuditPhase::PreRender); }
	private:
		void Interrupt(Scene& scene, AuditPhase phase) {
			if (m_State->Interrupted || phase != m_State->Phase) return;
			if (m_State->Suspend) {
				m_State->Interrupted = SceneManager::BeginLoadedSceneRecovery(SceneManager::GetLoadedScene(scene.GetName()).lock());
			}
			else {
				SceneManager::UnloadScene(scene.GetName());
				m_State->Interrupted = !scene.IsLoaded();
			}
			Application::Quit();
		}
		LifecycleAuditState* m_State;
	};

	class LaterAuditSystem final : public SceneSystem {
	public:
		explicit LaterAuditSystem(LifecycleAuditState* state) : m_State(state) {}
		void OnDestroy(Scene&) override { m_State->Destroyed = true; }
		void Update(Scene&) override { Record(); }
		void FixedUpdate(Scene&) override { Record(); }
		void OnPreRender(Scene&) override { Record(); }
	private:
		void Record() { if (m_State->Destroyed) ++m_State->CallsAfterDestroy; }
		LifecycleAuditState* m_State;
	};

	class LifecycleAuditApplication final : public Application {
	public:
		explicit LifecycleAuditApplication(LifecycleAuditState& state) : m_State(state) {}
		ApplicationConfig GetConfiguration() const override {
			auto config = AuditConfiguration(false);
			config.UseTargetFrameRateForMainLoop = true;
			return config;
		}
		void ConfigureScenes() override {
			SceneManager::RegisterScene("Lifecycle Audit")
				.WithoutDefaultCamera2D()
				.AddSystem<InterruptingAuditSystem>(&m_State)
				.AddSystem<LaterAuditSystem>(&m_State)
				.SetAsStartupScene();
		}
		void Start() override {
			Application::SetIsPlaying(true);
			Time::SetFixedDeltaTime(0.001f);
		}
		void Update() override { if (++m_Frames > 40) Application::Quit(); }
	private:
		LifecycleAuditState& m_State;
		int m_Frames = 0;
	};
}

TEST_CASE("Audit: parent transforms keep static child colliders aligned") {
	PhysicsAuditApplication app([](Scene& scene) {
		Entity parent = scene.CreateEntity("Parent");
		Entity boxChild = scene.CreateEntity("Box");
		boxChild.SetParent(parent);
		boxChild.GetComponent<Transform2DComponent>().SetPosition({ 2.0f, 0.0f });
		boxChild.AddComponent<BoxCollider2DComponent>();
		Entity circleChild = scene.CreateEntity("Circle");
		circleChild.SetParent(parent);
		circleChild.GetComponent<Transform2DComponent>().SetPosition({ -2.0f, 0.0f });
		circleChild.AddComponent<CircleCollider2DComponent>();
		PhysicsSystem2D physics;
		TransformHierarchySystem::Propagate(scene);
		physics.Update();
		REQUIRE_FALSE(boxChild.GetComponent<Transform2DComponent>().IsDirty());
		REQUIRE_FALSE(circleChild.GetComponent<Transform2DComponent>().IsDirty());

		auto& parentTransform = parent.GetComponent<Transform2DComponent>();
		parentTransform.SetPosition({ 10.0f, 3.0f });
		parentTransform.SetRotation(1.57079632679f);
		parentTransform.SetScale({ 2.0f, 3.0f });
		TransformHierarchySystem::Propagate(scene);
		physics.Update();
		const auto& box = boxChild.GetComponent<BoxCollider2DComponent>();
		const auto& circle = circleChild.GetComponent<CircleCollider2DComponent>();
		CHECK(box.GetBodyPosition().x == doctest::Approx(10.0f));
		CHECK(box.GetBodyPosition().y == doctest::Approx(7.0f));
		CHECK(circle.GetBodyPosition().x == doctest::Approx(10.0f));
		CHECK(circle.GetBodyPosition().y == doctest::Approx(-1.0f));
		CHECK(box.GetRotationRadiant() == doctest::Approx(1.57079632679f));
		CHECK(box.GetScale().x == doctest::Approx(2.0f));
		CHECK(box.GetScale().y == doctest::Approx(3.0f));
		CHECK(circle.GetRadius() == doctest::Approx(1.5f));
	});
	CHECK(app.Run() == 0);
}

TEST_CASE("Audit: cached physics prefabs preserve density and automatic mass") {
	bool circle = false;
	SUBCASE("box") {}
	SUBCASE("circle") { circle = true; }
	PhysicsAuditApplication app([circle](Scene& scene) {
		auto sourceScene = Scene::CreateDetachedScene("Density Prefab");
		Entity source = sourceScene->CreateEntity("Body");
		source.AddComponent<Rigidbody2DComponent>();
		if (circle) source.AddComponent<CircleCollider2DComponent>().SetDensity(5.0f);
		else source.AddComponent<BoxCollider2DComponent>().SetDensity(5.0f);
		constexpr uint64_t prefabId = 901105;
		auto& cache = PrefabTemplateCache::Get();
		cache.CaptureFromLive(prefabId, *sourceScene, source.GetHandle(), 0);
		REQUIRE(cache.Find(prefabId));
		REQUIRE(cache.Find(prefabId)->bakeable);
		const EntityHandle handle = cache.Hydrate(prefabId, scene);
		REQUIRE(scene.IsValid(handle));
		const Collider2D& collider = circle
			? static_cast<const Collider2D&>(scene.GetComponent<CircleCollider2DComponent>(handle))
			: static_cast<const Collider2D&>(scene.GetComponent<BoxCollider2DComponent>(handle));
		CHECK(collider.GetDensity() == doctest::Approx(5.0f));
		const auto& body = scene.GetComponent<Rigidbody2DComponent>(handle);
		CHECK_FALSE(body.IsMassOverridden());
		const float area = circle ? 3.14159265359f * 0.25f : 1.0f;
		CHECK(body.GetMass() == doctest::Approx(area * 5.0f));
		cache.Invalidate(prefabId);
	});
	CHECK(app.Run() == 0);
}

TEST_CASE("Audit: component reset restores text image and collider defaults") {
	auto scene = Scene::CreateDetachedScene("Component Reset Audit");
	Entity textEntity = scene->CreateEntity("Text");
	auto& text = textEntity.AddComponent<TextRendererComponent>();
	text.Text = "Changed";
	text.FontAssetId = UUID(1234);
	text.FontSize = 72.0f;
	text.Color = { 0.1f, 0.2f, 0.3f, 0.4f };
	text.HAlign = TextAlignment::Right;
	text.VAlign = TextVerticalAlignment::Bottom;
	text.WrapMode = TextWrapMode::Character;
	text.LetterSpacing = 3.0f;
	text.LineSpacing = 4.0f;
	text.Margin = { 1.0f, 2.0f, 3.0f, 4.0f };
	text.SortingOrder = 12;
	text.SortingLayer = 2;
	REQUIRE(SceneSerializer::ResetComponent(*scene, textEntity.GetHandle(), "TextRenderer"));
	const TextRendererComponent textDefaults;
	CHECK(text.Text == textDefaults.Text);
	CHECK(text.FontAssetId == textDefaults.FontAssetId);
	CHECK(text.FontSize == textDefaults.FontSize);
	CHECK(text.Color == textDefaults.Color);
	CHECK(text.HAlign == textDefaults.HAlign);
	CHECK(text.VAlign == textDefaults.VAlign);
	CHECK(text.WrapMode == textDefaults.WrapMode);
	CHECK(text.LetterSpacing == textDefaults.LetterSpacing);
	CHECK(text.LineSpacing == textDefaults.LineSpacing);
	CHECK(text.Margin == textDefaults.Margin);
	CHECK(text.SortingOrder == textDefaults.SortingOrder);
	CHECK(text.SortingLayer == textDefaults.SortingLayer);

	Entity imageEntity = scene->CreateEntity("Image");
	auto& image = imageEntity.AddComponent<ImageComponent>();
	image.SortingOrder = 19;
	image.SortingLayer = 3;
	image.FilterMode = Filter::Point;
	REQUIRE(SceneSerializer::ResetComponent(*scene, imageEntity.GetHandle(), "Image"));
	const ImageComponent imageDefaults;
	CHECK(image.SortingOrder == imageDefaults.SortingOrder);
	CHECK(image.SortingLayer == imageDefaults.SortingLayer);
	CHECK(image.FilterMode == imageDefaults.FilterMode);
	CheckColliderReset<BoxCollider2DComponent>(*scene, "BoxCollider2D");
	CheckColliderReset<CircleCollider2DComponent>(*scene, "CircleCollider2D");
	CheckColliderReset<PolygonCollider2DComponent>(*scene, "PolygonCollider2D");
}

TEST_CASE("Audit: Rigidbody paste and reset restore automatic mass") {
	bool paste = false;
	SUBCASE("reset") {}
	SUBCASE("paste auto mass") { paste = true; }
	PhysicsAuditApplication app([paste](Scene& scene) {
		Entity target = scene.CreateEntity("Target");
		target.AddComponent<Rigidbody2DComponent>();
		auto& box = target.AddComponent<BoxCollider2DComponent>();
		box.SetScale({ 2.0f, 3.0f }, scene);
		box.SetDensity(5.0f);
		auto& body = target.GetComponent<Rigidbody2DComponent>();
		body.SetMass(100.0f);
		REQUIRE(body.GetMass() == doctest::Approx(100.0f));
		if (paste) {
			Entity source = scene.CreateEntity("Automatic");
			source.AddComponent<Rigidbody2DComponent>();
			const Json::Value value = SceneSerializer::SerializeComponent(scene, source.GetHandle(), "Rigidbody2D");
			REQUIRE(SceneSerializer::DeserializeComponent(scene, target.GetHandle(), "Rigidbody2D", value));
		}
		else {
			REQUIRE(SceneSerializer::ResetComponent(scene, target.GetHandle(), "Rigidbody2D"));
		}
		CHECK_FALSE(body.IsMassOverridden());
		CHECK(body.GetMass() == doctest::Approx(30.0f));
		box.SetDensity(2.0f);
		body.ApplyMassFromState();
		CHECK(body.GetMass() == doctest::Approx(12.0f));
	});
	CHECK(app.Run() == 0);
}

TEST_CASE("Audit: Box2D uses existing workers only for large awake populations") {
	PhysicsAuditApplication app([](Scene& scene) {
		PhysicsSystem2D physics;
		auto& world = PhysicsSystem2D::GetMainPhysicsWorld();
		std::vector<Entity> bodies;
		const auto addBodies = [&](size_t count) {
			while (bodies.size() < count) {
				const size_t index = bodies.size();
				Entity body = scene.CreateEntity();
				body.GetComponent<Transform2DComponent>().SetPosition({
					static_cast<float>(index % 32) * 2.0f, 100.0f + static_cast<float>(index / 32) * 2.0f });
				body.AddComponent<Rigidbody2DComponent>().SetBodyType(BodyType::Dynamic);
				body.AddComponent<BoxCollider2DComponent>();
				bodies.push_back(body);
			}
		};
		const auto step = [&] { physics.FixedUpdate(1.0f / 60.0f); };
		JobSystem::Shutdown();
		addBodies(64);
		step();
		CHECK(world.GetWorkerCount() == 1);
		CHECK(bodies.front().GetComponent<Rigidbody2DComponent>().GetPosition().y < 100.0f);

		JobSystem::Initialize(JobSystemSpec{ 4 });
		step();
		CHECK(world.GetWorkerCount() == 1);
		CHECK(JobSystem::GetWorkerCount() == 4);
		addBodies(1024);
		const float beforeParallel = bodies.front().GetComponent<Rigidbody2DComponent>().GetPosition().y;
		step();
		CHECK(world.GetWorkerCount() == 4);
		CHECK(JobSystem::GetWorkerCount() == 4);
		CHECK(bodies.front().GetComponent<Rigidbody2DComponent>().GetPosition().y < beforeParallel);

		JobSystem::Reconfigure(2);
		step();
		CHECK(world.GetWorkerCount() == 2);
		CHECK(JobSystem::GetWorkerCount() == 2);
		for (size_t i = 0; i < 600; ++i) bodies[i].SetEnabled(false);
		step();
		CHECK(world.GetWorkerCount() == 1);
		for (size_t i = 0; i < 600; ++i) bodies[i].SetEnabled(true);
		step();
		CHECK(world.GetWorkerCount() == 2);

		JobSystem::Shutdown();
		const float beforeSerial = bodies.back().GetComponent<Rigidbody2DComponent>().GetPosition().y;
		step();
		CHECK(world.GetWorkerCount() == 1);
		CHECK(bodies.back().GetComponent<Rigidbody2DComponent>().GetPosition().y < beforeSerial);
	});
	CHECK(app.Run() == 0);
}

TEST_CASE("Audit: scene phases stop after teardown from a system callback") {
	LifecycleAuditState state;
	SUBCASE("update unload") {}
	SUBCASE("fixed update unload") { state.Phase = AuditPhase::FixedUpdate; }
	SUBCASE("pre-render unload") { state.Phase = AuditPhase::PreRender; }
	SUBCASE("update suspension") { state.Suspend = true; }
	LifecycleAuditApplication app(state);
	CHECK(app.Run() == 0);
	CHECK(state.Interrupted);
	CHECK(state.Destroyed);
	CHECK(state.CallsAfterDestroy == 0);
}
