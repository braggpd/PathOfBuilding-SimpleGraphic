// DyLua: SimpleGraphic
// (c) David Gowor, 2014
//
// Module: UI Main
//

#include "ui_local.h"
#if __APPLE__ && __MACH__
#include <dlfcn.h>

// Light C function replacements for LuaJIT built-ins whose interpreter fast-paths
// are broken in GC64 mode on arm64. Protected calls use lua_resume on a helper
// coroutine — lua_pcall from inside a LIGHTFUNC corrupts interpreter return state. (#8)

// Entry: L = [func, arg1, ..., argN]. Exit: [true, ...] or [false, err].
static int mac_lightfunc_pcall(lua_State* L, int nargs) {
    lua_State* co = lua_newthread(L);
    lua_xmove(L, co, nargs + 1);
    const int status = lua_resume(co, nullptr, nargs);
    if (status == 0) {
        const int nres = lua_gettop(co);
        lua_pushboolean(L, 1);
        lua_xmove(co, L, nres);
        lua_pop(L, 1); // drop thread
        return nres + 1;
    }
    if (status == LUA_YIELD) {
        lua_settop(co, 0);
        lua_pushliteral(co, "cannot resume non-synchronous function in pcall");
    }
    lua_pushboolean(L, 0);
    if (lua_gettop(co) > 0) {
        lua_xmove(co, L, 1);
    } else {
        lua_pushliteral(L, "(error object is not a string)");
    }
    lua_pop(L, 1); // drop thread
    return 2;
}

static int l_mac_pcall(lua_State* L) {
    const int n = lua_gettop(L);
    if (n < 1) {
        luaL_error(L, "bad argument #1 to 'pcall' (value expected)");
    }
    return mac_lightfunc_pcall(L, n - 1);
}

static int l_mac_xpcall(lua_State* L) {
    const int n = lua_gettop(L);
    if (n < 2) {
        luaL_error(L, "bad argument #2 to 'xpcall' (value expected)");
    }
    const int nargs = n - 2;

    lua_State* co = lua_newthread(L);
    lua_pushvalue(L, 1);
    for (int i = 3; i <= n; i++) {
        lua_pushvalue(L, i);
    }
    lua_xmove(L, co, nargs + 1);

    const int status = lua_resume(co, nullptr, nargs);
    if (status == 0) {
        const int nres = lua_gettop(co);
        lua_pushboolean(L, 1);
        lua_xmove(co, L, nres);
        lua_settop(L, nres + 1);
        return nres + 1;
    }

    const char* err = "(error object is not a string)";
    if (lua_gettop(co) > 0) {
        if (const char* s = lua_tostring(co, -1)) {
            err = s;
        }
    }
    lua_pushvalue(L, 2);
    lua_pushstring(L, err);
    if (lua_pcall(L, 1, 1, 0) == LUA_OK) {
        if (const char* s = lua_tostring(L, -1)) {
            err = s;
        }
    }
    lua_settop(L, 0);
    lua_pushboolean(L, 0);
    lua_pushstring(L, err);
    return 2;
}

// Call func+args on a helper coroutine (lua_resume). Replaces lua_call from LIGHTFUNCs. (#8)
static void mac_resume_call(lua_State* L, int nargs, int nresults) {
    const int func = lua_gettop(L) - nargs;
    lua_State* co = lua_newthread(L);
    lua_pushvalue(L, func);
    for (int i = 1; i <= nargs; i++) {
        lua_pushvalue(L, func + i);
    }
    lua_xmove(L, co, nargs + 1);
    lua_pop(L, 1); // drop thread handle
    lua_settop(L, func - 1); // drop func and args from parent stack

    const int status = lua_resume(co, nullptr, nargs);
    if (status != 0) {
        if (lua_gettop(co) > 0) {
            lua_xmove(co, L, 1);
        } else {
            lua_pushliteral(L, "error in require loader");
        }
        lua_error(L);
    }
    const int got = lua_gettop(co);
    const int want = (nresults == LUA_MULTRET) ? got : nresults;
    lua_xmove(co, L, want);
    for (int i = got; i < want; i++) {
        lua_pushnil(L);
    }
}

