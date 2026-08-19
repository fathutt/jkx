#!/usr/bin/env bash
# Point the engine at maps that are deliberately broken, one field at a time.
#
# Everything else on this bench asks whether a correct map draws correctly. This
# asks the other question, which no lane here had ever asked: what happens to a
# map that is WRONG. The answer matters more than it looks, because of where a
# map is loaded from. SV_SpawnServer calls Hunk_Clear before it calls
# CM_LoadMap, so by the time anything has looked at a single byte of the file
# the renderer's world is already gone. A loader that walks off the end of a
# lump there does not produce a rejected map and a menu - it produces a dead
# process, on a map that "sometimes does not work", with no frame left to
# photograph and nothing in the log.
#
# The maps come from make_test_bsp.py --corrupt KIND: the same fixture the rest
# of the bench loads, with exactly one field changed. See the long note above
# CORRUPTIONS there for how the list is derived - it comes out of the format in
# qfiles.h and not out of any loader, on purpose. A test written from the
# checks that exist can only ever agree that they exist.
#
# FOUR OUTCOMES, and telling them apart is the whole job:
#
#   refused   the engine said what was wrong and stayed alive. This is the only
#             passing outcome. It is not "exited non-zero": a map that fails to
#             load in single player is an ERR_DROP, which unwinds to the console
#             and carries on, so the process still quits cleanly at +quit. What
#             separates it from success is that the load did not finish.
#
#   crashed   died on a signal, or a sanitizer had something to say. The lump
#             was read past its end and the process paid for it.
#
#   hung      never came back. A tree walk with a loop in it does not fault -
#             it runs forever, and a lane that only checks the exit code reads
#             that as a slow machine.
#
#   loaded    accepted the broken map and carried on as if it were whole. The
#             quietest of the four and not the least dangerous: whatever the
#             corrupt field pointed at was read, and what came back was
#             whatever happened to be in memory after the lump.
#
#             (There is a fifth, "quiet": the map did not come up and nothing in
#             the log says why. Nothing produces it today, and it is here
#             because a refusal with no message is a bug report nobody can act
#             on and should not be allowed to hide among the passes.)
#
# The kinds that are NOT refused today are listed in tools/ci/badbsp-baseline.txt
# with what they do instead. That file is the point of this script rather than
# an apology for it: a strip that comes up green on the day it is written is a
# strip nobody has shown to work. It shrinks, it does not grow - a kind that
# starts crashing without an entry fails the stage, and a kind whose entry has
# become wrong because the engine now refuses it is reported as good news.
#
# Usage:
#   tools/verify/bad_bsp.sh <build dir> [kind ...]
#   tools/verify/bad_bsp.sh <build dir> --write-baseline
#
#   JKX_BADBSP_DISPLAY   which Xvfb display    (default :89)
#   JKX_BADBSP_PAK       a prebuilt shaders.pak, to skip a minute of glslc
#   JKX_BADBSP_TIMEOUT   seconds per run       (default: from the control run)
#   JKX_BADBSP_KEEP      keep the run directory and every log

set -uo pipefail

BUILD="${1:-}"
if [ -z "$BUILD" ] || [ ! -d "$BUILD" ]; then
    echo "usage: $0 <build dir> [kind ...]" >&2
    exit 2
fi
shift

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
ARCH="$(uname -m)"
BASELINE="$ROOT/tools/ci/badbsp-baseline.txt"

WRITE_BASELINE=0
if [ "${1:-}" = "--write-baseline" ]; then
    WRITE_BASELINE=1
    shift
fi

ENGINE="$BUILD/jkx_jka.$ARCH"
GAME="$BUILD/games/jka/game/jkagame$ARCH.so"
RENDERER="$BUILD/code/rd-vulkan/rdsp-vulkan_$ARCH.so"

[ -f "$ENGINE" ] || { echo "not built: $ENGINE" >&2; exit 2; }
[ -f "$RENDERER" ] || RENDERER=""

# One game and not both. jkx_jk2 is the same qcommon and the same rd-vulkan -
# the BSP loaders are shared code with no JK2_MODE branch in them - so running
# the second engine over the same twenty-two maps would double the time and ask
# the same question twice.
KINDS=()
if [ "$#" -gt 0 ]; then
    if [ "$WRITE_BASELINE" = "1" ]; then
        echo "--write-baseline takes the whole list, not a subset of it" >&2
        exit 2
    fi
    KINDS=( "$@" )
fi
if [ "${#KINDS[@]}" -eq 0 ]; then
    mapfile -t KINDS < <( python3 "$HERE/make_test_bsp.py" --corrupt-list |
                          awk '{ print $1 }' )
