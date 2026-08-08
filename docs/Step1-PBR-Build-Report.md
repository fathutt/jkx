# Шаг 1: сборка и проверка ветки `pbr` — отчёт

**Что проверяли:** `JKSunny/EternalJK@origin/pbr` (`7762e3c`, 06.08.2026)
**Где:** Ubuntu 24.04, GCC 13.3.0, CMake 3.28, x86_64, Release, 2 ядра
**Дата:** 8 августа 2026

**Вердикт: GO.** Ветка собирается, дефектов-блокеров не обнаружено. Два препятствия к сборке — легаси-зависимости, не имеющие отношения к Vulkan-коду, чинятся за 15 минут. Отдельно получен важный положительный результат: **весь baseline Vulkan 1.3 полностью поддержан программным драйвером lavapipe**, то есть CI сможет валидировать рендерер без GPU.

Оговорка: замеры FPS, времени загрузки карты и прогон RenderDoc здесь невозможны — нужны игровые ассеты и реальный GPU. Эта часть остаётся за тобой, чек-лист в §7.

---

## 1. Сборка: два блокера, оба легаси

Ветка **не собирается «из коробки»** на современном тулчейне. Причина не в Vulkan-коде, а в том, что EternalJK форкнулся от OpenJK до того, как там обновили зависимости.

### Блокер 1 — `lib/gsl-lite` версии 0.0.0

```
lib/gsl-lite/include/gsl/gsl-lite.h:479:18: error: 'reverse_iterator' in namespace 'gsl::std' does not name a template type
... и ещё ~40 ошибок того же класса
```

881 строка, `gsl_lite_VERSION "0.0.0"`. В OpenJK лежит 0.41.0 на 5544 строки. Старая версия не компилируется GCC 13.

**Фикс:** скопировать `lib/gsl-lite/include/gsl/gsl-lite.h` из OpenJK.

### Блокер 2 — `shared/qcommon/safe/*` под старый API gsl-lite

После обновления gsl-lite вылезает следующий слой: в 0.41 `gsl::cstring_span` переименован в `gsl::cstring_view`.

```
shared/qcommon/safe/gsl.h:21:13: error: 'cstring_view' in namespace 'gsl' does not name a type; did you mean 'cstring_span'?
```

**Фикс:** скопировать пять файлов из OpenJK — `files.h`, `gsl.h`, `sscanf.h`, `string.cpp`, `string.h`. Суммарная дельта — 60 строк.

### После двух фиксов

```
[  1%] Built target bundled_minizip
[  9%] Built target botlib
[ 34%] Built target jampgamex86_64
[ 43%] Built target uix86_64
[ 62%] Built target rd-vulkan_x86_64
[ 80%] Built target cgamex86_64
[100%] Built target eternaljk.x86_64
exit=0
```

Собирается всё, без единой ошибки. Полная сборка на 2 ядрах — **41 секунда**.

> Вывод для плана: пункт «привести к современному тулчейну» из фазы 0 стоит не недели, а полдня. Это хорошая новость — я закладывал больше.

---

## 2. Стоимость встроенного SPIR-V — измерена

Главный техдолг ветки подтверждён числами.

| Что | Значение |
|---|---|
| `shaders/spirv/shader_data.c` | **61,7 МБ**, ~1,9 млн строк |
| Блобов SPIR-V в нём | **600** (подсчитано по `^const unsigned char`) |
| Подключается как | `#include "shaders/spirv/shader_data.c"` внутри `vk_shaders.cpp` |
| **Время компиляции `vk_shaders.cpp`** | **32,7 с** |
| **Пиковая память компилятора на этой TU** | **1542 МБ** |
| Для сравнения: `vk_pipelines.cpp` (2341 строка) | **1,0 с** |
| Размер `rd-vulkan_x86_64.so` | **11,4 МБ** |
| Для сравнения: `jampgame.so` / `cgame.so` / движок | 3,4 / 1,5 / 2,0 МБ |

