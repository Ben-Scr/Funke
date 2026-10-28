#include <doctest/doctest.h>

#include "Components/General/HierarchyComponent.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "DevTools/RunAutomation.hpp"
#include "Scene/DelayedEntityDestruction.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneManager.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <vector>

using namespace Funke;

namespace {
	struct DestructionProbe {};

	struct DestructionObserver {
		std::vector<EntityHandle> Order;
		std::function<void(EntityHandle)> Callback;

		void OnDestroy(entt::registry&, EntityHandle entity) {
			Order.push_back(entity);
			if (Callback) Callback(entity);
		}
	};

	class DelayedDestructionApplication final : public Application {
	public:
		bool Completed = false;

		ApplicationConfig GetConfiguration() const override {
			ApplicationConfig config = ApplicationConfig::Headless();
			config.EnablePhysics2D = false;
			config.EnableAudio = false;
			config.EnableScripting = false;
			config.EnablePackageHost = false;
			config.JobSystemWorkerCount = 1;
			return config;
		}

		void ConfigureScenes() override {
			SceneManager::RegisterScene("Delayed destruction").WithoutDefaultCamera2D().SetAsStartupScene();
		}

		void Start() override {
			RunAutomation::SetFixedStepOverride(0.25f);
			Time::SetTimeScale(0.0f);
			Scene* scene = SceneManager::GetActiveScene();
			m_First = scene->CreateEmptyEntity();
			m_First.AddComponent<DestructionProbe>();
			m_Observer.Callback = [this, scene](EntityHandle entity) {
				if (entity != m_First.GetHandle()) return;
				m_FromCallback = scene->CreateEmptyEntity();
				scene->DestroyEntity(m_FromCallback.GetHandle(), 0.25f);
			};
			scene->GetRegistry().on_destroy<DestructionProbe>().connect<&DestructionObserver::OnDestroy>(m_Observer);
			scene->DestroyEntity(m_First.GetHandle(), 0.5f);
			Entity canceled = scene->CreateEmptyEntity();
			scene->DestroyEntity(canceled.GetHandle(), 0.125f);
			scene->DestroyEntity(canceled);
			m_Replacement = scene->CreateEmptyEntity();
		}

		void Update() override {
			++m_Frame;
			CHECK(m_Replacement.IsValid());
			if (m_Frame <= 5) CHECK(m_First.IsValid());
			if (m_Frame == 2) Time::SetTimeScale(0.5f);
			if (m_Frame == 6 || m_Frame == 7) {
				CHECK_FALSE(m_First.IsValid());
				CHECK(m_FromCallback.IsValid());
			}
			if (m_Frame == 8) {
				CHECK_FALSE(m_FromCallback.IsValid());
				CHECK(m_Observer.Order.size() == 1);
				Completed = true;
				Application::Quit();
			}
		}

		void OnQuit() override {
			RunAutomation::SetFixedStepOverride(0.0f);
			Time::SetTimeScale(1.0f);
			if (Scene* scene = SceneManager::GetActiveScene())
				scene->GetRegistry().on_destroy<DestructionProbe>().disconnect<&DestructionObserver::OnDestroy>(m_Observer);
		}

	private:
		Entity m_First = Entity::Null;
		Entity m_FromCallback = Entity::Null;
		Entity m_Replacement = Entity::Null;
		DestructionObserver m_Observer;
		int m_Frame = 0;
	};
}

