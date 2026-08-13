# Coding standards

**Project:** a single-player Jedi Outcast / Jedi Academy engine on Vulkan 1.3
**Status:** binding. Changes go through discussion and an edit to this document.

This describes how we write code. It is shorter than we would like, because a rule nobody remembers is not a rule. Anything a machine can check is checked by a machine (`clang-format`, `clang-tidy`, CI) and is not repeated here.

This document is in English. The working documents - the backlog, the roadmaps, the phase reports - are in Russian and live in the project rather than in this repository. This one is the exception because it is the only document the build depends on: five CI scripts, the root README, `ci.yml` and `CMakeLists.txt` name it, and rules 1, 6.1 and 12.2 are enforced. A rule that is enforced should be readable by everyone who has to read the code.

---

## 1. Language - code is Latin-only

**All code and all comments on it are Latin script only. Cyrillic is allowed in documentation and nowhere else.**

This is rule number one and it has no exceptions.

"Code" means everything under `code/`, `games/`, `shared/`, `tools/`, `third_party/`: sources, headers, shaders, CMake, build scripts, CI configuration, file and directory names, `git commit` messages, the contents of string literals, cvar and console command names, `assert` text, and Vulkan object names passed to `VK_SET_OBJECT_NAME`.

"Documentation" means `docs/**`, `*.md` in the root of the repository, issue and pull request text, design documents, decision records.

```cpp
// GOOD
// Bone hierarchy is evaluated lazily: only bones referenced by visible
// surfaces of the selected LOD are transformed.
static void G2_TransformBone(int child, CBoneCache& bones);

// BAD - Cyrillic in a comment
// Иерархия костей считается лениво
static void G2_TransformBone(int child, CBoneCache& bones);
```

**Why.** Encodings in a C++ toolchain are still a source of pain: MSVC without `/utf-8` reads a source file in the system code page, `#pragma execution_character_set` has a life of its own, and debuggers and profilers regularly mangle non-ASCII in symbol names. On top of that, `grep`/`rg` over the code stops being predictable, diffs get noisy, and bringing in an outside contributor to code with Cyrillic comments is close to impossible. None of this applies to documentation, where Cyrillic is fine and appropriate.

**Player-visible strings are not hard-coded at all.** They live in `.str` asset files and go through `SE_GetString()`. That is a separate rule (§6.5), but it removes 90% of the temptation to write Cyrillic in a `.cpp`.

**Sources are UTF-8 without BOM.** `/utf-8` for MSVC and `-finput-charset=UTF-8 -fexec-charset=UTF-8` for GCC/Clang are set globally in CMake. The non-ASCII check is `tools/ci/check_ascii.py` and it fails on the first violation.

---

## 2. Language standard and subset

**C++20.** `CMAKE_CXX_STANDARD 20`, `CXX_STANDARD_REQUIRED ON`, `CXX_EXTENSIONS OFF`, `/std:c++20` for MSVC.

### We use

`std::span`, `std::string_view`, `std::array`, `std::optional`, `std::bit_cast`, designated initializers (particularly useful for `VkXxxCreateInfo`), `constexpr`/`consteval`, `if constexpr`, `[[nodiscard]]`, `[[likely]]`/`[[unlikely]]` on hot paths, structured bindings, `enum class`, concepts for templated RHI helpers, `<format>` (or `fmt`, if `<format>` turns out to be patchy on the target compilers).

### We do not use