// Light C function replacement for LuaJIT built-in require().
// LuaJIT 2.1 arm64 interpreter crashes when Lua bytecode calls any GC C closure
// via GGET+CALL (e.g. require, which has upvalues → allocated as CClosure).
// Replacing it with a LIGHTFUNC (no GC allocation) and calling all loaders via
// the C API avoids the broken interpreter path entirely. (#8)
static int l_mac_require(lua_State* L) {
    const char* modname = luaL_checkstring(L, 1);
    lua_settop(L, 1);  // [1]=modname

    lua_getglobal(L, "package");        // [1,2]  2=package
    if (!lua_istable(L, -1))
        luaL_error(L, "l_mac_require: 'package' is %s (expected table)", luaL_typename(L, -1));
    lua_getfield(L, -1, "loaded");      // [1,2,3]  3=loaded
    if (!lua_istable(L, -1))
        luaL_error(L, "l_mac_require: 'package.loaded' is %s (expected table)", luaL_typename(L, -1));
    lua_getfield(L, 2, "preload");     // [1,2,3,4]  4=preload

    // 1. Return immediately if already loaded.
    lua_getfield(L, 3, modname);
    if (!lua_isnil(L, -1)) return 1;
    lua_pop(L, 1);

    // 2. Call package.preload[modname] if present.
    lua_getfield(L, 4, modname);
    if (lua_isfunction(L, -1)) {
        lua_pushvalue(L, 1);            // pass modname as arg
        mac_resume_call(L, 1, 1);       // loader(modname) → result
        if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushboolean(L, 1); }
        lua_pushvalue(L, -1);
        lua_setfield(L, 3, modname);   // package.loaded[modname] = result
        return 1;
    }
    lua_pop(L, 1);

    // 3. Search package.path for .lua files.
    lua_getfield(L, 2, "path");
    const char* pathcstr = lua_tostring(L, -1);
    std::string path(pathcstr ? pathcstr : "");
    lua_pop(L, 1);

    std::string modpath(modname);
    for (char& c : modpath) if (c == '.') c = '/';

    std::string tried;
    size_t pos = 0;
    while (pos <= path.size()) {
        size_t semi = path.find(';', pos);
        if (semi == std::string::npos) semi = path.size();
        std::string tmpl = path.substr(pos, semi - pos);
        pos = semi + 1;
        if (tmpl.empty()) continue;
        size_t q = tmpl.find('?');
        if (q != std::string::npos) tmpl.replace(q, 1, modpath);
        if (luaL_loadfile(L, tmpl.c_str()) == LUA_OK) {
            // PoB/runtime modules ignore the require() module name; passing it triggers
            // arm64 LuaJIT faults when invoking the chunk from a LIGHTFUNC. (#8)
            mac_resume_call(L, 0, 1);
            if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushboolean(L, 1); }
            lua_pushvalue(L, -1);
            lua_setfield(L, 3, modname);
            return 1;
        }
        tried += "\n\tno file '"; tried += tmpl; tried += "'";
        lua_pop(L, 1);  // pop load error
    }

    luaL_error(L, "module '%s' not found:%s", modname, tried.c_str());
    return 0;
}
#endif

// ======
// Locals
// ======

static struct {
	int key;
	const char* str;
} ui_keyNameMap[] = {
	KEY_LMOUSE, "LEFTBUTTON",
	KEY_MMOUSE, "MIDDLEBUTTON", 
	KEY_RMOUSE, "RIGHTBUTTON", 
	KEY_MOUSE4, "MOUSE4",
	KEY_MOUSE5, "MOUSE5",
	KEY_MWHEELUP, "WHEELUP", 
	KEY_MWHEELDOWN, "WHEELDOWN", 
	KEY_BACK, "BACK", 
	KEY_TAB, "TAB", 
	KEY_RETURN, "RETURN", 
	KEY_ESCAPE, "ESCAPE", 
	KEY_SHIFT, "SHIFT",
	KEY_CTRL, "CTRL",
	KEY_ALT, "ALT",
	KEY_PAUSE, "PAUSE",
	KEY_PGUP, "PAGEUP", 
	KEY_PGDN, "PAGEDOWN", 
	KEY_END, "END", 
	KEY_HOME, "HOME",
	KEY_PRINTSCRN, "PRINTSCREEN",
	KEY_INSERT, "INSERT", 
	KEY_DELETE, "DELETE", 
	KEY_UP, "UP", 
	KEY_DOWN, "DOWN", 
	KEY_LEFT, "LEFT", 
	KEY_RIGHT, "RIGHT",
	KEY_F1, "F1",
	KEY_F2, "F2",
	KEY_F3, "F3",
	KEY_F4, "F4",
	KEY_F5, "F5",
	KEY_F6, "F6",
	KEY_F7, "F7",
	KEY_F8, "F8",
	KEY_F9, "F9",
	KEY_F10, "F10",
	KEY_F11, "F11",
	KEY_F12, "F12",
	KEY_F13, "F13",
	KEY_F14, "F14",
	KEY_F15, "F15",
	KEY_NUMLOCK, "NUMLOCK",
	KEY_SCROLL, "SCROLLLOCK",
	0, 0
};