TEST_CASE("Wide entity destruction preserves callback order and remaining sibling membership")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("Wide destruction");
	Entity root = scene->CreateEmptyEntity();
	std::vector<EntityHandle> children;
	constexpr size_t count = 4096;
	for (size_t i = 0; i < count; ++i) {
		Entity child = scene->CreateEmptyEntity();
		child.SetParent(root);
		child.AddComponent<DestructionProbe>();
		children.push_back(child.GetHandle());
	}
	root.AddComponent<DestructionProbe>();
	DestructionObserver observer;
	observer.Callback = [&](EntityHandle entity) {
		if (entity == root.GetHandle()) {
			CHECK(root.GetChildren().empty());
			return;
		}
		CHECK(root.IsValid());
		const auto remaining = root.GetChildren();
		CHECK(remaining.size() == count - observer.Order.size());
		CHECK(std::find(remaining.begin(), remaining.end(), entity) == remaining.end());
		if (!remaining.empty()) CHECK(scene->IsValid(remaining.back()));
		scene->DestroyEntity(entity);
		scene->DestroyEntity(root);
	};
	scene->GetRegistry().on_destroy<DestructionProbe>().connect<&DestructionObserver::OnDestroy>(observer);
	scene->DestroyEntity(root);
	children.push_back(root.GetHandle());
	CHECK(observer.Order == children);
	CHECK(scene->GetEntityCount() == 0);
	scene->GetRegistry().on_destroy<DestructionProbe>().disconnect<&DestructionObserver::OnDestroy>(observer);
}

TEST_CASE("Destruction repairs callback hierarchy edits and preserves surviving parent order")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("Destruction edits");
	Entity root = scene->CreateEmptyEntity();
	Entity surviving = scene->CreateEmptyEntity();
	Entity before = scene->CreateEmptyEntity();
	before.SetParent(surviving);
	std::vector<Entity> children;
	for (int i = 0; i < 5; ++i) {
		Entity child = scene->CreateEmptyEntity();
		child.SetParent(root);
		child.AddComponent<DestructionProbe>();
		children.push_back(child);
	}
	Entity after = scene->CreateEmptyEntity();
	after.SetParent(surviving);
	Entity newcomer = scene->CreateEmptyEntity();
	DestructionObserver observer;
	observer.Callback = [&](EntityHandle entity) {
		if (entity == children[0].GetHandle()) {
			children[0].SetParent(surviving);
			CHECK(surviving.GetChildren().size() == 2);
			newcomer.SetParent(root);
			CHECK_FALSE(newcomer.HasParent());
			children[2].SetParent(surviving);
			auto& list = root.GetComponent<HierarchyComponent>().Children;
			std::reverse(list.begin(), list.end());
		}
		if (entity == children[1].GetHandle()) {
			auto& list = root.GetComponent<HierarchyComponent>().Children;
			std::reverse(list.begin(), list.end());
		}
		CHECK(scene->IsValid(entity));
	};
	scene->GetRegistry().on_destroy<DestructionProbe>().connect<&DestructionObserver::OnDestroy>(observer);
	scene->DestroyEntity(root);
	std::vector<EntityHandle> expected;
	for (Entity child : children) expected.push_back(child.GetHandle());
	CHECK(observer.Order == expected);
	REQUIRE(surviving.GetChildren().size() == 2);
	CHECK(surviving.GetChildren()[0] == before.GetHandle());
	CHECK(surviving.GetChildren()[1] == after.GetHandle());
	CHECK(newcomer.IsValid());
	scene->GetRegistry().on_destroy<DestructionProbe>().disconnect<&DestructionObserver::OnDestroy>(observer);
}

TEST_CASE("ClearEntities preserves notifications while indexing all doomed parent lists")
{
	Application app;
	auto scene = Scene::CreateDetachedScene("Clear wide hierarchy");
	Entity root = scene->CreateEmptyEntity();
	for (int i = 0; i < 2048; ++i) {
		Entity child = scene->CreateEmptyEntity();
		child.SetParent(root);
		child.AddComponent<DestructionProbe>();
	}
	DestructionObserver observer;
	observer.Callback = [&](EntityHandle entity) {
		CHECK(root.IsValid());
		CHECK(root.GetChildren().size() == 2048 - observer.Order.size());
		scene->DestroyEntity(entity);
	};
	scene->GetRegistry().on_destroy<DestructionProbe>().connect<&DestructionObserver::OnDestroy>(observer);
	scene->ClearEntities();
	CHECK(observer.Order.size() == 2048);
	CHECK(scene->GetEntityCount() == 0);
	scene->GetRegistry().on_destroy<DestructionProbe>().disconnect<&DestructionObserver::OnDestroy>(observer);
}

