#!/bin/sh
set -eu

fail=0
debug_eboot_pattern='TH07 PSP stage debug|TH07 PSP music perf|TH07 PSP perf diag|direct game|direct transition test|text render [0-9]|se_power0 caller|PERF S[0-9]'
forbidden_pattern='(^|/)(th07\.dat|thbgm\.dat|music_bg\.rgb565|title01\.psp1000\.(cache|tmp)|msgothic\.ttc|score\.dat|th07\.cfg|TH07PSP_BOOT\.LOG|SETTINGS\.TXT)$|\.rpy$'

archive_member_hash()
{
    archive=$1
    member=$2
    entry=$(unzip -Z1 "$archive" | awk -v wanted="TH07PSP/$member" '
        {
            normalized = $0
            gsub(/\\/, "/", normalized)
            if (normalized == wanted) {
                print $0
                exit
            }
        }')
    [ -n "$entry" ] || return 1
    # Compress-Archive stores Windows path separators. Info-ZIP treats a
    # backslash in a member pattern as an escape, so double it before reading.
    unzip_pattern=$(printf '%s\n' "$entry" | sed 's/\\/\\\\/g')
    unzip -p "$archive" "$unzip_pattern" 2>/dev/null | sha256sum | awk '{print $1}'
}

compare_archive_source()
{
    archive=$1
    member=$2
    source=$3
    if [ ! -f "$source" ]; then
        echo "[FAIL] source file for TH07PSP/$member is missing: $source"
        fail=1
        return
    fi
    if ! archived_hash=$(archive_member_hash "$archive" "$member"); then
        echo "[FAIL] $archive cannot read TH07PSP/$member"
        fail=1
        return
    fi
    source_hash=$(sha256sum "$source" | awk '{print $1}')
    if [ "$archived_hash" != "$source_hash" ]; then
        echo "[FAIL] $archive TH07PSP/$member is stale or differs from $source"
        fail=1
    fi
}

tracked_forbidden=$(git ls-files | tr '\\' '/' | grep -Ei "$forbidden_pattern" || true)
if [ -n "$tracked_forbidden" ]; then
    echo "[FAIL] proprietary/user/generated files are tracked:"
    echo "$tracked_forbidden"
    fail=1
fi

if [ -f EBOOT.PBP ] && strings EBOOT.PBP | grep -Eq "$debug_eboot_pattern"; then
    echo "[FAIL] stage-debug marker entered EBOOT.PBP"
    fail=1
fi

if [ -d dist ]; then
    release_forbidden=$(find dist -type f | tr '\\' '/' | grep -Ei "$forbidden_pattern" || true)
    if [ -n "$release_forbidden" ]; then
        echo "[FAIL] proprietary/user files entered dist/:"
        echo "$release_forbidden"
        fail=1
    fi
    for archive in dist/*.zip; do
        [ -f "$archive" ] || continue
        archive_entries=$(unzip -Z1 "$archive" | tr '\\' '/')
        archive_forbidden=$(printf '%s\n' "$archive_entries" | grep -Ei "$forbidden_pattern" || true)
        if [ -n "$archive_forbidden" ]; then
            echo "[FAIL] proprietary/user files entered $archive:"
            echo "$archive_forbidden"
            fail=1
        fi
        archive_name=$(basename "$archive")
        case "$archive_name" in
        th07-psp-native-v*-beta.zip|th07-psp-native-v*-beta-psp1000.zip|th07-psp-native-v*-beta-psp2000plus.zip)
            for required in EBOOT.PBP NotoSansJP-Regular.ttf README.md README_EN.md CREDITS.md CHANGELOG.md LICENSE \
                docs/KNOWN_ISSUES.md licenses/NotoSansJP/OFL.txt licenses/MECC/LICENSE.md; do
                if ! printf '%s\n' "$archive_entries" | grep -q "^TH07PSP/$required$"; then
                    echo "[FAIL] $archive does not contain TH07PSP/$required"
                    fail=1
                fi
            done
            for mapping in \
                NotoSansJP-Regular.ttf\|psp/assets/NotoSansJP-Regular.ttf \
                README.md\|README.md \
                README_EN.md\|README_EN.md \
                CREDITS.md\|CREDITS.md \
                CHANGELOG.md\|CHANGELOG.md \
                LICENSE\|LICENSE \
                docs/KNOWN_ISSUES.md\|docs/KNOWN_ISSUES.md \
                licenses/NotoSansJP/OFL.txt\|licenses/NotoSansJP/OFL.txt \
                licenses/MECC/LICENSE.md\|psp/third_party/me-custom-core/LICENSE.md; do
                member=${mapping%%|*}
                source=${mapping#*|}
                compare_archive_source "$archive" "$member" "$source"
            done
            ;;
        esac
        sidecar="$archive.EBOOT.sha256"
        if [ ! -f "$sidecar" ]; then
            echo "[FAIL] $archive has no EBOOT hash sidecar"
            fail=1
        else
            sidecar_hash=$(awk 'NR == 1 {print $1}' "$sidecar")
            archive_eboot_hash=$(unzip -p "$archive" '*EBOOT.PBP' 2>/dev/null |
                sha256sum | awk '{print $1}')
            if [ "$sidecar_hash" != "$archive_eboot_hash" ]; then
                echo "[FAIL] $archive EBOOT does not match $sidecar"
                fail=1
            fi
        fi
        case "$archive_name" in
        th07-psp-native-v*-beta-psp1000.zip)
            if ! unzip -p "$archive" '*EBOOT.PBP' 2>/dev/null | strings | grep -q 'BUILD PSP1000 pools'; then
                echo "[FAIL] PSP-1000 profile marker missing from $archive"
                fail=1
            fi
            if ! unzip -p "$archive" '*EBOOT.PBP' 2>/dev/null | strings | grep -q 'Touhou 7 PSP-1000 Beta'; then
                echo "[FAIL] PSP-1000 XMB title missing from $archive"
                fail=1
            fi
            if printf '%s\n' "$archive_entries" | grep -Eq '^TH07PSP/(ARK5_HIGHMEM_SNIPPET\.txt|docs/ARK5_HIGH_MEMORY\.md)$'; then
                echo "[FAIL] ARK-5 high-memory instructions entered PSP-1000 archive $archive"
                fail=1
            fi
            ;;
        th07-psp-native-v*-beta-psp2000plus.zip)
            if unzip -p "$archive" '*EBOOT.PBP' 2>/dev/null | strings | grep -q 'BUILD PSP1000 pools'; then
                echo "[FAIL] PSP-1000 profile entered PSP-2000+ archive $archive"
                fail=1
            fi
            if ! unzip -p "$archive" '*EBOOT.PBP' 2>/dev/null | strings | grep -q 'Touhou 7 PSP-2000+ Beta'; then
                echo "[FAIL] PSP-2000+ XMB title missing from $archive"
                fail=1
            fi
            for required in ARK5_HIGHMEM_SNIPPET.txt docs/ARK5_HIGH_MEMORY.md; do
                if ! printf '%s\n' "$archive_entries" | grep -q "^TH07PSP/$required$"; then
                    echo "[FAIL] $archive does not contain TH07PSP/$required"
                    fail=1
                fi
            done
            compare_archive_source "$archive" ARK5_HIGHMEM_SNIPPET.txt ark/ARK5_HIGHMEM_SNIPPET.txt
            compare_archive_source "$archive" docs/ARK5_HIGH_MEMORY.md docs/ARK5_HIGH_MEMORY.md
            ark_rule=$(unzip -p "$archive" '*ARK5_HIGHMEM_SNIPPET.txt' 2>/dev/null |
                grep -Ec '^homebrew, highmem, on$' || true)
            ark_active=$(unzip -p "$archive" '*ARK5_HIGHMEM_SNIPPET.txt' 2>/dev/null |
                grep -Evc '^[[:space:]]*(#|$)' || true)
            if [ "$ark_rule" -ne 1 ] || [ "$ark_active" -ne 1 ]; then
                echo "[FAIL] $archive ARK-5 snippet is not exactly one scoped active highmem rule"
                fail=1
            fi
            if ! unzip -p "$archive" '*ARK5_HIGHMEM_SNIPPET.txt' 2>/dev/null |
                grep -Fq 'Do not replace your complete SETTINGS.TXT'; then
                echo "[FAIL] $archive ARK-5 snippet lacks the merge-only warning"
                fail=1
            fi
            ;;
        esac
        if unzip -p "$archive" '*EBOOT.PBP' 2>/dev/null | strings | grep -Eq "$debug_eboot_pattern"; then
            echo "[FAIL] stage-debug marker entered $archive"
            fail=1
        fi
    done
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi

echo "[OK] release audit: no TH07 data, user state, or stage-debug EBOOT"
