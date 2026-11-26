#pragma once

#include <cstdint>

namespace Funke::BoltScriptHost::Locales {
    uint64_t Call(uint64_t op, void* tag, void* arguments) noexcept;
}
