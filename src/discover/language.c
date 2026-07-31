/*
 * language.c — Language detection from filename and extension.
 *
 * Maps file extensions and special filenames to CBMLanguage enum values.
 * Handles .m disambiguation (Objective-C vs Magma vs MATLAB).
 * Consults the process-global user config (set via pmm_set_user_lang_config)
 * before the built-in lookup table.
 */
#include "discover/discover.h"
#include "discover/userconfig.h"
#include "pmm.h" // CBMLanguage, PMM_LANG_*

#include "foundation/constants.h"
#include "foundation/compat_fs.h"

enum { LANG_SCAN_PASSES = 2 };
#define SLEN(s) (sizeof(s) - 1)
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* ── Extension → Language lookup table ───────────────────────────── */

typedef struct {
    const char *ext; /* including dot, e.g. ".go" */
    CBMLanguage language;
} ext_entry_t;

/* Sorted by extension for binary search (but linear scan is fine for ~120 entries) */
static const ext_entry_t EXT_TABLE[] = {
    /* Bash */
    {".bash", PMM_LANG_BASH},
    {".sh", PMM_LANG_BASH},

    /* C */
    {".c", PMM_LANG_C},

    /* C++ */
    {".cc", PMM_LANG_CPP},
    {".ccm", PMM_LANG_CPP},
    {".cpp", PMM_LANG_CPP},
    {".cppm", PMM_LANG_CPP},
    {".cxx", PMM_LANG_CPP},
    {".h", PMM_LANG_CPP},
    {".hh", PMM_LANG_CPP},
    {".hpp", PMM_LANG_CPP},
    {".hxx", PMM_LANG_CPP},
    {".ixx", PMM_LANG_CPP},

    /* C# */
    {".cs", PMM_LANG_CSHARP},

    /* Clojure */
    {".clj", PMM_LANG_CLOJURE},
    {".cljc", PMM_LANG_CLOJURE},
    {".cljs", PMM_LANG_CLOJURE},

    /* CMake */
    {".cmake", PMM_LANG_CMAKE},

    /* COBOL */
    {".cbl", PMM_LANG_COBOL},
    {".cob", PMM_LANG_COBOL},

    /* Common Lisp */
    {".cl", PMM_LANG_COMMONLISP},
    {".lisp", PMM_LANG_COMMONLISP},
    {".lsp", PMM_LANG_COMMONLISP},

    /* CSS */
    {".css", PMM_LANG_CSS},

    /* CUDA */
    {".cu", PMM_LANG_CUDA},
    {".cuh", PMM_LANG_CUDA},

    /* Dart */
    {".dart", PMM_LANG_DART},

    /* Dockerfile */
    {".dockerfile", PMM_LANG_DOCKERFILE},

    /* Elixir */
    {".ex", PMM_LANG_ELIXIR},
    {".exs", PMM_LANG_ELIXIR},

    /* DotEnv */
    {".env", PMM_LANG_DOTENV},

    /* Elm */
    {".elm", PMM_LANG_ELM},

    /* Emacs Lisp */
    {".el", PMM_LANG_EMACSLISP},

    /* Erlang */
    {".erl", PMM_LANG_ERLANG},

    /* F# */
    {".fs", PMM_LANG_FSHARP},
    {".fsi", PMM_LANG_FSHARP},
    {".fsx", PMM_LANG_FSHARP},

    /* FORM */
    {".frm", PMM_LANG_FORM},
    {".prc", PMM_LANG_FORM},

    /* Fortran */
    {".f03", PMM_LANG_FORTRAN},
    {".f08", PMM_LANG_FORTRAN},
    {".f90", PMM_LANG_FORTRAN},
    {".f95", PMM_LANG_FORTRAN},

    /* GLSL */
    {".frag", PMM_LANG_GLSL},
    {".glsl", PMM_LANG_GLSL},
    {".vert", PMM_LANG_GLSL},

    /* Go */
    {".go", PMM_LANG_GO},

    /* GraphQL */
    {".gql", PMM_LANG_GRAPHQL},
    {".graphql", PMM_LANG_GRAPHQL},

    /* Groovy */
    {".gradle", PMM_LANG_GROOVY},
    {".groovy", PMM_LANG_GROOVY},

    /* Haskell */
    {".hs", PMM_LANG_HASKELL},

    /* HCL / Terraform */
    {".hcl", PMM_LANG_HCL},
    {".tf", PMM_LANG_HCL},

    /* HTML */
    {".htm", PMM_LANG_HTML},
    {".html", PMM_LANG_HTML},

    /* INI */
    {".cfg", PMM_LANG_INI},
    {".conf", PMM_LANG_INI},
    {".ini", PMM_LANG_INI},

    /* Java */
    {".java", PMM_LANG_JAVA},

    /* JavaScript */
    {".js", PMM_LANG_JAVASCRIPT},
    {".jsx", PMM_LANG_JAVASCRIPT},
    {".mjs", PMM_LANG_JAVASCRIPT}, /* ES modules (#197) */
    {".cjs", PMM_LANG_JAVASCRIPT}, /* CommonJS modules */

    /* JSON */
    {".json", PMM_LANG_JSON},

    /* Julia */
    {".jl", PMM_LANG_JULIA},

    /* Kotlin */
    {".kt", PMM_LANG_KOTLIN},
    {".kts", PMM_LANG_KOTLIN},

    /* Lean */
    {".lean", PMM_LANG_LEAN},

    /* Lua */
    {".lua", PMM_LANG_LUA},

    /* Magma */
    {".mag", PMM_LANG_MAGMA},
    {".magma", PMM_LANG_MAGMA},

    /* Makefile */
    {".mk", PMM_LANG_MAKEFILE},

    /* Markdown */
    {".md", PMM_LANG_MARKDOWN},
    {".mdx", PMM_LANG_MARKDOWN},
    /* Plain / office context docs (discovered as markdown lane; pass_docs classifies) */
    {".txt", PMM_LANG_MARKDOWN},
    {".text", PMM_LANG_MARKDOWN},
    {".pdf", PMM_LANG_MARKDOWN},
    {".docx", PMM_LANG_MARKDOWN},

    /* MATLAB */
    {".m", PMM_LANG_MATLAB},
    {".matlab", PMM_LANG_MATLAB},
    {".mlx", PMM_LANG_MATLAB},

    /* Meson */
    {".meson", PMM_LANG_MESON},

    /* Mojo */
    {".mojo", PMM_LANG_MOJO},

    /* Nix */
    {".nix", PMM_LANG_NIX},

    /* OCaml */
    {".ml", PMM_LANG_OCAML},
    {".mli", PMM_LANG_OCAML},

    /* Perl */
    {".pl", PMM_LANG_PERL},
    {".pm", PMM_LANG_PERL},

    /* PHP */
    {".php", PMM_LANG_PHP},

    /* Protobuf */
    {".proto", PMM_LANG_PROTOBUF},

    /* Python */
    {".py", PMM_LANG_PYTHON},

    /* R — case insensitive handled separately */
    {".R", PMM_LANG_R},
    {".r", PMM_LANG_R},

    /* Ruby */
    {".gemspec", PMM_LANG_RUBY},
    {".rake", PMM_LANG_RUBY},
    {".rb", PMM_LANG_RUBY},

    /* Rust */
    {".rs", PMM_LANG_RUST},

    /* Scala */
    {".sc", PMM_LANG_SCALA},
    {".scala", PMM_LANG_SCALA},

    /* SCSS */
    {".scss", PMM_LANG_SCSS},

    /* SQL */
    {".sql", PMM_LANG_SQL},

    /* Svelte */
    {".svelte", PMM_LANG_SVELTE},

    /* Swift */
    {".swift", PMM_LANG_SWIFT},

    /* SystemVerilog + Verilog */
    {".sv", PMM_LANG_VERILOG},
    {".v", PMM_LANG_VERILOG},

    /* TOML */
    {".toml", PMM_LANG_TOML},

    /* TSX */
    {".tsx", PMM_LANG_TSX},

    /* TypeScript */
    {".ts", PMM_LANG_TYPESCRIPT},
    {".mts", PMM_LANG_TYPESCRIPT}, /* TS ES modules */
    {".cts", PMM_LANG_TYPESCRIPT}, /* TS CommonJS modules */

    /* VimScript */
    {".vim", PMM_LANG_VIMSCRIPT},
    {".vimrc", PMM_LANG_VIMSCRIPT},
    {"justfile", PMM_LANG_JUST},
    {"Justfile", PMM_LANG_JUST},
    {".justfile", PMM_LANG_JUST},
    {".just", PMM_LANG_JUST}, /* `import 'common.just'` target files */
    {"hyprland.conf", PMM_LANG_HYPRLANG},
    {"ssh_config", PMM_LANG_SSHCONFIG},
    {"sshd_config", PMM_LANG_SSHCONFIG},
    {"BUILD", PMM_LANG_STARLARK},
    {"BUILD.bazel", PMM_LANG_STARLARK},
    {"WORKSPACE", PMM_LANG_STARLARK},
    {"WORKSPACE.bazel", PMM_LANG_STARLARK},

    /* BitBake include fragments — `require/include foo.inc` target files.
     * NOTE: .inc is also used by ObjectScript include (macro) files; the
     * ambiguity is resolved by content in pmm_disambiguate_inc(). */
    {".inc", PMM_LANG_BITBAKE},

    /* InterSystems ObjectScript routines (.mac/.int/.rtn unambiguous; .cls is
     * shared with Apex and resolved by content in pmm_disambiguate_cls()). */
    {".mac", PMM_LANG_OBJECTSCRIPT_ROUTINE},
    {".int", PMM_LANG_OBJECTSCRIPT_ROUTINE},
    {".rtn", PMM_LANG_OBJECTSCRIPT_ROUTINE},

    /* Vue */
    {".vue", PMM_LANG_VUE},

    /* Wolfram */
    {".wl", PMM_LANG_WOLFRAM},
    {".wls", PMM_LANG_WOLFRAM},

    /* XML */
    {".xml", PMM_LANG_XML},
    {".xsd", PMM_LANG_XML},
    {".xsl", PMM_LANG_XML},
    {".svg", PMM_LANG_XML},

    /* YAML */
    {".yaml", PMM_LANG_YAML},
    {".yml", PMM_LANG_YAML},

    /* Ada */
    {".adb", PMM_LANG_ADA},

    /* Ada */
    {".ads", PMM_LANG_ADA},

    /* Agda */
    {".agda", PMM_LANG_AGDA},

    /* Astro */
    {".astro", PMM_LANG_ASTRO},

    /* AWK */
    {".awk", PMM_LANG_AWK},

    /* BitBake */
    {".bb", PMM_LANG_BITBAKE},

    /* BitBake */
    {".bbappend", PMM_LANG_BITBAKE},

    /* BitBake */
    {".bbclass", PMM_LANG_BITBAKE},

    /* Beancount */
    {".beancount", PMM_LANG_BEANCOUNT},

    /* BibTeX */
    {".bib", PMM_LANG_BIBTEX},

    /* Bicep */
    {".bicep", PMM_LANG_BICEP},

    /* Blade */
    /* .blade.php handled by userconfig compound extensions, not EXT_TABLE */

    /* Starlark */
    {".bzl", PMM_LANG_STARLARK},

    /* Cairo */
    {".cairo", PMM_LANG_CAIRO},

    /* Cap'n Proto */
    {".capnp", PMM_LANG_CAPNP},

    /* Apex */
    {".cls", PMM_LANG_APEX},

    /* Crystal */
    {".cr", PMM_LANG_CRYSTAL},

    /* CSV */
    {".csv", PMM_LANG_CSV},

    /* D */
    {".d", PMM_LANG_DLANG},

    /* Diff */
    {".diff", PMM_LANG_DIFF},

    /* Pascal */
    {".dpr", PMM_LANG_PASCAL},

    /* DeviceTree */
    {".dts", PMM_LANG_DEVICETREE},

    /* DeviceTree */
    {".dtsi", PMM_LANG_DEVICETREE},

    /* FunC */
    {".fc", PMM_LANG_FUNC},

    /* Fish */
    {".fish", PMM_LANG_FISH},

    /* Fennel */
    {".fnl", PMM_LANG_FENNEL},

    /* HLSL */
    {".fx", PMM_LANG_HLSL},

    /* GDScript */
    {".gd", PMM_LANG_GDSCRIPT},

    /* Gleam */
    {".gleam", PMM_LANG_GLEAM},

    /* GN */
    {".gn", PMM_LANG_GN},

    /* GN */
    {".gni", PMM_LANG_GN},

    /* Go Template */
    {".gotmpl", PMM_LANG_GOTEMPLATE},
    {".tpl", PMM_LANG_GOTEMPLATE}, /* Helm _helpers.tpl named-template definitions */

    /* Hare */
    {".ha", PMM_LANG_HARE},

    /* Hyprlang */
    {".hl", PMM_LANG_HYPRLANG},

    /* HLSL */
    {".hlsl", PMM_LANG_HLSL},

    /* HLSL */
    {".hlsli", PMM_LANG_HLSL},

    /* ISPC */
    {".ispc", PMM_LANG_ISPC},

    /* Jinja2 */
    {".j2", PMM_LANG_JINJA2},

    /* Janet */
    {".janet", PMM_LANG_JANET},

    /* Jinja2 */
    {".jinja", PMM_LANG_JINJA2},

    /* Jinja2 */
    {".jinja2", PMM_LANG_JINJA2},

    /* JSON5 */
    {".json5", PMM_LANG_JSON5},

    /* Jsonnet */
    {".jsonnet", PMM_LANG_JSONNET},

    /* KDL */
    {".kdl", PMM_LANG_KDL},

    /* Linker Script */
    {".ld", PMM_LANG_LINKERSCRIPT},

    /* Linker Script */
    {".lds", PMM_LANG_LINKERSCRIPT},

    /* Jsonnet */
    {".libsonnet", PMM_LANG_JSONNET},

    /* Liquid */
    {".liquid", PMM_LANG_LIQUID},

    /* LLVM IR */
    {".ll", PMM_LANG_LLVM_IR},

    /* Pascal */
    {".lpr", PMM_LANG_PASCAL},

    /* Luau */
    {".luau", PMM_LANG_LUAU},

    /* Qt QML */
    {".qml", PMM_LANG_QML},

    /* CFML / ColdFusion — .cfc components are script-dialect; .cfm are tag templates */
    {".cfc", PMM_LANG_CFSCRIPT},
    {".cfm", PMM_LANG_CFML},

    /* Mermaid */
    {".mermaid", PMM_LANG_MERMAID},

    /* Mermaid */
    {".mmd", PMM_LANG_MERMAID},

    /* Move */
    {".move", PMM_LANG_MOVE},

    /* NASM */
    {".nasm", PMM_LANG_NASM},

    /* Nickel */
    {".ncl", PMM_LANG_NICKEL},

    /* Nim */

    /* Nim */

    /* Squirrel */
    {".nut", PMM_LANG_SQUIRREL},

    /* Odin */
    {".odin", PMM_LANG_ODIN},

    /* DeviceTree */
    {".overlay", PMM_LANG_DEVICETREE},

    /* Pascal */
    {".pas", PMM_LANG_PASCAL},

    /* Diff */
    {".patch", PMM_LANG_DIFF},

    /* Pine Script */
    {".pine", PMM_LANG_PINE},

    /* Pkl */
    {".pkl", PMM_LANG_PKL},

    /* PO */
    {".po", PMM_LANG_PO},

    /* Pony */
    {".pony", PMM_LANG_PONY},

    /* PO */
    {".pot", PMM_LANG_PO},

    /* Puppet */
    {".pp", PMM_LANG_PUPPET},

    /* Prisma */
    {".prisma", PMM_LANG_PRISMA},

    /* Properties */
    {".properties", PMM_LANG_PROPERTIES},

    /* PowerShell */
    {".ps1", PMM_LANG_POWERSHELL},

    /* PowerShell */
    {".psd1", PMM_LANG_POWERSHELL},

    /* PowerShell */
    {".psm1", PMM_LANG_POWERSHELL},

    /* PureScript */
    {".purs", PMM_LANG_PURESCRIPT},

    /* ReScript */
    {".res", PMM_LANG_RESCRIPT},

    /* ReScript */
    {".resi", PMM_LANG_RESCRIPT},

    /* Regex */
    {".re", PMM_LANG_REGEX},

    /* Racket */
    {".rkt", PMM_LANG_RACKET},

    /* RON */
    {".ron", PMM_LANG_RON},

    /* reStructuredText */
    {".rst", PMM_LANG_RST},

    /* Assembly */
    {".s", PMM_LANG_ASSEMBLY},

    /* Assembly */
    {".S", PMM_LANG_ASSEMBLY},

    /* Scheme */
    {".scm", PMM_LANG_SCHEME},

    /* Slang */
    {".slang", PMM_LANG_SLANG},

    /* Smali */
    {".smali", PMM_LANG_SMALI},

    /* Smithy */
    {".smithy", PMM_LANG_SMITHY},

    /* Solidity */
    {".sol", PMM_LANG_SOLIDITY},

    /* SOQL */
    {".soql", PMM_LANG_SOQL},

    /* SOSL */
    {".sosl", PMM_LANG_SOSL},

    /* Scheme */
    {".ss", PMM_LANG_SCHEME},

    /* Starlark */
    {".star", PMM_LANG_STARLARK},

    /* SystemVerilog */

    /* SystemVerilog */

    /* Sway */
    {".sw", PMM_LANG_SWAY},

    /* Tcl */
    {".tcl", PMM_LANG_TCL},

    /* TableGen */
    {".td", PMM_LANG_TABLEGEN},

    /* Templ */
    {".templ", PMM_LANG_TEMPL},

    /* Thrift */
    {".thrift", PMM_LANG_THRIFT},

    /* Teal */
    {".tl", PMM_LANG_TEAL},

    /* TLA+ */
    {".tla", PMM_LANG_TLAPLUS},

    /* Go Template */
    {".tmpl", PMM_LANG_GOTEMPLATE},

    /* Apex */
    {".trigger", PMM_LANG_APEX},

    /* Typst */
    {".typ", PMM_LANG_TYPST},

    /* VHDL */
    {".vhd", PMM_LANG_VHDL},

    /* VHDL */
    {".vhdl", PMM_LANG_VHDL},

    /* WGSL */
    {".wgsl", PMM_LANG_WGSL},

    /* WIT */
    {".wit", PMM_LANG_WIT},

    /* Zsh */
    {".zsh", PMM_LANG_ZSH},

    /* Zig */
    {".zig", PMM_LANG_ZIG},
};

