#include "pch.hpp"
#include "Graphics/Shader.hpp"

#include "Core/Log.hpp"
#include "Graphics/Backend/WebGPUBackend.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>


namespace Funke {

	namespace {
		constexpr const char* k_SpriteWGSL = R"WGSL(
struct Uniforms {
	viewProj: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var t_albedo: texture_2d<f32>;
@group(0) @binding(2) var s_albedo: sampler;
@group(0) @binding(3) var t_mask: texture_2d<f32>;
@group(0) @binding(4) var s_mask: sampler;

struct VertexInput {
	@location(0) position: vec3<f32>,
	@location(1) i_data0: vec4<f32>,  // Pos.xy, Scale.xy
	@location(2) i_data1: vec4<f32>,  // Color RGBA
	@location(3) i_data2: vec4<f32>,  // rotation (radians), _, _, _
	@location(4) i_data3: vec4<f32>,  // Sprite slice UV rect: (u0, v0, u1, v1)
	@location(5) i_data4: vec4<f32>,  // Mask rect: (minX, minY, invW, invH), zero inv disables
	@location(6) i_data5: vec4<f32>,  // Mask inverse rotation: (pivotX, pivotY, cos, sin)
	@location(7) i_data6: vec4<f32>,  // Mask UV rect: (u0, v0, u1, v1)
};

struct VertexOutput {
	@builtin(position) clip_position: vec4<f32>,
	@location(0) color: vec4<f32>,
	@location(1) uv: vec2<f32>,
	@location(2) world: vec2<f32>,
	@location(3) maskRect: vec4<f32>,
	@location(4) maskRot: vec4<f32>,
	@location(5) maskUv: vec4<f32>,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
	// Per-instance scale, then rotate by the rotation angle, then translate.
	// sin/cos previously ran once per instance on the CPU during
	// EncodeInstance44; doing it here parallelises it across the GPU and
	// keeps the CPU encode path as branch-free struct stores.
	let scaled = in.position.xy * in.i_data0.zw;
	let r = in.i_data2.x;
	let c = cos(r);
	let s = sin(r);
	let rotated = vec2<f32>(
		scaled.x * c - scaled.y * s,
		scaled.x * s + scaled.y * c
	);
	let world = rotated + in.i_data0.xy;

	var out: VertexOutput;
	out.clip_position = u.viewProj * vec4<f32>(world, 0.0, 1.0);
	out.color = in.i_data1;
	out.world = world;
	out.maskRect = in.i_data4;
	out.maskRot = in.i_data5;
	out.maskUv = in.i_data6;
	// Unit-quad UV: [-0.5, 0.5] -> [0, 1] on X, Y flipped so texture top-left
	// shows at the quad's visual top (engine has Y-up world space; textures
	// are top-left origin per stb's load).
	let baseUV = vec2<f32>(in.position.x + 0.5, 0.5 - in.position.y);
	// Remap unit-quad UV into the sprite-slice rect via mix(). For the
	// default full-texture case (UvRect = 0,0,1,1) this collapses to
	// identity — every legacy SpriteRenderer renders exactly as before.
	out.uv = vec2<f32>(
		mix(in.i_data3.x, in.i_data3.z, baseUV.x),
		mix(in.i_data3.y, in.i_data3.w, baseUV.y)
	);
	return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
	let texel = textureSample(t_albedo, s_albedo, in.uv);
	var color = texel * in.color;

	if (in.maskRect.z != 0.0 && in.maskRect.w != 0.0) {
		let delta = in.world - in.maskRot.xy;
		let local = vec2<f32>(
			delta.x * in.maskRot.z - delta.y * in.maskRot.w,
			delta.x * in.maskRot.w + delta.y * in.maskRot.z
		) + in.maskRot.xy;

		let mask01 = vec2<f32>(
			(local.x - in.maskRect.x) * in.maskRect.z,
			(local.y - in.maskRect.y) * in.maskRect.w
		);
		if (mask01.x < 0.0 || mask01.x > 1.0 || mask01.y < 0.0 || mask01.y > 1.0) {
			discard;
		}

		let maskSampleUv = vec2<f32>(
			mix(in.maskUv.x, in.maskUv.z, mask01.x),
			mix(in.maskUv.y, in.maskUv.w, 1.0 - mask01.y)
		);
		let maskAlpha = textureSampleLevel(t_mask, s_mask, maskSampleUv, 0.0).a;
		if (maskAlpha <= 0.001) {
			discard;
		}
		color.a = color.a * maskAlpha;
	}

	return color;
}

// Wireframe-debug fragment entry point. Used by the Wireframe sprite
// pipeline (LineList topology, blending disabled) — outputs solid opaque
// black regardless of texture or instance colour so quad-edge lines render
// as the Editor View's debug overlay. Keeping the black-write in the
// shader (rather than mutating the shared instance buffer on the CPU)
// avoids aliasing the filled-pass's instance data in Mixed draw mode.
@fragment
fn fs_wire_main(in: VertexOutput) -> @location(0) vec4<f32> {
	return vec4<f32>(0.0, 0.0, 0.0, 1.0);
}
)WGSL";

		constexpr const char* k_TextWGSL = R"WGSL(
struct Uniforms {
	viewProj: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var t_atlas: texture_2d<f32>;
@group(0) @binding(2) var s_atlas: sampler;
@group(0) @binding(3) var t_text_mask: texture_2d<f32>;
@group(0) @binding(4) var s_text_mask: sampler;

struct VertexInput {
	@location(0) position: vec2<f32>,
	@location(1) uv:       vec2<f32>,
	@location(2) color:    vec4<f32>,
	@location(3) maskRect: vec4<f32>,
	@location(4) maskRot:  vec4<f32>,
	@location(5) maskUv:   vec4<f32>,
};

struct VertexOutput {
	@builtin(position) clip_position: vec4<f32>,
	@location(0) color: vec4<f32>,
	@location(1) uv:    vec2<f32>,
	@location(2) world: vec2<f32>,
	@location(3) maskRect: vec4<f32>,
	@location(4) maskRot: vec4<f32>,
	@location(5) maskUv: vec4<f32>,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
	var out: VertexOutput;
	out.clip_position = u.viewProj * vec4<f32>(in.position, 0.0, 1.0);
	out.color = in.color;
	out.uv = in.uv;
	out.world = in.position;
	out.maskRect = in.maskRect;
	out.maskRot = in.maskRot;
	out.maskUv = in.maskUv;
	return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
	let alpha = textureSample(t_atlas, s_atlas, in.uv).r;
	var outAlpha = in.color.a * alpha;

	if (in.maskRect.z != 0.0 && in.maskRect.w != 0.0) {
		let delta = in.world - in.maskRot.xy;
		let local = vec2<f32>(
			delta.x * in.maskRot.z - delta.y * in.maskRot.w,
			delta.x * in.maskRot.w + delta.y * in.maskRot.z
		) + in.maskRot.xy;

		let mask01 = vec2<f32>(
			(local.x - in.maskRect.x) * in.maskRect.z,
			(local.y - in.maskRect.y) * in.maskRect.w
		);
		if (mask01.x < 0.0 || mask01.x > 1.0 || mask01.y < 0.0 || mask01.y > 1.0) {
			discard;
		}

		let maskSampleUv = vec2<f32>(
			mix(in.maskUv.x, in.maskUv.z, mask01.x),
			mix(in.maskUv.y, in.maskUv.w, 1.0 - mask01.y)
		);
		let maskAlpha = textureSampleLevel(t_text_mask, s_text_mask, maskSampleUv, 0.0).a;
		if (maskAlpha <= 0.001) {
			discard;
		}
		outAlpha = outAlpha * maskAlpha;
	}

