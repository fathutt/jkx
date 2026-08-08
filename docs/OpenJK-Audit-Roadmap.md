# OpenJK: технический аудит и план разработки SP-движка для Jedi Outcast / Jedi Academy

**Дата:** 8 августа 2026
**Аудируемая ревизия:** `JACoders/OpenJK` @ `1a6a643427aa` (master, 11.07.2026)
**Дополнительно проанализировано:** `JKSunny/EternalJK` @ `b500596` (master) и ветки `pbr` / `rtx-update`, `JKSunny/OpenJK`, `ec-/Quake3e`
**Целевые платформы:** Windows x64, Linux x64
**Ограничения проекта:** совместимость с оригинальными ассетами обязательна; ABI модов можно ломать; мультиплеер вырезается

---

## 0. Резюме для нетерпеливых

**Что нашли:**

1. **Ключевой факт, определяющий весь проект:** современных рендереров для JK2/JKA существует два — `rd-rend2` (OpenGL 3.2, PBR/IBL/CSM/SSAO/tonemap) и `rd-vulkan` (Vulkan 1.1, из EternalJK). **Оба живут только в мультиплеерном дереве `codemp/`.** В single-player (`code/`) существует ровно один рендерер — `rd-vanilla`, это **fixed-function OpenGL 1.x** с `glBegin`/client arrays и нулём VBO. То есть «вырезать MP и оставить rend2» невозможно буквально: rend2 придётся сначала **перенести** в SP.

2. **Порт рендерера в SP — это 60–80 % всей работы проекта**, и это не графика, а API-слой: `refexport_t`/`refimport_t` в SP и MP разошлись на ~30 % (51 SP-only функция против 65 MP-only), `refEntity_t` несовместим на уровне ABI (разный порядок enum, разные значения `RF_*`), Ghoul2 структурно разъехался (`ghoul2_shared.h` — 811 строк в SP против 336 в MP).

3. **Узкое место SP сегодня — CPU, а не GPU.** Три главных потребителя: (а) полное отсутствие VBO — вся мировая геометрия копируется в `tess` и уезжает в драйвер через `qglVertexPointer` **каждый кадр**; (б) 100 % CPU-скиннинг Ghoul2; (в) **второй, независимый CPU-скиннинг всей модели на каждый коллизионный трейс** — самый тяжёлый и самый недооценённый узел. Перенос на GPU даёт кратный выигрыш, и почти всё это приходит «бесплатно» вместе с портом современного рендерера.

4. **Безопасность — плохо.** Ни один lump BSP не валидируется по длине файла; `drawVert_t points[1024]` на стеке заполняется по `patchWidth × patchHeight` **прямо из файла карты** (`tr_bsp.cpp:489`) — прямой стековый оверфлоу и RCE через кастомную карту. Найдено ~15 дефектов того же класса. Для проекта, который будет распространяться, это блокер релиза.

5. **Многопоточности в SP-движке нет вообще** — ноль вхождений `std::thread`/`SDL_CreateThread`/`pthread_create` во всём `code/` и `shared/`. Всё в одном потоке: I/O, декодирование MP3, загрузка текстур, `dlopen`, рендер.

6. **Проект собирается** современным GCC 13 без ошибок (проверено), но с 175 предупреждениями на `-Wall`, среди которых реальные дефекты: `-Warray-bounds` в `tr_world.cpp:416` и `cg_players.cpp:6521`, `-Wdangling-pointer` в `md4.cpp:185`, 5 × `-Wformat-overflow` в `g_roff.cpp`.

**Что рекомендуем (кратко):**

Взять за основу рендерера **ветку `pbr` форка `JKSunny/EternalJK`** (Vulkan + PBR/IBL уже написаны и работают) и портировать её в SP-дерево, вместо того чтобы проектировать RHI-абстракцию и писать Vulkan-бэкенд с нуля. Экономия — примерно **12–18 человеко-месяцев**. Подробное обоснование — раздел 9.

---

## 1. Что такое OpenJK сегодня

### 1.1 Размер и топология

```
code/       423 080 LOC   — движок и геймкод Jedi Academy SP (+ JK2 SP через -DJK2_MODE)
codemp/     597 230 LOC   — движок и геймкод Jedi Academy MP (вкл. rd-rend2)
codeJK2/    185 057 LOC   — ТОЛЬКО геймкод JK2 SP (game/, cgame/, icarus/); движка нет
shared/      10 978 LOC   — общий слой: sys/, sdl/, qcommon/safe/
lib/        145 233 LOC   — бандлы: SDL2 2.0.12, zlib, libpng, jpeg-9a, minizip, gsl-lite
───────────────────────────
итого     ~1 361 578 LOC
```

**Важно про JK2:** отдельного JK2-движка не существует. `openjo_sp` — это тот же `code/`, собранный с `-DJK2_MODE`. В `code/` **376 вхождений `#ifdef JK2_MODE`**. `codeJK2/` содержит только игровую логику (193 файла). Это хорошая новость: поддержка обеих игр в одном движке уже реализована и её надо просто сохранить.

### 1.2 Артефакты сборки

| Бинарник | Из чего | Примечание |
|---|---|---|
| `openjk_sp.x86_64` | `code/{qcommon,client,server,ui,icarus,mp3code}` + `shared/` | UI слинкован статически |
| `openjo_sp.x86_64` | то же + `JK2_MODE` | JK2 SP |
| `rdsp-vanilla_x86_64.so` | `code/rd-vanilla` + `code/rd-common` | Рендерер — отдельная DLL через `GetRefAPI` |
| `rdjosp-vanilla_x86_64.so` | то же + `JK2_MODE` | |
| `jagamex86_64.so` | `code/game` + **54 файла `code/cgame`** + `icarus` + `Ratl/Ragl/Ravl/Rufl` | **game и cgame в одной библиотеке** |
| `jospgamex86_64.so` | `codeJK2/` | JK2-геймплей |
| `openjk.x86_64`, `openjkded`, `jampgame`, `cgame`, `ui` | `codemp/` | **всё это удаляется** |

### 1.3 Активность проекта

| Год | Коммитов (в поверхностном клоне) |
|---|---|
| 2023 | 57 |
| 2024 | 40 |
| 2025 | 31 |
| 2026 | 9 (последний — 11.07.2026) |

Активные контрибьюторы за 2023–2026: Daggolin (42), SomaZ (30), razor (24), Ensiform (8), taysta (6). Проект **жив, но в режиме поддержки**: фиксы утечек, обновления libpng, CI-конфиги. Крупных архитектурных изменений нет. Это значит: форк не будет догонять быстро уезжающий апстрим — мерж-долг будет умеренным.

### 1.4 Стандарт языка и стиль

`CMakeLists.txt:262`: `set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")` — применяется **только к GCC/Clang**. Для MSVC `/std:` не задаётся вообще → фактически C++14 по умолчанию VS2017+. `CMAKE_CXX_STANDARD` / `target_compile_features` не используются.

Итог: **C++11 на Unix, C++14 на Windows, никакой явной политики.** Современных фич почти нет — `nullptr|auto|override|constexpr|= delete|unique_ptr|std::move` по всему `qcommon`+`client`+`server` дают **31 вхождение**.

Диагностика заглушена: `-Wno-invalid-offsetof`, `-Wno-write-strings`, `-Wno-comment`, `_CRT_SECURE_NO_WARNINGS`, `_SCL_SECURE_NO_WARNINGS`. `-Wall` есть, `-Wextra`/`-Werror` — нет.

Слои кода по возрасту:

1. **Ядро id/Raven** — чистый C в `.cpp`-файлах, венгерская нотация Raven (`iSize`, `psFilename`, `bZeroit`, `gbUsingCachedMapDataRightNow`).
2. **MFC/VC6-измы** — `code/win32/afxres.h` (заголовок MFC из Visual C++ 6), `#pragma hdrstop`, PCH `exe_headers.h`, 31 `#pragma warning`.
3. **Icarus** (скриптовая VM) — C++98 с перегруженными `operator new/delete` на каждом классе, роутящими в `IGameInterface::GetGame()->Malloc()`.
4. **Модернизация OpenJK** — `shared/qcommon/safe/` (`gsl-lite`, `cstring_span`, `limited_vector`) и `code/qcommon/ojk_*` (RAII, `std::vector`). **В ядро движка не проникли вообще** — используются в трёх файлах геймкода.

**Ratl / Ragl / Ravl / Rufl** — самописные библиотеки Raven 2002 года:

- `Ravl` — векторы/матрицы/боксы на классах (`CVec.h` — 1023 строки).
- `Ratl` — контейнеры со **статической ёмкостью как параметром шаблона** (`vector_vs`, `map_vs`, `pool_vs`, `hash_pool_vs`, ...). Определяет `operator new(size_t, TRatlNew*)` **в глобальном namespace** — загрязняет каждую TU.
- `Ragl` — графы и kd-дерево для навигации NPC.
- `Rufl` — `hstring` (интернированные строки), дублирует `code/qcommon/hstring.cpp`.

Используются в 8 файлах `code/game/AI_*.cpp`, `g_navigator.cpp`, `bg_vehicleLoad.cpp` и **в одном файле рендерера** — `code/rd-vanilla/tr_WorldEffects.cpp`. В `qcommon/`, `client/`, `server/` их нет.

**Вердикт:** `Ravl` и `Ratl` подлежат замене (`Q::LimitedVector` из `shared/qcommon/safe/limited_vector.h` — уже готовая замена с тестами). Начинать надо с `tr_WorldEffects.cpp` — это отвяжет рендерер от геймкодового дерева. `Ragl` — переписывать вместе с навигацией, отдельный проект. Приоритет — низкий, это не блокер.

---

## 2. Рендереры: полная карта

Это центральный раздел аудита. Все три существующих рендерера — разные кодовые базы, а не режимы одного.

### 2.1 `code/rd-vanilla` — то, что сейчас в SP

**Это OpenGL 1.x с ARB-программами.**

```c
// code/rd-vanilla/tr_shade.cpp:77
qglBegin(GL_TRIANGLE_STRIP);
// tr_shade.cpp:343,370,895,980
qglVertexPointer(3, GL_FLOAT, 16, tess.xyz);      // client-side arrays
// tr_shade.cpp:180,345,372,981
qglLockArraysEXT(...)                              // GL_EXT_compiled_vertex_array, 1999 год
```

- **Ни одного `glGenBuffers` во всём дереве.** VBO не используются нигде.
- **Ни одного GLSL-шейдера.** Освещение — `tr_arb.cpp` (192 строки, ARB vertex/fragment program) плюс CPU-вычисления в `tr_shade_calc.cpp`.
- Контекст создаётся **без запроса версии/профиля** → compatibility.
- Тени — stencil volumes (`edgeDefs[1000][32]`, 4 прохода `R_RenderShadowEdges`).
- Погода (`tr_WorldEffects.cpp`, 2187 строк) — все частицы интегрируются и рендерятся **на CPU каждый кадр**.
- Surface sprites, трава/листва (`tr_surfacesprites.cpp`, 1511 строк) — CPU-тесселяция каждый кадр.

Уникальные SP-фичи, которых нет ни в MP-vanilla, ни в rend2, ни в rd-vulkan:

```
code/rd-vanilla/tr_draw.cpp        1 025 LOC   2D-слой, Dissolve, LAGoggles, GetScreenShot, TempRawImage_*
code/rd-vanilla/tr_stl.cpp            78 LOC   загрузка STL-моделей
```

Плюс типы поверхностей, которых нет в MP: `RB_SurfaceCone`, `RB_SurfaceCylinder`, `RB_SurfaceElectricity`, `RB_SurfaceSaberGlow`, `RB_SurfaceLathe`, `RB_SurfaceClouds` (все в `code/rd-vanilla/tr_surface.cpp`). Плюс эффекты дезинтеграции (`RB_CalcDisintegrateColors:1230`, `RB_CalcDisintegrateVertDeform:1351`).

### 2.2 `codemp/rd-rend2` — OpenGL 3.2 core, самый богатый по фичам

**~92 600 LOC (66 500 своего C++ + 4 254 GLSL + сторонние `stb_image`/`glext`/MikkTSpace).**

#### Что сделано хорошо

**Persistent-mapped ring buffers + fence на кадр** — грамотная современная схема:

```c
// tr_cmds.cpp:507  RE_BeginFrame
gpuFrame_t *thisFrame = &backEndData->frames[frameNumber % MAX_FRAMES];   // MAX_FRAMES = 2
if (thisFrame->sync) {
    qglClientWaitSync(sync, 0, 0);   // ждём GPU
    thisFrame->uboWriteOffset = 0;
    thisFrame->dynamicIboCommitOffset = thisFrame->dynamicIboWriteOffset = 0;
    backEndData->perFrameMemory->Reset();
}
```

На кадр: UBO 8 МБ, dynamic VBO 12 МБ, dynamic IBO 4 МБ, линейный аллокатор 32 МБ.