#define EXT_TABLE_SIZE (sizeof(EXT_TABLE) / sizeof(EXT_TABLE[0]))

/* ── Special filename → Language lookup ──────────────────────────── */

typedef struct {
    const char *filename;
    CBMLanguage language;
} filename_entry_t;

static const filename_entry_t FILENAME_TABLE[] = {
    {"CMakeLists.txt", PMM_LANG_CMAKE},
    {"Dockerfile", PMM_LANG_DOCKERFILE},
    {"GNUmakefile", PMM_LANG_MAKEFILE},
    {"Makefile", PMM_LANG_MAKEFILE},
    {"makefile", PMM_LANG_MAKEFILE},
    {"meson.build", PMM_LANG_MESON},
    {"meson.options", PMM_LANG_MESON},
    {"meson_options.txt", PMM_LANG_MESON},
    {"kustomization.yaml", PMM_LANG_KUSTOMIZE},
    {"kustomization.yml", PMM_LANG_KUSTOMIZE},
    /* Note: FILENAME_TABLE uses case-sensitive strcmp, so mixed-case variants
     * (e.g. "Kustomization.yaml") are not matched here.  They fall through to
     * PMM_LANG_YAML and are re-classified by pmm_is_kustomize_file() in
     * pass_k8s.c, which performs a case-insensitive comparison.  This is the
     * intended behaviour — no additional entries are needed. */
    {".vimrc", PMM_LANG_VIMSCRIPT},
    {".zshrc", PMM_LANG_ZSH},
    {".zshenv", PMM_LANG_ZSH},
    {".zprofile", PMM_LANG_ZSH},
    {"justfile", PMM_LANG_JUST},
    {"Justfile", PMM_LANG_JUST},
    {".justfile", PMM_LANG_JUST},
    {"hyprland.conf", PMM_LANG_HYPRLANG},
    {"ssh_config", PMM_LANG_SSHCONFIG},
    {"sshd_config", PMM_LANG_SSHCONFIG},
    {".ssh/config", PMM_LANG_SSHCONFIG},
    {"BUILD", PMM_LANG_STARLARK},
    {"BUILD.bazel", PMM_LANG_STARLARK},
    {"WORKSPACE", PMM_LANG_STARLARK},
    {"WORKSPACE.bazel", PMM_LANG_STARLARK},
    {"requirements.txt", PMM_LANG_REQUIREMENTS},
    {"requirements-dev.txt", PMM_LANG_REQUIREMENTS},
    {"requirements-test.txt", PMM_LANG_REQUIREMENTS},
    {"Kconfig", PMM_LANG_KCONFIG},
    {"go.mod", PMM_LANG_GOMOD},
    {".env", PMM_LANG_DOTENV},
    {".env.local", PMM_LANG_DOTENV},
    {".gitattributes", PMM_LANG_GITATTRIBUTES},

};

