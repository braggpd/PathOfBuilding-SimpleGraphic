// Path of Building 2 — macOS host launcher
// Loads libSimpleGraphic.dylib by linking at build time; same role as
// Path of Building-PoE2.exe + SimpleGraphic.dll on Windows.

#include <cstdio>
#include <limits.h>
#include <mach-o/dyld.h>
#include <string>

extern "C" int RunLuaFileAsWin(int argc, char** argv);

// Walk up from the executable looking for src/Launch.lua.
// Works for both .app bundle layout (exe in Contents/MacOS/) and
// flat runtime-macos/ dev layout (exe alongside src/).
static std::string findDefaultScript()
{
	char raw[PATH_MAX];
	uint32_t size = sizeof(raw);
	if (_NSGetExecutablePath(raw, &size) != 0) return {};
	char resolved[PATH_MAX];
	if (!realpath(raw, resolved)) return {};

	std::string dir(resolved);
	for (int i = 0; i < 6; ++i) {
		auto slash = dir.rfind('/');
		if (slash == std::string::npos) break;
		dir = dir.substr(0, slash);
		std::string candidate = dir + "/src/Launch.lua";
		if (FILE* f = fopen(candidate.c_str(), "r")) {
			fclose(f);
			return candidate;
		}
	}
	return {};
}

int main(int argc, char** argv)
{
	if (argc < 2) {
		std::string script = findDefaultScript();
		if (!script.empty()) {
			char* newArgv[] = { argv[0], script.data(), nullptr };
			return RunLuaFileAsWin(2, newArgv);
		}
	}
	return RunLuaFileAsWin(argc, argv);
}
