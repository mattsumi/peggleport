FetchContent_Declare(openmpt
  URL https://lib.openmpt.org/files/libopenmpt/src/libopenmpt-0.8.10+release.makefile.tar.gz
      https://download.openmpt.org/archive/libopenmpt/src/libopenmpt-0.8.10+release.makefile.tar.gz
  URL_HASH SHA256=95073a10d33d0ed4352f41b68edccb9636002d2626f9fd2e00b191fcaabdd876
  PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_LIST_DIR}/openmpt-mo3.cmake
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(openmpt)

set(PP_OPENMPT_CXX
  common/ComponentManager.cpp
  common/Logging.cpp
  common/Profiler.cpp
  common/mptFileType.cpp
  common/mptPathString.cpp
  common/mptRandom.cpp
  common/mptStringBuffer.cpp
  common/mptTime.cpp
  common/serialization_utils.cpp
  common/version.cpp
  soundlib/AudioCriticalSection.cpp
  soundlib/ContainerMMCMP.cpp
  soundlib/ContainerPP20.cpp
  soundlib/ContainerUMX.cpp
  soundlib/ContainerXPK.cpp
  soundlib/Dlsbank.cpp
  soundlib/Fastmix.cpp
  soundlib/ITCompression.cpp
  soundlib/ITTools.cpp
  soundlib/InstrumentExtensions.cpp
  soundlib/InstrumentSynth.cpp
  soundlib/Load_667.cpp
  soundlib/Load_669.cpp
  soundlib/Load_amf.cpp
  soundlib/Load_ams.cpp
  soundlib/Load_c67.cpp
  soundlib/Load_cba.cpp
  soundlib/Load_dbm.cpp
  soundlib/Load_digi.cpp
  soundlib/Load_dmf.cpp
  soundlib/Load_dsm.cpp
  soundlib/Load_dsym.cpp
  soundlib/Load_dtm.cpp
  soundlib/Load_etx.cpp
  soundlib/Load_far.cpp
  soundlib/Load_fc.cpp
  soundlib/Load_fmt.cpp
  soundlib/Load_ftm.cpp
  soundlib/Load_gdm.cpp
  soundlib/Load_gmc.cpp
  soundlib/Load_gt2.cpp
  soundlib/Load_ice.cpp
  soundlib/Load_imf.cpp
  soundlib/Load_ims.cpp
  soundlib/Load_it.cpp
  soundlib/Load_itp.cpp
  soundlib/Load_kris.cpp
  soundlib/Load_mdl.cpp
  soundlib/Load_med.cpp
  soundlib/Load_mid.cpp
  soundlib/Load_mo3.cpp
  soundlib/Load_mod.cpp
  soundlib/Load_mt2.cpp
  soundlib/Load_mtm.cpp
  soundlib/Load_mus_km.cpp
  soundlib/Load_okt.cpp
  soundlib/Load_plm.cpp
  soundlib/Load_psm.cpp
  soundlib/Load_pt36.cpp
  soundlib/Load_ptm.cpp
  soundlib/Load_puma.cpp
  soundlib/Load_rtm.cpp
  soundlib/Load_s3m.cpp
  soundlib/Load_sfx.cpp
  soundlib/Load_stk.cpp
  soundlib/Load_stm.cpp
  soundlib/Load_stp.cpp
  soundlib/Load_symmod.cpp
  soundlib/Load_tcb.cpp
  soundlib/Load_uax.cpp
  soundlib/Load_ult.cpp
  soundlib/Load_unic.cpp
  soundlib/Load_wav.cpp
  soundlib/Load_xm.cpp
  soundlib/Load_xmf.cpp
  soundlib/MIDIEvents.cpp
  soundlib/MIDIMacroParser.cpp
  soundlib/MIDIMacros.cpp
  soundlib/MODTools.cpp
  soundlib/MPEGFrame.cpp
  soundlib/Message.cpp
  soundlib/MixFuncTable.cpp
  soundlib/MixerLoops.cpp
  soundlib/MixerSettings.cpp
  soundlib/ModChannel.cpp
  soundlib/ModInstrument.cpp
  soundlib/ModSample.cpp
  soundlib/ModSequence.cpp
  soundlib/OPL.cpp
  soundlib/OggStream.cpp
  soundlib/Paula.cpp
  soundlib/PlayState.cpp
  soundlib/PlaybackTest.cpp
  soundlib/RowVisitor.cpp
  soundlib/S3MTools.cpp
  soundlib/SampleFormatBRR.cpp
  soundlib/SampleFormatFLAC.cpp
  soundlib/SampleFormatMP3.cpp
  soundlib/SampleFormatMediaFoundation.cpp
  soundlib/SampleFormatOpus.cpp
  soundlib/SampleFormatSFZ.cpp
  soundlib/SampleFormatVorbis.cpp
  soundlib/SampleFormats.cpp
  soundlib/SampleIO.cpp
  soundlib/Snd_flt.cpp
  soundlib/Snd_fx.cpp
  soundlib/Sndfile.cpp
  soundlib/Sndmix.cpp
  soundlib/SoundFilePlayConfig.cpp
  soundlib/Tables.cpp
  soundlib/Tagging.cpp
  soundlib/TinyFFT.cpp
  soundlib/UMXTools.cpp
  soundlib/UpgradeModule.cpp
  soundlib/WAVTools.cpp
  soundlib/WindowedFIR.cpp
  soundlib/XMTools.cpp
  soundlib/load_j2b.cpp
  soundlib/mod_specifications.cpp
  soundlib/modcommand.cpp
  soundlib/modsmp_ctrl.cpp
  soundlib/pattern.cpp
  soundlib/patternContainer.cpp
  soundlib/tuning.cpp
  soundlib/tuningCollection.cpp
  soundlib/plugins/DigiBoosterEcho.cpp
  soundlib/plugins/LFOPlugin.cpp
  soundlib/plugins/PlugInterface.cpp
  soundlib/plugins/PluginManager.cpp
  soundlib/plugins/SymMODEcho.cpp
  soundlib/plugins/dmo/Chorus.cpp
  soundlib/plugins/dmo/Compressor.cpp
  soundlib/plugins/dmo/DMOPlugin.cpp
  soundlib/plugins/dmo/DMOUtils.cpp
  soundlib/plugins/dmo/Distortion.cpp
  soundlib/plugins/dmo/Echo.cpp
  soundlib/plugins/dmo/Flanger.cpp
  soundlib/plugins/dmo/Gargle.cpp
  soundlib/plugins/dmo/I3DL2Reverb.cpp
  soundlib/plugins/dmo/ParamEq.cpp
  soundlib/plugins/dmo/WavesReverb.cpp
  sounddsp/AGC.cpp
  sounddsp/DSP.cpp
  sounddsp/EQ.cpp
  sounddsp/Reverb.cpp
  libopenmpt/libopenmpt_c.cpp
  libopenmpt/libopenmpt_cxx.cpp
  libopenmpt/libopenmpt_ext_impl.cpp
  libopenmpt/libopenmpt_impl.cpp)

