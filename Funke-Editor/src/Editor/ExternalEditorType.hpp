#pragma once

namespace Funke {

	enum class ExternalEditorType {
		Auto,
		VisualStudio,
		VSCode,
		Rider,
		Custom,
		IntelliJ // IntelliJ IDEA: opens and edits scripts, but has no C# language support (that is Rider)
	};

} // namespace Funke