fi
if [ "${#KINDS[@]}" -eq 0 ]; then
    echo "make_test_bsp.py --corrupt-list produced nothing" >&2
    exit 2
fi

RUN="$(mktemp -d)"
DISPLAY_NUM="${JKX_BADBSP_DISPLAY:-:89}"
XVFB_PID=""
cleanup() {
    [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
    if [ -n "${JKX_BADBSP_KEEP:-}" ]; then
        echo "  run directory kept: $RUN"
    else
        rm -rf "$RUN"
    fi
}
trap cleanup EXIT

mkdir -p "$RUN/home" "$RUN/xdg"
cp -r "$HERE/fixtures/base" "$RUN/base"
python3 "$HERE/make_item_tables.py" --game jka --out "$RUN/base/ext_data" >/dev/null
cp "$ENGINE" "$RUN/"
[ -n "$RENDERER" ] && cp "$RENDERER" "$RUN/"
[ -f "$GAME" ] && cp "$GAME" "$RUN/"

# The shader pak, once for every run rather than once per run. Building it is
# the most expensive thing here by a wide margin - six hundred glslc
# invocations, most of a minute - and none of these runs is about shaders.
#
# The build tree already has one, and using it is not a shortcut: it is the pak
# that build produced, so a run here is the same renderer the smoke lanes get.
# Building a second copy would take longer than the twenty-two engine runs put
# together.
PAK="${JKX_BADBSP_PAK:-$BUILD/base/shaders.pak}"
if [ -f "$PAK" ]; then
    cp "$PAK" "$RUN/base/shaders.pak"
else
    python3 "$ROOT/tools/shadergen/shadergen.py" build --out "$RUN/spv" >/dev/null &&
    python3 "$ROOT/tools/shadergen/shadergen.py" pack --spv-dir "$RUN/spv" \
        --out "$RUN/base/shaders.pak" >/dev/null || {
        echo "could not build the shader pak" >&2
        exit 2
    }
fi

# The validation layer is deliberately NOT enabled here, which is the opposite
# of every other lane. This one runs the engine over twenty-two maps that are
# expected to go wrong inside the loader, and a layer that reports on every draw
# call turns a one-second run into a minute of noise about a frame that is not
# what is being measured. Vulkan correctness is the smoke lanes' job.
export VK_LOADER_LAYERS_ENABLE=""
export VK_INSTANCE_LAYERS=""

Xvfb "$DISPLAY_NUM" -screen 0 640x480x24 >/dev/null 2>&1 &
XVFB_PID=$!
sleep 2
if ! kill -0 "$XVFB_PID" 2>/dev/null; then
    echo "Xvfb did not start on $DISPLAY_NUM - is another one already using it?" >&2
    exit 2
fi

# What a finished load looks like, and it is a QUESTION rather than a message.
#
# The first version of this read the loaders' own progress lines - "Game
# Initialization" from the server and "loaded N faces" from the renderer - and
# called a map loaded when both had appeared. That is wrong in the one place it
# matters. R_LoadWorldMap walks the lumps in a fixed order and the surfaces come
# early: nodes, marksurfaces, submodels and visibility are all read AFTER the
# line that says how many faces there were. A map refused at the node lump
# therefore prints both marks and then dies, and the strip read that as "loaded
# silently" while the engine was in fact rejecting it correctly. Three kinds
# were reported as holes that are not holes.
#
# So the run asks the game instead. "viewpos" is registered by cgame when the
# client enters the world; it answers with the map name and where the camera is,
# and it is an unknown command when there is no world. Nothing about it is a
# statement the loader makes about itself - it is the client being in a map or
# not, which is the thing actually being asked.
IN_MAP='maps/jkx_bad\.bsp \('

# Still read, but only to say WHERE a crash happened: the collision loader runs
# first and prints when it hands the map to the game, so a fault with this line
# in the log is the renderer's and one without it is CM_LoadMap's.
COLLISION_MARK='------- Game Initialization -------'

# The engine, once, on whatever is in base/maps/jkx_bad.bsp.
#
# Its stderr is dropped and the shell's is not: what would otherwise appear is
# bash announcing "Segmentation fault" followed by the whole command line, once
# per crashing kind, which buries the table this script exists to print. The
# crash is not being hidden - it is the outcome column, and the log is in the
# run directory that JKX_BADBSP_KEEP keeps.
engine_run() {
    local log="$1"
    ( cd "$RUN" && \
      DISPLAY="$DISPLAY_NUM" \
      XDG_RUNTIME_DIR="$RUN/xdg" \
      VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}" \
      timeout -k 5 "$TIMEOUT" "./$(basename "$ENGINE")" \
          +set fs_basepath "$RUN" +set fs_homepath "$RUN/home" \
          +set s_initsound 0 +set com_errorDialog 0 +set con_notifytime 0 \
          +set cg_hudFiles ui/jkx_hud.txt +set g_char_model jkx \
          +set helpUsObi 1 +set r_drawfog 0 +set r_ext_multisample 0 \
          +wait 20 +map jkx_bad +wait 100 +viewpos +wait 5 +quit ) > "$log" 2>&1
    return $?
} 2>/dev/null

# What the engine said about it, first line only, with the timestamp and the
# colour codes taken off. Com_Error prints a banner of asterisks and then the
# message; the message is the part a person can act on.
said() {
    grep -m1 -E 'ERROR: ' "$1" |
        sed -e 's/^[0-9-]* [0-9:]* //' -e 's/\^[0-9]//g' -e 's/^ERROR: //' \
            -e 's| in maps/jkx_bad\.bsp||' |
        cut -c1-72
}

# 128 + n, and the n is worth naming. "Died with status 139" is a number a
# person has to look up; SIGSEGV is the thing that happened.
signal_name() {
    case "$1" in
        139) echo "SIGSEGV" ;;
        134) echo "SIGABRT" ;;
        136) echo "SIGFPE" ;;
        132) echo "SIGILL" ;;
        135) echo "SIGBUS" ;;
        *)   echo "status $1" ;;
    esac
}

