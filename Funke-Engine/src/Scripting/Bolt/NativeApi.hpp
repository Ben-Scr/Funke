#pragma once

#include "Scripting/ScriptBindings.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <climits>
#include <string>
#include <type_traits>

namespace Funke::BoltScriptHost {
	std::string* FrameText() noexcept;

	inline const NativeBindings& ScriptApi() {
		static const NativeBindings api = [] {
			NativeBindings bindings{};
			ScriptBindings::PopulateNativeBindings(bindings);
			return bindings;
		}();
		return api;
	}

	template<class T> T Decode(uint64_t value) {
		if constexpr (std::is_same_v<T, float>) return std::bit_cast<float>(static_cast<uint32_t>(value));
		else return static_cast<T>(value);
	}
	template<class T> uint64_t Encode(T value) {
		if constexpr (std::is_same_v<T, float>) return std::bit_cast<uint32_t>(value);
		else return static_cast<uint64_t>(value);
	}
	template<class R, class... T> uint64_t Invoke(R (*fn)(T...), T... args) {
		if constexpr (std::is_void_v<R>) { fn(args...); return 0; }
		else return Encode(fn(args...));
	}
	template<class R> uint64_t Call(R (*fn)(), uint64_t, uint64_t) { return Invoke(fn); }
	template<class R, class A> uint64_t Call(R (*fn)(A), uint64_t a, uint64_t) { return Invoke(fn, Decode<A>(a)); }
	template<class R, class A, class B> uint64_t Call(R (*fn)(A,B), uint64_t a, uint64_t b) {
		return Invoke(fn, Decode<A>(a), Decode<B>(b));
	}
	inline uint64_t Call(uint64_t (*fn)(const char*, uint64_t, int), uint64_t id, uint64_t kind) {
		return fn(id ? nullptr : FrameText()->c_str(), id, static_cast<int>(kind));
	}
	template<class R> uint64_t Call(R (*fn)(const char*), uint64_t, uint64_t) {
		return Invoke(fn, FrameText()->c_str());
	}
	template<class Read> uint64_t ReadTextBuffer(Read read) {
		auto& text = *FrameText();
		const int size = read(nullptr, 0);
		text.clear();
		if (size <= 0 || size == INT_MAX) return 0;
		text.resize(size + 1);
		read(text.data(), size + 1);
		text.resize(size);
		return size;
	}
	inline uint64_t Call(int (*fn)(char*, int), uint64_t, uint64_t) {
		return ReadTextBuffer([fn](char* out, int size) { return fn(out, size); });
	}
	inline uint64_t Call(void (*fn)(uint64_t, const char*), uint64_t id, uint64_t) {
		fn(id, FrameText()->c_str());
		return 0;
	}
	template<class T> uint64_t Call(int (*fn)(T, char*, int), uint64_t id, uint64_t) {
		return ReadTextBuffer([fn, id](char* out, int size) { return fn(static_cast<T>(id), out, size); });
	}
	inline uint64_t Pack(float x, float y) { return Encode(x) | (Encode(y) << 32); }
	inline uint64_t Call(void (*fn)(float*, float*), uint64_t, uint64_t) {
		float x{}, y{}; fn(&x, &y); return Pack(x, y);
	}
	inline uint64_t Call(void (*fn)(uint64_t, float*, float*), uint64_t id, uint64_t) {
		float x{}, y{}; fn(id, &x, &y); return Pack(x, y);
	}
	inline uint64_t Call(void (*fn)(uint64_t, float, float), uint64_t id, uint64_t xy) {
		fn(id, Decode<float>(xy), Decode<float>(xy >> 32)); return 0;
	}
	inline thread_local std::array<uint64_t, 5> s_ExtraWords{};
	inline uint64_t Call(int (*fn)(uint64_t, uint64_t, int), uint64_t id, uint64_t parent) {
		return fn(id, parent, static_cast<int>(s_ExtraWords[0]));
	}
	inline uint64_t ReadExtra(uint64_t index, uint64_t) noexcept { return index < s_ExtraWords.size() ? s_ExtraWords[index] : 0; }
	inline uint64_t WriteExtra(uint64_t value, uint64_t index) noexcept {
		if (index < s_ExtraWords.size()) s_ExtraWords[index] = value;
		return value;
	}
	inline uint64_t Call(void (*fn)(float*, float*, float*), uint64_t, uint64_t) {
		float x{}, y{}, z{}; fn(&x, &y, &z); s_ExtraWords[0] = Encode(z); return Pack(x, y);
	}
	inline uint64_t Call(void (*fn)(float*, float*, float*, float*), uint64_t, uint64_t) {
		float x{}, y{}, z{}, w{};
		fn(&x, &y, &z, &w);
		s_ExtraWords[0] = Pack(z, w);
		return Pack(x, y);
	}
	inline uint64_t Call(void (*fn)(uint64_t, float, float, float*, float*), uint64_t id, uint64_t xy) {
		float x{}, y{};
		fn(id, Decode<float>(xy), Decode<float>(xy >> 32), &x, &y);
		return Pack(x, y);
	}
	inline uint64_t Call(void (*fn)(uint64_t, float*, float*, float*), uint64_t id, uint64_t) {
		float x{}, y{}, z{}; fn(id, &x, &y, &z); s_ExtraWords[0] = Encode(z); return Pack(x, y);
	}
	inline uint64_t Call(void (*fn)(uint64_t, float*, float*, float*, float*), uint64_t id, uint64_t) {
		float x{}, y{}, z{}, w{}; fn(id, &x, &y, &z, &w); s_ExtraWords[0] = Pack(z,w); return Pack(x, y);
	}
	inline uint64_t Call(void (*fn)(uint64_t, float, float, float), uint64_t id, uint64_t xy) {
		fn(id, Decode<float>(xy), Decode<float>(xy >> 32), Decode<float>(s_ExtraWords[0])); return 0;
	}
	inline uint64_t Call(void (*fn)(uint64_t, float, float, float, float), uint64_t id, uint64_t xy) {
		fn(id, Decode<float>(xy), Decode<float>(xy >> 32), Decode<float>(s_ExtraWords[0]), Decode<float>(s_ExtraWords[0] >> 32)); return 0;
	}
	template<class T> uint64_t Call(void (*fn)(uint64_t, int, T), uint64_t id, uint64_t bits) {
		fn(id, static_cast<int>(s_ExtraWords[0]), Decode<T>(bits));
		return 0;
	}

