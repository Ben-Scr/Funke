#include <doctest/doctest.h>

#include "Assets/AssetRegistry.hpp"
#include "Serialization/File.hpp"
#include "Serialization/FileStamp.hpp"
#include "Serialization/Path.hpp"

#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Components/UI/GridLayoutGroupComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Physics3D/Collider3DComponents.hpp"
#include "Components/Physics3D/Rigidbody3DComponent.hpp"
#include "Core/Application.hpp"
#include "Core/Engine.hpp"
#include "Core/Window.hpp"
#include "Jobs/ParallelFor.hpp"
#include "Profiling/Profiler.hpp"
#include "Physics/Physics2D.hpp"
#include "Physics/PhysicsSystem2D.hpp"
#include "Physics3D/PhysicsWorld3D.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneManager.hpp"
#include "Systems/TransformHierarchySystem.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"
#include "Systems/UILayoutSystem.hpp"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <locale>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace {

	using namespace Funke;
	using Clock = std::chrono::steady_clock;

	int Setting(const char* name, int fallback) {
		const char* value = std::getenv(name);
		return value ? std::stoi(value) : fallback;
	}

	struct BenchmarkCase {
		std::string Name;
		int Entities = 0;
		int Workers = 0;
		int Operations = 1;
		std::function<void()> Run;
		std::function<void()> Validate;
	};

	bool Selected(const std::string& name) {
		const char* prefix = std::getenv("FUNKE_CPU_BENCH_PREFIX");
		return !prefix || name.starts_with(prefix);
	}

	void Measure(std::ofstream& raw, const BenchmarkCase& test, int repeat, int warmups, int samples) {
		if (!Selected(test.Name)) return;
		const double epoch = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
		const auto begin = Clock::now();
		for (int i = 0; i < warmups; ++i) test.Run();
		const double warmupSeconds = std::chrono::duration<double>(Clock::now() - begin).count();
		test.Validate();
		std::vector<double> times;
		times.reserve(samples);
		for (int i = 0; i < samples; ++i) {
			const auto start = Clock::now();
			test.Run();
			times.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
		}
		test.Validate();
		test.Run();
		test.Validate();
		const double wallSeconds = std::chrono::duration<double>(Clock::now() - begin).count();
		for (size_t i = 0; i < times.size(); ++i)
			raw << test.Name << ',' << repeat << ',' << i << ',' << times[i] << ','
				<< test.Entities << ',' << test.Workers << ',' << test.Operations << ','
				<< epoch << ',' << wallSeconds << ',' << warmupSeconds << '\n';
	}

	class PhysicsBenchmarkApplication final : public Application {
	public:
		explicit PhysicsBenchmarkApplication(std::function<void(Scene&)> run) : m_Run(std::move(run)) {}
		ApplicationConfig GetConfiguration() const override {
			auto config = ApplicationConfig::Headless();
			config.EnablePhysics2D = true;
			config.EnableAudio = false;
			config.EnableScripting = false;
			config.EnablePackageHost = false;
			config.JobSystemWorkerCount = 4;
			return config;
		}
		void ConfigureScenes() override {
			SceneManager::RegisterScene("Performance physics").WithoutDefaultCamera2D().SetAsStartupScene();
		}
		void Start() override {
			struct QuitOnExit { ~QuitOnExit() { Application::Quit(); } } quit;
			Application::SetIsPlaying(false);
			Profiler::SetPanelVisible(false);
			Profiler::SetBackgroundTracking(false);
			auto* scene = SceneManager::GetActiveScene();
			REQUIRE(scene != nullptr);
			m_Run(*scene);
		}
	private:
		std::function<void(Scene&)> m_Run;
	};

	void MeasurePhysics2D(std::ofstream& raw, int repeat, int warmups, int samples) {
		for (int count : { 100, 2048 }) for (const std::string mode : { "sync", "step", "queries" })
		for (int percent : { 0, 1, 100 }) {
			if (mode == "queries" && percent != 0) continue;
			BenchmarkCase test{ "physics-2d-" + mode + "-" + std::to_string(count) + "-changed-" + std::to_string(percent), count, 4 };
			if (!Selected(test.Name)) continue;
			PhysicsBenchmarkApplication app([&](Scene& scene) {
				std::vector<Entity> entities;
				for (int i = 0; i < count; ++i) {
					auto entity = scene.CreateEntity("Body");
					entity.GetComponent<Transform2DComponent>().SetPosition({ static_cast<float>(i * 2), 0 });
					auto& body = entity.AddComponent<Rigidbody2DComponent>();
					body.SetGravityScale(0);
					body.SetLinearDrag(0);
					entity.AddComponent<BoxCollider2DComponent>();
					entities.push_back(entity);
				}
				PhysicsSystem2D physics;
				for (int i = 0; i < 120; ++i) physics.FixedUpdate(1.0f / 60);
				REQUIRE(PhysicsSystem2D::GetMainPhysicsWorld().GetResourceCounts().bodyCount == count);
				for (auto entity : entities) REQUIRE_FALSE(entity.GetComponent<Rigidbody2DComponent>().IsAwake());
				const int changed = count * percent / 100;
				if (mode == "step") for (int i = 0; i < changed; ++i)
					entities[i].GetComponent<Rigidbody2DComponent>().SetVelocity({ 0, 1 });
				int tick = 0, hits = 0;
				bool correctHits = false;
				test.Operations = mode == "queries" ? count : 1;
				test.Run = [&] {
					++tick;
					if (mode == "sync") {
						for (int i = 0; i < changed; ++i)
							entities[i].GetComponent<Transform2DComponent>().SetPosition({ static_cast<float>(i * 2), static_cast<float>(tick & 1) });
						physics.Update();
					}
					else if (mode == "step") physics.FixedUpdate(1.0f / 60);
					else {
						hits = 0;
						correctHits = true;
						for (int i = 0; i < count; ++i) {
							const auto hit = Physics2D::Raycast({ static_cast<float>(i * 2), -2 }, { 0, 1 }, 4);
							if (hit) ++hits;
							correctHits &= hit && hit->entity == entities[i].GetHandle() && hit->scene == &scene;
						}
					}
				};
				test.Validate = [&] {
					bool correct = true;
					for (int i = 0; i < count; ++i) {
						const float expected = i >= changed ? 0.0f : mode == "step" ? tick / 60.0f : static_cast<float>(tick & 1);
						const auto& body = entities[i].GetComponent<Rigidbody2DComponent>();
						const auto& transform = entities[i].GetComponent<Transform2DComponent>();
						correct &= body.IsValid() && std::abs(body.GetPosition().y - expected) < 0.005f;
						correct &= std::abs(transform.Position.y - expected) < 0.005f;
						if (mode == "step") correct &= body.IsAwake() == (i < changed);
					}
					REQUIRE(correct);
					if (mode == "queries") { REQUIRE(hits == count); REQUIRE(correctHits); }
				};
				Measure(raw, test, repeat, warmups, samples);
			});
			REQUIRE(app.Run() == 0);
		}
	}

	void MeasurePhysics3D(std::ofstream& raw, int repeat, int warmups, int samples) {
		for (int count : { 100, 1000 }) for (const std::string mode : { "sync", "step", "queries" })
		for (int percent : { 0, 1, 100 }) {
			if (mode == "queries" && percent != 0) continue;
			BenchmarkCase test{ "physics-3d-" + mode + "-" + std::to_string(count) + "-changed-" + std::to_string(percent), count,
				static_cast<int>(std::clamp(std::max(2u, std::thread::hardware_concurrency()) - 1u, 1u, 8u)) };
			if (!Selected(test.Name)) continue;
			auto scene = Scene::CreateDetachedScene("Performance Jolt");
			std::vector<Entity> entities;
			for (int i = 0; i < count; ++i) {
				auto entity = scene->CreateEmptyEntity();
				entity.AddComponent<Transform3DComponent>().Position = { static_cast<float>(i * 2), 0, 0 };
				entity.AddComponent<BoxCollider3DComponent>();
				auto& body = entity.AddComponent<Rigidbody3DComponent>();
				body.LinearDrag = 0;
				body.Interpolate = false;
				entities.push_back(entity);
			}
			PhysicsWorld3D world;
			world.SetGravity(Vec3(0));
			Transform3DHierarchySystem::Propagate(*scene);
			world.Simulate(*scene, 1.0f / 60);
			REQUIRE(world.GetBodyCount() == count);
			for (auto entity : entities) world.Sleep(entity.GetHandle());
			const int changed = count * percent / 100;
			if (mode == "step") for (int i = 0; i < changed; ++i) world.SetLinearVelocity(entities[i].GetHandle(), { 0, 1, 0 });
			int tick = 0, hits = 0;
			bool correctHits = false;
			test.Operations = mode == "queries" ? count : 1;
			test.Run = [&] {
				++tick;
				if (mode == "sync") {
					for (int i = 0; i < changed; ++i) entities[i].GetComponent<Transform3DComponent>().Position.y = static_cast<float>(tick & 1);
					Transform3DHierarchySystem::Propagate(*scene);
					world.SyncFromScene(*scene, 1.0f / 60);
				}
				else if (mode == "step") {
					Transform3DHierarchySystem::Propagate(*scene);
					world.Simulate(*scene, 1.0f / 60);
					world.DispatchContacts([](const Collision3D&, CollisionPhase3D) {});
				}
				else {
					hits = 0;
					correctHits = true;
					for (int i = 0; i < count; ++i) {
						RaycastHit3D hit;
						const bool found = world.Raycast({ static_cast<float>(i * 2), -2, 0 }, { 0, 1, 0 }, 4, hit);
						if (found) ++hits;
						correctHits &= found && hit.Entity == entities[i].GetHandle();
					}
				}
			};
			test.Validate = [&] {
				bool correct = true;
				for (int i = 0; i < count; ++i) {
					const float expected = i >= changed ? 0.0f : mode == "step" ? tick / 60.0f : static_cast<float>(tick & 1);
					const auto& transform = entities[i].GetComponent<Transform3DComponent>();
					correct &= std::abs(transform.Position.y - expected) < 0.005f;
					RaycastHit3D hit;
					correct &= world.Raycast({ static_cast<float>(i * 2), expected - 2, 0 }, { 0, 1, 0 }, 4, hit)
						&& hit.Entity == entities[i].GetHandle() && std::abs(hit.Point.y - (expected - 0.5f)) < 0.01f;
					if (mode == "step") correct &= world.IsSleeping(entities[i].GetHandle()) == (i >= changed);
				}
				REQUIRE(correct);
				if (mode == "queries") { REQUIRE(hits == count); REQUIRE(correctHits); }
			};
			Measure(raw, test, repeat, warmups, samples);
		}
	}
}