// ==================
// ui_IMain Interface
// ==================

ui_IMain* ui_IMain::GetHandle(sys_IMain* sysHnd, core_IMain* coreHnd)
{
	return new ui_main_c(sysHnd, coreHnd);
}

void ui_IMain::FreeHandle(ui_IMain* hnd)
{
	delete (ui_main_c*)hnd;
}

ui_main_c::ui_main_c(sys_IMain* sysHnd, core_IMain* coreHnd)
	: sys(sysHnd), core(coreHnd), framesSinceWindowHidden(0)
{
	renderer = NULL;
}

// =======================
// Lua Interface Utilities
// =======================

void ui_main_c::LAssert(lua_State* L, int cond, const char* fmt, ...)
{
	if ( !cond ) {
		va_list va;
		va_start(va, fmt);
		lua_pushvfstring(L, fmt, va);
		va_end(va);
		lua_error(L);
	}
}

void ui_main_c::LExpect(lua_State* L, int cond, const char* fmt, ...)
{
	if (!cond) {
		va_list va;
		va_start(va, fmt);
		lua_pushvfstring(L, fmt, va);
		va_end(va);
		throw ui_expectationFailed_s{};
	}
}

int ui_main_c::IsUserData(lua_State* L, int index, const char* metaName)
{
	if (lua_type(L, index) != LUA_TUSERDATA || lua_getmetatable(L, index) == 0) {
		return 0;
	}
	lua_getfield(L, LUA_REGISTRYINDEX, metaName);
	int ret = lua_rawequal(L, -2, -1);
	lua_pop(L, 2);
	return ret;
}

int ui_main_c::PushCallback(const char* name)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "uicallbacks");
	lua_getfield(L, -1, name); // Index callbacks table
	// Stack: -2 = callbacks table, -1 = function or nil
	if (lua_isfunction(L, -1)) {
		lua_remove(L, -2); // Remove callbacks table
		return 0;
	} else {
		lua_pop(L, 1); // Pop nil value
		lua_getfield(L, -1, "MainObject"); // Index callbacks table
		lua_remove(L, -2); // Remove callbacks table
		// Stack: -1 = main object or nil
		if (lua_istable(L, -1)) {
			lua_getfield(L, -1, name); // Index main object
			// Stack: -2 = main object, -1 = function or nil
			if (lua_isfunction(L, -1)) {
				lua_insert(L, -2); // Insert function before main object
				return 1;
			} else {
				lua_pop(L, 2); // Pop main object, nil value
			}
		} else {
			lua_pop(L, 1); // Pop nil value
		}
	}
	return -1;
}

#if __APPLE__ && __MACH__
void ui_main_c::CallCallbackOnThread(int extraArgs)
{
	const int funcIdx = lua_gettop(L) - extraArgs;
	lua_State* co = lua_newthread(L);
	lua_pushvalue(L, 1);
	lua_xmove(L, co, 1);
	for (int i = extraArgs; i >= 0; --i) {
		lua_pushvalue(L, funcIdx + i);
	}
	lua_xmove(L, co, extraArgs + 1);
	lua_pop(L, 1);
	const int err = lua_resume(co, nullptr, extraArgs);
	if (err != 0 && !didExit) {
		const char* msg = lua_tostring(co, -1);
		DoError("Runtime error in", msg ? msg : "unknown");
	}
	lua_settop(L, 1);
}
#endif