#define FILENAME_TABLE_SIZE (sizeof(FILENAME_TABLE) / sizeof(FILENAME_TABLE[0]))

/* ── Language names ──────────────────────────────────────────────── */

static const char *LANG_NAMES[PMM_LANG_COUNT] = {
    [PMM_LANG_GO] = "Go",
    [PMM_LANG_PYTHON] = "Python",
    [PMM_LANG_JAVASCRIPT] = "JavaScript",
    [PMM_LANG_TYPESCRIPT] = "TypeScript",
    [PMM_LANG_TSX] = "TSX",
    [PMM_LANG_RUST] = "Rust",
    [PMM_LANG_JAVA] = "Java",
    [PMM_LANG_CPP] = "C++",
    [PMM_LANG_CSHARP] = "C#",
    [PMM_LANG_PHP] = "PHP",
    [PMM_LANG_LUA] = "Lua",
    [PMM_LANG_SCALA] = "Scala",
    [PMM_LANG_KOTLIN] = "Kotlin",
    [PMM_LANG_RUBY] = "Ruby",
    [PMM_LANG_C] = "C",
    [PMM_LANG_BASH] = "Bash",
    [PMM_LANG_ZIG] = "Zig",
    [PMM_LANG_ELIXIR] = "Elixir",
    [PMM_LANG_HASKELL] = "Haskell",
    [PMM_LANG_OCAML] = "OCaml",
    [PMM_LANG_OBJC] = "Objective-C",
    [PMM_LANG_SWIFT] = "Swift",
    [PMM_LANG_DART] = "Dart",
    [PMM_LANG_PERL] = "Perl",
    [PMM_LANG_GROOVY] = "Groovy",
    [PMM_LANG_ERLANG] = "Erlang",
    [PMM_LANG_R] = "R",
    [PMM_LANG_HTML] = "HTML",
    [PMM_LANG_CSS] = "CSS",
    [PMM_LANG_SCSS] = "SCSS",
    [PMM_LANG_YAML] = "YAML",
    [PMM_LANG_TOML] = "TOML",
    [PMM_LANG_HCL] = "HCL",
    [PMM_LANG_SQL] = "SQL",
    [PMM_LANG_DOCKERFILE] = "Dockerfile",
    [PMM_LANG_CLOJURE] = "Clojure",
    [PMM_LANG_FSHARP] = "F#",
    [PMM_LANG_JULIA] = "Julia",
    [PMM_LANG_VIMSCRIPT] = "VimScript",
    [PMM_LANG_NIX] = "Nix",
    [PMM_LANG_COMMONLISP] = "Common Lisp",
    [PMM_LANG_ELM] = "Elm",
    [PMM_LANG_FORTRAN] = "Fortran",
    [PMM_LANG_CUDA] = "CUDA",
    [PMM_LANG_COBOL] = "COBOL",
    [PMM_LANG_VERILOG] = "Verilog",
    [PMM_LANG_EMACSLISP] = "Emacs Lisp",
    [PMM_LANG_JSON] = "JSON",
    [PMM_LANG_XML] = "XML",
    [PMM_LANG_MARKDOWN] = "Markdown",
    [PMM_LANG_MAKEFILE] = "Makefile",
    [PMM_LANG_CMAKE] = "CMake",
    [PMM_LANG_PROTOBUF] = "Protobuf",
    [PMM_LANG_GRAPHQL] = "GraphQL",
    [PMM_LANG_VUE] = "Vue",
    [PMM_LANG_SVELTE] = "Svelte",
    [PMM_LANG_MESON] = "Meson",
    [PMM_LANG_GLSL] = "GLSL",
    [PMM_LANG_INI] = "INI",
    [PMM_LANG_MATLAB] = "MATLAB",
    [PMM_LANG_LEAN] = "Lean",
    [PMM_LANG_FORM] = "FORM",
    [PMM_LANG_MAGMA] = "Magma",
    [PMM_LANG_WOLFRAM] = "Wolfram",
    [PMM_LANG_KUSTOMIZE] = "Kustomize",
    [PMM_LANG_K8S] = "Kubernetes",
    [PMM_LANG_PINE] = "PineScript",
    [PMM_LANG_SOLIDITY] = "Solidity",
    [PMM_LANG_TYPST] = "Typst",
    [PMM_LANG_GDSCRIPT] = "GDScript",
    [PMM_LANG_GLEAM] = "Gleam",
    [PMM_LANG_POWERSHELL] = "PowerShell",
    [PMM_LANG_PASCAL] = "Pascal",
    [PMM_LANG_DLANG] = "D",
    [PMM_LANG_NIM] = "Nim",
    [PMM_LANG_SCHEME] = "Scheme",
    [PMM_LANG_FENNEL] = "Fennel",
    [PMM_LANG_FISH] = "Fish",
    [PMM_LANG_AWK] = "AWK",
    [PMM_LANG_ZSH] = "Zsh",
    [PMM_LANG_TCL] = "Tcl",
    [PMM_LANG_ADA] = "Ada",
    [PMM_LANG_AGDA] = "Agda",
    [PMM_LANG_RACKET] = "Racket",
    [PMM_LANG_ODIN] = "Odin",
    [PMM_LANG_RESCRIPT] = "ReScript",
    [PMM_LANG_PURESCRIPT] = "PureScript",
    [PMM_LANG_NICKEL] = "Nickel",
    [PMM_LANG_CRYSTAL] = "Crystal",
    [PMM_LANG_TEAL] = "Teal",
    [PMM_LANG_HARE] = "Hare",
    [PMM_LANG_PONY] = "Pony",
    [PMM_LANG_LUAU] = "Luau",
    [PMM_LANG_QML] = "QML",
    [PMM_LANG_CFSCRIPT] = "CFML",
    [PMM_LANG_CFML] = "CFML",
    [PMM_LANG_JANET] = "Janet",
    [PMM_LANG_SWAY] = "Sway",
    [PMM_LANG_NASM] = "NASM",
    [PMM_LANG_ASSEMBLY] = "Assembly",
    [PMM_LANG_ASTRO] = "Astro",
    [PMM_LANG_BLADE] = "Blade",
    [PMM_LANG_JUST] = "Just",
    [PMM_LANG_GOTEMPLATE] = "Go Template",
    [PMM_LANG_TEMPL] = "Templ",
    [PMM_LANG_LIQUID] = "Liquid",
    [PMM_LANG_JINJA2] = "Jinja2",
    [PMM_LANG_PRISMA] = "Prisma",
    [PMM_LANG_HYPRLANG] = "Hyprlang",
    [PMM_LANG_DOTENV] = "DotEnv",
    [PMM_LANG_SYSTEMVERILOG] = "SystemVerilog",
    [PMM_LANG_DIFF] = "Diff",
    [PMM_LANG_WGSL] = "WGSL",
    [PMM_LANG_KDL] = "KDL",
    [PMM_LANG_JSON5] = "JSON5",
    [PMM_LANG_JSONNET] = "Jsonnet",
    [PMM_LANG_RON] = "RON",
    [PMM_LANG_THRIFT] = "Thrift",
    [PMM_LANG_CAPNP] = "Cap'n Proto",
    [PMM_LANG_PROPERTIES] = "Properties",
    [PMM_LANG_SSHCONFIG] = "SSH Config",
    [PMM_LANG_BIBTEX] = "BibTeX",
    [PMM_LANG_STARLARK] = "Starlark",
    [PMM_LANG_BICEP] = "Bicep",
    [PMM_LANG_CSV] = "CSV",
    [PMM_LANG_REQUIREMENTS] = "Requirements",
    [PMM_LANG_HLSL] = "HLSL",
    [PMM_LANG_VHDL] = "VHDL",
    [PMM_LANG_DEVICETREE] = "DeviceTree",
    [PMM_LANG_LINKERSCRIPT] = "Linker Script",
    [PMM_LANG_GN] = "GN",
    [PMM_LANG_KCONFIG] = "Kconfig",
    [PMM_LANG_BITBAKE] = "BitBake",
    [PMM_LANG_SMALI] = "Smali",
    [PMM_LANG_TABLEGEN] = "TableGen",
    [PMM_LANG_ISPC] = "ISPC",
    [PMM_LANG_CAIRO] = "Cairo",
    [PMM_LANG_MOVE] = "Move",
    [PMM_LANG_SQUIRREL] = "Squirrel",
    [PMM_LANG_FUNC] = "FunC",
    [PMM_LANG_REGEX] = "Regex",
    [PMM_LANG_JSDOC] = "JSDoc",
    [PMM_LANG_RST] = "reStructuredText",
    [PMM_LANG_BEANCOUNT] = "Beancount",
    [PMM_LANG_MERMAID] = "Mermaid",
    [PMM_LANG_PUPPET] = "Puppet",
    [PMM_LANG_PO] = "PO",
    [PMM_LANG_GITATTRIBUTES] = "gitattributes",
    [PMM_LANG_GITIGNORE] = "gitignore",
    [PMM_LANG_SLANG] = "Slang",
    [PMM_LANG_LLVM_IR] = "LLVM IR",
    [PMM_LANG_SMITHY] = "Smithy",
    [PMM_LANG_WIT] = "WIT",
    [PMM_LANG_TLAPLUS] = "TLA+",
    [PMM_LANG_PKL] = "Pkl",
    [PMM_LANG_GOMOD] = "Go Mod",
    [PMM_LANG_APEX] = "Apex",
    [PMM_LANG_SOQL] = "SOQL",
    [PMM_LANG_SOSL] = "SOSL",
    [PMM_LANG_MOJO] = "Mojo",
    [PMM_LANG_OBJECTSCRIPT_UDL] = "ObjectScript UDL",
    [PMM_LANG_OBJECTSCRIPT_ROUTINE] = "ObjectScript Routine",
    [PMM_LANG_OBJECTSCRIPT_EXPORT] = "ObjectScript Export XML",

};