| What | Why |
|---|---|
| **Exceptions** | We compile with `-fno-exceptions` / `/EHs-c-`. The legacy `throw int` in `Com_Error` goes in phase 3 (see §7) |
| **RTTI** | `-fno-rtti`. `dynamic_cast` and `typeid` are not needed |
| **`std::shared_ptr`** | Ownership must be explicit and unambiguous. `std::unique_ptr` is fine, but on hot paths prefer arenas and indices |
| **`std::function` on a hot path** | An allocation plus an unpredictable call. In the job system and the render graph: raw function pointers plus a `void*` context, or templated lambdas by value |
| **Virtual calls in a loop over data** | See `CParticle::Update()` - 1200 virtual calls per frame. Replace with a switch on a tag or an SoA pass |
| **`std::regex`, `std::iostream`, `std::locale`** | Bloat the binary, drag in locales, slow |
| **Multiple inheritance other than pure interfaces** | - |
| **Macros where `constexpr` or a template would do** | Exception: macros for generated shader code (`global.h`) and for platform `#ifdef` |
| **`using namespace` in a header** | - |

### On the STL generally

We use the STL, with judgement. `std::vector` for level data is fine. `std::vector` inside a struct built thousands of times per frame is not. `std::map`/`std::unordered_map` during loading is fine, in a frame is not - use open addressing, or a flat array with a linear scan when N < 32.

### Raven's own containers

`Ratl`, `Ravl`, `Ragl`, `Rufl` are on the way out, but **not first**. The order:

1. `Ravl` (`CVec3`/`CMatrix`) -> `q_math` and POD types.
2. `Ratl` (`vector_vs`, `map_vs`, `pool_vs` - static capacity as a template parameter) -> `Q::LimitedVector` from `shared/qcommon/safe/limited_vector.h`, which already exists and has tests, and `std::array`. Start with `tr_WorldEffects.cpp`: it is the one place where the renderer depends on a gamecode tree.
3. `Rufl::hstring` competes with `qcommon/hstring.cpp` - two string interning implementations, one of which goes.
4. `Ragl` (the navigation graph) gets rewritten together with `g_navigator`. A separate project, low priority.

`ratl_common.h` defines `operator new(size_t, TRatlNew*)` **in the global namespace**, which pollutes every translation unit any Ratl header reaches. While Ratl lives, its headers must not reach the renderer.

---

## 3. Naming and formatting

Formatting is defined by the `.clang-format` in the root. Arguing about it in review is not allowed - edit the file or say nothing.

```
Indent         4 spaces, no tabs (legacy files are converted on first touch)
Width          120 columns
Braces         Allman for functions and types, K&R for control flow
Pointers       Type* name  (star binds to the type)
```

| Entity | Style | Example |
|---|---|---|
| Types, classes, structs | `PascalCase` | `RenderGraph`, `BoneCache`, `MediumParams` |
| Functions and methods | `camelCase` | `buildWorldVbo()`, `evalRender()` |
| Free functions in the renderer's public API | `R_PascalCase` | `R_PostFx_SetParams()`, `R_AddDecal()` |
| Variables, parameters | `camelCase` | `frameIndex`, `boneCount` |
| Class members | `m_camelCase` | `m_boneCount` |
| Globals (there should be few) | `g_camelCase` | `g_levelArena` |
| File-scope statics | `s_camelCase` | `s_pipelineCache` |
| Constants, `constexpr` | `kPascalCase` | `kMaxBones`, `kFramesInFlight` |
| Macros (minimal) | `UPPER_SNAKE` | `VK_CHECK` |
| `enum class` and its values | `PascalCase` | `PostFxSlot::Underwater` |
| Files | `snake_case.cpp/.h` | `render_graph.cpp`, `vk_device.cpp` |
| Namespaces | `lowercase` | `namespace render`, `namespace jobs` |

**We do not mass-rename legacy identifiers.** `tr_`, `RB_`, `RE_`, `G2API_`, `qhandle_t`, `vec3_t` stay as they are: they are recognisable, they are searchable, and they connect us to twenty years of mod documentation. New entities follow the table above; old ones are brought into line **only** when the file is being rewritten anyway.

**Raven's Hungarian notation** (`iSize`, `psFilename`, `bZeroit`, `pvAddress`, `gbUsingCachedMapDataRightNow`) is forbidden in new code and left alone in old code until that file is rewritten.

---

## 4. Memory

These rules reflect what we are fixing in this codebase, not abstract theory.

