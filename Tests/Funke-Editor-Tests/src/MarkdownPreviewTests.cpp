#include <doctest/doctest.h>
#include "Gui/MarkdownPreview.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <fstream>
#include <iterator>

using namespace Funke;

TEST_CASE("MarkdownPreview renders nested blocks and inline styles at narrow widths") {
	ImGuiContext* context = ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
	io.DisplaySize = ImVec2(1000, 900);
	MarkdownPreview preview;
	REQUIRE(preview.SetText(R"md(# Heading

A **bold** and *italic* paragraph with `inline code`, ~~deleted~~ text,
[a link](https://example.com) and &amp; &#x1F525; entities.

> Quote with **emphasis**
>
> - Nested list

3. Ordered item
   - Nested unordered item
4. Next item

- [x] Complete
- [ ] Open

| Name | Value |
| --- | --- |
| Test | **yes** |

```cpp
if (value < 3) {
    value++;
}
```

---
![Alt text](image.png)
An_unbroken_long_token_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz
)md"));
	for (const float width : { 90.0f, 320.0f, 750.0f }) {
		for (int frame = 0; frame < 3; ++frame) {
			ImGui::NewFrame();
			ImGui::SetNextWindowSize(ImVec2(width, 850));
			ImGui::Begin("Markdown");
			const int colors = context->ColorStack.Size;
			const int fonts = context->FontStack.Size;
			const int styles = context->StyleVarStack.Size;
			preview.DrawDocument();
			CHECK(context->ColorStack.Size == colors);
			CHECK(context->FontStack.Size == fonts);
			CHECK(context->StyleVarStack.Size == styles);
			CHECK(context->CurrentTable == nullptr);
			CHECK(ImGui::GetCursorPosY() > 200.0f);
			ImGui::End();
			ImGui::Render();
			CHECK(ImGui::GetDrawData()->TotalVtxCount > 0);
		}
	}
	ImGui::DestroyContext(context);
}

TEST_CASE("MarkdownPreview preserves code line spacing across parser text callbacks") {
	ImGuiContext* context = ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
	io.DisplaySize = ImVec2(600, 500);
	MarkdownPreview preview;
	REQUIRE(preview.SetText("```cpp\nif (value) {\n    value++;\n}\n```"));
	for (int frame = 0; frame < 3; ++frame) {
		ImGui::NewFrame();
		ImGui::SetNextWindowSize(ImVec2(500, 400));
		ImGui::Begin("Code preview");
		preview.DrawDocument();
		const auto& children = ImGui::GetCurrentWindow()->DC.ChildWindows;
		REQUIRE(children.Size == 1);
		if (frame > 0) {
			CHECK(children[0]->ContentSize.y >= ImGui::GetTextLineHeight() * 3);
			CHECK(children[0]->ContentSize.y <= ImGui::GetTextLineHeight() * 4);
		}
		ImGui::End();
		ImGui::Render();
	}
	ImGui::DestroyContext(context);
}

TEST_CASE("MarkdownPreview bounds input and accepts empty and incomplete documents") {
	MarkdownPreview preview;
	CHECK(preview.SetText(""));
	CHECK(preview.SetText("\xef\xbb\xbf# UTF-8 heading\r\n\r\nText"));
	CHECK(preview.SetText("**unfinished [link](\n```cpp\nno closing fence"));
	CHECK_FALSE(preview.SetText(std::string(1024 * 1024 + 1, 'x')));
	CHECK(preview.SetText("# Recovered"));
}

TEST_CASE("MarkdownPreview resolves Markdown and HTML images and folds details") {
	ImGuiContext* context = ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
	io.DisplaySize = ImVec2(1000, 1800);
	ImGui::GetStyle().FontSizeBase = 15;
	MarkdownPreview preview;
	const auto document = std::filesystem::current_path() / "docs/README.md";
	REQUIRE(preview.SetText(R"md(<p><IMG alt='Logo > text' WIDTH='64' src='images/logo%20test.png'></p>

![Screenshot with **emphasis**](../screens/main.png)

<details>
<summary>Closed screenshots</summary>

![Hidden](hidden.png)

</details>

<details open>
<summary>Visible screenshots</summary>

<p><img src="left.png" width="48%"><img src="right.png" width="48%"></p>

<details><summary>Nested closed</summary>

<img src="also-hidden.png">

</details>

</details>

```html
<img src="code-only.png">
```

![Remote](https://example.com/remote.png)
)md", document));
	for (int frame = 0; frame < 3; ++frame) {
		std::vector<std::filesystem::path> requested;
		ImGui::NewFrame();
		ImGui::SetNextWindowSize(ImVec2(700, 1700));
		ImGui::Begin("Images and details");
		const float indent = ImGui::GetCurrentWindow()->DC.Indent.x;
		preview.DrawDocument([&](const auto& path) {
			requested.push_back(path);
			return MarkdownPreview::ImageInfo{ 1000 + requested.size(), 1000, 500 };
		});
		CHECK(ImGui::GetCurrentWindow()->DC.Indent.x == indent);
		REQUIRE(requested.size() == 4);
		CHECK(requested[0] == document.parent_path() / "images/logo test.png");
		CHECK(requested[1] == document.parent_path().parent_path() / "screens/main.png");
		CHECK(requested[2].filename() == "left.png");
		CHECK(requested[3].filename() == "right.png");
		if (frame > 0) {
			std::vector<ImRect> rectangles;
			const auto* draw = ImGui::GetWindowDrawList();
			for (const auto& cmd : draw->CmdBuffer) {
				const ImTextureID texture = cmd.TexRef.GetTexID();
				if (texture < 1001 || texture > 1004 || cmd.ElemCount == 0) continue;
				ImRect bounds(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
				for (unsigned i = 0; i < cmd.ElemCount; ++i) bounds.Add(draw->VtxBuffer[cmd.VtxOffset + draw->IdxBuffer[cmd.IdxOffset + i]].pos);
				rectangles.push_back(bounds);
			}
			REQUIRE(rectangles.size() == 4);
			CHECK(rectangles[0].GetWidth() == doctest::Approx(64));
			CHECK(rectangles[0].GetHeight() == doctest::Approx(32));
			CHECK(rectangles[2].Min.y == rectangles[3].Min.y);
			CHECK(rectangles[2].Max.x < rectangles[3].Min.x);
			CHECK(rectangles[2].GetWidth() == doctest::Approx(rectangles[3].GetWidth()));
		}
		ImGui::End(); ImGui::Render();
	}
	ImGui::DestroyContext(context);
}

TEST_CASE("MarkdownPreview parses repository README image paths without raw HTML") {
	const auto repo = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
	std::ifstream stream(repo / "README.md", std::ios::binary);
	REQUIRE(stream.good());
	const std::string readme((std::istreambuf_iterator<char>(stream)), {});
	MarkdownPreview preview;
	REQUIRE(preview.SetText(readme, repo / "README.md"));
	ImGuiContext* context = ImGui::CreateContext();
	ImGui::GetIO().IniFilename = nullptr;
	ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
	ImGui::GetIO().DisplaySize = ImVec2(1000, 1000);
	ImGui::NewFrame();
	ImGui::SetNextWindowSize(ImVec2(700, 900));
	ImGui::Begin("Repository README");
	std::vector<std::filesystem::path> images;
	ImGui::LogToBuffer();
	preview.DrawDocument([&](const auto& path) {
		images.push_back(path);
		return MarkdownPreview::ImageInfo{ 0, 1920, 1080, false, true };
	});
	const std::string log(context->LogBuffer.c_str());
	ImGui::LogFinish();
	CHECK(log.find("<img") == std::string::npos);
	CHECK(log.find("<summary>") == std::string::npos);
	REQUIRE(images.size() == 2);
	CHECK(images[0].filename() == "icon_256.png");
	CHECK(images[1].filename() == "editor2.png");
	for (const auto& path : images) CHECK(std::filesystem::exists(path));
	ImGui::End(); ImGui::Render();
	ImGui::DestroyContext(context);
}