	return vec4<f32>(in.color.rgb, outAlpha);
}
)WGSL";

		constexpr const char* k_GizmoWGSL = R"WGSL(
struct Uniforms {
	viewProj: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> u: Uniforms;

struct VertexInput {
	@location(0) position: vec3<f32>,
	@location(1) color:    vec4<f32>,
};

struct VertexOutput {
	@builtin(position) clip_position: vec4<f32>,
	@location(0) color: vec4<f32>,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
	var out: VertexOutput;
	out.clip_position = u.viewProj * vec4<f32>(in.position, 1.0);
	out.color = in.color;
	return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
	return in.color;
}
)WGSL";

#if FUNKE_WITH_3D
		constexpr const char* k_MeshWGSL = R"WGSL(
const PI: f32 = 3.14159265;
const MAX_LIGHTS: u32 = 64u;

struct Frame {
	viewProj:      mat4x4<f32>,
	lightViewProj: mat4x4<f32>,
	cameraPos:     vec4<f32>,  // w = exposure
	ambient:       vec4<f32>,  // linear flat ambient, w = sky ambient strength
	skyTop:        vec4<f32>,  // linear
	skyHorizon:    vec4<f32>,
	skyGround:     vec4<f32>,
	params:        vec4<f32>,  // x = light count, y = tone mapping, z = 1 encodes sRGB, w = shadowed light index or -1
	shadowParams:  vec4<f32>,  // x = texel size in uv, y = depth bias, z = normal offset, w = shadow distance
	fogColor:      vec4<f32>,  // linear, w = 1 when fog is on
	fogParams:     vec4<f32>,  // x = start, y = end
};

struct Light {
	positionRange: vec4<f32>,
	directionType: vec4<f32>,  // xyz = direction the light travels, w = 0 directional / 1 point / 2 spot
	radiance:      vec4<f32>,  // linear color * intensity
	spot:          vec4<f32>,  // x = cos inner, y = cos outer
};

struct Lights {
	items: array<Light, 64>,
};

@group(0) @binding(0) var<uniform> frame: Frame;
@group(0) @binding(1) var<uniform> lights: Lights;
@group(0) @binding(2) var shadowMap: texture_depth_2d;
@group(0) @binding(3) var shadowSampler: sampler_comparison;
@group(1) @binding(0) var baseTexture: texture_2d<f32>;
@group(1) @binding(1) var baseSampler: sampler;

struct VertexInput {
	@location(0)  position:    vec3<f32>,
	@location(1)  normal:      vec3<f32>,
	@location(2)  uv:          vec2<f32>,
	@location(3)  model0:      vec4<f32>,
	@location(4)  model1:      vec4<f32>,
	@location(5)  model2:      vec4<f32>,
	@location(6)  model3:      vec4<f32>,
	@location(7)  normal0:     vec4<f32>,
	@location(8)  normal1:     vec4<f32>,
	@location(9)  normal2:     vec4<f32>,
	@location(10) color:       vec4<f32>,  // sRGB base color * tint
	@location(11) emissive:    vec4<f32>,  // linear, w = 1 when fog applies
	@location(12) surface:     vec4<f32>,  // x = metallic, y = roughness, z = lit, w = transparent
	@location(13) uvTransform: vec4<f32>,  // xy = tiling, zw = offset
};

struct VertexOutput {
	@builtin(position) clip_position: vec4<f32>,
	@location(0) worldPos: vec3<f32>,
	@location(1) normal:   vec3<f32>,
	@location(2) uv:       vec2<f32>,
	@location(3) color:    vec4<f32>,
	@location(4) emissive: vec3<f32>,
	@location(5) @interpolate(flat) surface: vec4<f32>,
	@location(6) @interpolate(flat) fogged: f32,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
	let model = mat4x4<f32>(in.model0, in.model1, in.model2, in.model3);
	let normalMatrix = mat3x3<f32>(in.normal0.xyz, in.normal1.xyz, in.normal2.xyz);
	let world = model * vec4<f32>(in.position, 1.0);
	var out: VertexOutput;
	out.clip_position = frame.viewProj * world;
	out.worldPos = world.xyz;
	out.normal = normalMatrix * in.normal;
	out.uv = in.uv * in.uvTransform.xy + in.uvTransform.zw;
	out.color = in.color;
	out.emissive = in.emissive.rgb;
	out.surface = in.surface;
	out.fogged = in.emissive.w;
	return out;
}

// Editor wireframe (LineList). Edges are pulled slightly towards the camera so they pass the depth test against the
// surfaces they outline in the Mixed draw mode.
const WIRE_PULL: f32 = 0.003;

@vertex
fn vs_wire(in: VertexInput) -> @builtin(position) vec4<f32> {
	let model = mat4x4<f32>(in.model0, in.model1, in.model2, in.model3);
	let world = (model * vec4<f32>(in.position, 1.0)).xyz;
	// An orthographic view-projection has (0, 0, 0, 1) as its last row; its rays are parallel, so pull in depth only.
	if (abs(frame.viewProj[0].w) + abs(frame.viewProj[1].w) + abs(frame.viewProj[2].w) > 0.0) {
		return frame.viewProj * vec4<f32>(mix(world, frame.cameraPos.xyz, WIRE_PULL), 1.0);
	}
	var clip = frame.viewProj * vec4<f32>(world, 1.0);
	clip.z = clip.z * (1.0 - WIRE_PULL);
	return clip;
}

@fragment
fn fs_wire() -> @location(0) vec4<f32> {
	return vec4<f32>(0.0, 0.0, 0.0, 1.0);
}

// Distance fog blended over the display-range color (after tone mapping), so FogColor shows as configured.
fn applyFog(color: vec3<f32>, worldPos: vec3<f32>, fogged: f32) -> vec3<f32> {
	if (frame.fogColor.w < 0.5 || fogged < 0.5) {
		return color;
	}
	let dist = length(worldPos - frame.cameraPos.xyz);
	let t = clamp((dist - frame.fogParams.x) / max(frame.fogParams.y - frame.fogParams.x, 1e-3), 0.0, 1.0);
	return mix(color, frame.fogColor.rgb, t * t * (3.0 - 2.0 * t));
}

fn srgbToLinear(c: vec3<f32>) -> vec3<f32> {
	let lo = c / 12.92;
	let hi = pow((c + vec3<f32>(0.055)) / 1.055, vec3<f32>(2.4));
	return select(hi, lo, c <= vec3<f32>(0.04045));
}

fn linearToSrgb(c: vec3<f32>) -> vec3<f32> {
	let x = max(c, vec3<f32>(0.0));
	let lo = x * 12.92;
	let hi = 1.055 * pow(x, vec3<f32>(1.0 / 2.4)) - vec3<f32>(0.055);
	return select(hi, lo, x <= vec3<f32>(0.0031308));
}

fn toneMap(c: vec3<f32>) -> vec3<f32> {
	let mode = frame.params.y;
	if (mode < 0.5) {
		return clamp(c, vec3<f32>(0.0), vec3<f32>(1.0));
	}
	if (mode < 1.5) {
		return c / (vec3<f32>(1.0) + c);
	}
	// ACES filmic fit (Narkowicz).
	return clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), vec3<f32>(0.0), vec3<f32>(1.0));
}

