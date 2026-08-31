#!/usr/bin/env bash
# Restricted public-demo menu for imgview. This is the ONLY thing the
# public ttyd session runs — never swap this for an open shell.
set -euo pipefail

BIN=/app/build/pngdecoder
DEMO_DIR=/app/demo

# Hard resource ceiling per session — a hostile or huge input should
# never be able to hang or exhaust the shared container.
ulimit -v 524288   # 512MB virtual memory ceiling
TIMEOUT_CMD="timeout 10s"

show_menu() {
  clear
  echo "imgview — zero-dependency PNG decoder & terminal renderer"
  echo "=========================================================="
  echo "Hand-rolled DEFLATE (RFC 1951), CRC-32, and terminal rendering."
  echo "No zlib. No libpng. No image libraries of any kind."
  echo
}

show_menu
PS3=$'\nPick a demo (number): '
select f in "$DEMO_DIR"/*.png "Show --info output" "Exit"; do
  case "${f:-}" in
    "Show --info output")
      files=("$DEMO_DIR"/*.png)
      echo
      for i in "${!files[@]}"; do
        printf "  %d) %s\n" "$((i+1))" "$(basename "${files[$i]}")"
      done
      read -rp "File number: " n
      idx=$((n-1))
      if [ "$idx" -ge 0 ] && [ "$idx" -lt "${#files[@]}" ]; then
        $TIMEOUT_CMD "$BIN" "${files[$idx]}" --info || true
      else
        echo "Invalid selection."
      fi
      ;;
    "Exit")
      echo "Thanks for checking out imgview!"
      exit 0
      ;;
    *)
      if [ -n "${f:-}" ]; then
        echo
        $TIMEOUT_CMD "$BIN" "$f" --width 90 || echo "(decode failed — see exit code)"
      else
        echo "Invalid selection."
      fi
      ;;
  esac
  echo
  read -rp "Press enter to see the menu again..." _
  show_menu
done
