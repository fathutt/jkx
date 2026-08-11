# Технический проект: SP-движок Jedi Outcast / Jedi Academy на Vulkan 1.3

**Рабочее название:** `JKX` (плейсхолдер — заменить)
**База рендерера:** `JKSunny/EternalJK@origin/pbr` (`7762e3c`, 06.08.2026)
**База движка:** `JACoders/OpenJK@1a6a643`, дерево `code/` + `shared/`
**Дата:** 8 августа 2026

Документ продолжает [аудит OpenJK](OpenJK-Audit-Roadmap.md). Там — «что есть»; здесь — «что строим и в каком порядке».

---

## 0. Зафиксированные решения

| Решение | Значение | Следствие |
|---|---|---|
| Рендерер | Форк `rd-vulkan@pbr`, портированный в SP | `rd-vanilla` и `rd-rend2` удаляются целиком; из них забираем только конкретные фичи (список в §7) |
| Графический API | **Vulkan 1.3**, без GL-пути | Baseline: GTX 900+ / RX 400+ / Intel Gen9+ / Mesa 22+. Dynamic rendering, sync2, descriptor indexing, timeline semaphores — всё в ядре |
| Структура | **Монолит** — один бинарник | Удаляются `refimport_t` (75 указателей), `refexport_t` (157), `game_import_t` (127), cgame syscall-ABI (126 номеров, 235 `case`). Итого ~550 точек ABI → 0 |
| Мультиплеер | Вырезан целиком | −508 908 LOC. Транспорт SV↔CL схлопывается до `memcpy` (см. аудит §6.3) |
| Платформы | Windows x64, Linux x64 | Удаляются x86, macOS, dedicated, XP-toolset |
| Язык | **C++20** (`CMAKE_CXX_STANDARD 20`, `/std:c++20`) | `std::span`, `std::bit_cast`, designated initializers, concepts для RHI-хелперов, `<format>` |
| Совместимость | Ассеты обязательна, ABI модов — нет, апстрим — нет | pk3/BSP/GLM/GLA/MD3/ROFF/`.efx`/`.shader` читаем как есть. Всё остальное свободно |
| PBR | Два режима: `r_pbr 0` (ванильный вид) / `r_pbr 1` | Старые карты выглядят как оригинал; PBR включается для ассетов с `.mtr` |
| Архитектура | Job system + render graph (id Tech 5/6), геймплейный слой не трогаем | Формат уровней, ассетов и игровая логика остаются |

---

## 1. Что берём из более поздних id Tech, а что нет

Честный разбор. Не всё, что делает id Tech 7, имеет смысл для игры 2002 года на ассетах 2002 года.

### Берём

| Идея | Откуда | Зачем именно нам |
|---|---|---|
| **Job system** (task graph, work stealing) | id Tech 5 | Bone-стадия Ghoul2 остаётся CPU-шной даже с GPU-скиннингом; загрузка карты полностью блокирующая; MT-запись командных буферов. Три независимых потребителя — окупается сразу |
| **Render graph** с автобарьерами и транзиентными ресурсами | id Tech 6/7, Frostbite | Прямо заменяет 29 `VkRenderPass` + 44 framebuffer + ручной layout-трекинг. Совмещается с переходом на dynamic rendering — одна работа вместо двух |
| **Clustered Forward+** (кластеры света и декалей) | id Tech 6 (Doom 2016) | JKA — лес alpha-blended поверхностей: сабли, форс, glow, погода. Deferred противопоказан. Заготовка уже есть: `CalcDynamicLightContribution` в `pbr.glsl` крутит цикл по `u_lights` |
| **Декали как кластеризованные объёмы** | id Tech 6 | Прямо решает проблему `cg_marks.cpp`: 256 марок × 10 вершин, переписываемых на CPU каждый кадр |
| **Bindless-текстуры + один draw на материал** | id Tech 7 | Убирает «дескриптор-сет на текстуру» и до 10 `vkCmdBindDescriptorSets` на draw. Заодно чинит баг: на GPU с `maxBoundDescriptorSets < 11` весь PBR **молча выключается** (`vk_init.cpp:517`) |
| **GPU-driven culling через indirect** | id Tech 7 | Но **последним**. В SP культинг сейчас не бутылочное горлышко |
| **GPU-частицы (compute)** | id Tech 6/7 | FX-система — 10 822 LOC, 1200 частиц потолок, виртуальный `Update()` на каждую, `G2API_GetBoltMatrix` **на частицу на кадр** |
| **Офлайн-компиляция ассетов в бинарный формат** | id Tech 5 | Опционально (§8.3). Даёт быструю загрузку и валидацию контента как побочный эффект |
| **Async compute + async transfer** | id Tech 6/7 | Бейк IBL, генерация нормалмапов, стриминг текстур — сейчас всё на одной очереди с `vkQueueWaitIdle` |

### Не берём

| Идея | Почему нет |
|---|---|
| **MegaTexture / виртуальное текстурирование** | Ассеты JKA — сотни мелких тайлящихся текстур с `tcMod`. Виртуальное текстурирование им ничего не даёт, а стоит человеко-годы |
| **Deferred / G-buffer** | См. выше: alpha-blended geometry везде. Forward+ — потолок разумного |
| **Полный ECS/DOD-переписывание геймплея** | 423k LOC игровой логики, ICARUS-скрипты, сейвы. Риск несоизмерим с выигрышем |
| **Мегатекстурный/мегагеометрический стриминг уровней** | Уровни JKA — 5–30 МБ BSP. Помещаются в память целиком |
| **Собственный шейдерный язык (Slang и т.п.)** | Ветка `pbr` уже завела **общий C/GLSL-заголовок** `global.h` — приём, решающий главную проблему (рассинхрон UBO-раскладок). Slang его ломает. Держим как опцию на потом |
| **Ray tracing** | Ветка `rtx-update` существует (562 КБ, порт Q2RTX), но это отдельный проект. Baseline 1.3 её не исключает — вернёмся, когда основа будет стабильна |

---

## 2. Целевая структура репозитория

```
engine/
  core/          арены и пулы, строки, контейнеры, лог, cvar, cmd, профайлер
  platform/      окно, ввод, время (монотонный таймер), потоки, загрузка библиотек
  jobs/          job system: планировщик, task graph, параллельные примитивы
  io/            VFS: pk3/zip, async I/O, кэш, стриминг
  math/          q_math (сохраняем ABI-совместимость с геймкодом)
render/
  vk/            Vulkan 1.3 backend: device, swapchain, VMA, дескрипторы, пайплайны
  rg/            render graph: ноды, ресурсы, барьеры, транзиенты
  frontend/      сцена, culling, сортировка, материалы (.shader/.mtr), модели
  passes/        конкретные проходы: depth, opaque, sky, transparent, postfx, ui
  fx/            GPU-частицы, декали, ribbons (сабля, трейлы)
  g2/            Ghoul2 — рендер-часть (VBO, скиннинг), математика костей в engine/
  shaders/       GLSL 4.50 + global.h, собирается CMake+glslc
game/
  sp/            бывший code/game    (JKA)
  jk2/           бывший codeJK2/game (JK2)
  cg/            бывший code/cgame — сильно похудевший
  icarus/        скриптовая VM
  ui/
tools/
  shadergen/     генератор перестановок шейдеров (кроссплатформенный)
  assetc/        офлайн-конвертер ассетов (фаза 8, опционально)
third_party/     volk, VMA, Vulkan-Headers, SDL3, stb, minimp3, MikkTSpace, Jolt (позже)
```

**Ключевой принцип:** DLL-границы больше нет, но **логическая граница остаётся**. `render/` не включает заголовки из `game/`, `game/` работает с рендерером через явный `namespace render` с плоским набором функций. Без этой дисциплины через полгода cgame начнёт лазить в `tr.`/`backEnd.` напрямую, и рефакторинг рендера станет невозможен.

Сейчас эта дисциплина нарушена в обе стороны и это надо чинить при переносе:
- `ghoul2_shared.h` (811 строк) лежит **внутри `code/game/`**, а Ghoul2 компилируется внутри рендерера;
- `G2_bones.cpp:2679` зовёт `ri.SV_Trace` — рендерер трассирует мир;
- cgame хранит рендер-состояние сабли (`saberTrail_t`) **внутри `playerState_t`**, то есть в сетевой структуре, которая сериализуется в сейв.

---

## 3. Vulkan 1.3 backend

Исходное состояние `pbr`: `apiVersion = VK_API_VERSION_1_1`, 107 ручных `PFN_vk*`, три самописных аллокатора, 29 `VkRenderPass` + 44 framebuffer, дескриптор-сет на каждую текстуру, 600 SPIR-V-блобов в виде **61,7 МБ C-исходника в git**, pipeline cache без сохранения на диск.

### 3.1 Фундамент (низкий риск, делать первым)