**World VBO** (`R_CreateWorldVBOs`, `tr_bsp.cpp:2043`) — вся BSP-геометрия в мега-буферах по 64 МБ VBO / 16 МБ IBO. **Статический мердж поверхностей** (`R_MergeLeafSurfaces`, `tr_bsp.cpp:3383`) объединяет поверхности одного кластера с общим шейдером. Рантайм-мердж в `glMultiDrawElements`.

**Почти вся per-vertex работа уже на GPU:**

| Задача | Где |
|---|---|
| `tcMod scale/scroll/stretch/rotate/transform` | 2 vec4-матрицы, `USE_TCMOD` |
| `tcGen` (lightmap/environment/vector/fog) | вершинный шейдер, `u_TCGen0` |
| `rgbGen` / `alphaGen` | `u_ColorGen`, `u_AlphaGen` |
| `deformVertexes wave/normal/bulge/move/projectionShadow` | `USE_DEFORM_VERTEXES` |
| fog | отдельный проход `fogpass.glsl` + `FogsBlock` UBO |
| **скиннинг Ghoul2** | `Bones` UBO, `mat3x4 matrices[72]`, `USE_SKELETAL_ANIMATION` |
| alpha test | `u_AlphaTestType` + `discard` |

**PBR — настоящий, не фейк** (`lightall.glsl`, 1188 строк):

```glsl
vec3 CalcSpecular(specular, NH, NL, NE, LH, VH, roughness) {
    vec3  F = F_Schlick(specular, VH);
    float D = D_GGX(NH, roughness);                    // Trowbridge-Reitz
    float V = V_SmithJointApprox(roughness, NE, NL);   // Heitz 2014
    return D * F * V;
}
```
Плюс cloth BRDF (`D_Charlie` + `V_Neubelt`), metallic-roughness ORMS-пайплайн, энергетическая нормировка.

**IBL — половина:** есть specular IBL (parallax-corrected prefiltered cubemaps, Karis split-sum, `prefilterEnvMap.glsl` с Hammersley + `ImportanceSampleGGX`, 256 сэмплов, env BRDF LUT 128×128 RGB16F). **Diffuse IBL / irradiance отсутствует полностью** — grep по `irradiance|SH|sphericalHarmonic` даёт ноль. Ambient берётся из запечённого lightmap'а.

> Это главный визуальный разрыв rend2: металлы и грубые поверхности получают specular-IBL, но не получают diffuse-IBL — освещение «раздваивается».

**Тени:** 3 каскада PSSM для солнца (`sunShadowFbo[3]`, стабилизация снапом по `worldUnitsPerTexel`), cube shadows для dlight'ов (`shadowCubeFbo[192]`, 9-точечный Poisson), projected pshadows (32 шт.), stencil volumes. Нет EVSM/VSM, нет per-cascade slope bias, нет cascade blending.

**Tone mapping:** Hable/Uncharted2 filmic активен; **ACES fitted написан, но не вызывается из `main()`** — готовый to-do на полчаса. Auto-exposure через `calclevels4x.glsl` (лог-яркостная пирамида).

**Погода на GPU через transform feedback** (`tr_weather.cpp`, 1258 строк): 30 000 частиц дождя / 10 000 снега, ping-pong VBO с `VBO_USAGE_XFB`, `GL_RASTERIZER_DISCARD` + `glBeginTransformFeedback`. Плюс ортографическая depth-карта сверху, чтобы частицы отсекались под крышами. Покрывает **18 из 19** команд SP-шного `tr_WorldEffects` (нет только `windzone`).

**Surface sprites на GPU** (`surface_sprites.glsl`, 256 пермутаций, данные пекутся в VBO при загрузке BSP).

Плюс: SSAO (выключен по умолчанию), dynamic glow / bloom (COD:AW-пирамида), sun rays с occlusion query, refraction (522 строки GLSL, 64 пермутации), IQM-формат, BC7/LATC компрессия, MikkTSpace-тангенты, GPU-таймеры.

#### Что сделано плохо

**Разделение frontend/backend фиктивно.** `RB_UpdateConstants()` вызывается из `RE_BeginScene()` (`tr_scene.cpp:496`) — то есть фронтенд делает GL-вызовы вне очереди команд. Рендер-потока нет (`grep r_smp|SMP|renderThread` → 0 совпадений) — это регресс даже относительно оригинального Q3.

**Дефект в ключе сортировки** (`tr_shade.cpp:571`):

```c
uint32_t RB_CreateSortKey( const DrawItem& item, int stage, int layer ) {
    uintptr_t shaderProgram = (uintptr_t)item.program;
    key |= (layer & 0xf) << 28;
    key |= (stage & 0xf) << 24;
    key |= shaderProgram & 0x00ffffff;     // младшие 24 бита УКАЗАТЕЛЯ
}
```
Плюс `std::sort` (не стабильная). Следствие: **порядок отрисовки прозрачных объектов зависит от адреса в памяти** — недетерминирован между запусками из-за ASLR. Артефакты альфа-блендинга воспроизводятся «через раз». Плюс `layer` клампится до 15, а `SS_NEAREST` = 16 — верхние sort-классы схлопываются.

**Противоречие в лимитах кубмапов:** код разрешает 128 кубмапов (`tr_bsp.cpp:3309`), а в sort key под индекс отведено **6 бит = 64**. Кубмапы с индексом ≥ 64 рендерятся с чужим окружением. Реальный баг.

**Дискового кеша шейдерных программ НЕТ.** `glGetProgramBinary` не встречается нигде. При старте компилируется и линкуется **~740 GLSL-программ** (`genericShader` 128 + `lightallShader` ~256 + `fogShader` 16 + `refractionShader` 64 + `spriteShader` 256 + ~20 одиночных), синхронно. На Mesa это единицы-десятки секунд, и это повторяется на каждом `vid_restart`.

**MD3 вершинная анимация в MP не работает вовсе.** `R_LoadMD3` (`tr_model.cpp:1090`) заливает в VBO только кадр 0; `LIGHTDEF_USE_VERTEX_ANIMATION` доступен только под `REND2_SP`, который нигде не определяется в CMake. **Для SP это блокер** — в SP много MD3-моделей с вершинной анимацией.

**Ghoul2 gore практически отсутствует:** `G2_gore_r2.cpp` — 49 строк, только деструктор. Логика под `#ifdef REND2_SP_MAYBE` не компилируется. В SP gore (следы от сабли на телах) — заметная механика.

**Никакого DSA, bindless, MDI, compute.** `glNamedBuffer*`, `glCreateTextures`, `glDispatchCompute`, `glMultiDrawElementsIndirect` присутствуют только как объявления в `glext.h` — ни одного вызова. Instancing: API есть, `numInstances` **всегда 1**.

`packedVertex_t` = **128 байт на вершину**, из них `colors[4]` как `vec4_t` float — 64 байта. Реалистичная упаковка даёт 60 байт, то есть **больше чем двукратная экономия bandwidth** на мировой геометрии.

Фиксированные лимиты с тихой деградацией:

| Константа | Значение | При переполнении |
|---|---|---|
| `MAX_DRAWSURFS` | 65536 | **молча теряет поверхности** (`tr_main.cpp:1801`) |
| `MAX_RENDER_COMMANDS` | 512 КБ | **молча дропает команды** (`tr_cmds.cpp:196`) |
| `SHADER_MAX_VERTEXES` | 1000 | `ri.Error(ERR_DROP)` |
| `Pass::maxDrawItems` | `numDrawSurfs*4` | `assert` + **тихий return в release** |
| `FRAME_VERTEX_BUFFER_SIZE` | 12 МБ | `assert` + return, `// TODO: Eh...resize?` |

### 2.3 `EternalJK/codemp/rd-vulkan` — Vulkan, из Quake3e

**~68 400 LOC** (18 248 — `vk_*` инфраструктура, 48 324 — `tr_*`/`G2_*`, 2 875 — GLSL).

#### Происхождение — подтверждено построчно

Это порт Vulkan-рендерера `ec-/Quake3e` (сам основан на `kennyalive/Quake-III-Arena-Kenny-Edition` → `suijingfeng/vkQuake3`). Сверка: `vk_create_swapchain`, `vk_find_pipeline_ext`, `vk_begin_frame`, `vk_alloc_pipeline` — совпадают телами функций. `Vk_Pipeline_Def` совпадает поле в поле, EternalJK добавил `vbo_ghoul2`, `vbo_mdv`, `surface_sprite_flags`.

**Важное отличие от Quake3e:** там всё в одном `vk.c` на 8022 строки; здесь разбито на **19 файлов** `vk_attachments`, `vk_bloom`, `vk_cmd`, `vk_debug`, `vk_dynamic_glow`, `vk_flares`, `vk_frame`, `vk_image`, `vk_image_process`, `vk_info`, `vk_init`, `vk_instance`, `vk_pipelines`, `vk_shade_geometry`, `vk_shaders`, `vk_swapchain`, `vk_vbo`, `vk_vbo_surfacesprites`. Это большой плюс для выборочного заимствования.

**Игровая часть — это порт `codemp/rd-vanilla`, не rend2.** Построчная сверка:

| файл | изменённых строк | всего |
|---|---|---|
| `tr_terrain.cpp` | **0** | 1039 |
| `tr_surfacesprites.cpp` | 28 | 1506 |
| `tr_WorldEffects.cpp` | 204 | 1888 |
| `tr_quicksprite.cpp` | 161 | 190 |
| `tr_ghoul2.cpp` | 742 | 5145 |
| `tr_shade.cpp` | 1954 | 179 (выпотрошен — логика ушла в `vk_shade_geometry.cpp`) |

То есть **вся JKA-специфика (погода, трава, quicksprite) уже портирована на Vulkan** и требует всего 28–204 строк правок каждая — это механическая замена `GL_State`/`GL_Bind` на `vk_bind_pipeline`/`vk_bind`.

#### Vulkan-слой: что есть

- **Vulkan 1.1** (fallback 1.0), bundled headers **1.4.356**. Консервативно, но работает везде от Intel HD до MoltenVK.
- Кадров в полёте: **2**. Per-frame semaphore + fence, **плюс отдельный `rendering_finished` семафор на каждый образ swapchain** — правильный фикс классического UB при `MAILBOX`.
- ~25 объектов `VkRenderPass`: main, gamma, screenmap, capture, refraction.extract, bloom.{extract, blur×8, blend}, dglow.{...}. Все single-subpass.
- **Кеш пайплайнов в памяти** — линейный `memcmp`-поиск по `Vk_Pipeline_Def` в массиве на 2304 слота, каждый слот держит 5 `VkPipeline` (по одному на render pass), создаваемых лениво.
- **Динамических состояний ровно три:** viewport, scissor, depth bias. Всё остальное (блендинг, cull, depth-func, alpha-test, топология) зашито в пайплайн → комбинаторный взрыв.
- **Specialization constants** используются — `hw_fog`, `SurfaceSpritesData{kFaceCamera, kFaceUp, kFaceFlattened, kFxSprite, kAdditive, kUseFog}`. Правильный подход.
- **Аллокатор самописный, VMA нет.** Bump-аллокатор по чанкам 32 МБ (до 256 чанков = 8 ГБ потолок). Освобождения внутри чанка нет — при смене карты чанки сбрасываются целиком.
- Attachment'ы — отдельный аллокатор с попыткой `LAZILY_ALLOCATED` (правильно для tile-based GPU).
- Mip-цепочки генерируются **на CPU** (`R_MipMap`/`R_MipMap2` в `vk_image_process.cpp`), загружаются одним `vkCmdCopyBufferToImage` с массивом регионов.
- **`R_BuildMDXM`** (`vk_vbo.cpp:940`) — VBO для Ghoul2 **с GPU-скиннингом**: `boneMatrices[72]` в UBO, веса из `mdxmVertex_t`. Это за пределами Quake3e.
- **`vk_vbo_surfacesprites.cpp`** (833 строки) — **самая интересная часть форка**: полностью GPU-инстансированные surface sprites через SSBO + `vkCmdDrawIndexedIndirect`. Этого нет даже в rend2.

#### Vulkan-слой: чего нет

| Фича | Статус |
|---|---|
| `VK_KHR_dynamic_rendering` | ❌ (25 классических render pass) |
| `VK_KHR_synchronization2` | ❌ (`vkCmdPipelineBarrier`, `VkSubmitInfo` v1) |
| Descriptor indexing / bindless | ❌ на master; прототип в `prototype-pbr-bindless` |
| `VK_EXT_extended_dynamic_state` | ❌ |
| VMA | ❌ |
| volk | ❌ (ручные `PFN_*`, ~150 глобалов) |
| Multi-queue / async compute / async transfer | ❌ — **одна** graphics+present очередь |
| Secondary command buffers / MT-запись | ❌ — запись полностью однопоточная |
| Compute pipelines | ❌ на master, 1 в `pbr`, много в RTX-ветках |
| **Персистентный pipeline cache на диск** | ❌ — `initialDataSize = 0`, `vkGetPipelineCacheData` не вызывается **нигде** |
| Validation layers | только Windows + `_DEBUG`, через **депрекейтнутый** `VK_EXT_debug_report`; `VK_EXT_debug_utils` закомментирован |

