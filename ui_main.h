// DyLua: SimpleGraphic
// (c) David Gowor, 2014
//
// UI Main Header
//

// =======
// Classes
// =======

struct ui_expectationFailed_s {};

#if __APPLE__ && __MACH__
// Custom LuaJIT allocator: ensures no address in [user, user+nsize) has lower
// 32 bits = 0, preventing 4GB-boundary crashes on arm64 GC64. (#8)
void* mac_gc64_alloc(void* ud, void* ptr, size_t osize, size_t nsize);
bool mac_pload_module_pcall(lua_State* L, const char* modName);
void mac_sync_globals_from_helper_co(lua_State* L);
#endif

// UI Manager
class ui_main_c: public ui_IMain {
public:
	// Interface
	void	Init(int argc, char** argv);
	void	Frame();
	void	Shutdown();
	void	KeyEvent(int key, int type);
	bool	CanExit();

	// Encapsulated
	ui_main_c(sys_IMain* sysHnd, core_IMain* coreHnd);

	sys_IMain* sys = nullptr;
	core_IMain* core = nullptr;

	r_IRenderer* renderer = nullptr;

	ui_IConsole* conUI = nullptr;
	ui_IDebug* debug = nullptr;

	dword	subScriptSize = 0;
	ui_ISubScript** subScriptList = nullptr;

	std::optional<sol::state> solState;
	lua_State* L = nullptr;
	std::filesystem::path scriptName;
	std::filesystem::path scriptCfg;
	std::filesystem::path scriptPath;
	std::filesystem::path scriptWorkDir;
	int		scriptArgc = 0;
	char**	scriptArgv = nullptr;
	bool	restartFlag = false;
	bool	didExit = false;
	bool	renderEnable = false;
	int		cursorX = 0;
	int		cursorY = 0;
	int		framesSinceWindowHidden = 0;
	volatile bool	inLua = false;
	bool	hasActiveCoroutine = false;
	int		ioOpenf = LUA_NOREF;

	float lastColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};

	static int InitAPI(lua_State* L);

	void	RenderInit(r_featureFlag_e features);
	void	ScriptInit();
	void	ScriptShutdown();

	void	LAssert(lua_State* L, int cond, const char* fmt, ...); // Non-local return to Lua code on failure
	void	LExpect(lua_State* L, int cond, const char* fmt, ...); // Throws ui_expectationFailed_s on failure, message on Lua stack
	int		IsUserData(lua_State* L, int index, const char* metaName);
	int		PushCallback(const char* name);
	void	PCall(int narg, int nret);
#if __APPLE__ && __MACH__
	void	CallCallbackOnThread(int extraArgs);
#endif
	void	DoError(const char* msg, const char* error);

	void	CallKeyHandler(const char* hname, int key, bool dblclk);
	const char* NameForKey(int key);
	int		KeyForName(const char* name);

	enum { REGISTRY_KEY = 1 };
};

#if __APPLE__ && __MACH__
typedef bool (*MacPLoadCompileFunc)(lua_State* L, lua_State* co, const char* modname);
// Stack: [func, arg1..argN] -> [true, ...] or [false, err]. (#8)
int mac_lightfunc_pcall(lua_State* L, int nargs);
// Stack: [chunk, arg1..argN]. Run in coroutine; optionally service "__mac_lm"
// yield requests via compile_fn. (#8)
int mac_pload_coroutine_call(lua_State* L, int extraArgs, MacPLoadCompileFunc compile_fn = nullptr);
bool mac_is_in_pload();
void mac_set_in_pload(bool in_pload);
bool mac_is_servicing_pload_queue();
void mac_set_servicing_pload_queue(bool servicing);
// luaJIT_setmode(LUAJIT_MODE_ENGINE|OFF) + flush; Lua jit.off() fallback only. (#8)
void mac_jit_off(lua_State* L);
#endif
