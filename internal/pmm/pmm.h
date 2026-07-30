#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stdbool.h>
#include "arena.h"
#include "tree_sitter/api.h"

// Language enum mirrors lang.Language in Go.
// Order must match lang_specs.c tables.
typedef enum {
    PMM_LANG_GO = 0,
    PMM_LANG_PYTHON,
    PMM_LANG_JAVASCRIPT,
    PMM_LANG_TYPESCRIPT,
    PMM_LANG_TSX,
    PMM_LANG_RUST,
    PMM_LANG_JAVA,
    PMM_LANG_CPP,
    PMM_LANG_CSHARP,
    PMM_LANG_PHP,
    PMM_LANG_LUA,
    PMM_LANG_SCALA,
    PMM_LANG_KOTLIN,
    PMM_LANG_RUBY,
    PMM_LANG_C,
    PMM_LANG_BASH,
    PMM_LANG_ZIG,
    PMM_LANG_ELIXIR,
    PMM_LANG_HASKELL,
    PMM_LANG_OCAML,
    PMM_LANG_OBJC,
    PMM_LANG_SWIFT,
    PMM_LANG_DART,
    PMM_LANG_PERL,
    PMM_LANG_GROOVY,
    PMM_LANG_ERLANG,
    PMM_LANG_R,
    PMM_LANG_HTML,
    PMM_LANG_CSS,
    PMM_LANG_SCSS,
    PMM_LANG_YAML,
    PMM_LANG_TOML,
    PMM_LANG_HCL,
    PMM_LANG_SQL,
    PMM_LANG_DOCKERFILE,
    // New languages (v0.5 expansion)
    PMM_LANG_CLOJURE,
    PMM_LANG_FSHARP,
    PMM_LANG_JULIA,
    PMM_LANG_VIMSCRIPT,
    PMM_LANG_NIX,
    PMM_LANG_COMMONLISP,
    PMM_LANG_ELM,
    PMM_LANG_FORTRAN,
    PMM_LANG_CUDA,
    PMM_LANG_COBOL,
    PMM_LANG_VERILOG,
    PMM_LANG_EMACSLISP,
    PMM_LANG_JSON,
    PMM_LANG_XML,
    PMM_LANG_MARKDOWN,
    PMM_LANG_MAKEFILE,
    PMM_LANG_CMAKE,
    PMM_LANG_PROTOBUF,
    PMM_LANG_GRAPHQL,
    PMM_LANG_VUE,
    PMM_LANG_SVELTE,
    PMM_LANG_MESON,
    PMM_LANG_GLSL,
    PMM_LANG_INI,
    // Scientific/math languages
    PMM_LANG_MATLAB,
    PMM_LANG_LEAN,
    PMM_LANG_FORM,
    PMM_LANG_MAGMA,
    PMM_LANG_WOLFRAM,
    PMM_LANG_SOLIDITY,
    PMM_LANG_TYPST,
    PMM_LANG_GDSCRIPT,
    PMM_LANG_GLEAM,
    PMM_LANG_POWERSHELL,
    PMM_LANG_PASCAL,
    PMM_LANG_DLANG,
    PMM_LANG_NIM,
    PMM_LANG_SCHEME,
    PMM_LANG_FENNEL,
    PMM_LANG_FISH,
    PMM_LANG_AWK,
    PMM_LANG_ZSH,
    PMM_LANG_TCL,
    PMM_LANG_ADA,
    PMM_LANG_AGDA,
    PMM_LANG_RACKET,
    PMM_LANG_ODIN,
    PMM_LANG_RESCRIPT,
    PMM_LANG_PURESCRIPT,
    PMM_LANG_NICKEL,
    PMM_LANG_CRYSTAL,
    PMM_LANG_TEAL,
    PMM_LANG_HARE,
    PMM_LANG_PONY,
    PMM_LANG_LUAU,
    PMM_LANG_JANET,
    PMM_LANG_SWAY,
    PMM_LANG_NASM,
    PMM_LANG_ASSEMBLY,
    PMM_LANG_ASTRO,
    PMM_LANG_BLADE,
    PMM_LANG_JUST,
    PMM_LANG_GOTEMPLATE,
    PMM_LANG_TEMPL,
    PMM_LANG_LIQUID,
    PMM_LANG_JINJA2,
    PMM_LANG_PRISMA,
    PMM_LANG_HYPRLANG,
    PMM_LANG_DOTENV,
    PMM_LANG_DIFF,
    PMM_LANG_WGSL,
    PMM_LANG_KDL,
    PMM_LANG_JSON5,
    PMM_LANG_JSONNET,
    PMM_LANG_RON,
    PMM_LANG_THRIFT,
    PMM_LANG_CAPNP,
    PMM_LANG_PROPERTIES,
    PMM_LANG_SSHCONFIG,
    PMM_LANG_BIBTEX,
    PMM_LANG_STARLARK,
    PMM_LANG_BICEP,
    PMM_LANG_CSV,
    PMM_LANG_REQUIREMENTS,
    PMM_LANG_HLSL,
    PMM_LANG_VHDL,
    PMM_LANG_SYSTEMVERILOG,
    PMM_LANG_DEVICETREE,
    PMM_LANG_LINKERSCRIPT,
    PMM_LANG_GN,
    PMM_LANG_KCONFIG,
    PMM_LANG_BITBAKE,
    PMM_LANG_SMALI,
    PMM_LANG_TABLEGEN,
    PMM_LANG_ISPC,
    PMM_LANG_CAIRO,
    PMM_LANG_MOVE,
    PMM_LANG_SQUIRREL,
    PMM_LANG_FUNC,
    PMM_LANG_REGEX,
    PMM_LANG_JSDOC,
    PMM_LANG_RST,
    PMM_LANG_BEANCOUNT,
    PMM_LANG_MERMAID,
    PMM_LANG_PUPPET,
    PMM_LANG_PO,
    PMM_LANG_GITATTRIBUTES,
    PMM_LANG_GITIGNORE,
    PMM_LANG_SLANG,
    PMM_LANG_LLVM_IR,
    PMM_LANG_SMITHY,
    PMM_LANG_WIT,
    PMM_LANG_TLAPLUS,
    PMM_LANG_PKL,
    PMM_LANG_GOMOD,
    PMM_LANG_APEX,
    PMM_LANG_SOQL,
    PMM_LANG_SOSL,
    PMM_LANG_KUSTOMIZE,            // kustomization.yaml — Kubernetes overlay tool
    PMM_LANG_K8S,                  // Generic Kubernetes manifest (apiVersion: detected)
    PMM_LANG_PINE,                 // Pine Script (TradingView indicator / strategy language)
    PMM_LANG_QML,                  // Qt QML (Qt Modeling Language — declarative UI + embedded JS)
    PMM_LANG_CFSCRIPT,             // CFML script dialect (.cfc components — Lucee/ColdFusion)
    PMM_LANG_CFML,                 // CFML tag dialect (.cfm templates — Lucee/ColdFusion)
    PMM_LANG_MOJO,                 // Mojo
    PMM_LANG_OBJECTSCRIPT_UDL,     // InterSystems ObjectScript UDL (.cls class files)
    PMM_LANG_OBJECTSCRIPT_ROUTINE, // InterSystems ObjectScript routine (.mac/.int/.rtn/.inc)
    PMM_LANG_OBJECTSCRIPT_EXPORT,  // InterSystems Studio Export XML (<Export generator="Cache">)
    PMM_LANG_COUNT
} CBMLanguage;