void ui_main_c::PCall(int narg, int nret)
{
	sys->SetWorkDir(scriptWorkDir);
	inLua = true;
	hasActiveCoroutine = false;
	int err = lua_pcall(L, narg, nret, 1);
	if (err == 0) {
		lua_getglobal(L, "coroutine");
		lua_getfield(L, -1, "_list");
		// PoB defines coroutine._list in Modules/Common.lua (after Main loads). Before that, skip.
		if (lua_isfunction(L, -1)) {
			lua_remove(L, -2);  // drop coroutine table; stack: errfunc, _list()
			if (lua_pcall(L, 0, 1, 0) == LUA_OK && lua_istable(L, -1)) {
				lua_pushnil(L);
				while (lua_next(L, -2)) {
					lua_State* co = lua_tothread(L, -2);
					if (co && lua_status(co) == LUA_YIELD) {
						hasActiveCoroutine = true;
					}
					lua_pop(L, 1);
				}
				lua_pop(L, 1);  // active_coroutines table
			} else {
				lua_pop(L, 1);  // _list error or non-table result
			}
		} else {
			lua_pop(L, 2);  // coroutine table + nil _list
		}
	}
	inLua = false;
	sys->SetWorkDir();
	if (err && !didExit) {
		const char* msg = lua_tostring(L, -1);
		if (!msg) {
			msg = lua_typename(L, lua_type(L, -1));
		}
		DoError("Runtime error in", msg);
	} else if (!err && lua_gettop(L) > 1 && lua_isfunction(L, 1)) {
		// PCall leaves the traceback handler at index 1; drop stray stack values.
		lua_settop(L, 1);
	}
}

void ui_main_c::DoError(const char* msg, const char* error)
{
	auto scriptStr = scriptName.generic_u8string();
	char* errText = AllocStringLen(strlen(msg) + scriptStr.size() + strlen(error) + 30);
	sprintf(errText, "--- SCRIPT ERROR ---\n%s '%s':\n%s\n", msg, scriptStr.c_str(), error);
	sys->Exit(errText);
	FreeString(errText);
	didExit = true;
}

// From lua.c
static int traceback (lua_State *L) {
  if (!lua_isstring(L, 1))  /* 'message' not a string? */
    return 1;  /* keep it intact */
  lua_getglobal(L, "debug");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 1;
  }
  lua_getfield(L, -1, "traceback");
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    return 1;
  }
  lua_pushvalue(L, 1);  /* pass error message */
  lua_pushinteger(L, 2);  /* skip this function and traceback */
  lua_call(L, 2, 1);  /* call debug.traceback */
  return 1;
}

