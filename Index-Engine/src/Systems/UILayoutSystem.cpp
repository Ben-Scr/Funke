#include "pch.hpp"
#include "Systems/UILayoutSystem.hpp"

#include "Collections/Viewport.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/Tags.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/UI/ContentSizeFitterComponent.hpp"
#include "Components/UI/GridLayoutGroupComponent.hpp"
#include "Components/UI/HorizontalLayoutGroupComponent.hpp"
#include "Components/UI/VerticalLayoutGroupComponent.hpp"
#include "Components/UI/WidthConstraintComponent.hpp"
#include "Core/Application.hpp"
#include "Core/Window.hpp"
#include "Graphics/Text/Font.hpp"
#include "Graphics/Text/TextRenderer.hpp"
#include "Math/Trigonometry.hpp"
#include "Profiling/Profiler.hpp"
#include "Project/IndexProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/Scene.hpp"

#include <algorithm>
#include <cmath>

namespace Index {

	namespace {
		// Set per ComputeUILayout call: the full UI surface and its safe-area part, in UI space.
		Vec2 s_ScreenMin, s_ScreenMax, s_SafeMin, s_SafeMax;

		// Edges of the parent that stop at the safe area reach the screen edge instead.
		void ExtendToScreenEdges(Vec2& parentMin, Vec2& parentMax) {
			constexpr float k_Tolerance = 0.5f;
			if (std::abs(parentMin.x - s_SafeMin.x) < k_Tolerance) parentMin.x = s_ScreenMin.x;
			if (std::abs(parentMin.y - s_SafeMin.y) < k_Tolerance) parentMin.y = s_ScreenMin.y;
			if (std::abs(parentMax.x - s_SafeMax.x) < k_Tolerance) parentMax.x = s_ScreenMax.x;
			if (std::abs(parentMax.y - s_SafeMax.y) < k_Tolerance) parentMax.y = s_ScreenMax.y;
		}


		// Width/height: SizeDelta*worldScale (parent*local); parent resolved size does NOT propagate, but world scale does.
		// Rotation: child world = parent.Rotation+LocalRotation; pivot rotated around parentPivot; ResolvedMin/Max stay axis-aligned.
				void ResolveRect(RectTransform2DComponent& rect,
			const Vec2& parentMin, const Vec2& parentMax,
			float parentRotation, const Vec2& parentScale,
			const Vec2& parentPivot)
		{
			rect.Rotation = parentRotation + rect.LocalRotation;
			rect.Scale = Vec2{ parentScale.x * rect.LocalScale.x,
			                   parentScale.y * rect.LocalScale.y };

			const Vec2 parentSize{ parentMax.x - parentMin.x, parentMax.y - parentMin.y };

			// Inverted anchors tolerated (inspector allows mid-drag inversion); use clamped local copies, preserve authored values.
			const Vec2 normMin{
				std::min(rect.AnchorMin.x, rect.AnchorMax.x),
				std::min(rect.AnchorMin.y, rect.AnchorMax.y)
			};
			const Vec2 normMax{
				std::max(rect.AnchorMin.x, rect.AnchorMax.x),
				std::max(rect.AnchorMin.y, rect.AnchorMax.y)
			};

			const Vec2 anchorBL{
				parentMin.x + parentSize.x * normMin.x,
				parentMin.y + parentSize.y * normMin.y
			};
			const Vec2 anchorTR{
				parentMin.x + parentSize.x * normMax.x,
				parentMin.y + parentSize.y * normMax.y
			};

			// finalSize = SizeDelta*worldScale: world = parent*local; parent scale must resize children too.
			const Vec2 finalSize{
				rect.FillWidth ? parentSize.x : rect.SizeDelta.x * rect.Scale.x,
				rect.FillHeight ? parentSize.y : rect.SizeDelta.y * rect.Scale.y
			};

			// Anchor centre — the reference point that AnchoredPosition
			// is offset from.
			const Vec2 anchorCenter{
				(anchorBL.x + anchorTR.x) * 0.5f,
				(anchorBL.y + anchorTR.y) * 0.5f
			};

			// Pivot world position in pre-rotation parent-local space:
			// anchor centre + AnchoredPosition scaled by parent's world
			// scale so a scaled parent moves its children proportionally.
			Vec2 pivotWorld{
				anchorCenter.x + rect.AnchoredPosition.x * parentScale.x,
				anchorCenter.y + rect.AnchoredPosition.y * parentScale.y
			};

			if (rect.FillWidth || rect.FillHeight) {
				// parentMin/Max are already world-space, so the parent's rotation is not applied a second time.
				if (rect.FillWidth) pivotWorld.x = parentMin.x + parentSize.x * rect.Pivot.x;
				if (rect.FillHeight) pivotWorld.y = parentMin.y + parentSize.y * rect.Pivot.y;
			}
			// Rotate pivotWorld around parentPivot; without this, children stay at their unrotated slot and float visually.
			else if (parentRotation != 0.0f) {
				const float c = std::cos(parentRotation);
				const float s = std::sin(parentRotation);
				const float dx = pivotWorld.x - parentPivot.x;
				const float dy = pivotWorld.y - parentPivot.y;
				pivotWorld = {
					parentPivot.x + c * dx - s * dy,
					parentPivot.y + s * dx + c * dy
				};
			}

			const Vec2 bottomLeft{
				pivotWorld.x - finalSize.x * rect.Pivot.x,
				pivotWorld.y - finalSize.y * rect.Pivot.y
			};
			const Vec2 topRight{
				bottomLeft.x + finalSize.x,
				bottomLeft.y + finalSize.y
			};

			rect.ResolvedMin = bottomLeft;
			rect.ResolvedMax = topRight;
			rect.ResolvedPivot = pivotWorld;
			rect.ResolvedValid = true;
		}