### 4.1 Lifetimes are explicit

```cpp
Arena  g_permanent;    // the life of the process
Arena  g_level;        // reset on map change
Arena  g_frame[2];     // per frame, double buffered
```

Every allocation belongs to one of them. `Hunk_*` gets its meaning back: `g_level.reset()` is O(1), not a linear walk over a global list the way `Z_TagFree` is today.

### 4.2 Zero allocations in a frame

On the hot path - everything that runs every frame - `new`, `malloc`, a growing `std::vector::push_back` and any `std::string` are forbidden. Only `g_frame[i]`, a bump allocator, and pools allocated up front.

This is not aesthetics. `new CBoneCache` the first time a model appears, `new CTrail` every 2 ms for a sabre, 1200 separate `new` calls for particles - these are specific, measurable problems in this codebase.

### 4.3 Alignment, seriously

An allocator must honour the alignment it was asked for. In the legacy `Z_Malloc(int iSize, ..., int iAlign)` the parameter is marked `/*unusedAlign*/` and ignored, while `-msse2` is on globally. That is a mine. The new allocator is `alignas`-correct by default at 16 bytes, with an explicit parameter for more.

### 4.4 Ownership

- A raw pointer **does not own**. Always.
- Ownership is a `unique_ptr`, a value, or an explicit arena.
- Handles rather than pointers where the object lives in a pool: `struct TextureHandle { uint32_t index; uint32_t generation; };` - the generation catches use-after-free without ASan.

### 4.5 No raw struct dumps to files

Today a save game is a `memcpy` of `gentity_t`/`gclient_t` into a file. Hence: 32/64-bit incompatibility, breakage from any layout change, and addresses leaking into the file - `EnumerateField` writes `*(int*)pv` into an 8-byte pointer field, so the top four bytes go to disk.

All serialisation is explicit, field by field, with a format version in the header. The save format is versioned **before** the first change to a game struct.

---

## 5. Errors and failure

### 5.1 There are no exceptions

`-fno-exceptions`. The legacy `Com_Error` does `throw code;` and catches it in `Com_Frame` with `catch (int)`, while `Com_Error` is exported to the gamecode and the renderer - so an exception unwinds C-like frames of another module with no RAII anywhere. In a monolith that is already safer, but the model still changes.

The target:

```cpp
[[noreturn]] void Sys_FatalError(const char* fmt, ...);   // does not return; logs and dies
enum class LoadResult { Ok, NotFound, Corrupt, OutOfMemory };
```

- A **programming error** - an invariant we control is broken - is an `assert` in debug and a UB contract in release. Not checked in release.
- A **data error** - a corrupt BSP, a missing texture, a malformed `.shader` - is **always checked, including in release**, returned as a code, and logged with the file path and offset. Never an `assert`.
- A **fatal environment failure** - no Vulkan device, out of memory - is `Sys_FatalError`.

### 5.2 Validating external data is not up for discussion

Everything that came from a file or from the network is hostile. This is not paranoia: the audit found 15 defects of the form "an index read from the BSP is used without a bounds check", including a stack overflow in `tr_bsp.cpp:489` that gives remote code execution through a custom map.

The rule: **every field read from a file and used as a size, an offset or an index is checked where it is read.** Not "somewhere later", not "it is valid anyway". Helpers:

```cpp
class Reader {                       // reads from a buffer, with bounds
public:
    bool read(void* dst, size_t n);
    template <typename T> bool read(T& out);
    bool seek(size_t offset);
    [[nodiscard]] bool ok() const;   // sticky error flag
};
bool checkLump(const lump_t& l, size_t fileSize, size_t elemSize, int maxCount);
```

Format loaders - BSP, MD3, GLM, GLA, TGA, ROFF, RoQ, saves - must have a fuzz target in `tests/fuzz/`. A new loader without a fuzzer does not merge.

### 5.3 Logging

