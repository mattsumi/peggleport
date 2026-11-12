function(pp_openmpt_patch file anchor addition)
  file(READ "${file}" text)
  string(FIND "${text}" "${addition}" patched)
  string(FIND "${text}" "${anchor}" found)
  if(patched EQUAL -1 AND NOT found EQUAL -1)
    string(REPLACE "${anchor}" "${addition}" text "${text}")
    file(WRITE "${file}" "${text}")
  endif()
endfunction()

set(anchor "&channels, &output, &decodedSamples);")
pp_openmpt_patch("soundlib/Load_mo3.cpp" "${anchor}" "${anchor} if(consumed <= 0) break;")

set(anchor "for (i=0; i < p->comment_list_length; ++i) {")
pp_openmpt_patch("include/stb_vorbis/stb_vorbis.c" "${anchor}" "for (i=0; p->comment_list && i < p->comment_list_length; ++i) {")

set(anchor "f->comment_list_length = get32_packet(f);")
pp_openmpt_patch("include/stb_vorbis/stb_vorbis.c" "${anchor}" "${anchor} if (f->comment_list_length < 0 || f->comment_list_length > 65535) return error(f, VORBIS_invalid_setup);")
