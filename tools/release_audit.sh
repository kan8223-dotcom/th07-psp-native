#!/bin/sh
set -eu

fail=0
debug_eboot_pattern='TH07 PSP stage debug|TH07 PSP music perf|TH07 PSP perf diag|direct game|direct transition test|text render [0-9]|se_power0 caller|PERF S[0-9]'
forbidden_pattern='(^|/)(th07\.dat|thbgm\.dat|music_bg\.rgb565|title01\.psp1000\.(cache|tmp)|msgothic\.ttc|score\.dat|th07\.cfg|TH07PSP_BOOT\.LOG)$|\.rpy$'

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
            for required in EBOOT.PBP NotoSansJP-Regular.ttf README.md CREDITS.md CHANGELOG.md LICENSE docs/KNOWN_ISSUES.md; do
                if ! printf '%s\n' "$archive_entries" | grep -q "^TH07PSP/$required$"; then
                    echo "[FAIL] $archive does not contain TH07PSP/$required"
                    fail=1
                fi
            done
            ;;
        esac
        case "$archive_name" in
        th07-psp-native-v0.1.5-beta*.zip)
            if ! printf '%s\n' "$archive_entries" | grep -q '^TH07PSP/README_EN.md$'; then
                echo "[FAIL] $archive does not contain TH07PSP/README_EN.md"
                fail=1
            fi
            ;;
        esac
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
