#pragma once
#include "Core/Log.hpp"

#include <exception>
#include <type_traits>

#if defined(_MSC_VER)
	#define IDX_BINDING_GUARD_SIGNATURE __FUNCSIG__
#else
	#define IDX_BINDING_GUARD_SIGNATURE __PRETTY_FUNCTION__
#endif

namespace Index::ScriptBindingGuard {
	// C++ exceptions unwinding into CoreCLR are UB; every native binding is registered through this
	// noexcept thunk so a throw becomes a logged error and a default return value.
	template<auto Fn>
	struct Guard;

	template<typename R, typename... Args, R(*Fn)(Args...)>
	struct Guard<Fn> {
		static R Call(Args... args) noexcept {
			try {
				return Fn(args...);
			}
			catch (const std::exception& e) {
				IDX_CORE_ERROR_TAG("ScriptBinding", "exception in {}: {}", IDX_BINDING_GUARD_SIGNATURE, e.what());
			}
			catch (...) {
				IDX_CORE_ERROR_TAG("ScriptBinding", "unknown exception in {}", IDX_BINDING_GUARD_SIGNATURE);
			}
			if constexpr (!std::is_void_v<R>) {
				return R{};
			}
		}
	};
}

#define IDX_GUARDED_BINDING(fn) (&::Index::ScriptBindingGuard::Guard<&fn>::Call)