| # | Работа | Детали | Оценка |
|---|---|---|---|
| 3.1.1 | **volk** | `volkInitializeCustom(SDL_Vulkan_GetVkGetInstanceProcAddr())`, `volkLoadDevice`. Затем механическое `qvk*` → `vk*` (~2500 вхождений). Удалить вендоренный `codemp/rd-vulkan/vulkan/` (~30k строк) | 2–3 дня |
| 3.1.2 | **VMA** | Три аллокатора → один. Текстуры (`vk_image.cpp:1273`, чанки 32 МБ bump-only без освобождения), аттачменты (`vk_attachments.cpp:48`, ручные offset'ы), буферы (~15 сайтов `vkAllocateMemory`). Получаем: дефрагментацию, budget tracking вместо `ri.Error("GPU memory heap overflow")`, корректный ReBAR | 4–6 дней |
| 3.1.3 | **Персистентный pipeline cache** | `vkGetPipelineCacheData` → `fs_homepath/vkcache.bin`, при старте — проверка `pipelineCacheUUID`/`vendorID`/`deviceID` из `VkPhysicalDeviceProperties`. ~50 строк, при 2304 def'ах × до 6 пайплайнов эффект на время загрузки заметный | 1 день |
| 3.1.4 | **synchronization2** | 2 хелпера + 7 прямых `vkCmdPipelineBarrier` + 5 submit-сайтов. **Обязательно ужесточить stage/access:** сейчас в `vk_cubemap.cpp:342,351,397` передаётся `0, 0`, что разворачивается в `ALL_COMMANDS` — прямой простой конвейера | 3–4 дня |
| 3.1.5 | **Валидация везде** | Сейчас — только Windows + `_DEBUG`, через депрекейтнутый `VK_EXT_debug_report`. Переход на `VK_EXT_debug_utils`, включение на всех платформах, CI-джоба с `VK_LAYER_KHRONOS_validation` + sync-validation на llvmpipe/lavapipe | 3 дня |

### 3.2 Шейдерный тулчейн — делать до любых правок шейдеров

Сейчас: `shaders/tools/compile_threaded.cpp` (549 строк, `<windows.h>`, `_beginthreadex`, `_findfirst`), `.bat`-скрипты с `VsDevCmd.bat` и `cl.exe`, закоммиченные `.exe`-тулы (`bin2hex.exe`, `bindshader.exe`), запуск **процесса на каждый шейдер** (600 процессов), выход — `shader_data.c` на 61,7 МБ и ~1,9 млн строк.

Целевая схема:

```cmake
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin)
add_custom_command(OUTPUT ${OUT}/${name}.spv
  COMMAND ${GLSLC} --target-env=vulkan1.3 -O -MD -MF ${OUT}/${name}.d
          -I ${GLSL_DIR} -fshader-stage=${stage} ${DEFINES} ${SRC} -o ${OUT}/${name}.spv
  DEPENDS ${SRC} DEPFILE ${OUT}/${name}.d)
```

`glslc` вместо `glslangValidator` — ради `-I`, `#include` (`GL_GOOGLE_include_directive` уже используется), `-MD` для depfile'ов (CMake автоматически подхватит зависимости от `global.h` и `common/*.glsl`).

Упаковка: вместо 61,7 МБ C — бинарный `shaders.pak` (таблица `{name_hash, offset, size}`), встраиваемый через `#embed` или грузящийся из `base/`. `VkShaderModule` создаются **лениво** при первом использовании вместо 572 штук на старте.

**Сокращение перестановок.** Сейчас 8 осей дают ровно 600 блобов:

```
vbo(3) × fastlight(2) × light(4) × tx(3) × cl(×1.67) × env(2) × fog(2) × fog_only(2)
```

Что переводится на specialization constants без потери качества:

| Ось | Размер | Переводима | Как |
|---|---|---|---|
| `fog` | ×2 | да | `constant_id use_fog`, varying и биндинг объявляются всегда |
| `env` | ×2 (vert) | да | чисто вычислительная ветка |
| `cl` (`USE_CL1/CL2`) | ×1.67 | да | при переходе vertColor в per-draw SSBO исчезает как ось |
| `light` | ×4 | да | `constant_id light_mode` (0..3) |
| `tx` (`USE_TX1/TX2`) | ×3 | **при bindless — исчезает** | число бандлов становится spec-константой |
| `vbo` | ×3 | нет | разный vertex input; уходит с `VK_EXT_vertex_input_dynamic_state` или SSBO-VBO |
| `fastlight` | ×2 | оставить | ветка большая (весь `pbr.glsl`), риск регистрового давления |

Прогноз: **600 → ~135** (fog/env/cl) → **~55** (+light) → **~25** (+bindless) → **~10–15** (+vbo). `shader_data` из 61,7 МБ → менее 1 МБ.

> Оговорка: spec-constant-варианты всё равно порождают отдельные `VkPipeline`. Выигрыш не в числе пайплайнов, а в размере репозитория, времени компиляции C++, времени старта, числе `VkShaderModule` и — главное — в поддерживаемости: одна ветка кода вместо N `#ifdef`.

**Оценка:** тулчейн 1–1,5 нед; перевод осей на spec-constants 2–3 нед (много мелких визуальных регрессий, особенно fog — в JKA три режима).

### 3.3 Render graph + dynamic rendering — одна работа, не две

Это ключевое архитектурное решение фазы. Сейчас:

```
main, gamma, screenmap, capture, brdflut, cubemap        →  6
refraction.extract                                        →  1
bloom.extract, bloom.blend, bloom.blur[8]                 → 10
dglow.extract, dglow.blend, dglow.blur[8]                 → 10
prefilter[2] (в vk_cubemap.cpp)                           →  2
                                                    ВСЕГО 29 VkRenderPass + ~44 framebuffer
```

Плюс `VK_Pipeline_t::handle[RENDER_PASS_COUNT]` = 6 слотов на каждый def — прямой ×6 множитель к числу пайплайнов.

Переходя на `VK_KHR_dynamic_rendering`, мы обязаны завести явный трекинг layout'ов (сейчас его нет: `vk_record_image_layout_transition()` принимает старый layout параметром, вызывающий обязан его знать — 32 сайта). **Трекер layout'ов — это и есть половина render graph.** Поэтому делать надо сразу граф.

Минимальный дизайн:

```cpp
namespace rg {

using ResourceId = uint32_t;

struct TextureDesc { uint32_t w, h, layers, mips; VkFormat format;
                     VkSampleCountFlagBits samples; bool transient; };

class Builder {                      // фаза объявления
public:
    ResourceId createTexture(std::string_view name, const TextureDesc&);
    ResourceId importTexture(std::string_view name, VkImage, VkImageView,
                             VkImageLayout current, const TextureDesc&);
    ResourceId read (ResourceId, VkPipelineStageFlags2, VkAccessFlags2, VkImageLayout);
    ResourceId write(ResourceId, VkPipelineStageFlags2, VkAccessFlags2, VkImageLayout);
    ResourceId colorAttachment(ResourceId, VkAttachmentLoadOp, VkAttachmentStoreOp);
    ResourceId depthAttachment(ResourceId, VkAttachmentLoadOp, VkAttachmentStoreOp);
};

// нода = имя + setup-лямбда + execute-лямбда
template <typename Data, typename Setup, typename Exec>
const Data& addPass(std::string_view name, PassKind kind, Setup&&, Exec&&);

// PassKind: Graphics | Compute | AsyncCompute | Transfer

void compile();   // топосорт, вычисление времени жизни, алиасинг транзиентов, барьеры
void execute(FrameContext&);   // vkCmdBeginRendering / vkCmdPipelineBarrier2 / callbacks
}
```

Что это даёт помимо удаления 29+44 объектов:

1. **Автоматические барьеры и layout-переходы.** Все 32 ручных сайта исчезают; граф выводит `VkImageMemoryBarrier2` из объявленных read/write. Это единственный надёжный способ не закрепить существующие скрытые гонки при переходе на sync2.
2. **Алиасинг транзиентных ресурсов.** Сейчас `vk.bloom_image[]`, `vk.dglow_image[]`, `vk.refraction_extract_image`, `screenMap`, `capture` живут постоянно. Большинство нужны 2–3 прохода в кадре.
3. **Async compute бесплатно.** Нода объявляется `AsyncCompute`, граф сам вставляет семафорные зависимости. Симуляция частиц, генерация нормалмапов, бейк IBL — кандидаты.
4. **MT-запись командных буферов.** Граф знает независимые ветки → раздаёт их job system (см. §4). Абстракция `DrawItem`/`Pass` из `pbr` (`tr_local.h:2706`, память из `perFrameMemory`) — уже готовый задел под это.
5. **Пайплайн привязывается к форматам, а не к render pass.** `handle[6]` → `handle[1]` (main/screenmap/cubemap/refraction используют одинаковые `color_format`+`depth_format`, различаясь только sample count, а он снимается через EDS3).
6. **Layered rendering для бейка кубмапов через `viewMask`** вместо geometry shader `filtercube.geom` — 6 граней за проход без GS.

Целевой граф кадра:

```
[JobSync: bones/skinning]
        │
        ├─► [ShadowCascades ×3]           (фаза 8)
        ├─► [SkyPortalView]               (если карта имеет skybox-портал)
        │
        └─► [MainView]
              ├─ DepthPrepass             (прототип 28a48a4)
              ├─ HiZ build (compute)      (фаза 9, под GPU culling)
              ├─ ClusterAssign (compute)  свет + декали → кластеры
              ├─ Opaque (Forward+, MRT: color + glow)
              ├─ Sky
              ├─ Decals                   (кластеризованные)
              ├─ ParticleSim (async compute) ──┐
              ├─ Transparent + WorldFX ◄───────┘
              ├─ Refraction extract → Refraction fill
              └─ SSAO (фаза 8)
        │
        └─► [PostFX Stack]
              Underwater → Distortion → Bloom → ForceSight → NightVision
              → ToneMap+ColorGrade → ScreenTint → Vignette → Letterbox → Fade
        │
        └─► [UI/2D] ─► [Present]
```

**Оценка:** render graph каркас 2–3 нед, миграция всех проходов 2–3 нед, dynamic rendering в рамках этого — включено. Риск средний.

### 3.4 Timeline semaphores

Сейчас: `VkFence rendering_finished_fence` × 2 кадра + `image_acquired[2]` + `swapchain_rendering_finished[8]` + `rendering_finished2[2]` + `image_uploaded2` + `aux_fence`. Логика `USE_UPLOAD_QUEUE` в `vk_end_frame()` — самая запутанная часть синхронизации, и она **отключена** коммитом `60a7a10`.

Целевое: один timeline-семафор на очередь, монотонный счётчик кадров, `vkWaitSemaphores(frame - FRAMES_IN_FLIGHT)`. Swapchain-семафоры остаются binary (timeline не поддерживается в `VkPresentInfoKHR`).

**Отдельный бонус:** `vk_end_command_buffer()` сейчас делает `vkQueueWaitIdle(vk.queue)` **на каждой одноразовой операции** — создание аттачментов, загрузка каждой текстуры, бейк каждого кубмапа. Timeline позволяет это убрать и заметно ускорить загрузку карты.

**Оценка:** 4–6 дней. Риск средний (легко получить фризы; тестировать `r_swapInterval` 0/1 × оконный/полноэкранный).

### 3.5 Bindless

Текущий антипаттерн: на каждую `image_t` аллоцируется свой `VkDescriptorSet` с одним `COMBINED_IMAGE_SAMPLER` (`vk_image.cpp:1352`); пул рассчитан на `MAX_DRAWIMAGES × 3`; горячий путь биндит **до 10 сетов на draw** (`vk_shade_geometry.cpp:849`).

Хуже того — `vk_init.cpp:517`:
```cpp
if ((!normalMapping && !specularMapping) || vk.maxBoundDescriptorSets < 11)
    vk.useFastLight = qtrue;   // весь PBR молча выключается
```
На части Intel-драйверов `maxBoundDescriptorSets == 8`. То есть PBR там не работает вообще, без единого сообщения. Bindless — единственное радикальное лечение.

Целевое:
```glsl
layout(set = 1, binding = 0) uniform sampler2D   texture_array[];
layout(set = 1, binding = 1) uniform samplerCube cubemap_array[];
```
с `DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VARIABLE_DESCRIPTOR_COUNT_BIT`, индексы в per-draw SSBO (`texture_idx[8]`), доступ через `nonuniformEXT`, `draw_id` push-константой.

**Заготовка есть** — ветка `prototype-pbr-bindless` (`vk_bindless.cpp`, 708 строк; `texture.h::global_texture()`; `texture_idx[8]`). Но ветка на год отстала от `pbr` и построена на другом `global.h` (GLSL-only, 540 строк, тогда как в `pbr` он стал shared C/GLSL). **Мержить нельзя — переписывать поверх `pbr`, переиспользуя ~40–50 % кода.**

10 дескриптор-сетов → 2 (global uniform/SSBO + bindless textures). `uniform_global` естественно становится SSBO с индексацией по `draw_id`, что убирает 7 динамических offset'ов.

**Оценка:** 3–4 нед. Риск высокий — трогает шейдеры, `image_t`, все SPIR-V-блобы, ломает `RB_AddDrawItem*`. **Делать после render graph, но до работы над сокращением пайплайнов.**

### 3.6 Сжатие ключа пайплайна (EDS3)

`Vk_Pipeline_Def` — 17 полей. Разложение по тому, что можно сделать динамическим:

| Источник | Vulkan 1.3 core | EDS3 |
|---|---|---|
| cull, frontFace, topology, depthTest/Write/CompareOp, depthBiasEnable, stencilOp | ✅ `vkCmdSet*` | — |
| blend equation/enable, colorWriteMask, polygonMode, **rasterizationSamples**, alphaToCoverage | — | ✅ `vkCmdSet*EXT` |
| `shader_type` (~50), `light_flags` (4), `pbr_flags` (2), `fog_stage` (3), `vbo` (3) | — | остаются статикой (разные модули) |
| `alpha_test`, `tex_mode`, `discard_mode`, `identity_color`, sprite-флаги | spec-constants (уже 21 штука) | — |

Прогноз по числу `VkPipeline` на типичной карте:

```
сейчас                                        ~800–1500
+ dynamic rendering (убирает ×6 render pass)   ~300–500
+ Vulkan 1.3 core dynamic state                ~120–200
+ EDS3                                          ~60–100
```

**Сокращение в 10–20 раз**, и — важнее — практически исчезают хитчи от ленивого `vk_gen_pipeline()` в середине кадра (`vk_shade_geometry.cpp:887`).

**Важно:** `VK_EXT_extended_dynamic_state3` — **не core в 1.3**, поддержка пофичевая (каждая feature — отдельный булев флаг). Обязателен fallback: неподдержанная фича остаётся в статическом ключе. Значит `Vk_Pipeline_Def` должен уметь оба режима — хэш-ключ маскируется по поддержанным динамическим состояниям. Плюс на некоторых драйверах избыток dynamic state снижает производительность — нужен бенчмарк.

**Оценка:** 1,5–2 нед с fallback-матрицей и замерами. Риск средний-высокий.

### 3.7 Сводка по Vulkan-модернизации

| Работа | Оценка | Риск | Порядок |
|---|---|---|---|
| volk + Vulkan-Headers | 0,5 нед | низкий | 1 |
| VMA | 1,0 нед | низкий | 1 |
| Персистентный pipeline cache | 0,2 нед | нулевой | 1 |
| Валидация на всех платформах, debug_utils | 0,6 нед | низкий | 1 |
| Шейдерный тулчейн (CMake + glslc + бинарная упаковка) | 1,2 нед | низкий | 1 |
| synchronization2 | 0,8 нед | низкий | 2 |
| **Render graph + dynamic rendering** | 5,0 нед | средний | 2 |
| Timeline semaphores | 1,0 нед | средний | 3 |
| Spec-constants (600 → ~55 блобов) | 2,5 нед | средний | 3 |
| **Bindless** | 3,5 нед | **высокий** | 4 |
| EDS3 + сжатие ключа | 2,0 нед | ср.-высокий | 5 |
| Кросс-вендорная валидация (NV/AMD/Intel × Win/Linux) | 1,5 нед | — | сквозная |
| **Итого** | **~20 чел-недель** | | |

---

## 4. Job system

Три независимых потребителя оправдывают его сразу:

1. **Bone-стадия Ghoul2** — остаётся CPU-шной даже при GPU-скиннинге (`G2_TransformGhoulBones` по энтити независимы). ~0,3–0,6 мс на 8–10 NPC, идеально параллелится.
2. **Загрузка карты** — сейчас полностью блокирующая: чтение BSP, `dlopen`, генерация mip-цепочек **на CPU** (`vk_image_process.cpp`), тангенты через MikkTSpace, бейк IBL (при 128 пробах — 768 полноценных прогонов сцены).
3. **MT-запись командных буферов** — render graph знает независимые ветки.

Дизайн — минимальный, без фиберов:

```cpp
namespace jobs {
    using JobFn = void(*)(void* userData, uint32_t index);

    struct Counter { std::atomic<uint32_t> pending; };

    void init(int workerCount);           // = hardware_concurrency() - 1
    void shutdown();

    void dispatch(JobFn, void* data, uint32_t count, uint32_t groupSize, Counter* out);
    void wait(Counter*);                  // воркер, ждущий счётчик, помогает пулу
    bool isBusy(const Counter*);
}
```

Work-stealing deque на воркера, главный поток — тоже воркер при `wait()`. Никаких `std::function`, никаких аллокаций в горячем пути.

**Блокеры, которые надо снять до параллельной анимации** (все три найдены в аудите):

| Блокер | Где | Что делать |
|---|---|---|
| Ragdoll держит всё состояние в файловых статиках | `G2_bones.cpp:1118-1124`: `static mdxaBone_t ragBones[256]`, `static SRagEffector ragEffectors[256]`, `static int numRags` | Вынести в per-instance контекст. Заодно вынести весь ragdoll (3350 из 4784 строк) в `G2_ragdoll.cpp` |
| Рендерер трассирует мир | `G2_bones.cpp:2679`: `ri.SV_Trace(...)` | Собирать запросы трасс, выполнять пакетом до анимационной фазы |
| Глобал `HackadelicOnClient` | `tr_ghoul2.cpp` | Переносится в контекст |

**Оценка:** каркас job system 1 нед; распараллеливание bone-стадии 1,5 нед; расшивка ragdoll-блокеров 1,5 нед; параллельная загрузка ассетов 1,5 нед.

---

## 5. Что переезжает из cgame в рендерер

`code/cgame/` — 51 571 LOC, из них **порядка 35 % — чистая графика**. Каждый спрайт частицы сейчас проходит через vararg-трамплин `Q_syscall()` → `CL_CgameSystemCalls()` — `switch` на ~150 веток (`cl_cgame.cpp:811`). В монолите трамплин исчезает, но код всё равно надо переразложить по слоям, иначе получится просто «всё в одном .exe».

### 5.1 Подводные эффекты — разбор и целевой дизайн

Ты назвал их прямо, и они оказались показательным примером: **единого подводного эффекта не существует**, он размазан на девять несвязанных кусков, и рендерер о воде не знает вообще ничего.

**(а) «Искажение» — это дрожание FOV на CPU.** `cg_view.cpp:1284-1302`:

```c
if ( cg.refdef.viewContents & ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) ) {
    phase = cg.time / 1000.0 * WAVE_FREQUENCY * M_PI * 2;
    v = WAVE_AMPLITUDE * sin( phase );
    fov_x += v;   fov_y -= v;
    inwater = qtrue;
}
```

`WAVE_AMPLITUDE 1`, `WAVE_FREQUENCY 0.4` (`cg_local.h:100`). Это ±1° синусоидальная модуляция FOV с периодом 2,5 с — **не warp**, никакого экранного искажения, никакого преломления. Наследие Quake 2 один-в-один. Побочный эффект: дрожит вся проекция, включая вьюмодель и экранные координаты HUD. Авторы прямо в комментарии (`cg_view.cpp:1281`) признают баг: `CG_PointContents` смотрит только leafbrushes, поэтому вода из `func_door` варпает вид всегда, независимо от реального уровня воды.

**(б) «Цвет воды» — fullscreen `CG_FillRect`.** `cg_draw.cpp:3893`:
```c
hcolor[3] = 0.3 + (0.05f*sin( phase ));
hcolor[0] = 0; hcolor[1] = 0.2f; hcolor[2] = 0.8f;
CG_FillRect( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, hcolor );
```
Лава — красный (`:3873`), слизь — зелёный (`:3883`). Композиция неправильная: заливка идёт до HUD, поэтому прицел и худ **не** под фильтром, а вьюмодель — под.

**(в) Канал в рендерер существует и игнорируется.** `refdef_t` (`tr_types.h:190`) содержит `int viewContents`. Заполняется в cgame, читается **только в cgame**. Внутренний `trRefdef_t` этого поля не имеет вообще — `RE_RenderScene` его не копирует.

**(г–и)** Звук (`S_Respatialize(inwater)`, EAX-only и Windows-only), пузыри дыхания (`CG_BreathPuffs`, работает **только в третьем лице** — свои пузыри игрок не видит никогда), всплеск (`_PlayerSplash`, гейтится по `cg_shadows` — «след на воде» отключается вместе с тенями), сабля в воде, физика частиц. Водной поверхности как кода нет вообще — только BSP-шейдер с `tcMod turb`.

**Целевой дизайн — `UnderwaterPass` в графе:**

```cpp
struct MediumParams {           // данные, не if/else
    vec3  absorption;           // коэффициент Бугера по RGB
    vec3  scatter;
    float density;
    vec2  warpAmp, warpFreq, warpSpeed;
    float causticsScale, causticsSpeed;
    qhandle_t causticsTex;
    float lowpassHz;            // для звука
};
```

Один экземпляр на WATER / SLIME / LAVA, читается из `.medium`-файла, а не хардкодится тремя `else if`.

Сам проход:

1. **`viewContents` вычисляет рендерер**, а не геймкод: `CM_PointContents(tr.refdef.vieworg)` внутри `RE_RenderScene`. Уходит баг с `func_door`.
2. **Screen-space UV-warp** по двум частотам + шум, амплитуда в пикселях, а не в градусах FOV. Это то, чего игра хочет с 1999 года и никогда не делала.
3. **Depth-based absorption/scattering**: `color = mix(color, mediumColor, 1 - exp(-density * linearDepth))`. Требует depth как ресурс — граф его уже даёт.
4. **Caustics** — проекция анимированной текстуры по нормали вверх, additive-проход по геометрии под ватерлинией.
5. **Ватерлиния.** Сейчас логика бинарная (`CONTENTS_WATER` в точке глаза), и при пересечении поверхности картинка щёлкает мгновенно. Нужен `waterPlaneZ` — трассировка вверх от `vieworg`, и плавный переход с частично погружённой камерой.
6. **Chromatic aberration + vignette** по краям, опционально.
7. **Звук:** `S_SetListenerMedium(medium)` с low-pass IIR прямо в микшере, вместо зависимости от `EaxMan.dll` (которая на 64-битных сборках всё равно не работает — см. аудит §3.8).

Стоимость: ~1 неделя **после** того, как готов post-fx stack.

### 5.2 Post-process stack — каркас, разблокирующий всё остальное

В cgame сейчас **20 fullscreen-вызовов** `DrawStretchPic`/`FillRect`. Из них `CG_Draw2DScreenTints()` (`cg_draw.cpp:3564-3903`) — один статический метод на **340 строк**, вручную реализующий шесть независимых fade-машин на 12 глобальных переменных (`cgRageTime`, `cgRageFadeTime`, `cgRageFadeVal`, `cgAbsorbTime`, ... ). Каждая — копипаст одного паттерна. Буквально `lerp(sceneColor, tint, a)` в 340 строках императивного кода с ручным таймингом.

```cpp
enum class PostFxSlot : uint8_t {
    Underwater, Distortion, Bloom, ForceSight, NightVision,
    ToneMap, ColorGrade, ScreenTint, DamageVignette, Vignette,
    Letterbox, GlobalFade, _Count
};

struct PostFxParams {                 // POD, копируется в UBO
    vec4  tint;
    vec2  warpAmp, warpFreq, warpSpeed;
    vec3  absorption, scatter;
    float density, vignette, saturation, exposure;
    float damageDirX, damageDirY, damageIntensity;
    float letterboxFrac, fadeAlpha;
};

void  R_PostFx_SetEnabled(PostFxSlot, bool);
void  R_PostFx_SetParams (PostFxSlot, const PostFxParams&);
float R_PostFx_PushEnvelope(PostFxSlot, float target, int attackMs, int releaseMs);
```

Шесть fade-машин из 340 строк схлопываются в шесть строк `R_PostFx_PushEnvelope` с общим ADSR-envelope (~40 строк на всё).

Сюда же переезжают: damage blend blob (`cg_view.cpp:1478` — спрайт в 8 юнитах перед глазом, ломается при малых FOV, конфликтует с вьюмоделью по сортировке → directional damage vignette), letterbox и cinematic fade (`cg_camera.cpp:1063,1316`), LA Goggles, Force Sight, дезинтеграция, cloak/refraction.

Отдельно: `CFlash::Draw()` (`FxPrimitives.cpp:2310`) — «полноэкранная вспышка», реализованная как спрайт в 8 юнитах от глаза с `radius = 8 * tan(fov_x/2)`, с комментарием в коде *«if znear is set > than this, then the flash doesn't appear at all»*. Должно быть чистым additive post-эффектом.

### 5.3 GPU-частицы

FX-система — 10 822 LOC (21 % cgame). Профайлер (`fx_debug 1`) показывает пороги, которые авторы считали красными: 500 частиц, 500 oriented, 500 lines, 400 tails, 600 активных эффектов, жёсткий потолок `MAX_EFFECTS = 1200`.

Патологии:

- **Каждый `CEffect` держит целый `refEntity_t` inline** (`FxPrimitives.h:136`), ~200+ байт, и аллоцируется отдельным `new`. 1200 слотов ≈ 250 КБ, разбросанных по куче. Худший возможный layout для объектов, обновляемых каждый кадр.
- При переполнении — `FX_FreeMember(&effectList[0])`, то есть просто убивает первый, с честным комментарием авторов: *«Hmmm.. just trashing the first effect in the list is a poor approach»*.
- **`gi.G2API_GetBoltMatrix` на каждую частицу каждый кадр** для bolted-эффектов (`FxSystem.cpp:190`). Для эффекта из 50 частиц на болте — 50 запросов болт-матрицы в кадр.
- При `FX_EXPENSIVE_PHYSICS` + `fx_expensivePhysics 1` (**дефолт**) полная трасса делается безусловно каждый кадр на частицу.
- `CBezier::Draw()` использует `static vec3_t lastEnd[2]` **внутри метода класса** — два одновременных безье гарантированно портят друг другу шов.
- Со стороны рендерера: `MAX_POLYS = 2048` при переполнении молча дропает, с комментарием *«happens a lot with high fighting scenes and particles»*.

Целевой пайплайн:

```
CPU: эмиссия и параметры (парсер .efx остаётся, но компилирует в POD-дескриптор)
  GpuEmitterInstance { descId, entityNum, mat3x4 transform, startTime, killTime }
     ↑ ОДНА болт-матрица на эмиттер, не на частицу

GPU:
  EmitCS      → аппенд в pool (atomic counter)
  SimulateCS  → интегрирование + curve eval из LUT-атласа + depth-buffer collision
  CullSortCS  → frustum cull → compacted index buffer, bitonic sort по view-depth
  DrawIndirect→ vertex-shader billboard expansion из point-list

SoA-layout: pos.xyz | age | vel.xyz | seed | color.rgba(packed) | sizeRot = 48 байт
            → 100k частиц = 4,8 МБ
```

CPU-трассы остаются только для `FX_IMPACT_RUNS_FX` / `FX_KILL_ON_IMPACT`: GPU пишет impact-события в append-буфер, CPU читает с задержкой 1–2 кадра и спавнит дочерние эффекты. Для 95 % эффектов (искры, дым, вспышки) фидбек не нужен вообще.

Кривые (`UpdateSize/RGB/Alpha/Rotation` — по ~70 строк `switch` с `sin`/`cos` на частицу, 5 типов: linear/nonlinear/wave/clamp/rand) становятся lookup'ом в 1D-LUT-текстуру.

**Предусловие:** сначала перевести `CTrail`/`CPoly`/`CBezier` на renderer-side reTypes (они единственные, кто ещё зовёт `AddPolyToScene`). Паттерн уже есть — `RB_SurfaceElectricity` (`tr_surface.cpp:820`).

### 5.4 Декали — прямо ложатся на кластеры

`cg_marks.cpp` (287 LOC): пул на **256** марок, `CG_ImpactMark` зовёт `cgi_CM_MarkFragments` (клиппинг по BSP делает движок), но собирает `polyVert_t` обратно в cgame. `CG_AddMarks()` каждый кадр **пересчитывает `modulate[0..3]` для каждой вершины** и шлёт `AddPolyToScene`. До 2560 вершин, переписываемых на CPU каждый кадр, затем ещё раз `memcpy` внутри `RE_AddPolyToScene`, и для каждой ещё поиск fog-объёма перебором `tr.world->numfogs`.

Целевое — декали как кластеризованные объёмы (id Tech 6): атлас декалей, проекция в фрагментном шейдере по кластерам, fade через per-decal instance data. Снимается лимит 256, исчезают 2560 вершин/кадр.

Показательно: **Ghoul2-марки уже сделаны правильно** — `CG_AddGhoul2Mark` заполняет `SSkinGoreData` и уходит в `G2API_AddSkinGore`. Нужно было и обычные так же.

### 5.5 Полный список переноса, по приоритету

| # | Что | Откуда | LOC | Оценка | Выигрыш |
|---|---|---|---|---|---|
| 1 | **Post-process stack** + все tints, damage blob, letterbox, fade | `cg_draw.cpp:3564-3903`, `cg_view.cpp:1478`, `cg_camera.cpp:1063,1316` | ~450 | 1,5 нед | **Очень высокий** — разблокирует всё остальное, чинит порядок композиции |
| 2 | **Underwater pass** | `cg_view.cpp:1284`, `cg_draw.cpp:3873-3902` | ~60 | 1 нед | **Очень высокий** — впервые настоящий эффект; дёшево после п.1 |
| 3 | **Декали в рендерер** | `cg_marks.cpp` целиком | ~370 | 1,5 нед | Высокий |
| 4 | **Lightstyles + анимация dlight** | `cg_lights.cpp`, `cg_ents.cpp:1924` | ~220 | 3 дня | Средний/высокий — убирает 64 вызова в кадр, код наполовину уже в рендерере |
| 5 | **Сабля → `RT_SABER`** (клинки, core, glow, dlight, trail) | `cg_players.cpp:5632-5841, 5974-6800` | ~900 | 3 нед | Высокий — убирает `new CTrail` каждые 2 мс и рендер-состояние `saberTrail_t` из `playerState_t` |
| 6 | `CBezier`/`CTrail`/`CPoly` → renderer reTypes | `FxPrimitives.cpp:1750-1822,1906,2091-2273` | ~350 | 1,5 нед | Высокий — последние `AddPolyToScene` из FX |
| 7 | **GPU Particle System** | `FxPrimitives.cpp` + `FxUtil.cpp` | ~3760 | 6–8 нед | **Максимальный по перфу**: 1200 → 100k+ частиц |
| 8 | Force Sight / LA Goggles → пассы | `cg_players.cpp:4504`, `cg_draw.cpp:2276,3977,4275` | ~250 | 1 нед | Средний — outline через stencil вместо второго прохода геометрии |
| 9 | Disintegrate / Cloak / Distortion → entity-effect API | `cg_players.cpp:4040-4157, 4645-4780` | ~350 | 1,5 нед | Средний — убирает выбор размера render-target'а **из геймкода** |
| 10 | Skybox-портал → view-нода графа | `cg_view.cpp:1791-1936` | ~145 | 1 нед | Средний — убирает парсинг конфигстринга каждый кадр |
| 11 | Тени: политика в рендерер | `cg_players.cpp:3441-3557` | ~120 | 4 дня | Средний — `cg_shadows` перестаёт решать, будет ли всплеск на воде |
| 12 | Lens flares | `cg_view.cpp:1520-1570` + включить `RB_RenderFlares` | ~80 | 1 нед | Низкий/средний |
| 13 | Screen shake → camera modifier | `cg_camera.cpp:1352-1450` | ~150 | 4 дня | Низкий/средний — нужен для motion blur (velocity камеры) |
| 14 | Стекло/обломки | `cg_effects.cpp:667-1001` | ~340 | 2 нед | Низкий — делать после п.7 |
| 15 | 2D-слой → aspect-aware immediate UI | `cg_drawtools.cpp` | ~490 | 1 нед | Низкий — виртуальные 640×480 ломают маски на широких экранах |

**Не переносим:** третьеличную камеру и damping, скриптовые камеры/ROFF/ICARUS, интерполяцию снапшотов, HUD/датапад/скорборд, логику анимаций игрока, парсер `.efx` (остаётся, но с POD-выходом).

**Принцип разделения:** геймкод описывает **намерение** («эта сущность дезинтегрируется, t=0.4, точка входа P», «у сабли два клинка, синие, длина 32»), рендерер решает **как** — сколько проходов, какой таргет, blob или stencil, CPU или compute.

**Целевые метрики:**

| Метрика | Сейчас | Цель |
|---|---|---|
| LOC в `cgame` | 51 571 | ~35 000 |
| `AddRefEntityToScene` / кадр | 500–1500 | < 300 |
| `AddPolyToScene` / кадр | до 2800 | **0** |
| Живых частиц | 1200 (жёсткий потолок) | 100 000+ |
| Fullscreen `FillRect` | 9 | **0** |

---

## 6. Материалы: два режима PBR

`r_pbr 0` — ванильная модель освещения на оригинальных текстурах. `r_pbr 1` — полный PBR.

Почему это обязательно, а не опция: JKA-текстуры запечены с бликами и AO **внутри diffuse**, а диффузная составляющая освещения берётся из lightmap'ов, посчитанных radiosity-солвером 2001 года в LDR. Включить PBR поверх этого — значит получить картинку **хуже** оригинала на 90 % контента.

Механизм `.mtr` из ветки `pbr` для этого хорошо устроен: для каждого `shaders/*.shader` сначала пробуется одноимённый `shaders/*.mtr`, и если он есть — **полностью замещает** `.shader`. То есть автор ремастера кладёт `.mtr` рядом, оригинал не трогается.

Поддерживаемые упаковки (из `pbr`, оставляем как есть — набор исчерпывающий):

| Кейворд | Упаковка |
|---|---|
| `normalMap`, `normalHeightMap` | tangent-space, height в альфе |
| `specMap` / `specularMap` | spec-gloss (legacy) |
| `rmoMap`, `rmosMap`, `moxrMap`, `mosrMap` | Roughness/Metal/Occlusion варианты |
| `ormMap`, `ormsMap` | glTF-порядок |
| `$deluxemap` | направление света из q3map2 |

Свизлы каналов задаются таблицей `textureMapTypes[]` и применяются **прямо в `VkImageViewCreateInfo.components`** — все упаковки нормализуются к одному layout во view, шейдер один. Отличный приём, сохраняем.

Скаляры: `roughness`, `gloss`, `specularReflectance`, `specularExponent`, `specularScale`, `normalScale`, `parallaxDepth`, `parallaxBias`.

Авто-подбор по суффиксу (`_rmo`, `_orm`, `_n`, `_nh`, ...) уже реализован; при отсутствии normal и `r_genNormalMaps 1` — очередь в compute-генератор.

**Diffuse IBL — готовый, но не подключённый слот.** `vk_cubemap.cpp` уже генерирует irradiance-кубмап (64², `R32G32B32A32_SFLOAT`, 7 мипов), но `VK_DESC_PBR_IRRADIANCE` закомментирован в `global.h:40` и `vk_pipelines.cpp:160`. Код бейка готов — нужен только биндинг и ветка в шейдере. ~0,8 нед. Это то, чего нет **вообще** в rend2, и главный визуальный разрыв PBR-режима.

**Кэш IBL-бейка на диск.** Сейчас бейк — рантайм на загрузке карты: `numCubemaps × 6` полноценных прогонов сцены в 256² + 16 draw'ов свёртки. При 128 пробах это **768 прогонов сцены**. Свёртка детерминирована → кэшировать в `.ktx2` рядом с `env.json`. ~1 нед, резко ускоряет загрузку.

**Латентный баг, чинить при переносе:** `R_RenderAllCubemaps` берёт `MIN(tr.numCubemaps, 128)`, а `QSORT_CUBEMAP_BITS = 6` → в sort-key индекс влезает только до 63. Пробы с индексом ≥ 64 получают чужое окружение.

---

## 7. Что забираем из удаляемых рендереров

`rd-vanilla` и `rd-rend2` удаляются, но не раньше, чем из них извлечено следующее.

### Из `code/rd-vanilla` (SP) — обязательно, иначе SP не заработает

| Что | LOC | Комментарий |
|---|---|---|
| `tr_draw.cpp` | 1025 | 2D-слой, `RE_Scissor`, Dissolve (переход уровня), `GetScreenShot`, `TempRawImage_*`, LAGoggles. **Нет ни в MP-vanilla, ни в rend2, ни в rd-vulkan** |
| Типы поверхностей | — | `RB_SurfaceCone`, `Cylinder`, `Electricity`, `SaberGlow`, `Lathe`, `Clouds` — SP-only |
| Дезинтеграция | — | `RB_CalcDisintegrateColors:1230`, `RB_CalcDisintegrateVertDeform:1351` |
| SP World Effects API | — | `IsOutside`, `IsOutsideCausingPain`, `IsShaking`, `GetWindVector`, `GetWindGusting`, `GetChanceOfSaberFizz`. **Это геймплей, а не графика**: кислотный дождь наносит урон, ветер влияет на физику, сабля шипит в дождь |
| Ghoul2 gore | — | В rend2 — 49-строчная заглушка; в SP это заметная механика (следы сабли на телах) |
| `tr_stl.cpp` | 78 | Маргинально, но дёшево сохранить |

### Из `codemp/rd-rend2` — точечно, по фичам

| Что | Оценка порта | Когда |
|---|---|---|
| **Tone mapping**: Hable/Uncharted2 + **ACES fitted, который уже написан и просто не вызывается** из `main()` (`tonemap.glsl:54`) | 2–3 нед вместе с linear workspace | Фаза 8, **первым** |
| **Auto-exposure**: `calclevels4x.glsl` — иерархическая лог-яркостная пирамида | включено выше | Фаза 8 |
| **CSM для солнца**: 3 каскада PSSM, стабилизация снапом по `worldUnitsPerTexel` (`tr_main.cpp:2864-3000`), `sunShadow()` в `lightall.glsl:531` | 6–8 нед | Фаза 8 |
| **SSAO** + bilateral depth-blur | 4 нед (после depth prepass) | Фаза 8 |
| **Point shadows** (cube, 9-точечный Poisson) | 2 нед | Фаза 8, опционально |
| **Parallax / deluxe mapping** | 1 нед | Фаза 8 |
| `Diff_Burley` (Disney 2012) — **написан, не вызывается** | 1 час | когда угодно |
| Multiscatter GGX energy compensation (Fdez-Agüera) | ~50 строк | когда угодно |

### Дефекты rend2, которые НЕ надо тащить

- `RB_CreateSortKey` (`tr_shade.cpp:571`) кладёт **младшие 24 бита указателя** `shaderProgram` в ключ сортировки, плюс `std::sort` не стабильная → порядок прозрачных объектов зависит от адреса в памяти, недетерминирован из-за ASLR. Артефакты альфа-блендинга «через раз».
- `layer` клампится до 15, а `SS_NEAREST` = 16 → верхние sort-классы схлопываются.
- MD3-вершинная анимация не работает: `R_LoadMD3` заливает в VBO только кадр 0.

---

## 8. Ядро движка

### 8.1 Память

`Hunk_Alloc` **объявлен и нигде не определён**; hunk-аллокатор заменён тегом в глобальном двусвязном списке всех аллокаций (`Z_TagFree` — линейный проход). Результат: потеря локальности, фрагментация, ручной `Cvar_Defrag()` в `SV_SpawnServer` с комментарием «This frees, then allocates».

Целевое: настоящие арены с областями жизни.

```cpp
Arena  g_permanent;   // на весь процесс
Arena  g_level;       // сбрасывается при смене карты — возврат смысла Hunk_*
Arena  g_frame[2];    // per-frame, double-buffered
Pool<T> ...           // для объектов фиксированного размера
```

Плюс обязательно: `Z_Malloc` должен наконец **уважать параметр выравнивания** — сейчас `int /*unusedAlign*/` игнорируется, а глобально включён `-msse2`. Убрать `Sys_Sleep(1000)` из аварийного пути аллокатора.

### 8.2 Тайминг

- Монотонный высокоточный таймер: `QueryPerformanceCounter` / `clock_gettime(CLOCK_MONOTONIC)`. Сейчас на Unix — `gettimeofday()`, **не монотонный**: скачок NTP сдвигает игровое время.
- Убрать округление `msec < 1 → 1` (`common.cpp:1321`) — при >1000 FPS игровые часы обгоняют реальные.
- Накапливать дробный остаток независимо от `com_timescale` (сейчас механизм работает только в slow-motion).
- Убрать busy-wait последней миллисекунды.
- `pmove_fixed` / `pmove_msec` — их в SP-ветке нет вообще (grep даёт ноль), а `ClientThink` считает `msec` из разницы времён клиента. Это классический источник fps-зависимой физики: высота прыжка, страйф-разгон. Ввести фиксированный шаг pmove.

### 8.3 VFS и async I/O

Сейчас: ноль вхождений `aio_*`/`io_uring`/`OVERLAPPED`/`mmap`. `FS_ReadFile` зовёт `S_ClearSoundBuffer()`, чтобы звук не заикался, — диагностический признак того, что чтение блокирует главный поток.

Плюс реальный баг: `pack_t` держит **один** `unzFile handle` на весь сеанс, и при `uniqueFILE == qfalse` он отдаётся напрямую (`files.cpp:1341`) — два одновременно открытых не-unique файла из одного pk3 разрушают друг другу состояние zip-стрима.

Целевое: VFS поверх job system, async-чтение, декомпрессия в воркерах, mip-генерация и тангенты — тоже в воркерах.

**Офлайн-компиляция ассетов** (id Tech 5) — опционально и позже: конвертер `pk3 → .jkpak` с предпосчитанными тангентами, mip-цепочками в BC7, валидированными BSP-лампами. Побочный эффект — часть P0-уязвимостей закрывается самим фактом валидации на этапе сборки. Но исходные pk3 обязаны читаться напрямую всегда.

### 8.4 Звук

Сейчас `USE_OPENAL` включается **только на MSVC 32-бит** (`snd_local.h:33`). Все 64-битные сборки играют через программный микшер 1999 года с ресемплингом «шаг по индексу» без интерполяции. Плюс мёртвая EAX-ветка с `EaxMan.dll` (бинарник лежит в репозитории).

Целевое: единый **OpenAL Soft** с HRTF и EFX-реверберацией (ровно то, ради чего когда-то был EAX), `minimp3` вместо собственного MP3-декодера (10 094 LOC), `S_SetListenerMedium()` с low-pass для подводного звука.

### 8.5 Безопасность

Полный список — аудит §4. Из него P0, которые надо закрыть **до первого публичного билда**:

1. Валидация BSP: `ident`, сверка всех `lump_t.fileofs/filelen` с длиной файла, валидация всех индексов (`planeNum`, `shaderNum`, `firstSide/numSides`, `leafbrushes`, `leafsurfaces`).
2. `tr_bsp.cpp:489` — проверка `patchWidth`/`patchHeight` против `MAX_PATCH_SIZE`. **Стековый оверфлоу, RCE через кастомную карту.** Одна строка.
3. Сверка `ofsEnd`/`ofs*` в MD3/GLM/GLA с реальной длиной файла.
4. Границы в `LoadTGA`, RoQ, `ojk_saved_game`.
5. Убрать `Sys_LoadMachOBundle` (dlopen из pk3); homepath не первым в порядке поиска DLL.
6. Фаззинг загрузчиков (libFuzzer) в CI.
7. ASan/UBSan в CI — опции в CMake уже есть, но не включены ни в одном workflow.

---

## 9. Фазовый план

> **Статус на 11 августа 2026.** Фазы 0 и 1 закрыты
> ([Phase1-Report.md](Phase1-Report.md)). Фаза 2 сделана структурно и находится
> в пункте 2.10 — bring-up по кампаниям. Подробности ниже, по пунктам.
>
> **Где мы на самом деле:** движок впервые запущен на настоящем железе и
> настоящих ассетах. Windows, дискретная видеокарта, ретейловая установка Jedi
> Academy. Меню поднимается, рисуется нашими SDF-шрифтами, кликается. Кампания
> пока падает — за один день найдено и починено три падения подряд на первой же
> загрузке карты, все одной природы (см. Backlog §13), четвёртое ждёт проверки.
>
> Это смена режима работы, а не просто ещё один пункт. До вчерашнего дня всё
> проверялось headless на lavapipe: шейдеры компилируются, слои валидации молчат,
> `local.sh` зелёный — и ни одного кадра, увиденного глазами. За сутки на реальном
> железе нашлось больше дефектов, чем за предыдущие недели, и ни один из них
> headless-прогон найти не мог: PBR-ветка на lavapipe вообще не исполняется
> (`maxBoundDescriptorSets` 8 против нужных 11), а карта в смоук-тесте не
> грузится.
>
> **Отклонение от плана, которое надо помнить:** `rd-vanilla` удалён (`58a75a0`)
> **до** того, как обе кампании стали проходиться, хотя выход из фазы 2 требовал
> обратного порядка. Решение сознательное — модуль тянул за собой границу,
> которую мы убирали, — но эталон для попиксельного сравнения потерян. Сравнивать
> теперь не с чем, и это делает пункт 2.10 дороже, чем он был запланирован.

Принцип: **на каждой фазе есть запускаемый билд**. Дорогие и инвазивные вещи (render graph, bindless) делаются там, где их можно проверить на целевом контенте.

### Фаза 0 — Основание (3–4 нед)

- Новый репозиторий: OpenJK `code/` + `shared/` + `lib/`, `codemp/` удалён (−508 908 LOC), `codeJK2/game` сохранён.
- `rd-vulkan@pbr` трансплантирован как `render/` — **пока не собирается**, это нормально.
- CMake: единый монолитный таргет, `CMAKE_CXX_STANDARD 20`, `-Wextra`, отдельный `-Werror`-таргет, Windows x64 + Linux x64. Удалены x86, macOS, dedicated, XP-toolset.
- CI: Win + Linux, ASan + UBSan-джобы, валидационные слои Vulkan на lavapipe.
- Регресс-харнесс: автопрохождение `t1_sour → t3_bounty` (JKA) и `kejim_post → doom` (JK2) с сейв/лоадом на каждом уровне, автоматический сбор крашей и скриншотов для попиксельного сравнения.
- Чистка: `data.spv`, закоммиченные `.exe`-тулы, вендоренный `vulkan/`, `EaxMan.dll`, `OpenAL32.dll`.

**Выход:** репозиторий, CI, харнесс. `rdsp-vanilla` пока ещё собирается и работает — это эталон.

### Фаза 1 — Дешёвая модернизация Vulkan на живом MP-билде (5–6 нед)

Делается **в параллельном форке EternalJK**, который запускается прямо сейчас на ассетах JKA MP. Всё верифицируется немедленно.

volk → VMA → персистентный pipeline cache → sync2 → debug_utils/валидация везде → кроссплатформенный шейдерный тулчейн → spec-constants (600 → ~135 блобов).

**Выход:** `rd-vulkan`, готовый к трансплантации, без 61 МБ в git и без Windows-only тулчейна. Результаты фазы черри-пикаются в новый репозиторий.

> Почему не сразу в SP: эти работы механические и требуют возможности **запустить и увидеть**. MP-билд это даёт сегодня, SP-билд — только через 4 месяца.

### Фаза 2 — Bring-up в SP + монолит (14–18 нед) ← главный риск

Порядок внутри критичен:

| # | Работа | Оценка | Статус |
|---|---|---|---|
| 2.1 | `tr_types.h`: MP-раскладка → SP. Выкинуть `miniRefEntity_t`, `RT_ENT_CHAIN`, `RT_ORIENTEDLINE`; вернуть `RT_LATHE`, `RT_CLOUDS`. Перенумеровать `RF_*`/`RDF_*` (у них **разные значения** в SP и MP). Пройтись по всем `e.uRefEnt`/`e.sprite`/`e.line`/`e.bezier` | 2–3 нед | сделано |
| 2.2 | Монолит: удалить `GetRefAPI`/`Sys_LoadDll`/`REF_API_VERSION`, заинлайнить 75 `ri.*` (~800 сайтов) и 157 `re.*` (532 сайта). Переименовать локальную `orientationr_t ri` (~180 конфликтов) в `oriR`. Удалить `PD_Store`/`PD_Load`, `CM_*CachedMapDiskImage`, `GetCurrentVM`/`CGVMLoaded`/`GetSharedMemory` | 2 нед | сделано |
| 2.3 | Ghoul2: вынести `G2_*` (10 930 строк) **из рендерера** в `engine/`. Взять SP-версии `G2_bones/misc/bolts/surfaces` практически как есть — дельта рендерера к своей базе всего 28–220 строк на файл. `tr_ghoul2.cpp` брать из `rd-vulkan`. Убрать `ri.SV_Trace` из рендерера | 3 нед | сделано |
| 2.4 | SP World Effects API поверх GPU-погоды: `IsOutside`, `IsOutsideCausingPain`, `IsShaking`, `GetWindVector`, `GetChanceOfSaberFizz`. Нужен point-in-brush тест, которого в рантайме нет | 2–3 нед | не начато |
| 2.5 | Портировать `tr_draw.cpp`-эквивалент, SP-типы поверхностей (`Cone`/`Cylinder`/`Electricity`/`SaberGlow`/`Lathe`/`Clouds`), дезинтеграцию | 2 нед | сделано |
| 2.6 | Ghoul2 gore | 1,5–2,5 нед | сделано |
| 2.7 | Image manager: SP и MP имеют разные; свести к MP-версии с PBR-расширениями | 1,5 нед | частично: счётчик уровней починен (Backlog §18), выгрузка изображений ждёт фазы 5 |
| 2.8 | Cvar-совместимость: набор Quake3e ≠ ванильный JKA, SP-меню ждёт ванильных имён | 1 нед | частично |
| 2.9 | JK2: `OldToNewRemapTable[72]` (ремап костей JK2→JKA), проверка ассетов | 1 нед | не проверено |
| 2.10 | Bring-up и отладка по обеим кампаниям | 3–5 нед | **идёт сейчас** |

**Митигация:** ~~`rdsp-vanilla` держится рабочим параллельно как эталон до самого
конца фазы; попиксельное сравнение через харнесс из фазы 0.~~ **Не соблюдено.**
`rd-vanilla` удалён в `58a75a0`, раньше срока и сознательно: он держал ту самую
границу модуля, которую снимал пункт 2.2, и тащить его дальше значило тащить и
её. Цена — эталона для сравнения нет, и всё, что раньше проверялось бы одним
скриншотом, теперь проверяется чтением кода и запуском на живом железе.

**Выход:** JK2 и JKA проходятся на Vulkan. ~~Только после этого удаляем
`rd-vanilla`.~~

**Чего не хватает, чтобы 2.10 шёл быстрее.** Сейчас цикл выглядит так: собрать,
передать сборку человеку с видеокартой и ретейловыми ассетами, получить скриншот
или `jkx_crash.txt`, прочитать, починить, повторить. Один оборот — часы. Всё, что
сокращает оборот, окупается немедленно, и сегодняшний день это показал дважды:
`.pdb` рядом с exe превратил двенадцать шестнадцатеричных чисел в имя функции и
номер строки, а одна печатная строка в `UI_SaberDrawBlade` за один запуск отсекла
половину гипотез. Диагностика здесь дешевле догадок, и это стоит считать частью
работы, а не накладными расходами.

### Фаза 3 — Ядро движка (8–10 нед, параллелится с фазой 2)

Job system → арены → монотонный таймер и фиксированный pmove → VFS с async I/O → P0-безопасность и фаззинг → 64-битные фиксы (сейвы, `long` в ABI, `%x` с указателями) → OpenAL Soft.

### Фаза 4 — Render graph + dynamic rendering + timeline + MT-запись (8–10 нед)

Делается **в SP-дереве**, где можно проверить на целевом контенте. 29 render pass + 44 framebuffer → граф. Layered cubemap-бейк через `viewMask` вместо geometry shader. Многопоточная запись командных буферов через job system.

### Фаза 5 — Bindless + EDS3 (6–8 нед)

Bindless (снимает баг с `maxBoundDescriptorSets < 11`, при котором PBR молча выключается) → сокращение перестановок до ~25 блобов → EDS3 + сжатие ключа пайплайна (~800–1500 → ~60–100 объектов).

### Фаза 6 — Миграция cgame → рендерер (8–10 нед)

Post-fx stack → **underwater pass** → декали → lightstyles → `CBezier`/`CTrail`/`CPoly` → сабля → Force Sight / LA Goggles → dissolve/cloak → skybox-портал как view-нода.

### Фаза 7 — GPU-частицы (6–8 нед)

Требует фазы 4 (граф, depth как ресурс, async compute) и п.6 фазы 6.

### Фаза 8 — Графические фичи (16–20 нед)

Порядок по соотношению «эффект / стоимость» именно для JK2/JKA:

1. **Tone mapping + linear workspace** (2–3 нед) — **первым**. Сейчас `r_hdr` это просто `R16G16B16A16_UNORM` без tone mapping; без него PBR работает не в своём режиме. ACES уже написан.
2. **Diffuse IBL** (0,8 нед) — слот и код бейка готовы, нужен биндинг.
3. **Depth pre-pass** (2 нед) — прототип `28a48a4` есть; надо доделать MSAA и alpha-test.
4. **Кэш IBL-бейка на диск** (1 нед).
5. **Clustered Forward+** (6–8 нед) — свет и декали в кластерах.
6. **CSM для солнца** (6–8 нед) — главный визуальный апгрейд outdoor-карт.
7. **SSAO** (4 нед) — поверх depth pre-pass.
8. **HDR lightmap** (3–5 нед) — перезапекание в q3map2 с `-hdr` либо остаться на RGBM.
9. Point shadows, parallax, specular AO, Burley diffuse, multiscatter GGX (3–4 нед).

### Фаза 9 — GPU-driven и полировка

GPU culling (compute + indirect + HiZ two-phase) — **последним**: в SP культинг сейчас не главный потребитель CPU. Ragdoll → Jolt Physics. Профилирование, кросс-вендорная валидация.

### Сводный бюджет

| Фаза | Содержание | Чел-недели |
|---|---|---|
| 0 | Основание, CI, харнесс | 3,5 |
| 1 | Дешёвая модернизация Vulkan (на MP-билде) | 5,5 |
| 2 | **Bring-up в SP + монолит** | 16 |
| 3 | Ядро: job system, арены, VFS, безопасность, звук | 9 |
| 4 | Render graph + dynamic rendering + timeline + MT | 9 |
| 5 | Bindless + EDS3 | 7 |
| 6 | Миграция cgame → рендерер | 9 |
| 7 | GPU-частицы | 7 |
| 8 | Графические фичи | 18 |
| 9 | GPU-driven, Jolt, полировка | 8 |
| — | Буфер 15 % | 14 |
| | **Итого** | **~106 чел-недель** |

**Один инженер full-time: ~24 месяца.** Фазы 3 и 4 хорошо параллелятся с 2, 5 с 6, 8 частично сама с собой.

**Два инженера (разделение «Vulkan-бэкенд» / «SP-фронтенд и геймкод»): ~12–14 месяцев.**
**Три (+ третий на ядро, безопасность, инструменты): ~9–11 месяцев.**

**Промежуточные вехи, каждая — играбельный билд:**

| Веха | После фазы | Что получаем |
|---|---|---|
| M1 | 1 | Vulkan-рендерер без техдолга тулчейна, в MP-форке |
| M2 | 2 | **Обе кампании проходятся на Vulkan.** Самая важная веха |
| M3 | 4 | Render graph, MT-запись, async compute |
| M4 | 6 | Настоящий underwater, post-fx stack, cgame похудел на 16k строк |
| M5 | 8.1–8.4 | Tone mapping + diffuse IBL — PBR наконец работает как задумано |
| M6 | 8 | CSM, SSAO, Forward+ — визуальный паритет с современными форками и выше |

---

## 10. Порядок работ на первые две недели

1. Собрать `EternalJK@origin/pbr` под Windows и Linux, запустить на ассетах JKA MP. Замерить FPS, время старта, время загрузки карты, прогнать RenderDoc. **Go/no-go принимается здесь** — автор ветки сам пишет в коммите `313d852`: *«This is part of larger refactor, stability is not guaranteed»*. → **см. отчёт [Step1-PBR-Build-Report.md](Step1-PBR-Build-Report.md), Linux-часть выполнена**
2. Поднять новый репозиторий: OpenJK как основа, `codemp/` удалён, `rd-vulkan@pbr` положен рядом. Пока не собирается — это ожидаемо.
3. CMake на C++20, монолитный таргет, CI Win+Linux, ASan/UBSan-джоба + Vulkan-валидация на lavapipe (проверено: baseline 1.3 полностью поддержан программным драйвером).
4. Закрыть `tr_bsp.cpp:489` (проверка `patchWidth`/`patchHeight`) — одна строка, закрывает RCE.
5. Начать фазу 1 с volk + VMA + pipeline cache: три недели механической работы с немедленно проверяемым результатом.

---

## 11. Риски и что с ними делать

| Риск | Вер. | Влияние | Митигация |
|---|---|---|---|
| **Ветка `pbr` нестабильна** — автор предупреждает открытым текстом | Средняя | Высокое | Фаза 0/шаг 1 — разведка с замерами до всех коммитов. Запасной план: база на `master`-ветке EternalJK + ручной перенос PBR (+3–4 нед) |
| **Bring-up в SP затянется** — это 16 из 106 недель и главная неизвестная | **Высокая** | **Высокое** | `rdsp-vanilla` рабочим до конца фазы 2; попиксельный харнесс; дельта Ghoul2 к своей базе всего 28–220 строк на файл — это главный источник оптимизма |
| **PBR испортит вид ванильных карт** | Высокая | Среднее | `r_pbr 0` — не опция, а требование. Приёмка: попиксельное сравнение с `rdsp-vanilla` на 20 контрольных точках кампании |
| **EDS3 не поддержан на части железа** | Высокая | Низкое | Fallback-матрица по фичам заложена в дизайн ключа пайплайна с самого начала |
| **Bindless ломает шейдеры массово** | Средняя | Высокое | Делать после render graph и после сокращения перестановок — иначе перекомпилировать 600 блобов вместо 25 |
| **Сейвы ломаются** | Средняя | Высокое | Версионировать формат сейва **до** любых изменений структур (сейчас это сырой дамп `gentity_t`) |
| **World Effects API окажется сложнее** — это геймплей, живущий в рендерере | Средняя | Среднее | Резерв. В крайнем случае — временно оставить CPU-путь погоды для этих запросов |
| **Job system упрётся в ragdoll-статики** | Средняя | Низкое | Расшивка блокеров вынесена в отдельную задачу фазы 3, до распараллеливания анимации |
| **Объём проекта** — 106 чел-недель для одного это 2 года | — | — | Вехи M1–M6 дают играбельный билд на каждой. M2 (обе кампании на Vulkan) достигается за ~25 недель |

**Юридическое.** Всё дерево — **GPLv2 без «or later»** (Raven/Activision 2013 + Quake III + Quake3e + EternalJK). Проект обязан быть GPLv2; закрыть исходники или уйти в MIT/Apache нельзя.

**Атрибуция.** Файлы `vk_*.cpp` в EternalJK несут шапку id/Raven/OpenJK, хотя код на 60–80 % происходит из Quake3e. При форке восстановить корректную атрибуцию: id Software + ec-/Quake3e + kennyalive + JKSunny. Это и правильно, и снимает риск претензий.

---

## Приложение: точки входа для фазы 1

```
# Vulkan-фундамент
codemp/rd-vulkan/vk_instance.cpp:238        apiVersion → VK_API_VERSION_1_3
codemp/rd-vulkan/vk_instance.cpp:60-170     107 PFN_vk* → volk
codemp/rd-vulkan/vk_instance.cpp:554-583    одна queue family → +transfer, +compute
codemp/rd-vulkan/vk_image.cpp:1273-1350     chunk-аллокатор → VMA
codemp/rd-vulkan/vk_attachments.cpp:48-125  пул аттачментов → VMA
codemp/rd-vulkan/vk_init.cpp:594-597        pipeline cache без персистентности
codemp/rd-vulkan/vk_init.cpp:517            useFastLight при maxBoundDescriptorSets < 11 (баг)
codemp/rd-vulkan/vk_local.h:52-60           валидация только Win+Debug, debug_report
codemp/rd-vulkan/vk_cmd.cpp:96              vkQueueWaitIdle на каждой одноразовой операции
codemp/rd-vulkan/vk_cubemap.cpp:342,351,397 барьеры с stage-масками 0,0 → ALL_COMMANDS

# Render graph (фаза 4)
codemp/rd-vulkan/vk_frame.cpp:112-609       vk_create_render_passes (~500 строк, удаляется)
codemp/rd-vulkan/vk_frame.cpp:610-828       vk_create_framebuffers (~220, удаляется)
codemp/rd-vulkan/vk_frame.cpp:1003-1170     10 обёрток begin_render_pass → VkRenderingInfo
codemp/rd-vulkan/vk_local.h:482-489         renderPass_t, RENDER_PASS_COUNT=6 → 1

# Шейдеры (фаза 1)
codemp/rd-vulkan/shaders/tools/compile_threaded.cpp   549 строк Windows-only
codemp/rd-vulkan/shaders/spirv/shader_data.c          61,7 МБ / 1,9 млн строк в git
codemp/rd-vulkan/shaders/glsl/global.h                253 строки — общий C/GLSL (сохраняем)
codemp/rd-vulkan/shaders/glsl/common/pbr.glsl         147 строк GGX/Smith/Schlick/IBL

# Прототипы для черри-пика
origin/prototype-pbr-bindless        9bf4fc6   vk_bindless.cpp (708), texture.h (216)
origin/prototype-master-...z-prepass 28a48a4   depth prepass; 832e1bf dglow как MRT
origin/prototype-beta-model-instancing 15ac1b4 vk_buffer.cpp (498), entity/bones UBO→SSBO

# cgame → рендерер (фаза 6)
code/cgame/cg_view.cpp:1284-1302     underwater FOV-варп (удаляется)
code/cgame/cg_draw.cpp:3564-3903     CG_Draw2DScreenTints, 340 строк, 6 fade-машин
code/cgame/cg_marks.cpp              весь файл, 287 строк
code/cgame/FxPrimitives.cpp:2310     CFlash — «fullscreen» вспышка через спрайт на znear
code/cgame/FxPrimitives.cpp:2129     CBezier с static vec3_t lastEnd[2] внутри метода
code/cgame/FxSystem.cpp:190          G2API_GetBoltMatrix на частицу на кадр
```