		std::vector<EntityHandle> CollectLaidOutChildren(entt::registry& registry, EntityHandle entity) {
			std::vector<EntityHandle> out;
			if (auto* hierarchy = registry.try_get<HierarchyComponent>(entity)) {
				out.reserve(hierarchy->Children.size());
				for (EntityHandle child : hierarchy->Children) {
					if (!registry.valid(child)) continue;
					if (!registry.all_of<RectTransform2DComponent>(child)) continue;
					if (registry.all_of<DisabledTag>(child)) continue;
					out.push_back(child);
				}
			}
			return out;
		}

		// Divides by parentScale before storing so ResolveRect's multiply doesn't double-scale child dimensions.
				void PinChildToTopLeft(RectTransform2DComponent& rect,
			float xLeft, float yTop, float width, float height, float parentScale)
		{
			const float invScale = parentScale != 0.0f ? 1.0f / parentScale : 1.0f;
			rect.AnchorMin = Vec2{ 0.0f, 1.0f };
			rect.AnchorMax = Vec2{ 0.0f, 1.0f };
			rect.Pivot     = Vec2{ 0.0f, 1.0f };
			rect.AnchoredPosition = Vec2{ xLeft * invScale, -yTop * invScale };
			rect.SizeDelta = Vec2{ width * invScale, height * invScale };
		}

