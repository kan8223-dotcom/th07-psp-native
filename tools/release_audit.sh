#!/bin/sh
set -eu

fail=0
formal_title='東方妖々夢 ～ Perfect Cherry Blossom.'
psp1000_sha='d49f1683f370224e102b13c8a14a1d09d9bead77d55bff449ed26f0b65c08ef6'
psp2000plus_sha='356fbd32ee75dced8b1c9384b31a47613d1848ebd6a2af0b3b21cc92ba8e5a3d'
unified_sha='822e0a4c43ac84509a25af16d921b0bb9bcb1c4597dcdbb315e9583d5e92fad4'
forbidden_pattern='(^|/)(th07\.dat|thbgm\.dat|music_bg\.rgb565|title01\.psp1000\.(cache|tmp)|score\.dat|th07\.cfg|TH07PSP_BOOT\.LOG|TH07UNIFIED\.LOG|TH07RUNTIME\.PBP|SETTINGS\.TXT|ICON0\.PNG|ICON1\.PMF|PIC0\.PNG|PIC1\.PNG|SND0\.AT3|\.TH07XMB\.(TMP|BAK))$|\.rpy$'
msgothic_font_pattern='(^|/)msgothic[^/]*\.(ttc|ttf|otf)$'
approved_tracked_subset='psp/assets/msgothic-subset.ttf'
approved_archive_subset='TH07PSP/msgothic-subset.ttf'

archive_member_entry()
{
    archive=$1
    member=$2
    unzip -Z1 "$archive" | awk -v wanted="TH07PSP/$member" '
        {
            original = $0
            normalized = $0
            gsub(/\\/, "/", normalized)
            if (normalized == wanted) {
                print original
                exit
            }
        }'
}

archive_member_hash()
{
    archive=$1
    member=$2
    entry=$(archive_member_entry "$archive" "$member")
    [ -n "$entry" ] || return 1
    unzip_pattern=$(printf '%s\n' "$entry" | sed 's/\\/\\\\/g')
    unzip -p "$archive" "$unzip_pattern" 2>/dev/null | sha256sum | awk '{print $1}'
}

extract_archive_member()
{
    archive=$1
    member=$2
    destination=$3
    entry=$(archive_member_entry "$archive" "$member")
    [ -n "$entry" ] || return 1
    unzip_pattern=$(printf '%s\n' "$entry" | sed 's/\\/\\\\/g')
    unzip -p "$archive" "$unzip_pattern" > "$destination"
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

# The accepted compatibility filename contains an OFL Noto derivative.  Every
# other msgothic-named font is a possible user-local Microsoft artifact and
# remains forbidden. The exact approved file is independently hash-audited.
tracked_msgothic=$(git ls-files | tr '\\' '/' | grep -Ei "$msgothic_font_pattern" || true)
tracked_private_msgothic=$(printf '%s\n' "$tracked_msgothic" | grep -Ev "^$approved_tracked_subset$" || true)
if [ -n "$tracked_private_msgothic" ]; then
    echo "[FAIL] private/unapproved msgothic font is tracked:"
    echo "$tracked_private_msgothic"
    fail=1
fi

if ! python3 tools/build_release_noto_subset.py --check; then
    fail=1
fi

if ! git ls-files -z | xargs -0 python3 tools/check_no_original_assets.py; then
    fail=1
fi

tmp_root=$(mktemp -d)
trap 'rm -rf "$tmp_root"' EXIT HUP INT TERM

if [ "$#" -gt 0 ]; then
    archives=$*
else
    archives=$(find dist -maxdepth 1 -type f -name 'th07-psp-native-v*.zip' -print 2>/dev/null || true)
fi

archive_count=0
for archive in $archives; do
    [ -f "$archive" ] || continue
    archive_count=$((archive_count + 1))
    archive_name=$(basename "$archive")
    case "$archive_name" in
        *beta*|*tester*|*psp1000*|*psp2000plus*)
            echo "[FAIL] obsolete model/Beta archive name: $archive_name"
            fail=1
            ;;
        th07-psp-native-v*.zip)
            ;;
        *)
            echo "[FAIL] unexpected release archive name: $archive_name"
            fail=1
            ;;
    esac

    archive_entries=$(unzip -Z1 "$archive" | tr '\\' '/')
    archive_forbidden=$(printf '%s\n' "$archive_entries" | grep -Ei "$forbidden_pattern" || true)
    if [ -n "$archive_forbidden" ]; then
        echo "[FAIL] proprietary/user/generated files entered $archive:"
        echo "$archive_forbidden"
        fail=1
    fi
    archive_msgothic=$(printf '%s\n' "$archive_entries" | grep -Ei "$msgothic_font_pattern" || true)
    archive_private_msgothic=$(printf '%s\n' "$archive_msgothic" | grep -Ev "^$approved_archive_subset$" || true)
    if [ -n "$archive_private_msgothic" ]; then
        echo "[FAIL] private/unapproved msgothic font entered $archive:"
        echo "$archive_private_msgothic"
        fail=1
    fi

    required_files='EBOOT.PBP
