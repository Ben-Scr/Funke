// @name Static Stripes
// @target Mesh3D
// @property color a = (0.95, 0.55, 0.1, 1)
// @property color b = (0.1, 0.3, 0.7, 1)

fn surface(in: SurfaceInput) -> SurfaceOutput {
	var out = defaultSurface(in);
	let band = fract((in.uv.x + in.uv.y) * 4.0) < 0.5;
	out.baseColor = select(material.b.rgb, material.a.rgb, band);
	return out;
}