		// Rewrites child anchors/positions for one row; Padding/Spacing are scaled by parentWorldScale to match resolved frame units.
				void ApplyHorizontalLayout(entt::registry& registry, EntityHandle entity,
			const HorizontalLayoutGroupComponent& layout, const Vec2& parentMin, const Vec2& parentMax,
			float parentWorldScale)
		{
			auto children = CollectLaidOutChildren(registry, entity);
			if (children.empty()) return;
			if (layout.ReverseArrangement) std::reverse(children.begin(), children.end());

			const float s = parentWorldScale;
			const float frameWidth  = (parentMax.x - parentMin.x) - (layout.PaddingLeft + layout.PaddingRight) * s;
			const float frameHeight = (parentMax.y - parentMin.y) - (layout.PaddingTop  + layout.PaddingBottom) * s;
			const float frameLeft   = layout.PaddingLeft * s;
			const float frameTop    = layout.PaddingTop  * s;
			if (frameWidth <= 0.0f || frameHeight <= 0.0f) return;

			const int n = static_cast<int>(children.size());
			const float totalSpacing = layout.Spacing * s * static_cast<float>(std::max(0, n - 1));

			const bool equalSlots = layout.ControlChildWidth && layout.ChildForceExpandWidth;
			std::vector<float> widths(n);
			float sumWidth = 0.0f;
			if (equalSlots) {
				const float slot = (frameWidth - totalSpacing) / static_cast<float>(n);
				for (float& w : widths) { w = std::max(0.0f, slot); sumWidth += w; }
			}
			else {
				for (int i = 0; i < n; ++i) {
					const auto& cr = registry.get<RectTransform2DComponent>(children[i]);
					// UseChildScale: fold LocalScale into the natural size so
					// a scaled-up child reserves more space along the axis.
					const float scale = layout.UseChildScaleWidth ? cr.LocalScale.x : 1.0f;
					widths[i] = cr.SizeDelta.x * scale * s;
					sumWidth += widths[i];
				}
			}
			const float usedWidth = sumWidth + totalSpacing;

			// Horizontal alignment of the row block inside the frame.
			float xCursor = frameLeft;
			const int alignIdx = static_cast<int>(layout.ChildAlignment);
			const int hAlign = alignIdx % 3; // 0=Left,1=Center,2=Right
			if (hAlign == 1)      xCursor = frameLeft + (frameWidth - usedWidth) * 0.5f;
			else if (hAlign == 2) xCursor = frameLeft + (frameWidth - usedWidth);

			// Vertical alignment of each child inside the row's height.
			const int vAlign = alignIdx / 3; // 0=Upper,1=Middle,2=Lower

			for (int i = 0; i < n; ++i) {
				auto& cr = registry.get<RectTransform2DComponent>(children[i]);
				float w = widths[i];
				const float heightScale = layout.UseChildScaleHeight ? cr.LocalScale.y : 1.0f;
				float h = layout.ControlChildHeight ? frameHeight : cr.SizeDelta.y * heightScale * s;
				if (layout.ControlChildHeight && layout.ChildForceExpandHeight) {
					h = frameHeight;
				}
				h = std::max(0.0f, h);
				float yTop = frameTop;
				if (vAlign == 1)      yTop = frameTop + (frameHeight - h) * 0.5f;
				else if (vAlign == 2) yTop = frameTop + (frameHeight - h);
				PinChildToTopLeft(cr, xCursor, yTop, w, h, s);
				xCursor += w + layout.Spacing * s;
			}
		}

		void ApplyVerticalLayout(entt::registry& registry, EntityHandle entity,
			const VerticalLayoutGroupComponent& layout, const Vec2& parentMin, const Vec2& parentMax,
			float parentWorldScale)
		{
			auto children = CollectLaidOutChildren(registry, entity);
			if (children.empty()) return;
			if (layout.ReverseArrangement) std::reverse(children.begin(), children.end());

			const float s = parentWorldScale;
			const float frameWidth  = (parentMax.x - parentMin.x) - (layout.PaddingLeft + layout.PaddingRight) * s;
			const float frameHeight = (parentMax.y - parentMin.y) - (layout.PaddingTop  + layout.PaddingBottom) * s;
			const float frameLeft   = layout.PaddingLeft * s;
			const float frameTop    = layout.PaddingTop  * s;
			if (frameWidth <= 0.0f || frameHeight <= 0.0f) return;

			const int n = static_cast<int>(children.size());
			const float totalSpacing = layout.Spacing * s * static_cast<float>(std::max(0, n - 1));

			const bool equalSlots = layout.ControlChildHeight && layout.ChildForceExpandHeight;
			std::vector<float> heights(n);
			float sumHeight = 0.0f;
			if (equalSlots) {
				const float slot = (frameHeight - totalSpacing) / static_cast<float>(n);
				for (float& h : heights) { h = std::max(0.0f, slot); sumHeight += h; }
			}
			else {
				for (int i = 0; i < n; ++i) {
					const auto& cr = registry.get<RectTransform2DComponent>(children[i]);
					const float scale = layout.UseChildScaleHeight ? cr.LocalScale.y : 1.0f;
					heights[i] = cr.SizeDelta.y * scale * s;
					sumHeight += heights[i];
				}
			}
			const float usedHeight = sumHeight + totalSpacing;

			float yCursor = frameTop;
			const int alignIdx = static_cast<int>(layout.ChildAlignment);
			const int vAlign = alignIdx / 3; // 0=Upper,1=Middle,2=Lower
			if (vAlign == 1)      yCursor = frameTop + (frameHeight - usedHeight) * 0.5f;
			else if (vAlign == 2) yCursor = frameTop + (frameHeight - usedHeight);

			const int hAlign = alignIdx % 3; // 0=Left,1=Center,2=Right

			for (int i = 0; i < n; ++i) {
				auto& cr = registry.get<RectTransform2DComponent>(children[i]);
				float h = heights[i];
				const float widthScale = layout.UseChildScaleWidth ? cr.LocalScale.x : 1.0f;
				float w = layout.ControlChildWidth ? frameWidth : cr.SizeDelta.x * widthScale * s;
				if (layout.ControlChildWidth && layout.ChildForceExpandWidth) {
					w = frameWidth;
				}
				w = std::max(0.0f, w);
				float xLeft = frameLeft;
				if (hAlign == 1)      xLeft = frameLeft + (frameWidth - w) * 0.5f;
				else if (hAlign == 2) xLeft = frameLeft + (frameWidth - w);
				PinChildToTopLeft(cr, xLeft, yCursor, w, h, s);
				yCursor += h + layout.Spacing * s;
			}
		}

