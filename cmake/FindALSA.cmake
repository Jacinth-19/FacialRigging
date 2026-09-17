# Override of CMake's FindALSA used while configuring the vendored PortAudio: points at the
# alsa-lib headers assembled from third_party/alsa-lib (see top-level CMakeLists.txt).
# PortAudio is built with PA_ALSA_DYNAMIC, so no libasound link library is required.
if(FR_ALSA_GEN AND EXISTS ${FR_ALSA_GEN}/alsa/asoundlib.h)
  set(ALSA_FOUND TRUE)
  set(ALSA_INCLUDE_DIRS ${FR_ALSA_GEN})
  set(ALSA_INCLUDE_DIR ${FR_ALSA_GEN})
  set(ALSA_LIBRARIES "")
  set(ALSA_LIBRARY "")
  set(ALSA_VERSION_STRING "1.2.16.1")
else()
  set(ALSA_FOUND FALSE)
endif()