One channel, levels `Trace/Debug/Info/Warn/Error`, categories (`Render`, `IO`, `Ghoul2`, `Fx`, ...). `Com_Printf` stays for compatibility, but new code goes through the categorised logger. Formatting is `<format>`, not `va()`.

**`va()` is forbidden in new code.** Four static buffers with `index++ & 3` is guaranteed silent string corruption on nested calls. The last upstream commit at the time of the audit (`1a6a643`, "Fix spawn item error va eval") is about exactly this. There are 608 calls; they go as files are touched.

---

## 6. Architecture

### 6.1 Layers and the direction of dependencies

```
game  ──►  render  ──►  engine  ──►  platform
 └───────────────────►  engine
```

The layers, as `tools/ci/check_layering.py` defines them:

| Layer | Directories |
|---|---|
| `platform` | `shared/sys`, `shared/sdl` |
| `engine` | `code/qcommon`, `code/server`, `code/client`, `shared/qcommon` |
| `api` | `code/api` |
| `render` | `code/rd-vulkan`, `code/rd-common` |
| `game` | `games/jka`, `games/jk2`, `code/ui` |

- `render` **does not include** a header from `game`. Not one.
- `engine` does not know about `render` or `game`.
- `game` reaches the renderer through `namespace render`, a flat set of free functions in `render/api.h`. No access to `tr.`, `backEnd.`, `glState`, `tess`.

The DLL boundary is gone, and that is a temptation. The logical boundary stays: without it, in six months cgame will be reaching into the renderer's internals and refactoring becomes impossible. A dependency that points the wrong way blocks review.

`check_layering.py` carries the violations we inherited in `tools/ci/layering-baseline.txt`. That file may shrink and may not grow, and every entry must name the phase that removes it.

### 6.2 Gamecode states intent, the renderer decides how

Gamecode says: "this entity is disintegrating, t = 0.4, entry point P", "the player is underwater", "this sabre has two blades, blue, length 32". The renderer decides how many passes, which render target, blob or stencil, CPU or compute.

The sign of a violation is gamecode choosing a render target size (`cg_players.cpp:4126`), drawing fullscreen quads (`CG_FillRect`), or assembling `polyVert_t` by hand (`cg_marks.cpp`). All of that moves.

### 6.3 Data instead of branches

Three `else if` on `CONTENTS_WATER/SLIME/LAVA` with hard-coded colours become a `MediumParams` table. Six copied fade state machines over 12 globals become one ADSR envelope plus data. This is not refactoring for beauty: 340 lines of `CG_Draw2DScreenTints` collapse to 40.

### 6.4 No hidden state inside functions

`static` inside a function or a method is forbidden, except for `constexpr` tables. The reason is specific: `CBezier::DrawSegment()` keeps `static vec3_t lastEnd[2]` inside a method, so two simultaneous beziers are guaranteed to corrupt each other's seam. The ragdoll keeps all its state in file-scope statics (`static mdxaBone_t ragBones[256]`, `static int numRags`), which blocks any multithreading of animation.

### 6.5 Player-visible strings

Not hard-coded. They live in `.str` assets and go through `SE_GetString()`. This also removes the temptation to break §1.

### 6.6 Limits do not fail silently

Today an overflow of `MAX_DRAWSURFS` loses surfaces silently; `MAX_RENDER_COMMANDS` silently drops commands; `Pass::maxDrawItems` asserts in debug and **returns silently in release**; `MAX_POLYS` drops with the comment "happens a lot with high fighting scenes".

The rule: a limit overflow is either handled properly - growth, LRU eviction - or logged at `Warn` **once per map**. Never silently.

---

## 7. Working with the legacy

The codebase is 24 years old. Half of it is working Raven code that nobody understands in full. So:

### 7.1 The boy scout rule, with a limit

If you touch a function, tidy it: names, `const`, array bounds, drop `va()`, drop `static`. **But do not touch the neighbouring functions in the same file.** Diffs have to be reviewable; "tidied the file while I was in there" is a 2000-line diff that nobody will check.