TEST_CASE("Delayed entity destruction retains the earliest deadline and bounded queue entries")
{
	DelayedEntityDestruction queue;
	constexpr uint32_t count = 20000;
	for (uint32_t i = 0; i < count; ++i) queue.Schedule(static_cast<EntityHandle>(i), 100.0f);
	for (uint32_t i = 0; i < count; ++i) {
		queue.Schedule(static_cast<EntityHandle>(i), 50.0f);
		queue.Schedule(static_cast<EntityHandle>(i), 150.0f);
	}
	CHECK(queue.Size() == count);
	queue.Advance(49.0f);
	EntityHandle entity = entt::null;
	CHECK_FALSE(queue.PopDue(entity));
	queue.Advance(1.0f);
	for (uint32_t i = 0; i < count; ++i) {
		REQUIRE(queue.PopDue(entity));
		CHECK(entity == static_cast<EntityHandle>(i));
	}
	CHECK_FALSE(queue.PopDue(entity));
	CHECK(queue.Size() == 0);
}

TEST_CASE("Delayed destruction supports callback scheduling cancellation generations and paused time")
{
	DelayedEntityDestruction queue;
	const EntityHandle first = static_cast<EntityHandle>(1);
	const EntityHandle second = static_cast<EntityHandle>(2);
	const EntityHandle replacement = entt::entt_traits<EntityHandle>::next(first);
	queue.Schedule(first, 1.0f);
	queue.Schedule(second, 10.0f);
	queue.Advance(0.5f);
	queue.Schedule(second, 0.5f);
	queue.Schedule(second, 30.0f);
	queue.Cancel(first);
	queue.Schedule(replacement, 5.0f);
	EntityHandle entity = entt::null;
	queue.Advance(0.0f);
	CHECK_FALSE(queue.PopDue(entity));
	queue.Advance(0.5f);
	REQUIRE(queue.PopDue(entity));
	CHECK(entity == second);
	queue.Schedule(first, 0.5f);
	CHECK_FALSE(queue.PopDue(entity));
	queue.Advance(0.5f);
	REQUIRE(queue.PopDue(entity));
	CHECK(entity == first);
	CHECK_FALSE(queue.PopDue(entity));
	queue.Advance(4.0f);
	REQUIRE(queue.PopDue(entity));
	CHECK(entity == replacement);
	queue.Schedule(first, 1.0f);
	queue.Clear();
	queue.Advance(100.0f);
	CHECK_FALSE(queue.PopDue(entity));
}

TEST_CASE("Nonfinite delayed destruction input cannot poison deadline ordering")
{
	DelayedEntityDestruction queue;
	const EntityHandle first = static_cast<EntityHandle>(1);
	const EntityHandle second = static_cast<EntityHandle>(2);
	queue.Schedule(first, std::numeric_limits<float>::quiet_NaN());
	CHECK(queue.Size() == 0);
	queue.Schedule(first, std::numeric_limits<float>::infinity());
	queue.Schedule(second, 1.0f);
	queue.Advance(std::numeric_limits<float>::quiet_NaN());
	queue.Advance(std::numeric_limits<float>::infinity());
	queue.Advance(-1.0f);
	EntityHandle entity = entt::null;
	CHECK_FALSE(queue.PopDue(entity));
	queue.Advance(1.0f);
	REQUIRE(queue.PopDue(entity));
	CHECK(entity == second);
	CHECK_FALSE(queue.PopDue(entity));
	CHECK(queue.Size() == 1);
	queue.Schedule(first, 1.0f);
	queue.Advance(1.0f);
	REQUIRE(queue.PopDue(entity));
	CHECK(entity == first);
}

TEST_CASE("Scene delayed destruction uses scaled time and starts callback deadlines at the current tick")
{
	DelayedDestructionApplication application;
	CHECK(application.Run() == 0);
	CHECK(application.Completed);
}