static int l_panicFunc(lua_State* L)
{
	lua_rawgeti(L, LUA_REGISTRYINDEX, ui_main_c::REGISTRY_KEY);
	ui_main_c* ui = (ui_main_c*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	ui->sys->Error("Unprotected Lua error:\n%s", lua_tostring(L, -1));
	return 0;
}

// ======================
// UI Init/Frame/Shutdown
// ======================

void ui_main_c::Init(int argc, char** argv)
{
	// Find paths
	scriptName = std::filesystem::u8path(argv[0]);
	if (scriptName.is_relative()) {
#if __APPLE__ && __MACH__
		// SetWorkDir() runs before Init; resolve scripts from the original launch directory.
		auto fromLaunch = sys->launchCwd / scriptName;
		if (std::filesystem::exists(fromLaunch)) {
			scriptName = fromLaunch;
		} else
#endif
		{
			scriptName = sys->basePath / scriptName;
		}
	}
	{
		std::error_code ec;
		auto resolved = std::filesystem::weakly_canonical(scriptName, ec);
		if (ec) {
			sys->Error("Script path error for '%s': %s",
				scriptName.generic_u8string().c_str(), ec.message().c_str());
		}
		scriptName = resolved;
	}

	scriptCfg = scriptName;
	scriptCfg.replace_extension(".cfg");

	auto scriptParent = scriptName.parent_path();
	scriptPath = scriptParent;
	scriptWorkDir = scriptParent;

	scriptArgc = argc;
	scriptArgv = new char*[argc];
	for (int a = 0; a < argc; a++) {
		scriptArgv[a] = AllocString(argv[a]);
	}

	// Load config files
	core->config->LoadConfig("SimpleGraphic/SimpleGraphic.cfg");
	core->config->LoadConfig("SimpleGraphic/SimpleGraphicAuto.cfg");
	if (core->config->LoadConfig(scriptCfg)) {
		scriptCfg.clear();
	}

	// Initialise script
	ScriptInit();
	while (restartFlag && !didExit) {
		ScriptShutdown();
		ScriptInit();
	}
}

void ui_main_c::RenderInit(r_featureFlag_e features)
{
	if (renderer) {
		return;
	}

	sys->SetWorkDir();

	sys->con->ExecCommands(true);

	// Initialise window
	core->video->Apply();

	// Initialise renderer
	renderer = r_IRenderer::GetHandle(sys);
	renderer->Init(features);

	// Create UI console handler
	conUI = ui_IConsole::GetHandle(this);

	sys->con->Printf("\n");
	sys->SetWorkDir(scriptWorkDir);
}

void ui_main_c::ScriptInit()
{
	sys->con->PrintFunc("UI Init");

	sys->con->Printf("Script: %s\n", scriptName.generic_u8string().c_str());
	if (!scriptPath.empty()) {
		sys->con->Printf("Script working directory: %s\n", scriptWorkDir.generic_u8string().c_str());
	}
	sys->video->SetTitle(scriptName.generic_u8string().c_str());

	restartFlag = false;
	didExit = false;
	renderEnable = false;
	inLua = false;

	// Initialise Lua
	sys->con->Printf("Initialising Lua...\n");
	solState.emplace();
	L = solState->lua_state();
	if ( !L ) sys->Error("Error: unable to create Lua state.");
	lua_atpanic(L, l_panicFunc);
	lua_pushlightuserdata(L, this);
	lua_seti(L, LUA_REGISTRYINDEX, ui_main_c::REGISTRY_KEY);
	lua_pushcfunction(L, traceback);
	lua_pushvalue(L, -1);
	lua_setfield(L, LUA_REGISTRYINDEX, "traceback");

#if _WIN32
	lua_pushboolean(L, 1);
	lua_setfield(L, LUA_REGISTRYINDEX, "LUA_NOENV");
#endif

	// Add libraries and APIs
	lua_gc(L, LUA_GCSTOP, 0);
	lua_pushcfunction(L, InitAPI);
	int err = lua_pcall(L, 0, 0, 0);
	if (err) sys->Error("Error initialising Lua environment: \n%s\n", lua_tostring(L, -1));
	lua_gc(L, LUA_GCRESTART, -1);

#if __APPLE__ && __MACH__
	// Stub jit.opt.start (broken GC64 interpreter path); keep JIT on for multi-assign. (#8)
	static char const* const kMacJit =
		"if jit then "
		"jit.opt.start = function(...) end "
		"end";
	if (luaL_dostring(L, kMacJit) != LUA_OK) {
		sys->con->Printf("Warning: macOS JIT stub failed: %s\n", lua_tostring(L, -1));
		lua_pop(L, 1);
	} else {
		sys->con->Printf("LuaJIT JIT enabled on macOS (jit.opt.start stubbed).\n");
	}

	// LuaJIT 2.1 arm64 interpreter crashes when Lua bytecode calls any function
	// stored in a GC heap table (e.g. package.loadlib, package.loaders entries).
	// Pre-register C extensions in package.preload via the C API so require()
	// finds them through the C-side preload path, bypassing the broken interpreter
	// table-call path. (#8)
	{
		struct LuaCExt { const char* module; const char* sym; const char* file; };
		static const LuaCExt kExts[] = {
			{ "lcurl.safe", "luaopen_lcurl_safe", "lcurl.so"    },
			{ "lcurl",      "luaopen_lcurl",      "lcurl.so"    },
			{ "lzip",       "luaopen_lzip",       "lzip.so"     },
			{ "lua-utf8",   "luaopen_utf8",       "lua-utf8.so" },
		};
		lua_getglobal(L, "package");
		lua_getfield(L, -1, "preload");
		for (const auto& ext : kExts) {
			auto soPath = (sys->basePath / ext.file).lexically_normal();
			void* handle = dlopen(soPath.generic_u8string().c_str(), RTLD_NOW | RTLD_LOCAL);
			if (handle) {
				auto fn = reinterpret_cast<lua_CFunction>(dlsym(handle, ext.sym));
				if (fn) {
					lua_pushcfunction(L, fn);
					lua_setfield(L, -2, ext.module);
				} else {
					sys->con->Printf("Warning: macOS preload: symbol %s not found in %s\n", ext.sym, ext.file);
				}
			} else {
				sys->con->Printf("Warning: macOS preload: dlopen %s failed: %s\n", ext.file, dlerror());
			}
		}
		lua_pop(L, 2); // preload table, package table
	}

	// Replace built-ins that are GC closures (or use broken fast-paths) in arm64 GC64.
	lua_pushcfunction(L, l_mac_require); lua_setglobal(L, "require");
	lua_pushcfunction(L, l_mac_pcall);   lua_setglobal(L, "pcall");
	lua_pushcfunction(L, l_mac_xpcall);  lua_setglobal(L, "xpcall");
	sys->con->Printf("macOS: require/pcall/xpcall replaced with LIGHTFUNCs.\n");
#endif

	// Setup debug system
	debug = ui_IDebug::GetHandle(this);

	// Setup subscript system
	subScriptSize = 16;
	subScriptList = new ui_ISubScript*[subScriptSize];
	for (dword i = 0; i < subScriptSize; i++) {
		subScriptList[i] = NULL;
	}
	
	// Load the script file
	sys->SetWorkDir(scriptWorkDir);
	err = luaL_loadfile(L, scriptName.generic_u8string().c_str());
	if (err) {
		DoError("Error loading", lua_tostring(L, -1));
		return;
	}
	sys->SetWorkDir();

	// Run the script
	sys->con->Printf("Running script...\n");
	for (int i = 0; i < scriptArgc; i++) {
		lua_pushstring(L, scriptArgv[i]);
	}
	lua_createtable(L, scriptArgc - 1, 1);
	for (int i = 0; i < scriptArgc; i++) {
		lua_pushstring(L, scriptArgv[i]);
		lua_rawseti(L, -2, i);
	}
	lua_setglobal(L, "arg");
	PCall(scriptArgc, 0);

	if ( !didExit && !restartFlag ) {
		// Run initialisation callback
		int extraArgs = PushCallback("OnInit");
		if (extraArgs >= 0) {
			PCall(extraArgs, 0);
		}
	}
	if ( !didExit && !restartFlag ) {
		// Check for frame callback
		int extraArgs = PushCallback("OnFrame");
		if (extraArgs < 0) {
			sys->con->Printf("\nScript didn't set frame callback, exiting...\n");
			sys->Exit();
		} else if (lua_gettop(L) > 1 && lua_isfunction(L, 1)) {
			lua_settop(L, 1);
		}
	}
}

void ui_main_c::Frame()
{
	// Check for any subscripts we need to run
	bool hasSubscript = false;
	for (dword i = 0; i < subScriptSize; i++) {
		if (subScriptList[i]) {
			hasSubscript = true;
			break;
		}
	}
	// Always runs 10 frames after finishing the boot process
	if (!sys->video->IsVisible() || sys->conWin->IsVisible() || restartFlag || didExit) {
		framesSinceWindowHidden = 0;
	}
	else if (framesSinceWindowHidden <= 10) {
		framesSinceWindowHidden++;
	}
	// Otherwise only runs frames if the mouse is on screen, there is an active coroutine, or there is an active subscript
	else if (!sys->video->IsActive() && !sys->video->IsCursorOverWindow() && !hasActiveCoroutine && !hasSubscript) {
		sys->Sleep(100);
		return;
	}	
	
	if (renderer) {
		// Prepare for rendering
		renderer->BeginFrame();

		sys->video->GetRelativeCursor(cursorX, cursorY);
	}

	renderEnable = true;

	// Run subscript system
	for (dword i = 0; i < subScriptSize; i++) {
		if (subScriptList[i]) {
			subScriptList[i]->SubScriptFrame();
			if ( !subScriptList[i]->IsRunning() ) {
				ui_ISubScript::FreeHandle(subScriptList[i]);
				subScriptList[i] = NULL;
			}
		}
	}

	// Run script
	//sys->con->Printf("OnFrame...\n");
	int extraArgs = PushCallback("OnFrame");
	if (extraArgs >= 0) {
		PCall(extraArgs, 0);
	}

	renderEnable = false;

	if (renderer) {
		// Render console
		//sys->con->Printf("Render console...\n");
		renderer->SetDrawLayer(10000);
		conUI->Render();

		// Finish up
		//sys->con->Printf("EndFrame...\n");
		renderer->EndFrame();
	}

	//sys->con->Printf("Finishing up...\n");
	if ( !sys->video->IsActive() && !hasActiveCoroutine && !hasSubscript ) {
		sys->Sleep(100);
	}

	while (restartFlag) {
		ScriptShutdown();
		if (renderer) {
			renderer->PurgeShaders();
		}
		ScriptInit();
	}
}

void ui_main_c::ScriptShutdown()
{
	// Run exit callback
	int extraArgs = PushCallback("OnExit");
	if (extraArgs >= 0) {
		PCall(extraArgs, 0);
	}

	// Shutdown subscript and debug systems
	for (dword i = 0; i < subScriptSize; i++) {
		if (subScriptList[i]) {
			ui_ISubScript::FreeHandle(subScriptList[i]);
		}
	}
	delete subScriptList;
	ui_IDebug::FreeHandle(debug);

	// Shutdown Lua
	L = NULL;
	solState.reset();
}

void ui_main_c::Shutdown()
{
	// Shutdown script
	ScriptShutdown();

	if (renderer) {
		ui_IConsole::FreeHandle(conUI);

		// Shutdown renderer
		renderer->Shutdown();
		r_IRenderer::FreeHandle(renderer);	

		// Shutdown window
		sys->video->SetVisible(false);
		core->video->Save();
	}

	// Save config
	if (!scriptCfg.empty()) {
		core->config->SaveConfig(scriptCfg);
	} else {
		core->config->SaveConfig("SimpleGraphic/SimpleGraphic.cfg");
	}

	for (int a = 0; a < scriptArgc; a++) {
		FreeString(scriptArgv[a]);
	}
	delete scriptArgv;
}

bool ui_main_c::CanExit()
{
	bool ret = true;
	int extraArgs = PushCallback("CanExit");
	if (extraArgs >= 0) {
		PCall(extraArgs, 1);
		ret = !!lua_toboolean(L, -1);
		lua_pop(L, 1);
	}
	return ret;
}

// ==============
// Input Handling
// ==============

void ui_main_c::KeyEvent(int key, int type)
{
	if (conUI && conUI->KeyEvent(key, type)) {
		return;
	}

	switch (type) {
	case KE_CHAR:
		CallKeyHandler("OnChar", key, false);
		break;
	case KE_KEYDOWN:
	case KE_DBLCLK:
		CallKeyHandler("OnKeyDown", key, type == KE_DBLCLK);
		break;
	case KE_KEYUP:
		switch (key) {
		case KEY_F10:
			if (renderer) {
				renderer->ToggleDebugImGui();
			}
			break;
		case KEY_PAUSE:
			if (sys->IsKeyDown(KEY_SHIFT)) {
				debug->ToggleProfiling();
				break;
			}
		default:
			CallKeyHandler("OnKeyUp", key, false);
			break;
		}
		break;
	} 
}

void ui_main_c::CallKeyHandler(const char* hname, int key, bool dblclk)
{
	if ( !L ) return;
	int extraArgs = PushCallback(hname);
	if (extraArgs < 0) {
		return;
	}
	if (key < 128) {
		lua_pushfstring(L, "%c", key);
	} else {
		lua_pushstring(L, NameForKey(key));
	}
	lua_pushboolean(L, dblclk);
	PCall(2 + extraArgs, 0);
}

const char* ui_main_c::NameForKey(int key)
{
	for (int i = 0; ui_keyNameMap[i].key; i++) {
		if (ui_keyNameMap[i].key == key) {
			return ui_keyNameMap[i].str;
		}
	}
	return "?";
}

int ui_main_c::KeyForName(const char* keyName)
{
	if (keyName[1]) {
		// > 1 character in length, do a lookup
		for (int i = 0; ui_keyNameMap[i].key; i++) {
			if ( !_stricmp(keyName, ui_keyNameMap[i].str) ) {
				return ui_keyNameMap[i].key;
			}
		}
	} else {
		if (isalpha(*keyName)) {
			return tolower(*keyName);
		} else if (*keyName == ' ' || isdigit(*keyName)) {
			return *keyName;
		}
	}
	return 0;
}
