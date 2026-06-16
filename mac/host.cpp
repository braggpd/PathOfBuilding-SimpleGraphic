// Path of Building 2 — macOS host launcher (dev / runtime-macos)
// Loads libSimpleGraphic.dylib by linking at build time; same role as
// Path of Building-PoE2.exe + SimpleGraphic.dll on Windows.

extern "C" int RunLuaFileAsWin(int argc, char** argv);

int main(int argc, char** argv)
{
	return RunLuaFileAsWin(argc, argv);
}
