#pragma once

#include <cstddef>
#include <cstring>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

namespace Index {

	// Copyable callable wrapper without vtables or RTTI. std::function emits a vtable, ~6 helper
	// functions and type_info per stored lambda type; here a small trivially-copyable callable costs
	// one call thunk, anything else lives on the heap behind one extra manager function.
	template <typename Signature>
	class InlineFunction;

	template <typename R, typename... Args>
	class InlineFunction<R(Args...)> {
	public:
		InlineFunction() noexcept = default;
		InlineFunction(std::nullptr_t) noexcept {}

		template <typename F, typename Fn = std::decay_t<F>,
			typename = std::enable_if_t<!std::is_same_v<Fn, InlineFunction> && std::is_invocable_r_v<R, Fn&, Args...>>>
		InlineFunction(F&& f) {
			Emplace<Fn>(std::forward<F>(f));
		}

		InlineFunction(const InlineFunction& other) { CopyFrom(other); }
		InlineFunction(InlineFunction&& other) noexcept { MoveFrom(other); }

		InlineFunction& operator=(const InlineFunction& other) {
			if (this != &other) {
				Reset();
				CopyFrom(other);
			}
			return *this;
		}

		InlineFunction& operator=(InlineFunction&& other) noexcept {
			if (this != &other) {
				Reset();
				MoveFrom(other);
			}
			return *this;
		}

		InlineFunction& operator=(std::nullptr_t) noexcept {
			Reset();
			return *this;
		}

		template <typename F, typename Fn = std::decay_t<F>,
			typename = std::enable_if_t<!std::is_same_v<Fn, InlineFunction> && std::is_invocable_r_v<R, Fn&, Args...>>>
		InlineFunction& operator=(F&& f) {
			Reset();
			Emplace<Fn>(std::forward<F>(f));
			return *this;
		}

		~InlineFunction() { Reset(); }

		R operator()(Args... args) const {
			return m_Invoke(m_Storage, std::forward<Args>(args)...);
		}

		explicit operator bool() const noexcept { return m_Invoke != nullptr; }
		friend bool operator==(const InlineFunction& f, std::nullptr_t) noexcept { return !f; }

	private:
		enum class Op { Copy, Destroy };

		static constexpr std::size_t k_InlineSize = 4 * sizeof(void*);
		static constexpr std::size_t k_InlineAlign = alignof(std::max_align_t);

		template <typename Fn>
		static constexpr bool k_StoredInline = std::is_trivially_copyable_v<Fn>
			&& std::is_trivially_destructible_v<Fn>
			&& sizeof(Fn) <= k_InlineSize && alignof(Fn) <= k_InlineAlign;

		template <typename Fn, typename F>
		void Emplace(F&& f) {
			if constexpr (std::is_pointer_v<Fn> || std::is_member_pointer_v<Fn>) {
				if (!f) return;
			}
			if constexpr (k_StoredInline<Fn>) {
				::new (static_cast<void*>(m_Storage)) Fn(std::forward<F>(f));
				m_Invoke = [](void* storage, Args... args) -> R {
					return std::invoke(*std::launder(static_cast<Fn*>(storage)), std::forward<Args>(args)...);
				};
			}
			else {
				*reinterpret_cast<Fn**>(m_Storage) = new Fn(std::forward<F>(f));
				m_Invoke = [](void* storage, Args... args) -> R {
					return std::invoke(**static_cast<Fn**>(storage), std::forward<Args>(args)...);
				};
				m_Manage = [](Op op, void* dst, const void* src) {
					if (op == Op::Copy) {
						*static_cast<Fn**>(dst) = new Fn(**static_cast<Fn* const*>(src));
					}
					else {
						delete *static_cast<Fn**>(dst);
					}
				};
			}
		}

		void CopyFrom(const InlineFunction& other) {
			if (other.m_Manage) {
				other.m_Manage(Op::Copy, m_Storage, other.m_Storage);
			}
			else {
				std::memcpy(m_Storage, other.m_Storage, k_InlineSize);
			}
			m_Invoke = other.m_Invoke;
			m_Manage = other.m_Manage;
		}

		void MoveFrom(InlineFunction& other) noexcept {
			// Inline callables are trivially copyable and heap ones are a single owning pointer,
			// so a byte copy transfers ownership either way.
			std::memcpy(m_Storage, other.m_Storage, k_InlineSize);
			m_Invoke = other.m_Invoke;
			m_Manage = other.m_Manage;
			other.m_Invoke = nullptr;
			other.m_Manage = nullptr;
		}

		void Reset() noexcept {
			if (m_Manage) {
				m_Manage(Op::Destroy, m_Storage, nullptr);
			}
			m_Invoke = nullptr;
			m_Manage = nullptr;
		}

		alignas(k_InlineAlign) mutable unsigned char m_Storage[k_InlineSize]{};
		R (*m_Invoke)(void*, Args...) = nullptr;
		void (*m_Manage)(Op, void*, const void*) = nullptr;
	};

} // namespace Index
