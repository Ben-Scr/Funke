#include "Core/Export.hpp"
#undef FUNKE_API
#define FUNKE_API __declspec(dllimport)
#include "Scripting/ScriptEngine.hpp"
#include "Core/Application.hpp"
#include "Core/Window.hpp"

#undef FUNKE_API
#define FUNKE_API

