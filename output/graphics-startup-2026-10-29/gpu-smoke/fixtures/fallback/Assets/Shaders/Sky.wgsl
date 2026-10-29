// @name Test Sky
// @target Skybox
// @property color top = (0.24, 0.45, 0.78, 1)
// @property color horizon = (0.73, 0.83, 0.93, 1)
// @property color ground = (0.36, 0.34, 0.31, 1)

fn surface(in: SurfaceInput) -> SurfaceOutput {
	var out: SurfaceOutput;
	let up = in.direction.y;
	var color = mix(material.horizon.rgb, material.ground.rgb, smoothstep(0.0, 0.2, -up));
	if (up >= 0.0) {
		color = mix(material.horizon.rgb, material.top.rgb, pow(up, 0.6));
	}
	out.color = addSun(color, in);
	return out;
}
