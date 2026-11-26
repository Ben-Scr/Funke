#pragma once

#include <cstdint>

namespace Funke::ScriptPreferences {
    enum Operation { GetInt, GetFloat, GetString, SetInt, SetFloat, SetString, HasKey, DeleteKey, DeleteAll, Save };
    int Access(const char* path, int operation, const char* key, int keyBytes, uint64_t* value, char* text, int capacity) noexcept;
    void SavePending() noexcept;
}

