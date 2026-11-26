#ifndef BOLT_COMPILER_HOST_H
#define BOLT_COMPILER_HOST_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
# ifdef BOLT_COMPILER_BUILD
#  define BOLT_HOST_API __declspec(dllexport)
# else
#  define BOLT_HOST_API __declspec(dllimport)
# endif
#else
# define BOLT_HOST_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define BOLT_COMPILER_HOST_ABI 1u
typedef struct BoltCompiler BoltCompiler;
typedef struct BoltCompiledModule BoltCompiledModule;

/* Matches Bolt's extern "C" ulong Function(ulong, ulong).
   The host owns the implementation until every module using it is released
   and every in-flight invocation has returned. Callbacks may not throw. */
typedef uint64_t (*BoltHostFunction)(uint64_t, uint64_t);
typedef struct BoltHostBinding {
    const char *name;
    BoltHostFunction function;
} BoltHostBinding;

enum BoltCompilerProfile { BOLT_HOST_DEBUG = 0, BOLT_HOST_RELEASE = 1 };
enum BoltHostStatus { BOLT_HOST_OK = 0, BOLT_HOST_ERROR = 1, BOLT_HOST_NO_MODULE = 2 };

/* Error output is optional. A nonempty destination always receives a
   NUL-terminated UTF-8 string (possibly truncated). Handles belong to the
   caller. Destruction requires the caller to finish all uses of that handle. */
BOLT_HOST_API uint32_t bolt_compiler_host_abi(void);
BOLT_HOST_API const char *bolt_compiler_version(void);
BOLT_HOST_API int bolt_compiler_create_with_llvm(uint32_t abi, int profile,
    const BoltHostBinding *bindings, size_t count, const char *llvm_library, BoltCompiler **out,
    char *error, size_t capacity);
BOLT_HOST_API int bolt_compiler_create(uint32_t abi, int profile,
    const BoltHostBinding *bindings, size_t count, BoltCompiler **out,
    char *error, size_t capacity);
typedef uint64_t (*BoltHostSubmitFunction)(uint64_t, void *, void *);
BOLT_HOST_API int bolt_compiler_bind_submit(BoltCompiler *compiler, const char *name,
    BoltHostSubmitFunction function, char *error, size_t capacity);
typedef void *(*BoltHostPointerFunction)(uint64_t, uint64_t);
BOLT_HOST_API int bolt_compiler_bind_pointer(BoltCompiler *compiler, const char *name,
    BoltHostPointerFunction function, char *error, size_t capacity);
BOLT_HOST_API void bolt_compiler_destroy(BoltCompiler *compiler);

/* Each compile returns an independent native image. The source is a counted
   UTF-8 buffer. The project variant accepts a UTF-8 file or directory path. */
BOLT_HOST_API int bolt_compiler_compile(BoltCompiler *compiler, const char *name,
    const char *source, size_t length, BoltCompiledModule **out,
    char *error, size_t capacity);
BOLT_HOST_API int bolt_compiler_compile_project(BoltCompiler *compiler, const char *path,
    BoltCompiledModule **out, char *error, size_t capacity);
typedef struct BoltSource {
    const char *name;
    const char *source;
    size_t length;
} BoltSource;
/* Compile named, in-memory units in one module. Names and diagnostic lines are
   preserved independently. A null entry selects the usual Program.Main. */
BOLT_HOST_API int bolt_compiler_compile_sources(BoltCompiler *compiler, const BoltSource *sources,
    size_t count, const char *entry, BoltCompiledModule **out, char *error, size_t capacity);
typedef struct BoltAttributeInfo {
    const char *name;
    const char *const *arguments;
    size_t argument_count;
} BoltAttributeInfo;
typedef struct BoltFieldInfo {
    const char *owner, *name, *type, *literal;
    int is_public, is_static, is_constant;
    const BoltAttributeInfo *attributes;
    size_t attribute_count;
} BoltFieldInfo;
typedef void (*BoltFieldVisitor)(void *user, const BoltFieldInfo *field);
/* Parse source declarations without loading or executing code. All pointers
   passed to the visitor are borrowed for the duration of that callback. */
BOLT_HOST_API int bolt_compiler_visit_fields(const BoltSource *source, int profile,
    BoltFieldVisitor visitor, void *user, char *error, size_t capacity);
typedef struct BoltCompileStats {
    uint64_t parsed_files, reused_files;
    uint64_t frontend_ns, native_codegen_ns;
} BoltCompileStats;
/* Parse and check a project without native lowering, linking, running or loading it.
   On success optional stats contain zero native_codegen_ns. */
BOLT_HOST_API int bolt_compiler_check_project(BoltCompiler *compiler, const char *path,
    BoltCompileStats *stats, char *error, size_t capacity);
BOLT_HOST_API int bolt_module_stats(const BoltCompiledModule *module, BoltCompileStats *out);
typedef struct BoltLayoutField {
    const char *name, *type;
    uint32_t offset, size;
    int is_public;
} BoltLayoutField;
typedef void (*BoltLayoutVisitor)(void *user, uint32_t size, uint32_t alignment,
    const BoltLayoutField *fields, size_t count);
/* Describe the verified native byte layout of a plain struct and its nested fields. */
BOLT_HOST_API int bolt_module_visit_layout(const BoltCompiledModule *module, const char *type,
    BoltLayoutVisitor visit, void *user, char *error, size_t capacity);
BOLT_HOST_API void bolt_module_release(BoltCompiledModule *module);

/* Load acquires its own reference. Passing NULL unloads the active module.
   An invocation takes a snapshot of the active image. bolt_compiler_run starts
   fresh state for each call to a module without a persistent session. A compiler accepts only modules built by that instance. */
BOLT_HOST_API int bolt_compiler_load(BoltCompiler *compiler, const BoltCompiledModule *module,
    char *error, size_t capacity);
BOLT_HOST_API int bolt_compiler_run(BoltCompiler *compiler, int32_t *exit_code,
    char *error, size_t capacity);
/* Optional ABI 1 extension. Keeps module statics and allocations alive across Main
   calls. Each invocation has a fresh step budget. Replacing or releasing the
   module ends its session. A failed session must be replaced before reuse. */
/* Run one entry per host record. Advance prepares the next record under the module's execution
   lock and must not reenter this module. Every entry receives a fresh instruction budget. */
BOLT_HOST_API int bolt_module_run_persistent_batch(const BoltCompiledModule* module,int (*advance)(void*),
    void* state,int32_t* exit_code,char* error,size_t capacity);
BOLT_HOST_API int bolt_compiler_run_persistent(BoltCompiler *compiler, int32_t *exit_code,
    char *error, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