**Шейдерный пайплайн — Windows-only.** `shaders/tools/compile_threaded.cpp` (577 строк) использует `<windows.h>`, `_beginthreadex`, `_findfirst`, `WaitForMultipleObjects`. `.bat`-скрипты зовут `VsDevCmd.bat` и `cl.exe`. **На Linux/macOS перекомпилировать шейдеры невозможно без переписывания тулзы.** Результат (194 SPIR-V-блоба) коммитится в репозиторий как `shader_data.c` — **14,6 МБ сгенерированного C**, в ветке `pbr` — 60+ МБ.

**Реальный дефект** (`tr_quicksprite.cpp:151`): цикл `for (i = 0; i < 6; i++)` записывает одни и те же 6 индексов в одни и те же слоты — 6× лишних записей на каждый спрайт в горячем пути (дождь/снег — тысячи вызовов за кадр). В `rd-vanilla` этого цикла нет.

#### Ветка `pbr` — вот где интересное

`git diff --numstat origin/master origin/pbr` за вычетом сгенерированного: **+8 613 / −4 502 строк**, то есть ~9k строк осмысленного кода. HEAD от 06.08.2026 — **самая активная ветка форка**.

Новые файлы:
```
vk_cubemap.cpp        489   IBL: irradiance + prefiltered env + BRDF LUT
vk_normalmap.cpp      334   compute-генерация normal map из albedo
vk_mikktspace.cpp     353   тангенты
shaders/glsl/common/pbr.glsl   148   GGX/Smith/Schlick + IBL + dlights
shaders/glsl/global.h          254   ОБЩИЙ C/GLSL заголовок
shaders/glsl/brdflut.frag       92
shaders/glsl/irradiancecube.frag 56
shaders/glsl/prefilterenvmap.frag 122
shaders/glsl/filtercube.{vert,geom} 28  geometry shader, 6 граней за проход
shaders/glsl/normalmap.comp      35
```

`global.h` — образцовый ход: макросы `STRUCT/VEC4/MAT4/PAD1`, компилирующиеся и в C, и в GLSL:

```c
STRUCT (
    VEC4 ( ambientLight ) VEC4 ( directedLight )
    VEC4 ( localLightOrigin ) VEC4 ( localViewOrigin ) MAT4 ( modelMatrix )
, vkUniformEntity_t )
```

Устраняет целый класс ошибок рассинхрона UBO-раскладок между C++ и шейдером. Это стоит перенять независимо от выбора стратегии.

**В `pbr` есть:** GGX/Smith/Schlick, **diffuse IBL (irradiance cube!)** — то, чего нет в rend2, prefiltered env map, BRDF LUT, MikkTSpace-тангенты, `.mtr`-материалы (`roughness`, `metallicRoughness`, `specularScale`, `normalScale`, `parallax`), compute-генерация нормалей.