/* ── Public API ──────────────────────────────────────────────────── */

CBMLanguage pmm_language_for_extension(const char *ext) {
    if (!ext || !ext[0]) {
        return PMM_LANG_COUNT;
    }

    /* Check user-defined overrides first */
    const pmm_userconfig_t *ucfg = pmm_get_user_lang_config();
    if (ucfg) {
        CBMLanguage ulang = pmm_userconfig_lookup(ucfg, ext);
        if (ulang != PMM_LANG_COUNT) {
            return ulang;
        }
    }

    for (size_t i = 0; i < EXT_TABLE_SIZE; i++) {
        if (strcmp(EXT_TABLE[i].ext, ext) == 0) {
            return EXT_TABLE[i].language;
        }
    }
    return PMM_LANG_COUNT;
}

CBMLanguage pmm_language_for_filename(const char *filename) {
    if (!filename || !filename[0]) {
        return PMM_LANG_COUNT;
    }

    /* Check special filenames first */
    for (size_t i = 0; i < FILENAME_TABLE_SIZE; i++) {
        if (strcmp(FILENAME_TABLE[i].filename, filename) == 0) {
            return FILENAME_TABLE[i].language;
        }
    }

    /* DotEnv variant filenames (".env.local", ".env.production", …): the
     * filename starts with ".env." but its last "extension" (e.g. ".local")
     * is not a real language extension.  Match the dotenv convention used by
     * pass_envscan/pass_infrascan (".env" exact, ".env." prefix, "*.env"
     * suffix) so file-index routing agrees with direct extraction. */
    if (strncmp(filename, ".env.", SLEN(".env.")) == 0) {
        return PMM_LANG_DOTENV;
    }

    /* Fall back to extension-based lookup.
     * For compound extensions (e.g. ".blade.php") defined in the user config,
     * scan from the first dot in the basename toward the last, checking user
     * config at each position.  Built-in extensions use the last dot only. */
    const char *last_dot = strrchr(filename, '.');
    if (!last_dot) {
        return PMM_LANG_COUNT;
    }

    /* Probe compound extensions (e.g. ".blade.php") from the first dot toward
     * the last. Built-in compounds are checked first so e.g. Laravel Blade
     * templates map to Blade rather than the single-extension fallback (PHP);
     * user config can still add more (#258). */
    static const struct {
        const char *ext;
        CBMLanguage lang;
    } COMPOUND_EXT_TABLE[] = {
        {".blade.php", PMM_LANG_BLADE},
    };
    const pmm_userconfig_t *ucfg = pmm_get_user_lang_config();
    const char *p = strchr(filename, '.');
    while (p && p < last_dot) {
        for (size_t i = 0; i < sizeof(COMPOUND_EXT_TABLE) / sizeof(COMPOUND_EXT_TABLE[0]); i++) {
            if (strcmp(p, COMPOUND_EXT_TABLE[i].ext) == 0) {
                return COMPOUND_EXT_TABLE[i].lang;
            }
        }
        if (ucfg) {
            CBMLanguage lang = pmm_userconfig_lookup(ucfg, p);
            if (lang != PMM_LANG_COUNT) {
                return lang;
            }
        }
        p = strchr(p + SKIP_ONE, '.');
    }

    /* Standard single-extension lookup (built-ins + user overrides). */
    return pmm_language_for_extension(last_dot);
}

