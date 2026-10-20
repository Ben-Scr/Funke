// Camera choice for the 2D layer of the editor's game renders (Game View, `funke editor screenshot`, project thumbnail).

#include <doctest/doctest.h>

#include "Editor/GameView2DLayer.hpp"

#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Core/Application.hpp"
#include "Graphics/Renderer3D.hpp"
#include "Scene/Entity.hpp"
#include "Scene/EntityHelper.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <memory>

using namespace Funke;

namespace {
	glm::vec2 ToNdc(const glm::mat4& viewProjection, Vec2 world) {
		const glm::vec4 clip = viewProjection * glm::vec4(world.x, world.y, 0.0f, 1.0f);
		return glm::vec2(clip) / clip.w;
	}

	bool SameMatrix(const glm::mat4& a, const glm::mat4& b) {
		for (int column = 0; column < 4; ++column) {
			for (int row = 0; row < 4; ++row) {
				if (std::abs(a[column][row] - b[column][row]) > 1e-5f) return false;
			}
		}
		return true;
	}

	struct Layer2DResults {
		bool Ran = false;
		glm::mat4 Camera2DViewProjection{ 1.0f };
		glm::mat4 Camera3DViewProjection{ 1.0f };
		GameView2DLayer Mixed;
		GameView2DLayer Only3D;
		GameView2DLayer Neither;
	};

	// A live scene: the Camera 2D only gets its owner and matrices in a scene that isn't detached.
	class Layer2DApplication final : public Application {
	public:
		Layer2DResults Results;

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
			SceneManager::RegisterScene(k_SceneName).WithoutDefaultCamera2D().SetAsStartupScene();
		}

		void Start() override {
			if (std::shared_ptr<Scene> scene = SceneManager::GetLoadedScene(k_SceneName).lock()) {
				Camera2DComponent& camera2D = EntityHelper::CreateCamera2DEntity(*scene).GetComponent<Camera2DComponent>();
				camera2D.SetPosition({ 2.0f, 1.0f });
				camera2D.UpdateViewport();

				// Like the mixed test scene: a perspective Camera 3D 6 units in front of the sprites' z = 0 plane.
				const glm::mat4 cameraWorld = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.1f, 6.0f));
				const Camera3DView view3D = Renderer3D::MakeView(cameraWorld, Camera3DComponent{}, 16.0f / 9.0f);

				Results.Camera2DViewProjection = camera2D.GetViewProjectionMatrix();
				Results.Camera3DViewProjection = view3D.Projection * view3D.View;
				Results.Mixed = ResolveGameView2DLayer(&camera2D, &view3D);
				Results.Only3D = ResolveGameView2DLayer(nullptr, &view3D);
				Results.Neither = ResolveGameView2DLayer(nullptr, nullptr);
				Results.Ran = true;
			}
			Application::Quit();
		}

	private:
		static constexpr const char* k_SceneName = "Game View 2D Layer";
	};
}

TEST_CASE("Game View draws the 2D layer through the Camera 2D also over a Camera 3D, as builds do") {
	Layer2DApplication application;
	application.Run();
	REQUIRE(application.Results.Ran);
	const Layer2DResults& results = application.Results;

	// Renderer2D::BeginFrame uses the Camera 2D's matrix in builds; the Camera 3D used to take it over in the editor.
	CHECK(SameMatrix(results.Mixed.ViewProjection, results.Camera2DViewProjection));
	const glm::vec2 cameraCenter = ToNdc(results.Mixed.ViewProjection, { 2.0f, 1.0f });
	CHECK(cameraCenter.x == doctest::Approx(0.0f));
	CHECK(cameraCenter.y == doctest::Approx(0.0f));
	CHECK(std::abs(ToNdc(results.Camera3DViewProjection, { 2.0f, 1.0f }).x) > 0.1f);

	// Without a Camera 2D, sprites follow the Camera 3D's z = 0 plane and nothing is culled.
	CHECK(SameMatrix(results.Only3D.ViewProjection, results.Camera3DViewProjection));
	CHECK(results.Only3D.ViewAABB.Min.x < -1.0e8f);
	CHECK(results.Only3D.ViewAABB.Max.y > 1.0e8f);
	CHECK(SameMatrix(results.Neither.ViewProjection, glm::mat4(1.0f)));
}
