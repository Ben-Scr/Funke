#include "pch.hpp"
#include "Graphics/CustomShader.hpp"
#include "Utils/CharConv.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <unordered_set>

namespace Funke {
	namespace {
		constexpr const char* k_CommonPrelude = R"WGSL(
struct Globals {
	view: mat4x4<f32>,
	projection: mat4x4<f32>,
	viewProj: mat4x4<f32>,
	invViewProj: mat4x4<f32>,
	cameraPosition: vec4<f32>,
	time: f32,
	deltaTime: f32,
	frame: f32,
	fnk_pad0: f32,
	screenSize: vec4<f32>,
};

@group(1) @binding(0) var<uniform> globals: Globals;

fn srgbToLinear(c: vec3<f32>) -> vec3<f32> {
	let x = max(c, vec3<f32>(0.0));
	let lo = x / 12.92;
	let hi = pow((x + vec3<f32>(0.055)) / 1.055, vec3<f32>(2.4));
	return select(hi, lo, x <= vec3<f32>(0.04045));
}

fn linearToSrgb(c: vec3<f32>) -> vec3<f32> {
	let x = max(c, vec3<f32>(0.0));
	let lo = x * 12.92;
	let hi = 1.055 * pow(x, vec3<f32>(1.0 / 2.4)) - vec3<f32>(0.055);
	return select(hi, lo, x <= vec3<f32>(0.0031308));
}
)WGSL";

		// Mirrors the lighting of the built-in "mesh" WGSL in Shader.cpp; keep both in step.
		constexpr const char* k_Mesh3DPrelude = R"WGSL(
const FNK_PI: f32 = 3.14159265;

struct FnkFrame {
	viewProj: mat4x4<f32>,
	lightViewProj: mat4x4<f32>,
	cameraPos: vec4<f32>,
	ambient: vec4<f32>,
	skyTop: vec4<f32>,
	skyHorizon: vec4<f32>,
	skyGround: vec4<f32>,
	params: vec4<f32>,
	shadowParams: vec4<f32>,
	fogColor: vec4<f32>,
	fogParams: vec4<f32>,
};

struct FnkLight {
	positionRange: vec4<f32>,
	directionType: vec4<f32>,
	radiance: vec4<f32>,
	spot: vec4<f32>,
};

struct FnkLights {
	items: array<FnkLight, 64>,
};

@group(0) @binding(0) var<uniform> fnk_frame: FnkFrame;
@group(0) @binding(1) var<uniform> fnk_lights: FnkLights;
@group(0) @binding(2) var fnk_shadowMap: texture_depth_2d;
@group(0) @binding(3) var fnk_shadowSampler: sampler_comparison;

struct VertexData {
	localPosition: vec3<f32>,
	localNormal: vec3<f32>,
	worldPosition: vec3<f32>,
	worldNormal: vec3<f32>,
	uv: vec2<f32>,
	color: vec4<f32>,
	model: mat4x4<f32>,
};

struct SurfaceInput {
	worldPosition: vec3<f32>,
	worldNormal: vec3<f32>,
	viewDirection: vec3<f32>,
	localPosition: vec3<f32>,
	objectPosition: vec3<f32>,
	uv: vec2<f32>,
	screenUV: vec2<f32>,
	color: vec4<f32>,
	frontFacing: bool,
};

struct SurfaceOutput {
	baseColor: vec3<f32>,
	alpha: f32,
	metallic: f32,
	roughness: f32,
	normal: vec3<f32>,
	emissive: vec3<f32>,
	unlit: bool,
};

fn defaultSurface(in: SurfaceInput) -> SurfaceOutput {
	var out: SurfaceOutput;
	out.baseColor = in.color.rgb;
	out.alpha = in.color.a;
	out.metallic = 0.0;
	out.roughness = 0.5;
	out.normal = in.worldNormal;
	out.emissive = vec3<f32>(0.0);
	out.unlit = false;
	return out;
}
)WGSL";

		constexpr const char* k_Mesh3DEntryPoints = R"WGSL(
struct FnkVertexInput {
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
	@location(10) color:       vec4<f32>,
	@location(11) emissive:    vec4<f32>,
	@location(12) surface:     vec4<f32>,
	@location(13) uvTransform: vec4<f32>,
};

struct FnkVaryings {
	@builtin(position) clip: vec4<f32>,
	@location(0) worldPosition: vec3<f32>,
	@location(1) worldNormal: vec3<f32>,
	@location(2) uv: vec2<f32>,
	@location(3) color: vec4<f32>,
	@location(4) localPosition: vec3<f32>,
	@location(5) objectPosition: vec3<f32>,
	@location(6) @interpolate(flat) fogged: f32,
};

@vertex
fn vs_main(in: FnkVertexInput) -> FnkVaryings {
	let model = mat4x4<f32>(in.model0, in.model1, in.model2, in.model3);
	let normalMatrix = mat3x3<f32>(in.normal0.xyz, in.normal1.xyz, in.normal2.xyz);
	var v: VertexData;
	v.localPosition = in.position;
	v.localNormal = in.normal;
	v.worldPosition = (model * vec4<f32>(in.position, 1.0)).xyz;
	v.worldNormal = normalMatrix * in.normal;
	v.uv = in.uv * in.uvTransform.xy + in.uvTransform.zw;
	v.color = in.color;
	v.model = model;
	FNK_VERTEX_CALL
	var out: FnkVaryings;
	out.clip = fnk_frame.viewProj * vec4<f32>(v.worldPosition, 1.0);
	out.worldPosition = v.worldPosition;
	out.worldNormal = v.worldNormal;
	out.uv = v.uv;
	out.color = v.color;
	out.localPosition = v.localPosition;
	out.objectPosition = model[3].xyz;
	out.fogged = in.emissive.w;
	return out;
}

// Depth-only shadow pass, so fn vertex moves the shadow too.
@vertex
fn vs_shadow(in: FnkVertexInput) -> @builtin(position) vec4<f32> {
	let model = mat4x4<f32>(in.model0, in.model1, in.model2, in.model3);
	let normalMatrix = mat3x3<f32>(in.normal0.xyz, in.normal1.xyz, in.normal2.xyz);
	var v: VertexData;
	v.localPosition = in.position;
	v.localNormal = in.normal;
	v.worldPosition = (model * vec4<f32>(in.position, 1.0)).xyz;
	v.worldNormal = normalMatrix * in.normal;
	v.uv = in.uv * in.uvTransform.xy + in.uvTransform.zw;
	v.color = in.color;
	v.model = model;
	FNK_VERTEX_CALL
	return fnk_frame.lightViewProj * vec4<f32>(v.worldPosition, 1.0);
}

fn fnk_toneMap(c: vec3<f32>) -> vec3<f32> {
	let mode = fnk_frame.params.y;
	if (mode < 0.5) {
		return clamp(c, vec3<f32>(0.0), vec3<f32>(1.0));
	}
	if (mode < 1.5) {
		return c / (vec3<f32>(1.0) + c);
	}
	return clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), vec3<f32>(0.0), vec3<f32>(1.0));
}