fn encodeOutput(c: vec3<f32>) -> vec3<f32> {
	if (frame.params.z > 0.5) {
		return linearToSrgb(c);
	}
	return c;
}

// Same gradient as the "sky" shader, evaluated in linear space.
fn skyColor(dir: vec3<f32>) -> vec3<f32> {
	let up = dir.y;
	if (up >= 0.0) {
		return mix(frame.skyHorizon.rgb, frame.skyTop.rgb, pow(up, 0.6));
	}
	return mix(frame.skyHorizon.rgb, frame.skyGround.rgb, smoothstep(0.0, 0.2, -up));
}

fn distributionGGX(NoH: f32, a: f32) -> f32 {
	let a2 = a * a;
	let f = (NoH * a2 - NoH) * NoH + 1.0;
	return a2 / (PI * f * f);
}

fn visibilitySmithGGX(NoV: f32, NoL: f32, a: f32) -> f32 {
	let a2 = a * a;
	let ggxL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
	let ggxV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
	return 0.5 / max(ggxV + ggxL, 1e-5);
}

fn fresnelSchlick(VoH: f32, f0: vec3<f32>) -> vec3<f32> {
	return f0 + (vec3<f32>(1.0) - f0) * pow(1.0 - VoH, 5.0);
}

// Karis' analytic fit of the split-sum environment BRDF.
fn envBRDF(f0: vec3<f32>, roughness: f32, NoV: f32) -> vec3<f32> {
	let c0 = vec4<f32>(-1.0, -0.0275, -0.572, 0.022);
	let c1 = vec4<f32>(1.0, 0.0425, 1.04, -0.04);
	let r = roughness * c0 + c1;
	let a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
	let ab = vec2<f32>(-1.04, 1.04) * a004 + r.zw;
	return f0 * ab.x + vec3<f32>(ab.y);
}

fn pointAttenuation(dist: f32, lightRange: f32) -> f32 {
	if (dist >= lightRange) {
		return 0.0;
	}
	let ratio = dist / lightRange;
	let window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
	return (window * window) / (dist * dist + 1.0);
}

fn shadowFactor(worldPos: vec3<f32>, normal: vec3<f32>) -> f32 {
	let lp = frame.lightViewProj * vec4<f32>(worldPos + normal * frame.shadowParams.z, 1.0);
	let ndc = lp.xyz / lp.w;
	let uv = vec2<f32>(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || ndc.z > 1.0) {
		return 1.0;
	}
	let depth = ndc.z - frame.shadowParams.y;
	let texel = frame.shadowParams.x;
	var sum = 0.0;
	for (var y = -1; y <= 1; y++) {
		for (var x = -1; x <= 1; x++) {
			sum += textureSampleCompareLevel(shadowMap, shadowSampler, uv + vec2<f32>(f32(x), f32(y)) * texel, depth);
		}
	}
	let fade = smoothstep(frame.shadowParams.w * 0.85, frame.shadowParams.w, length(worldPos - frame.cameraPos.xyz));
	return mix(sum / 9.0, 1.0, fade);
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
	let texel = textureSample(baseTexture, baseSampler, in.uv);
	let albedo = srgbToLinear(in.color.rgb * texel.rgb);
	// Opaque surfaces keep alpha 1 so the target stays opaque.
	let alpha = select(1.0, clamp(in.color.a * texel.a, 0.0, 1.0), in.surface.w > 0.5);
	if (in.surface.z < 0.5) {
		return vec4<f32>(encodeOutput(applyFog(albedo + in.emissive, in.worldPos, in.fogged)), alpha);
	}

	let metallic = clamp(in.surface.x, 0.0, 1.0);
	let roughness = clamp(in.surface.y, 0.045, 1.0);
	let a = roughness * roughness;
	let n = normalize(in.normal);
	let v = normalize(frame.cameraPos.xyz - in.worldPos);
	let NoV = max(dot(n, v), 1e-4);
	let f0 = mix(vec3<f32>(0.04), albedo, metallic);
	let diffuseColor = albedo * (1.0 - metallic);

	var color = vec3<f32>(0.0);
	let count = min(u32(frame.params.x), MAX_LIGHTS);
	let shadowedLight = i32(frame.params.w);
	for (var i = 0u; i < count; i++) {
		let light = lights.items[i];
		let lightType = light.directionType.w;
		var l = -light.directionType.xyz;
		var attenuation = 1.0;
		if (lightType > 0.5) {
			let toLight = light.positionRange.xyz - in.worldPos;
			let dist = length(toLight);
			l = toLight / max(dist, 1e-4);
			attenuation = pointAttenuation(dist, light.positionRange.w);
			if (lightType > 1.5) {
				let cosAngle = dot(-l, light.directionType.xyz);
				let t = clamp((cosAngle - light.spot.y) / max(light.spot.x - light.spot.y, 1e-4), 0.0, 1.0);
				attenuation *= t * t;
			}
		}
		let NoL = dot(n, l);
		if (NoL <= 0.0 || attenuation <= 0.0) {
			continue;
		}
		if (i32(i) == shadowedLight) {
			attenuation *= shadowFactor(in.worldPos, n);
		}
		let h = normalize(l + v);
		let NoH = max(dot(n, h), 0.0);
		let VoH = max(dot(v, h), 0.0);
		let f = fresnelSchlick(VoH, f0);
		let specular = distributionGGX(NoH, a) * visibilitySmithGGX(NoV, NoL, a) * f;
		let diffuse = diffuseColor * (vec3<f32>(1.0) - f) / PI;
		// Scaled by PI so a white light of intensity 1 lights a white surface head-on to 1.
		color += (diffuse + specular) * light.radiance.rgb * (attenuation * NoL * PI);
	}

	let skyStrength = frame.ambient.w;
	let r = reflect(-v, n);
	let ambientDiffuse = frame.ambient.rgb + skyColor(n) * skyStrength;
	let ambientSpecular = frame.ambient.rgb + skyColor(normalize(mix(r, n, a))) * skyStrength;
	let reflectance = envBRDF(f0, roughness, NoV);
	color += diffuseColor * ambientDiffuse + reflectance * ambientSpecular;
	color += in.emissive;

	// Reflected light isn't filtered by the surface's transparency: glass and water turn mirror-like at grazing angles.
	let fresnelAlpha = clamp(alpha + (1.0 - alpha) * max(reflectance.r, max(reflectance.g, reflectance.b)), 0.0, 1.0);
	return vec4<f32>(encodeOutput(applyFog(toneMap(color * frame.cameraPos.w), in.worldPos, in.fogged)),
		select(alpha, fresnelAlpha, in.surface.w > 0.5));
}
)WGSL";

		constexpr const char* k_SkyWGSL = R"WGSL(
struct Sky {
	inverseViewProj: mat4x4<f32>,
	top:          vec4<f32>,  // display (sRGB) colors
	horizon:      vec4<f32>,
	ground:       vec4<f32>,
	sunDirection: vec4<f32>,  // xyz = towards the sun, w = cos of the disc radius
	sunColor:     vec4<f32>,  // a = 1 when the sun is drawn
	params:       vec4<f32>,  // x = 1 when the target format is sRGB (output must be linear)
};