	inline uint64_t Call(int (*fn)(uint64_t, const char*), uint64_t id, uint64_t) {
		return fn(id, FrameText()->c_str());
	}
	inline uint64_t Call(float (*fn)(uint64_t, const char*, int), uint64_t id, uint64_t kind) {
		return Encode(fn(id, FrameText()->c_str(), static_cast<int>(kind)));
	}
	inline uint64_t Call(int (*fn)(uint64_t, const char*, int, float), uint64_t id, uint64_t bits) {
		return fn(id, FrameText()->c_str(), static_cast<int>(s_ExtraWords[0]), Decode<float>(bits));
	}
	inline uint64_t Call(int (*fn)(uint64_t, const char*, float, float), uint64_t id, uint64_t pair) {
		return fn(id, FrameText()->c_str(), Decode<float>(pair), Decode<float>(pair >> 32));
	}
	inline uint64_t Call(int (*fn)(uint64_t, int, char*, int), uint64_t id, uint64_t index) {
		return ReadTextBuffer([=](char* out, int size) { return fn(id, static_cast<int>(index), out, size); });
	}

	inline uint64_t Call(void (*fn)(uint64_t, int, const char*), uint64_t id, uint64_t index) {
		fn(id, static_cast<int>(index), FrameText()->c_str());
		return 0;
	}

