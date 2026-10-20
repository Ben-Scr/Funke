#include "pch.hpp"
#include "Graphics/RenderStatistics.hpp"

#include "Core/Application.hpp"
#if FUNKE_WITH_2D
#include "Graphics/Renderer2D.hpp"
#endif
#if FUNKE_WITH_3D
#include "Graphics/Renderer3D.hpp"
#endif

namespace Funke {

	RenderGeometryStats CaptureRenderGeometryTotals() {
		RenderGeometryStats totals;
		Application* app = Application::GetInstance();
		if (!app) return totals;
#if FUNKE_WITH_2D
		if (const Renderer2D* renderer = app->GetRenderer2D()) totals.AddQuads(renderer->GetRenderedInstancesTotal());
#endif
#if FUNKE_WITH_3D
		if (const Renderer3D* renderer = app->GetRenderer3D()) totals = totals + renderer->GetGeometryTotals();
#endif
		return totals;
	}

} // namespace Funke