// --- Extraction result structs ---

typedef struct {
    const char *name;           // short name
    const char *qualified_name; // project.path.name
    const char *label;          // "Function", "Method", "Class", "Variable", "Module"
    const char *file_path;      // relative path
    uint32_t start_line;
    uint32_t end_line;
    const char *signature;     // parameter text (NULL if none)
    const char *return_type;   // return type text (NULL if none)
    const char *receiver;      // Go method receiver (NULL if none)
    const char *docstring;     // leading doc comment (NULL if none)
    const char *parent_class;  // enclosing class QN for methods (NULL if none)
    const char **decorators;   // NULL-terminated array (NULL if none)
    const char **base_classes; // NULL-terminated array (NULL if none)
    const char **param_names;  // NULL-terminated array (NULL if none)
    const char **param_types;  // NULL-terminated array (NULL if none)
    const char **return_types; // NULL-terminated array (NULL if none)
    const char *route_path;    // HTTP route path from decorator (e.g., "/api/users") or NULL
    const char *route_method;  // HTTP method from decorator (e.g., "POST") or NULL
    int complexity;            // cyclomatic complexity
    int cognitive;             // cognitive complexity (nesting-weighted)
    int loop_count;            // number of loop constructs in the body
    int loop_depth;            // max nested-loop depth (bottleneck proxy)
    bool is_recursive;         // body contains a direct self-call (seed for "recursive")
    int param_count;           // number of parameters (large = complexity smell)
    int max_access_depth;      // deepest chained member/subscript access (a.b.c.d)
    int linear_scan_in_loop;   // count of linear-scan calls (find/contains/indexOf) inside loops
    int alloc_in_loop;         // count of allocation/append calls inside loops
    bool recursion_in_loop;    // a self-call occurs inside a loop body
    bool unguarded_recursion;  // recursive with no self-call guarded by a conditional
    int lines;                 // body line count
    uint32_t *fingerprint;     // MinHash fingerprint (arena-allocated, K values) or NULL
    int fingerprint_k;         // number of hash values (PMM_MINHASH_K or 0)
    bool is_exported;
    bool is_abstract;
    bool is_test;
    bool is_entry_point;
    const char *structural_profile; // AST structural profile (arena-allocated) or NULL
    const char *body_tokens; // space-separated raw identifier tokens from body (arena) or NULL
} CBMDefinition;