	inline uint64_t Call(int (*fn)(uint64_t, float, float, float*, int), uint64_t id, uint64_t xy) {
		float ray[6]{};
		const int valid = fn(id, Decode<float>(xy), Decode<float>(xy >> 32), ray, 6);
		s_ExtraWords = { Pack(ray[0], ray[1]), Pack(ray[2], ray[3]), Pack(ray[4], ray[5]) };
		return valid;
	}
	inline uint64_t Call(int (*fn)(uint64_t, float, float, float, float*, int), uint64_t id, uint64_t xy) {
		float screen[3]{};
		const int visible = fn(id, Decode<float>(xy), Decode<float>(xy >> 32), Decode<float>(s_ExtraWords[0]), screen, 3);
		s_ExtraWords[0] = Pack(screen[0], screen[1]);
		s_ExtraWords[1] = Encode(screen[2]);
		return visible;
	}
	inline uint64_t Call(void (*fn)(uint64_t, float, float, int), uint64_t id, uint64_t xy) {
		fn(id, Decode<float>(xy), Decode<float>(xy >> 32), static_cast<int>(s_ExtraWords[0]));
		return 0;
	}
	inline uint64_t Call(void (*fn)(uint64_t, int, float, int), uint64_t id, uint64_t bits) {
		fn(id, static_cast<int>(s_ExtraWords[0]), Decode<float>(bits), static_cast<int>(s_ExtraWords[1]));
		return 0;
	}
	inline uint64_t Call(void (*fn)(uint64_t, int, float, float, float, float, int), uint64_t id, uint64_t xy) {
		fn(id, static_cast<int>(s_ExtraWords[1]), Decode<float>(xy), Decode<float>(xy >> 32),
			Decode<float>(s_ExtraWords[0]), Decode<float>(s_ExtraWords[0] >> 32), static_cast<int>(s_ExtraWords[2]));
		return 0;
	}
	inline uint64_t Call(void (*fn)(uint64_t, int, float*, float*, float*), uint64_t id, uint64_t kind) {
		float x{}, y{}, z{};
		fn(id, static_cast<int>(kind), &x, &y, &z);
		s_ExtraWords[0] = Encode(z);
		return Pack(x, y);
	}
	inline uint64_t Call(void (*fn)(uint64_t, int, float, float, float), uint64_t id, uint64_t xy) {
		fn(id, static_cast<int>(s_ExtraWords[1]), Decode<float>(xy), Decode<float>(xy >> 32), Decode<float>(s_ExtraWords[0]));
		return 0;
	}
	inline uint64_t Call(void (*fn)(uint64_t, int, int, float, float, float, float, float, float), uint64_t id, uint64_t options) {
		fn(id, static_cast<int>(options), static_cast<int>(options >> 32), Decode<float>(s_ExtraWords[0]),
			Decode<float>(s_ExtraWords[0] >> 32), Decode<float>(s_ExtraWords[1]), Decode<float>(s_ExtraWords[1] >> 32),
			Decode<float>(s_ExtraWords[2]), Decode<float>(s_ExtraWords[2] >> 32));
		return 0;
	}
	inline uint64_t Call(int (*fn)(uint64_t, int*), uint64_t id, uint64_t) {
		int count{};
		const int state = fn(id, &count);
		return uint32_t(state) | (uint64_t(uint32_t(count)) << 32);
	}

	inline uint64_t Call(int (*fn)(uint64_t, int, void*, int), uint64_t id, uint64_t descriptor) {
		const uint32_t size = static_cast<uint32_t>(descriptor >> 32);
		if (size > sizeof(s_ExtraWords)) return 0;
		return fn(id, static_cast<int>(descriptor), s_ExtraWords.data(), static_cast<int>(size));
	}