То есть **одна трансляционная единица стоит в 33 раза дороже обычной по времени и требует полтора гигабайта RAM**. На машине с 8 потоками и `make -j8` это означает риск ухода в своп, если параллельно компилируется что-то ещё тяжёлое.

### Влияние на git

| Метрика | Значение |
|---|---|
| `.git` при **поверхностном** клоне на 40 коммитов | **219 МБ** |
| Крупнейшие blob-объекты | 75,5 МБ, 63,0 МБ, 63,0 МБ, 63,0 МБ, 62,9 МБ |
| Ревизий `shader_data.c` в этих 40 коммитах | **8** |

Восемь ревизий по ~63 МБ за сорок коммитов. Полный клон репозитория весит существенно больше, а `git bisect` по истории рендерера практически неработоспособен.

**Подтверждает приоритет:** кроссплатформенный шейдерный тулчейн (фаза 1) идёт **до** любых правок шейдеров, иначе каждая итерация тянет за собой 63 МБ дифа.

---

## 3. Vulkan 1.3 baseline проверен — и это лучше, чем ожидалось

Проверено на `lavapipe` (Mesa LLVM 20.1.2, программный растеризатор), Vulkan Instance 1.3.275 / device 1.4.318.

| Фича, нужная нам по плану | lavapipe |
|---|---|
| `dynamicRendering` | ✅ |
| `synchronization2` | ✅ |
| `timelineSemaphore` | ✅ |
| `descriptorIndexing` | ✅ |
| `runtimeDescriptorArray` | ✅ |
| `shaderSampledImageArrayNonUniformIndexing` | ✅ (+ `...Native`) |
| `descriptorBindingPartiallyBound` | ✅ |
| `descriptorBindingVariableDescriptorCount` | ✅ |
| `maintenance4` | ✅ |
| `dynamicRenderingUnusedAttachments` | ✅ |

И весь набор `VK_EXT_extended_dynamic_state3`, который нужен для сжатия ключа пайплайна:

```
extendedDynamicState3ColorBlendEnable        = true
extendedDynamicState3ColorBlendEquation      = true
extendedDynamicState3ColorWriteMask          = true
extendedDynamicState3PolygonMode             = true
extendedDynamicState3RasterizationSamples    = true
extendedDynamicState3AlphaToCoverageEnable   = true
```

**Практический вывод:** CI-джоба, гоняющая рендерер с валидационными слоями и sync-validation **на программном драйвере, без GPU**, покрывает весь целевой baseline. Это снимает главное возражение против правила «валидация чистая — иначе красный CI» из кодстайла (§8.1): его можно ввести с первого дня, а не «когда появится машина с видеокартой».

### Побочная находка: баг с тихим отключением PBR подтверждён экспериментально

```
maxBoundDescriptorSets = 8
```

А в коде (`vk_init.cpp:517`):

```cpp
if ((!normalMapping && !specularMapping) || vk.maxBoundDescriptorSets < 11)
    vk.useFastLight = qtrue;   // весь PBR выключается, без единого сообщения
```

То есть **на lavapipe PBR отключился бы молча**, и CI-джоба на программном драйвере тестировала бы не тот путь, думая, что тестирует нужный. Это ровно тот класс дефекта, ради которого в кодстайл внесено правило §8.2 «никакой тихой деградации».

Два следствия для плана:

1. Bindless переходит из «желательно» в «обязательно» — иначе PBR не работает не только на части Intel, но и на любом software-фолбэке.
2. До появления bindless CI-джоба обязана **явно проверять**, что PBR-путь активен, и падать, если рендерер молча ушёл в `useFastLight`.

---

## 4. Предупреждения компилятора

`rd-vulkan` в одиночку: **195 предупреждений** на `-Wall` (флаги `-Wextra`/`-Werror` в проекте не включены).