const char *pmm_language_name(CBMLanguage lang) {
    if (lang < 0 || lang >= PMM_LANG_COUNT) {
        return "Unknown";
    }
    return LANG_NAMES[lang] ? LANG_NAMES[lang] : "Unknown";
}

/* ── .m file disambiguation ──────────────────────────────────────── */

/* Simple substring search helper */
static bool str_contains(const char *haystack, const char *needle) {
    return strstr(haystack, needle) != NULL;
}

static bool has_objc_markers(const char *buf) {
    return str_contains(buf, "@interface") || str_contains(buf, "@implementation") ||
           str_contains(buf, "@protocol") || str_contains(buf, "@property") ||
           str_contains(buf, "#import") || str_contains(buf, "@selector") ||
           str_contains(buf, "@encode") || str_contains(buf, "@synthesize") ||
           str_contains(buf, "@dynamic");
}

static bool has_magma_end_markers(const char *buf) {
    return str_contains(buf, "end function;") || str_contains(buf, "end procedure;") ||
           str_contains(buf, "end intrinsic;") || str_contains(buf, "end if;") ||
           str_contains(buf, "end for;") || str_contains(buf, "end while;");
}

/* Check for "intrinsic Name(" or "procedure Name(" patterns. */
static bool has_magma_callable_pattern(const char *buf) {
    const char *markers[] = {"intrinsic ", "procedure "};
    for (int i = 0; i < LANG_SCAN_PASSES; i++) {
        const char *p = strstr(buf, markers[i]);
        if (!p) {
            continue;
        }
        p += strlen(markers[i]);
        while (*p && isalpha((unsigned char)*p)) {
            p++;
        }
        if (*p == '(') {
            return true;
        }
    }
    return false;
}