	inline uint64_t Call(void (*fn)(int*, int*), uint64_t, uint64_t) {
		int x{}, y{};
		fn(&x, &y);
		return uint32_t(x) | (uint64_t(uint32_t(y)) << 32);
	}
	inline uint64_t Call(void (*fn)(float, float, float, float), uint64_t xy, uint64_t zw) {
		fn(Decode<float>(xy), Decode<float>(xy >> 32), Decode<float>(zw), Decode<float>(zw >> 32));
		return 0;
	}
	inline uint64_t PolygonPoints(uint64_t entity, void* request, void* data) noexcept {
		struct Request { int32_t Operation, Index, Count, Paths; const int* Counts; };
		if (!FrameText() || !request) return 0;
		const auto& args = *static_cast<const Request*>(request);
		auto* points = static_cast<float*>(data);
		const auto& api = ScriptApi();
		if (args.Count < 0) return 0;
		try {
			switch (args.Operation) {
			case 0:
				if (api.PolygonCollider2D_SetPoints && points) api.PolygonCollider2D_SetPoints(entity, points, args.Count);
				break;
			case 1:
				if (api.PolygonCollider2D_GetPath) return api.PolygonCollider2D_GetPath(entity, args.Index, points, args.Count);
				break;
			case 2: {
				if (!api.PolygonCollider2D_SetPaths || args.Paths < 0 || (args.Paths && !args.Counts)) break;
				int remaining = args.Count;
				for (int i = 0; i < args.Paths; ++i) {
					if (args.Counts[i] < 0 || args.Counts[i] > remaining) return 0;
					remaining -= args.Counts[i];
				}
				if (!remaining) api.PolygonCollider2D_SetPaths(entity, points, args.Counts, args.Paths);
				break;
			}
			case 3:
				if (api.PolygonCollider2D_GetWorldPoints && points && args.Count <= INT_MAX / 2)
					return api.PolygonCollider2D_GetWorldPoints(entity, points, args.Count * 2);
				break;
			}
		} catch (...) {}
		return 0;
	}

	enum class FieldAccess { Get, Set };

	template<FieldAccess Access, auto Read, auto Write, size_t Count, size_t Index, size_t Width, int Kind>
	uint64_t PackedField(uint64_t id, uint64_t low) noexcept {
		static_assert(Width >= 1 && Width <= 4 && Index + Width <= Count);
		if (!FrameText()) return 0;
		try {
			float values[Count]{};
			const auto& api = ScriptApi();
			if (!(api.*Read)) return 0;
			if constexpr (Kind < 0) (api.*Read)(id, values, Count);
			else (api.*Read)(id, Kind, values, Count);
			auto* field = values + Index;
			if constexpr (Access == FieldAccess::Set) {
				if (!(api.*Write)) return 0;
				field[0] = Decode<float>(low);
				if constexpr (Width >= 2) field[1] = Decode<float>(low >> 32);
				if constexpr (Width >= 3) field[2] = Decode<float>(s_ExtraWords[0]);
				if constexpr (Width == 4) field[3] = Decode<float>(s_ExtraWords[0] >> 32);
				if constexpr (Kind < 0) (api.*Write)(id, values, Count);
				else (api.*Write)(id, Kind, values, Count);
				return 0;
			} else {
				if constexpr (Width == 4) s_ExtraWords[0] = Pack(field[2], field[3]);
				if constexpr (Width == 3) s_ExtraWords[0] = Encode(field[2]);
				if constexpr (Width >= 2) return Pack(field[0], field[1]);
				return Encode(field[0]);
			}
		} catch (...) { return 0; }
	}
	template<auto Member> uint64_t ComponentCall(uint64_t id, uint64_t kind) noexcept {
		static constexpr const char* names[] = {
#include "Scripting/Bolt/ComponentNames.inc"
		};
		if (!FrameText() || kind >= std::size(names)) return 0;
		try { return (ScriptApi().*Member)(id, names[kind]); } catch (...) { return 0; }
	}
	template<auto Member> uint64_t ApiCall(uint64_t a, uint64_t b) noexcept {
		if (!FrameText() || !(ScriptApi().*Member)) return 0;
		try { return Call(ScriptApi().*Member, a, b); } catch (...) { return 0; }
	}
}