/* Argument captured from a call expression */
typedef struct {
    const char *expr;    // raw expression text ("payload.info", "MY_URL", "'hello'")
    const char *value;   // resolved string value or NULL (constant propagation)
    const char *keyword; // keyword name if keyword arg ("url", "topic_id"), NULL if positional
    int index;           // positional index (0-based)
} CBMCallArg;

#define PMM_MAX_CALL_ARGS 8

typedef struct {
    const char *callee_name;            // raw callee text ("pkg.Func", "foo")
    const char *enclosing_func_qn;      // QN of enclosing function (or module QN)
    const char *first_string_arg;       // first string literal argument (URL, topic, key) or NULL
    const char *second_arg_name;        // second argument identifier (handler ref) or NULL
    CBMCallArg args[PMM_MAX_CALL_ARGS]; // first N arguments with expressions
    int arg_count;                      // number of captured arguments
    int loop_depth;                     // enclosing loop nesting at the call site
    int branch_depth;                   // enclosing branch nesting at the call site
    int start_line;                     // 1-based source line of the call (for def range-match)
    bool is_method;                     // method/member call with a non-self receiver. Perl:
                                        // arrow/method call ($obj->m). TS/JS/TSX: member call
                                        // x.foo() whose receiver is not this/super. Default false.
} CBMCall;

typedef struct {
    const char *local_name;  // local alias or name
    const char *module_path; // resolved module path / QN
} CBMImport;

typedef struct {
    const char *ref_name;          // referenced identifier
    const char *enclosing_func_qn; // QN of enclosing function (or module QN)
} CBMUsage;

typedef struct {
    const char *exception_name;    // exception class/type name
    const char *enclosing_func_qn; // QN of enclosing function
} CBMThrow;

typedef struct {
    const char *var_name;          // variable name
    const char *enclosing_func_qn; // QN of enclosing function
    bool is_write;                 // true = write, false = read
} CBMReadWrite;

typedef struct {
    const char *type_name;         // referenced type/class name
    const char *enclosing_func_qn; // QN of enclosing function
} CBMTypeRef;

typedef struct {
    const char *env_key;           // environment variable key
    const char *enclosing_func_qn; // QN of enclosing function
} CBMEnvAccess;

typedef struct {
    const char *var_name;          // variable being assigned
    const char *type_name;         // class/type name of RHS constructor
    const char *enclosing_func_qn; // QN of enclosing function
} CBMTypeAssign;

// String reference: URL, config key, or async target found in source.
// Extracted from string literals during AST walk.
typedef enum {
    PMM_STRREF_URL = 0,    // REST path or full URL
    PMM_STRREF_CONFIG = 1, // config file path or env var key
} CBMStringRefKind;

