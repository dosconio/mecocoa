#!/usr/bin/env bash
set -euo pipefail

if [ $# -lt 1 ]; then
  echo "Usage: $0 input.elf [output.elf]" >&2
  exit 1
fi

in="$1"
out="${2:-${in%.elf}.mapped_0x100000.elf}"

readelf_out=$(readelf -l -W "$in")

first_load_line=$(printf "%s\n" "$readelf_out" | awk '/LOAD/ {print; exit}')
if [ -z "$first_load_line" ]; then
  echo "No PT_LOAD found in $in" >&2
  exit 2
fi

# Get p_offset (col2) and p_vaddr (col3)
off_hex=$(printf "%s\n" "$first_load_line" | awk '{print $2}')
v_hex=$(printf "%s\n" "$first_load_line" | awk '{print $3}')

if [ -z "$off_hex" ] || [ -z "$v_hex" ]; then
  echo "failed to parse readelf output line: $first_load_line" >&2
  exit 3
fi

off=$((off_hex))
vaddr=$((v_hex))
old_diff=$((vaddr - off))
desired=0x100000
delta=$((desired - old_diff))

# Print entry
entry_before_hex=$(readelf -h "$in" | awk '/Entry point address/ {print $4}')
entry_before_val=$((entry_before_hex))
entry_after_val=$((entry_before_val + delta))

printf "Input: %s\nFirst LOAD: p_offset=%s p_vaddr=%s\nold_diff=0x%x\nWill adjust VMA by delta=%d (0x%x)\n\n" \
  "$in" "$off_hex" "$v_hex" "$old_diff" "$delta" "$delta"

printf "Entry before: %s\nEntry after (expected): 0x%x\n\n" "$entry_before_hex" "$entry_after_val"

# Apply adjustment
objcopy --adjust-vma=$delta "$in" "$out"

# Verification
echo "Verification for $out (p_vaddr - p_offset for each LOAD):"
readelf -l -W "$out" | awk '/LOAD/ {print $2, $3}' | while read -r off_h v_h; do
  off_n=$((off_h))
  v_n=$((v_h))
  printf "  p_offset=%s  p_vaddr=%s  diff=0x%x\n" "$off_h" "$v_h" $((v_n - off_n))
done

echo
echo "Output written to: $out"