@group(0) @binding(0) var<uniform> sky: Sky;

struct VertexOutput {
	@builtin(position) clip_position: vec4<f32>,
	@location(0) nearPoint: vec4<f32>,
	@location(1) farPoint:  vec4<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) index: u32) -> VertexOutput {
	// One triangle covering the viewport; homogeneous points stay linear in screen space.
	let uv = vec2<f32>(f32((index << 1u) & 2u), f32(index & 2u));
	let ndc = uv * 2.0 - vec2<f32>(1.0);
	var out: VertexOutput;
	out.clip_position = vec4<f32>(ndc, 0.0, 1.0);
	out.nearPoint = sky.inverseViewProj * vec4<f32>(ndc, 0.0, 1.0);
	out.farPoint = sky.inverseViewProj * vec4<f32>(ndc, 1.0, 1.0);
	return out;
}

fn srgbToLinear(c: vec3<f32>) -> vec3<f32> {
	let lo = c / 12.92;
	let hi = pow((c + vec3<f32>(0.055)) / 1.055, vec3<f32>(2.4));
	return select(hi, lo, c <= vec3<f32>(0.04045));
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
	let dir = normalize(in.farPoint.xyz / in.farPoint.w - in.nearPoint.xyz / in.nearPoint.w);
	let up = dir.y;
	var color: vec3<f32>;
	if (up >= 0.0) {
		color = mix(sky.horizon.rgb, sky.top.rgb, pow(up, 0.6));
	} else {
		color = mix(sky.horizon.rgb, sky.ground.rgb, smoothstep(0.0, 0.2, -up));
	}

	if (sky.sunColor.a > 0.5) {
		let cosAngle = dot(dir, sky.sunDirection.xyz);
		let edge = (1.0 - sky.sunDirection.w) * 0.25 + 1e-5;
		let disc = smoothstep(sky.sunDirection.w - edge, sky.sunDirection.w + edge, cosAngle);
		let glow = pow(max(cosAngle, 0.0), 256.0) * 0.6 + pow(max(cosAngle, 0.0), 16.0) * 0.12;
		let horizonFade = smoothstep(-0.05, 0.02, up);
		color = mix(color, sky.sunColor.rgb, disc * horizonFade) + sky.sunColor.rgb * glow * horizonFade;
	}

	color = clamp(color, vec3<f32>(0.0), vec3<f32>(1.0));
	if (sky.params.x > 0.5) {
		color = srgbToLinear(color);
	}
	return vec4<f32>(color, 1.0);
}
)WGSL";

		constexpr const char* k_Shadow3DWGSL = R"WGSL(
struct ShadowFrame {
	lightViewProj: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> shadowFrame: ShadowFrame;

struct VertexInput {
	@location(0) position: vec3<f32>,
	@location(1) model0:   vec4<f32>,
	@location(2) model1:   vec4<f32>,
	@location(3) model2:   vec4<f32>,
	@location(4) model3:   vec4<f32>,
};

@vertex
fn vs_main(in: VertexInput) -> @builtin(position) vec4<f32> {
	let model = mat4x4<f32>(in.model0, in.model1, in.model2, in.model3);
	return shadowFrame.lightViewProj * model * vec4<f32>(in.position, 1.0);
}
)WGSL";

		constexpr const char* k_Particle3DWGSL = R"WGSL(
struct ParticleFrame {
	viewProj:    mat4x4<f32>,
	cameraRight: vec4<f32>,
	cameraUp:    vec4<f32>,
	params:      vec4<f32>,  // x = 1 when the target format is sRGB (output must be linear)
};

@group(0) @binding(0) var<uniform> frame: ParticleFrame;
@group(1) @binding(0) var particleTexture: texture_2d<f32>;
@group(1) @binding(1) var particleSampler: sampler;

struct InstanceInput {
	@location(0) positionSize: vec4<f32>,  // xyz = world center, w = edge length
	@location(1) color:        vec4<f32>,  // sRGB
	@location(2) params:       vec4<f32>,  // x = rotation in radians, y = 1 draws a soft round dot instead of the texture
};

struct VertexOutput {
	@builtin(position) clip_position: vec4<f32>,
	@location(0) uv: vec2<f32>,
	@location(1) color: vec4<f32>,
	@location(2) @interpolate(flat) softDot: f32,
};

@vertex
fn vs_main(@builtin(vertex_index) index: u32, in: InstanceInput) -> VertexOutput {
	var corners = array<vec2<f32>, 6>(
		vec2<f32>(-0.5, -0.5), vec2<f32>(0.5, -0.5), vec2<f32>(-0.5, 0.5),
		vec2<f32>(-0.5, 0.5), vec2<f32>(0.5, -0.5), vec2<f32>(0.5, 0.5));
	let corner = corners[index];
	let c = cos(in.params.x);
	let s = sin(in.params.x);
	let rotated = vec2<f32>(corner.x * c - corner.y * s, corner.x * s + corner.y * c);
	let world = in.positionSize.xyz + (frame.cameraRight.xyz * rotated.x + frame.cameraUp.xyz * rotated.y) * in.positionSize.w;
	var out: VertexOutput;
	out.clip_position = frame.viewProj * vec4<f32>(world, 1.0);
	out.uv = vec2<f32>(corner.x + 0.5, 0.5 - corner.y);
	out.color = in.color;
	out.softDot = in.params.y;
	return out;
}

fn srgbToLinear(c: vec3<f32>) -> vec3<f32> {
	let lo = c / 12.92;
	let hi = pow((c + vec3<f32>(0.055)) / 1.055, vec3<f32>(2.4));
	return select(hi, lo, c <= vec3<f32>(0.04045));
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
	var texel = textureSample(particleTexture, particleSampler, in.uv);
	if (in.softDot > 0.5) {
		let a = clamp(1.0 - length(in.uv - vec2<f32>(0.5)) * 2.0, 0.0, 1.0);
		texel = vec4<f32>(1.0, 1.0, 1.0, a * a * (3.0 - 2.0 * a));
	}
	var rgb = clamp(in.color.rgb * texel.rgb, vec3<f32>(0.0), vec3<f32>(1.0));
	let alpha = clamp(in.color.a * texel.a, 0.0, 1.0);
	if (frame.params.x > 0.5) {
		rgb = srgbToLinear(rgb);
	}
	return vec4<f32>(rgb, alpha);
}
)WGSL";
#endif

		constexpr const char* k_PostBlitWGSL = R"WGSL(
@group(0) @binding(0) var t_src: texture_2d<f32>;
@group(0) @binding(1) var s_src: sampler;