typedef struct {
    const char *value;             // the string literal content
    const char *enclosing_func_qn; // QN of enclosing function
    const char *key_path;          // dotted key path from YAML/JSON nesting (NULL if flat)
    CBMStringRefKind kind;         // URL, CONFIG
} CBMStringRef;

/* Infrastructure binding: topic/queue → endpoint URL.
 * Extracted from YAML/HCL/JSON subscription/scheduler configs.
 * Used by pass_route_nodes to connect async Route nodes to handler services. */
typedef struct {
    const char *source_name; // topic, queue, or schedule name
    const char *target_url;  // push_endpoint, uri, or http_target URL
    const char *broker;      // "pubsub", "cloud_tasks", "cloud_scheduler", "sqs", "kafka"
} CBMInfraBinding;

/* Pub/sub channel participation.  One record per emit() or on()/addListener()
 * call detected in source — the receiver (e.g. Socket.IO client, EventEmitter
 * instance) is intentionally NOT identified; matching is by channel_name
 * across files, which captures the common pattern of one logical bus per
 * service.  Transport disambiguates Socket.IO vs EventEmitter vs future
 * detectors (Kafka, Cloud Pub/Sub, etc.). */
typedef enum {
    PMM_CHANNEL_EMIT = 0,
    PMM_CHANNEL_LISTEN = 1,
} CBMChannelDirection;

typedef struct {
    const char *channel_name;      // literal channel name (e.g. "user.created")
    const char *transport;         // "socketio", "event_emitter", ...
    const char *enclosing_func_qn; // QN of the function containing the emit/on call
    CBMChannelDirection direction;
} CBMChannel;

// Rust: impl Trait for Struct
typedef struct {
    const char *trait_name;  // trait name (raw text)
    const char *struct_name; // struct/type name (raw text)
} CBMImplTrait;

// LSP-resolved call: high-confidence type-aware call resolution
typedef struct {
    const char *caller_qn; // enclosing function QN
    const char *callee_qn; // resolved target QN (fully qualified)
    const char *strategy;  // "lsp_type_dispatch", "lsp_direct", etc.
    float confidence;      // 0.90-0.95
    const char *reason;    // diagnostic label for unresolved calls (NULL if resolved)
} CBMResolvedCall;

typedef struct {
    CBMResolvedCall *items;
    int count;
    int cap;
} CBMResolvedCallArray;

// Growable arrays used during extraction.
typedef struct {
    CBMDefinition *items;
    int count;
    int cap;
} CBMDefArray;

typedef struct {
    CBMCall *items;
    int count;
    int cap;
} CBMCallArray;

typedef struct {
    CBMImport *items;
    int count;
    int cap;
} CBMImportArray;

typedef struct {
    CBMUsage *items;
    int count;
    int cap;
} CBMUsageArray;

typedef struct {
    CBMThrow *items;
    int count;
    int cap;
} CBMThrowArray;

typedef struct {
    CBMReadWrite *items;
    int count;
    int cap;
} CBMRWArray;

typedef struct {
    CBMTypeRef *items;
    int count;
    int cap;
} CBMTypeRefArray;

typedef struct {
    CBMEnvAccess *items;
    int count;
    int cap;
} CBMEnvAccessArray;

typedef struct {
    CBMTypeAssign *items;
    int count;
    int cap;
} CBMTypeAssignArray;

typedef struct {
    CBMStringRef *items;
    int count;
    int cap;
} CBMStringRefArray;

typedef struct {
    CBMInfraBinding *items;
    int count;
    int cap;
} CBMInfraBindingArray;

typedef struct {
    CBMImplTrait *items;
    int count;
    int cap;
} CBMImplTraitArray;

typedef struct {
    CBMChannel *items;
    int count;
    int cap;
} CBMChannelArray;