		void ApplyGridLayout(entt::registry& registry, EntityHandle entity,
			const GridLayoutGroupComponent& layout, const Vec2& parentMin, const Vec2& parentMax,
			float parentWorldScale)
		{
			auto children = CollectLaidOutChildren(registry, entity);
			if (children.empty()) return;

			const float s = parentWorldScale;
			const float frameWidth  = (parentMax.x - parentMin.x) - (layout.PaddingLeft + layout.PaddingRight) * s;
			const float frameHeight = (parentMax.y - parentMin.y) - (layout.PaddingTop  + layout.PaddingBottom) * s;
			const float frameLeft   = layout.PaddingLeft * s;
			const float frameTop    = layout.PaddingTop  * s;
			if (frameWidth <= 0.0f || frameHeight <= 0.0f) return;

			const int n = static_cast<int>(children.size());
			const float cellW = std::max(1.0f, layout.CellSize.x) * s;
			const float cellH = std::max(1.0f, layout.CellSize.y) * s;
			const float spacingX = layout.Spacing.x * s;
			const float spacingY = layout.Spacing.y * s;

			// Decide column / row counts from the constraint.
			int cols = 0;
			int rows = 0;
			switch (layout.Constraint) {
			case GridLayoutConstraint::FixedColumnCount:
				cols = std::max(1, layout.ConstraintCount);
				rows = (n + cols - 1) / cols;
				break;
			case GridLayoutConstraint::FixedRowCount:
				rows = std::max(1, layout.ConstraintCount);
				cols = (n + rows - 1) / rows;
				break;
			case GridLayoutConstraint::Flexible:
			default:
				if (layout.StartAxis == GridLayoutStartAxis::Horizontal) {
					cols = std::max(1, static_cast<int>((frameWidth + spacingX) / (cellW + spacingX)));
					rows = (n + cols - 1) / cols;
				}
				else {
					rows = std::max(1, static_cast<int>((frameHeight + spacingY) / (cellH + spacingY)));
					cols = (n + rows - 1) / rows;
				}
				break;
			}
			cols = std::max(1, cols);
			rows = std::max(1, rows);

			const float usedWidth  = static_cast<float>(cols) * cellW + static_cast<float>(std::max(0, cols - 1)) * spacingX;
			const float usedHeight = static_cast<float>(rows) * cellH + static_cast<float>(std::max(0, rows - 1)) * spacingY;

			const int alignIdx = static_cast<int>(layout.ChildAlignment);
			const int hAlign = alignIdx % 3;
			const int vAlign = alignIdx / 3;
			float originX = frameLeft;
			float originY = frameTop;
			if (hAlign == 1)      originX = frameLeft + (frameWidth - usedWidth) * 0.5f;
			else if (hAlign == 2) originX = frameLeft + (frameWidth - usedWidth);
			if (vAlign == 1)      originY = frameTop + (frameHeight - usedHeight) * 0.5f;
			else if (vAlign == 2) originY = frameTop + (frameHeight - usedHeight);

			const bool flipX = (layout.StartCorner == GridLayoutStartCorner::UpperRight)
				|| (layout.StartCorner == GridLayoutStartCorner::LowerRight);
			const bool flipY = (layout.StartCorner == GridLayoutStartCorner::LowerLeft)
				|| (layout.StartCorner == GridLayoutStartCorner::LowerRight);

			for (int i = 0; i < n; ++i) {
				int col = 0;
				int row = 0;
				if (layout.StartAxis == GridLayoutStartAxis::Horizontal) {
					col = i % cols;
					row = i / cols;
				}
				else {
					row = i % rows;
					col = i / rows;
				}
				if (flipX) col = (cols - 1) - col;
				if (flipY) row = (rows - 1) - row;
				const float x = originX + static_cast<float>(col) * (cellW + spacingX);
				const float y = originY + static_cast<float>(row) * (cellH + spacingY);
				const int childIdx = layout.Reverse ? (n - 1 - i) : i;
				auto& cr = registry.get<RectTransform2DComponent>(children[childIdx]);
				PinChildToTopLeft(cr, x, y, cellW, cellH, s);
			}
		}