/* Scan lines for MATLAB-specific markers (function/classdef/%%). */
static bool has_matlab_line_markers(const char *buf) {
    const char *line = buf;
    while (*line) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (strncmp(p, "function ", SLEN("function ")) == 0 ||
            strncmp(p, "function\t", SLEN("function\t")) == 0 ||
            strncmp(p, "classdef ", SLEN("classdef ")) == 0 ||
            strncmp(p, "classdef\t", SLEN("classdef\t")) == 0 || strncmp(p, "%%", PAIR_LEN) == 0 ||
            (*p == '%' && *(p + SKIP_ONE) != '{')) {
            return true;
        }
        const char *nl = strchr(line, '\n');
        if (!nl) {
            break;
        }
        line = nl + SKIP_ONE;
    }
    return false;
}

CBMLanguage pmm_disambiguate_m(const char *path) {
    if (!path) {
        return PMM_LANG_MATLAB;
    }

    FILE *f = pmm_fopen(path, "r");
    if (!f) {
        return PMM_LANG_MATLAB;
    }

    /* Read first 4KB */
    char buf[PMM_SZ_4K + SKIP_ONE];
    size_t n = fread(buf, SKIP_ONE, PMM_SZ_4K, f);
    buf[n] = '\0';
    (void)fclose(f);

    if (has_objc_markers(buf)) {
        return PMM_LANG_OBJC;
    }
    if (has_magma_end_markers(buf)) {
        return PMM_LANG_MAGMA;
    }
    if ((str_contains(buf, "intrinsic ") || str_contains(buf, "procedure ")) &&
        has_magma_callable_pattern(buf)) {
        return PMM_LANG_MAGMA;
    }
    if (has_matlab_line_markers(buf)) {
        return PMM_LANG_MATLAB;
    }

    return PMM_LANG_MATLAB;
}

