#pragma once

#include "Core/PlatformDetection.hpp"

#include <filesystem>
#include <string>

#ifdef IDX_PLATFORM_WINDOWS
#include <Windows.h>
#elif IDX_PLATFORM_DESKTOP
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace Index {

    class SingleInstance
    {
    public:
        explicit SingleInstance(const std::string& mutexName)
        {
#ifdef IDX_PLATFORM_WINDOWS
            m_Handle = CreateMutexA(nullptr, TRUE, mutexName.c_str());
            if (m_Handle && GetLastError() == ERROR_ALREADY_EXISTS)
                m_AlreadyRunning = true;
#elif IDX_PLATFORM_DESKTOP
            std::error_code ec;
            const std::filesystem::path path = std::filesystem::temp_directory_path(ec) / (mutexName + ".lock");
            m_FileDescriptor = ec ? -1 : open(path.c_str(), O_CREAT | O_RDWR, 0666);
            if (m_FileDescriptor != -1)
                m_AlreadyRunning = (flock(m_FileDescriptor, LOCK_EX | LOCK_NB) != 0);
#else
            // Mobile and Web: the OS already guarantees a single app instance.
            (void)mutexName;
#endif
        }

        ~SingleInstance()
        {
#ifdef IDX_PLATFORM_WINDOWS
            if (m_Handle)
            {
                ReleaseMutex(m_Handle);
                CloseHandle(m_Handle);
            }
#elif IDX_PLATFORM_DESKTOP
            if (m_FileDescriptor != -1)
            {
                flock(m_FileDescriptor, LOCK_UN);
                close(m_FileDescriptor);
            }
#endif
        }

        bool IsAlreadyRunning() const { return m_AlreadyRunning; }

    private:
#ifdef IDX_PLATFORM_WINDOWS
        HANDLE m_Handle = nullptr;
#else
        int m_FileDescriptor = -1;
#endif
        bool m_AlreadyRunning = false;
    };
}