# Fails the build if the executable defines zlib's entry points itself.
#
# Run after every link on macOS; see the JUCE_INCLUDE_ZLIB_CODE=0 definition in CMakeLists.txt.
# TagLib is built against the SDK's libz there, so libz is always on the link line. A defined
# _inflate in the executable means JUCE's embedded zlib is compiled in beside it, and with it the
# conflict BREAKING_CHANGES.md warns about for JUCE 9.0.1 and later.
#
# Three outcomes, all deliberate: clean symbols pass, a defined _inflate fails, and an inspection
# that could not run - no nm, no executable - fails too. A check that passes when it could not look
# is not a check.
#
# Arguments: -DNM=<path to nm> -DEXECUTABLE=<path to the linked executable>

if(NOT NM OR NOT EXECUTABLE)
    message(FATAL_ERROR "CheckSingleZlib.cmake needs -DNM=<nm> and -DEXECUTABLE=<binary>")
endif()

execute_process(
    COMMAND "${NM}" -g "${EXECUTABLE}"
    RESULT_VARIABLE nmResult
    OUTPUT_VARIABLE symbols
    ERROR_VARIABLE nmError
)

if(NOT nmResult EQUAL 0)
    message(FATAL_ERROR "Could not inspect ${EXECUTABLE} for a second zlib: ${NM} exited ${nmResult}. ${nmError}")
endif()

# nm -g prints one symbol per line as "<address> T _inflate". Anchored at both ends, so that
# _inflateInit_ or _inflateEnd alone would not match; if JUCE's copy is in, _inflate is in.
if(symbols MATCHES "(^|\n)[0-9a-fA-F]+ T _inflate(\r?\n|$)")
    message(FATAL_ERROR
        "${EXECUTABLE} defines its own zlib (inflate) beside libz - JUCE's embedded copy is compiled in. "
        "See JUCE_INCLUDE_ZLIB_CODE in CMakeLists.txt.")
endif()
