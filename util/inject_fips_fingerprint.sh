#!/bin/bash
#
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Calculate hash of fips module and inject it into the .elf file.

SCRIPT="$(basename "$0")"

main() {
  local objcopy="${1}"
  local objdump="${2}"
  local rw_elf_in="${3}"
  local check_result="${4}"

  local base="${rw_elf_in%.elf}"
  local rw_elf_out="${rw_elf_in}.fips"
  local checksum_section=".text.fips_checksum"
  local fips_body="${base}.fips.body"
  local fips_checksum_dump="${base}.fips.checksum_dump"
  local fips_error="${base}.fips.error"
  local size
  local sections
  local fips_start
  local fips_end
  local fips_offset
  local file_offset
  local base_addr

  if [ ! -f "${rw_elf_in}" ] ; then
    echo "  ${rw_elf_in} doesn't exist"
    return 1
  fi

  echo "${rw_elf_in} ${rw_elf_out}"
  sections=$( objdump -t "${rw_elf_in}" )

  # Never mind the shellcheck suggestion to remove the quotes,
  # literal match is required in this case.
  if [[ "${sections}" =~ "${checksum_section}" ]] ; then
    echo "  get fips checksum"
  else
    echo "  no fips checksum"
    return 1
  fi
  vals=( $(${objdump}  -x -j .text "${rw_elf_in}" | awk '
  {
          if ($1 == "LOAD" && $2 == "off") {
                  load_segments[strtonum($5)] = strtonum($3)
          }
          if ($2 == ".text") {
                  text_vma = strtonum("0x" $4)
          }
          if ($5 == "__fips_module_start") { fips_start = strtonum("0x" $1) }
          if ($5 == "__fips_module_end") { fips_end = strtonum("0x" $1) }
  }
  END {
          file_offs = load_segments[text_vma]
          base_addr = text_vma
          printf "0x%x 0x%x 0x%x 0x%x\n", file_offs, base_addr,
                 fips_start, fips_end
  }') )

  file_offset=${vals[0]}
  base_addr=${vals[1]}
  fips_start=${vals[2]}
  fips_end=${vals[3]}
  size=$((fips_end - fips_start))
  fips_offset=$((file_offset + fips_start - base_addr))

  if ! dd if="${rw_elf_in}" skip="${fips_offset}" count="${size}" bs=1 \
     >"${fips_body}" 2>"${fips_error}"; then
    printf "%s: error:\n$(cat "${fips_error}")" "${SCRIPT}" >&2
    exit 1
  fi

  # TODO (b/313717258): consider reverting this once xxd is added back
  ./util/fipsdigest.py -i "${fips_body}" -o "${fips_checksum_dump}"

  cp "${rw_elf_in}" "${rw_elf_out}"

  # don't update digest if run with FIPS_BREAK=1
  [ -v FIPS_BREAK ] && return 0
  if [[ ${check_result} == 1 ]]; then
    local snapshot="board/${BOARD}/fips/$(basename "${fips_checksum_dump}")"
    if ! cmp "${fips_checksum_dump}" "${snapshot}"; then
      echo "Generated fips hash ${fips_checksum_dump} mismatch!" >&2
      exit 1
    fi
  fi
  ${objcopy} --update-section "${checksum_section}"="${fips_checksum_dump}" \
		"${rw_elf_out}"
}

main "$@"
