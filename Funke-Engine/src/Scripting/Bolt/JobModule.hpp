#pragma once
#include "Scripting/Bolt/CompilerHost.h"
#include "Scripting/Bolt/NativeApi.hpp"
#include "Core/Platform.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace Funke::BoltScriptHost {
    struct JobImage;
    inline std::mutex s_JobImagesMutex;
    inline std::unordered_map<uint64_t, std::weak_ptr<JobImage>> s_JobImages;
    inline std::atomic<uint64_t> s_NextJobImage{1};

    // A queued callback owns the native code and its context through the worker's return.
    struct JobImage {
        uint64_t Id;
        void* Library = nullptr;
        BoltCompiledModule* Image = nullptr;
        decltype(&bolt_module_release) Release = nullptr;
#ifdef FNK_PLATFORM_WINDOWS
        void* Session = nullptr;
        int (*RunAot)(void**, int, int32_t*) = nullptr;
        void (*CloseAot)(void*) = nullptr;
#endif
        ~JobImage() {
            {
                std::lock_guard lock(s_JobImagesMutex);
                const auto it = s_JobImages.find(Id);
                if (it != s_JobImages.end() && it->second.expired())
                    s_JobImages.erase(it);
            }
#ifdef FNK_PLATFORM_WINDOWS
            if (Session) CloseAot(Session);
#endif
            if (Image)
                Release(Image);
            if (Library)
                Platform::UnloadSharedLibrary(Library);
        }
    };
    inline uint64_t SubmitJob(uint64_t module, void* entry, void* capture) noexcept {
        if (!entry || !capture)
            return 0;
        try {
            std::shared_ptr<JobImage> image;
            {
                std::lock_guard lock(s_JobImagesMutex);
                auto it = s_JobImages.find(module);
                if (it == s_JobImages.end())
                    return 0;
                image = it->second.lock();
            }
            if (!image)
                return 0;
            struct Work {
                std::shared_ptr<JobImage> Image;
                void* (*Entry)(void*);
                void* Capture;
            };
            auto* work = new Work{std::move(image), reinterpret_cast<void* (*)(void*)>(entry), capture};
            return ScriptApi().JobSystem_Enqueue(
                [](void* context) {
                    auto& work = *static_cast<Work*>(context);
                    work.Entry(work.Capture);
                },
                work, [](void* context) { delete static_cast<Work*>(context); });
        } catch (...) {
            return 0;
        }
    }
} // namespace Funke::BoltScriptHost