// Full extraction result for one file.
typedef struct {
    CBMArena arena; // owns all string memory

    CBMDefArray defs;
    CBMCallArray calls;
    CBMImportArray imports;
    CBMUsageArray usages;
    CBMThrowArray throws;
    CBMRWArray rw;
    CBMTypeRefArray type_refs;
    CBMEnvAccessArray env_accesses;
    CBMTypeAssignArray type_assigns;
    CBMImplTraitArray impl_traits;       // Rust: impl Trait for Struct pairs
    CBMResolvedCallArray resolved_calls; // LSP-resolved calls (high confidence)
    CBMStringRefArray string_refs;       // URL/config string literals from AST
    CBMInfraBindingArray infra_bindings; // topic→URL pairs from IaC configs
    CBMChannelArray channels;            // Socket.IO / EventEmitter pub/sub participation

    const char *module_qn;      // module qualified name
    const char *namespace_name; // declared namespace/package (Java/Kotlin/C#/PHP), NULL if none
    const char **exports;       // NULL-terminated (NULL if none)
    const char **constants;     // NULL-terminated (NULL if none)
    const char **global_vars;   // NULL-terminated (NULL if none)
    const char **macros;        // NULL-terminated, C/C++ only (NULL if none)

    bool has_error;
    const char *error_msg;
    /* Best-effort parse-coverage signal (experimental). parse_incomplete is true
     * when the parse tree contains tree-sitter ERROR/MISSING nodes — constructs
     * in those regions are silently absent from the graph. error_ranges is a
     * compact "start-end,start-end" list of 1-based line ranges (arena-owned) or
     * NULL. This only marks what we can DETECT: the absence of a flag is NOT a
     * completeness guarantee. Callers should treat a flagged file as "prefer
     * grep here", never treat an unflagged file as provably complete. */
    bool parse_incomplete;
    const char *error_ranges;
    int error_region_count;
    bool is_test_file;
    int imports_count;
    TSTree *cached_tree;     // retained parse tree (caller frees via pmm_free_tree)
    CBMLanguage cached_lang; // language of cached tree (for parser selection)

    // Retained source bytes — copied into `arena` by the parallel
    // extract pass so the fused cross-file LSP step in resolve_worker
    // can run without re-reading the file from disk. NULL when the
    // file exceeded the per-file (100 MB) or total (2 GB) retention
    // cap; in that case the cross-file LSP step is skipped for this
    // file (defs/calls already extracted are unaffected).
    const char *source;
    int source_len;
} CBMFileResult;

// --- Enclosing function cache ---
// Avoids repeated parent-chain walks for nodes within the same function body.
// Each entry records a function's byte range and its precomputed QN.
#define EFC_SIZE 64 // power of 2 for fast modulo

typedef struct {
    uint32_t start_byte;
    uint32_t end_byte;
    const char *qn;
} EFCEntry;

typedef struct {
    EFCEntry entries[EFC_SIZE];
    int count;
} EFCache;

// --- Extraction context passed to sub-extractors ---

// Module-level string constant map (for constant propagation)
#define PMM_MAX_STRING_CONSTANTS 256
typedef struct {
    const char *names[PMM_MAX_STRING_CONSTANTS];
    const char *values[PMM_MAX_STRING_CONSTANTS];
    int count;
} CBMStringConstantMap;

// Forward declaration: ObjectScript macro table (defined in macro_table.h).
typedef struct CBMMacroTable CBMMacroTable;

// Method-return-type table for ObjectScript variable type inference. Populated
// from definition nodes (method QN -> declared return type) so a later
// `Set x = obj.Method()` can resolve x's class.
#define PMM_RETURN_TYPE_TABLE_CAP 2048

typedef struct {
    const char *method_qn;
    const char *return_type;
} CBMReturnTypeEntry;

typedef struct {
    CBMReturnTypeEntry entries[PMM_RETURN_TYPE_TABLE_CAP];
    int count;
} CBMReturnTypeTable;

typedef struct {
    CBMArena *arena;
    CBMFileResult *result;
    const char *source;
    int source_len;
    CBMLanguage language;
    const char *project;
    const char *rel_path;
    const char *module_qn;
    TSNode root;
    EFCache ef_cache;                            // enclosing function cache
    const char *enclosing_class_qn;              // for nested class QN computation
    CBMStringConstantMap string_constants;       // module-level NAME = "value" pairs
    const CBMMacroTable *macro_table;            // ObjectScript $$$macro table (NULL if none)
    const CBMReturnTypeTable *return_type_table; // ObjectScript method return types (NULL if none)
} CBMExtractCtx;