		// Sizes rect to the measured text when WrapMode::None; runs before ContentSizeFitter so a parent fitter sees fresh dimensions.
				void FitTextNaturalSize(entt::registry& registry, EntityHandle entity)
		{
			auto* text = registry.try_get<TextRendererComponent>(entity);
			auto* rect = registry.try_get<RectTransform2DComponent>(entity);
			if (!text || !rect) return;
			if (text->WrapMode != TextWrapMode::None) return;
			if (!text->AutoSize) return; // manual sizing: leave SizeDelta to the user

			Font* font = TextRenderer::ResolveFont(*text);
			if (!font || !font->IsLoaded()) return;

			const Vec2 naturalAtlasPx = TextRenderer::MeasureNaturalSize(
				*font, text->Text, text->LetterSpacing, text->LineSpacing);

			const float bakedSize = font->GetPixelSize() > 0.0f
				? font->GetPixelSize()
				: text->FontSize;
			const float pxScale = bakedSize > 0.0f ? (text->FontSize / bakedSize) : 1.0f;

			rect->SizeDelta.x = naturalAtlasPx.x * pxScale;
			rect->SizeDelta.y = naturalAtlasPx.y * pxScale;
		}

		// Bottom-up pass: updates SizeDelta on fitter entities before the top-down ResolveHierarchy so layout-groups see fitted sizes.
				void FitContentSize(entt::registry& registry, EntityHandle entity)
		{
			if (auto* hierarchy = registry.try_get<HierarchyComponent>(entity)) {
				for (EntityHandle child : hierarchy->Children) {
					if (registry.valid(child)) {
						FitContentSize(registry, child);
					}
				}
			}

			FitTextNaturalSize(registry, entity);

			auto* rect = registry.try_get<RectTransform2DComponent>(entity);
			if (!rect) return;

			if (auto* csf = registry.try_get<ContentSizeFitterComponent>(entity);
				csf && (csf->HorizontalFit || csf->VerticalFit))
			{
				float minLeft = 0.0f;
				float maxRight = 0.0f;
				float minTop = 0.0f;
				float maxBottom = 0.0f;
				bool anyChild = false;
				if (auto* hierarchy = registry.try_get<HierarchyComponent>(entity)) {
					for (EntityHandle child : hierarchy->Children) {
						if (!registry.valid(child)) continue;
						if (registry.all_of<DisabledTag>(child)) continue;
						auto* childRect = registry.try_get<RectTransform2DComponent>(child);
						if (!childRect) continue;
						const float w = childRect->SizeDelta.x * childRect->LocalScale.x;
						const float h = childRect->SizeDelta.y * childRect->LocalScale.y;
						const float left   = childRect->AnchoredPosition.x - w * childRect->Pivot.x;
						const float right  = childRect->AnchoredPosition.x + w * (1.0f - childRect->Pivot.x);
						const float top    = -childRect->AnchoredPosition.y - h * (1.0f - childRect->Pivot.y);
						const float bottom = -childRect->AnchoredPosition.y + h * childRect->Pivot.y;
						minLeft   = std::min(minLeft, left);
						maxRight  = std::max(maxRight, right);
						minTop    = std::min(minTop, top);
						maxBottom = std::max(maxBottom, bottom);
						anyChild = true;
					}
				}
				if (anyChild) {
					if (csf->HorizontalFit) {
						rect->SizeDelta.x = std::max(0.0f, maxRight - minLeft) + csf->PaddingLeft + csf->PaddingRight;
					}
					if (csf->VerticalFit) {
						rect->SizeDelta.y = std::max(0.0f, maxBottom - minTop) + csf->PaddingTop + csf->PaddingBottom;
					}
				}
			}

			// WidthConstraint clamps SizeDelta.x after CSF so a fitted row can still be capped at MaxWidth.
			if (auto* wc = registry.try_get<WidthConstraintComponent>(entity)) {
				if (wc->MinWidth >= 0.0f) {
					rect->SizeDelta.x = std::max(rect->SizeDelta.x, wc->MinWidth);
				}
				if (wc->MaxWidth >= 0.0f) {
					rect->SizeDelta.x = std::min(rect->SizeDelta.x, wc->MaxWidth);
				}
			}
		}