**В `pbr` НЕТ** (проверено grep'ом по всей ветке): shadow maps, cascades, SSAO, deferred/G-buffer, tone mapping, auto-exposure.

#### Ветки RTX

`codemp/rd-vulkan/rtx/` — 29 файлов, **562 КБ** исходников. Порт NVIDIA Q2RTX: `vk_rtx_accel.cpp` (BLAS/TLAS), `vk_rtx_asvgf.cpp` + 7 compute-шейдеров денойзера, `vk_rtx_bsp.cpp` (78 КБ — конвертация BSP JKA в RT-представление), god rays, physical sky, tonemap. WIP, релизов нет, но объём работы огромный и реальный.

### 2.4 Сводная таблица

| | `rd-vanilla` SP | `rd-rend2` | `rd-vulkan` master | `rd-vulkan` pbr |
|---|---|---|---|---|
| LOC (без генерённого) | 58 685 | 88 322 | 68 400 | ~77 000 |
| API | **OpenGL 1.x** | OpenGL 3.2 core | **Vulkan 1.0/1.1** | Vulkan |
| VBO | ❌ вообще нет | ✅ world megabuffer | ⚠️ есть, `r_vbo 0` по умолчанию | ⚠️ |
| GPU-скиннинг Ghoul2 | ❌ | ✅ | ✅ (опц.) | ✅ |
| PBR (GGX/Smith/Schlick) | ❌ | ✅ | ❌ | ✅ |
| Specular IBL | ❌ | ✅ | ❌ | ✅ |
| **Diffuse IBL (irradiance)** | ❌ | **❌** | ❌ | **✅** |
| Sun shadows / CSM | ❌ | ✅ 3 каскада PSSM | ❌ | ❌ |
| Point shadows | ❌ | ✅ 32 | ❌ | ❌ |
| SSAO | ❌ | ✅ (off by default) | ❌ | ❌ |
| Tone mapping / auto-exposure | ❌ | ✅ | ❌ | ❌ |
| Normal / parallax mapping | ❌ | ✅ | ❌ | ✅ |
| Depth pre-pass | ❌ | ✅ | прототип | ❌ |
| Bloom / dynamic glow | glow only | ✅ | ✅ | ✅ |
| Погода | CPU (2187 LOC) | GPU (transform feedback) | Vulkan-порт CPU-версии | ✅ |
| Surface sprites | CPU (1511 LOC) | GPU (VBO-bake) | **GPU instancing + indirect** | ✅ |
| Quicksprite | ✅ | ❌ | ✅ | ✅ |
| `.mtr` материалы | ❌ | ✅ | ❌ | ✅ |
| MSAA | ⚠️ | ⚠️ off by default | ✅ до 64× + SSAA | ✅ |
| **Существует в SP-дереве** | **✅** | ❌ | ❌ | ❌ |
| Активно развивается | ❌ | ⚠️ SomaZ, редко | ✅ | ✅ (HEAD 06.08.2026) |

---

## 3. Аудит ядра движка

### 3.1 Память: hunk-аллокатора нет

`Hunk_Alloc` **объявлен и нигде не определён** (`code/qcommon/qcommon.h:700` — единственное вхождение). Функции hunk-аллокатора превращены в заглушки:

```c
// code/qcommon/common.cpp:693-750
void Com_InitHunkMemory(void){ Hunk_Clear(); }
void Com_ShutdownHunkMemory(void){}                 // пусто
void Hunk_SetMark(void){}                           // пусто
void Hunk_Clear(void){ Z_TagFree(TAG_HUNKALLOC); }  // тег вместо стека меток
```

То есть бамп-аллокатор id Tech 3 со стеком меток заменён на **тег в глобальном двусвязном списке всех аллокаций**. `Z_Malloc` (`z_memman_pc.cpp:252`) — просто обёртка над `malloc` с заголовком, магиком и вставкой в глобальный список; `Z_TagFree` — линейный проход по всему списку.

**Последствия:** полная потеря локальности данных уровня, фрагментация кучи, освобождение за O(n) вместо O(1). `SV_SpawnServer` вынужден вручную бороться с фрагментацией — там прямо есть вызов `Cvar_Defrag()` с комментарием «This frees, then allocates».

**Аварийное освобождение внутри аллокатора** (`z_memman_pc.cpp:271`): при неудаче `malloc` Z_Malloc последовательно выбрасывает BSP-кэш → звуки → текстуры → кэш моделей, и между попытками делает **`Sys_Sleep(1000)`**. Секундный столл внутри аллокатора с реэнтрантным вызовом рендерера и звука.

**`Z_Malloc(int iSize, ..., int iAlign)` — параметр выравнивания игнорируется** (`z_memman_pc.cpp:253`: `int /*unusedAlign*/`). Возвращается `malloc + sizeof(zoneHeader_t)`. На 32 бит это 20 байт → выравнивание по 4 байтам, при глобально включённом `/arch:SSE2` и `-msse2`. Любая выровненная SSE-загрузка по такому указателю — SIGSEGV. Мина замедленного действия.

Фиксированные пулы:

| Пул | Размер | Где |
|---|---|---|
| `CMiniHeap` Ghoul2 (сервер) | **256 КиБ**, бамп без освобождения | `sv_init.cpp:375` |
| `Ghoul2InfoArray` | 512 моделей | `G2_API.cpp:337` |
| `cvar_indexes` | 8192 | `cvar.cpp:34` |
| `MAX_FILE_HANDLES` | 64 | `files.cpp:273` |
| `va()` | 4 × 32 000 байт статики | `q_shared.cpp:658` |

При переполнении `CMiniHeap`: `Com_Error(ERR_DROP, "Ran out of transform space for Ghoul2 Models. Adjust G2_MINIHEAP_SIZE in sv_init.cpp")` — ошибка «поправь константу в исходнике» в релизной сборке.

`MAX_PATCH_PLANES` определён **дважды с разными значениями**: 2048 в `cm_patch.cpp:68` и 4096 в `cm_patch.h:65`.

### 3.2 64-битность: что осталось

Часть уже починена (`intptr_t` в `mTransformedVertsArray`, `ptrdiff_t` в `cLeaf_t`). Осталось:

**1. Сейв-система пишет по 4 байта в 8-байтовые поля-указатели** (`code/game/g_savegame.cpp:398`):
```c
case F_STRING:   *(int*)pv = GetStringNum(*(char**)pv);
case F_GROUP:    *(int*)pv = GetGroupNumber(*(AIGroupInfo_t**)pv);
case F_ITEM:     *(int*)pv = GetGItemNum(*(gitem_t**)pv);
case F_VEHINFO:  *(int*)pv = GetVehicleInfoNum(...);
```
`F_GENTITY`/`F_GCLIENT` уже поправлены на `intptr_t`, остальные — нет. На LP64 старшие 4 байта указателя остаются в структуре и **сериализуются в файл сейва** (утечка адресов, обход ASLR). Сама сериализация — сырой дамп `gentity_t`/`gclient_t`, поэтому сейвы 32/64-бит несовместимы и ломаются от любого изменения layout.

**2. Указатель-разница между несвязанными аллокациями** (`cm_load.cpp:172`):
```c
indexes = (int*)Z_Malloc(out->leaf.numLeafBrushes*4, TAG_BSP, qfalse);
out->leaf.firstLeafBrush = indexes - cm.leafbrushes;   // разные malloc-блоки!
```
Формально UB (C++ §expr.add). Работает только потому, что поле расширили до `ptrdiff_t`.

**3. `long` в ABI движка:** `long FS_ReadFile(const char*, void**)` в `game_import_t` (`g_public.h:180`), `long FS_FOpenFileRead`, `int FS_Seek(fileHandle_t, long, int)`. LLP64 (Win64) даёт 4 байта, LP64 (Linux) — 8. **ABI мода различается между платформами.** Плюс хеш-функции на `long` (`files.cpp:309`, `cvar.cpp:61`, `hstring.cpp:194`) — результат хеша зависит от разрядности.

**4. `%x` с указателем** (`z_memman_pc.cpp:511, 579`) — UB и обрезка на LP64. Строка 588 дополнительно печатает `%s` **по уже освобождённому указателю**.

### 3.3 VM и ABI геймкода

QVM-интерпретатора в SP **нет**. `code/client/vmachine.cpp` — 80 строк «fake virtual machine», тонкая обёртка вокруг указателей на функции нативной DLL.

Три разных механизма связывания одновременно:

| Механизм | API | Размер |
|---|---|---|
| **game** — Quake2-style структура указателей | `GetGameAPI(&import)` | `game_import_t` — **127 указателей**, `game_export_t` — 15 |
| **cgame** — QVM-style syscall поверх той же DLL | `vmMain`/`dllEntry` | `cgameImport_t` — **126 номеров syscall**, 235 `case` в `CL_CgameSystemCalls` |
| **renderer** — третья структура | `GetRefAPI(18, &rit)` | `refimport_t` — 63, `refexport_t` — **214** |

**Итого поверхность ABI движок↔моды: ~550 точек.**

Почему это неремонтопригодно:

- **По границе DLL передаются C++-типы по ссылке:** `CGhoul2Info_v&`, `IGhoul2InfoArray&`, `std::vector<CGhoul2Info>&`, `CRagDollParams*`, `ojk::ISavedGame*` с виртуальными функциями. Жёсткая привязка к компилятору + версии STL + настройкам итератор-дебага.
- **Общая куча через ABI:** мод обязан аллоцировать движковым `Z_Malloc`, иначе `Z_Free` упадёт на проверке магика.
- **`throw int` через границу DLL.** `Com_Error` (`common.cpp:326`) делает `throw code;`, ловится в `Com_Frame` (`common.cpp:1386`). При этом `Com_Error` экспортируется в игру как `gi.Error` и в рендерер как `ri.Error` — **исключение раскручивает C-подобные кадры чужой DLL**. Никакого RAII там нет → гарантированные утечки; при несовпадении рантаймов — `std::terminate`.
- `VM_Call` (`vmachine.cpp:35`) всегда вычитывает 8 `va_arg`, сколько бы ни передали — UB, живущее на честном слове ABI x86-64.

**Хорошая новость:** ломать почти нечего. SP-модов с нативными DLL мало, и ABI уже де-факто несовместим между компиляторами. Целевой дизайн — один плоский C-ABI (никаких C++-ссылок), версионирование через `size_t struct_size`, вынос Ghoul2 на сторону движка с opaque-хендлами, удаление `throw` через границу.

### 3.4 Файловая система

`code/qcommon/files.cpp`, 3169 строк, прямой потомок q3.

**Async I/O нет.** Проверено grep'ом: ноль вхождений `aio_*`, `io_uring`, `OVERLAPPED`, `ReadFileEx`, `mmap`, `CreateFileMapping`, `posix_fadvise`. Всё — `fopen`/`fread` и `unzReadCurrentFile`.

Диагностический признак: `FS_ReadFile` вызывает `S_ClearSoundBuffer()` (`files.cpp:1761`), чтобы звук не заикался, **потому что чтение блокирует главный поток**.

**Баг с общим zip-хендлом:** `pack_t` держит **один** `unzFile handle` на весь сеанс, и при `uniqueFILE == qfalse` он отдаётся напрямую (`files.cpp:1341`). Два одновременно открытых не-unique файла из одного pk3 разрушают друг другу состояние zip-стрима.

**Загрузка уровня — полностью блокирующая**, на главном потоке, линейно: выгрузка jagame → `CM_ClearMap` → `Hunk_Clear` → `Cvar_Defrag` → синхронное чтение всего BSP → `dlopen` + `GetGameAPI` → 4 прогона `ge->RunFrame()` «to allow everything to settle».

BSP держится в памяти дважды: `gpvCachedMapDiskImage` не освобождается после collision-загрузки, его переиспользует рендерер, если `Sys_LowPhysicalMemory()` == false. На Unix эта функция — `return qfalse;` с комментарием `TODO` (`sys_unix.cpp:182`), то есть образ карты **всегда** кэшируется.

### 3.5 Многопоточности нет

```
grep -rn "SDL_CreateThread|SDL_Thread|std::thread|pthread_create|CreateThread|
          _beginthread|SDL_CreateMutex|std::atomic|std::mutex"  code/ shared/
→ 0 совпадений
grep -rn "r_smp|R_SMP|SMP_FRAME|smpFrame"  code/ shared/
→ 0 совпадений
```

Всё в одном потоке: I/O, декодирование MP3, загрузка текстур, `dlopen`, рендер. Единственный «чужой» поток — аудио-колбэк SDL (`sdl_sound.cpp:51`), который читает `dma.buffer` **без синхронизации**, пока микшер пишет в него из главного потока. Формально это гонка данных.

### 3.6 Главный цикл и тайминг

Симуляция — фиксированный шаг **20 Гц** (`sv_fps = "20"`) с накоплением остатка (`sv_main.cpp:431`). Отдельного fixed-step для физики нет.

**Движение игрока — исключение:** `ClientThink` считает `msec = ucmd->serverTime - client->ps.commandTime` с клампом `[1..200]` (`g_active.cpp:5191`), то есть **pmove привязан к частоте кадров**. `pmove_fixed`/`pmove_msec` в SP-ветке отсутствуют (grep — ноль). Это классический источник fps-зависимой физики: высота прыжка, страйф-разгон.

Проблемы с высоким FPS и высокой развёрткой:

1. **Таймер целочисленный, 1 мс.** Win: `timeGetTime()` (legacy multimedia timer, обёртка на 49.7 суток). Unix: **`gettimeofday()` — не монотонный**; перевод часов / скачок NTP сдвигает игровое время. `QueryPerformanceCounter` / `clock_gettime(CLOCK_MONOTONIC)` не используются нигде.
2. **`Com_ModifyMsec` округляет 0 → 1** (`common.cpp:1321`). При `com_maxfps 0` и >1000 FPS каждый кадр прибавляет 1 мс вместо ~0.4 мс → **игровые часы обгоняют реальные**.
3. **`fractionMsec` работает только при `com_timescale != 1`** — механизм спасает slow-motion, а не суб-миллисекундные кадры.
4. **Sleep-спинлок**: busy-wait последнюю миллисекунду, ест ядро.
5. **Нет привязки к развёртке**, нет адаптивного vsync, нет интерполяции рендера между 20-герцовыми тиками (вся интерполяция — в cgame через снапшоты).
6. `timing_c::Start/End` (`timing.h:40`) — `__rdtsc()` на Windows, **`return 0` на всех остальных платформах**: `com_speeds`-профилирование вне Windows не работает.

### 3.7 Платформа

`code/win32/` и `codeJK2/win32/` — **только ресурсы**, ни одного `.cpp`. Среди них `afxres.h` — заголовок MFC из Visual C++ 6.

Прямые вызовы Win32 API остались в: `shared/sys/sys_win32.cpp` (timeGetTime, GlobalMemoryStatusEx, CryptGenRandom, ShellExecute), `con_win32.cpp`, `code/client/snd_dma.cpp:40` (`windows.h` ради OpenAL/EAX), `files.cpp:47` и `common.cpp:34` (логика `fs_copyfiles` и `Sys_FileOutOfDate`).

**SDL2 версии 2.0.12** (`lib/SDL2/include/SDL_version.h`) — февраль 2020, шесть лет отставания. Используется только на Windows.

| Возможность | Статус |
|---|---|
| High-DPI | **Нет.** `SDL_WINDOW_ALLOW_HIGHDPI` не используется вообще. На Retina и при масштабировании Windows — растянутый мыльный рендер и рассинхрон координат мыши |
| Gamepad | **Нет.** Только legacy `SDL_Joystick`, без `SDL_GameController`, без базы маппингов, без rumble |
| Raw input | Частично: `SDL_SetRelativeMouseMode`; `SDL_HINT_MOUSE_RELATIVE_SCALING` не трогается |
| Современные дисплеи | HDR / VRR / G-Sync не учитываются, `r_mode` дефолт 4 |

### 3.8 Звук — самый гнилой модуль

```c
// code/client/snd_local.h:33
#if defined(_MSC_VER) && !defined(WIN64)
#define USE_OPENAL
#endif
```

**OpenAL включается только на MSVC 32-бит.** На Linux, macOS, Win64, MinGW весь OpenAL-путь выключен препроцессором, и играет **собственный программный микшер 1999 года** (`snd_mix.cpp`, целочисленный `paintbuffer[]`, ресемплинг «шаг по индексу» без интерполяции). Пространственный звук — ручная панорама + затухание, без HRTF, без окклюзии.

То есть **официальные 64-битные сборки на всех платформах играют через софт-микшер**.

`snd_dma.cpp` — 6365 строк, где каждый второй блок — `#ifdef USE_OPENAL ... #else ... #endif` (две параллельные реализации). Плюс мёртвая EAX-ветка: `LPEAXMANAGER`, `EAXSet/EAXGet`, захардкоженные GUID'ы EAX 4.0 с комментарием «confidential information», загрузка `EaxMan.dll` (бинарник **лежит в репозитории**). Creative EAX умер вместе с аппаратным ускорением звука в Vista (2007).

Плюс собственный MP3-декодер (`code/mp3code/`, 8 файлов, 10 094 LOC) вместо minimp3/libmpg123.

**Цель:** выкинуть оба бэкенда и EAX целиком, оставить единый **OpenAL Soft** (кроссплатформенно, с HRTF и EFX-реверберацией из коробки — ровно то, ради чего когда-то был EAX), декодирование — `minimp3` + `dr_wav`.

---

## 4. Безопасность: P0-блокеры

Это раздел, который нельзя откладывать, если движок будет распространяться и запускать пользовательские карты и моды.

### 4.1 Корневой дефект: загрузка BSP без валидации

`CM_LoadMap_Actual` (`cm_load.cpp:759`):

```c
header = *(dheader_t *)buf;                          // без проверки длины файла
for (i=0; i<sizeof(dheader_t)/4; i++) ((int*)&header)[i] = LittleLong(...);
if (header.version != BSP_VERSION) { ... }           // ident НЕ проверяется вообще
cmod_base = (byte*)buf;
CMod_LoadShaders(&header.lumps[LUMP_SHADERS], cm);   // и далее 11 лампов
```

**Ни один `lump_t.fileofs` / `lump_t.filelen` не сверяется с реальной длиной файла.** Каждый `CMod_Load*` делает `in = (X*)(cmod_base + l->fileofs)` и читает `l->filelen / sizeof(*in)` элементов. Произвольное чтение по смещению из файла карты — база для всего остального.

Показательно: `code/qcommon/qfiles.h:212-235` определяет полный набор `MAX_MAP_*` — **и ни один из них не используется в `cm_load.cpp` ни разу**.

### 4.2 Топ-15 дефектов

| # | Место | Дефект |
|---|---|---|
| 1 | `code/rd-vanilla/tr_bsp.cpp:454,489` | **Переполнение буфера на стеке → RCE.** `drawVert_t points[MAX_PATCH_SIZE*MAX_PATCH_SIZE]` (32×32=1024), затем `numPoints = ds->patchWidth * ds->patchHeight` **из файла, без валидации**, и цикл пишет `points[i]` до `numPoints`. В ioquake3 здесь стоит явная проверка `width > MAX_PATCH_SIZE → ri.Error` |
| 2 | `cm_load.cpp:516` | `memcpy(cm.visibility, buf + VIS_HEADER, len - VIS_HEADER)` при `0 < len < 8` даёт отрицательное значение → `size_t` ≈ 2^64 |
| 3 | `cm_load.cpp:450` | `out->plane = &cm.planes[LittleLong(in->planeNum)]` — индекс не проверен. Произвольный указатель, разыменовываемый на каждом трейсе |
| 4 | `cm_load.cpp:213` | То же в `CMod_LoadNodes` |
| 5 | `cm_load.cpp:264,230` | `out->sides = cm.brushsides + firstSide` без проверки; сразу после `CM_BoundBrush` безусловно читает `b->sides[0..5]`, не проверив `numsides >= 6` |
| 6 | `cm_load.cpp:567,575` | `dv_p = dv + firstVert` и `cm.shaders[shaderNum]` — оба индекса из файла, оба без проверки |
| 7 | `cm_trace.cpp:257,281,542`, `cm_test.cpp:166,361,424` | `brushnum = leafbrushes[leaf->firstLeafBrush+k]; b = &brushes[brushnum];` — **читается в каждом трейсе**, эксплуатируется стабильно |
| 8 | `cm_load.cpp:471` | `entName[strlen(entName)-3] = 'e';` — при имени карты короче 3 символов запись до начала буфера |
| 9 | `tr_model.cpp:806,819`, `tr_ghoul2.cpp:3580,3933` | `size = pinmodel->ofsEnd` → буфер файла просто переклеивается по тегу; **реальная длина файла с `ofsEnd` не сверяется**. Любой `.md3`/`.glm`/`.gla` из pk3 даёт произвольное чтение |
| 10 | `tr_image_tga.cpp:84,205` | `R_Malloc(w * h * 4)` — `unsigned short × unsigned short × 4` в `int`: 65535×16384×4 переполняется в отрицательное. Заголовок разыменовывается без проверки длины; RLE-декодер читает без верхней границы |
| 11 | `z_memman_pc.cpp:588` | `Com_Error(ERR_FATAL, "...%s...", pvAddress)` — `%s` **по уже освобождённой памяти** |
| 12 | `q_shared.cpp:658` | `va()`: 4 статических буфера, `index++ & 3`. **608 вызовов в SP.** Пять вложенных `va()` в одном выражении затирают первый результат — последний коммит репозитория (`1a6a643`, «Fix spawn item error va eval») ровно про это |
| 13 | `sys_main.cpp:352,384` | `Sys_LoadMachOBundle`: движок **распаковывает `.dylib` из pk3 в homepath и делает `dlopen`**. Плюс `Sys_LoadDllFromPaths` ставит `fs_homepath` **первым** — DLL из пользовательской писабельной директории перекрывает системную |
| 14 | `ojk_saved_game.cpp:264` | `rle_buffer_.resize(compressed_size)` из 32-битного поля файла сейва **до** проверки контрольной суммы. Управляемая аллокация до 4 ГБ |
| 15 | `tr_bsp.cpp:394-431` | `ParseFace`: `numVerts`/`numIndexes`/`firstVert`/`firstIndex` не валидируются вообще, при том что в `ParseTriSurf` (`:555`) проверки есть — асимметрия |

Дополнительно: `cl_cin.cpp` — RoQ-декодер с классическими `vq2/vq4/vq8[256*...]` и индексами `(*input++)*4` прямо из потока.

**Чего нет:** проверок целостности контента (CRC pk3 считается, но нигде не сверяется), фаззинга загрузчиков, санитайзеров в CI (опции `UseAddressSanitizer`/`UseUndefinedSanitizer` в CMake есть, **но не включены ни в одном workflow**).

---

## 5. Ghoul2, анимация и CPU-нагрузка

### 5.1 Устройство

Ghoul2 — скелетная система Raven. Форматы: `.glm` (mdxm, меш) + `.gla` (mdxa, анимация со сжатыми костями).

Физическое расположение **несимметрично**:
```
SP:  code/ghoul2/                 440 LOC   (только заголовки)
     code/game/ghoul2_shared.h    811 LOC   ← ВНУТРИ game!
     code/rd-vanilla/G2_*.cpp   9 818 LOC
     code/rd-vanilla/tr_ghoul2.cpp 4 043 LOC
MP:  codemp/ghoul2/             1 013 LOC   (5 файлов, ghoul2_shared.h — 336 LOC)
```

`boneInfo_t` в SP — 255 строк полей против 103 в MP: SP держит прямо в структуре `originalTrueBoneMatrix`, `parentTrueBoneMatrix`, `basepose`, `baseposeInv`, `ragOverrideMatrix`, `extraMatrix`, `animFrameMatrix`.

Дивергенция SP ↔ MP по файлам: `G2_bones.cpp` 12 %, `G2_misc.cpp` 15 %, `tr_ghoul2.cpp` 18 %, `G2_API.cpp` 37 %, `mdx_format.h` **4 %** (формат файлов практически один).

### 5.2 Ключевой вывод для порта рендерера

Дивергенция **rend2 ↔ codemp/rd-vanilla** (то есть насколько rend2 переписал свою же базу):

```
G2_bones.cpp       206 изменённых строк из 4889   ← математика скелета НЕ тронута
G2_bolts.cpp        28                  из  334
G2_surfaces.cpp     64                  из  683
G2_misc.cpp        220                  из 1907
G2_API.cpp         819                  из 3001
tr_ghoul2.cpp    4 022                  из 4907   ← ПОЛНАЯ переработка рендер-части
```

**rend2 не менял математику Ghoul2 — он переписал только `tr_ghoul2.cpp` (VBO + GPU-скиннинг).** Значит при портировании в SP можно взять **SP-версии** `G2_bones.cpp` / `G2_misc.cpp` / `G2_bolts.cpp` / `G2_surfaces.cpp` практически как есть, и портировать только `tr_ghoul2.cpp` плюс дельту `G2_API.cpp`. Это резко снижает риск.

### 5.3 Скиннинг: сейчас 100 % CPU, причём дважды

**Рендер-скиннинг** — `RB_SurfaceGhoul` (`tr_ghoul2.cpp:2771`), главный цикл строки 2991–3078:

```c
for (j = 0; j < numVerts; j++, baseVertex++, v++) {
    bone = &bones->EvalRender(piBoneReferences[G2_GetVertBoneIndex(v,0)]);
    tess.normal[baseVertex][0] = DotProduct(bone->matrix[0], v->normal);
    ...  // 1 вес: 33 flops, 2 веса: ~70 flops
}
```

Результат пишется в `tess.xyz`/`tess.normal` — статические массивы на `SHADER_MAX_VERTEXES = 1000`. Меши >1000 вершин принудительно рвутся на куски, каждый — отдельный draw call. Затем всё уезжает в GL через client-side arrays.

Оценка: player model LOD0 ~2000–3000 вершин → **100–200k flops на модель на кадр**, плюс двойная индирекция `mFinalBones[piBoneReferences[...]]` на каждый вес каждой вершины. Множители: ×2 при `r_shadows > 1`, ×N на каждый портал.

При 8–10 NPC в кадре — **1,5–4 мс/кадр только на скиннинг вершин**.

**Коллизионный скиннинг — второй, полностью независимый.** Путь:

```
SV_Trace → re.G2API_CollisionDetect (G2_API.cpp:1899)
            ├ G2_TransformModel (G2_misc.cpp:569)
            │    └ R_TransformEachSurface (G2_misc.cpp:413)
            │         ПОЛНЫЙ CPU-СКИННИНГ ВСЕЙ ГЕОМЕТРИИ МОДЕЛИ
            └ G2_TraceModels → G2_RadiusTracePolys / G2_TracePolys
```

Доминирует **не трейс, а `G2_TransformModel`** — он линеен по полному числу вершин модели, а не по числу пересечений. **Кэша нет** (в MP есть `G2API_CollisionDetectCache`, в SP его нет). Сабля в JKA делает несколько трейсов за кадр по сегментам клинка, и **каждый** платит полный скиннинг заново.

> **Это самый тяжёлый и самый недооценённый CPU-узел SP.** При этом лечится дёшево: ~200 LOC кэша «модель + frameNum + LOD → transformedVerts».

### 5.4 Ragdoll: прототип 2003 года

Весь ragdoll живёт в `code/rd-vanilla/G2_bones.cpp`, строки 1049–4400 — **3350 LOC из 4784, то есть 70 % файла костей**.

Проблемы:

1. **Рендерер зовёт сервер.** `G2_bones.cpp:2679`: `ri.SV_Trace(...)` — ragdoll-эффекторы трассируют мир **изнутри renderer DLL**. Комментарий автора: `//rww - changed to this.. it was getting up to around 600 traces at times before (which is insane)`.
2. **Всё состояние — файловые статики** (`:1118-1124`): `static mdxaBone_t ragBones[256]`, `static SRagEffector ragEffectors[256]`, `static int numRags`. **Не реентерабельно, один ragdoll за раз.** Это блокирует любую многопоточность анимации.
3. `G2_RagDollSettlePositionNumeroTrois` определена **дважды** (`:2872` и `:3317`), ~450 строк дубликата.
4. Гейтится cvar'ом `broadsword` + 11 подрежимов. Во многих сборках выключен — код почти мёртвый, но компилируется всегда.

Физически некорректен: не солвер с массами и импульсами, а позиционный CCD с эвристической «гравитацией» (`MAX_GRAVITY_PULL 256`) и подгоночными коэффициентами.

**Рекомендация:** краткосрочно — вынести в `G2_ragdoll.cpp` (`G2_bones.cpp` ужмётся с 4784 до ~1450 строк, что резко упростит портирование). Среднесрочно — заменить на Jolt Physics с Ghoul2 как чисто анимационным слоем.

### 5.5 Приоритеты по FPS

**Узкое место SP — не GPU.** Порядок по соотношению «выигрыш / стоимость»:

| # | Что | Выигрыш | Сложность |
|---|---|---|---|
| **1** | **Кэш коллизионного скиннинга** + агрессивный LOD для трейсов | **Высокий, и это самое дешёвое** | **~200 LOC, риск ≈ 0, не требует смены рендерера** |
| **2** | VBO для мировой геометрии | **Очень высокий** | 0, если делать порт рендерера |
| **3** | GPU-скиннинг Ghoul2 | Высокий | 0 при порте |
| **4** | GPU surface sprites (трава/листва) | Высокий на `yavin`, `hoth`, `t2_rancor` | часть порта |
| **5** | GPU-погода | Высокий на outdoor-картах | часть порта |
| **6** | `SHADER_MAX_VERTEXES` 1000 → 16384 | Средний | одна константа + проверка статики |
| **7** | Многопоточная bone-стадия (job system) | Средний-высокий на 8+ NPC | средняя; блокеры — ragdoll-статики и `ri.SV_Trace` |
| **8** | Clustered lighting вместо `ProjectDlightTexture` | Средний-высокий | средняя-высокая |
| **9** | Shadow maps / CSM вместо stencil | Средний | часть порта rend2 |
| **10** | **GPU-driven culling** (compute + MDI) | Зависит от сцены | **высокая — делать последним** |

> **Чего НЕ делать первым:** GPU-driven culling. Он требует поднятия требований API и большой работы, а в SP культинг сейчас **не** главный потребитель CPU — им является per-vertex работа и двойной скиннинг Ghoul2.

---

## 6. Вырезание мультиплеера

### 6.1 Что удаляется

```
codemp/ всего                          601 484 LOC
  из них сохраняем (rd-rend2)          -92 576
─────────────────────────────────────────────
УДАЛЯЕТСЯ                              508 908 LOC
```

Детально: `codemp/game` 179 500, `rd-vanilla` 62 854, `cgame` 54 879, `client` 42 250, **`botlib` 36 437** (не нужен — SP-AI живёт в `code/game/AI_*.cpp`), `qcommon` 32 131, `ui` 25 961, `rd-dedicated` 23 590, `icarus` 16 292, `server` 15 863, `mp3code` 10 086 (байт-идентичный дубликат `code/mp3code`).

### 6.2 Остатки MP-архитектуры внутри SP

В SP всё равно есть клиент-серверное разделение — это архитектура Quake 3. Что реально осталось:

| Файл | LOC | Что |
|---|---|---|
| `code/qcommon/msg.cpp` | 1287 | Битовая сериализация: `entityStateFields[]` — **68 полей**, `playerStateFields[]` — **63 поля** |
| `code/qcommon/net_chan.cpp` | 592 | Netchan + loopback. **Реальных сокетов нет** — `NET_SendPacket` обслуживает только `NA_LOOPBACK` |
| `code/server/sv_snapshot.cpp` | 724 | Дельта-снапшоты |
| `code/client/cl_parse.cpp` | 540 | Разбор снапшотов |
| частично `sv_client.cpp`, `sv_main.cpp`, `cl_main.cpp` | ~1300 | Connectionless-протокол, resend, timeouts |

Чего уже нет (Raven вычистил): `net_ip.cpp`, `huffman.cpp`, botlib, master-сервер, серверный браузер, PunkBuster.

**Итого ~4 400–4 700 LOC ≈ 1,1 % дерева.** Мало по объёму, но это **горячий путь каждого кадра**: `SV_EmitPacketEntities` на каждый серверный кадр прогоняет каждую энтити через 68 полей побитовой упаковки, `CL_ParsePacketEntities` распаковывает обратно. При 200–400 активных энтити — ~15–30 тысяч побитовых операций на кадр в обе стороны, плюс полная потеря предсказуемости ветвлений.

### 6.3 Схлопывать ли SV/CL — двухфазный ответ

**Фаза 1 (низкий риск, весь выигрыш) — заменить транспорт, сохранив API.**

`SV_SendMessageToClient` вместо `Netchan_Transmit` кладёт в кольцевой буфер уже готовые структуры (`clientSnapshot_t` + массив `entityState_t` + `playerState_t`), а `CL_ParseServerMessage` читает их `memcpy`-ем.

Удаляется: `msg.cpp` целиком (1287), `net_chan.cpp` целиком (592), `SV_EmitPacketEntities` + MSG-часть `SV_WriteSnapshotToClient` (~250), `CL_DeltaEntity` + `CL_ParsePacketEntities` (~200), connectionless-протокол (~400). **≈ 2 700 LOC минус.** cgame не трогается вообще, сейвы не ломаются, сериализационный налог → 0.

**Фаза 2 (высокий риск, малый выигрыш) — не делать.** Выкинуть `PACKET_BACKUP`/`parseEntities`, дать cgame напрямую `g_entities`. Требует переписать `cg_snapshot.cpp` и сотни обращений к `currentState`/`nextState` в 51 571 LOC cgame. Плюс сломается интерполяция: cgame лерпит между `snap` и `nextSnap`, а тикрейт 20 Гц — убрав это, получим регресс по плавности. Выигрыш после Фазы 1 уже почти нулевой.

### 6.4 Что забрать из `codemp/` кроме рендерера

| Что | LOC | Зачем |
|---|---|---|
| `tr_image_stb.cpp` + `stb_image.h` | 7 700 | Снимает зависимость от `lib/libpng` + `lib/jpeg-9a` (~90k LOC в `lib/`) |
| `MikkTSpace/` + `tr_tangentspace.cpp` | 2 334 | **Обязательно для PBR** — в SP тангентов нет вообще |
| `tr_model_iqm.cpp` + `iqm.h` | 1 316 | IQM как современный формат; снимает лимит 32 bone-refs на поверхность для новых ассетов |
| `tr_weather.{cpp,h}` + GLSL | ~1 550 | Прямая GPU-замена `tr_WorldEffects.cpp` |
| `surface_sprites.glsl` + код | ~400 | GPU-замена `tr_surfacesprites.cpp` |
| `tr_cache.{cpp,h}`, `tr_allocator.{cpp,h}` | ~700 | Модельный кеш и линейные аллокаторы |
| `tr_ghoul2.cpp:3721+` `OldToNewRemapTable[72]` | ~200 | **Таблица ремапа костей JK2→JKA** — прямо нужна для поддержки JK2 |

**Чего НЕ брать:** `codemp/rd-common/`. Вопреки ожиданиям, MP-версия **беднее**: `tr_font.cpp` 1903 против 2271 в SP, `tr_image_jpg.cpp` 421 против 585 (в SP есть `SaveJPGToBuffer` для скриншотов). Правильный ход — вести `code/rd-common/` как основную ветку.

### 6.5 Главные риски порта рендерера

**Риск 1 — `tr_public.h`, дивергенция 70 %.**

```
указателей на функции в SP : 214
                      в MP : 228
общих                      : 163
SP-only                    :  51   ← реализовать
MP-only                    :  65   ← удалить/заглушить
```

SP-only, критичные для геймплея: `IsOutside`, `IsOutsideCausingPain`, `IsShaking`, `GetWindVector`, `GetWindGusting`, `GetChanceOfSaberFizz` — SP-код вызывает `re.IsOutside(pos)` для урона от кислотного дождя, `re.GetWindVector()` для физики, `re.GetChanceOfSaberFizz()` для механики светового меча в дожде. **Это не графика, это геймплей, живущий в рендерере.** В rend2 внутренний `weatherSystem_t` содержит нужные данные (`windDirection`, `windSpeed`, `weatherBrushes[]`), но экспортов нет и нет point-in-volume теста.

Плюс: `InitDissolve`/`ProcessDissolve` (переход уровня), `LAGoggles`, указатели-переменные `tr_distortionAlpha/Stretch/PrePost/Negate`, индексированный Ghoul2 API (`G2API_GetAnimIndex`, `G2API_SetAnimIndex`, `G2API_GetBoneAnimIndex`, ...), `TheGhoul2InfoArray`, `SV_Trace`, `R_inPVS`, `Hunk_ClearToMark`, `gpvCachedMapDiskImage`.

**Риск 2 — `refEntity_t` несовместим на уровне ABI.**

```c
// SP                                  // MP
#define RF_MORELIGHT      0x00001      #define RF_MINLIGHT       0x00001
#define RF_CAP_FRAMES     0x00400      #define RF_FORCE_ENT_ALPHA 0x00400
#define RF_ALPHA_FADE     0x00800      #define RF_RGB_TINT       0x00800
#define RF_SETANIMINDEX   0x20000      #define RF_DISINTEGRATE1  0x20000
```

`refEntityType_t` — **другой порядок enum**:
```
SP: RT_MODEL, RT_POLY, RT_SPRITE, RT_ORIENTED_QUAD, RT_LINE, RT_ELECTRICITY,
    RT_CYLINDER, RT_LATHE, RT_BEAM, RT_SABER_GLOW, RT_PORTALSURFACE, RT_CLOUDS
MP: RT_MODEL, RT_POLY, RT_SPRITE, RT_ORIENTED_QUAD, RT_BEAM, RT_SABER_GLOW,
    RT_ELECTRICITY, RT_PORTALSURFACE, RT_LINE, RT_ORIENTEDLINE, RT_CYLINDER, RT_ENT_CHAIN
```

MP ввёл `miniRefEntity_t` и `refEntity_t` как надмножество с union'ами. SP использует плоскую структуру. `RDF_*` тоже разошлись: SP `RDF_doLAGoggles 32` против MP `RDF_AUTOMAP 32`.

**Копирование «как есть» приведёт к молчаливому мусору.** Нужно переписать `tr_types.h` порта под SP-раскладку и вычистить MP-only типы.

Плюс `SHADERNUM_BITS` 13 в SP против 14 в MP — пересчёт sort key.

**Риск 3 — SP-only фичи, которых нет ни в rend2, ни в rd-vulkan:**
```
tr_draw.cpp                1 025 LOC   2D-слой, Dissolve, LAGoggles, GetScreenShot
tr_stl.cpp                    78 LOC
RB_SurfaceCone / Cylinder / Electricity / SaberGlow / Lathe / Clouds
RB_CalcDisintegrateColors / RB_CalcDisintegrateVertDeform
```

---

## 7. Сборка и CI: фактическая проверка

### 7.1 Что проверили

Собрали на GCC 13.3 / Ubuntu 24.04 / CMake 3.28, x86_64, Release, с системными SDL2 / zlib / libpng / libjpeg:

```
[ 43%] Built target openjk_sp.x86_64
[ 66%] Built target rdsp-vanilla_x86_64
[ 98%] Built target rd-rend2_x86_64
[100%] Built target jagamex86_64
exit=0
```

**Собирается без ошибок.** Это хорошая база: не придётся тратить недели на приведение к современному тулчейну.

### 7.2 Предупреждения — 175 на `-Wall`

| Тип | Кол-во |
|---|---|
| `-Wsign-compare` | 63 |
| `-Waddress` | 24 |
| `-Wclass-memaccess` | 18 |
| `-Wstringop-truncation` | 16 |
| `-Wunused-variable` | 13 |
| `-Wmaybe-uninitialized` | 7 |
| `-Warray-bounds=` | 6 |
| `-Wformat-overflow=` | 5 |
| `-Wdangling-pointer=` | 1 |

Реальные дефекты среди них:

```
code/qcommon/md4.cpp:185         storing the address of local variable 'md' in 'm'   [-Wdangling-pointer]
code/rd-vanilla/tr_world.cpp:416 array subscript 2/3 is above array bounds of 'float [1][18]'
code/cgame/cg_players.cpp:6521   array subscript -1 is below array bounds of 'float [2][3]'
code/game/g_roff.cpp:348,412,427,433   '%s' writing up to 511 bytes into a region of size 227..238
code/game/Q3_Interface.cpp:7670  '%s' writing 4 bytes into a region of size between 1 and 256
codemp/rd-rend2/G2_bones.cpp:860 startFrame/endFrame/flags/currentFrame/animSpeed may be used uninitialized
codemp/rd-rend2/tr_bsp.cpp:451   'buf_p' may be used uninitialized
```

Плюс `memcpy ... is out of the bounds [0, 6376] of object 'retail_client' with type 'RetailGClient'` — тот самый паттерн сырых дампов структур.

### 7.3 CI и тесты

`.github/workflows/build.yml`: MSVC на `windows-2022` (x86/x64 × Debug/Release × Portable), Ubuntu 22.04 (x86/x64), macOS 15 Intel + ARM. Плюс `build-deprecated.yml` — раз в 4 месяца, toolset `v141_xp` (Windows XP), `continue-on-error: true`.

**Тесты: 222 строки на ~500 kLOC.** `tests/` содержит Boost.Test boilerplate + `safe/string.cpp` (76 строк) + `safe/limited_vector.cpp` (146 строк), покрывающие исключительно `shared/qcommon/safe/`. `BuildTests` по умолчанию `OFF` и **не включается ни в одном workflow**; `ctest` в CI не вызывается.

**Итог: CI проверяет только «компилируется и линкуется».**

`Dockerfile` — `ubuntu:18.04` (EOL с 2023), собирает только `openjkded` (MP dedicated). К SP отношения не имеет, удаляется вместе с MP.

Бандлы: **SDL2 2.0.12** (2020), **jpeg-9a** (2014), minizip форкнут ради `Z_Malloc`.

---

## 8. Стратегия рендерера: разбор вариантов

Это главное архитектурное решение проекта. Ниже — честный разбор с цифрами.

### Вариант A. Взять `rd-vulkan` из EternalJK (ветка `pbr`) и портировать в SP

**За:**

- **Vulkan уже написан и работает.** 18 248 строк проверенного в бою кода: инстанс, девайс, swapchain, синхронизация, аллокатор, staging, 25 render pass, MSAA/SSAA, post-process chain, выбор GPU, обход бага гибридных ноутов. Это самая скучная и самая дорогая часть, и она сделана.
- **JKA-специфика уже портирована на Vulkan:** WorldEffects, surface sprites (**+ GPU-инстансинг через SSBO и `vkCmdDrawIndexedIndirect`, чего нет даже в rend2**), quicksprite, terrain, Ghoul2 с GPU-скиннингом и gore-VBO, stencil-тени, dynamic glow, refraction, flares с GPU-тестом видимости.
- **SP и MP версии `rd-vanilla` совпадают на 70–98 % пофайлово**, а Vulkan-правки в WorldEffects / surfacesprites / quicksprite — всего 204 / 28 / 161 строка механической замены `GL_State`→`vk_bind_pipeline`. Эти же правки переносятся на SP-версии почти автоматически.
- **PBR-слой написан:** ветка `pbr` — +8,6k строк рабочего кода, включая **diffuse IBL (irradiance cube), которого нет в rend2**.
- **Активное сопровождение** одним человеком (JKSunny), с регулярным апстримингом из Quake3e со ссылками на конкретные SHA.
- Кроссплатформенность подтверждена CI (Win / Linux / macOS Intel + ARM).
- **Vulkan получается сразу** — то есть цель «уйти на Vulkan» достигается, а не откладывается.

**Против:**

- Архитектурно это forward-рендерер, эмулирующий фиксированный конвейер id Tech 3. `vk_shade_geometry.cpp` (2722 строки) итерирует по стадиям шейдера и делает draw call **на стадию**. Добавление CSM/SSAO означает правку этой петли.
- Комбинаторный взрыв SPIR-V: 194 блоба на master → 600 в `pbr`; с тенями и SSAO будет 1500+.
- **Нет CSM, SSAO, tone mapping** — придётся писать (это 4–5 месяцев аддитивной работы).
- Vulkan-1.0-эра: нет dynamic rendering, sync2, bindless, timeline semaphores, VMA, async transfer/compute, MT-записи команд.
- Нет персистентного pipeline cache → долгий старт.
- Шейдерная сборка Windows-only.
- **Мерж-долг:** форкнув `pbr`, вы берёте на себя синхронизацию с апстримом JKSunny.
- SP-порт всё равно нужен: **3–6 месяцев**.

### Вариант B. Взять `rd-rend2` и написать Vulkan-бэкенд с нуля (RHI)

**За:**

- Фичи, которых нет нигде больше: 3 каскада PSSM sun shadows, point shadows (32), SSAO с bilateral depth-blur, tone mapping + auto-exposure с иерархической гистограммой, parallax/deluxe mapping, depth pre-pass, GPU-погода.
- Зрелая материальная система `.mtr` и экосистема ассетов (Bespin Reborn, PBR-ретекстуры на JKHub).
- `DrawItem` (`tr_local.h:3810`) — уже «почти command buffer»: `RenderState`, IBO, program, массивы `vertexAttribute_t`, `SamplerBinding`, `UniformBlockBinding`, blob `UniformData`, union `DrawCommand`. **Хорошая отправная точка для RHI.**

**Против:**

- Это OpenGL 3.2. `tr_glsl.cpp` (2638 строк рантайм-компиляции с `#define`-перестановками и `glUniform*`), `tr_fbo.cpp` (961), `tr_vbo.cpp` (824), `tr_extensions.cpp` — весь backend завязан на GL-стейт.
- Написать Vulkan-бэкенд «с нуля» = воспроизвести ровно те 18–21k строк, которые уже есть в `rd-vulkan`, **плюс** переложить ~740 GLSL-программ с рантайм-`#define` на офлайн-SPIR-V, **плюс** заменить `glUniform` на UBO/push-constants/дескрипторы, **плюс** переписать `tr_fbo` на render passes, **плюс** расставить барьеры для 20+ render target'ов.
- **Разделение frontend/backend в rend2 фиктивно** — `RB_UpdateConstants` вызывается из `RE_BeginScene`, `R_IssuePendingRenderCommands` вызывается изнутри `R_CreateVBO`. Без развязки этого RHI бессмыслен, а развязка — 4–6 недель сама по себе.
- rend2 тоже MP-only, и **дальше ушёл от `rd-vanilla`, чем `rd-vulkan`**, то есть SP-порт тяжелее.
- В rend2 нет `tr_quicksprite`, нет `tr_terrain`, gore — 49-строчная заглушка, MD3-вершинная анимация не работает.

**Оценка: 28–45 недель только на RHI+Vulkan, плюс 14–22 недели на SP-порт. Итого 12–18 месяцев до первого кадра в Vulkan.**

### Вариант C. RHI-абстракция поверх обоих

**Против — и это решающее:** backend id Tech 3 принципиально враждебен RHI-абстракции. `RB_StageIteratorGeneric` / `tess` — immediate-mode модель, где на каждый draw пересобираются атрибуты и итерируются стадии шейдера. RHI поверх этого либо станет тонкой обёрткой (и не окупится), либо потребует переписать backend целиком — и тогда это уже вариант B с дополнительным слоем.

Плюс: `rd-vulkan` и `rd-rend2` разошлись на уровне `tr_local.h`, `tr_bsp.cpp`, `tr_shader.cpp`, `shaderStage_t`. «Взять фичи из rend2» — не copy-paste, а ручной перенос каждой фичи под другую модель данных.

**Оценка: 18–30 месяцев, высочайший риск заглохнуть.**

### Рекомендация

> **Вариант A, с базой на ветке `pbr` форка `JKSunny/EternalJK`.**
> Затем — точечный перенос недостающих фич rend2 (tone mapping → CSM → depth pre-pass → SSAO) на уровне отдельных проходов и GLSL, **без RHI-абстракции**.

Обоснование в одну фразу: вариант B требует заново написать те 18–21 тысячи строк Vulkan-инфраструктуры, которые в A уже есть, работают на четырёх форках и трёх платформах и покрыты CI; при этом **обе стратегии одинаково требуют SP-порта**, а недостающие в A фичи — это ~4–5 месяцев аддитивной работы против ~12–18 месяцев на воспроизведение бэкенда.

**Почему это не противоречит вашей исходной установке «сразу RHI + Vulkan»:** цель установки — получить Vulkan и не застрять в GL. Вариант A даёт Vulkan **немедленно**. RHI-абстракция при этом не отменяется навсегда — она становится осмысленной позже, когда/если понадобится D3D12 или Metal, и когда backend будет развязан. Строить абстракцию **до** того, как есть работающий Vulkan-путь, — это абстракция над воображаемым вторым бэкендом.

**Важная оговорка перед решением:** ветка `pbr` — работа одного человека, релизов нет (только prerelease-CI), автор сам пишет в коммите `313d852`: *«This is part of larger refactor, stability is not guaranteed»*. **Фаза 0 (разведка) обязательна** — собрать, погонять, измерить, и только потом принимать go/no-go. Показательно, что SP-порта автор не делал ни разу: Vulkan портирован в **три** MP-форка и **ни в один** SP — это косвенно говорит о нетривиальности задачи.

---

## 9. План разработки

### Фаза 0 — Разведка и фундамент (3–5 недель)

Цель: принять go/no-go по рендереру на данных, а не на README, и параллельно закрыть то, что нужно в любом случае.

| # | Задача | Оценка |
|---|---|---|
| 0.1 | Собрать `EternalJK@origin/pbr` под Windows и Linux. Погонять на JKA MP-картах с PBR-ретекстурами. Замерить FPS, время старта, стабильность. Прогнать RenderDoc | 1–2 нед |
| 0.2 | Собрать `OpenJK@master` с `rd-rend2` и сравнить: качество картинки, FPS, набор фич | 3–5 дн |
| 0.4 | **Зафиксировать тулчейн:** C++20 через `CMAKE_CXX_STANDARD 20`, `cmake_minimum_required(3.20)`, `/std:c++20` для MSVC, `-Wextra`, отдельный `-Werror`-таргет | 3 дн |
| 0.5 | **Включить ASan + UBSan в CI** (опции уже есть в CMake, просто не используются) | 2 дн |
| 0.6 | Написать регресс-скрипт: прохождение `t1_sour → t3_bounty` с сейв/лоадом на каждом уровне, автоматический сбор крашей | 1 нед |

**Выход фазы:** решение по рендереру + рабочий CI с санитайзерами + база для регрессии.

### Фаза 1 — Безопасность и 64-битность (4–6 недель, параллельно Фазе 2)

Эти вещи не зависят от выбора рендерера и нужны в любом случае.

| # | Задача | Оценка |
|---|---|---|
| 1.1 | **Валидация BSP:** проверка `ident`, сверка всех `lump_t.fileofs/filelen` с длиной файла, валидация всех индексов (`planeNum`, `shaderNum`, `firstSide/numSides`, `leafbrushes`, `leafsurfaces`) | 1 нед |
| 1.2 | **`tr_bsp.cpp:489` — проверка `patchWidth/patchHeight` против `MAX_PATCH_SIZE`** (стековый оверфлоу) | 1 дн |
| 1.3 | Сверка `ofsEnd`/`ofs*` в MD3/GLM/GLA с реальной длиной файла | 3 дн |
| 1.4 | Границы в `LoadTGA`, RoQ-декодере, `ojk_saved_game` | 3 дн |
| 1.5 | Убрать `Sys_LoadMachOBundle` (dlopen из pk3); переставить порядок поиска DLL (homepath — не первый) | 1 дн |
| 1.6 | **Фаззинг** `CM_LoadMap` / `R_LoadMD3` / `R_LoadMDXM` / `LoadTGA` через libFuzzer, встроить в CI | 1,5 нед |
| 1.7 | `EnumerateField`/`EvaluateField` → `intptr_t` для всех case; **версионировать формат сейва** | 1 нед |
| 1.8 | Убрать `long` из `game_import_t` и `qcommon.h` → `int64_t`/`size_t` | 3 дн |
| 1.9 | Дать `Z_Malloc` реальное выравнивание (параметр `iAlign` сейчас игнорируется) | 2 дн |
| 1.10 | Починить `%x`-печать указателей и `%s` по освобождённой памяти | 1 дн |
| 1.11 | Исправить дефекты из компилятора: `md4.cpp:185`, `tr_world.cpp:416`, `cg_players.cpp:6521`, `g_roff.cpp` × 4 | 3 дн |

### Фаза 2 — Дешёвые победы по производительности (2–3 недели)

Не требуют смены рендерера, дают немедленный эффект.

| # | Задача | Оценка |
|---|---|---|
| 2.1 | **Кэш коллизионного скиннинга Ghoul2** («модель + frameNum + LOD → transformedVerts»), порт `G2API_CollisionDetectCache` из MP. **Самый дешёвый большой выигрыш во всём проекте** | 1 нед |
| 2.2 | Агрессивный LOD для коллизионных трейсов (`G2_DecideTraceLod`) | 2 дн |
| 2.3 | Увеличить `CMiniHeap` с 256 КБ и сделать динамический рост вместо `Com_Error` | 2 дн |
| 2.4 | Монотонный высокоточный таймер: `clock_gettime(CLOCK_MONOTONIC)` / `QueryPerformanceCounter` | 3 дн |
| 2.5 | Убрать округление `msec < 1 → 1`, накапливать дробное время независимо от `timescale` | 2 дн |
| 2.6 | Убрать busy-wait в `Com_Frame` | 1 дн |

### Фаза 3 — Вырезание мультиплеера (2–3 недели)

Порядок важен: rend2/rd-vulkan надо **вынести до** удаления `codemp/`.

| Шаг | Что | Оценка | Риск |
|---|---|---|---|
| 3.0 | Базовая линия: собрать master со всеми `BuildMP*=OFF`, кроме рендерера. Зафиксировать эталон | 0,5 дн | — |
| 3.1 | `git mv codemp/rd-{rend2\|vulkan} → code/`, `codemp/ghoul2/{G2_gore.*, g2_local.h} → code/ghoul2/`. Временно оставить `codemp/{qcommon,rd-common,ghoul2}` в include-path | 1–2 дн | низкий |
| 3.2 | `git rm -r codemp/` — **−508 908 LOC**. Убрать 7 опций `BuildMP*` из корневого CMake | 0,5 дн | низкий (revert тривиален) |
| 3.3 | Снять `DEDICATED` из `shared/`; `_JK2EXE` сделать безусловным (или переименовать в `SP_ENGINE` — имя вводит в заблуждение) | 0,5 дн | низкий |
| 3.4 | Забрать `stb_image`, снести `lib/libpng` + `lib/jpeg-9a` (−90k LOC). Внимание: `SaveJPGToBuffer` для скриншотов → `stb_image_write` | 2–3 дн | низкий |
| 3.5 | Чистка мёртвого кода: `tr_arb.cpp`, `qglLockArraysEXT`, дубликат `glext.h`, `tr_stl.cpp`. `code/mp3code` (10 094) → `minimp3` | 1 нед | низкий |
| 3.6 | **Фаза 1 схлопывания SV/CL:** удалить `msg.cpp`, `net_chan.cpp`, MSG-части снапшотов. −2 700 LOC | 1–2 нед | средний (проверять сейвы) |

**Итог фазы:** дерево сжимается с ~1 360 000 до ~550 000 LOC.

### Фаза 4 — Порт рендерера в SP (3–6 месяцев) ← **главная работа**

Это 60–80 % трудоёмкости всего проекта. Порядок внутри:

| # | Задача | Оценка |
|---|---|---|
| 4.1 | **`tr_types.h`:** заменить MP-раскладку на SP-ю. Выкинуть `miniRefEntity_t`, `RT_ENT_CHAIN`, `RT_ORIENTEDLINE`; вернуть `RT_LATHE`, `RT_CLOUDS`. Перенумеровать `RF_*`/`RDF_*`. Пройтись по всем `e.uRefEnt`/`e.sprite`/`e.line`/`e.bezier` | 2–3 нед |
| 4.2 | **`tr_public.h`:** 163 общих + реализовать 51 SP-only + удалить/заглушить 65 MP-only. Заглушить сразу: `GetSharedMemory`, `CGVM_*`, `Automap*`, `TakeVideoFrame`, `RegisterServer*`. Реализовать обязательно: `SV_Trace`, `R_inPVS`, `Malloc`, `Hunk_ClearToMark`, `TheGhoul2InfoArray`, `com_frameTime` | 3–4 нед |
| 4.3 | **`ghoul2_shared.h`:** взять SP-версию (811 строк) как истину, адаптировать рендерер. Взять SP `G2_bones.cpp`/`G2_misc.cpp`/`G2_bolts.cpp`/`G2_surfaces.cpp` **как есть** (дельта рендерера к своей базе — всего 28–220 строк), портировать вручную. `tr_ghoul2.cpp` — брать версию рендерера | 3–4 нед |
| 4.4 | **World effects API поверх GPU-погоды:** `IsOutside`, `IsOutsideCausingPain`, `IsShaking`, `GetWindVector`, `GetWindGusting`, `GetChanceOfSaberFizz`, `SetTempGlobalFogColor`. Нужен point-in-brush тест и `outsidepain`/`outsideshake`, которых нет в рантайме | 2–3 нед |
| 4.5 | **MD3 vertex animation** (в rend2 — блокер: грузится только кадр 0; в rd-vulkan — проверить) | 1–1,5 нед |
| 4.6 | Эквивалент `tr_draw.cpp`: `RE_Scissor`, Dissolve, `GetScreenShot`, `TempRawImage_*`, LAGoggles | 1,5–2 нед |
| 4.7 | SP-типы поверхностей: `RT_LATHE`, `RT_CLOUDS`, `RT_CONE`, `RT_CYLINDER`, `RT_ELECTRICITY`, `RT_SABER_GLOW`; дезинтеграция в GLSL | 2 нед |
| 4.8 | **Ghoul2 gore** (в rend2 — 49-строчная заглушка; в SP это заметная механика) | 1,5–2,5 нед |
| 4.9 | Слой совместимости cvar'ов для SP-меню (набор cvar'ов Quake3e ≠ ванильный JKA) | 1 нед |
| 4.10 | JK2-специфика: `OldToNewRemapTable[72]` (ремап костей JK2→JKA), проверка ассетов JK2 | 1 нед |
| 4.11 | Отладка и регрессия по обеим кампаниям | 3–5 нед |