// --- Public API ---

// Bind third-party allocators (tree-sitter, sqlite3) to mimalloc as
// defense-in-depth, so they never depend on the fragile MI_OVERRIDE symbol
// override (#424). MUST be called as the very first statement of main(), before
// any sqlite3_open*/sqlite3_initialize (SQLITE_CONFIG_MALLOC returns
// SQLITE_MISUSE once sqlite has initialized).
// Idempotent (static guard); intended for single-threaded startup. pmm_init()
// also calls it so non-main entry points (pipeline passes) still get the binds.
// In the test build (no PMM_BIND_TS_ALLOCATOR) this is a no-op.
void pmm_alloc_init(void);

// Initialize the library. Call once at startup. Returns 0 on success.
int pmm_init(void);

// True when rel_path is in the crash-quarantine set — the newline-delimited list
// of files (PMM_INDEX_QUARANTINE_FILE) the crash supervisor pinned as crashers
// during its single-threaded recovery re-run. Loaded once, lazily; read-only
// after load. pmm_extract_file short-circuits such files to an empty result so no
// pass can crash on them; the pipeline extract loops call this to also REPORT the
// skip as phase="crash". Always false (cheap no-op) when the env var is unset.
bool pmm_index_is_quarantined(const char *rel_path);

// Phase a quarantined file was pinned under: "crash" (a fault signal) or "hang"
// (killed for making no progress). Returns NULL when rel_path is not quarantined.
// Drives the same lazy once-load as pmm_index_is_quarantined. Used by the pipeline
// extract loops to report the skip's phase in skipped[] (falls back to "crash").
const char *pmm_index_quarantine_phase(const char *rel_path);

// Crash-supervisor marker journal (parallel-safe): appends "S <rel_path>" /
// "D <rel_path>" to PMM_INDEX_MARKER_FILE. Files with an S but no D form the
// parent's crash/hang suspect set. No-ops when the env var is unset.
// pmm_extract_file journals its own start/done; long-running per-file phases
// (cross-LSP resolve) call these around their per-file work so a hang there
// is attributed to the RIGHT file instead of a stale extraction marker.
void pmm_index_mark_start(const char *rel_path);
void pmm_index_mark_done(const char *rel_path);

// Extract all data from one file. Caller must call pmm_free_result().
// source must remain valid for the duration of the call.
// timeout_micros: per-file parse timeout in microseconds (0 = no timeout).
CBMFileResult *pmm_extract_file(const char *source, int source_len, CBMLanguage language,
                                const char *project, const char *rel_path, int64_t timeout_micros,
                                const char **extra_defines, // NULL-terminated, or NULL
                                const char **include_paths  // NULL-terminated, or NULL
);

// Pipeline-internal variant of pmm_extract_file() carrying ObjectScript
// per-project tables (macro table + method-return-type table). The public
// pmm_extract_file() is a thin wrapper that passes NULL, NULL for both.
CBMFileResult *pmm_extract_file_ex(
    const char *source, int source_len, CBMLanguage language, const char *project,
    const char *rel_path, int64_t timeout_micros,
    const char **extra_defines,                 // NULL-terminated, or NULL
    const char **include_paths,                 // NULL-terminated, or NULL
    const CBMMacroTable *macro_table,           // ObjectScript macros, or NULL
    const CBMReturnTypeTable *return_type_table // OS return types, or NULL
);

// Free all memory associated with a result.
void pmm_free_result(CBMFileResult *result);

// Free only the cached tree from a result (caller retained it for reuse).
void pmm_free_tree(CBMFileResult *result);

// Free a standalone TSTree pointer (for Go layer cleanup).
void pmm_free_tree_ptr(TSTree *tree);

// Reset the thread-local parser's internal state, releasing slab-allocated
// subtrees. Must be called BEFORE pmm_slab_reset_thread() so the slab rebuild
// doesn't corrupt live parser state.
void pmm_reset_thread_parser(void);

// Destroy the thread-local parser. Call on worker thread exit.
void pmm_destroy_thread_parser(void);