struct VertexOutput {
	@builtin(position) clip_position: vec4<f32>,
	@location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> VertexOutput {
	// Fullscreen triangle covering the viewport. Vertices in NDC:
	//   idx 0 -> (-1, -1)    uv (0, 1)
	//   idx 1 -> ( 3, -1)    uv (2, 1)
	//   idx 2 -> (-1,  3)    uv (0, -1)
	// The triangle extends past the clip rect; the rasterizer discards
	// the off-screen pixels. One triangle instead of a quad's two means
	// no diagonal seam where the two halves meet.
	var pos = array<vec2<f32>, 3>(
		vec2<f32>(-1.0, -1.0),
		vec2<f32>( 3.0, -1.0),
		vec2<f32>(-1.0,  3.0),
	);
	var uv = array<vec2<f32>, 3>(
		vec2<f32>(0.0, 1.0),
		vec2<f32>(2.0, 1.0),
		vec2<f32>(0.0, -1.0),
	);
	var out: VertexOutput;
	out.clip_position = vec4<f32>(pos[idx], 0.0, 1.0);
	out.uv = uv[idx];
	return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
	return textureSample(t_src, s_src, in.uv);
}
)WGSL";

		struct BuiltIn {
			std::string_view Name;
			const char*      WGSL;
		};

		// Bloom: threshold extract at half-res, then H and V blur; postcomposite adds it to the scene.
		// Threshold extract — Uniform: { threshold, _, _, _ }.
		constexpr const char* k_PostBloomThresholdWGSL = R"WGSL(
struct Params { p: vec4<f32>, };
@group(0) @binding(0) var t_src: texture_2d<f32>;
@group(0) @binding(1) var s_src: sampler;
@group(0) @binding(2) var<uniform> u: Params;

struct VertexOutput {
	@builtin(position) clip_position: vec4<f32>,
	@location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> VertexOutput {
	var pos = array<vec2<f32>, 3>(
		vec2<f32>(-1.0, -1.0), vec2<f32>( 3.0, -1.0), vec2<f32>(-1.0,  3.0));
	var uv = array<vec2<f32>, 3>(
		vec2<f32>(0.0, 1.0), vec2<f32>(2.0, 1.0), vec2<f32>(0.0, -1.0));
	var out: VertexOutput;
	out.clip_position = vec4<f32>(pos[idx], 0.0, 1.0);
	out.uv = uv[idx];
	return out;
}

fn luma(c: vec3<f32>) -> f32 {
	return dot(c, vec3<f32>(0.2126, 0.7152, 0.0722));
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
	let scene = textureSample(t_src, s_src, in.uv).rgb;
	let l = luma(scene);
	// Soft-knee threshold. The /0.5 keeps the falloff narrow enough
	// that a pixel only slightly over threshold contributes a small
	// fraction (avoids a hard binary cutoff).
	let excess = max(l - u.p.x, 0.0);
	let knee = excess / (excess + 0.5);
	return vec4<f32>(scene * knee, 1.0);
}
)WGSL";

		// Separable Gaussian blur, shared by H and V passes. Uniform: { dirX, dirY, texelSize, halfKernel }; halfK is a uniform so Dawn accepts the runtime-bounded loop.
		constexpr const char* k_PostBloomBlurWGSL = R"WGSL(
struct Params { p: vec4<f32>, };
@group(0) @binding(0) var t_src: texture_2d<f32>;
@group(0) @binding(1) var s_src: sampler;
@group(0) @binding(2) var<uniform> u: Params;

struct VertexOutput {
	@builtin(position) clip_position: vec4<f32>,
	@location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> VertexOutput {
	var pos = array<vec2<f32>, 3>(
		vec2<f32>(-1.0, -1.0), vec2<f32>( 3.0, -1.0), vec2<f32>(-1.0,  3.0));
	var uv = array<vec2<f32>, 3>(
		vec2<f32>(0.0, 1.0), vec2<f32>(2.0, 1.0), vec2<f32>(0.0, -1.0));
	var out: VertexOutput;
	out.clip_position = vec4<f32>(pos[idx], 0.0, 1.0);
	out.uv = uv[idx];
	return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
	let dir = u.p.xy;
	let texel = u.p.z;
	// Clamp upper bound bumped to 250 so the user-facing Taps slider
	// (7..500 taps per pass, mapping to halfK 3..250) fits with
	// headroom. Past ~250 the Gaussian tail is microscopic so the
	// extra taps are wasted GPU work, but the upper bound matches the
	// inspector's slider range. Loop is uniform-bounded across the
	// dispatch (halfK is a uniform), which lets Dawn unroll up to a
	// driver-dependent point.
	let half_k = i32(clamp(u.p.w, 1.0, 250.0));
	let half_kf = f32(half_k);

	// sigma chosen so the kernel-edge tap is ~exp(-4.5) ~ 0.011 of the
	// centre tap (3-sigma covers the kernel half). That gives a clean
	// Gaussian falloff without leaving a hard edge at the kernel boundary.
	let sigma = half_kf / 3.0;
	let sigma_inv2 = 1.0 / (2.0 * sigma * sigma);

	var result = vec3<f32>(0.0);
	var weight_sum = 0.0;
	for (var i = -half_k; i <= half_k; i = i + 1) {
		let offset = dir * f32(i) * texel;
		let sample = textureSample(t_src, s_src, in.uv + offset).rgb;
		let w = exp(-f32(i * i) * sigma_inv2);
		result = result + sample * w;
		weight_sum = weight_sum + w;
	}
	return vec4<f32>(result / weight_sum, 1.0);
}
)WGSL";

		// Every per-pixel effect in one full-screen pass: bloom composite, color grading, lens distortion,
		// chromatic aberration, vignette and grain, in the order the effect list runs them. Bindings:
		// scene@0, sampler@1, bloom@2 (the scene again when bloom is off), uniform@3.
		constexpr const char* k_PostCompositeWGSL = R"WGSL(
struct Params {
	flags0:          vec4<f32>,  // bloom, colorGrading, lens, chromatic
	flags1:          vec4<f32>,  // vignette, grain, time, aspect
	bloom:           vec4<f32>,  // tint rgb (premultiplied by tint alpha), intensity
	cgFilter:        vec4<f32>,
	cgExposureCS:    vec4<f32>,  // exposure, contrast, saturation
	cgTempTint:      vec4<f32>,
	lens:            vec4<f32>,  // intensity, scale, center
	chroma:          vec4<f32>,  // intensity
	vigColor:        vec4<f32>,
	vigCenterSmooth: vec4<f32>,  // center, intensity, smoothness
	vigRound:        vec4<f32>,  // roundness
	grain:           vec4<f32>,  // intensity, size, colored
};
@group(0) @binding(0) var t_scene: texture_2d<f32>;
@group(0) @binding(1) var s_lin:   sampler;
@group(0) @binding(2) var t_bloom: texture_2d<f32>;
@group(0) @binding(3) var<uniform> u: Params;

