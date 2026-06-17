// Path of Building 2 — macOS host launcher (dev / runtime-macos)
// Links libSimpleGraphic.dylib at build time; same role as
// Path of Building-PoE2.exe + SimpleGraphic.dll on Windows.
//
// The engine's RunLuaFileAsWin() follows the Windows host convention where the
// Lua entry script is argv[0]. When launched by the OS, argv[0] is the
// executable path and the script (if any) is argv[1], so we shift past argv[0]
// before handing off to the engine. This keeps the engine code identical to
// upstream.

extern "C" int RunLuaFileAsWin(int argc, char** argv);

int main(int argc, char** argv)
{
	if (argc > 1) {
		return RunLuaFileAsWin(argc - 1, argv + 1);
	}
	return RunLuaFileAsWin(argc, argv);
}
