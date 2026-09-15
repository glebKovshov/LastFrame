# Contributing

1. Install Visual Studio 2022 Desktop development with C++, CMake 3.28+, Qt 6.8+
   and a shared FFmpeg build.
2. Configure with `cmake --preset windows-debug` or `cmake --preset core-only`.
3. Build and run the relevant CTest preset before opening a pull request.
4. Keep capture, audio and media backends behind platform-neutral interfaces;
   diagnostics must never contain pixels, audio samples or microphone data.

Changes that affect settings must preserve `schemaVersion`, unknown JSON fields,
and the atomic temporary-file rename path.