### 7.2 Mechanical sweeps go in their own commits

`clang-format` over a file, renaming `qvk*` to `vk*`, replacing `ri.` with direct calls - each of those is its own commit with no substantive change inside it. Then `git blame` stays useful and a reviewer can check the commit by looking at the command that produced it.

### 7.3 Do not rewrite what works and is not in the way

The Ghoul2 maths (`G2_bones.cpp`, the skeletal hierarchy) has worked for 24 years and is not a bottleneck. The rend2/rd-vulkan delta against it is 206 lines out of 4889 - even the authors of modern renderers left it alone. So do we.

We rewrite what: (a) blocks the architecture, (b) is a measured bottleneck, (c) is a security problem.

### 7.4 Dead code goes immediately

`tr_arb.cpp`, `qglLockArraysEXT`, `code/mp3code`, `#if 0` blocks, `RB_CalcFogTexCoords` (no callers), `Diff_Burley` (written, never called - either wire it up or delete it). A comment reading `// TODO: Eh...resize?` is not documentation, it is a bug report: either fix it or open an issue and reference it by number.

`tools/ci/check_sources.py` asks two questions here: is there a source on disk that no CMake list builds, and is there a directory holding sources that no `CMakeLists.txt` names at all. The second one found `ui/`, a whole top-level directory, and through it the OpenAL and EAX path - 2,806 lines that only ever compiled under 32-bit MSVC.

### 7.5 Attribution

The whole tree is **GPLv2 without "or later"**. File headers are kept and added to, never replaced. The `vk_*.cpp` files came from Quake3e by way of EternalJK under an id/Raven header; when forking, restore the real chain: id Software -> Raven -> OpenJK contributors -> ec-/Quake3e -> kennyalive -> JKSunny -> us.

A practical consequence: public domain, MIT, MIT-0 and BSD are compatible with GPLv2 and can be vendored. **Apache-2.0 is not** - its patent clause is incompatible with GPLv2 specifically because we have no "or later".

---

## 8. Vulkan

### 8.1 Required

- **The validation layers are clean.** A CI job runs a scene on lavapipe with `VK_LAYER_KHRONOS_validation` plus sync validation. A new validator warning is a red CI. Without this rule, sync2 and dynamic rendering will entrench the existing hidden races rather than fix them.
- **Every object is named.** `VK_SET_OBJECT_NAME` through `VK_EXT_debug_utils`, not `debug_report`, which is deprecated. Unnamed objects in RenderDoc are a lost day of debugging.
- **Barriers only through the render graph.** A hand-written `vkCmdPipelineBarrier2` is acceptable only inside the graph itself and in resource loaders. No `vk_record_image_layout_transition(..., oldLayout)` where the caller "just knows" the layout - there are 32 such sites today.
- **Precise stage and access masks.** `VK_PIPELINE_STAGE_ALL_COMMANDS_BIT` only with a comment saying why nothing tighter will do. Today `vk_cubemap.cpp` passes `0, 0`, which the helper expands to `ALL_COMMANDS` - a direct pipeline stall.
- **No `vkQueueWaitIdle`/`vkDeviceWaitIdle` outside shutdown and resize.** Today `vk_end_command_buffer()` does a full stall on every one-shot operation: creating an attachment, uploading each texture, baking each cubemap.
- **A persistent pipeline cache** is required, validated against `pipelineCacheUUID`/`vendorID`/`deviceID`.
- **Lazy creation only outside a frame.** A lazy `vk_gen_pipeline()` mid-frame is a hitch; pipeline creation is warmed during loading.

### 8.2 Features and fallbacks

The baseline is Vulkan 1.3 core: dynamic rendering, synchronization2, descriptor indexing, timeline semaphores, `maintenance4`. We write no fallbacks for those.

