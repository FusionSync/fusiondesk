#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
CONFIGURATION="${CONFIGURATION:-Release}"
OUTPUT_DIR="${OUTPUT_DIR:-artifacts/release}"
VERSION="${VERSION:-}"
PLATFORM="${PLATFORM:-linux-x86_64}"
SKIP_SMOKE="${SKIP_SMOKE:-0}"

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

if [[ "$BUILD_DIR" != /* ]]; then
  build_root="$repo_root/$BUILD_DIR"
else
  build_root="$BUILD_DIR"
fi

if [[ "$OUTPUT_DIR" != /* ]]; then
  output_root="$repo_root/$OUTPUT_DIR"
else
  output_root="$OUTPUT_DIR"
fi

find_exe() {
  local name="$1"
  local candidates=(
    "$build_root/$name"
    "$build_root/$CONFIGURATION/$name"
  )
  for candidate in "${candidates[@]}"; do
    if [[ -x "$candidate" || -f "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  echo "Required executable was not found: $name under $build_root" >&2
  return 1
}

git_value() {
  git "$@" 2>/dev/null | head -n 1 || true
}

if [[ -z "$VERSION" ]]; then
  VERSION="$(git_value describe --tags --always --dirty)"
fi
if [[ -z "$VERSION" ]]; then
  VERSION="dev"
fi

safe_version="$(printf '%s' "$VERSION" | sed 's/[^A-Za-z0-9_.-]/-/g')"
package_name="fusiondesk-clip-$PLATFORM-$safe_version"
stage_root="$output_root/$package_name"
archive_path="$output_root/$package_name.zip"

mkdir -p "$output_root"
output_real="$(cd "$output_root" && pwd)"
case "$stage_root" in
  "$output_real"/*) ;;
  *) echo "Refusing to stage outside output directory: $stage_root" >&2; exit 1 ;;
esac

rm -rf "$stage_root" "$archive_path"
mkdir -p "$stage_root/bin" "$stage_root/lib" "$stage_root/samples"

clip_exe="$(find_exe fusiondesk_clip)"
profile_plan_exe="$(find_exe fusiondesk_pc_profile_plan)"

cp "$clip_exe" "$stage_root/bin/"
cp "$profile_plan_exe" "$stage_root/bin/"
chmod +x "$stage_root/bin/"*

copy_qt_dependencies() {
  local exe="$1"
  if [[ -z "${QT_ROOT_DIR:-}" ]]; then
    return 0
  fi

  while IFS= read -r line; do
    local dep
    dep="$(printf '%s\n' "$line" | awk '/=>/ {print $3} /^[[:space:]]*\// {print $1}')"
    if [[ -n "$dep" && "$dep" == "$QT_ROOT_DIR"* && -f "$dep" ]]; then
      cp -L "$dep" "$stage_root/lib/"
    fi
  done < <(ldd "$exe" || true)
}

copy_qt_dependencies "$stage_root/bin/fusiondesk_clip"
copy_qt_dependencies "$stage_root/bin/fusiondesk_pc_profile_plan"

cat > "$stage_root/fusiondesk_clip_agent.sh" <<'EOF'
#!/usr/bin/env bash
APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$APP_DIR/lib:${LD_LIBRARY_PATH:-}"
exec "$APP_DIR/bin/fusiondesk_clip" --clip-role agent "$@"
EOF

cat > "$stage_root/fusiondesk_clip_client.sh" <<'EOF'
#!/usr/bin/env bash
APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$APP_DIR/lib:${LD_LIBRARY_PATH:-}"
exec "$APP_DIR/bin/fusiondesk_clip" --clip-role client "$@"
EOF

chmod +x "$stage_root/fusiondesk_clip_agent.sh" "$stage_root/fusiondesk_clip_client.sh"

commit="$(git_value rev-parse --short HEAD)"
cat > "$stage_root/manifest.txt" <<EOF
name=fusiondesk-clip
platform=$PLATFORM
version=$VERSION
commit=$commit
configuration=$CONFIGURATION
createdUtc=$(date -u +"%Y-%m-%d %H:%M:%S UTC")
EOF

cat > "$stage_root/README.txt" <<'EOF'
FusionDesk clipboard CLI release package

Contents:
- bin/fusiondesk_clip
- bin/fusiondesk_pc_profile_plan
- fusiondesk_clip_agent.sh
- fusiondesk_clip_client.sh

Smoke:
  ./fusiondesk_clip_agent.sh --smoke
  ./fusiondesk_clip_client.sh --smoke

Generate paired local TCP profiles:
  bin/fusiondesk_pc_profile_plan --client-profile samples/client.json --agent-profile samples/agent.json --channel control=127.0.0.1:47101 --channel small_data=127.0.0.1:47102 --channel main_screen=127.0.0.1:47103 --channel large_data=127.0.0.1:47104

Run clipboard endpoints with generated profiles:
  ./fusiondesk_clip_agent.sh --profile samples/agent.json --start-clipboard --pump-clipboard --print-clipboard-diagnostics
  ./fusiondesk_clip_client.sh --profile samples/client.json --start-clipboard --pump-clipboard --print-clipboard-diagnostics

This package is an unsigned engineering release.
EOF

cat > "$stage_root/samples/README.txt" <<'EOF'
Use fusiondesk_pc_profile_plan to generate runtime profiles for the local
clipboard endpoints you want to test.
EOF

if [[ "$SKIP_SMOKE" != "1" ]]; then
  "$stage_root/fusiondesk_clip_agent.sh" --smoke
  "$stage_root/fusiondesk_clip_client.sh" --smoke
fi

(cd "$output_root" && cmake -E tar cf "$archive_path" --format=zip "$package_name")
echo "PACKAGE_ARCHIVE=$archive_path"