		void ResolveHierarchy(entt::registry& registry, EntityHandle entity,
			const Vec2& parentMin, const Vec2& parentMax,
			float parentRotation, const Vec2& parentScale,
			const Vec2& parentPivot)
		{
			RectTransform2DComponent* rect = registry.try_get<RectTransform2DComponent>(entity);

			Vec2 childParentMin = parentMin;
			Vec2 childParentMax = parentMax;
			float childParentRotation = parentRotation;
			Vec2 childParentScale = parentScale;
			Vec2 childParentPivot = parentPivot;

			if (rect) {
				Vec2 resolveMin = parentMin;
				Vec2 resolveMax = parentMax;
				if (rect->IgnoreSafeArea) ExtendToScreenEdges(resolveMin, resolveMax);
				ResolveRect(*rect, resolveMin, resolveMax, parentRotation, parentScale, parentPivot);
				childParentMin = rect->ResolvedMin;
				childParentMax = rect->ResolvedMax;
				childParentRotation = rect->Rotation;
				childParentScale = rect->Scale;
				childParentPivot = rect->ResolvedPivot;

				// Apply layout-group rewrites AFTER parent resolves but BEFORE children, so rewritten anchors/sizes propagate.
				const float layoutScale = childParentScale.x;
				if (auto* h = registry.try_get<HorizontalLayoutGroupComponent>(entity)) {
					ApplyHorizontalLayout(registry, entity, *h, childParentMin, childParentMax, layoutScale);
				}
				else if (auto* v = registry.try_get<VerticalLayoutGroupComponent>(entity)) {
					ApplyVerticalLayout(registry, entity, *v, childParentMin, childParentMax, layoutScale);
				}
				else if (auto* g = registry.try_get<GridLayoutGroupComponent>(entity)) {
					ApplyGridLayout(registry, entity, *g, childParentMin, childParentMax, layoutScale);
				}
			}

			if (auto* hierarchy = registry.try_get<HierarchyComponent>(entity)) {
				for (EntityHandle child : hierarchy->Children) {
					if (registry.valid(child)) {
						ResolveHierarchy(registry, child,
							childParentMin, childParentMax,
							childParentRotation, childParentScale,
							childParentPivot);
					}
				}
			}
		}

		uint64_t s_UILayoutEpoch = 1;

		// Inputs a layout pass depends on besides the entities themselves; stored in the registry context.
		struct UILayoutStamp {
			uint64_t Epoch = 0;
			int Width = 0, Height = 0;
			float InsetLeft = 0.0f, InsetRight = 0.0f, InsetTop = 0.0f, InsetBottom = 0.0f;
			float UIScale = 0.0f;

			bool operator==(const UILayoutStamp&) const = default;
		};

		void LayoutScene(Scene& scene, bool skipIfCurrent);

	} // namespace

	void InvalidateUILayout() {
		++s_UILayoutEpoch;
	}

	void ComputeUILayout(Scene& scene) {
		LayoutScene(scene, false);
	}

	void UpdateUILayout(Scene& scene) {
		// The editor edits rects outside any system, so only the player trusts the epoch.
		LayoutScene(scene, !Application::IsEditor());
	}

	namespace {

