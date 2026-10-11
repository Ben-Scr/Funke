#include <doctest/doctest.h>

#include "Components/Graphics/SpriteAnimator2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Systems/SpriteAnimationSystem.hpp"

using namespace Index;

namespace {
	SpriteAnimator2DComponent MakeAnimator(bool loop) {
		SpriteAnimator2DComponent animator;
		animator.Frames = { "walk_0", "walk_1", "walk_2" };
		animator.FramesPerSecond = 10.0f; // 0.1 s per frame
		animator.Loop = loop;
		return animator;
	}
}

TEST_CASE("Sprite animator shows the first frame and advances at its frame rate") {
	SpriteAnimator2DComponent animator = MakeAnimator(true);
	SpriteRendererComponent sprite;

	SpriteAnimationSystem::Step(animator, sprite, 0.0f);
	CHECK(sprite.SpriteName == "walk_0");
	CHECK(animator.Playing);

	SpriteAnimationSystem::Step(animator, sprite, 0.05f);
	CHECK(sprite.SpriteName == "walk_0");
	SpriteAnimationSystem::Step(animator, sprite, 0.06f);
	CHECK(sprite.SpriteName == "walk_1");
}

TEST_CASE("Looping animations wrap and a long hitch skips frames") {
	SpriteAnimator2DComponent animator = MakeAnimator(true);
	SpriteRendererComponent sprite;
	SpriteAnimationSystem::Step(animator, sprite, 0.35f); // 3.5 frames -> wraps to frame 0
	CHECK(sprite.SpriteName == "walk_0");
	SpriteAnimationSystem::Step(animator, sprite, 0.06f);
	CHECK(sprite.SpriteName == "walk_1");
}

TEST_CASE("One-shot animations stop on their last frame") {
	SpriteAnimator2DComponent animator = MakeAnimator(false);
	SpriteRendererComponent sprite;
	SpriteAnimationSystem::Step(animator, sprite, 1.0f);
	CHECK(sprite.SpriteName == "walk_2");
	CHECK_FALSE(animator.Playing);
}

TEST_CASE("PlayOnStart off keeps the first frame until Play is called") {
	SpriteAnimator2DComponent animator = MakeAnimator(true);
	animator.PlayOnStart = false;
	SpriteRendererComponent sprite;
	SpriteAnimationSystem::Step(animator, sprite, 0.5f);
	CHECK(sprite.SpriteName == "walk_0");

	animator.Play();
	SpriteAnimationSystem::Step(animator, sprite, 0.1f);
	CHECK(sprite.SpriteName == "walk_1");

	animator.Stop();
	SpriteAnimationSystem::Step(animator, sprite, 0.5f);
	CHECK(sprite.SpriteName == "walk_0");
}

TEST_CASE("An animator without frames leaves the sprite alone") {
	SpriteAnimator2DComponent animator;
	SpriteRendererComponent sprite;
	sprite.SpriteName = "idle";
	SpriteAnimationSystem::Step(animator, sprite, 1.0f);
	CHECK(sprite.SpriteName == "idle");
}