**Митигация риска:** держать `rdsp-vanilla` рабочим параллельно как эталон, переключение через `cl_renderer`, сравнивать покадрово.

### Фаза 5 — Техдолг Vulkan-слоя (4–6 недель, можно параллелить с Фазой 4)

По убыванию отдачи:

| # | Задача | Оценка |
|---|---|---|
| 5.1 | **Персистентный pipeline cache** — `vkGetPipelineCacheData` в `fs_homepath`, загрузка при старте. ~50 строк, немедленный выигрыш в времени загрузки | 3 дн |
| 5.2 | **`VK_EXT_debug_utils` + валидация на всех платформах** (сейчас только Windows + Debug, и через депрекейтнутый `debug_report`) | 3 дн |
| 5.3 | **Кроссплатформенный шейдер-компилятор**: переписать `compile_threaded.cpp` на `std::filesystem` + `std::thread`, либо CMake-таргет с `glslc`. **Без этого вы заперты на Windows для любой работы с шейдерами** | 1,5 нед |
| 5.4 | Убрать `shader_data.c` (14,6 МБ, в `pbr` — 60+) из git, генерировать на этапе сборки | 3 дн |
| 5.5 | Перенять `shaders/glsl/global.h` — общий C/GLSL-заголовок для UBO-раскладок | (уже есть в `pbr`) |
| 5.6 | VMA вместо самописного chunk-аллокатора | 1,5 нед |
| 5.7 | `synchronization2`, выделенная transfer-очередь, включить `USE_UPLOAD_QUEUE` | 2 нед |
| 5.8 | Исправить `tr_quicksprite.cpp:151` (избыточный цикл ×6 в горячем пути) | 1 дн |