fn fnk_encodeOutput(c: vec3<f32>) -> vec3<f32> {
	if (fnk_frame.params.z > 0.5) {
		return linearToSrgb(c);
	}
	return c;
}

fn fnk_skyColor(dir: vec3<f32>) -> vec3<f32> {
	let up = dir.y;
	if (up >= 0.0) {
		return mix(fnk_frame.skyHorizon.rgb, fnk_frame.skyTop.rgb, pow(up, 0.6));
	}
	return mix(fnk_frame.skyHorizon.rgb, fnk_frame.skyGround.rgb, smoothstep(0.0, 0.2, -up));
}

fn fnk_distributionGGX(NoH: f32, a: f32) -> f32 {
	let a2 = a * a;
	let f = (NoH * a2 - NoH) * NoH + 1.0;
	return a2 / (FNK_PI * f * f);
}

fn fnk_visibilitySmithGGX(NoV: f32, NoL: f32, a: f32) -> f32 {
	let a2 = a * a;
	let ggxL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
	let ggxV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
	return 0.5 / max(ggxV + ggxL, 1e-5);
}

fn fnk_fresnelSchlick(VoH: f32, f0: vec3<f32>) -> vec3<f32> {
	return f0 + (vec3<f32>(1.0) - f0) * pow(1.0 - VoH, 5.0);
}

fn fnk_envBRDF(f0: vec3<f32>, roughness: f32, NoV: f32) -> vec3<f32> {
	let c0 = vec4<f32>(-1.0, -0.0275, -0.572, 0.022);
	let c1 = vec4<f32>(1.0, 0.0425, 1.04, -0.04);
	let r = roughness * c0 + c1;
	let a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
	let ab = vec2<f32>(-1.04, 1.04) * a004 + r.zw;
	return f0 * ab.x + vec3<f32>(ab.y);
}

fn fnk_pointAttenuation(dist: f32, lightRange: f32) -> f32 {
	if (dist >= lightRange) {
		return 0.0;
	}
	let ratio = dist / lightRange;
	let window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
	return (window * window) / (dist * dist + 1.0);
}

fn fnk_shadowFactor(worldPos: vec3<f32>, normal: vec3<f32>) -> f32 {
	let lp = fnk_frame.lightViewProj * vec4<f32>(worldPos + normal * fnk_frame.shadowParams.z, 1.0);
	let ndc = lp.xyz / lp.w;
	let uv = vec2<f32>(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || ndc.z > 1.0) {
		return 1.0;
	}
	let depth = ndc.z - fnk_frame.shadowParams.y;
	let texel = fnk_frame.shadowParams.x;
	var sum = 0.0;
	for (var y = -1; y <= 1; y++) {
		for (var x = -1; x <= 1; x++) {
			sum += textureSampleCompareLevel(fnk_shadowMap, fnk_shadowSampler, uv + vec2<f32>(f32(x), f32(y)) * texel, depth);
		}
	}
	let fade = smoothstep(fnk_frame.shadowParams.w * 0.85, fnk_frame.shadowParams.w, length(worldPos - fnk_frame.cameraPos.xyz));
	return mix(sum / 9.0, 1.0, fade);
}

// Same distance fog as the standard material, blended over the display-range color.
fn fnk_applyFog(color: vec3<f32>, worldPos: vec3<f32>, fogged: f32) -> vec3<f32> {
	if (fnk_frame.fogColor.w < 0.5 || fogged < 0.5) {
		return color;
	}
	let dist = length(worldPos - fnk_frame.cameraPos.xyz);
	let t = clamp((dist - fnk_frame.fogParams.x) / max(fnk_frame.fogParams.y - fnk_frame.fogParams.x, 1e-3), 0.0, 1.0);
	return mix(color, fnk_frame.fogColor.rgb, t * t * (3.0 - 2.0 * t));
}

fn fnk_outputAlpha(alpha: f32) -> f32 {
	if (FNK_BLEND_MODE == 0u) {
		return 1.0;
	}
	return clamp(alpha, 0.0, 1.0);
}

fn fnk_shade(s: SurfaceInput, o: SurfaceOutput, fogged: f32) -> vec4<f32> {
	let albedo = srgbToLinear(o.baseColor);
	let alpha = fnk_outputAlpha(o.alpha);
	if (o.unlit) {
		return vec4<f32>(fnk_encodeOutput(fnk_applyFog(albedo + max(o.emissive, vec3<f32>(0.0)), s.worldPosition, fogged)), alpha);
	}

	let metallic = clamp(o.metallic, 0.0, 1.0);
	let roughness = clamp(o.roughness, 0.045, 1.0);
	let a = roughness * roughness;
	var n = s.worldNormal;
	if (dot(o.normal, o.normal) > 1e-8) {
		n = normalize(o.normal);
	}
	let v = s.viewDirection;
	let NoV = max(dot(n, v), 1e-4);
	let f0 = mix(vec3<f32>(0.04), albedo, metallic);
	let diffuseColor = albedo * (1.0 - metallic);

	var color = vec3<f32>(0.0);
	let count = min(u32(fnk_frame.params.x), 64u);
	let shadowedLight = i32(fnk_frame.params.w);
	for (var i = 0u; i < count; i++) {
		let light = fnk_lights.items[i];
		let lightType = light.directionType.w;
		var l = -light.directionType.xyz;
		var attenuation = 1.0;
		if (lightType > 0.5) {
			let toLight = light.positionRange.xyz - s.worldPosition;
			let dist = length(toLight);
			l = toLight / max(dist, 1e-4);
			attenuation = fnk_pointAttenuation(dist, light.positionRange.w);
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
			attenuation *= fnk_shadowFactor(s.worldPosition, n);
		}
		let h = normalize(l + v);
		let NoH = max(dot(n, h), 0.0);
		let VoH = max(dot(v, h), 0.0);
		let f = fnk_fresnelSchlick(VoH, f0);
		let specular = fnk_distributionGGX(NoH, a) * fnk_visibilitySmithGGX(NoV, NoL, a) * f;
		let diffuse = diffuseColor * (vec3<f32>(1.0) - f) / FNK_PI;
		color += (diffuse + specular) * light.radiance.rgb * (attenuation * NoL * FNK_PI);
	}

	let skyStrength = fnk_frame.ambient.w;
	let r = reflect(-v, n);
	let ambientDiffuse = fnk_frame.ambient.rgb + fnk_skyColor(n) * skyStrength;
	let ambientSpecular = fnk_frame.ambient.rgb + fnk_skyColor(normalize(mix(r, n, a))) * skyStrength;
	let reflectance = fnk_envBRDF(f0, roughness, NoV);
	color += diffuseColor * ambientDiffuse + reflectance * ambientSpecular;
	color += max(o.emissive, vec3<f32>(0.0));
	return vec4<f32>(fnk_encodeOutput(fnk_applyFog(fnk_toneMap(color * fnk_frame.cameraPos.w), s.worldPosition, fogged)), alpha);
}