Anything **not** core in 1.3 (`VK_EXT_extended_dynamic_state3`, `VK_EXT_vertex_input_dynamic_state`, mesh shaders, ray query) is optional, detected per feature, and must have a static fallback. The pipeline key is masked by the dynamic states actually supported, not by "we decided EDS3 is there".

Forbidden: degrading quality silently. Today, when `maxBoundDescriptorSets < 11`, **the whole of PBR switches off without a single message** (`vk_init.cpp:517`). Any degradation is logged at `Warn` and visible in `/vkinfo`.

### 8.3 Resources

One allocator: **VMA**. The three hand-written ones - a bump-only texture chunk allocator that never frees, manual attachment offsets, ad-hoc `vkAllocateMemory` for buffers - go.

Loader: **volk**. The 107 manual `PFN_vk*` pointers and the ~30k lines of vendored `vulkan/*.h` go.

---

## 9. Shaders

### 9.1 A shared C/GLSL header is the central technique

`shaders/global.h` compiles as both C++ and GLSL through switch macros. It came from the `pbr` branch and it is the best thing there:

```c
STRUCT (
    VEC4 ( ambientLight ) VEC4 ( directedLight )
    VEC4 ( localLightOrigin ) VEC4 ( localViewOrigin ) MAT4 ( modelMatrix )
, UniformEntity )
```

**Any struct that both C++ and a shader can see is declared only here.** Duplicating a UBO/SSBO layout in two places is forbidden - it is a whole class of bug that this removes completely. The rule covers push constants and bindless indices as well.

### 9.2 Specialization constants rather than `#define` permutations

Permutations are a last resort. Today 8 axes produce 600 SPIR-V blobs and 61.7 MB of generated C in git. The target is around 25 blobs.

An axis stays a permutation only if it changes the **set of inputs** (the vertex input layout) or creates unacceptable register pressure. Everything else is a spec constant. Document the decision in a comment next to the axis.

### 9.3 Toolchain

`glslc` (shaderc), wired into CMake through `add_custom_command` plus `DEPFILE` so that dependencies on `global.h` and `common/*.glsl` are picked up automatically. `--target-env=vulkan1.3`.

**Generated files are not committed.** Not `shader_data.c`, not `shader_binding.c`, not `.spv`, not `.exe` tools. Everything is built. This is not style: 61.7 MB and 1.9 million lines break `git bisect`, `git blame` and build time.

### 9.4 Other

- GLSL 4.50, `#extension GL_GOOGLE_include_directive`.
- Naming in shaders follows C++ (§3), so `global.h` reads the same from both sides.
- `VkShaderModule` objects are created lazily on first use, not 572 of them at startup.
- No runtime concatenation of defines into a `char extradefines[1200]` - that is rend2's approach and we are not taking it.

---

## 10. Performance

### 10.1 Measure first

An optimisation without a measurement does not merge. In the repository:

- a built-in CPU profiler with zones (`PROFILE_ZONE("R_AddWorldSurfaces")`), output to Tracy or our own overlay;
- GPU timers on each render graph node;
- a `bench` mode: a deterministic run of a recorded demo with frame times in CSV.

A performance regression on the reference scenes is a red CI, the same as a failing test.

### 10.2 What counts as a hot path

Everything that runs per frame: frontend scene assembly, culling, sorting, command recording, bone updates, particle simulation, FX updates. §4.2 (zero allocations) and §2 (no `std::function`, no virtual calls in loops) apply strictly here, and loosely in loading code.

### 10.3 Data-oriented where it pays

Not a dogma. Apply it to what is processed in batches: particles (SoA, GPU), bones, drawsurfs, decals, light clusters. Do not apply it to what exists as a single instance or is handled one at a time: cvars, config, UI widgets, game entities.

The reference point is a specific anti-pattern in this codebase: `CEffect` holds an entire `refEntity_t` (200+ bytes) inline and is allocated with its own `new`; 1200 such objects, scattered across the heap, are updated every frame through a virtual call. That is the worst possible layout.

---

