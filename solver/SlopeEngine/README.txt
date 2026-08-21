SlopeEngine.exe is the delivered Windows x64 command-line engine for the slope
standard-method check. The binary is intentionally not tracked because this
repository ignores prebuilt *.exe files.

Place the delivered executable at:

    solver/SlopeEngine/SlopeEngine.exe

or configure CMake with:

    -DCAE_SLOPE_ENGINE_EXECUTABLE=<absolute path to SlopeEngine.exe>

The build deploys it to runtime/solver/SlopeEngine/SlopeEngine.exe.