TEST_CASE("CPU performance benchmark validates measured work" * doctest::skip()) {
	const char* output = std::getenv("FUNKE_CPU_BENCH_OUTPUT");
	REQUIRE(output != nullptr);
	const int samples = Setting("FUNKE_CPU_BENCH_SAMPLES", 200);
	const int repeats = Setting("FUNKE_CPU_BENCH_REPEATS", 3);
	const int warmups = Setting("FUNKE_CPU_BENCH_WARMUPS", 20);
	REQUIRE(samples >= 1);
	REQUIRE(repeats >= 1);
	REQUIRE(warmups >= 1);
	std::ofstream raw(std::filesystem::path(output) / "samples.csv");
	raw.imbue(std::locale::classic());
	raw << std::setprecision(17) << "case,repeat,sample,cpu_ms,entities,workers,operations,started_epoch,wall_seconds,warmup_seconds\n";
	REQUIRE(raw.good());
	struct Cleanup {
		~Cleanup() {
			JobSystem::Shutdown();
			Profiler::Shutdown();
			Window::ClearUIRegion();
		}
	} cleanup;
	Profiler::Initialize();
	Profiler::SetSamplingHz(0);
	Profiler::SetTrackingSpan(200);
	Profiler::SetPanelVisible(false);
	Profiler::SetBackgroundTracking(false);
	Window::SetUIRegion(0, 0, 1920, 1080);

	for (int repeat = 0; repeat < repeats; ++repeat) {
		for (int count : { 100, 1000, 10000 }) {
			auto scene = Scene::CreateDetachedScene("Performance transforms");
			std::vector<Entity> entities;
			entities.reserve(count);
			for (int i = 0; i < count; ++i) {
				auto entity = scene->CreateEmptyEntity();
				entity.AddComponent<Transform2DComponent>();
				entities.push_back(entity);
			}
			TransformHierarchySystem::Propagate(*scene);
			for (int percent : { 0, 1, 100 }) {
				const int changed = count * percent / 100;
				int tick = 0;
				BenchmarkCase test{ "transform-flat-" + std::to_string(count) + "-dirty-" + std::to_string(percent), count };
				test.Run = [&] {
					++tick;
					for (int i = 0; i < changed; ++i)
						entities[i].GetComponent<Transform2DComponent>().SetPosition({ static_cast<float>(tick & 1), 0 });
					TransformHierarchySystem::Propagate(*scene);
				};
				test.Validate = [&] {
					REQUIRE(TransformHierarchySystem::GetLastPropagationStats(*scene).TransformsVisited == changed);
					if (changed) REQUIRE(entities[changed - 1].GetComponent<Transform2DComponent>().Position.x == static_cast<float>(tick & 1));
				};
				Measure(raw, test, repeat, warmups, samples);
			}
		}
		for (int dimension : { 2, 3 }) for (int shape : { 0, 1, 2 }) for (int count : { 1000, 10000 }) {
			auto scene = Scene::CreateDetachedScene("Performance hierarchy");
			std::vector<Entity> entities;
			std::vector<int> parents;
			entities.reserve(count);
			parents.reserve(count);
			for (int i = 0; i < count; ++i) {
				auto entity = scene->CreateEmptyEntity();
				if (dimension == 2) entity.AddComponent<Transform2DComponent>();
				else entity.AddComponent<Transform3DComponent>();
				const int parent = i == 0 || shape == 0 ? -1 : shape == 1 ? (i - 1) / 8 : i % 512 == 0 ? -1 : i - 1;
				if (parent >= 0) entity.SetParent(entities[parent]);
				parents.push_back(parent);
				entities.push_back(entity);
			}
			auto propagate = [&] {
				if (dimension == 2) TransformHierarchySystem::Propagate(*scene);
				else Transform3DHierarchySystem::Propagate(*scene);
			};
			propagate();
			for (int percent : { 0, 1, 100 }) {
				const int changed = count * percent / 100;
				int tick = 0;
				BenchmarkCase test{ "hierarchy-" + std::to_string(dimension) + "d-shape-" + std::to_string(shape)
					+ "-" + std::to_string(count) + "-dirty-" + std::to_string(percent), count };
				test.Run = [&] {
					++tick;
					for (int i = count - changed; i < count; ++i) {
						if (dimension == 2) entities[i].GetComponent<Transform2DComponent>().SetPosition({ static_cast<float>(tick & 1), 0 });
						else entities[i].GetComponent<Transform3DComponent>().Position.x = static_cast<float>(tick & 1);
					}
					propagate();
				};
				test.Validate = [&] {
					const auto visited = dimension == 2 ? TransformHierarchySystem::GetLastPropagationStats(*scene).TransformsVisited
						: Transform3DHierarchySystem::GetLastPropagationStats(*scene).WorldMatricesRecomputed;
					REQUIRE(visited == changed);
					float expected = 0;
					for (int i = count - 1; i >= 0; i = parents[i])
						expected += dimension == 2 ? entities[i].GetComponent<Transform2DComponent>().LocalPosition.x
							: entities[i].GetComponent<Transform3DComponent>().Position.x;
					const float actual = dimension == 2 ? entities.back().GetComponent<Transform2DComponent>().Position.x
						: entities.back().GetComponent<Transform3DComponent>().GetWorldPosition().x;
					REQUIRE(actual == doctest::Approx(expected));
				};
				Measure(raw, test, repeat, warmups, samples);
				for (auto entity : entities) {
					if (dimension == 2) entity.GetComponent<Transform2DComponent>().SetPosition({ 0, 0 });
					else entity.GetComponent<Transform3DComponent>().Position.x = 0;
				}
				propagate();
			}
		}
		for (int count : { 100, 1000, 5000 }) {
			auto scene = Scene::CreateDetachedScene("Performance layout");
			std::vector<Entity> entities;
			for (int i = 0; i < count; ++i) {
				auto entity = scene->CreateEmptyEntity();
				entity.AddComponent<RectTransform2DComponent>();
				entities.push_back(entity);
			}
			ComputeUILayout(*scene);
			ComputeUILayout(*scene);
			for (bool changed : { false, true }) {
				int tick = 0;
				BenchmarkCase test{ "layout-roots-" + std::to_string(count) + (changed ? "-one-change" : "-idle"), count };
				test.Run = [&] {
					++tick;
					if (changed) entities.front().GetComponent<RectTransform2DComponent>().SizeDelta.x = 100.0f + (tick & 1);
					ComputeUILayout(*scene);
				};
				test.Validate = [&] {
					const auto& first = entities.front().GetComponent<RectTransform2DComponent>();
					REQUIRE(first.ResolvedValid);
					REQUIRE(first.ResolvedMax.x - first.ResolvedMin.x == doctest::Approx(first.SizeDelta.x));
					REQUIRE(entities.back().GetComponent<RectTransform2DComponent>().ResolvedValid);
				};
				Measure(raw, test, repeat, warmups, samples);
			}
		}
		for (bool grid : { false, true }) for (int count : { 100, 1000, 5000 }) {
			auto scene = Scene::CreateDetachedScene("Performance nested layout");
			std::vector<Entity> entities;
			std::vector<int> parents;
			for (int i = 0; i <= count; ++i) {
				auto entity = scene->CreateEmptyEntity();
				entity.AddComponent<RectTransform2DComponent>().SizeDelta = { 100, 100 };
				const int parent = i == 0 ? -1 : grid ? 0 : (i - 1) / 8;
				if (parent >= 0) entity.SetParent(entities[parent]);
				parents.push_back(parent);
				entities.push_back(entity);
			}
			if (grid) {
				entities.front().GetComponent<RectTransform2DComponent>().SizeDelta = { 1000, 1000 };
				auto& layout = entities.front().AddComponent<GridLayoutGroupComponent>();
				layout.Constraint = GridLayoutConstraint::FixedColumnCount;
				layout.ConstraintCount = 10;
				layout.CellSize = { 20, 20 };
				layout.Spacing = { 2, 2 };
			}
			ComputeUILayout(*scene);
			ComputeUILayout(*scene);
			for (bool changed : { false, true }) {
				int tick = 0;
				BenchmarkCase test{ std::string("layout-") + (grid ? "grid-" : "tree-")
					+ std::to_string(count) + (changed ? "-one-change" : "-idle"), count + 1 };
				test.Run = [&] {
					++tick;
					if (changed) {
						if (grid) entities.front().GetComponent<GridLayoutGroupComponent>().CellSize = { 20.0f + (tick & 1), 20.0f + (tick & 1) };
						else entities.front().GetComponent<RectTransform2DComponent>().AnchoredPosition.x = static_cast<float>(tick & 1);
					}
					ComputeUILayout(*scene);
				};
				test.Validate = [&] {
					const auto& root = entities.front().GetComponent<RectTransform2DComponent>();
					bool correct = root.ResolvedValid;
					for (int i = 1; i <= count; ++i) {
						const auto& rect = entities[i].GetComponent<RectTransform2DComponent>();
						correct &= rect.ResolvedValid;
						if (grid) {
							const auto cell = entities.front().GetComponent<GridLayoutGroupComponent>().CellSize;
							correct &= rect.SizeDelta == cell;
							correct &= rect.ResolvedMin.x == doctest::Approx(root.ResolvedMin.x + ((i - 1) % 10) * (cell.x + 2));
							correct &= rect.ResolvedMax.y == doctest::Approx(root.ResolvedMax.y - ((i - 1) / 10) * (cell.y + 2));
						}
						else {
							float expected = 0;
							for (int ancestor = i; ancestor >= 0; ancestor = parents[ancestor])
								expected += entities[ancestor].GetComponent<RectTransform2DComponent>().AnchoredPosition.x;
							correct &= rect.ResolvedPivot.x == doctest::Approx(expected);
							correct &= rect.ResolvedMax.x - rect.ResolvedMin.x == doctest::Approx(100);
						}
					}
					REQUIRE(correct);
				};
				Measure(raw, test, repeat, warmups, samples);
			}
		}
		for (int workers : { 0, 2, 8 }) {
			JobSystem::Shutdown();
			if (workers) JobSystem::Initialize({ workers });
			for (int count : { 256, 4096, 65536 }) {
				std::vector<uint64_t> values(count);
				for (int mode : { 0, 1, 2 }) {
					BenchmarkCase test{ "jobs-" + std::to_string(workers) + "-items-" + std::to_string(count) + "-mode-" + std::to_string(mode), 0, workers, count };
					uint64_t tick = 0;
					test.Run = [&] {
						++tick;
						auto body = [&](size_t lo, size_t hi) {
							for (size_t i = lo; i < hi; ++i) values[i] = i + tick;
						};
						if (mode == 0) body(0, count);
						else ParallelFor(0, count, body, mode == 1 ? count : 0);
					};
					test.Validate = [&] {
						REQUIRE(std::accumulate(values.begin(), values.end(), uint64_t{ 0 }) ==
							static_cast<uint64_t>(count) * (count - 1) / 2 + count * tick);
					};
					Measure(raw, test, repeat, warmups, samples);
				}
			}
		}
		JobSystem::Shutdown();
#ifdef FUNKE_PROFILER_ENABLED
		for (int mode : { 0, 1, 2 }) {
			Profiler::SetPanelVisible(mode != 0);
			Profiler::SetSamplingHz(mode == 2 ? 60 : 0);
			BenchmarkCase test{ "profiler-push-mode-" + std::to_string(mode), 0, 0, 10000 };
			test.Run = [&] {
				for (int i = 0; i < test.Operations; ++i) Profiler::PushSample("Physics", static_cast<float>(i % 101));
			};
			test.Validate = [&] {
				REQUIRE(Profiler::Find("Physics") != nullptr);
				if (mode) REQUIRE(Profiler::Find("Physics")->Count > 0);
			};
			Measure(raw, test, repeat, warmups, samples);
		}
		Profiler::SetPanelVisible(false);
#endif
	}
	for (int repeat = 0; repeat < repeats; ++repeat) {
		MeasurePhysics2D(raw, repeat, warmups, samples);
		MeasurePhysics3D(raw, repeat, warmups, samples);
	}
	if (const char* assetRoot = std::getenv("FUNKE_CPU_ASSET_ROOT")) {
		const auto root = Path::FromUtf8(assetRoot);
		for (int repeat = 0; repeat < repeats; ++repeat) for (int count : { 1, 256 }) {
			std::vector<std::string> paths;
			for (int i = 0; i < count; ++i) {
				paths.push_back(Path::ToUtf8(root / (count == 1 ? "large.tga" : "small-" + std::to_string(i) + ".tga")));
				REQUIRE(File::Exists(paths.back()));
				REQUIRE(File::Exists(AssetRegistry::GetMetaPath(paths.back())));
			}
			for (int mode = 0; mode < 5; ++mode) {
				BenchmarkCase test{ "asset-io-mode-" + std::to_string(mode) + "-files-" + std::to_string(count), 0, 0, count };
				int valid = 0;
				test.Run = [&] {
					valid = 0;
					for (const auto& path : paths) switch (mode) {
					case 0: valid += File::Exists(path); break;
					case 1: valid += FileStamp::Read(path).Exists; break;
					case 2: valid += FileStamp::Read(AssetRegistry::GetMetaPath(path)).Exists; break;
					case 3: valid += AssetRegistry::ReadTextureMeta(path).Sprites.empty(); break;
					case 4: valid += Path::CanonicalIdentity(Path::FromUtf8(path)) == Path::FromUtf8(path); break;
					}
				};
				test.Validate = [&] { REQUIRE(valid == count); };
				Measure(raw, test, repeat, warmups, samples);
			}
		}
	}
	REQUIRE(raw.good());
	std::ofstream done(std::filesystem::path(output) / "complete.txt");
	done << Engine::GetVersionLong();
	REQUIRE(done.good());
	std::ofstream compiler(std::filesystem::path(output) / "compiler.txt");
#ifdef _MSC_FULL_VER
	compiler << "MSVC " << _MSC_FULL_VER;
#else
	compiler << __VERSION__;
#endif
	REQUIRE(compiler.good());
}