## 11. Tests

The project has 222 lines of tests against roughly 500 kLOC. The bar is low; we raise it in specific places rather than "covering everything".

**Tests are required for:**

| What | Kind |
|---|---|
| Every format loader | fuzz target (libFuzzer) plus a corpus of real assets |
| Save serialisation | round trip: write, read, compare field by field |
| Maths (`q_math`, quaternions, bone decompression) | unit |
| Allocators, arenas, pools | unit plus ASan |
| Job system | unit plus TSan |
| Render graph: topological sort, barrier computation, aliasing | unit, no GPU |
| The `.shader`/`.mtr` parser | unit over a set of real shaders |

**The regression harness** is this project's main quality instrument: an automated run of `t1_sour` to `t3_bounty` (JKA) and `kejim_post` to `doom` (JK2) with a save and load at each level, collecting crashes, and screenshots at checkpoints for pixel comparison against an `rdsp-vanilla` reference. It runs overnight.

The gates on a pull request: build on Windows and Linux, `-Wextra` with no new warnings, ASan plus UBSan unit tests, the Vulkan validation layers on lavapipe, `clang-format --dry-run --Werror`, and the policy gates in §14.

---

## 12. Git

### 12.1 Commits

```
<scope>: <imperative summary, <= 72 chars, English>

<body: why, not what. Reference issues by number.>
```

`scope` is one of: `vk`, `rg`, `render`, `g2`, `fx`, `engine`, `jobs`, `io`, `game`, `cg`, `build`, `ci`, `docs`, `shaders`.

```
vk: replace chunk allocator with VMA

The old allocator was bump-only and never reused freed sub-allocations,
so texture memory grew monotonically until map change. Fixes #42.
```

One commit, one change. Mechanical edits - formatting, renames - stay separate from substantive ones (§7.2).

### 12.2 What a commit message must not contain

**A link to a session, a chat, or any other conversation the change came out of.** Not in the body and not in a trailer.

The reason is not privacy, it is shelf life. A commit message is the only explanation that reaches the person reading `git log` three years from now: it is versioned with the code, it survives a change of hosting, and it works with no network. A link has none of those properties. It rots silently and leaves behind a promise of an explanation where the explanation was supposed to be written.

So the rule is simple: if something from the conversation needs to be known, **write it into the message** rather than pointing at it. If there is nothing to write, the link is not needed either.

`Co-Authored-By` stays: that is attribution, not a pointer.

Checked by `tools/ci/check_commits.py` in the `policy` stage.

### 12.3 Branches

`main` always builds and passes the regression run. Work happens in `feature/<scope>-<short>` and merges through a reviewed pull request. Long-lived branches rebase onto `main` weekly - otherwise we repeat the history of `prototype-pbr-bindless`, which fell a year behind `pbr` and became unusable.

### 12.4 What we do not commit

Generated files (`shader_data.c`, `.spv`), binaries (`.exe`, `.dll`, `.lib`), build artefacts, `data.spv` and similar rubbish, and vendored dependencies that a package manager or `FetchContent` can supply.

Exception: `third_party/` for header-only and small libraries (VMA, volk, stb, MikkTSpace), with a pinned version and an entry in `third_party/README.md`.

---

## 13. Review checklist

A reviewer must check these and may block on any of them:

1. **No Cyrillic in code or comments.** §1
2. Dependencies point the right way: `render` does not include `game`. §6.1
3. All data from files is validated where it is read. §5.2
4. No allocations on the hot path. §4.2
5. No `static` inside functions or methods. §6.4
6. No `va()`, `strcpy`, `sprintf`, `strcat` in new code.
7. No limit overflows in silence. §6.6
8. Structs shared with a shader are declared only in `global.h`. §9.1
9. Vulkan objects are named, barriers go through the graph, no `QueueWaitIdle`. §8.1
10. A performance change is backed by a measurement. §10.1
11. A new format loader has a fuzz target. §11
12. The diff is readable: mechanical edits separated from substantive ones. §7.2