/* Disambiguate .cls files: shared by InterSystems ObjectScript UDL and
 * Salesforce Apex. ObjectScript class files begin with a line of the form
 * "Class <UppercasePackage>...". Defaults to Apex on any doubt. */
CBMLanguage pmm_disambiguate_cls(const char *path) {
    if (!path) {
        return PMM_LANG_APEX;
    }

    FILE *f = pmm_fopen(path, "r");
    if (!f) {
        return PMM_LANG_APEX;
    }

    char buf[PMM_SZ_4K + SKIP_ONE];
    size_t n = fread(buf, SKIP_ONE, PMM_SZ_4K, f);
    buf[n] = '\0';
    (void)fclose(f);

    const char *line = buf;
    while (*line) {
        if (strncmp(line, "Class ", SLEN("Class ")) == 0 &&
            isupper((unsigned char)line[SLEN("Class ")])) {
            return PMM_LANG_OBJECTSCRIPT_UDL;
        }
        const char *nl = strchr(line, '\n');
        if (!nl) {
            break;
        }
        line = nl + SKIP_ONE;
    }
    return PMM_LANG_APEX;
}

/* Disambiguate .inc files: shared by BitBake include fragments and
 * InterSystems ObjectScript include (macro) files. ObjectScript .inc files are
 * predominantly macro definitions ("#define NAME ..." / "#def1arg NAME ...");
 * some also carry a "ROUTINE <Name>" header. The macro-preprocessor directives
 * are the strongest signal because that is the primary content of an .inc file,
 * whereas BitBake uses '#' only for "# comment" lines (always '#' + space).
 * We therefore match ObjectScript preprocessor directives ('#' immediately
 * followed by 'def'/';'), which BitBake never produces. Defaults to BitBake on
 * any doubt (preserves existing behaviour). */
