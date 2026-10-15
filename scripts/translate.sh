#!/bin/bash
# Translate the DOL and the 15 RSO overlay profiles into generated/ (about
# 3.5 min; logs in logs/). Run it after adding or removing a native
# registration or editing symbols.txt, recomp.yml or runtime/src/tick_sites.inc,
# then rebuild. TRANSLATE_JOBS (default: all CPUs) and OVERLAY_JOBS (default:
# what the available memory allows, at most 5) set the parallelism; DOTNET_BIN and WIICOMPILED_ROOT select the dotnet binary
# and the translator tree (default toolchain/).
set -euo pipefail
cd "$(dirname "$0")/.."
ulimit -c 0
wiicompiled_root="${WIICOMPILED_ROOT:-toolchain}"
dotnet_bin="${DOTNET_BIN:-$PWD/tools/dotnet/dotnet}"
translator="$wiicompiled_root/translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll"
mkdir -p logs generated
# Written last: a translation stopped part way (an overlay killed for memory)
# is not taken for a finished one by build-linux.sh.
rm -f generated/.translation-complete
python3 scripts/inspect_disc.py
python3 scripts/generate_symbols.py
"$dotnet_bin" "$translator" generate-data-init --project recomp.yml 2>&1 | tee logs/data-init.log
python3 scripts/prepare_rso_translation.py
"$dotnet_bin" "$translator" translate-recursive 0x80006310 \
    --project build/recomp-analysis.yml --outdir generated/functions \
    --output-metadata generated/translation.json --threads "${TRANSLATE_JOBS:-$(getconf _NPROCESSORS_ONLN)}" --prune-stale 2>&1 | tee logs/translate.log
python3 scripts/prepare_native_dispatch.py --force --dotnet "$dotnet_bin" --translator "$translator" \
    2>&1 | tee logs/shards.log
# The overlay profiles are independent (each has its own work directory and
# output file): translate them in parallel, OVERLAY_JOBS at a time. Each
# translator takes up to about 6 GB at its peak, so by default as many run
# at once as the available memory allows (the kernel kills them otherwise).
if [[ -z ${OVERLAY_JOBS:-} ]]; then
    available_kb=$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo 2>/dev/null || echo 0)
    OVERLAY_JOBS=$(( available_kb / (6 * 1024 * 1024) ))
    (( OVERLAY_JOBS < 1 )) && OVERLAY_JOBS=1
    (( OVERLAY_JOBS > 5 )) && OVERLAY_JOBS=5
    echo "Translating the overlays $OVERLAY_JOBS at a time ($(( available_kb / 1024 / 1024 )) GB available; OVERLAY_JOBS overrides)"
fi
overlay_profiles="map_village quest_field enemy_field map00_field map01_field map02_field map03_field
    map04_field map05_field map06_field map07_field map08_field map09_field map10_field map11_field net_data map_town_online map_town_quest"
printf '%s\n' $overlay_profiles | xargs -P "${OVERLAY_JOBS:-5}" -I{} sh -c \
    'python3 scripts/translate_rso_overlays.py --profile {} > logs/{}-translate.log 2>&1 ||
     { echo "overlay {} failed (logs/{}-translate.log)"; tail -5 logs/{}-translate.log;
       grep -q SIGKILL logs/{}-translate.log && echo "(killed: out of memory? rerun with OVERLAY_JOBS=1)"; exit 255; }'
touch generated/.translation-complete