NotoSansJP-Regular.ttf
msgothic-subset.ttf
README.md
README_EN.md
CREDITS.md
CHANGELOG.md
LICENSE
ARK5_HIGHMEM_SNIPPET.txt
docs/KNOWN_ISSUES.md
docs/ARK5_HIGH_MEMORY.md
docs/PSP_RELEASE_FONTS.md
licenses/NotoSansJP/OFL.txt
licenses/NotoSansJP/FONTLOG-TH07PSP.txt
licenses/MECC/LICENSE.md'
    for required in $required_files; do
        count=$(printf '%s\n' "$archive_entries" | grep -Ec "^TH07PSP/$required$" || true)
        if [ "$count" -ne 1 ]; then
            echo "[FAIL] $archive must contain exactly one TH07PSP/$required (found $count)"
            fail=1
        fi
    done

    unexpected=$(printf '%s\n' "$archive_entries" | awk '
        /\/$/ { next }
        $0 == "TH07PSP/EBOOT.PBP" { next }
        $0 == "TH07PSP/NotoSansJP-Regular.ttf" { next }
        $0 == "TH07PSP/msgothic-subset.ttf" { next }
        $0 == "TH07PSP/README.md" { next }
        $0 == "TH07PSP/README_EN.md" { next }
        $0 == "TH07PSP/CREDITS.md" { next }
        $0 == "TH07PSP/CHANGELOG.md" { next }
        $0 == "TH07PSP/LICENSE" { next }
        $0 == "TH07PSP/ARK5_HIGHMEM_SNIPPET.txt" { next }
        $0 == "TH07PSP/docs/KNOWN_ISSUES.md" { next }
        $0 == "TH07PSP/docs/ARK5_HIGH_MEMORY.md" { next }
        $0 == "TH07PSP/docs/PSP_RELEASE_FONTS.md" { next }
        $0 == "TH07PSP/licenses/NotoSansJP/OFL.txt" { next }
        $0 == "TH07PSP/licenses/NotoSansJP/FONTLOG-TH07PSP.txt" { next }
        $0 == "TH07PSP/licenses/MECC/LICENSE.md" { next }
        { print }
    ')
    if [ -n "$unexpected" ]; then
        echo "[FAIL] unexpected file entered $archive:"
        echo "$unexpected"
        fail=1
    fi

    for mapping in \
        NotoSansJP-Regular.ttf\|psp/assets/NotoSansJP-Regular.ttf \
        msgothic-subset.ttf\|psp/assets/msgothic-subset.ttf \
        README.md\|README.md \
        README_EN.md\|README_EN.md \
        CREDITS.md\|CREDITS.md \
        CHANGELOG.md\|CHANGELOG.md \
        LICENSE\|LICENSE \
        ARK5_HIGHMEM_SNIPPET.txt\|ark/ARK5_HIGHMEM_SNIPPET.txt \
        docs/KNOWN_ISSUES.md\|docs/KNOWN_ISSUES.md \
        docs/ARK5_HIGH_MEMORY.md\|docs/ARK5_HIGH_MEMORY.md \
        docs/PSP_RELEASE_FONTS.md\|docs/PSP_RELEASE_FONTS.md \
        licenses/NotoSansJP/OFL.txt\|licenses/NotoSansJP/OFL.txt \
        licenses/NotoSansJP/FONTLOG-TH07PSP.txt\|licenses/NotoSansJP/FONTLOG-TH07PSP.txt \
        licenses/MECC/LICENSE.md\|psp/third_party/me-custom-core/LICENSE.md; do
        member=${mapping%%|*}
        source=${mapping#*|}
        compare_archive_source "$archive" "$member" "$source"
    done

    candidate="$tmp_root/$archive_name.EBOOT.PBP"
    if extract_archive_member "$archive" EBOOT.PBP "$candidate"; then
        candidate_sha=$(sha256sum "$candidate" | awk '{print $1}')
        if [ "$candidate_sha" != "$unified_sha" ]; then
            echo "[FAIL] $archive unified EBOOT SHA mismatch: $candidate_sha"
            fail=1
        fi
        if ! python3 tools/audit_unified_pbp.py "$candidate" \
            --title "$formal_title" \
            --psp1000-sha256 "$psp1000_sha" \
            --psp2000plus-sha256 "$psp2000plus_sha"; then
            fail=1
        fi
        if ! python3 tools/check_no_original_assets.py "$candidate"; then
            fail=1
        fi
    else
        echo "[FAIL] cannot extract the unified EBOOT from $archive"
        fail=1
    fi

    sidecar="$archive.EBOOT.sha256"
    if [ ! -f "$sidecar" ]; then
        echo "[FAIL] $archive has no EBOOT hash sidecar"
        fail=1
    else
        sidecar_hash=$(awk 'NR == 1 {print $1}' "$sidecar")
        archive_eboot_hash=$(archive_member_hash "$archive" EBOOT.PBP || true)
        if [ -z "$archive_eboot_hash" ] || [ "$sidecar_hash" != "$archive_eboot_hash" ] || \
           [ "$archive_eboot_hash" != "$unified_sha" ]; then
            echo "[FAIL] $archive EBOOT does not match $sidecar"
            fail=1
        fi
    fi

    snippet="$tmp_root/$archive_name.ARK5_HIGHMEM_SNIPPET.txt"
    if extract_archive_member "$archive" ARK5_HIGHMEM_SNIPPET.txt "$snippet"; then
        ark_rule=$(grep -Ec '^homebrew, highmem, on$' "$snippet" || true)
        ark_active=$(grep -Evc '^[[:space:]]*(#|$)' "$snippet" || true)
        if [ "$ark_rule" -ne 1 ] || [ "$ark_active" -ne 1 ]; then
            echo "[FAIL] $archive ARK-5 snippet is not exactly one scoped active highmem rule"
            fail=1
        fi
        if ! grep -Fq 'Do not replace your complete SETTINGS.TXT' "$snippet"; then
            echo "[FAIL] $archive ARK-5 snippet lacks the merge-only warning"
            fail=1
        fi
        if ! grep -Fq 'Use Extra Memory' "$snippet" || ! grep -Fq 'Max' "$snippet"; then
            echo "[FAIL] $archive ARK-5 snippet does not require Use Extra Memory = Max"
            fail=1
        fi
    else
        echo "[FAIL] cannot extract ARK5_HIGHMEM_SNIPPET.txt from $archive"
        fail=1
    fi
done

if [ "$#" -eq 0 ] && [ "$archive_count" -gt 1 ]; then
    echo "[FAIL] dist contains $archive_count formal TH07 archives; the release contract permits one"
    fail=1
fi

if [ -f build/psp_unified_release/EBOOT.PBP ]; then
    if ! python3 tools/audit_unified_pbp.py build/psp_unified_release/EBOOT.PBP \
        --title "$formal_title" \
        --psp1000-sha256 "$psp1000_sha" \
        --psp2000plus-sha256 "$psp2000plus_sha"; then
        fail=1
    fi
    if ! python3 tools/check_no_original_assets.py build/psp_unified_release/EBOOT.PBP; then
        fail=1
    fi
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi

echo "[OK] release audit: unified payloads, exact full/subset OFL Noto fonts, neutral XMB placeholders, no original/user data"
