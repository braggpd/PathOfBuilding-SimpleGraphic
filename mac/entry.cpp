// DyLua: SimpleGraphic
// (c) David Gowor, 2014
//
// Entry Point
// Platform: macOS
//

#include "system/win/sys_local.h"

#include <cstring>

#if __GNUC__ >= 4
	#define SIMPLEGRAPHIC_DLL_PUBLIC __attribute__((visibility("default")))
#else
	#define SIMPLEGRAPHIC_DLL_PUBLIC
#endif

static int RunSimpleGraphic(int argc, char** argv)
{
#ifdef _MEMTRAK_H
	std::strncpy(_memTrak_reportName, "SimpleGraphic/memtrak.log", sizeof(_memTrak_reportName) - 1);
	_memTrak_reportName[sizeof(_memTrak_reportName) - 1] = '\0';
#endif

	sys_main_c* sys = new sys_main_c;

	while (sys->Run(argc, argv));

	delete sys;

	return 0;
}

extern "C" SIMPLEGRAPHIC_DLL_PUBLIC int RunLuaFileAsWin(int argc, char** argv)
{
	return RunSimpleGraphic(argc, argv);
}

int main(int argc, char** argv)
{
	return RunSimpleGraphic(argc, argv);
}