	void LayoutScene(Scene& scene, bool skipIfCurrent) {
		entt::registry& registry = scene.GetRegistry();
		if (registry.view<RectTransform2DComponent>().size() == 0) {
			return;
		}

		// Use window viewport (not camera): UIRegion overrides with panel rect in editor mode so layout matches the visible sub-panel.
		const Window::UIRegion uiRegion = Window::GetUIRegion();
		int vpW = 0;
		int vpH = 0;
		if (uiRegion.IsActive()) {
			vpW = uiRegion.Width;
			vpH = uiRegion.Height;
		}
		else {
			Window* window = Application::GetWindow();
			Viewport* vp = window ? Window::GetMainViewport() : nullptr;
			if (!vp || vp->GetWidth() <= 0 || vp->GetHeight() <= 0) {
				return;
			}
			vpW = vp->GetWidth();
			vpH = vp->GetHeight();
		}

		const float halfW = static_cast<float>(vpW) * 0.5f;
		const float halfH = static_cast<float>(vpH) * 0.5f;
		const Vec2 windowMin{ -halfW, -halfH };
		const Vec2 windowMax{ +halfW, +halfH };
		// UI space is y-up, so the top inset lowers the max edge.
		const Window::SafeAreaInsets insets = Window::GetSafeAreaInsets();
		s_ScreenMin = windowMin;
		s_ScreenMax = windowMax;
		s_SafeMin = Vec2{ windowMin.x + insets.Left, windowMin.y + insets.Bottom };
		s_SafeMax = Vec2{ windowMax.x - insets.Right, windowMax.y - insets.Top };

		// Scaled against the safe area so Fit keeps the reference resolution clear of notches too.
		const IndexProject* project = ProjectManager::GetCurrentProject();
		const float uiScale = project
			? project->ComputeUIScale(s_SafeMax.x - s_SafeMin.x, s_SafeMax.y - s_SafeMin.y)
			: 1.0f;

		const UILayoutStamp stamp{ s_UILayoutEpoch, vpW, vpH,
			insets.Left, insets.Right, insets.Top, insets.Bottom, uiScale };
		if (skipIfCurrent) {
			if (const UILayoutStamp* last = registry.ctx().find<UILayoutStamp>(); last && *last == stamp) return;
		}
		registry.ctx().insert_or_assign(stamp);

		auto rectView = registry.view<RectTransform2DComponent>();
		for (auto entity : rectView) {
			rectView.get<RectTransform2DComponent>(entity).ResolvedValid = false;
		}

		// Root = no RectTransform anywhere above. ResolveHierarchy recurses through plain (rect-less) entities,
		// so a rect under a plain child of a UI rect is already resolved there; treating it as a root too laid it
		// out twice, with the EnTT pool order deciding which result won.
		auto isUIRoot = [&](EntityHandle entity) {
			EntityHandle current = entity;
			for (int depth = 0; depth < 1024; ++depth) {
				const HierarchyComponent* hierarchy = registry.try_get<HierarchyComponent>(current);
				if (!hierarchy || hierarchy->Parent == entt::null || !registry.valid(hierarchy->Parent)) return true;
				if (registry.all_of<RectTransform2DComponent>(hierarchy->Parent)) return false;
				current = hierarchy->Parent;
			}
			return true;
		};

		thread_local std::vector<EntityHandle> t_Roots;
		t_Roots.clear();
		for (auto entity : rectView) {
			if (isUIRoot(entity)) t_Roots.push_back(entity);
		}

		for (EntityHandle entity : t_Roots) {
			FitContentSize(registry, entity);
		}

		const Vec2 windowCenter{
			(windowMin.x + windowMax.x) * 0.5f,
			(windowMin.y + windowMax.y) * 0.5f
		};
		for (EntityHandle entity : t_Roots) {
			// parentPivot=windowCenter so root rotation pivots at screen center; parentScale=uiScale propagates reference-res scaling.
			ResolveHierarchy(registry, entity, s_SafeMin, s_SafeMax, 0.0f, Vec2{ uiScale, uiScale }, windowCenter);
		}
	}

	} // namespace

	void UILayoutSystem::Update(Scene& scene) {
		if (scene.GetRegistry().view<RectTransform2DComponent>().size() == 0) return;
		// Scope after the gate — non-zero readings prove the gate fired and
		// ComputeUILayout actually ran. Stays 0.0 in scenes with no UI.
		INDEX_PROFILE_SCOPE("UILayout");
		UpdateUILayout(scene);
		// Scripts, animators and UI events run after this system; the render pass must lay out again.
		InvalidateUILayout();
	}

}