@fragment
fn fs_main(in: FnkVaryings, @builtin(front_facing) frontFacing: bool) -> @location(0) vec4<f32> {
	var n = normalize(in.worldNormal);
	if (!frontFacing) {
		n = -n;
	}
	var s: SurfaceInput;
	s.worldPosition = in.worldPosition;
	s.worldNormal = n;
	s.viewDirection = normalize(fnk_frame.cameraPos.xyz - in.worldPosition);
	s.localPosition = in.localPosition;
	s.objectPosition = in.objectPosition;
	s.uv = in.uv;
	s.screenUV = in.clip.xy * globals.screenSize.zw;
	s.color = in.color;
	s.frontFacing = frontFacing;
	return fnk_shade(s, surface(s), in.fogged);
}
)WGSL";

		constexpr const char* k_Sprite2DPrelude = R"WGSL(
struct FnkSpriteUniforms {
	viewProj: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> fnk_sprite: FnkSpriteUniforms;
@group(0) @binding(1) var mainTexture: texture_2d<f32>;
@group(0) @binding(2) var mainSampler: sampler;
@group(0) @binding(3) var fnk_maskTexture: texture_2d<f32>;
@group(0) @binding(4) var fnk_maskSampler: sampler;

struct VertexData {
	worldPosition: vec2<f32>,
	uv: vec2<f32>,
	quadUV: vec2<f32>,
	color: vec4<f32>,
};

struct SurfaceInput {
	worldPosition: vec2<f32>,
	uv: vec2<f32>,
	quadUV: vec2<f32>,
	screenUV: vec2<f32>,
	color: vec4<f32>,
	texColor: vec4<f32>,
};

struct SurfaceOutput {
	color: vec4<f32>,
};

fn defaultSurface(in: SurfaceInput) -> SurfaceOutput {
	var out: SurfaceOutput;
	out.color = in.texColor * in.color;
	return out;
}
)WGSL";

		constexpr const char* k_Sprite2DEntryPoints = R"WGSL(
struct FnkVertexInput {
	@location(0) position: vec3<f32>,
	@location(1) i_data0: vec4<f32>,
	@location(2) i_data1: vec4<f32>,
	@location(3) i_data2: vec4<f32>,
	@location(4) i_data3: vec4<f32>,
	@location(5) i_data4: vec4<f32>,
	@location(6) i_data5: vec4<f32>,
	@location(7) i_data6: vec4<f32>,
};

struct FnkVaryings {
	@builtin(position) clip: vec4<f32>,
	@location(0) color: vec4<f32>,
	@location(1) uv: vec2<f32>,
	@location(2) worldPosition: vec2<f32>,
	@location(3) maskRect: vec4<f32>,
	@location(4) maskRot: vec4<f32>,
	@location(5) maskUv: vec4<f32>,
	@location(6) quadUV: vec2<f32>,
};

@vertex
fn vs_main(in: FnkVertexInput) -> FnkVaryings {
	let scaled = in.position.xy * in.i_data0.zw;
	let c = cos(in.i_data2.x);
	let s = sin(in.i_data2.x);
	let rotated = vec2<f32>(scaled.x * c - scaled.y * s, scaled.x * s + scaled.y * c);
	let quadUV = vec2<f32>(in.position.x + 0.5, 0.5 - in.position.y);
	var v: VertexData;
	v.worldPosition = rotated + in.i_data0.xy;
	v.uv = vec2<f32>(mix(in.i_data3.x, in.i_data3.z, quadUV.x), mix(in.i_data3.y, in.i_data3.w, quadUV.y));
	v.quadUV = quadUV;
	v.color = in.i_data1;
	FNK_VERTEX_CALL
	var out: FnkVaryings;
	out.clip = fnk_sprite.viewProj * vec4<f32>(v.worldPosition, 0.0, 1.0);
	out.color = v.color;
	out.uv = v.uv;
	out.worldPosition = v.worldPosition;
	out.maskRect = in.i_data4;
	out.maskRot = in.i_data5;
	out.maskUv = in.i_data6;
	out.quadUV = v.quadUV;
	return out;
}

@fragment
fn fs_main(in: FnkVaryings) -> @location(0) vec4<f32> {
	var s: SurfaceInput;
	s.worldPosition = in.worldPosition;
	s.uv = in.uv;
	s.quadUV = in.quadUV;
	s.screenUV = in.clip.xy * globals.screenSize.zw;
	s.color = in.color;
	s.texColor = textureSample(mainTexture, mainSampler, in.uv);
	var color = surface(s).color;

	if (in.maskRect.z != 0.0 && in.maskRect.w != 0.0) {
		let delta = in.worldPosition - in.maskRot.xy;
		let local = vec2<f32>(delta.x * in.maskRot.z - delta.y * in.maskRot.w, delta.x * in.maskRot.w + delta.y * in.maskRot.z) + in.maskRot.xy;
		let mask01 = vec2<f32>((local.x - in.maskRect.x) * in.maskRect.z, (local.y - in.maskRect.y) * in.maskRect.w);
		if (mask01.x < 0.0 || mask01.x > 1.0 || mask01.y < 0.0 || mask01.y > 1.0) {
			discard;
		}
		let maskSampleUv = vec2<f32>(mix(in.maskUv.x, in.maskUv.z, mask01.x), mix(in.maskUv.y, in.maskUv.w, 1.0 - mask01.y));
		let maskAlpha = textureSampleLevel(fnk_maskTexture, fnk_maskSampler, maskSampleUv, 0.0).a;
		if (maskAlpha <= 0.001) {
			discard;
		}
		color.a = color.a * maskAlpha;
	}
	if (FNK_BLEND_MODE == 0u) {
		color.a = 1.0;
	}
	return color;
}
)WGSL";

		constexpr const char* k_PostProcessPrelude = R"WGSL(
@group(0) @binding(0) var screenTexture: texture_2d<f32>;
@group(0) @binding(1) var screenSampler: sampler;

struct SurfaceInput {
	uv: vec2<f32>,
	pixel: vec2<f32>,
	screenColor: vec4<f32>,
};

struct SurfaceOutput {
	color: vec4<f32>,
};

fn defaultSurface(in: SurfaceInput) -> SurfaceOutput {
	var out: SurfaceOutput;
	out.color = in.screenColor;
	return out;
}
)WGSL";

		constexpr const char* k_PostProcessEntryPoints = R"WGSL(
struct FnkVaryings {
	@builtin(position) clip: vec4<f32>,
	@location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> FnkVaryings {
	var pos = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
	var uv = array<vec2<f32>, 3>(vec2<f32>(0.0, 1.0), vec2<f32>(2.0, 1.0), vec2<f32>(0.0, -1.0));
	var out: FnkVaryings;
	out.clip = vec4<f32>(pos[idx], 0.0, 1.0);
	out.uv = uv[idx];
	return out;
}

@fragment
fn fs_main(in: FnkVaryings) -> @location(0) vec4<f32> {
	var s: SurfaceInput;
	s.uv = in.uv;
	s.pixel = in.clip.xy;
	s.screenColor = textureSample(screenTexture, screenSampler, in.uv);
	return surface(s).color;
}
)WGSL";

		// Group 0 is Renderer3D's sky block (the one the built-in gradient sky reads).
		constexpr const char* k_SkyboxPrelude = R"WGSL(