### Фаза 6 — Фичи графики, по одной, без абстракций (4–6 месяцев)

Порядок по соотношению «эффект / стоимость» именно для JK2/JKA:

| # | Фича | Почему в этом порядке | Оценка |
|---|---|---|---|
| 6.1 | **Tone mapping + linear workspace** | Сейчас `r_hdr` — это просто `R16G16B16A16_UNORM` без tone mapping. **Без него PBR работает не в своём режиме.** Самое дешёвое и самое заметное улучшение. ACES fitted в rend2 уже написан, только не вызывается | 2–3 нед |
| 6.2 | **CSM для солнца** — порт `sunShadow()` из `rend2/glsl/lightall.glsl:531` + `VPT_SUN_SHADOWS`-логика из `tr_main.cpp:2864` | Главный визуальный апгрейд для outdoor-карт JKA | 6–8 нед |
| 6.3 | **Depth pre-pass** | Даёт и производительность, и базу под SSAO. Уже прототипирован автором форка | 2 нед |
| 6.4 | **SSAO** — порт `ssao.glsl` + `depthblur.glsl` | Поверх depth pre-pass | 4 нед |
| 6.5 | **Diffuse IBL / probe grid** | В `pbr`-ветке irradiance cube уже есть; нужна интерполяция между 2–4 пробами и смена sort key (6 бит не хватит) | 4–6 нед |
| 6.6 | **HDR lightmap** — перезапекание в q3map2 с `-hdr`, либо остаться на RGBM | Без этого диффузная часть остаётся не-PBR (запечена radiosity-солвером 2001 года) | 3–5 нед |
| 6.7 | **Clustered forward (Forward+)** | `CalcDynamicLightContribution` в `pbr.glsl` уже делает per-pixel цикл — это фактически заготовка. Имеет смысл только если дойдёте до сотен источников | 6–8 нед |
| 6.8 | Point shadows, parallax, specular AO, Burley diffuse, multiscatter GGX | Мелочи с готовым кодом | 3–4 нед |