set(PP_OPENMPT_C
  include/miniz/miniz.c
  include/minimp3/minimp3.c
  include/stb_vorbis/stb_vorbis.c)

set(PP_OPENMPT_SOURCES "")
foreach(_f ${PP_OPENMPT_CXX} ${PP_OPENMPT_C})
  list(APPEND PP_OPENMPT_SOURCES "${openmpt_SOURCE_DIR}/${_f}")
endforeach()

add_library(pp_openmpt STATIC ${PP_OPENMPT_SOURCES})
target_include_directories(pp_openmpt PRIVATE ${openmpt_SOURCE_DIR} ${openmpt_SOURCE_DIR}/src ${openmpt_SOURCE_DIR}/common ${openmpt_SOURCE_DIR}/include)
target_include_directories(pp_openmpt SYSTEM INTERFACE ${openmpt_SOURCE_DIR})
target_compile_definitions(pp_openmpt PRIVATE LIBOPENMPT_BUILD MPT_WITH_MINIZ MPT_WITH_MINIMP3 MPT_WITH_STBVORBIS STB_VORBIS_NO_PULLDATA_API STB_VORBIS_NO_STDIO)
set_target_properties(pp_openmpt PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_definitions(pp_openmpt PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  target_compile_options(pp_openmpt PRIVATE /W0 /utf-8 /Zc:__cplusplus)
else()
  target_compile_options(pp_openmpt PRIVATE -w)
endif()