classify() {
    local rc="$1" log="$2"

    # Signals first, and from two places. The shell reports 128+n when the
    # process dies on one, but the engine installs handlers of its own and a
    # build that catches SIGSEGV and exits tidily is still a build that
    # segfaulted - so the log is read as well as the status.
    if grep -qE 'Segmentation fault|SIGSEGV|SIGABRT|Received signal|stack smashing|corrupted (double-linked|size)|AddressSanitizer|UndefinedBehaviorSanitizer' "$log"; then
        echo "crashed"
        return
    fi
    case "$rc" in
        124|137) echo "hung"; return ;;
    esac
    if [ "$rc" -ge 128 ]; then
        echo "crashed"
        return
    fi

    if grep -qE -- "$IN_MAP" "$log"; then
        echo "loaded"
        return
    fi
    # No world, no signal, and still a non-zero exit: the engine did not reach
    # the +quit it was given, which is an ERR_FATAL or an exit() somewhere in
    # the load. Counted with the crashes rather than with the refusals on
    # purpose - the requirement is that a broken map costs the player his map,
    # not his session, and this costs him the session.
    if [ "$rc" -ne 0 ]; then
        echo "crashed"
        return
    fi
    if [ -n "$( said "$log" )" ]; then
        echo "refused"
    else
        # Did not load it and did not say why. A player sees a map that does
        # nothing, which is the hardest of all of these to report.
        echo "quiet"
    fi
}

# The control, and it is not a formality. A lane that rejects every map passes
# every safety check ever written, so the first thing measured here is that the
# UNBROKEN fixture still loads all the way through both loaders. If this fails
# nothing below it means anything and the script says so rather than reporting
# twenty-two refusals as a triumph.
TIMEOUT="${JKX_BADBSP_TIMEOUT:-120}"
python3 "$HERE/make_test_bsp.py" "$RUN/base/maps/jkx_bad.bsp" >/dev/null || exit 2

control_started=$( date +%s )
engine_run "$RUN/control.log"
control_rc=$?
control_seconds=$(( $( date +%s ) - control_started ))
control="$( classify "$control_rc" "$RUN/control.log" )"

printf 'control: an unbroken fixture map -> %s (%ss)\n' "$control" "$control_seconds"
if [ "$control" != "loaded" ]; then
    echo "the control map did not load, so nothing below this line is a"
    echo "statement about corruption. Run tools/verify/smoke_headless.sh first."
    said "$RUN/control.log"
    exit 1
fi

# How long a run is allowed before it counts as hung, derived from the control
# rather than picked. A good load is a second or two on this bench; twenty times
# that plus twenty seconds leaves room for a loaded machine and still catches a
# tree walk that never terminates - which is the only thing this number has to
# separate.
if [ -z "${JKX_BADBSP_TIMEOUT:-}" ]; then
    TIMEOUT=$(( control_seconds * 20 + 20 ))
fi
echo "a run gets ${TIMEOUT}s before it counts as hung"
echo

