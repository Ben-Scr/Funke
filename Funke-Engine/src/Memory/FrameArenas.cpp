#include "pch.hpp"
#include "Memory/FrameArenas.hpp"

#ifdef FNK_DEBUG
#include <thread>
#endif

namespace Funke::FrameArenas {

	namespace {

		// Storage for the two arenas. Function-local statics so we get a
		// well-defined empty-arena state before Initialize and don't depend
		// on TU-static init ordering with the tracked Allocator.
		Arena& FrameStorage() {
			static Arena s_Frame;
			return s_Frame;
		}

		Arena& PersistentStorage() {
			static Arena s_Persistent;
			return s_Persistent;
		}

		bool& InitializedFlag() {
			static bool s_Initialized = false;
			return s_Initialized;
		}

#ifdef FNK_DEBUG
		std::thread::id g_OwnerThread;

		void CheckThreadAccess() {
			if (g_OwnerThread != std::thread::id{} && g_OwnerThread != std::this_thread::get_id()) {
				FNK_THROW(FunkeErrorCode::InvalidArgument,
					"FrameArenas must only be accessed from their initializing thread; use a local Arena on workers.");
			}
		}
#endif

	} // anonymous namespace

	void Initialize(const FrameArenasSpec& spec) {
#ifdef FNK_DEBUG
		CheckThreadAccess();
#endif
		FrameStorage()      = Arena(spec.FrameCapacityBytes);
		PersistentStorage() = Arena(spec.PersistentCapacityBytes);
		InitializedFlag()   = true;
#ifdef FNK_DEBUG
		g_OwnerThread = std::this_thread::get_id();
		FrameStorage().BindToCurrentThread();
		PersistentStorage().BindToCurrentThread();
#endif
	}

	void Shutdown() {
#ifdef FNK_DEBUG
		CheckThreadAccess();
#endif
		// Move-assign empty arenas: Frame()/Persistent() remain valid references but return nullptr from Allocate.
		FrameStorage()      = Arena();
		PersistentStorage() = Arena();
		InitializedFlag()   = false;
#ifdef FNK_DEBUG
		g_OwnerThread = {};
#endif
	}

	bool IsInitialized() {
#ifdef FNK_DEBUG
		CheckThreadAccess();
#endif
		return InitializedFlag();
	}

	Arena& Frame() {
#ifdef FNK_DEBUG
		CheckThreadAccess();
#endif
		return FrameStorage();
	}

	Arena& Persistent() {
#ifdef FNK_DEBUG
		CheckThreadAccess();
#endif
		return PersistentStorage();
	}

	void OnEndFrame() {
#ifdef FNK_DEBUG
		CheckThreadAccess();
#endif
		// Reset is safe on an empty arena (no-op). We don't check
		// IsInitialized first so the call site in Application::EndFrame
		// doesn't have to know about init state.
		FrameStorage().Reset();
	}

	void ResetPersistent() {
#ifdef FNK_DEBUG
		CheckThreadAccess();
#endif
		PersistentStorage().Reset();
	}

} // namespace Funke::FrameArenas