---

## 14. Tooling in the repository

```
.clang-format                  formatting, required, a CI gate
.clang-tidy                    the check set; warnings are errors in new code
.editorconfig                  UTF-8, LF, 4 spaces, final newline
tools/ci/local.sh              everything CI runs, in thirteen stages
tools/ci/check_ascii.py        §1
tools/ci/check_layering.py     §6.1, over the #include graph; ratchet
tools/ci/check_interface.py    how much of a game the engine can see; ratchet
tools/ci/check_strings.py      §13.6: bans strcpy/strcat/vsprintf, ratchets the rest
tools/ci/check_sources.py      a source no CMake list builds; a directory none names
tools/ci/check_msvc.py         declaration shapes only MSVC accepts
tools/ci/check_commits.py      §12.2
tools/shadergen/               the cross-platform permutation generator
tools/verify/                  the headless bench: the engine draws and quits
tests/fuzz/                    loader fuzz targets
tests/regression/              the campaign harness
```

Two of the gates are ratchets rather than gates: `check_layering.py` carries a baseline file and `check_interface.py` carries a line count. Both may shrink and never grow. Moving one is a decision that belongs in a commit message, not in a passing build.

---

## Appendix: why the rules are what they are

Every rule here came out of a specific defect found during the audit. A short map, so that a year from now nobody drops a rule without knowing what it was fixing:

| Rule | Defect |
|---|---|
| §1 Latin-only | Prevention: MSVC encodings, greppability, outside contributors |
| §2 no exceptions | `Com_Error` throws an `int` across a module boundary with no RAII |
| §4.2 zero allocations per frame | 1200 separate `new` calls for particles; `new CTrail` every 2 ms |
| §4.3 alignment | `Z_Malloc` ignores `iAlign` while `-msse2` is on |
| §4.5 no raw dumps | Saves are 32/64-bit incompatible; pointer addresses leak into the file |
| §5.2 validate data | 15 defects, including a stack overflow and RCE through a custom map |
| §5.3 no `va()` | 608 calls, 4 static buffers, silent string corruption |
| §6.1 layers | `ghoul2_shared.h` inside `game/`; the renderer calls `SV_Trace` |
| §6.4 no `static` in functions | `CBezier` with `static lastEnd[2]`; ragdoll statics block multithreading |
| §6.6 limits do not fail silently | `MAX_DRAWSURFS`, `MAX_POLYS`, `Pass::maxDrawItems` - all silent |
| §7.4 dead code | `ui/`, and the OpenAL/EAX path: 2,806 lines that never compiled here |
| §8.1 clean validation | Validation ran only on Windows debug, through a deprecated extension |
| §8.1 no `QueueWaitIdle` | A full stall on every texture uploaded |
| §8.2 no silent degradation | PBR switched itself off when `maxBoundDescriptorSets < 11` |
| §9.1 shared header | UBO layouts were duplicated in C++ and GLSL |
| §9.3 nothing generated in git | 61.7 MB and 1.9 million lines of `shader_data.c` |
| §12.3 rebase weekly | `prototype-pbr-bindless` fell a year behind and became unusable |
| §13.6 no `strcpy`/`strcat` | 214 call sites; four of them were live overflows from game data |

---

## Before pushing

```sh
tools/ci/local.sh
```

It runs what CI runs: the policy gates, Release, **Debug**, the Windows cross build, sanitizers, unit tests and six headless runs of the engine on the Vulkan renderer. A few minutes against fifteen spent waiting for an answer from CI.

Debug is not in that list for completeness. Debug compiles different code - in the renderer that is the ragdoll debug drawing - and a configuration nobody builds locally breaks silently and surfaces three pushes later.

What the script does not cover is MSVC. There is no Windows compiler here, and the Windows jobs remain the one thing that can fail only remotely. That is an argument for keeping the Windows-specific surface small, not for skipping the rest.