| Тип | Кол-во |
|---|---|
| `-Wsign-compare` | 99 |
| `-Wconversion-null` | 58 |
| `-Wunused-function` | 11 |
| `-Wunused-variable` | 6 |
| `-Wunused-but-set-variable` | 5 |
| `-Wmisleading-indentation` | 5 |
| `-Wclass-memaccess` | 2 |
| `-Warray-bounds=` | 2 |
| `-Wrestrict` | 1 |
| `-Wmaybe-uninitialized` | 1 |
| `-Wdangling-pointer=` | 1 |

### Что заслуживает разбора

**1. `vk_local.h:188` — `-Wconversion-null` в таблице `textureMapTypes[]`.**
Это источник **58 из 58** предупреждений этого типа: заголовок включается везде, предупреждение размножается. Таблица PBR-свизлов инициализируется так, что `NULL` попадает в поле `uint32_t`. Правка на одну строку, минус 30 % всего шума разом.

**2. `tr_world.cpp:690` — `-Warray-bounds`.**
```c
face = (srfSurfaceFace_t*)surfs->data;
dist = GetQuadArea( face->points[0], face->points[1], face->points[2], face->points[3] );
```
`points` объявлен как `float points[1][21]` — классический трейлинг-массив id Tech. Формально это ложное срабатывание, **если** у поверхности действительно ≥ 4 вершины. Проверки нет. Это тот же класс, что 15 дефектов из аудита: данные из BSP используются без валидации границ.

**3. `vk_shade_geometry.cpp:915` — `-Wdangling-pointer`** в `vk_update_depth_range()`, горячий путь смены scissor/viewport. Вероятно ложное срабатывание после инлайна (`vkCmdSetScissor` копирует), но требует проверки — функция вызывается на каждую смену depth range в кадре.

**4. `tr_bsp.cpp:551` — `'buf_p' may be used uninitialized`** в загрузчике BSP.

**5. `tr_bsp.cpp:2099` — `-Wrestrict`:** аргументы 1 и 2 `restrict`-функции алиасят друг друга. Формальный UB.

**6. `G2_bones.cpp:88`, `G2_misc.cpp:1866` — `-Wclass-memaccess`:** `memset`/`memcpy` по нетривиальным типам (`boneInfo_t`, `boltInfo_t`), причём `memcpy` **оставляет 48 байт нетронутыми**.

**7. 99 × `-Wsign-compare`** — фоновый шум, но именно в этом классе живут ошибки вида «отрицательная длина из файла сравнивается с `size_t`».

> Для сравнения: OpenJK SP + rend2 дают 175 предупреждений на всё дерево. То есть плотность предупреждений в `rd-vulkan` заметно выше — унаследовано из Quake3e, где стиль «C в .cpp-файлах».

---

## 5. Метрики кодовой базы `rd-vulkan@pbr`

```
vk_*.cpp/.h    20 291 строк   собственно Vulkan-бэкенд
tr_*.cpp/.h    39 259 строк   фронтенд рендера (порт rd-vanilla MP)
G2_*.cpp/.h    10 930 строк   Ghoul2 — живёт внутри рендерера
GLSL            3 100 строк   32 файла
──────────────────────────
              ~73 600 строк   (без vendored vulkan/, utils/, json)
```

Соотношение подтверждает тезис из технического проекта: **Vulkan-инфраструктура — только 28 % кода**, остальное — фронтенд и Ghoul2, то есть то, что при порте в SP придётся согласовывать с SP-типами. Основная работа фазы 2 — не графика.

---

## 6. Запуск

Бинарник запускается, инициализирует консоль, доходит до `FS_Startup` и корректно останавливается на отсутствии игровых файлов:

```
EternalJK: Aug  8 2026 linux-x86_64
----- FS_Startup -----
0 files in pk3 files
Couldn't load mpdefault.cfg
Automatically freeing 157 blocks making up 65076 bytes
```

Заодно проверено, что механизм аварийного лога работает (`crashlog-*.txt` пишется).

Дальше без ассетов JKA не пройти — это ожидаемо и не является проблемой ветки.

---

## 7. Что осталось за тобой (нужны ассеты и GPU)