# The baseline: kind and what it does today, for the kinds that are not refused.
declare -A EXPECTED=()
if [ -f "$BASELINE" ]; then
    # A line is a kind and the outcome (or outcomes) it has today. More than one
    # is allowed and is not sloppiness: an out-of-bounds READ has no defined
    # behaviour, so what follows the lump decides the outcome, and one of these
    # kinds genuinely alternates between being swallowed and being tripped over
    # from run to run. A gate that demanded one of the two would be flaky at
    # exactly the place where the defect is worst.
    while read -r kind rest; do
        case "$kind" in ""|\#*) continue ;; esac
        EXPECTED["$kind"]="$rest"
    done < "$BASELINE"
fi

declare -A OUTCOME=()
declare -A DETAIL=()
regressions=()
improvements=()

printf '%-24s %-8s %s\n' "corruption" "outcome" "what the engine said"
printf '%-24s %-8s %s\n' "------------------------" "--------" \
       "----------------------------------------"

for kind in "${KINDS[@]}"; do
    python3 "$HERE/make_test_bsp.py" "$RUN/base/maps/jkx_bad.bsp" \
        --corrupt "$kind" >/dev/null || { echo "cannot generate: $kind"; exit 2; }

    log="$RUN/log-$kind.txt"
    engine_run "$log"
    rc=$?
    outcome="$( classify "$rc" "$log" )"
    detail="$( said "$log" )"
    where=""
    # Which of the two loaders was holding it. The collision side runs first and
    # prints when it hands over, so a crash with that line in the log is the
    # renderer's and a crash without it is CM_LoadMap's. Free to record and the
    # first thing anyone will want to know.
    where="in the collision loader"
    if grep -q -- "$COLLISION_MARK" "$log"; then
        where="in the renderer, past CM_LoadMap"
    fi

    if [ -z "$detail" ]; then
        case "$outcome" in
            crashed) detail="died on $( signal_name "$rc" ), $where" ;;
            hung)    detail="still running after ${TIMEOUT}s, $where" ;;
            loaded)  detail="loaded, and the client stood in it, without a word" ;;
            *)       detail="-" ;;
        esac
    fi

    OUTCOME["$kind"]="$outcome"
    DETAIL["$kind"]="$detail"

    expected="${EXPECTED[$kind]:-}"
    note=""
    if [ "$outcome" != "refused" ]; then
        if [[ " $expected " == *" $outcome "* ]]; then
            note="   (expected today)"
        else
            regressions+=( "$kind: $outcome, and the baseline says ${expected:-refused}" )
            note="   <<< NEW"
        fi
    elif [ -n "$expected" ] && [[ " $expected " != *" refused "* ]]; then
        improvements+=( "$kind: was $expected, is now refused" )
        note="   <<< fixed"
    fi

    printf '%-24s %-8s %s%s\n' "$kind" "$outcome" "$detail" "$note"
done

echo

if [ "$WRITE_BASELINE" = "1" ]; then
    {
        echo "# What the engine does today with a BSP broken one field at a time."
        echo "# Written by tools/verify/bad_bsp.sh --write-baseline; see the note at"
        echo "# the top of that script. This file may shrink, never grow: a kind"
        echo "# listed here is a hole somebody has to close, and a kind that starts"
        echo "# failing without a line here fails the stage."
        echo "#"
        echo "# This header replaced whatever notes were above the list. Put them back:"
        echo "# what a line means is not in the line."
        echo "#"
        echo "# kind                     outcome"
        for kind in "${KINDS[@]}"; do
            if [ "${OUTCOME[$kind]}" != "refused" ]; then
                printf '%-25s %s\n' "$kind" "${OUTCOME[$kind]}"
            fi
        done
    } > "$BASELINE"
    echo "wrote $BASELINE"
fi

refused=0
for kind in "${KINDS[@]}"; do
    [ "${OUTCOME[$kind]}" = "refused" ] && refused=$(( refused + 1 ))
done
echo "$refused of ${#KINDS[@]} corruptions were refused with a message"

if [ "${#improvements[@]}" -gt 0 ]; then
    echo
    echo "good: the engine now refuses ${#improvements[@]} kind(s) the baseline gives up on:"
    printf '  %s\n' "${improvements[@]}"
    echo "  lock it in: tools/verify/bad_bsp.sh <build> --write-baseline"
fi

if [ "${#regressions[@]}" -gt 0 ]; then
    echo
    echo "FAILED: ${#regressions[@]} corruption(s) do something the baseline does not allow:"
    printf '  %s\n' "${regressions[@]}"
    exit 1
fi

exit 0