struct VertexOutput {
	@builtin(position) clip_position: vec4<f32>,
	@location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> VertexOutput {
	var pos = array<vec2<f32>, 3>(
		vec2<f32>(-1.0, -1.0), vec2<f32>( 3.0, -1.0), vec2<f32>(-1.0,  3.0));
	var uv = array<vec2<f32>, 3>(
		vec2<f32>(0.0, 1.0), vec2<f32>(2.0, 1.0), vec2<f32>(0.0, -1.0));
	var out: VertexOutput;
	out.clip_position = vec4<f32>(pos[idx], 0.0, 1.0);
	out.uv = uv[idx];
	return out;
}

fn grade(c_in: vec3<f32>) -> vec3<f32> {
	// Exposure (stops), color filter, then a cheap temperature/tint shift.
	var c = c_in * exp2(u.cgExposureCS.x) * u.cgFilter.rgb;
	c.r = c.r + u.cgTempTint.x * 0.1;
	c.b = c.b - u.cgTempTint.x * 0.1;
	c.g = c.g + u.cgTempTint.y * 0.1;
	c = max(c, vec3<f32>(0.0));
	// Contrast around mid grey, then saturation towards Rec.709 luma.
	c = max((c - 0.5) * u.cgExposureCS.y + 0.5, vec3<f32>(0.0));
	let luma = dot(c, vec3<f32>(0.2126, 0.7152, 0.0722));
	return mix(vec3<f32>(luma), c, u.cgExposureCS.z);
}

// The image the effects before chromatic aberration produce at p: scene + bloom, graded, lens-warped.
fn shade(p: vec2<f32>) -> vec4<f32> {
	var uv = p;
	var edge = 1.0;
	if (u.flags0.z > 0.5) {
		// Polynomial distortion; > 0 barrel, < 0 pincushion. Samples past the frame fade to black.
		let center = u.lens.zw;
		let d = p - center;
		let warped = d * (1.0 + u.lens.x * dot(d, d) * 2.0);
		let src_uv = center + warped / max(u.lens.y, 0.001);
		let edge_d = min(min(src_uv.x, 1.0 - src_uv.x), min(src_uv.y, 1.0 - src_uv.y));
		edge = smoothstep(-0.02, 0.0, edge_d);
		uv = clamp(src_uv, vec2<f32>(0.0), vec2<f32>(1.0));
	}
	var c = textureSampleLevel(t_scene, s_lin, uv, 0.0);
	if (u.flags0.x > 0.5) {
		let bloom = textureSampleLevel(t_bloom, s_lin, uv, 0.0).rgb;
		c = vec4<f32>(c.rgb + bloom * u.bloom.rgb * u.bloom.a, c.a);
	}
	if (u.flags0.y > 0.5) {
		c = vec4<f32>(grade(c.rgb), c.a);
	}
	return vec4<f32>(c.rgb * edge, c.a);
}

// IQ-style cheap hash. Stable per UV; the time seed perturbs each frame.
fn hash21(p: vec2<f32>) -> f32 {
	let q = fract(p * vec2<f32>(123.34, 456.21));
	let r = q + dot(q, q + 78.233);
	return fract(r.x * r.y);
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
	var c: vec4<f32>;
	if (u.flags0.w > 0.5) {
		// Offset grows towards the corners, like a real lens defect.
		let offset = (in.uv - vec2<f32>(0.5, 0.5)) * u.chroma.x * 0.05;
		let center = shade(in.uv);
		let r = shade(clamp(in.uv + offset, vec2<f32>(0.0), vec2<f32>(1.0))).r;
		let b = shade(clamp(in.uv - offset, vec2<f32>(0.0), vec2<f32>(1.0))).b;
		c = vec4<f32>(r, center.g, b, center.a);
	}
	else {
		c = shade(in.uv);
	}

	if (u.flags1.x > 0.5) {
		// roundness 1 = UV circle, 0 = aspect-corrected screen circle; 1.4142 puts r = 1 at a corner.
		let stretch = mix(u.flags1.w, 1.0, u.vigRound.x);
		let d = in.uv - u.vigCenterSmooth.xy;
		let r = length(vec2<f32>(d.x * stretch, d.y)) * 1.4142;
		let inner = 1.0 - u.vigCenterSmooth.z;
		let mask = smoothstep(inner, inner + max(u.vigCenterSmooth.w, 0.0001), r);
		c = vec4<f32>(mix(c.rgb, u.vigColor.rgb, mask), c.a);
	}

	if (u.flags1.y > 0.5) {
		let seed = in.uv * (1.0 / max(u.grain.y, 0.001)) * 1000.0 + vec2<f32>(u.flags1.z * 17.0, u.flags1.z * 23.0);
		let mono = hash21(seed) - 0.5;
		var noise = vec3<f32>(mono, mono, mono);
		if (u.grain.z > 0.5) {
			noise = vec3<f32>(mono, hash21(seed + vec2<f32>(11.7, 4.3)) - 0.5, hash21(seed + vec2<f32>(31.2, 17.9)) - 0.5);
		}
		c = vec4<f32>(c.rgb + noise * u.grain.x, c.a);
	}
	return c;
}
)WGSL";

		// Uniform: p = { BlockSize, ViewportWidth, ViewportHeight, PaletteSteps } (0=palette off, >=2=levels/channel).
		constexpr const char* k_PostPixelatedWGSL = R"WGSL(
struct Params { p: vec4<f32>, };
@group(0) @binding(0) var t_src: texture_2d<f32>;
@group(0) @binding(1) var s_src: sampler;
@group(0) @binding(2) var<uniform> u: Params;

struct VertexOutput {
	@builtin(position) clip_position: vec4<f32>,
	@location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> VertexOutput {
	var pos = array<vec2<f32>, 3>(
		vec2<f32>(-1.0, -1.0), vec2<f32>( 3.0, -1.0), vec2<f32>(-1.0,  3.0));
	var uv = array<vec2<f32>, 3>(
		vec2<f32>(0.0, 1.0), vec2<f32>(2.0, 1.0), vec2<f32>(0.0, -1.0));
	var out: VertexOutput;
	out.clip_position = vec4<f32>(pos[idx], 0.0, 1.0);
	out.uv = uv[idx];
	return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
	let block = max(u.p.x, 1.0);
	let vw = max(u.p.y, 1.0);
	let vh = max(u.p.z, 1.0);
	let palette = u.p.w;

	// Snap UV to the centre of its block. floor((uv * pixelDims) / block)
	// gives the integer cell index; multiply back to UV and add a half-
	// block centre offset so the sample lands on cell centre instead of
	// cell edge (looks more stable when the grid scrolls).
	let cellsX = vw / block;
	let cellsY = vh / block;
	let cell = floor(vec2<f32>(in.uv.x * cellsX, in.uv.y * cellsY));
	let snappedUv = (cell + vec2<f32>(0.5, 0.5)) / vec2<f32>(cellsX, cellsY);

	var sampled = textureSample(t_src, s_src, snappedUv);

	if (palette >= 2.0) {
		// Per-channel quantise to `palette` levels: round(c * (n-1)) / (n-1).
		let n = palette - 1.0;
		sampled = vec4<f32>(
			round(sampled.r * n) / n,
			round(sampled.g * n) / n,
			round(sampled.b * n) / n,
			sampled.a
		);
	}
	return sampled;
}
)WGSL";

		// FXAA (edge-walking luma anti-aliasing) at the source's own resolution; fs_low / fs_medium / fs_high are the
		// quality levels. Colors are premultiplied and filtered with alpha, so a transparent scene composites cleanly.
		constexpr const char* k_PostFxaaWGSL = R"WGSL(
@group(0) @binding(0) var t_src: texture_2d<f32>;
@group(0) @binding(1) var s_src: sampler;