Чек-лист, который закрывает go/no-go полностью:

| # | Проверка | Как | На что смотреть |
|---|---|---|---|
| 1 | Сборка под Windows / MSVC 2022 | те же два фикса легаси, скорее всего не понадобятся (MSVC мягче к старому gsl-lite) | собирается ли, сколько предупреждений |
| 2 | Запуск на JKA MP | `/cl_renderer rd-vulkan; vid_restart` | стабильность, артефакты |
| 3 | **Время первого старта и `vid_restart`** | секундомер | это стоимость отсутствующего persistent pipeline cache: 2304 def'а × до 6 пайплайнов |
| 4 | **Время загрузки карты с кубмапами** | карта с `env.json` или `misc_cubemap` | при N пробах это N×6 полных прогонов сцены + свёртка; ожидаю десятки секунд |
| 5 | FPS на `mp/ffa3`, `mp/siege_desert`, любой карте с PBR-ретекстурой | `/cg_drawFPS 1`, `r_speeds` | сравнить с `rd-vanilla` на той же сцене |
| 6 | **Активен ли PBR** | `r_pbr`/`vkinfo`, проверить `useFastLight` | на твоей карте `maxBoundDescriptorSets` должен быть ≥ 11 |
| 7 | RenderDoc: один кадр | — | число draw call'ов, смен пайплайна, где время |
| 8 | Валидационные слои в Debug-сборке | `_DEBUG` + Windows | сколько нарушений на старте и в кадре |
| 9 | Хитчи при первом появлении новых материалов | пройтись по карте, смотреть frametime | подтверждение проблемы ленивого `vk_gen_pipeline()` в середине кадра |

Пункты 3, 4 и 9 — не «интересно посмотреть», а прямая проверка трёх конкретных предположений плана. Если они окажутся мягче ожидаемого, приоритеты фазы 1 сдвигаются.

---

## 8. Что это меняет в плане

| Наблюдение | Следствие |
|---|---|
| Собирается за полдня работы, а не за неделю | Оценка фазы 0 снижается на ~2–3 дня |
| Весь baseline 1.3 + EDS3 работает на lavapipe | **CI с валидацией и без GPU реален с первого дня.** Вводим правило «чистая валидация» сразу |
| `maxBoundDescriptorSets = 8` на software-драйвере → PBR молча выключается | Bindless — обязателен, не опционален. До него CI обязан явно проверять, что PBR-путь активен |
| Одна TU = 32,7 с и 1,5 ГБ; `.git` 219 МБ на 40 коммитов | Шейдерный тулчейн — первым в фазе 1, до любых правок шейдеров. Подтверждено |
| Vulkan-бэкенд — лишь 28 % кода рендерера | Ещё раз подтверждает: главная работа фазы 2 не графическая, а согласование типов и API |
| Дефектов-блокеров не найдено | **GO** на вариант A |

### Патчи, применённые в этой проверке

Для воспроизведения (относительно `origin/pbr`):

```
lib/gsl-lite/include/gsl/gsl-lite.h        ← из JACoders/OpenJK@master (0.41.0)
shared/qcommon/safe/{files,gsl,sscanf,string}.h
shared/qcommon/safe/string.cpp             ← из JACoders/OpenJK@master
```

Конфигурация сборки:

```
cmake .. -DCMAKE_BUILD_TYPE=Release \
  -DBuildMPRdVulkan=ON -DBuildMPRdVanilla=OFF -DBuildMPDed=OFF \
  -DBuildDiscordRichPresence=OFF \
  -DUseInternalSDL2=OFF -DUseInternalZlib=OFF -DUseInternalPNG=OFF -DUseInternalJPEG=OFF
```

Пакеты: `libvulkan-dev`, `mesa-vulkan-drivers`, `vulkan-validationlayers`, `vulkan-tools`, `libsdl2-dev`, `libjpeg-dev`, `libpng-dev`, `zlib1g-dev`, `libopenal-dev`, `libgl1-mesa-dev`.
