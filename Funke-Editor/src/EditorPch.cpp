// PCH TU for Funke-Editor (pchsource in premake5.lua). This file exists only
// to compile EditorPch.hpp once; see that header for what it carries and why.
// Without the PCH, large TUs like ComponentInspectors.cpp exhaust the compiler
// heap (C1060).

#include "EditorPch.hpp"