struct VertexOutput {
	@builtin(position) clip_position: vec4<f32>,
	@location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> VertexOutput {
	var pos = array<vec2<f32>, 3>(
		vec2<f32>(-1.0, -1.0), vec2<f32>( 3.0, -1.0), vec2<f32>(-1.0,  3.0));
	var uv = array<vec2<f32>, 3>(
		vec2<f32>(0.0, 1.0), vec2<f32>(2.0, 1.0), vec2<f32>(0.0, -1.0));
	var out: VertexOutput;
	out.clip_position = vec4<f32>(pos[idx], 0.0, 1.0);
	out.uv = uv[idx];
	return out;
}

fn sampleAt(uv: vec2<f32>) -> vec4<f32> {
	// Level 0 explicitly: the edge walk below is non-uniform control flow.
	return textureSampleLevel(t_src, s_src, uv, 0.0);
}

fn luma(c: vec4<f32>) -> f32 {
	return dot(c.rgb, vec3<f32>(0.299, 0.587, 0.114));
}

fn lumaAt(uv: vec2<f32>) -> f32 {
	return luma(sampleAt(uv));
}

fn fxaa(uv: vec2<f32>, quality: u32) -> vec4<f32> {
	// Search steps along the edge (in texels, the later ones jump), local contrast thresholds and how strongly
	// single-pixel features are softened.
	var stepCount = 12;
	var steps = array<f32, 12>(1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);
	var edgeThreshold = 0.125;
	var edgeThresholdMin = 0.0312;
	var subpixel = 0.75;
	if (quality == 0u) {
		stepCount = 5;
		steps = array<f32, 12>(1.0, 1.5, 2.0, 4.0, 12.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
		edgeThreshold = 0.25;
		edgeThresholdMin = 0.0833;
		subpixel = 0.5;
	} else if (quality == 1u) {
		stepCount = 8;
		steps = array<f32, 12>(1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0, 0.0, 0.0, 0.0, 0.0);
		edgeThreshold = 0.166;
		edgeThresholdMin = 0.0625;
	}

	let texel = 1.0 / vec2<f32>(textureDimensions(t_src, 0));
	let center = sampleAt(uv);
	let lumaM = luma(center);
	let lumaN = lumaAt(uv + vec2<f32>(0.0, -texel.y));
	let lumaS = lumaAt(uv + vec2<f32>(0.0, texel.y));
	let lumaW = lumaAt(uv + vec2<f32>(-texel.x, 0.0));
	let lumaE = lumaAt(uv + vec2<f32>(texel.x, 0.0));

	let lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
	let lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
	let lumaRange = lumaMax - lumaMin;
	if (lumaRange < max(edgeThresholdMin, lumaMax * edgeThreshold)) {
		return center;
	}

	let lumaNW = lumaAt(uv + vec2<f32>(-texel.x, -texel.y));
	let lumaNE = lumaAt(uv + vec2<f32>(texel.x, -texel.y));
	let lumaSW = lumaAt(uv + vec2<f32>(-texel.x, texel.y));
	let lumaSE = lumaAt(uv + vec2<f32>(texel.x, texel.y));
	let lumaNS = lumaN + lumaS;
	let lumaWE = lumaW + lumaE;
	let lumaWestCorners = lumaNW + lumaSW;
	let lumaEastCorners = lumaNE + lumaSE;
	let lumaNorthCorners = lumaNW + lumaNE;
	let lumaSouthCorners = lumaSW + lumaSE;

	// A horizontal edge changes along y: compare the second differences of the three columns and rows.
	let edgeHorizontal = abs(lumaWestCorners - 2.0 * lumaW) + 2.0 * abs(lumaNS - 2.0 * lumaM)
		+ abs(lumaEastCorners - 2.0 * lumaE);
	let edgeVertical = abs(lumaNorthCorners - 2.0 * lumaN) + 2.0 * abs(lumaWE - 2.0 * lumaM)
		+ abs(lumaSouthCorners - 2.0 * lumaS);
	let isHorizontal = edgeHorizontal >= edgeVertical;

	// Neighbours across the edge on the negative (1) and positive (2) side; blend towards the steeper one.
	let luma1 = select(lumaW, lumaN, isHorizontal);
	let luma2 = select(lumaE, lumaS, isHorizontal);
	let gradient1 = luma1 - lumaM;
	let gradient2 = luma2 - lumaM;
	let is1Steepest = abs(gradient1) >= abs(gradient2);
	let gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));
	var stepLength = select(texel.x, texel.y, isHorizontal);
	var lumaLocalAverage = 0.5 * (luma2 + lumaM);
	if (is1Steepest) {
		stepLength = -stepLength;
		lumaLocalAverage = 0.5 * (luma1 + lumaM);
	}

	// Walk both ways along the edge, half a texel towards the steeper side, until the luma leaves the edge.
	var edgeUv = uv;
	if (isHorizontal) {
		edgeUv.y += 0.5 * stepLength;
	} else {
		edgeUv.x += 0.5 * stepLength;
	}
	let along = select(vec2<f32>(0.0, texel.y), vec2<f32>(texel.x, 0.0), isHorizontal);
	var uv1 = edgeUv;
	var uv2 = edgeUv;
	var lumaEnd1 = 0.0;
	var lumaEnd2 = 0.0;
	var reached1 = false;
	var reached2 = false;
	for (var i = 0; i < stepCount; i++) {
		if (!reached1) {
			uv1 -= along * steps[i];
			lumaEnd1 = lumaAt(uv1) - lumaLocalAverage;
			reached1 = abs(lumaEnd1) >= gradientScaled;
		}
		if (!reached2) {
			uv2 += along * steps[i];
			lumaEnd2 = lumaAt(uv2) - lumaLocalAverage;
			reached2 = abs(lumaEnd2) >= gradientScaled;
		}
		if (reached1 && reached2) {
			break;
		}
	}

	// Pixels near the closer end of the edge shift the most; only when that end's luma moves the other way.
	let distance1 = select(uv.y - uv1.y, uv.x - uv1.x, isHorizontal);
	let distance2 = select(uv2.y - uv.y, uv2.x - uv.x, isHorizontal);
	let isDirection1 = distance1 < distance2;
	let distanceFinal = min(distance1, distance2);
	let edgeLength = distance1 + distance2;
	let pixelOffset = 0.5 - distanceFinal / edgeLength;
	let isLumaMSmaller = lumaM < lumaLocalAverage;
	let correctVariation = (select(lumaEnd2, lumaEnd1, isDirection1) < 0.0) != isLumaMSmaller;
	var finalOffset = select(0.0, pixelOffset, correctVariation);

	// Single-pixel features: blend towards the 3x3 average.
	let lumaAverage = (2.0 * (lumaNS + lumaWE) + lumaWestCorners + lumaEastCorners) / 12.0;
	let subpixelBlend = clamp(abs(lumaAverage - lumaM) / lumaRange, 0.0, 1.0);
	let subpixelSmooth = (3.0 - 2.0 * subpixelBlend) * subpixelBlend * subpixelBlend;
	finalOffset = max(finalOffset, subpixelSmooth * subpixelSmooth * subpixel);

	var finalUv = uv;
	if (isHorizontal) {
		finalUv.y += finalOffset * stepLength;
	} else {
		finalUv.x += finalOffset * stepLength;
	}
	return sampleAt(finalUv);
}

@fragment
fn fs_low(in: VertexOutput) -> @location(0) vec4<f32> {
	return fxaa(in.uv, 0u);
}

@fragment
fn fs_medium(in: VertexOutput) -> @location(0) vec4<f32> {
	return fxaa(in.uv, 1u);
}