struct FnkSky {
	inverseViewProj: mat4x4<f32>,
	top: vec4<f32>,
	horizon: vec4<f32>,
	ground: vec4<f32>,
	sunDirection: vec4<f32>,
	sunColor: vec4<f32>,
	params: vec4<f32>,
};

@group(0) @binding(0) var<uniform> fnk_sky: FnkSky;

const FNK_PI: f32 = 3.14159265;

struct SurfaceInput {
	// World-space direction from the camera through the pixel (normalized).
	direction: vec3<f32>,
	// Equirectangular coordinates of the direction: u around the horizon, v from top (0) to bottom (1).
	panoramaUv: vec2<f32>,
	// Towards the main DirectionalLight, its color pulled towards white, and 1 when the scene has one.
	sunDirection: vec3<f32>,
	sunColor: vec3<f32>,
	hasSun: f32,
	// cos of the camera's Sun Size; a disc is `dot(direction, sunDirection) > sunCosRadius`.
	sunCosRadius: f32,
};

struct SurfaceOutput {
	// Display (sRGB) color, like color properties; the engine converts it for the target.
	color: vec3<f32>,
};

fn directionToPanoramaUv(d: vec3<f32>) -> vec2<f32> {
	return vec2<f32>(0.5 + atan2(d.x, -d.z) / (2.0 * FNK_PI), acos(clamp(d.y, -1.0, 1.0)) / FNK_PI);
}

// Rotates a direction around the vertical axis (degrees), e.g. to turn a panorama.
fn rotateY(d: vec3<f32>, degrees: f32) -> vec3<f32> {
	let r = radians(degrees);
	let c = cos(r);
	let s = sin(r);
	return vec3<f32>(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
}

// The camera's sun disc and glow over `color`, as the built-in gradient sky draws them.
fn addSun(color: vec3<f32>, in: SurfaceInput) -> vec3<f32> {
	if (in.hasSun < 0.5) {
		return color;
	}
	let cosAngle = dot(in.direction, in.sunDirection);
	let edge = (1.0 - in.sunCosRadius) * 0.25 + 1e-5;
	let disc = smoothstep(in.sunCosRadius - edge, in.sunCosRadius + edge, cosAngle);
	let glow = pow(max(cosAngle, 0.0), 256.0) * 0.6 + pow(max(cosAngle, 0.0), 16.0) * 0.12;
	let horizonFade = smoothstep(-0.05, 0.02, in.direction.y);
	return mix(color, in.sunColor, disc * horizonFade) + in.sunColor * glow * horizonFade;
}
)WGSL";

		constexpr const char* k_SkyboxEntryPoints = R"WGSL(
