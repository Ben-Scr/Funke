#pragma once

#include "Scripting/Bolt/NativeApi.hpp"
#include <cstring>
#include <utility>

namespace Funke::BoltScriptHost::Services {
    template<class T>
    T FromWord(uint64_t word) {
        static_assert(std::is_trivially_copyable_v<T> && sizeof(T) <= sizeof(word));
        if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(static_cast<uintptr_t>(word));
        else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) return static_cast<T>(word);
        else {
            T value;
            std::memcpy(&value, &word, sizeof(value));
            return value;
        }
    }

    template<class R, class... Args, size_t... I>
    uint64_t Invoke(R (*fn)(Args...), const uint64_t* words, std::index_sequence<I...>) {
        if (!fn || (sizeof...(Args) && !words)) return 0;
        if constexpr (std::is_void_v<R>) {
            fn(FromWord<Args>(words[I])...);
            return 0;
        } else {
            R value = fn(FromWord<Args>(words[I])...);
            static_assert(std::is_trivially_copyable_v<R> && sizeof(R) <= sizeof(uint64_t));
            uint64_t result{};
            std::memcpy(&result, &value, sizeof(value));
            return result;
        }
    }

    template<class R, class... Args>
    uint64_t Invoke(R (*fn)(Args...), void* packet) {
        return Invoke(fn, static_cast<const uint64_t*>(packet), std::index_sequence_for<Args...>{});
    }

    inline uint64_t Call(uint64_t slot, void* words, void*) noexcept {
        if (!FrameText()) return 0;
        try {
            switch (slot) {
#define FUNKE_BOLT_SERVICE(id, name) case id: return Invoke(ScriptApi().name, words);
#include "Scripting/Bolt/ServiceSlots.inc"
#undef FUNKE_BOLT_SERVICE
            default: return 0;
            }
        } catch (...) { return 0; }
    }
}

