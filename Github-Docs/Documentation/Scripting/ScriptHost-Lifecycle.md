# ScriptHost responsibilities and reload ownership

`ScriptInstanceManager` is one static partial type. Splitting its implementation
does not create another lifetime owner or another set of static caches. Native
callback names, signatures, attributes and exception boundaries stay on the same
type. The refactoring preserved the bodies of all 207 members.

| Source | Responsibility |
|---|---|
| `ScriptInstanceManager.cs` | Assembly preparation/commit/unload, reload state and cross-service cleanup orchestration. |
| `ScriptInstanceManager.Instances.cs` | Script instances/handles, per-instance lifecycle, callback caches and coroutine cleanup. |
| `ScriptInstanceManager.Metadata.cs` | Type catalogs, reflection/inspector metadata and method discovery/invocation. |
| `ScriptInstanceManager.Fields.cs` | Field codecs, serialized field access and owned pinned UTF-8 result buffers. |
| `ScriptInstanceManager.Events.cs` | Input/application/scene event delivery through the existing contained callback paths. |
| `ScriptAssemblyLoadContext.cs` | Collectible context dependency resolution and shared core assembly identity. |
| `ScriptCallbacks.cs` | Callback-mask discovery. |

Strong references requiring cleanup include instance/handle dictionaries,
Type/FieldInfo/MethodInfo metadata, play-reset methods, managed component wrappers,
editor/dev-command/package registries, user event delegates, coroutine state and
continuations, job layout/exception caches, prepared assemblies and reload-state
snapshots. Pinned output buffers own bytes and must be freed even though they do
not themselves root a user Type. Unload diagnostics retain only WeakReferences to
old collectible contexts.

Preparation loads and validates a candidate without discarding the current valid
assembly. The existing lifecycle orchestrator then stops/disposes old instances,
removes dynamic registrations and metadata, sweeps event delegates, clears the
engine-side stores/continuations, releases field buffers and unloads the old
context (the low-level unload helper also releases its result buffer after
dropping the context/assembly references). The implementation's existing call order and exception containment are
unchanged by this source split; ownership must not be duplicated in a helper's
new static cache.

Reload, metadata lifetime, inspector asset/list/struct fields, hot-reload state,
type identity and coroutine tests run against the split implementation. The
collectible-context WeakReference tests still pass. This is managed lifecycle
evidence; native ScriptEngine/scene restoration after a complete editor reload
has a separate integration acceptance.