CBMLanguage pmm_disambiguate_inc(const char *path) {
    if (!path) {
        return PMM_LANG_BITBAKE;
    }

    FILE *f = pmm_fopen(path, "r");
    if (!f) {
        return PMM_LANG_BITBAKE;
    }

    char buf[PMM_SZ_4K + SKIP_ONE];
    size_t n = fread(buf, SKIP_ONE, PMM_SZ_4K, f);
    buf[n] = '\0';
    (void)fclose(f);

    const char *line = buf;
    while (*line) {
        /* ObjectScript include header: a line beginning "ROUTINE <Uppercase>". */
        if (strncmp(line, "ROUTINE ", SLEN("ROUTINE ")) == 0 &&
            isupper((unsigned char)line[SLEN("ROUTINE ")])) {
            return PMM_LANG_OBJECTSCRIPT_ROUTINE;
        }
        /* ObjectScript macro directives — the primary content of .inc files.
         * "#define"/"#def1arg" (macro defs) and "#;" (line comment). BitBake's
         * only '#' use is "# comment" (hash + space), so these never collide. */
        if (strncmp(line, "#define", SLEN("#define")) == 0 ||
            strncmp(line, "#def1arg", SLEN("#def1arg")) == 0 ||
            strncmp(line, "#;", SLEN("#;")) == 0) {
            return PMM_LANG_OBJECTSCRIPT_ROUTINE;
        }
        const char *nl = strchr(line, '\n');
        if (!nl) {
            break;
        }
        line = nl + SKIP_ONE;
    }
    return PMM_LANG_BITBAKE;
}
