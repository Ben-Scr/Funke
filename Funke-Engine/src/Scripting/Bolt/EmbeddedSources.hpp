#pragma once

namespace Funke::BoltScriptHost::EmbeddedSources {
	struct ExtraModule {
		const char* RuntimePath;
		const char* Filename;
		const char* Preamble;
		const char* Body;
		const char* Editor;
	};
	extern const ExtraModule ExtraModules[];
	extern const char EditorCore[];
	extern const char EditorUI[];
	extern const char EditorJobs[];
	extern const char EditorComponentNamespace[];
	extern const char EditorCoroutines[];
	extern const char CorePreamble[];
	extern const char Core[];
	extern const char UIPreamble[];
	extern const char UI[];
	extern const char Jobs[];
	extern const char Coroutines[];
	extern const char ComponentNamespace[];
}