@fragment
fn fs_high(in: VertexOutput) -> @location(0) vec4<f32> {
	return fxaa(in.uv, 2u);
}
)WGSL";

		constexpr BuiltIn k_BuiltIns[] = {
			{ "sprite",              k_SpriteWGSL              },
			{ "text",                k_TextWGSL                },
			{ "gizmo",               k_GizmoWGSL               },
#if FUNKE_WITH_3D
			{ "mesh",                k_MeshWGSL                },
			{ "sky",                 k_SkyWGSL                 },
			{ "shadow3d",            k_Shadow3DWGSL            },
			{ "particle3d",          k_Particle3DWGSL          },
#endif
			{ "postblit",            k_PostBlitWGSL            },
			{ "postbloomthreshold",  k_PostBloomThresholdWGSL  },
			{ "postbloomblur",       k_PostBloomBlurWGSL       },
			{ "postcomposite",       k_PostCompositeWGSL       },
			{ "postpixelated",       k_PostPixelatedWGSL       },
			{ "postfxaa",            k_PostFxaaWGSL            },
		};

		struct GpuShader {
			wgpu::ShaderModule Module;
			std::string        Name;
		};
		std::unordered_map<unsigned, GpuShader> g_Shaders;
		unsigned g_NextShaderId = 1;  // 0 reserved as "invalid"

		unsigned AllocateShaderSlot(GpuShader&& s) {
			unsigned id = g_NextShaderId++;
			if (id == 0) id = g_NextShaderId++;
			g_Shaders.emplace(id, std::move(s));
			return id;
		}

		void FreeShaderSlot(unsigned id) {
			if (id == 0) return;
			g_Shaders.erase(id);
		}

		// Stem extraction — strips path + extension, drops "Shader"
		// suffix, lowercases. Callers (WebGPUSpriteResources /
		// GizmoRenderer / TextRenderer) hit this to look up by name.
		std::string ExtractName(const std::string& path) {
			std::filesystem::path p(path);
			std::string stem = p.stem().string();
			static const std::string suffix = "Shader";
			if (stem.size() > suffix.size()
				&& stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0)
			{
				stem.erase(stem.size() - suffix.size());
			}
			for (char& c : stem) {
				if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
			}
			return stem;
		}

		const char* FindBuiltinWGSL(std::string_view name) {
			for (const BuiltIn& b : k_BuiltIns) {
				if (b.Name == name) return b.WGSL;
			}
			return nullptr;
		}

		std::string ResolveOnDiskWGSL(const std::string& name) {
			const std::string rel = std::string("FunkeAssets/Shaders/webgpu/") + name + ".wgsl";
			const std::string exeRel = Path::Combine(Path::ExecutableDir(), rel);
			if (std::filesystem::exists(exeRel)) return exeRel;
			if (std::filesystem::exists(rel)) return rel;
			return {};
		}

		wgpu::ShaderModule CompileWGSL(const std::string& name, std::string_view wgsl) {
			wgpu::Device device = WebGPUBackend::GetDevice();
			if (!device) {
				FNK_CORE_ERROR_TAG("Shader",
					"CompileWGSL '{}' called before WebGPU device exists", name);
				return nullptr;
			}

			wgpu::ShaderSourceWGSL src{};
			src.code = wgsl.data();

			wgpu::ShaderModuleDescriptor desc{};
			desc.nextInChain = &src;
			desc.label = name.c_str();

			wgpu::ShaderModule module = device.CreateShaderModule(&desc);
			if (!module) {
				FNK_CORE_ERROR_TAG("Shader",
					"wgpu::Device::CreateShaderModule returned null for '{}'", name);
				return module;
			}
			// A WGSL error still yields a non-null error module; using it would invalidate every pipeline and
			// silently drop frames, and it would replace a working shader on hot reload.
			std::string compileErrors;
			if (!WebGPUBackend::ShaderModuleCompiled(module, &compileErrors)) {
				FNK_CORE_ERROR_TAG("Shader", "WGSL compile failed for '{}':\n{}", name, compileErrors);
				return nullptr;
			}
			return module;
		}
	}

	// ── WebGPUBackend::LookupShader (declared in WebGPUBackend.hpp) ─────────
	namespace WebGPUBackend {
		ShaderLookup LookupShader(unsigned shaderHandleId) {
			if (shaderHandleId == 0) return ShaderLookup{};
			auto it = g_Shaders.find(shaderHandleId);
			if (it == g_Shaders.end()) return ShaderLookup{};
			ShaderLookup out;
			out.Module = it->second.Module;
			out.Valid  = static_cast<bool>(out.Module);
			return out;
		}
	}

	// ── Shader ──────────────────────────────────────────────────────────────

	Shader::Shader(const std::string& vsPath, const std::string& /*fsPath*/) {
		// fsPath ignored — WebGPU uses a single WGSL module; stem extracted from vsPath.
		const std::string name = ExtractName(vsPath);

		std::string wgslHolder;          // owns the on-disk source if loaded
		std::string_view wgslSource;     // points into the holder OR a built-in

		if (const char* builtin = FindBuiltinWGSL(name)) {
			wgslSource = builtin;
		} else {
			const std::string path = ResolveOnDiskWGSL(name);
			if (path.empty()) {
				FNK_CORE_WARN_TAG("Shader",
					"No WGSL source for '{}' (no built-in, no FunkeAssets/Shaders/webgpu/{}.wgsl) — Shader will be invalid",
					name, name);
				return;
			}
			std::vector<uint8_t> bytes = File::ReadAllBytes(path);
			if (bytes.empty()) {
				FNK_CORE_WARN_TAG("Shader", "WGSL file empty: {}", path);
				return;
			}
			wgslHolder.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
			wgslSource = wgslHolder;
		}

		wgpu::ShaderModule module = CompileWGSL(name, wgslSource);
		if (!module) return;

		GpuShader slot;
		slot.Module = std::move(module);
		slot.Name   = name;

		m_Program = AllocateShaderSlot(std::move(slot));
		m_IsValid = (m_Program != 0);
	}

	Shader::Shader(GLuint /*program*/) {}

	Shader::~Shader() {
		if (m_Program != 0) {
			FreeShaderSlot(m_Program);
			m_Program = 0;
			m_IsValid = false;
		}
	}

	Shader::Shader(Shader&& other) noexcept
		: m_Program(other.m_Program)
		, m_IsValid(other.m_IsValid)
	{
		other.m_Program = 0;
		other.m_IsValid = false;
	}

	Shader& Shader::operator=(Shader&& other) noexcept {
		if (this != &other) {
			if (m_Program != 0) FreeShaderSlot(m_Program);
			m_Program = other.m_Program;
			m_IsValid = other.m_IsValid;
			other.m_Program = 0;
			other.m_IsValid = false;
		}
		return *this;
	}

	Shader Shader::FromBinary(const std::string& binaryPath) {
		return Shader{ binaryPath, binaryPath };
	}

	bool Shader::ExportBinary(const std::string& /*outputPath*/) const {
		// No native binary representation worth exporting — WGSL text is
		// already the source-of-truth.
		return false;
	}

	Shader Shader::LoadWithBinaryCache(const std::string& /*binaryPath*/,
		const std::string& vsPath, const std::string& fsPath)
	{
		return Shader{ vsPath, fsPath };
	}

	void Shader::Submit() const {
		// WebGPU binds shader modules via wgpu::RenderPipeline at
		// pipeline-creation time, not per-draw. Submit stays a no-op.
	}

	GLuint Shader::LoadAndCompile(GLenum /*type*/, const std::string& /*path*/) {
		// Legacy single-stage helper from the OpenGL era — unused.
		return 0;
	}

}  // namespace Funke