### Фаза 7 — Многопоточность и GPU-driven (по остаточному принципу)

| # | Задача | Блокеры |
|---|---|---|
| 7.1 | Job system + многопоточная bone-стадия Ghoul2 | Ragdoll-статики (`G2_bones.cpp:1118`), `ri.SV_Trace` из рендерера, глобал `HackadelicOnClient` |
| 7.2 | Многопоточная запись командных буферов (secondary command buffers) | Однопоточный `vk_cmd.cpp` |
| 7.3 | Async transfer queue для загрузки текстур | `USE_UPLOAD_QUEUE` уже написан, но отключён |
| 7.4 | GPU-driven culling (compute + `vkCmdDrawIndexedIndirect`, HiZ two-phase) | **Делать последним.** В SP культинг сейчас не главный потребитель CPU |
| 7.5 | Ragdoll → Jolt Physics | Снимает блокер многопоточности и убирает `ri.SV_Trace` из рендерера |

### Что НЕ делать

- **Не делать deferred rendering.** JK2/JKA — это лес alpha-blended поверхностей: сабли, glow, форс-эффекты, surface sprites, погода, порталы. G-buffer им противопоказан. Forward+ — единственный разумный апгрейд.
- **Не делать RHI-абстракцию сейчас.** Только после того, как есть работающий Vulkan-путь и развязанный frontend, и только если реально понадобится второй бэкенд.
- **Не начинать с `master`-ветки EternalJK** — повторите работу по PBR, которая уже сделана в `pbr`.
- **Не делать Фазу 2 схлопывания SV/CL** (полное удаление снапшотов) — высокий риск, почти нулевой выигрыш после Фазы 1.
- **Не начинать с GPU-driven culling.**