// Shutdown the library. Call once at exit.
void pmm_shutdown(void);

// Profiling: get accumulated parse/extraction times and file count.
typedef struct {
    uint64_t *parse_ns;
    uint64_t *extract_ns;
    uint64_t *files;
} pmm_profile_out_t;
void pmm_get_profile(pmm_profile_out_t out);
uint64_t pmm_get_lsp_ns(void);
uint64_t pmm_get_preprocess_ns(void);
uint64_t pmm_get_files_preprocessed(void);
void pmm_reset_profile(void);

// Toggle C/C++ preprocessor Macro-node extraction (#375). The pipeline enables
// it only for full/advanced index modes (it dominates extraction on macro-dense
// codebases). Default ON. Set before extraction; read-only during.
void pmm_set_macro_extraction(int enabled);
int pmm_macro_extraction_enabled(void);

// --- Internal helpers used by extractors ---

// Growable array push functions (arena-allocated, no individual free needed).
void pmm_defs_push(CBMDefArray *arr, CBMArena *a, CBMDefinition def);
void pmm_calls_push(CBMCallArray *arr, CBMArena *a, CBMCall call);
void pmm_imports_push(CBMImportArray *arr, CBMArena *a, CBMImport imp);
void pmm_usages_push(CBMUsageArray *arr, CBMArena *a, CBMUsage usage);
void pmm_throws_push(CBMThrowArray *arr, CBMArena *a, CBMThrow thr);
void pmm_rw_push(CBMRWArray *arr, CBMArena *a, CBMReadWrite rw);
void pmm_typerefs_push(CBMTypeRefArray *arr, CBMArena *a, CBMTypeRef tr);
void pmm_envaccess_push(CBMEnvAccessArray *arr, CBMArena *a, CBMEnvAccess ea);
void pmm_typeassign_push(CBMTypeAssignArray *arr, CBMArena *a, CBMTypeAssign ta);
void pmm_stringref_push(CBMStringRefArray *arr, CBMArena *a, CBMStringRef sr);
void pmm_infrabinding_push(CBMInfraBindingArray *arr, CBMArena *a, CBMInfraBinding ib);
void pmm_impltrait_push(CBMImplTraitArray *arr, CBMArena *a, CBMImplTrait it);
void pmm_resolvedcall_push(CBMResolvedCallArray *arr, CBMArena *a, CBMResolvedCall rc);
void pmm_channels_push(CBMChannelArray *arr, CBMArena *a, CBMChannel ch);

// --- Sub-extractor entry points ---

void pmm_extract_definitions(CBMExtractCtx *ctx);
void pmm_extract_imports(CBMExtractCtx *ctx);
void pmm_extract_usages(CBMExtractCtx *ctx);
void pmm_extract_semantic(CBMExtractCtx *ctx);
void pmm_extract_type_refs(CBMExtractCtx *ctx);
void pmm_extract_env_accesses(CBMExtractCtx *ctx);
void pmm_extract_type_assigns(CBMExtractCtx *ctx);
void pmm_extract_channels(CBMExtractCtx *ctx);

// Single-pass unified extraction (replaces the 7 calls above except defs+imports).
void pmm_extract_unified(CBMExtractCtx *ctx);

// K8s / Kustomize semantic extractor (called when language is PMM_LANG_K8S or PMM_LANG_KUSTOMIZE).
void pmm_extract_k8s(CBMExtractCtx *ctx);

// --- Label predicates ---

// True when `label` names a TYPE-LIKE container definition — a node that can own
// methods/fields, be a base/embedded type, satisfy/declare an interface, and be a
// target of name→type resolution. The canonical set is:
//   Class, Struct, Interface, Enum, Type, Trait.
// Single source of truth for every type-resolution / registry-seeding /
// INHERITS·IMPLEMENTS / LSP-type-registrar consumer, so adding a new type-like
// label (e.g. "Struct" for Rust/Go/Swift/D structs) updates them all at once
// instead of scattering `|| strcmp(label,"Struct")==0` across the tree.
// `label` may be NULL (returns false). Defined in helpers.c.
bool pmm_label_is_type_like(const char *label);

#endif // PMM_H
