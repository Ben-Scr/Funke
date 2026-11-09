#include <doctest/doctest.h>

#include "Components/General/Transform2DComponent.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Physics/Joint2DComponents.hpp"
#include "Scripting/NativeEntityCommandBuffer.hpp"
#include "Core/Application.hpp"
#include "Events/ScenePostStartEvent.hpp"
#include "Jobs/JobSystem.hpp"
#include "Physics/PhysicsSystem2D.hpp"
#include "Physics/Physics2D.hpp"
#include <limits>
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

	class RecoveryDestroyFailure final : public SceneSystem {
	public:
		explicit RecoveryDestroyFailure(bool* fail) : m_Fail(fail) {}
		void OnDestroy(Scene&) override { if (*m_Fail) throw std::runtime_error("expected recovery failure"); }
	private:
		bool* m_Fail;
	};

	class PhysicsAuditApplication final : public Application {
	public:
		explicit PhysicsAuditApplication(std::function<void(Scene&)> test, bool* failDestroy = nullptr) : m_Test(std::move(test)), m_FailDestroy(failDestroy) {}
		ApplicationConfig GetConfiguration() const override { return AuditConfiguration(true); }
		void ConfigureScenes() override {
			auto& definition = SceneManager::RegisterScene("Physics Audit").WithoutDefaultCamera2D().SetAsStartupScene();
			if (m_FailDestroy) definition.AddSystem<RecoveryDestroyFailure>(m_FailDestroy);
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
		bool* m_FailDestroy;
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

TEST_CASE("Audit: sleeping Box2D bodies still read back direct teleports") {
	bool parented = false;
	SUBCASE("root body") {}
	SUBCASE("parented body") { parented = true; }
	PhysicsAuditApplication app([parented](Scene& scene) {
		Entity parent = scene.CreateEntity("Parent");
		Entity body = scene.CreateEntity("Sleeping body");
		parent.GetComponent<Transform2DComponent>().SetPosition({ 10.0f, 0.0f });
		if (parented) body.SetParent(parent);
		auto& rigidbody = body.AddComponent<Rigidbody2DComponent>();
		rigidbody.SetBodyType(BodyType::Dynamic);
		rigidbody.SetGravityScale(0.0f);
		body.AddComponent<BoxCollider2DComponent>();
		TransformHierarchySystem::Propagate(scene);
		PhysicsSystem2D physics;
		physics.FixedUpdate(1.0f / 60.0f);
		auto& liveBody = body.GetComponent<Rigidbody2DComponent>();
		REQUIRE(liveBody.IsValid());
		// Box2D state belongs to the engine DLL, not the test executable's static copy.
		for (int frame = 0; frame < 120 && liveBody.IsAwake(); ++frame) physics.FixedUpdate(1.0f / 60.0f);
		REQUIRE_FALSE(liveBody.IsAwake());
		liveBody.SetPosition({ 25.0f, 4.0f });
		liveBody.SetRotation(0.5f);
		physics.FixedUpdate(1.0f / 60.0f);
		const auto& transform = body.GetComponent<Transform2DComponent>();
		CHECK(transform.Position.x == doctest::Approx(25.0f));
		CHECK(transform.Position.y == doctest::Approx(4.0f));
		CHECK(transform.Rotation == doctest::Approx(0.5f));
		CHECK(transform.LocalPosition.x == doctest::Approx(parented ? 15.0f : 25.0f));
		CHECK_FALSE(liveBody.IsAwake());
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

TEST_CASE("Backlog raycasts accept infinite distance and reject invalid coordinates") {
	PhysicsAuditApplication app([](Scene& scene) {
		auto box = scene.CreateEntity("Ray target");
		box.GetComponent<Transform2DComponent>().SetPosition({5.0f, 0.0f});
		box.AddComponent<BoxCollider2DComponent>();
		for (float distance : {10.0f, std::numeric_limits<float>::infinity()}) {
			const auto hit = Physics2D::Raycast({0,0}, {1,0}, distance);
			REQUIRE(hit.has_value());
			CHECK(hit->distance == doctest::Approx(4.5f));
		}
		CHECK_FALSE(Physics2D::Raycast({0,0}, {0,0}, 10));
		CHECK_FALSE(Physics2D::Raycast({0,0}, {1,0}, std::numeric_limits<float>::quiet_NaN()));
		CHECK_FALSE(Physics2D::Raycast({0,0}, {std::numeric_limits<float>::infinity(),0}, 10));
	});
	CHECK(app.Run() == 0);
}

TEST_CASE("Backlog geometry edits refresh auto mass and preserve pinned mass") {
	PhysicsAuditApplication app([](Scene& scene) {
		auto box = scene.CreateEntity("Box");
		box.AddComponent<Rigidbody2DComponent>().SetBodyType(BodyType::Dynamic);
		auto& shape = box.AddComponent<BoxCollider2DComponent>();
		auto& body = box.GetComponent<Rigidbody2DComponent>();
		const float initial = body.GetMass();
		const float initialInertia = body.GetInertia();
		shape.SetScale({2,2}, scene);
		CHECK(body.GetMass() == doctest::Approx(initial * 4));
		const float centeredInertia = body.GetInertia();
		CHECK(centeredInertia == doctest::Approx(initialInertia * 16));
		shape.SetCenter({1,2}, scene);
		CHECK(body.GetLocalCenterOfMass().x == doctest::Approx(1));
		CHECK(body.GetLocalCenterOfMass().y == doctest::Approx(2));
		CHECK(body.GetInertia() == doctest::Approx(centeredInertia));
		body.SetMass(7);
		shape.SetCenter({1,2}, scene);
		shape.SetScale({3,3}, scene);
		CHECK(body.GetMass() == doctest::Approx(7));
		auto circle = scene.CreateEntity("Circle");
		circle.AddComponent<Rigidbody2DComponent>().SetBodyType(BodyType::Dynamic);
		auto& disk = circle.AddComponent<CircleCollider2DComponent>();
		const float before = circle.GetComponent<Rigidbody2DComponent>().GetMass();
		disk.SetRadius(1, scene);
		CHECK(circle.GetComponent<Rigidbody2DComponent>().GetMass() == doctest::Approx(before * 4));
	});
	CHECK(app.Run() == 0);
}

TEST_CASE("Backlog multipart overlaps return each entity once") {
	PhysicsAuditApplication app([](Scene& scene) {
		auto entity = scene.CreateEntity("Multipart");
		auto& polygon = entity.AddComponent<PolygonCollider2DComponent>();
		const std::vector<Vec2> square{{-1,-1},{1,-1},{1,1},{-1,1}};
		polygon.SetPaths({square, square}, scene);
		CHECK(Physics2D::OverlapCircleAllRefs({0,0}, 3).size() == 1);
		CHECK(Physics2D::OverlapBoxAllRefs({0,0}, {3,3}, 0).size() == 1);
		CHECK(Physics2D::OverlapPolygonAllRefs({0,0}, square).size() == 1);
		CHECK(Physics2D::ContainsPointAllRefs({0,0}).size() == 1);
		SceneManager::RegisterScene("Overlap second").WithoutDefaultCamera2D();
		auto result = SceneManager::TryLoadSceneAdditive("Overlap second");
		REQUIRE(result.Succeeded());
		auto otherScene = result.LoadedScene.lock();
		REQUIRE(otherScene);
		auto other = otherScene->CreateEntity("Same local handle");
		REQUIRE(other.GetHandle() == entity.GetHandle());
		other.AddComponent<PolygonCollider2DComponent>().SetPaths({square, square}, *otherScene);
		for (const auto& hits : {Physics2D::OverlapCircleAllRefs({0,0}, 3),
			Physics2D::OverlapBoxAllRefs({0,0}, {3,3}, 0),
			Physics2D::OverlapPolygonAllRefs({0,0}, square), Physics2D::ContainsPointAllRefs({0,0})}) {
			REQUIRE(hits.size() == 2);
			CHECK(hits[0].entity == hits[1].entity);
			CHECK(hits[0].scene != hits[1].scene);
		}
	});
	CHECK(app.Run() == 0);
}

namespace {
	struct BacklogTickSystem : SceneSystem {
		int* Count;
		bool Unload;
		BacklogTickSystem(int* count, bool unload) : Count(count), Unload(unload) {}
		void Update(Scene& scene) override {
			++*Count;
			if (Unload) SceneManager::UnloadScene(scene.GetName());
		}
	};
	class BacklogSceneApplication final : public Application {
	public:
		int Counts[3]{};
		std::vector<int> Events;
		ApplicationConfig GetConfiguration() const override { return AuditConfiguration(false); }
		void ConfigureScenes() override {
			SubscribeEvent<ScenePostStartEvent>([n = 0, this](ScenePostStartEvent&) mutable { Events.push_back(++n); });
			for (int i = 0; i < 3; ++i) SceneManager::RegisterScene("Backlog" + std::to_string(i))
				.WithoutDefaultCamera2D().AddSystem<BacklogTickSystem>(&Counts[i], i == 0).SetAsStartupScene();
		}
		void Start() override { Application::SetIsPlaying(true); }
		void Update() override { Application::Quit(); }
	};
}

TEST_CASE("Backlog scene self-unload does not skip the next scene and event state persists") {
	BacklogSceneApplication app;
	CHECK(app.Run() == 0);
	CHECK(app.Counts[0] == 1);
	CHECK(app.Counts[1] == 1);
	CHECK(app.Counts[2] == 1);
	CHECK(app.Events == std::vector<int>{1,2,3});
}

namespace {
	struct PhaseMutationState {
		AuditPhase Phase;
		int Mode;
		int Calls[4]{};
		bool IgnoreFollowingFixedTicks = false;
		bool Complete = false;
	};
	class PhaseMutationSystem final : public SceneSystem {
	public:
		PhaseMutationSystem(PhaseMutationState* state, int index) : m_State(state), m_Index(index) {}
		void Update(Scene&) override { Tick(*m_State, m_Index, AuditPhase::Update); }
		void FixedUpdate(Scene&) override { Tick(*m_State, m_Index, AuditPhase::FixedUpdate); }
		void OnPreRender(Scene&) override { Tick(*m_State, m_Index, AuditPhase::PreRender); }
	private:
		static void Tick(PhaseMutationState& state, int index, AuditPhase phase) {
			if (phase != state.Phase || state.IgnoreFollowingFixedTicks || (state.Complete && index != 3)) return;
			++state.Calls[index];
			if (index == 0 && state.Mode == 0) SceneManager::UnloadScene("Phase0");
			if (index == 0 && state.Mode == 1) SceneManager::UnloadScene("Phase1");
			if (index == 1 && state.Mode == 2) SceneManager::UnloadScene("Phase0");
			if (index == 0 && state.Mode == 3) CHECK(SceneManager::TryLoadSceneAdditive("Phase3").Succeeded());
			if (index == 2) { state.Complete = true; Application::Quit(); }
		}
		PhaseMutationState* m_State;
		int m_Index;
	};
	class PhaseMutationApplication final : public Application {
	public:
		explicit PhaseMutationApplication(PhaseMutationState& state) : m_State(state) {}
		ApplicationConfig GetConfiguration() const override {
			auto config = AuditConfiguration(false);
			config.UseTargetFrameRateForMainLoop = true;
			return config;
		}
		void ConfigureScenes() override {
			for (int i = 0; i < 4; ++i) {
				auto& scene = SceneManager::RegisterScene("Phase" + std::to_string(i))
					.WithoutDefaultCamera2D().AddSystem<PhaseMutationSystem>(&m_State, i);
				if (i < 3) scene.SetAsStartupScene();
			}
		}
		void Start() override { Application::SetIsPlaying(true); Time::SetFixedDeltaTime(0.001f); }
		void FixedUpdate() override {
			// Quit completes at frame end; later catch-up ticks have their own valid scene snapshots.
			if (m_State.Phase == AuditPhase::FixedUpdate && m_State.Complete)
				m_State.IgnoreFollowingFixedTicks = true;
		}
		void Update() override { if (++m_Frames > 40) Application::Quit(); }
	private:
		PhaseMutationState& m_State;
		int m_Frames = 0;
	};
}

TEST_CASE("Backlog scene mutations preserve every phase snapshot") {
	const float previousStep = Time::GetFixedDeltaTime();
	for (auto phase : {AuditPhase::Update, AuditPhase::FixedUpdate, AuditPhase::PreRender}) for (int mode = 0; mode < 4; ++mode) {
		CAPTURE(mode);
		PhaseMutationState state{phase, mode};
		PhaseMutationApplication app(state);
		CHECK(app.Run() == 0);
		CHECK(state.Complete);
		CHECK(state.Calls[0] == 1);
		CHECK(state.Calls[1] == (mode == 1 ? 0 : 1));
		CHECK(state.Calls[2] == 1);
		CHECK(state.Calls[3] == 0);
	}
	Time::SetFixedDeltaTime(previousStep);
}

namespace {
	template<typename TCollider>
	void CheckRecoveryResources(Scene& scene, bool withRigidbody, bool& failDestroy) {
		auto& world = PhysicsSystem2D::GetMainPhysicsWorld();
		const auto baseline = world.GetResourceCounts();
		Entity entity = scene.CreateEntity("Retained collider");
		entity.AddComponent<TCollider>();
		if constexpr (std::is_same_v<TCollider, PolygonCollider2DComponent>) {
			entity.GetComponent<TCollider>().SetPoints({ {0, 0}, {3, 0}, {3, 1}, {1, 1}, {1, 3}, {0, 3} }, scene);
			REQUIRE(entity.GetComponent<TCollider>().GetShapePolygons().size() > 1);
		}
		Entity connected = Entity::Null;
		b2JointId joint = b2_nullJointId;
		if (withRigidbody) {
			entity.AddComponent<Rigidbody2DComponent>().SetGravityScale(0.0f);
			connected = scene.CreateEntity("Connected body");
			connected.AddComponent<Rigidbody2DComponent>().SetGravityScale(0.0f);
			entity.AddComponent<RevoluteJoint2DComponent>().ConnectedBody = connected.GetHandle();
			Application::SetIsPlaying(true);
			PhysicsSystem2D physics;
			physics.FixedUpdate(1.0f / 60.0f);
			joint = entity.GetComponent<RevoluteJoint2DComponent>().m_JointId;
			REQUIRE(entity.GetComponent<RevoluteJoint2DComponent>().IsCreated());
			auto& body = entity.GetComponent<Rigidbody2DComponent>();
			body.SetPosition({ 8.0f, 9.0f });
			body.SetRotation(0.7f);
			body.SetVelocity({ 2.0f, 3.0f });
			body.SetAngularVelocity(0.4f);
		}
		const auto bodyId = entity.GetComponent<TCollider>().m_BodyId;
		const auto shapeId = entity.GetComponent<TCollider>().m_ShapeId;
		const auto counts = world.GetResourceCounts();
		auto retained = SceneManager::GetLoadedScene(scene.GetName()).lock();
		REQUIRE(retained);
		for (int iteration = 0; iteration < 50; ++iteration) {
			CAPTURE(iteration);
			const bool expectedSuspend = !failDestroy;
			CHECK(SceneManager::SuspendLoadedSceneForRelease(retained) == expectedSuspend);
			failDestroy = false;
			REQUIRE(SceneManager::RollbackLoadedSceneRecovery(retained));
			REQUIRE(SceneManager::ActivateSuspendedScene(retained, false));
			REQUIRE(SceneManager::CommitSuspendedSceneActivation(retained));
			const auto current = world.GetResourceCounts();
			CHECK(current.bodyCount == counts.bodyCount);
			CHECK(current.shapeCount == counts.shapeCount);
			CHECK(current.jointCount == counts.jointCount);
			CHECK(B2_ID_EQUALS(entity.GetComponent<TCollider>().m_BodyId, bodyId));
			CHECK(B2_ID_EQUALS(entity.GetComponent<TCollider>().m_ShapeId, shapeId));
			if (withRigidbody) {
				const auto& body = entity.GetComponent<Rigidbody2DComponent>();
				CHECK(body.GetPosition().x == doctest::Approx(8.0f));
				CHECK(body.GetPosition().y == doctest::Approx(9.0f));
				CHECK(body.GetRotation() == doctest::Approx(0.7f));
				CHECK(body.GetVelocity().x == doctest::Approx(2.0f));
				CHECK(body.GetVelocity().y == doctest::Approx(3.0f));
				CHECK(body.GetAngularVelocity() == doctest::Approx(0.4f));
				CHECK(B2_ID_EQUALS(entity.GetComponent<RevoluteJoint2DComponent>().m_JointId, joint));
			}
		}
		entity.Destroy();
		if (connected.IsValid()) connected.Destroy();
		const auto finalCounts = world.GetResourceCounts();
		CHECK(finalCounts.bodyCount == baseline.bodyCount);
		CHECK(finalCounts.shapeCount == baseline.shapeCount);
		CHECK(finalCounts.jointCount == baseline.jointCount);
	}

	template<typename TFirst, typename TSecond>
	void CheckColliderConflict(Scene& scene) {
		auto& world = PhysicsSystem2D::GetMainPhysicsWorld();
		for (int api = 0; api < 5; ++api) {
			CAPTURE(api);
			Entity entity = scene.CreateEntity("Conflicting collider");
			entity.AddComponent<TFirst>();
			const auto body = entity.GetComponent<TFirst>().m_BodyId;
			const auto shape = entity.GetComponent<TFirst>().m_ShapeId;
			const auto counts = world.GetResourceCounts();
			if (api == 0) CHECK_THROWS_AS(entity.AddComponent<TSecond>(), FunkeException);
			else if (api == 1) CHECK_THROWS_AS(scene.AddComponent<TSecond>(entity.GetHandle()), FunkeException);
			else if (api == 2) CHECK_THROWS_AS(ComponentUtils::AddComponent<TSecond>(scene.GetRegistry(), entity.GetHandle()), FunkeException);
			else {
				NativeEntityCommandBuffer commands;
				auto reference = NativeEntityCommandBuffer::Existing(entity);
				if (api == 3) {
					commands.AddComponent<TSecond>(reference);
					commands.Playback(scene);
				}
				else {
					commands.AddComponent<TSecond>(reference, [](TSecond&) { FAIL("conflicting configure callback ran"); });
					CHECK_THROWS_AS(commands.Playback(scene), FunkeException);
				}
			}
			CHECK_FALSE(entity.HasComponent<TSecond>());
			CHECK(entity.GetComponent<TFirst>().IsValid());
			CHECK(B2_ID_EQUALS(entity.GetComponent<TFirst>().m_BodyId, body));
			CHECK(B2_ID_EQUALS(entity.GetComponent<TFirst>().m_ShapeId, shape));
			CHECK(world.GetResourceCounts().bodyCount == counts.bodyCount);
			CHECK(world.GetResourceCounts().shapeCount == counts.shapeCount);
			CHECK(&entity.AddComponent<TFirst>() == &entity.GetComponent<TFirst>());
			entity.AddComponent<Rigidbody2DComponent>();
			CHECK(B2_ID_EQUALS(entity.GetComponent<Rigidbody2DComponent>().GetBodyHandle(), body));
			entity.Destroy();
		}
	}
}

TEST_CASE("Memory: recovery retains collider bodies shapes and joints for 50 cycles") {
	bool withRigidbody = false;
	SUBCASE("static colliders") {}
	SUBCASE("rigidbody colliders") { withRigidbody = true; }
	bool failDestroy = true;
	PhysicsAuditApplication app([&](Scene& scene) {
		CheckRecoveryResources<BoxCollider2DComponent>(scene, withRigidbody, failDestroy);
		CheckRecoveryResources<CircleCollider2DComponent>(scene, withRigidbody, failDestroy);
		CheckRecoveryResources<PolygonCollider2DComponent>(scene, withRigidbody, failDestroy);
	}, &failDestroy);
	CHECK(app.Run() == 0);
}

TEST_CASE("Memory: public collider additions enforce all directed conflicts before allocation") {
	PhysicsAuditApplication app([](Scene& scene) {
		CheckColliderConflict<BoxCollider2DComponent, CircleCollider2DComponent>(scene);
		CheckColliderConflict<CircleCollider2DComponent, BoxCollider2DComponent>(scene);
		CheckColliderConflict<BoxCollider2DComponent, PolygonCollider2DComponent>(scene);
		CheckColliderConflict<PolygonCollider2DComponent, BoxCollider2DComponent>(scene);
		CheckColliderConflict<CircleCollider2DComponent, PolygonCollider2DComponent>(scene);
		CheckColliderConflict<PolygonCollider2DComponent, CircleCollider2DComponent>(scene);
	});
	CHECK(app.Run() == 0);
}

TEST_CASE("Memory: suspended physics initialization rejects foreign copied runtime handles") {
	PhysicsAuditApplication app([](Scene& scene) {
		Entity source = scene.CreateEntity("Foreign owner");
		source.AddComponent<PolygonCollider2DComponent>().SetPoints(
			{ {0, 0}, {3, 0}, {3, 1}, {1, 1}, {1, 3}, {0, 3} }, scene);
		source.AddComponent<Rigidbody2DComponent>().SetGravityScale(0.0f);
		const auto body = source.GetComponent<PolygonCollider2DComponent>().m_BodyId;
		const auto shape = source.GetComponent<PolygonCollider2DComponent>().m_ShapeId;
		SceneManager::RegisterScene("Foreign handles").WithoutDefaultCamera2D().OnLoad([source](Scene& target) {
			Entity copy = target.CreateEntity("Copied runtime handles");
			copy.AddComponent<PolygonCollider2DComponent>(source.GetComponent<PolygonCollider2DComponent>());
			copy.AddComponent<Rigidbody2DComponent>(source.GetComponent<Rigidbody2DComponent>());
		});
		REQUIRE(SceneManager::TryLoadSceneAdditiveSuspended("Foreign handles").Succeeded());
		auto target = SceneManager::GetLoadedScene("Foreign handles").lock();
		REQUIRE(target);
		REQUIRE(SceneManager::ActivateSuspendedScene(target, false));
		REQUIRE(SceneManager::CommitSuspendedSceneActivation(target));
		const auto handle = target->GetRegistry().view<PolygonCollider2DComponent>().front();
		const auto& copied = target->GetComponent<PolygonCollider2DComponent>(handle);
		CHECK(copied.IsValid());
		CHECK_FALSE(B2_ID_EQUALS(copied.m_BodyId, body));
		CHECK_FALSE(B2_ID_EQUALS(copied.m_ShapeId, shape));
		CHECK(source.GetComponent<PolygonCollider2DComponent>().IsValid());
		CHECK(B2_ID_EQUALS(source.GetComponent<PolygonCollider2DComponent>().m_ShapeId, shape));
		CHECK(copied.GetShapePolygons().size() == source.GetComponent<PolygonCollider2DComponent>().GetShapePolygons().size());
		SceneManager::UnloadScene("Foreign handles");
		CHECK(source.GetComponent<PolygonCollider2DComponent>().IsValid());
	});
	CHECK(app.Run() == 0);
}

TEST_CASE("Memory: fresh suspended colliders initialize while retained sleeping bodies remain asleep") {
	PhysicsAuditApplication app([](Scene& scene) {
		auto blueprint = Scene::CreateDetachedScene("Fresh physics");
		blueprint->CreateEntity("Fresh box").AddComponent<BoxCollider2DComponent>();
		blueprint->CreateEntity("Fresh circle").AddComponent<CircleCollider2DComponent>();
		blueprint->CreateEntity("Fresh polygon").AddComponent<PolygonCollider2DComponent>();
		const auto serialized = SceneSerializer::SerializeScene(*blueprint);
		SceneManager::RegisterScene("Fresh physics").WithoutDefaultCamera2D().OnLoadChecked([serialized](Scene& fresh) {
			return SceneSerializer::DeserializeScene(fresh, serialized, "Fresh physics");
		});
		const auto before = PhysicsSystem2D::GetMainPhysicsWorld().GetResourceCounts();
		REQUIRE(SceneManager::TryLoadSceneAdditiveSuspended("Fresh physics").Succeeded());
		auto fresh = SceneManager::GetLoadedScene("Fresh physics").lock();
		REQUIRE(fresh);
		CHECK(PhysicsSystem2D::GetMainPhysicsWorld().GetResourceCounts().bodyCount == before.bodyCount);
		REQUIRE(SceneManager::ActivateSuspendedScene(fresh, false));
		REQUIRE(SceneManager::CommitSuspendedSceneActivation(fresh));
		CHECK(PhysicsSystem2D::GetMainPhysicsWorld().GetResourceCounts().bodyCount == before.bodyCount + 3);
		CHECK(PhysicsSystem2D::GetMainPhysicsWorld().GetResourceCounts().shapeCount == before.shapeCount + 3);
		SceneManager::UnloadScene("Fresh physics");
		Entity entity = scene.CreateEntity("Sleeping retained body");
		entity.AddComponent<Rigidbody2DComponent>().SetGravityScale(0.0f);
		entity.AddComponent<BoxCollider2DComponent>();
		Application::SetIsPlaying(true);
		PhysicsSystem2D physics;
		for (int frame = 0; frame < 120 && entity.GetComponent<Rigidbody2DComponent>().IsAwake(); ++frame)
			physics.FixedUpdate(1.0f / 60.0f);
		REQUIRE_FALSE(entity.GetComponent<Rigidbody2DComponent>().IsAwake());
		auto retained = SceneManager::GetLoadedScene(scene.GetName()).lock();
		REQUIRE(SceneManager::SuspendLoadedSceneForRelease(retained));
		REQUIRE(SceneManager::RollbackLoadedSceneRecovery(retained));
		REQUIRE(SceneManager::ActivateSuspendedScene(retained, false));
		REQUIRE(SceneManager::CommitSuspendedSceneActivation(retained));
		CHECK_FALSE(entity.GetComponent<Rigidbody2DComponent>().IsAwake());
		entity.GetComponent<BoxCollider2DComponent>().SetEnabled(false);
		REQUIRE(SceneManager::SuspendLoadedSceneForRelease(retained));
		REQUIRE(SceneManager::RollbackLoadedSceneRecovery(retained));
		REQUIRE(SceneManager::ActivateSuspendedScene(retained, false));
		REQUIRE(SceneManager::CommitSuspendedSceneActivation(retained));
		CHECK_FALSE(entity.GetComponent<BoxCollider2DComponent>().IsEnabled());
	});
	CHECK(app.Run() == 0);
}
