#include <doctest/doctest.h>

#include "Graphics/Texture2D.hpp"

using Index::Texture2D;

static_assert(Texture2D::ComputeMipLevelCount(1, 1) == 1);
static_assert(Texture2D::ComputeMipLevelCount(256, 256) == 9);

TEST_CASE("Texture2D mip level count is floor(log2(max(w,h))) + 1") {
	CHECK(Texture2D::ComputeMipLevelCount(0, 0) == 0);
	CHECK(Texture2D::ComputeMipLevelCount(1, 1) == 1);
	CHECK(Texture2D::ComputeMipLevelCount(2, 1) == 2);
	CHECK(Texture2D::ComputeMipLevelCount(1, 3) == 2);
	CHECK(Texture2D::ComputeMipLevelCount(4, 4) == 3);
	CHECK(Texture2D::ComputeMipLevelCount(255, 17) == 8);
	CHECK(Texture2D::ComputeMipLevelCount(256, 17) == 9);
	CHECK(Texture2D::ComputeMipLevelCount(1920, 1080) == 11);
	CHECK(Texture2D::ComputeMipLevelCount(1, 4096) == 13);
	CHECK(Texture2D::ComputeMipLevelCount(0xFFFFFFFFu, 1) == 32);
}