struct FnkVaryings {
	@builtin(position) clip: vec4<f32>,
	@location(0) nearPoint: vec4<f32>,
	@location(1) farPoint: vec4<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> FnkVaryings {
	let uv = vec2<f32>(f32((idx << 1u) & 2u), f32(idx & 2u));
	let ndc = uv * 2.0 - vec2<f32>(1.0);
	var out: FnkVaryings;
	out.clip = vec4<f32>(ndc, 0.0, 1.0);
	out.nearPoint = fnk_sky.inverseViewProj * vec4<f32>(ndc, 0.0, 1.0);
	out.farPoint = fnk_sky.inverseViewProj * vec4<f32>(ndc, 1.0, 1.0);
	return out;
}

@fragment
fn fs_main(in: FnkVaryings) -> @location(0) vec4<f32> {
	var s: SurfaceInput;
	s.direction = normalize(in.farPoint.xyz / in.farPoint.w - in.nearPoint.xyz / in.nearPoint.w);
	s.panoramaUv = directionToPanoramaUv(s.direction);
	s.sunDirection = fnk_sky.sunDirection.xyz;
	s.sunColor = fnk_sky.sunColor.rgb;
	s.hasSun = fnk_sky.sunColor.a;
	s.sunCosRadius = fnk_sky.sunDirection.w;
	var color = max(surface(s).color, vec3<f32>(0.0));
	if (fnk_sky.params.x > 0.5) {
		color = srgbToLinear(color);
	}
	return vec4<f32>(color, 1.0);
}
)WGSL";

		constexpr const char* k_SkyboxFallback = R"WGSL(fn surface(in: SurfaceInput) -> SurfaceOutput {
	var out: SurfaceOutput;
	out.color = select(vec3<f32>(0.25, 0.0, 0.25), vec3<f32>(1.0, 0.0, 1.0), in.direction.y > 0.0);
	return out;
}
)WGSL";

		constexpr const char* k_Mesh3DFallback = R"WGSL(fn surface(in: SurfaceInput) -> SurfaceOutput {
	var out = defaultSurface(in);
	out.baseColor = vec3<f32>(1.0, 0.0, 1.0);
	out.alpha = 1.0;
	out.unlit = true;
	return out;
}
)WGSL";

		constexpr const char* k_Sprite2DFallback = R"WGSL(fn surface(in: SurfaceInput) -> SurfaceOutput {
	var out: SurfaceOutput;
	out.color = vec4<f32>(1.0, 0.0, 1.0, in.texColor.a * in.color.a);
	return out;
}
)WGSL";

		// A broken effect must not hide the game: the screen stays visible, tinted.
		constexpr const char* k_PostProcessFallback = R"WGSL(fn surface(in: SurfaceInput) -> SurfaceOutput {
	var out: SurfaceOutput;
	out.color = vec4<f32>(mix(in.screenColor.rgb, vec3<f32>(1.0, 0.0, 1.0), 0.35), in.screenColor.a);
	return out;
}
)WGSL";

		// Names the template declares at module scope; a user declaration of them would collide.
		const std::unordered_set<std::string_view>& ReservedNames() {
			static const std::unordered_set<std::string_view> s_Names = {
				"globals", "material", "Globals", "MaterialProperties", "VertexData", "SurfaceInput", "SurfaceOutput",
				"defaultSurface", "surface", "vertex", "srgbToLinear", "linearToSrgb", "vs_main", "fs_main", "vs_shadow",
				"mainTexture", "mainSampler", "screenTexture", "screenSampler",
				"directionToPanoramaUv", "rotateY", "addSun",
				// WGSL keywords and predeclared types a property name could plausibly hit.
				"alias", "break", "case", "const", "continue", "default", "diagnostic", "discard", "else", "enable",
				"false", "fn", "for", "if", "let", "loop", "override", "return", "struct", "switch", "true", "var", "while",
				"bool", "f32", "f16", "i32", "u32", "vec2", "vec3", "vec4", "mat2x2", "mat3x3", "mat4x4", "array",
				"sampler", "texture_2d", "ptr", "atomic", "uniform", "storage", "function", "private", "workgroup",
			};
			return s_Names;
		}

		std::string_view Trim(std::string_view text) {
			size_t begin = 0;
			while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
			size_t end = text.size();
			while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
			return text.substr(begin, end - begin);
		}

		std::string ToLower(std::string_view text) {
			std::string result(text);
			std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return result;
		}

		bool IsIdentifierStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
		bool IsIdentifierChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

		bool IsIdentifier(std::string_view text) {
			if (text.empty() || !IsIdentifierStart(text[0])) return false;
			return std::all_of(text.begin(), text.end(), IsIdentifierChar);
		}

		// Comments become spaces so line numbers and offsets survive the scan for functions and bindings.
		std::string StripComments(std::string_view source) {
			std::string result(source);
			int blockDepth = 0;
			for (size_t i = 0; i < result.size(); ++i) {
				if (blockDepth == 0 && result[i] == '/' && i + 1 < result.size() && result[i + 1] == '/') {
					while (i < result.size() && result[i] != '\n') result[i++] = ' ';
					continue;
				}
				// WGSL block comments nest.
				if (result[i] == '/' && i + 1 < result.size() && result[i + 1] == '*') {
					++blockDepth;
					result[i] = result[i + 1] = ' ';
					++i;
					continue;
				}
				if (blockDepth > 0 && result[i] == '*' && i + 1 < result.size() && result[i + 1] == '/') {
					--blockDepth;
					result[i] = result[i + 1] = ' ';
					++i;
					continue;
				}
				if (blockDepth > 0 && result[i] != '\n') result[i] = ' ';
			}
			return result;
		}

		int LineAt(std::string_view text, size_t offset) {
			return 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(std::min(offset, text.size())), '\n'));
		}

		// Line of the first module-scope `fn <name>(`, 0 when there is none.
		int FindFunction(std::string_view code, std::string_view name) {
			size_t pos = 0;
			while ((pos = code.find("fn", pos)) != std::string_view::npos) {
				const bool startsToken = pos == 0 || !IsIdentifierChar(code[pos - 1]);
				size_t cursor = pos + 2;
				if (!startsToken || cursor >= code.size() || !std::isspace(static_cast<unsigned char>(code[cursor]))) {
					pos = cursor;
					continue;
				}
				while (cursor < code.size() && std::isspace(static_cast<unsigned char>(code[cursor]))) ++cursor;
				size_t nameEnd = cursor;
				while (nameEnd < code.size() && IsIdentifierChar(code[nameEnd])) ++nameEnd;
				if (code.substr(cursor, nameEnd - cursor) == name) return LineAt(code, pos);
				pos = nameEnd;
			}
			return 0;
		}

		bool ParseFloat(std::string_view text, float& out) {
			text = Trim(text);
			if (!text.empty() && text.front() == '+') text.remove_prefix(1);
			if (text.empty()) return false;
			const auto result = CharConv::ParseFloat(text.data(), text.data() + text.size(), out);
			return result.ec == std::errc() && result.ptr == text.data() + text.size();
		}

		bool ParseHexColor(std::string_view text, glm::vec4& out) {
			if (text.size() != 7 && text.size() != 9) return false;
			float channels[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			for (size_t i = 0; i * 2 + 1 < text.size(); ++i) {
				unsigned value = 0;
				const char* begin = text.data() + 1 + i * 2;
				const auto result = std::from_chars(begin, begin + 2, value, 16);
				if (result.ec != std::errc() || result.ptr != begin + 2) return false;
				channels[i] = static_cast<float>(value) / 255.0f;
			}
			out = { channels[0], channels[1], channels[2], channels[3] };
			return true;
		}

		// "1.5", "(1, 2, 3)" or "#RRGGBB[AA]" (colors). A single number fills every component.
		bool ParseVector(std::string_view text, ShaderPropertyType type, glm::vec4& out, std::string& error) {
			text = Trim(text);
			const int count = type == ShaderPropertyType::Float ? 1
				: type == ShaderPropertyType::Vec2 ? 2
				: type == ShaderPropertyType::Vec3 ? 3 : 4;
			if (type == ShaderPropertyType::Color && !text.empty() && text.front() == '#') {
				if (ParseHexColor(text, out)) return true;
				error = "'" + std::string(text) + "' is not a #RRGGBB or #RRGGBBAA color";
				return false;
			}
			float single = 0.0f;
			if (ParseFloat(text, single)) {
				out = glm::vec4(single);
				if (type == ShaderPropertyType::Color) out.a = 1.0f;
				return true;
			}
			if (text.size() < 2 || text.front() != '(' || text.back() != ')') {
				error = count == 1 ? "default must be a number" : "default must be a number or (x, y, ...)";
				return false;
			}
			std::vector<float> values;
			std::string_view inner = text.substr(1, text.size() - 2);
			while (true) {
				const size_t comma = inner.find(',');
				float value = 0.0f;
				if (!ParseFloat(inner.substr(0, comma), value)) {
					error = "'" + std::string(Trim(inner.substr(0, comma))) + "' is not a number";
					return false;
				}
				values.push_back(value);
				if (comma == std::string_view::npos) break;
				inner.remove_prefix(comma + 1);
			}
			const bool colorWithoutAlpha = type == ShaderPropertyType::Color && values.size() == 3;
			if (static_cast<int>(values.size()) != count && !colorWithoutAlpha) {
				error = "expected " + std::to_string(count) + " components, got " + std::to_string(values.size());
				return false;
			}
			out = glm::vec4(0.0f);
			for (size_t i = 0; i < values.size(); ++i) out[static_cast<int>(i)] = values[i];
			if (colorWithoutAlpha) out.a = 1.0f;
			return true;
		}

		bool ParsePropertyType(std::string_view text, ShaderPropertyType& out) {
			const std::string lower = ToLower(text);
			if (lower == "float" || lower == "f32") out = ShaderPropertyType::Float;
			else if (lower == "vec2") out = ShaderPropertyType::Vec2;
			else if (lower == "vec3") out = ShaderPropertyType::Vec3;
			else if (lower == "vec4") out = ShaderPropertyType::Vec4;
			else if (lower == "color") out = ShaderPropertyType::Color;
			else if (lower == "texture") out = ShaderPropertyType::Texture;
			else return false;
			return true;
		}

		// `<type> <name> [= <default>] [range(<min>, <max>)]`
		bool ParseProperty(std::string_view text, int line, ShaderProperty& out, std::string& error) {
			text = Trim(text);
			size_t cursor = 0;
			auto readWord = [&]() {
				while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
				const size_t begin = cursor;
				while (cursor < text.size() && IsIdentifierChar(text[cursor])) ++cursor;
				return text.substr(begin, cursor - begin);
			};
			const std::string_view typeText = readWord();
			if (!ParsePropertyType(typeText, out.Type)) {
				error = typeText.empty() ? "@property needs a type: float, vec2, vec3, vec4, color or texture"
					: "unknown property type '" + std::string(typeText) + "' (float, vec2, vec3, vec4, color, texture)";
				return false;
			}
			const std::string_view name = readWord();
			if (!IsIdentifier(name)) {
				error = "@property needs a name after the type";
				return false;
			}
			if (name.size() >= 4 && name.substr(0, 4) == "fnk_") {
				error = "property names starting with 'fnk_' are reserved for the engine";
				return false;
			}
			if (ReservedNames().count(name)) {
				error = "'" + std::string(name) + "' is reserved; pick another property name";
				return false;
			}
			out.Name = std::string(name);
			out.Line = line;
			out.Default = out.Type == ShaderPropertyType::Color ? glm::vec4(1.0f) : glm::vec4(0.0f);

			std::string_view rest = Trim(text.substr(cursor));
			std::string_view rangeText;
			const std::string lowerRest = ToLower(rest);
			if (const size_t range = lowerRest.find("range"); range != std::string::npos) {
				rangeText = Trim(rest.substr(range + 5));
				rest = Trim(rest.substr(0, range));
			}
			if (!rest.empty()) {
				if (rest.front() != '=') {
					error = "unexpected '" + std::string(rest) + "' (expected '= <default>' or 'range(min, max)')";
					return false;
				}
				const std::string_view value = Trim(rest.substr(1));
				if (out.Type == ShaderPropertyType::Texture) {
					const std::string lower = ToLower(value);
					if (lower == "white") out.DefaultTexture = ShaderDefaultTexture::White;
					else if (lower == "black") out.DefaultTexture = ShaderDefaultTexture::Black;
					else if (lower == "gray" || lower == "grey") out.DefaultTexture = ShaderDefaultTexture::Gray;
					else if (lower == "normal" || lower == "bump") out.DefaultTexture = ShaderDefaultTexture::Normal;
					else {
						error = "texture default must be white, black, gray or normal";
						return false;
					}
				}
				else if (!ParseVector(value, out.Type, out.Default, error)) {
					return false;
				}
			}
			if (!rangeText.empty()) {
				if (out.Type == ShaderPropertyType::Texture || out.Type == ShaderPropertyType::Color) {
					error = "range() applies to float and vector properties only";
					return false;
				}
				glm::vec4 bounds{ 0.0f };
				std::string rangeError;
				if (!ParseVector(rangeText, ShaderPropertyType::Vec2, bounds, rangeError) || rangeText.front() != '(') {
					error = "range must be range(min, max)";
					return false;
				}
				if (bounds.x > bounds.y) {
					error = "range minimum is greater than its maximum";
					return false;
				}
				out.HasRange = true;
				out.Min = bounds.x;
				out.Max = bounds.y;
				out.Default = glm::clamp(out.Default, glm::vec4(out.Min), glm::vec4(out.Max));
			}
			return true;
		}

		bool ParseBool(std::string_view text, bool& out) {
			const std::string lower = ToLower(Trim(text));
			if (lower == "true" || lower == "on" || lower == "yes" || lower == "1") { out = true; return true; }
			if (lower == "false" || lower == "off" || lower == "no" || lower == "0") { out = false; return true; }
			return false;
		}

		uint32_t AlignUp(uint32_t value, uint32_t alignment) {
			return (value + alignment - 1) / alignment * alignment;
		}

		const char* WgslType(ShaderPropertyType type) {
			switch (type) {
				case ShaderPropertyType::Float: return "f32";
				case ShaderPropertyType::Vec2: return "vec2<f32>";
				case ShaderPropertyType::Vec3: return "vec3<f32>";
				case ShaderPropertyType::Vec4:
				case ShaderPropertyType::Color: return "vec4<f32>";
				case ShaderPropertyType::Texture: break;
			}
			return "";
		}

		int CountLines(std::string_view text) {
			return static_cast<int>(std::count(text.begin(), text.end(), '\n'));
		}

		void ReplaceAll(std::string& text, std::string_view from, std::string_view to) {
			for (size_t pos = text.find(from); pos != std::string::npos; pos = text.find(from, pos + to.size())) {
				text.replace(pos, from.size(), to);
			}
		}
	}

	int ShaderProperty::GetComponentCount() const {
		switch (Type) {
			case ShaderPropertyType::Float: return 1;
			case ShaderPropertyType::Vec2: return 2;
			case ShaderPropertyType::Vec3: return 3;
			case ShaderPropertyType::Vec4:
			case ShaderPropertyType::Color: return 4;
			case ShaderPropertyType::Texture: break;
		}
		return 0;
	}

	const ShaderProperty* ShaderDescription::FindProperty(std::string_view name) const {
		for (const ShaderProperty& property : Properties) {
			if (property.Name == name) return &property;
		}
		return nullptr;
	}

	bool ShaderParseResult::HasErrors() const {
		return std::any_of(Diagnostics.begin(), Diagnostics.end(), [](const ShaderDiagnostic& d) { return d.IsError; });
	}

	ShaderParseResult ParseCustomShader(std::string_view source) {
		ShaderParseResult result;
		ShaderDescription& description = result.Description;
		auto error = [&](int line, std::string message) { result.Diagnostics.push_back({ line, std::move(message), true }); };
		auto warning = [&](int line, std::string message) { result.Diagnostics.push_back({ line, std::move(message), false }); };

		bool blendSet = false, cullSet = false, depthWriteSet = false;
		std::unordered_set<std::string> declaredNames;
		int lineNumber = 0;
		size_t lineStart = 0;
		while (lineStart <= source.size()) {
			const size_t lineEnd = std::min(source.find('\n', lineStart), source.size());
			const std::string_view line = Trim(source.substr(lineStart, lineEnd - lineStart));
			++lineNumber;
			lineStart = lineEnd + 1;
			if (line.size() < 3 || line.substr(0, 2) != "//") continue;
			const std::string_view comment = Trim(line.substr(2));
			if (comment.empty() || comment.front() != '@') continue;

			size_t keyEnd = 1;
			while (keyEnd < comment.size() && IsIdentifierChar(comment[keyEnd])) ++keyEnd;
			const std::string key = ToLower(comment.substr(1, keyEnd - 1));
			const std::string_view value = Trim(comment.substr(keyEnd));

			if (key == "name" || key == "shader") {
				std::string_view name = value;
				if (name.size() >= 2 && name.front() == '"' && name.back() == '"') name = name.substr(1, name.size() - 2);
				description.Name = std::string(Trim(name));
			}
			else if (key == "target") {
				const std::string lower = ToLower(value);
				if (lower == "mesh3d" || lower == "mesh") description.Target = ShaderTarget::Mesh3D;
				else if (lower == "sprite2d" || lower == "sprite") description.Target = ShaderTarget::Sprite2D;
				else if (lower == "ui") description.Target = ShaderTarget::UI;
				else if (lower == "postprocess" || lower == "post") description.Target = ShaderTarget::PostProcess;
				else if (lower == "skybox" || lower == "sky") description.Target = ShaderTarget::Skybox;
				else error(lineNumber, "unknown target '" + std::string(value) + "' (Mesh3D, Sprite2D, UI, PostProcess, Skybox)");
			}
			else if (key == "blend") {
				const std::string lower = ToLower(value);
				blendSet = true;
				if (lower == "opaque" || lower == "off") description.Blend = ShaderBlend::Opaque;
				else if (lower == "alpha" || lower == "transparent") description.Blend = ShaderBlend::Alpha;
				else if (lower == "additive" || lower == "add") description.Blend = ShaderBlend::Additive;
				else error(lineNumber, "unknown blend '" + std::string(value) + "' (Opaque, Alpha, Additive)");
			}
			else if (key == "cull") {
				const std::string lower = ToLower(value);
				cullSet = true;
				if (lower == "back") description.Cull = ShaderCull::Back;
				else if (lower == "front") description.Cull = ShaderCull::Front;
				else if (lower == "none" || lower == "off") description.Cull = ShaderCull::None;
				else error(lineNumber, "unknown cull mode '" + std::string(value) + "' (Back, Front, None)");
			}
			else if (key == "depthwrite") {
				depthWriteSet = true;
				if (!ParseBool(value, description.DepthWrite)) error(lineNumber, "@depthWrite expects true or false");
			}
			else if (key == "displacement") {
				if (!ParseFloat(value, description.Displacement) || description.Displacement < 0.0f) {
					description.Displacement = 0.0f;
					error(lineNumber, "@displacement expects a distance >= 0 (local units)");
				}
			}
			else if (key == "property") {
				ShaderProperty property;
				std::string message;
				if (!ParseProperty(value, lineNumber, property, message)) {
					error(lineNumber, message);
					continue;
				}
				const std::string samplerName = property.Name + "Sampler";
				if (declaredNames.count(property.Name)
					|| (property.Type == ShaderPropertyType::Texture && declaredNames.count(samplerName))) {
					error(lineNumber, "property '" + property.Name + "' is declared twice");
					continue;
				}
				declaredNames.insert(property.Name);
				if (property.Type == ShaderPropertyType::Texture) declaredNames.insert(samplerName);
				description.Properties.push_back(std::move(property));
			}
			else {
				warning(lineNumber, "unknown directive '@" + key + "' is ignored (@name, @target, @blend, @cull, @depthWrite, @displacement, @property)");
			}
		}

		if (!blendSet) {
			description.Blend = description.Target == ShaderTarget::Sprite2D || description.Target == ShaderTarget::UI
				? ShaderBlend::Alpha : ShaderBlend::Opaque;
		}
		if (!cullSet) description.Cull = description.Target == ShaderTarget::Mesh3D ? ShaderCull::Back : ShaderCull::None;
		if (!depthWriteSet) description.DepthWrite = description.Blend == ShaderBlend::Opaque;
		if (description.Target == ShaderTarget::PostProcess && blendSet && description.Blend != ShaderBlend::Opaque) {
			warning(0, "@blend is ignored for PostProcess shaders: they replace the screen");
		}
		if (description.Target == ShaderTarget::Skybox) {
			if (blendSet && description.Blend != ShaderBlend::Opaque) warning(0, "@blend is ignored for Skybox shaders: the sky is the background");
			description.Blend = ShaderBlend::Opaque;
			description.DepthWrite = false;
		}

		// WGSL uniform layout: f32 aligns to 4, vec2 to 8, vec3 and vec4 to 16; the block is padded to 16.
		uint32_t cursor = 0;
		uint32_t textureSlot = 0;
		for (ShaderProperty& property : description.Properties) {
			if (property.Type == ShaderPropertyType::Texture) {
				property.TextureSlot = textureSlot++;
				continue;
			}
			const int components = property.GetComponentCount();
			const uint32_t alignment = components == 1 ? 4u : components == 2 ? 8u : 16u;
			property.Offset = AlignUp(cursor, alignment);
			cursor = property.Offset + static_cast<uint32_t>(components) * 4u;
		}
		description.UniformSize = std::max(16u, AlignUp(cursor, 16u));
		description.TextureCount = textureSlot;
		if (textureSlot > k_MaxShaderTextureProperties) {
			error(0, "at most " + std::to_string(k_MaxShaderTextureProperties) + " texture properties are supported, found "
				+ std::to_string(textureSlot));
		}

		const std::string code = StripComments(source);
		if (FindFunction(code, "surface") == 0) {
			error(0, "missing `fn surface(in: SurfaceInput) -> SurfaceOutput`");
		}
		if (const int vertexLine = FindFunction(code, "vertex"); vertexLine != 0) {
			if (description.Target == ShaderTarget::PostProcess || description.Target == ShaderTarget::Skybox) {
				warning(vertexLine, std::string(ShaderTargetToString(description.Target)) + " shaders have no vertex stage; fn vertex is not called");
			}
			else {
				description.HasVertexFunction = true;
			}
		}
		for (const char* attribute : { "@group", "@binding" }) {
			if (const size_t pos = code.find(attribute); pos != std::string::npos) {
				error(LineAt(code, pos), std::string(attribute) + " is generated by the engine; declare textures with `// @property texture <name>`");
			}
		}
		for (const char* entry : { "vs_main", "fs_main", "vs_shadow" }) {
			if (const int line = FindFunction(code, entry); line != 0) {
				error(line, std::string(entry) + " is the engine's entry point; write fn surface (and optionally fn vertex) instead");
			}
		}
		return result;
	}

	int ComposedShader::ToUserLine(int composedLine) const {
		const int line = composedLine - UserLineOffset;
		return line >= 1 && line <= UserLineCount ? line : 0;
	}

	ComposedShader ComposeCustomShader(const ShaderDescription& description, std::string_view userSource) {
		std::string prelude = "// Generated by Funke from a user shader; the user source follows the prelude verbatim.\n";
		prelude += k_CommonPrelude;

		prelude += "\nstruct MaterialProperties {\n";
		bool anyValue = false;
		for (const ShaderProperty& property : description.Properties) {
			if (property.Type == ShaderPropertyType::Texture) continue;
			prelude += "\t" + property.Name + ": " + WgslType(property.Type) + ",\n";
			anyValue = true;
		}
		if (!anyValue) prelude += "\tfnk_unused: vec4<f32>,\n";
		prelude += "};\n\n@group(1) @binding(1) var<uniform> material: MaterialProperties;\n";
		for (const ShaderProperty& property : description.Properties) {
			if (property.Type != ShaderPropertyType::Texture) continue;
			const uint32_t binding = k_CustomShaderFirstTextureBinding + property.TextureSlot * 2;
			prelude += "@group(1) @binding(" + std::to_string(binding) + ") var " + property.Name + ": texture_2d<f32>;\n";
			prelude += "@group(1) @binding(" + std::to_string(binding + 1) + ") var " + property.Name + "Sampler: sampler;\n";
		}

		const char* targetPrelude = k_Mesh3DPrelude;
		const char* entryPoints = k_Mesh3DEntryPoints;
		switch (description.Target) {
			case ShaderTarget::Mesh3D: break;
			case ShaderTarget::Sprite2D:
			case ShaderTarget::UI:
				targetPrelude = k_Sprite2DPrelude;
				entryPoints = k_Sprite2DEntryPoints;
				break;
			case ShaderTarget::PostProcess:
				targetPrelude = k_PostProcessPrelude;
				entryPoints = k_PostProcessEntryPoints;
				break;
			case ShaderTarget::Skybox:
				targetPrelude = k_SkyboxPrelude;
				entryPoints = k_SkyboxEntryPoints;
				break;
		}
		prelude += targetPrelude;
		prelude += "\nconst FNK_BLEND_MODE: u32 = " + std::to_string(static_cast<int>(description.Blend)) + "u;\n";
		prelude += "// ---- user source ----\n";

		std::string entries = entryPoints;
		ReplaceAll(entries, "FNK_VERTEX_CALL", description.HasVertexFunction ? "v = vertex(v);" : "");

		ComposedShader composed;
		composed.UserLineOffset = CountLines(prelude);
		composed.UserLineCount = CountLines(userSource) + 1;
		composed.Wgsl.reserve(prelude.size() + userSource.size() + entries.size() + 64);
		composed.Wgsl += prelude;
		composed.Wgsl.append(userSource.data(), userSource.size());
		composed.Wgsl += "\n// ---- engine entry points ----\n";
		composed.Wgsl += entries;
		return composed;
	}

	std::string_view GetFallbackShaderSource(ShaderTarget target) {
		switch (target) {
			case ShaderTarget::Mesh3D: return k_Mesh3DFallback;
			case ShaderTarget::Sprite2D:
			case ShaderTarget::UI: return k_Sprite2DFallback;
			case ShaderTarget::PostProcess: return k_PostProcessFallback;
			case ShaderTarget::Skybox: return k_SkyboxFallback;
		}
		return k_Mesh3DFallback;
	}

	const std::vector<ShaderTemplate>& GetSkyboxShaderTemplates() {
		static const std::vector<ShaderTemplate> s_Templates = {
			{ "Gradient Sky", "GradientSky", R"WGSL(// @name Gradient Sky
// @target Skybox
// @property color top = (0.24, 0.45, 0.78, 1)
// @property color horizon = (0.73, 0.83, 0.93, 1)
// @property color ground = (0.36, 0.34, 0.31, 1)
// @property float curve = 0.6 range(0.05, 4)
// @property float showSun = 1 range(0, 1)

fn surface(in: SurfaceInput) -> SurfaceOutput {
	var out: SurfaceOutput;
	let up = in.direction.y;
	var color = mix(material.horizon.rgb, material.ground.rgb, smoothstep(0.0, 0.2, -up));
	if (up >= 0.0) {
		color = mix(material.horizon.rgb, material.top.rgb, pow(up, material.curve));
	}
	if (material.showSun > 0.5) {
		color = addSun(color, in);
	}
	out.color = color;
	return out;
}
)WGSL" },
			{ "6-Sided Sky", "SixSidedSky", R"WGSL(// @name 6-Sided Sky
// @target Skybox
// Each cube face shows its texture times its color; leave a texture empty for a plain color.
// Front looks down -Z (the default camera direction).
// @property texture front = white
// @property texture back = white
// @property texture left = white
// @property texture right = white
// @property texture up = white
// @property texture down = white
// @property color frontColor = (1, 1, 1, 1)
// @property color backColor = (1, 1, 1, 1)
// @property color leftColor = (1, 1, 1, 1)
// @property color rightColor = (1, 1, 1, 1)
// @property color upColor = (1, 1, 1, 1)
// @property color downColor = (1, 1, 1, 1)
// @property float rotation = 0 range(0, 360)
// @property float exposure = 1 range(0, 8)

fn surface(in: SurfaceInput) -> SurfaceOutput {
	var out: SurfaceOutput;
	let d = rotateY(in.direction, material.rotation);
	let a = abs(d);
	// Face-local UV in 0..1, image upright when seen from inside the cube.
	var color: vec3<f32>;
	if (a.x >= a.y && a.x >= a.z) {
		if (d.x > 0.0) {
			let uv = vec2<f32>(-d.z, -d.y) / a.x * 0.5 + 0.5;
			color = textureSampleLevel(right, rightSampler, uv, 0.0).rgb * material.rightColor.rgb;
		} else {
			let uv = vec2<f32>(d.z, -d.y) / a.x * 0.5 + 0.5;
			color = textureSampleLevel(left, leftSampler, uv, 0.0).rgb * material.leftColor.rgb;
		}
	} else if (a.y >= a.z) {
		if (d.y > 0.0) {
			let uv = vec2<f32>(d.x, d.z) / a.y * 0.5 + 0.5;
			color = textureSampleLevel(up, upSampler, uv, 0.0).rgb * material.upColor.rgb;
		} else {
			let uv = vec2<f32>(d.x, -d.z) / a.y * 0.5 + 0.5;
			color = textureSampleLevel(down, downSampler, uv, 0.0).rgb * material.downColor.rgb;
		}
	} else {
		if (d.z < 0.0) {
			let uv = vec2<f32>(d.x, -d.y) / a.z * 0.5 + 0.5;
			color = textureSampleLevel(front, frontSampler, uv, 0.0).rgb * material.frontColor.rgb;
		} else {
			let uv = vec2<f32>(-d.x, -d.y) / a.z * 0.5 + 0.5;
			color = textureSampleLevel(back, backSampler, uv, 0.0).rgb * material.backColor.rgb;
		}
	}
	out.color = color * material.exposure;
	return out;
}
)WGSL" },
			{ "Panoramic Sky", "PanoramicSky", R"WGSL(// @name Panoramic Sky
// @target Skybox
// An equirectangular (2:1) panorama image around the camera.
// @property texture panorama = gray
// @property color tint = (1, 1, 1, 1)
// @property float rotation = 0 range(0, 360)
// @property float exposure = 1 range(0, 8)
// @property float showSun = 0 range(0, 1)

fn surface(in: SurfaceInput) -> SurfaceOutput {
	var out: SurfaceOutput;
	let uv = directionToPanoramaUv(rotateY(in.direction, material.rotation));
	var color = textureSampleLevel(panorama, panoramaSampler, uv, 0.0).rgb * material.tint.rgb * material.exposure;
	if (material.showSun > 0.5) {
		color = addSun(color, in);
	}
	out.color = color;
	return out;
}
)WGSL" },
		};
		return s_Templates;
	}

	const char* ShaderTargetToString(ShaderTarget target) {
		switch (target) {
			case ShaderTarget::Mesh3D: return "Mesh3D";
			case ShaderTarget::Sprite2D: return "Sprite2D";
			case ShaderTarget::UI: return "UI";
			case ShaderTarget::PostProcess: return "PostProcess";
			case ShaderTarget::Skybox: return "Skybox";
		}
		return "Mesh3D";
	}

	const char* ShaderPropertyTypeToString(ShaderPropertyType type) {
		switch (type) {
			case ShaderPropertyType::Float: return "float";
			case ShaderPropertyType::Vec2: return "vec2";
			case ShaderPropertyType::Vec3: return "vec3";
			case ShaderPropertyType::Vec4: return "vec4";
			case ShaderPropertyType::Color: return "color";
			case ShaderPropertyType::Texture: return "texture";
		}
		return "float";
	}

}