### Сводный график

| Фаза | Содержание | Срок | Может идти параллельно |
|---|---|---|---|
| 0 | Разведка, тулчейн, CI | 3–5 нед | — |
| 1 | Безопасность, 64-бит | 4–6 нед | с 2, 3 |
| 2 | Дешёвые победы по FPS | 2–3 нед | с 1, 3 |
| 3 | Вырезание MP | 2–3 нед | с 1, 2 |
| 4 | **Порт рендерера в SP** | **3–6 мес** | с 5 |
| 5 | Техдолг Vulkan-слоя | 4–6 нед | с 4 |
| 6 | Фичи графики | 4–6 мес | — |
| 7 | Многопоточность, GPU-driven | по остатку | — |

**Реалистичный горизонт до «играбельная кампания JK2 и JKA на Vulkan с PBR»: 9–14 месяцев для одного опытного инженера на полной занятости.** Для двух — 6–9 месяцев, потому что Фазы 1/2/3 хорошо параллелятся с 4.

---

## 10. Риски

| Риск | Вероятность | Влияние | Митигация |
|---|---|---|---|
| **Ветка `pbr` окажется нестабильной** (автор сам предупреждает) | Средняя | Высокое | Фаза 0 — обязательная разведка с замерами. Запасной план: база на `master`-ветке EternalJK + порт PBR вручную |
| **Ghoul2 SP↔MP расхождение больше ожидаемого** | Средняя | Высокое | Математика костей не тронута рендерерами (206 строк дельты из 4889) — берём SP-версии как есть. Резерв +3–4 недели |
| **Визуальная несовместимость PBR с ванильными ассетами** | **Высокая** | Среднее | JKA-текстуры запечены с бликами и AO. **Обязательно двухрежимная схема `r_pbr 0/1`** с fallback на ванильную модель. Иначе будет выглядеть хуже оригинала |
| **Мерж-долг с апстримом JKSunny** | Высокая | Среднее | Апстрим не планируется; форк ведётся автономно. Правки апстрима черри-пикаются выборочно, пока это дёшево |
| **Сейвы ломаются при рефакторинге** | Средняя | Высокое | Версионировать формат сейва **до** любых изменений структур. Регресс-скрипт с сейв/лоадом на каждом уровне |
| **World effects API (геймплейные вызовы в рендерере)** окажется сложнее | Средняя | Среднее | Это не графика, а геймплей. Резерв. В крайнем случае — временно вынести погоду обратно на CPU-путь для этих запросов |
| **Шейдерный пайплайн Windows-only тормозит Linux-разработку** | Высокая | Среднее | Фаза 5.3 — приоритезировать выше, если команда работает под Linux |
| Один мейнтейнер `pbr`-ветки перестанет её вести | Средняя | Низкое | Код GPLv2, форк остаётся; но тогда весь техдолг Vulkan-слоя — на вас |

**Юридическое:** всё дерево — **GPLv2 без «or later»** (Raven/Activision 2013). Quake III (GPLv2+) в комбинации схлопывается в GPLv2. Q2RTX в RTX-ветках — тоже GPLv2. **Проект обязан быть GPLv2**, закрыть исходники или уйти в MIT/Apache нельзя. Для JK2/JKA-движка это в любом случае неизбежно.

**Атрибуция:** файлы `vk_*.cpp` в EternalJK несут шапку id/Raven/OpenJK, хотя код на 60–80 % происходит из Quake3e. При заимствовании стоит **восстановить корректную атрибуцию** (id Software + ec-/Quake3e + kennyalive + JKSunny) — это и правильно, и снижает риск претензий.

---

## 11. Что делать на первой неделе

1. Собрать три конфигурации и сравнить руками: `OpenJK@master` + `rd-vanilla` (эталон), `OpenJK@master` + `rd-rend2` (MP), `EternalJK@origin/pbr` (MP, Vulkan + PBR). Замерить FPS на `t2_trip`, `vjun_1`, `yavin`, время старта, время загрузки карты.
2. Прогнать `EternalJK@origin/pbr` под RenderDoc — посмотреть на реальный кадр: сколько draw call'ов, сколько смен пайплайна, где время.
3. Написать JKSunny (Issue или Discussion в его репозитории) — описать проект, спросить про SP-порт.
4. Поднять форк `JACoders/OpenJK`, завести ветку, включить `-DUseAddressSanitizer=ON` в CI-джобу.
5. Сделать самый дешёвый и самый безопасный фикс из всего аудита прямо сейчас: **проверку `patchWidth`/`patchHeight` в `tr_bsp.cpp:489`** — это одна строка, закрывающая RCE.

---

## Приложение: ключевые точки в коде

### OpenJK — ядро
```
code/qcommon/common.cpp:1386          Com_Frame — главный цикл
code/qcommon/common.cpp:693-750       Hunk_* — заглушки, hunk-аллокатора нет
code/qcommon/z_memman_pc.cpp:252      Z_Malloc — обёртка над malloc
code/qcommon/z_memman_pc.cpp:271      аварийное освобождение + Sys_Sleep(1000)
code/qcommon/cm_load.cpp:759          CM_LoadMap_Actual — БЕЗ валидации лампов
code/qcommon/files.cpp:1341           общий unzFile handle — баг параллельного чтения
code/qcommon/msg.cpp:518,951          entityStateFields[68], playerStateFields[63]
code/client/vmachine.cpp:35           VM_Call — всегда 8 va_arg
code/game/g_public.h:149-390          game_import_t — 127 указателей
code/game/g_savegame.cpp:398          EnumerateField — 4 байта в 8-байтовые поля
code/server/sv_main.cpp:431           SV_Frame — фиксированный тик 20 Гц
code/game/g_active.cpp:5191           ClientThink — pmove привязан к FPS
code/client/snd_local.h:33            USE_OPENAL только на MSVC 32-бит
shared/sys/sys_unix.cpp:105           Sys_Milliseconds на gettimeofday (не монотонный)
shared/sys/sys_unix.cpp:182           Sys_LowPhysicalMemory — return qfalse; // TODO
```

### OpenJK — рендер SP
```
code/rd-vanilla/tr_shade.cpp:77       qglBegin(GL_TRIANGLE_STRIP)
code/rd-vanilla/tr_shade.cpp:980      qglVertexPointer — client-side arrays
code/rd-vanilla/tr_ghoul2.cpp:2991    RB_SurfaceGhoul — CPU-скиннинг
code/rd-vanilla/G2_misc.cpp:413       R_TransformEachSurface — 2-й CPU-скиннинг для коллизий
code/rd-vanilla/tr_bsp.cpp:489        ПЕРЕПОЛНЕНИЕ СТЕКА через patchWidth/Height
code/rd-vanilla/G2_bones.cpp:1118     ragdoll-статики — блокер многопоточности
code/rd-vanilla/G2_bones.cpp:2679     ri.SV_Trace — рендерер зовёт сервер
code/rd-common/tr_public.h            refexport_t — 214 функций
```

### rend2
```
codemp/rd-rend2/tr_init.cpp:504       GL 3.2 Core профиль
codemp/rd-rend2/tr_cmds.cpp:507       RE_BeginFrame — fence + persistent ring buffers
codemp/rd-rend2/tr_shade.cpp:571      RB_CreateSortKey — БАГ: биты указателя
codemp/rd-rend2/tr_bsp.cpp:2043       R_CreateWorldVBOs
codemp/rd-rend2/tr_bsp.cpp:3383       R_MergeLeafSurfaces
codemp/rd-rend2/tr_bsp.cpp:3309       128 кубмапов при 6 битах в sort key — БАГ
codemp/rd-rend2/tr_main.cpp:2864      3 каскада PSSM
codemp/rd-rend2/tr_weather.cpp:383    RB_SimulateWeather — transform feedback
codemp/rd-rend2/glsl/lightall.glsl:751 CalcSpecular — GGX/Smith/Schlick
codemp/rd-rend2/glsl/lightall.glsl:944 CalcIBLContribution — split-sum
codemp/rd-rend2/glsl/tonemap.glsl:54  ACESFitted — НАПИСАН, НЕ ВЫЗЫВАЕТСЯ
codemp/rd-rend2/tr_glsl.cpp:2339      GLSL_InitGPUShaders — ~740 программ синхронно
```

### EternalJK rd-vulkan
```
README.md                             родословная, ветки, RTX
codemp/rd-vulkan/vk_local.h:24-45     список известных проблем ОТ АВТОРА
codemp/rd-vulkan/vk_local.h:76        NUM_COMMAND_BUFFERS = 2
codemp/rd-vulkan/vk_instance.cpp:229  apiVersion = VK_API_VERSION_1_1
codemp/rd-vulkan/vk_instance.cpp:554  одна queue family, одна очередь
codemp/rd-vulkan/vk_init.cpp:585      pipeline cache БЕЗ персистентности
codemp/rd-vulkan/vk_frame.cpp:112-562 все render passes
codemp/rd-vulkan/vk_image.cpp:1142    самописный chunk-аллокатор (32 МБ × 256)
codemp/rd-vulkan/vk_vbo.cpp:940       R_BuildMDXM — GPU-скиннинг Ghoul2
codemp/rd-vulkan/vk_vbo_surfacesprites.cpp  GPU-инстансинг + indirect
codemp/rd-vulkan/tr_quicksprite.cpp:151  ДЕФЕКТ: избыточный цикл ×6
codemp/rd-common/tr_public.h:350      4 VK-хука в refimport_t
shared/sdl/sdl_window.cpp:960         SDL2-Vulkan surface
```

### EternalJK ветка `pbr`
```
codemp/rd-vulkan/shaders/glsl/global.h        общий C/GLSL header (254 стр)
codemp/rd-vulkan/shaders/glsl/common/pbr.glsl GGX/Smith/Schlick/IBL (148 стр)
codemp/rd-vulkan/vk_cubemap.cpp               IBL: irradiance + prefiltered + BRDF LUT
codemp/rd-vulkan/vk_normalmap.cpp             compute-нормалмапы
codemp/rd-vulkan/vk_mikktspace.cpp            тангенты
```

### Полезные ссылки
- `JACoders/OpenJK` — https://github.com/JACoders/OpenJK
- `SomaZ/OpenJK` — активная разработка rend2
- `JKSunny/EternalJK` — https://github.com/JKSunny/EternalJK (Vulkan, ветки `pbr`, `rtx-update`)
- `JKSunny/OpenJK` — тот же Vulkan-рендерер, но в дереве OpenJK MP
- `taysta/TaystJK` — преемник EternalJK, уже включает Vulkan
- `ec-/Quake3e` — https://github.com/ec-/Quake3e (первоисточник Vulkan-рендерера)
- `NVIDIA/Q2RTX` — первоисточник RTX-веток
