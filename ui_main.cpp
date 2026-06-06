// DyLua: SimpleGraphic
// (c) David Gowor, 2014
//
// Module: UI Main
//

#include "ui_local.h"
#if __APPLE__ && __MACH__
#include <dlfcn.h>
#include <luajit.h>

// Light C function replacements for LuaJIT built-ins whose interpreter fast-paths
// are broken in GC64 mode on arm64. Protected calls use lua_resume on a helper
// coroutine — lua_pcall from inside a LIGHTFUNC corrupts interpreter return state. (#8)

#if __APPLE__ && __MACH__
// LuaJIT arm64 GC64: several bytecode handlers contain 32-bit loads for
// GCobj pointers (e.g. BC_GGET). When an object sits at a 4GB-multiple
// address (lower 32 bits = 0x0) the truncated read yields NULL, triggering
// EXC_BAD_ACCESS at FAR=0x0. Fix: custom allocator installed via
// lua_newstate that reserves 16 bytes before every user pointer. The
// 1-byte offset field stored at user[-1] allows safe free/realloc. If
// user = raw+16 would itself land on a 4GB boundary, offset 8 is used
// instead (raw+8 can never simultaneously be at a boundary). (#8)
void* mac_gc64_alloc(void* /*ud*/, void* ptr, size_t osize, size_t nsize) {
    // Layout: [raw ... (off-1 padding bytes) ... offset_byte ... user_data(nsize)]
    // off = 16 normally; off = 8 when raw+16 lower-32 == 0.
    if (nsize == 0) {
        if (ptr) {
            uint8_t off = *((uint8_t*)ptr - 1);
            free((char*)ptr - off);
        }
        return nullptr;
    }
    char* raw = nullptr;
    uint8_t old_off = 16;
    if (ptr) {
        old_off = *((uint8_t*)ptr - 1);
        raw = (char*)realloc((char*)ptr - old_off, nsize + 16);
    } else {
        raw = (char*)malloc(nsize + 16);
    }
    if (!raw) return nullptr;
    uint8_t new_off = (((uintptr_t)(raw + 16)) & 0xFFFFFFFFu) == 0u ? 8u : 16u;
    if (ptr && new_off != old_off) {
        size_t copy = nsize < osize ? nsize : osize;
        memmove(raw + new_off, raw + old_off, copy);
    }
    *((uint8_t*)(raw + new_off) - 1) = new_off;
    return raw + new_off;
}

// Copy launch from helper thread into uicallbacks.MainObject (SetMainObject from co is unreliable). (#8)
static void mac_sync_main_object_from_co(lua_State* L, lua_State* co) {
	lua_getglobal(co, "launch");
	if (!lua_istable(co, -1)) {
		lua_pop(co, 1);
		return;
	}
	lua_getfield(L, LUA_REGISTRYINDEX, "uicallbacks");
	lua_pushstring(L, "MainObject");
	lua_xmove(co, L, 1);
	lua_rawset(L, -3);
	lua_pop(L, 1);
}

// SetMainObject from the launch coroutine may not update _G.launch; modules read the global. (#8)
static void mac_ensure_global_launch(lua_State* L) {
	lua_getfield(L, LUA_REGISTRYINDEX, "uicallbacks");
	if (!lua_istable(L, -1)) {
		lua_settop(L, 0);
		return;
	}
	lua_getfield(L, -1, "MainObject");
	if (lua_istable(L, -1)) {
		lua_setglobal(L, "launch");
	}
	lua_settop(L, 0);
}
#endif

static void mac_restore_raw_coroutine_create(lua_State* L) {
    lua_getglobal(L, "coroutine");
    lua_getfield(L, LUA_REGISTRYINDEX, "mac_co_create");
    lua_setfield(L, -2, "create");
    lua_pop(L, 1);
}

static int l_mac_restore_co(lua_State* L) {
    mac_restore_raw_coroutine_create(L);
    return 0;
}

// Disable the whole JIT engine via the LuaJIT C API (reliable on arm64 GC64). (#8)
void mac_jit_off(lua_State* L) {
	const int off = LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF;
	if (luaJIT_setmode(L, 0, off) != 1) {
		lua_getglobal(L, "jit");
		if (lua_istable(L, -1)) {
			lua_getfield(L, -1, "off");
			if (lua_isfunction(L, -1)) {
				lua_call(L, 0, 0);
			}
			lua_pop(L, 1);
		} else {
			lua_pop(L, 1);
		}
	}
	luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_FLUSH);
}

// LuaJIT arm64 FFUNC replacement: LJLIB_ASM fast functions have broken assembly
// dispatch on arm64 GC64. Functions with lua_tocfunction() != NULL (LJLIB_CF)
// are re-registered as LIGHTFUNCs. Functions returning NULL need manual C
// implementations. (#8)
//
// Converts one function: if LJLIB_ASM (tocfunction==NULL), skip (needs manual).
// If LJLIB_CF, re-register as LIGHTFUNC.
static int mac_convert_cfunc_in_table(lua_State* L, sys_IMain* sys,
    const char* tblName, int tblIdx)
{
    int converted = 0;
    lua_pushnil(L);
    while (lua_next(L, tblIdx) != 0) {
        if (lua_type(L, -1) == LUA_TFUNCTION && lua_iscfunction(L, -1)) {
            lua_CFunction cfn = lua_tocfunction(L, -1);
            if (cfn) {
                // CClosures with upvalues must keep them — skip
                const char* upname = lua_getupvalue(L, -1, 1);
                if (upname) {
                    lua_pop(L, 2); // pop upvalue value + function value, keep key
                    continue;
                }
                lua_pop(L, 1); // pop value
                lua_pushcfunction(L, cfn);
                const char* key = lua_isstring(L, -2) ? lua_tostring(L, -2) : nullptr;
                if (key) {
                    lua_setfield(L, tblIdx, key);
                    converted++;
                } else {
                    lua_pop(L, 1);
                }
            } else {
                lua_pop(L, 1); // pop value, keep key
            }
        } else {
            lua_pop(L, 1); // pop value, keep key
        }
    }
    return converted;
}

// Manual LIGHTFUNC replacements for LJLIB_ASM functions (tocfunction==NULL).
static int mac_lf_tostring(lua_State* L) {
    luaL_checkany(L, 1);
    if (luaL_callmeta(L, 1, "__tostring")) return 1;
    switch (lua_type(L, 1)) {
        case LUA_TNIL: lua_pushliteral(L, "nil"); break;
        case LUA_TBOOLEAN: lua_pushstring(L, lua_toboolean(L, 1) ? "true" : "false"); break;
        case LUA_TNUMBER: {
            char buf[64];
            if (lua_isinteger(L, 1))
                snprintf(buf, sizeof(buf), "%lld", (long long)lua_tointeger(L, 1));
            else
                snprintf(buf, sizeof(buf), "%.14g", lua_tonumber(L, 1));
            lua_pushstring(L, buf);
            break;
        }
        case LUA_TSTRING: lua_pushvalue(L, 1); break;
        default:
            lua_pushfstring(L, "%s: %p", luaL_typename(L, 1), lua_topointer(L, 1));
            break;
    }
    return 1;
}

static int mac_lf_tonumber(lua_State* L) {
    int base = (int)luaL_optinteger(L, 2, 10);
    if (base == 10) {
        luaL_checkany(L, 1);
        if (lua_type(L, 1) == LUA_TNUMBER) {
            lua_pushvalue(L, 1);
            return 1;
        }
        const char* s = lua_tostring(L, 1);
        if (s) {
            char* end;
            double d = strtod(s, &end);
            if (end != s && *end == '\0') { lua_pushnumber(L, d); return 1; }
            while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') end++;
            if (*end == '\0' && end != s) { lua_pushnumber(L, d); return 1; }
        }
        lua_pushnil(L);
        return 1;
    }
    const char* s = luaL_checkstring(L, 1);
    luaL_argcheck(L, base >= 2 && base <= 36, 2, "base out of range");
    char* end;
    unsigned long long r = strtoull(s, &end, base);
    while (*end == ' ' || *end == '\t') end++;
    if (end == s || *end != '\0') { lua_pushnil(L); return 1; }
    lua_pushnumber(L, (lua_Number)r);
    return 1;
}

static int mac_lf_assert(lua_State* L) {
    if (!lua_toboolean(L, 1)) {
        const char* msg = lua_isstring(L, 2) ? lua_tostring(L, 2) : "assertion failed!";
        return luaL_error(L, "%s", msg);
    }
    return lua_gettop(L);
}

static int mac_lf_next(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 2);
    if (lua_next(L, 1)) return 2;
    lua_pushnil(L);
    return 1;
}

static int mac_lf_rawget(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checkany(L, 2);
    lua_rawget(L, 1);
    return 1;
}

static int mac_lf_rawlen(lua_State* L) {
    int t = lua_type(L, 1);
    luaL_argcheck(L, t == LUA_TTABLE || t == LUA_TSTRING, 1, "table or string expected");
    lua_pushinteger(L, (lua_Integer)lua_rawlen(L, 1));
    return 1;
}

static int mac_lf_setmetatable(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    int t2 = lua_type(L, 2);
    luaL_argcheck(L, t2 == LUA_TNIL || t2 == LUA_TTABLE, 2, "nil or table expected");
    lua_settop(L, 2);
    lua_setmetatable(L, 1);
    lua_settop(L, 1);
    return 1;
}

static int mac_lf_getmetatable(lua_State* L) {
    luaL_checkany(L, 1);
    if (!lua_getmetatable(L, 1)) { lua_pushnil(L); return 1; }
    lua_getfield(L, -1, "__metatable");
    if (!lua_isnil(L, -1)) return 1;
    lua_pop(L, 1);
    return 1;
}

static int mac_lf_rawequal(lua_State* L) {
    luaL_checkany(L, 1);
    luaL_checkany(L, 2);
    lua_pushboolean(L, lua_rawequal(L, 1, 2));
    return 1;
}

static int mac_lf_collectgarbage(lua_State* L) {
    static const char* const opts[] = {
        "stop", "restart", "collect", "count", "step",
        "setpause", "setstepmul", "isrunning", nullptr
    };
    static const int optsnum[] = {
        LUA_GCSTOP, LUA_GCRESTART, LUA_GCCOLLECT, LUA_GCCOUNT, LUA_GCSTEP,
        LUA_GCSETPAUSE, LUA_GCSETSTEPMUL, 9/*LUA_GCISRUNNING*/
    };
    int o = luaL_checkoption(L, 1, "collect", opts);
    int ex = (int)luaL_optinteger(L, 2, 0);
    int res = lua_gc(L, optsnum[o], ex);
    if (o == 3) { // count
        int b = lua_gc(L, LUA_GCCOUNTB, 0);
        lua_pushnumber(L, (lua_Number)res + (lua_Number)b / 1024.0);
        return 1;
    }
    lua_pushinteger(L, res);
    return 1;
}

// String library LJLIB_ASM replacements
static int mac_lf_string_byte(lua_State* L) {
    size_t len;
    const char* s = luaL_checklstring(L, 1, &len);
    lua_Integer pi = luaL_optinteger(L, 2, 1);
    lua_Integer pj = luaL_optinteger(L, 3, pi);
    if (pi < 0) pi += (lua_Integer)len + 1;
    if (pj < 0) pj += (lua_Integer)len + 1;
    if (pi < 1) pi = 1;
    if (pj > (lua_Integer)len) pj = (lua_Integer)len;
    int n = 0;
    for (lua_Integer i = pi; i <= pj; i++) {
        lua_pushinteger(L, (unsigned char)s[i - 1]);
        n++;
    }
    return n;
}

static int mac_lf_string_char(lua_State* L) {
    int n = lua_gettop(L);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (int i = 1; i <= n; i++) {
        int c = (int)luaL_checkinteger(L, i);
        luaL_argcheck(L, (unsigned int)c <= 255, i, "invalid value");
        luaL_addchar(&b, (char)c);
    }
    luaL_pushresult(&b);
    return 1;
}

static int mac_lf_string_len(lua_State* L) {
    size_t len;
    luaL_checklstring(L, 1, &len);
    lua_pushinteger(L, (lua_Integer)len);
    return 1;
}

static int mac_lf_string_sub(lua_State* L) {
    size_t len;
    const char* s = luaL_checklstring(L, 1, &len);
    lua_Integer start = luaL_checkinteger(L, 2);
    lua_Integer end = luaL_optinteger(L, 3, -1);
    if (start < 0) start += (lua_Integer)len + 1;
    if (end < 0) end += (lua_Integer)len + 1;
    if (start < 1) start = 1;
    if (end > (lua_Integer)len) end = (lua_Integer)len;
    if (start > end) { lua_pushliteral(L, ""); return 1; }
    lua_pushlstring(L, s + start - 1, (size_t)(end - start + 1));
    return 1;
}

static int mac_lf_string_rep(lua_State* L) {
    size_t len;
    const char* s = luaL_checklstring(L, 1, &len);
    int n = (int)luaL_checkinteger(L, 2);
    if (n <= 0) { lua_pushliteral(L, ""); return 1; }
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    while (n-- > 0) luaL_addlstring(&b, s, len);
    luaL_pushresult(&b);
    return 1;
}

static int mac_lf_string_reverse(lua_State* L) {
    size_t len;
    const char* s = luaL_checklstring(L, 1, &len);
    luaL_Buffer b;
    char* p = luaL_buffinitsize(L, &b, len);
    for (size_t i = 0; i < len; i++) p[i] = s[len - 1 - i];
    luaL_pushresultsize(&b, len);
    return 1;
}

static int mac_lf_string_lower(lua_State* L) {
    size_t len;
    const char* s = luaL_checklstring(L, 1, &len);
    luaL_Buffer b;
    char* p = luaL_buffinitsize(L, &b, len);
    for (size_t i = 0; i < len; i++) p[i] = (char)tolower((unsigned char)s[i]);
    luaL_pushresultsize(&b, len);
    return 1;
}

static int mac_lf_string_upper(lua_State* L) {
    size_t len;
    const char* s = luaL_checklstring(L, 1, &len);
    luaL_Buffer b;
    char* p = luaL_buffinitsize(L, &b, len);
    for (size_t i = 0; i < len; i++) p[i] = (char)toupper((unsigned char)s[i]);
    luaL_pushresultsize(&b, len);
    return 1;
}

// Math library LJLIB_ASM replacements
static int mac_lf_math_abs(lua_State* L) { lua_pushnumber(L, fabs(luaL_checknumber(L, 1))); return 1; }
static int mac_lf_math_floor(lua_State* L) { lua_pushnumber(L, floor(luaL_checknumber(L, 1))); return 1; }
static int mac_lf_math_ceil(lua_State* L) { lua_pushnumber(L, ceil(luaL_checknumber(L, 1))); return 1; }
static int mac_lf_math_sqrt(lua_State* L) { lua_pushnumber(L, sqrt(luaL_checknumber(L, 1))); return 1; }
static int mac_lf_math_log(lua_State* L) { lua_pushnumber(L, log(luaL_checknumber(L, 1))); return 1; }
static int mac_lf_math_log10(lua_State* L) { lua_pushnumber(L, log10(luaL_checknumber(L, 1))); return 1; }
static int mac_lf_math_exp(lua_State* L) { lua_pushnumber(L, exp(luaL_checknumber(L, 1))); return 1; }
static int mac_lf_math_sin(lua_State* L) { lua_pushnumber(L, sin(luaL_checknumber(L, 1))); return 1; }
static int mac_lf_math_cos(lua_State* L) { lua_pushnumber(L, cos(luaL_checknumber(L, 1))); return 1; }
static int mac_lf_math_tan(lua_State* L) { lua_pushnumber(L, tan(luaL_checknumber(L, 1))); return 1; }
static int mac_lf_math_asin(lua_State* L) { lua_pushnumber(L, asin(luaL_checknumber(L, 1))); return 1; }
static int mac_lf_math_acos(lua_State* L) { lua_pushnumber(L, acos(luaL_checknumber(L, 1))); return 1; }
static int mac_lf_math_atan(lua_State* L) { lua_pushnumber(L, atan(luaL_checknumber(L, 1))); return 1; }
static int mac_lf_math_atan2(lua_State* L) { lua_pushnumber(L, atan2(luaL_checknumber(L, 1), luaL_checknumber(L, 2))); return 1; }
static int mac_lf_math_sinh(lua_State* L) { lua_pushnumber(L, sinh(luaL_checknumber(L, 1))); return 1; }
static int mac_lf_math_cosh(lua_State* L) { lua_pushnumber(L, cosh(luaL_checknumber(L, 1))); return 1; }
static int mac_lf_math_tanh(lua_State* L) { lua_pushnumber(L, tanh(luaL_checknumber(L, 1))); return 1; }
static int mac_lf_math_pow(lua_State* L) { lua_pushnumber(L, pow(luaL_checknumber(L, 1), luaL_checknumber(L, 2))); return 1; }
static int mac_lf_math_fmod(lua_State* L) { lua_pushnumber(L, fmod(luaL_checknumber(L, 1), luaL_checknumber(L, 2))); return 1; }
static int mac_lf_math_max(lua_State* L) {
    int n = lua_gettop(L);
    luaL_argcheck(L, n >= 1, 1, "value expected");
    lua_Number m = luaL_checknumber(L, 1);
    for (int i = 2; i <= n; i++) { lua_Number v = luaL_checknumber(L, i); if (v > m) m = v; }
    lua_pushnumber(L, m);
    return 1;
}
static int mac_lf_math_min(lua_State* L) {
    int n = lua_gettop(L);
    luaL_argcheck(L, n >= 1, 1, "value expected");
    lua_Number m = luaL_checknumber(L, 1);
    for (int i = 2; i <= n; i++) { lua_Number v = luaL_checknumber(L, i); if (v < m) m = v; }
    lua_pushnumber(L, m);
    return 1;
}
static int mac_lf_math_frexp(lua_State* L) {
    int e; lua_pushnumber(L, frexp(luaL_checknumber(L, 1), &e));
    lua_pushinteger(L, e); return 2;
}
static int mac_lf_math_ldexp(lua_State* L) {
    lua_pushnumber(L, ldexp(luaL_checknumber(L, 1), (int)luaL_checkinteger(L, 2))); return 1;
}
static int mac_lf_math_modf(lua_State* L) {
    double ip; double fp = modf(luaL_checknumber(L, 1), &ip);
    lua_pushnumber(L, ip); lua_pushnumber(L, fp); return 2;
}
static int mac_lf_math_deg(lua_State* L) { lua_pushnumber(L, luaL_checknumber(L, 1) * (180.0 / M_PI)); return 1; }
static int mac_lf_math_rad(lua_State* L) { lua_pushnumber(L, luaL_checknumber(L, 1) * (M_PI / 180.0)); return 1; }

// Table library replacements
static int mac_lf_table_concat(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    size_t seplen;
    const char* sep = luaL_optlstring(L, 2, "", &seplen);
    lua_Integer i = luaL_optinteger(L, 3, 1);
    lua_Integer j = luaL_optinteger(L, 4, (lua_Integer)lua_rawlen(L, 1));
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (; i <= j; i++) {
        lua_rawgeti(L, 1, i);
        luaL_addvalue(&b);
        if (i < j) luaL_addlstring(&b, sep, seplen);
    }
    luaL_pushresult(&b);
    return 1;
}

// Master function to replace all broken LJLIB_ASM FFUNCs
static void mac_replace_broken_ffuncs(lua_State* L, sys_IMain* sys) {
    int total = 0;

    // 1. Replace LJLIB_ASM globals with manual implementations
    struct { const char* name; lua_CFunction fn; } baseReplacements[] = {
        {"tostring", mac_lf_tostring},
        {"tonumber", mac_lf_tonumber},
        {"assert", mac_lf_assert},
        {"next", mac_lf_next},
        {"rawget", mac_lf_rawget},
        {"rawlen", mac_lf_rawlen},
        {"rawequal", mac_lf_rawequal},
        {"setmetatable", mac_lf_setmetatable},
        {"getmetatable", mac_lf_getmetatable},
        {"collectgarbage", mac_lf_collectgarbage},
    };
    for (auto& r : baseReplacements) {
        lua_pushcfunction(L, r.fn);
        lua_setglobal(L, r.name);
        total++;
    }

    // 2. Convert all LJLIB_CF globals (with valid C pointers) to LIGHTFUNCs
    lua_pushglobaltable(L);
    total += mac_convert_cfunc_in_table(L, sys, "_G", lua_gettop(L));
    lua_pop(L, 1);

    // 3. Replace LJLIB_ASM string functions
    lua_getglobal(L, "string");
    if (lua_istable(L, -1)) {
        struct { const char* name; lua_CFunction fn; } strReplacements[] = {
            {"byte", mac_lf_string_byte},
            {"char", mac_lf_string_char},
            {"len", mac_lf_string_len},
            {"sub", mac_lf_string_sub},
            {"rep", mac_lf_string_rep},
            {"reverse", mac_lf_string_reverse},
            {"lower", mac_lf_string_lower},
            {"upper", mac_lf_string_upper},
        };
        for (auto& r : strReplacements) {
            lua_pushcfunction(L, r.fn);
            lua_setfield(L, -2, r.name);
            total++;
        }
        // Convert remaining LJLIB_CF string functions
        total += mac_convert_cfunc_in_table(L, sys, "string", lua_gettop(L));
    }
    lua_pop(L, 1);

    // 4. Replace LJLIB_ASM math functions
    lua_getglobal(L, "math");
    if (lua_istable(L, -1)) {
        struct { const char* name; lua_CFunction fn; } mathReplacements[] = {
            {"abs", mac_lf_math_abs}, {"floor", mac_lf_math_floor},
            {"ceil", mac_lf_math_ceil}, {"sqrt", mac_lf_math_sqrt},
            {"log", mac_lf_math_log}, {"log10", mac_lf_math_log10},
            {"exp", mac_lf_math_exp}, {"sin", mac_lf_math_sin},
            {"cos", mac_lf_math_cos}, {"tan", mac_lf_math_tan},
            {"asin", mac_lf_math_asin}, {"acos", mac_lf_math_acos},
            {"atan", mac_lf_math_atan}, {"atan2", mac_lf_math_atan2},
            {"sinh", mac_lf_math_sinh}, {"cosh", mac_lf_math_cosh},
            {"tanh", mac_lf_math_tanh}, {"pow", mac_lf_math_pow},
            {"fmod", mac_lf_math_fmod}, {"max", mac_lf_math_max},
            {"min", mac_lf_math_min}, {"frexp", mac_lf_math_frexp},
            {"ldexp", mac_lf_math_ldexp}, {"modf", mac_lf_math_modf},
            {"deg", mac_lf_math_deg}, {"rad", mac_lf_math_rad},
        };
        for (auto& r : mathReplacements) {
            lua_pushcfunction(L, r.fn);
            lua_setfield(L, -2, r.name);
            total++;
        }
        total += mac_convert_cfunc_in_table(L, sys, "math", lua_gettop(L));
    }
    lua_pop(L, 1);

    // 5. Convert LJLIB_CF functions in remaining library tables
    // Skip "io" — its functions use lj_lib_upvalue internally (not exposed via
    // lua_getupvalue) to set file-handle metatables; converting to LIGHTFUNC
    // strips those hidden upvalues, producing bare userdata from io.open.
    // Do not convert coroutine.* — yield/resume must stay raw LJLIB_CF (PLoad depends on yield). (#8)
    const char* libs[] = {"table", "os", "debug", nullptr};
    for (int i = 0; libs[i]; i++) {
        lua_getglobal(L, libs[i]);
        if (lua_istable(L, -1)) {
            if (strcmp(libs[i], "table") == 0) {
                lua_pushcfunction(L, mac_lf_table_concat);
                lua_setfield(L, -2, "concat");
                total++;
            }
            total += mac_convert_cfunc_in_table(L, sys, libs[i], lua_gettop(L));
        }
        lua_pop(L, 1);
    }

    // 6. Also update string metatable so s:method() calls use our replacements
    lua_pushliteral(L, "");
    if (lua_getmetatable(L, -1)) {
        lua_getglobal(L, "string");
        lua_setfield(L, -2, "__index");
        lua_pop(L, 1); // metatable
    }
    lua_pop(L, 1); // empty string

    sys->con->Printf("macOS arm64: replaced %d broken FFUNC builtins with LIGHTFUNCs\n", total);
}

static int s_macHelperCoRef = LUA_NOREF;
static bool s_macInPload = false;
static bool s_macServicingPloadQueue = false;
bool mac_is_in_pload() { return s_macInPload; }
void mac_set_in_pload(bool in_pload) { s_macInPload = in_pload; }
bool mac_is_servicing_pload_queue() { return s_macServicingPloadQueue; }
void mac_set_servicing_pload_queue(bool servicing) { s_macServicingPloadQueue = servicing; }
static ui_main_c* mac_get_ui(lua_State* L) {
    lua_geti(L, LUA_REGISTRYINDEX, ui_main_c::REGISTRY_KEY);
    ui_main_c* ui = (ui_main_c*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return ui;
}

void mac_sync_globals_from_helper_co(lua_State* L)
{
	if (s_macHelperCoRef == LUA_NOREF) {
		return;
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, s_macHelperCoRef);
	if (!lua_isthread(L, -1)) {
		lua_pop(L, 1);
		return;
	}
	lua_State* co = lua_tothread(L, -1);
	lua_pop(L, 1);
	static const char* const kSyncKeys[] = { "main", "launch", nullptr };
	for (int i = 0; kSyncKeys[i]; i++) {
		lua_getglobal(co, kSyncKeys[i]);
		if (!lua_isnil(co, -1)) {
			lua_xmove(co, L, 1);
			lua_setglobal(L, kSyncKeys[i]);
		} else {
			lua_pop(co, 1);
		}
	}
}

// Entry: L = [chunk, arg1..argN]. Run in fresh coroutine; store coroutine for
// mac_sync_globals_from_helper_co. Exit: [true, results...] or [false, errmsg]. (#8)
// Uses copy+xmove pattern from CallCallbackOnThread: push copies in reverse, xmove
// reverses them back to correct order on co. mac_lightfunc_pcall's plain xmove
// moves co_thread instead of chunk from direct C call sites.
int mac_pload_coroutine_call(lua_State* L, int extraArgs, MacPLoadCompileFunc compile_fn) {
    mac_restore_raw_coroutine_create(L);
    lua_State* co = lua_newthread(L);           // L = [chunk, args..., co_thread]
    for (int i = extraArgs; i >= 0; --i) {      // push copies in reverse
        lua_pushvalue(L, 1 + i);
    }
    lua_xmove(L, co, extraArgs + 1);            // xmove reversal → correct order on co
    // co_thread is at extraArgs+2 on L after xmove consumed the copies
    if (s_macHelperCoRef != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, s_macHelperCoRef);
    }
    lua_pushvalue(L, extraArgs + 2);
    s_macHelperCoRef = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_settop(L, 0);

    s_macInPload = true;
    lua_pushboolean(L, 1);
    lua_setglobal(L, "__mac_in_pload_flag");
    int status = lua_resume(co, nullptr, extraArgs);
    if (ui_main_c* ui = mac_get_ui(L)) {
        ui->sys->con->Printf("macOS: PLoad initial resume status=%d (YIELD=%d) coTop=%d\n",
            status, LUA_YIELD, lua_gettop(co));
    }
    while (status == LUA_YIELD) {
        if (ui_main_c* ui = mac_get_ui(L)) {
            ui->sys->con->Printf("macOS: PLoad yield handler coTop=%d\n", lua_gettop(co));
        }
        const int n = lua_gettop(co);
        if (n != 1 || !lua_istable(co, 1)) {
            lua_pushliteral(co, "unexpected yield in PLoadModule coroutine");
            status = LUA_ERRRUN;
            break;
        }
        lua_getfield(co, 1, "tag");
        const bool isLoadModuleYield = lua_isstring(co, -1) &&
            strcmp(lua_tostring(co, -1), "__mac_lm") == 0;
        lua_pop(co, 1);
        if (!isLoadModuleYield) {
            lua_pushliteral(co, "unexpected yield in PLoadModule coroutine");
            status = LUA_ERRRUN;
            break;
        }
        // Load the file on root L and pass the compiled chunk to co as the return
        // value of __mac_lm_yield_c. The Lua wrapper calls chunk(...) directly inside
        // co, so nested LoadModule calls (e.g. Data.lua → Data/Global) also yield
        // and are handled by this same loop — no nested l_LoadModule on root. (#8)
        lua_getfield(co, 1, "a1");
        const char* modName = lua_isstring(co, -1) ? lua_tostring(co, -1) : nullptr;
        if (!modName) {
            lua_pop(co, 1); // drop nil a1
            lua_pushliteral(co, "PLoadModule: missing module name in yield");
            status = LUA_ERRRUN;
            break;
        }
        ui_main_c* ui = mac_get_ui(L);
        if (!ui) {
            lua_pop(co, 1);
            lua_pushliteral(co, "PLoadModule: no ui context");
            status = LUA_ERRRUN;
            break;
        }
        ui->sys->con->Printf("macOS: PLoad servicing LoadModule %s\n", modName);
        auto fileName = std::filesystem::u8path(modName);
        if (!fileName.has_extension()) fileName.replace_extension(".lua");
        auto filePath = (ui->scriptPath / fileName).lexically_normal();
        auto fileStr = filePath.generic_u8string();
        lua_pop(co, 1); // drop a1
        lua_pop(co, 1); // drop yield table (co stack now empty)
        // Run module on root L (not co): nested LoadModule calls inside modules
        // use lua_pcall on root L — safe at any depth. Disable yield mode so
        // those nested calls take the direct __mac_loadmodule_c path. (#8)
        lua_pushboolean(L, 0);
        lua_setglobal(L, "__mac_in_pload_flag");
        ui->sys->SetWorkDir(ui->scriptPath);
        const int loadErr = luaL_loadfile(L, fileStr.c_str());
        ui->sys->SetWorkDir(ui->scriptWorkDir);
        if (loadErr != LUA_OK) {
            const char* loadErrMsg = lua_tostring(L, -1);
            ui->sys->con->Printf("macOS: PLoad load error: %s\n", loadErrMsg ? loadErrMsg : "?");
            lua_pop(L, 1);
            if (loadErrMsg) lua_pushstring(co, loadErrMsg);
            else lua_pushliteral(co, "PLoadModule: file load failed");
            status = LUA_ERRRUN;
            break;
        }
        const int callErr = lua_pcall(L, 0, 0, 0);
        if (callErr != LUA_OK) {
            const char* callErrMsg = lua_tostring(L, -1);
            ui->sys->con->Printf("macOS: PLoad module error: %s\n", callErrMsg ? callErrMsg : "?");
            lua_pop(L, 1);
            if (callErrMsg) lua_pushstring(co, callErrMsg);
            else lua_pushliteral(co, "PLoadModule: module execution failed");
            status = LUA_ERRRUN;
            break;
        }
        // Module populated globals on L (shared with co). Resume co with 0 values;
        // wrapLoadModule yields with no return — modules are called for side effects. (#8)
        lua_pushboolean(L, 1);
        lua_setglobal(L, "__mac_in_pload_flag");
        status = lua_resume(co, nullptr, 0);
        ui->sys->con->Printf("macOS: PLoad resume status=%d coTop=%d\n",
            status, lua_gettop(co));
    }
    s_macInPload = false;
    lua_pushboolean(L, 0);
    lua_setglobal(L, "__mac_in_pload_flag");
    if (status == 0) {
        if (ui_main_c* ui = mac_get_ui(L)) {
            ui->sys->con->Printf("macOS: PLoad coroutine finished OK (coTop=%d)\n", lua_gettop(co));
        }
        lua_pushboolean(L, 1);
        // Do not xmove return values from co (GC64); use mac_sync_globals_from_helper_co.
        return 1;
    }
    lua_pushboolean(L, 0);
    if (lua_gettop(co) > 0 && lua_isstring(co, -1)) {
        lua_pushstring(L, lua_tostring(co, -1));
    } else {
        lua_pushliteral(L, "PLoadModule: unknown error");
    }
    return 2;
}

// Entry: L = [func, arg1, ..., argN]. Exit: [true, ...] or [false, err].
int mac_lightfunc_pcall(lua_State* L, int nargs) {
    mac_restore_raw_coroutine_create(L);
    lua_State* co = lua_newthread(L);
    lua_xmove(L, co, nargs + 1);
    const int status = lua_resume(co, nullptr, nargs);
    if (status == 0) {
        const int nres = lua_gettop(co);
        if (s_macHelperCoRef != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, s_macHelperCoRef);
        }
        lua_pushvalue(L, 1); // helper thread object left on root stack by lua_newthread
        s_macHelperCoRef = luaL_ref(L, LUA_REGISTRYINDEX);
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

static int mac_run_chunk_result_table(lua_State* L, bool ok, int firstResultIndex) {
    if (!ok) {
        lua_createtable(L, 0, 2);
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "ok");
        if (lua_gettop(L) >= firstResultIndex && lua_isstring(L, firstResultIndex)) {
            lua_pushvalue(L, firstResultIndex);
        } else {
            lua_pushliteral(L, "unknown error");
        }
        lua_setfield(L, -2, "err");
        lua_settop(L, 1);
        return 1;
    }
    const int n = lua_gettop(L) - firstResultIndex + 1;
    lua_createtable(L, n, 2);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "ok");
    for (int i = 0; i < n; i++) {
        lua_pushvalue(L, firstResultIndex + i);
        lua_rawseti(L, -2, i + 1);
    }
    lua_pushinteger(L, n);
    lua_setfield(L, -2, "n");
    lua_replace(L, 1);
    lua_settop(L, 1);
    return 1;
}

// Stack: [func, arg1..argN] -> { ok=true, n=N, [1..N]=... } or { ok=false, err=... }.
// Fast path: lua_pcall on main state (no select('#', ...) in Lua). (#8)
static int l_mac_call_chunk(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    const int nargs = lua_gettop(L) - 1;
    const int err = lua_pcall(L, nargs, LUA_MULTRET, 0);
    if (err != LUA_OK) {
        return mac_run_chunk_result_table(L, false, 1);
    }
    return mac_run_chunk_result_table(L, true, 1);
}

// Stack: [func, arg1..argN] -> { ok=true, n=N, [1..N]=... } or { ok=false, err=... }.
// Always uses mac_lightfunc_pcall (restore raw coroutine.create before lua_newthread). (#8)
static int l_mac_run_chunk(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    const int nargs = lua_gettop(L) - 1;
    const int pret = mac_lightfunc_pcall(L, nargs);
    if (!lua_toboolean(L, 1)) {
        return mac_run_chunk_result_table(L, false, 2);
    }
    lua_remove(L, 1);
    return mac_run_chunk_result_table(L, true, 1);
}

static int l_mac_in_pload(lua_State* L) {
    lua_pushboolean(L, s_macInPload);
    return 1;
}

// Yield to mac_pload_coroutine_call with one table arg (request). (#8)
static int l_mac_lm_yield(lua_State* L) {
    return lua_yield(L, 1);
}

static int l_mac_setmetatable(lua_State* L) {
    luaL_checkany(L, 1);
    luaL_checkany(L, 2);
    lua_settop(L, 2);
    if (!lua_setmetatable(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_settop(L, 1);
    return 1;
}

static int l_mac_pcall(lua_State* L) {
    const int n = lua_gettop(L);
    if (n < 1) {
        luaL_error(L, "bad argument #1 to 'pcall' (value expected)");
    }
    // Use lua_pcall instead of mac_lightfunc_pcall: lua_resume hangs
    // when called from inside a lua_pcall-protected frame on arm64 GC64. (#8)
    const int status = lua_pcall(L, n - 1, LUA_MULTRET, 0);
    lua_pushboolean(L, status == LUA_OK);
    lua_insert(L, 1);
    return lua_gettop(L);
}

#if __APPLE__ && __MACH__
// Prerequire(name) -> status, lib — avoids pcall(require, ...) on arm64 GC64. (#8)
static int l_mac_prerequire(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    lua_settop(L, 1);
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "loaded");
    lua_getfield(L, -1, name);
    if (!lua_isnil(L, -1)) {
        lua_pushboolean(L, 1);
        lua_insert(L, -2);
        lua_settop(L, 2);
        return 2;
    }
    lua_settop(L, 1);
    lua_getglobal(L, "require");
    lua_pushvalue(L, 1);
    const int status = lua_pcall(L, 1, 1, 0);
    if (status != LUA_OK) {
        lua_settop(L, 0);
        lua_pushboolean(L, 0);
        lua_pushnil(L);
        return 2;
    }
    lua_pushboolean(L, 1);
    lua_insert(L, 1);
    return 2;
}
#endif

static int l_mac_xpcall(lua_State* L) {
    const int n = lua_gettop(L);
    if (n < 2) {
        luaL_error(L, "bad argument #2 to 'xpcall' (value expected)");
    }
    // xpcall(f, msgh, ...) → status, ... using lua_pcall with error handler. (#8)
    lua_pushvalue(L, 2); // error handler
    lua_insert(L, 1);   // [msgh, f, msgh_orig, arg1, ...]
    lua_remove(L, 3);   // [msgh, f, arg1, ...]
    const int nargs = n - 2;
    const int status = lua_pcall(L, nargs, LUA_MULTRET, 1);
    lua_remove(L, 1); // remove error handler
    lua_pushboolean(L, status == LUA_OK);
    lua_insert(L, 1);
    return lua_gettop(L);
}

// loadfile + lua_pcall on the main state, then package.loaded[modname] = result. (#8)
static bool mac_preload_lua_file(lua_State* L, sys_IMain* sys, char const* modname,
                                 std::filesystem::path const& file) {
    if (!std::filesystem::exists(file)) {
        sys->con->Printf("Warning: macOS preload %s: file not found: %s\n", modname,
                         file.generic_u8string().c_str());
        return false;
    }
    const auto path = file.generic_u8string();
    if (luaL_loadfile(L, path.c_str()) != LUA_OK) {
        sys->con->Printf("Warning: macOS preload %s: load failed: %s\n", modname,
                         lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        sys->con->Printf("Warning: macOS preload %s: run failed: %s\n", modname,
                         lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    // stack: [1]=result, [2]=package, [3]=loaded
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "loaded");
    lua_pushvalue(L, -3);
    lua_setfield(L, -2, modname);
    lua_pop(L, 3);
    return true;
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

static int l_mac_coroutine_create(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_getfield(L, LUA_REGISTRYINDEX, "mac_co_create");
    lua_insert(L, 1);
    mac_resume_call(L, 1, 1);
    return 1;
}

// loadfile() from Lua bytecode crashes after RenderInit on arm64 GC64 (same as setmetatable). (#8)
static int l_mac_loadfile(lua_State* L) {
    lua_geti(L, LUA_REGISTRYINDEX, ui_main_c::REGISTRY_KEY);
    auto* ui = static_cast<ui_main_c*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    const char* path = luaL_checkstring(L, 1);
    auto filePath = std::filesystem::u8path(path);
    if (!filePath.is_absolute()) {
        filePath = (ui->scriptWorkDir / filePath).lexically_normal();
    }
    const auto pathStr = filePath.generic_u8string();
    ui->sys->SetWorkDir(ui->scriptPath);
    const int err = luaL_loadfile(L, pathStr.c_str());
    ui->sys->SetWorkDir(ui->scriptWorkDir);
    if (err != LUA_OK) {
        return 1;
    }
    return 1;
}

// Stash loadfile result for MacLoadfile() Lua wrapper (bytecode cannot capture C returns). (#8)
static int l_mac_loadfile_stash(lua_State* L) {
    const int err = l_mac_loadfile(L);
    if (err == 1 && lua_isfunction(L, -1)) {
        lua_setglobal(L, "__mac_loadfile_chunk");
        lua_pushnil(L);
        lua_setglobal(L, "__mac_loadfile_err");
        return 0;
    }
    if (err == 1) {
        lua_setglobal(L, "__mac_loadfile_err");
    } else {
        lua_pushliteral(L, "loadfile failed");
        lua_setglobal(L, "__mac_loadfile_err");
    }
    lua_pushnil(L);
    lua_setglobal(L, "__mac_loadfile_chunk");
    return 0;
}

// Load and run a file; stash {ok,err,...} for MacDofile() Lua wrapper. (#8)
static int l_mac_dofile(lua_State* L) {
    const int err = l_mac_loadfile(L);
    if (err != 1 || !lua_isfunction(L, -1)) {
        if (lua_gettop(L) < 1 || !lua_isstring(L, -1)) {
            lua_settop(L, 0);
            lua_pushliteral(L, "loadfile failed");
        }
        mac_run_chunk_result_table(L, false, 1);
        lua_setglobal(L, "__mac_dofile_result");
        return 0;
    }
    const int perr = lua_pcall(L, 0, 0, 0);
    if (perr != LUA_OK) {
        mac_run_chunk_result_table(L, false, 1);
        lua_setglobal(L, "__mac_dofile_result");
        return 0;
    }
    mac_run_chunk_result_table(L, true, lua_gettop(L) + 1);
    lua_setglobal(L, "__mac_dofile_result");
    return 0;
}

#if __APPLE__ && __MACH__
// __mac_loadfile_stash_c from Lua bytecode crashes arm64 GC64; call from C only. (#8)
static void mac_stash_loadfile(lua_State* L, ui_main_c* ui, char const* path) {
    lua_getglobal(L, "__mac_loadfile_stash_c");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    auto const filePath = (ui->scriptWorkDir / path).lexically_normal();
    lua_pushstring(L, filePath.generic_u8string().c_str());
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        ui->sys->con->Printf("mac_stash_loadfile(%s) failed: %s\n", path, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

static void mac_run_stashed_chunk(lua_State* L, ui_main_c* ui, char const* label) {
    lua_getglobal(L, "__mac_loadfile_chunk");
    if (!lua_isfunction(L, -1)) {
        lua_getglobal(L, "__mac_loadfile_err");
        ui->sys->con->Printf("%s load failed: %s\n", label, lua_tostring(L, -1));
        lua_settop(L, 0);
        return;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        ui->sys->con->Printf("%s failed: %s\n", label, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    lua_pushnil(L);
    lua_setglobal(L, "__mac_loadfile_chunk");
    lua_pushnil(L);
    lua_setglobal(L, "__mac_loadfile_err");
    lua_settop(L, 0);
}

// Call main:Init via C API (Init must be fetched from global main, not launch). (#8)
static void mac_invoke_main_init(lua_State* L, ui_main_c* ui) {
    lua_settop(L, 0);
    lua_getglobal(L, "main");
    if (lua_isnil(L, -1)) {
        ui->sys->con->Printf("main.Init: global main is nil after PLoadModule\n");
        lua_settop(L, 0);
        return;
    }
    lua_getglobal(L, "launch");
    if (lua_istable(L, -1)) {
        lua_pushvalue(L, -2);
        lua_setfield(L, -2, "main");
        lua_pop(L, 1);
    } else {
        lua_pop(L, 1);
    }
    lua_getfield(L, -1, "Init");
    if (!lua_isfunction(L, -1)) {
        ui->sys->con->Printf("main.Init: Init is %s\n", luaL_typename(L, -1));
        lua_settop(L, 0);
        return;
    }
    lua_pushvalue(L, -2); // push main as self argument
    ui->sys->con->Printf("macOS: calling main.Init...\n");
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        ui->sys->con->Printf("main.Init failed: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    } else {
        ui->sys->con->Printf("macOS: main.Init OK\n");
    }
    lua_settop(L, 0);
}

// Run LaunchAfterMain.lua from C after PLoadModule (split from OnInit on arm64 GC64). (#8)
static void mac_run_after_main_if_requested(lua_State* L, ui_main_c* ui) {
    mac_ensure_global_launch(L);
    lua_getglobal(L, "launch");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    lua_pop(L, 1);

    // Set platform flag so Lua side can skip unsupported features (e.g. update check). (#8)
    lua_getglobal(L, "launch");
    if (lua_istable(L, -1)) {
        lua_pushboolean(L, 1);
        lua_setfield(L, -2, "_isMacOS");
    }
    lua_pop(L, 1);

    mac_stash_loadfile(L, ui, "LaunchCallbacks.lua");
    lua_getglobal(L, "launch");
    if (!lua_istable(L, -1)) {
        ui->sys->con->Printf("macOS: global launch missing before LaunchCallbacks\n");
        lua_settop(L, 0);
        return;
    }
    lua_pop(L, 1);
    mac_run_stashed_chunk(L, ui, "LaunchCallbacks.lua");
    lua_getglobal(L, "launch");
    lua_getfield(L, -1, "_finishAfterMain");
    if (!lua_isfunction(L, -1)) {
        ui->sys->con->Printf("macOS: launch._finishAfterMain missing after LaunchCallbacks\n");
        lua_settop(L, 0);
        return;
    }
    lua_pop(L, 2);

    ui->sys->con->Printf("macOS: PLoadModule Modules/Main from C...\n");
    lua_getglobal(L, "__mac_loadmodule_c");
    const int lmIsC = lua_iscfunction(L, -1) ? 1 : 0;
    ui->sys->con->Printf("macOS: __mac_loadmodule_c type=%s isc=%d\n", luaL_typename(L, -1), lmIsC);
    lua_pop(L, 1);
    lua_getglobal(L, "LoadModule");
    ui->sys->con->Printf("macOS: LoadModule type=%s isc=%d\n", luaL_typename(L, -1), lua_iscfunction(L, -1) ? 1 : 0);
    lua_pop(L, 1);
    if (!mac_pload_module_pcall(L, "Modules/Main")) {
        const char* err = lua_tostring(L, -1);
        ui->sys->con->Printf("PLoadModule failed: %s\n", err ? err : "(no message)");
        lua_settop(L, 0);
        return;
    }
    ui->sys->con->Printf("macOS: PLoadModule done (C path)\n");
    mac_sync_globals_from_helper_co(L);  // copy main/launch from coroutine _G to root _G (#8)
    lua_getglobal(L, "main");
    ui->sys->con->Printf("macOS: global main after plm is %s\n", luaL_typename(L, -1));
    lua_settop(L, 0);
    mac_ensure_global_launch(L);
    ui->sys->con->Printf("macOS: global launch synced\n");

    ui->sys->con->Printf("macOS: running LaunchAfterMain (_finishAfterMain)...\n");
    lua_getglobal(L, "launch");
    lua_getfield(L, -1, "_finishAfterMain");
    if (!lua_isfunction(L, -1)) {
        ui->sys->con->Printf("macOS: launch._finishAfterMain is not a function\n");
        lua_settop(L, 0);
        return;
    }
    lua_insert(L, -2);
    int errfunc = 0;
    lua_getfield(L, LUA_REGISTRYINDEX, "traceback");
    if (lua_isfunction(L, -1)) {
        lua_insert(L, 1);
        errfunc = 1;
    } else {
        lua_pop(L, 1);
    }
    if (lua_pcall(L, 1, 0, errfunc) != LUA_OK) {
        ui->sys->con->Printf("LaunchAfterMain failed: %s\n", lua_tostring(L, -1));
        lua_settop(L, 0);
        return;
    }
    lua_settop(L, 0);
    mac_ensure_global_launch(L);
    ui->sys->con->Printf("macOS: calling main.Init from C...\n");
    mac_invoke_main_init(L, ui);
    ui->sys->con->Printf("macOS: post main.Init\n");

    // Parse manifest.xml for version info after main.Init (arm64 GC64 post-init). (#8)
    static char const* const kManifestVersion =
        "do\n"
        "  require('xml')\n"
        "  local xml = package.loaded['xml']\n"
        "  if xml then\n"
        "    local man = xml.LoadXMLFile('manifest.xml') or xml.LoadXMLFile('../manifest.xml')\n"
        "    if man and man[1] and man[1].elem == 'PoBVersion' then\n"
        "      for i = 1, #man[1] do\n"
        "        local node = man[1][i]\n"
        "        if node and node.elem == 'Version' then\n"
        "          launch.versionNumber = node.attrib.number\n"
        "          launch.versionBranch = node.attrib.branch\n"
        "          launch.versionPlatform = node.attrib.platform\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "end\n";
    if (luaL_dostring(L, kManifestVersion) != LUA_OK) {
        ui->sys->con->Printf("macOS: manifest version parse failed: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    lua_getglobal(L, "launch");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "versionNumber");
        lua_getfield(L, -2, "versionBranch");
        ui->sys->con->Printf("macOS: version=%s branch=%s\n",
            lua_tostring(L, -2) ? lua_tostring(L, -2) : "(nil)",
            lua_tostring(L, -1) ? lua_tostring(L, -1) : "(nil)");
        lua_pop(L, 3);
    } else {
        lua_pop(L, 1);
    }
}
#endif

// Light C function replacement for LuaJIT built-in require().
// LuaJIT 2.1 arm64 interpreter crashes when Lua bytecode calls any GC C closure
// via GGET+CALL (e.g. require, which has upvalues → allocated as CClosure).
// Replacing it with a LIGHTFUNC (no GC allocation) and calling all loaders via
// the C API avoids the broken interpreter path entirely. (#8)
static int mac_require_return_loaded(lua_State* L, int loadedIdx, const char* modname) {
    // Stack has [modname, package, loaded, preload, ...]. Leave only the module at index 1.
    lua_getfield(L, loadedIdx, modname);
    lua_replace(L, 1);
    lua_settop(L, 1);
    return 1;
}

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
    const int loadedIdx = 3;

    // 1. Return immediately if already loaded.
    lua_getfield(L, loadedIdx, modname);
    if (!lua_isnil(L, -1)) {
        lua_replace(L, 1);
        lua_settop(L, 1);
        return 1;
    }
    lua_pop(L, 1);

    // 2. Call package.preload[modname] if present.
    lua_getfield(L, 4, modname);
    if (lua_isfunction(L, -1)) {
        lua_pushvalue(L, 1);            // pass modname as arg
        if (lua_pcall(L, 1, 1, 0) != LUA_OK) return lua_error(L);
        if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushboolean(L, 1); }
        lua_pushvalue(L, -1);
        lua_setfield(L, loadedIdx, modname);   // package.loaded[modname] = result
        return mac_require_return_loaded(L, loadedIdx, modname);
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
            // sha1/init.lua calls require() from bytecode; run it on a pcall coroutine. (#8)
            const bool sha1Tree = (modname[0] == 's' && modname[1] == 'h' && modname[2] == 'a'
                                   && modname[3] == '1'
                                   && (modname[4] == '\0' || modname[4] == '.'));
            {
                if (lua_pcall(L, 0, 1, 0) != LUA_OK) return lua_error(L);
                if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushboolean(L, 1); }
                lua_pushvalue(L, -1);
            }
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                lua_pushboolean(L, 1);
            }
            lua_setfield(L, loadedIdx, modname);
            return mac_require_return_loaded(L, loadedIdx, modname);
        }
        tried += "\n\tno file '"; tried += tmpl; tried += "'";
        lua_pop(L, 1);  // pop load error
    }

    // 4. Search package.cpath for native C modules (.so / .dylib).
    lua_getfield(L, 2, "cpath");
    const char* cpathcstr = lua_tostring(L, -1);
    std::string cpath(cpathcstr ? cpathcstr : "");
    lua_pop(L, 1);

    pos = 0;
    while (pos <= cpath.size()) {
        size_t semi = cpath.find(';', pos);
        if (semi == std::string::npos) semi = cpath.size();
        std::string tmpl = cpath.substr(pos, semi - pos);
        pos = semi + 1;
        if (tmpl.empty()) continue;
        size_t q = tmpl.find('?');
        if (q != std::string::npos) tmpl.replace(q, 1, modpath);
        void* lib = dlopen(tmpl.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (lib) {
            std::string funcname = "luaopen_";
            for (const char* p = modname; *p; p++) {
                funcname += (*p == '.' ? '_' : *p);
            }
            using opener_t = int (*)(lua_State*);
            auto opener = (opener_t)dlsym(lib, funcname.c_str());
            if (!opener) {
                dlclose(lib);
                luaL_error(L, "module '%s': found '%s' but no '%s' symbol", modname, tmpl.c_str(), funcname.c_str());
            }
            lua_pushcfunction(L, opener);
            lua_pushstring(L, modname);
            if (lua_pcall(L, 1, 1, 0) != LUA_OK) return lua_error(L);
            if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushboolean(L, 1); }
            lua_pushvalue(L, -1);
            lua_setfield(L, loadedIdx, modname);
            return mac_require_return_loaded(L, loadedIdx, modname);
        }
        tried += "\n\tno C file '"; tried += tmpl; tried += "'";
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
	// Same as mac_lightfunc_pcall: restore raw coroutine.create, resume [func, args] only. (#8)
	mac_restore_raw_coroutine_create(L);
	const int funcIdx = lua_gettop(L) - extraArgs;
	lua_State* co = lua_newthread(L);
	for (int i = extraArgs; i >= 0; --i) {
		lua_pushvalue(L, funcIdx + i);
	}
	lua_xmove(L, co, extraArgs + 1);
	lua_pop(L, 1);
	const int err = lua_resume(co, nullptr, extraArgs);
	if (err != 0 && err != LUA_YIELD && !didExit) {
		const char* msg = lua_tostring(co, -1);
		sys->con->Printf("Lua callback error: %s\n", msg ? msg : "unknown");
		DoError("Runtime error in", msg ? msg : "unknown");
	}
	lua_settop(L, 0);
}
#endif

void ui_main_c::PCall(int narg, int nret)
{
	sys->SetWorkDir(scriptWorkDir);
	inLua = true;
	hasActiveCoroutine = false;
	int err = lua_pcall(L, narg, nret, 1);
	if (err == 0) {
#if !(__APPLE__ && __MACH__)
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
#endif
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
#if __APPLE__ && __MACH__
	// Use lua_newstate with our custom allocator so ALL allocations — including
	// the initial lua_State itself — use the GC64 4GB-boundary-safe scheme.
	// Cannot use sol::state here because luaL_newstate (inside sol) uses the
	// default allocator; installing it later via lua_setallocf would leave the
	// initial objects (string table, global_State, etc.) with the wrong header,
	// causing heap corruption on first realloc. solState remains empty on macOS
	// so ScriptShutdown closes L directly via lua_close. (#8)
	L = lua_newstate(mac_gc64_alloc, nullptr);
	if (!L) sys->Error("Error: unable to create Lua state.");
	sys->con->Printf("macOS: GC64 4GB-boundary allocator installed (#8)\n");
	// luaJIT_setmode(MODE_OFF) + FLUSH breaks interpreter on arm64 GC64
	// (function return capture hangs after FFUNC replacements fix that).
	// Instead, call jit.off() from Lua AFTER replacing FFUNCs, to disable
	// new trace compilation without corrupting the interpreter state. (#8)
	sys->con->Printf("macOS arm64: JIT off deferred to after FFUNC replacement\n");
#else
	solState.emplace();
	L = solState->lua_state();
	if ( !L ) sys->Error("Error: unable to create Lua state.");
#endif
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
	// LuaJIT arm64: LJLIB_ASM fast functions (tostring, tonumber, etc.) have
	// broken assembly dispatch that hangs. LJLIB_CF functions (with valid C
	// pointer from lua_tocfunction) work fine. Replace all LJLIB_ASM builtins
	// with LIGHTFUNC equivalents. (#8)
	mac_replace_broken_ffuncs(L, sys);
	// Now disable JIT compilation to prevent traces from hitting remaining
	// unreplaced FFUNCs. Use jit.off() Lua API (not C luaJIT_setmode which
	// breaks interpreter state). (#8)
	{
		static char const* const kJitOff =
			"if jit and jit.off then jit.off() end";
		if (luaL_dostring(L, kJitOff) != LUA_OK) {
			sys->con->Printf("Warning: jit.off() failed: %s\n", lua_tostring(L, -1));
			lua_pop(L, 1);
		} else {
			sys->con->Printf("macOS arm64: JIT compilation disabled via jit.off()\n");
		}
	}
	// Launch.lua calls jit.opt.start at top level; stub it so it's harmless. (#8)
	static char const* const kMacJit =
		"if jit then "
		"jit.opt.start = function(...) end "
		"end";
	if (luaL_dostring(L, kMacJit) != LUA_OK) {
		sys->con->Printf("Warning: macOS JIT stub failed: %s\n", lua_tostring(L, -1));
		lua_pop(L, 1);
	} else {
		sys->con->Printf("macOS: jit.opt.start stubbed (JIT engine left enabled).\n");
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

	// Optional dev dependency; avoid broken require path during Common.lua prerequire(). (#8)
	{
		lua_getglobal(L, "package");
		lua_getfield(L, -1, "loaded");
		lua_newtable(L);
		lua_setfield(L, -2, "lua-profiler");
		lua_pop(L, 2);
	}

	// sha1.common has no require(); safe before LIGHTFUNC install. (#8)
	{
		auto const commonLua =
		    (sys->basePath / ".." / "runtime" / "lua" / "sha1" / "common.lua").lexically_normal();
		if (!mac_preload_lua_file(L, sys, "sha1.common", commonLua)) {
			sys->con->Printf("Warning: macOS sha1.common preload failed.\n");
		}
	}

	// Replace built-ins that are GC closures (or use broken fast-paths) in arm64 GC64.
	lua_pushcfunction(L, l_mac_require); lua_setglobal(L, "require");
	lua_pushcfunction(L, l_mac_loadfile);
	lua_setglobal(L, "loadfile");
	lua_getglobal(L, "loadfile");
	sys->con->Printf("macOS: loadfile global is %s after install\n", luaL_typename(L, -1));
	lua_pop(L, 1);
	lua_pushcfunction(L, l_mac_loadfile_stash);
	lua_setglobal(L, "__mac_loadfile_stash_c");
	lua_pushcfunction(L, l_mac_dofile);
	lua_setglobal(L, "__mac_dofile_c");
	lua_pushcfunction(L, l_mac_pcall);   lua_setglobal(L, "pcall");
	lua_pushcfunction(L, l_mac_xpcall);  lua_setglobal(L, "xpcall");
	lua_pushcfunction(L, l_mac_run_chunk); lua_setglobal(L, "__mac_run_chunk");
	lua_pushcfunction(L, l_mac_call_chunk); lua_setglobal(L, "__mac_call_chunk");
	lua_pushcfunction(L, l_mac_in_pload); lua_setglobal(L, "__mac_in_pload_c");
	lua_pushcfunction(L, l_mac_lm_yield); lua_setglobal(L, "__mac_lm_yield_c");
	lua_pushboolean(L, 0);
	lua_setglobal(L, "__mac_in_pload_flag");
	lua_pushboolean(L, 0);
	lua_setglobal(L, "__mac_servicing_lm_flag");
	lua_pushcfunction(L, l_mac_restore_co); lua_setglobal(L, "__mac_restore_co");
	lua_pushcfunction(L, l_mac_prerequire); lua_setglobal(L, "MacPrerequire");
	sys->con->Printf("macOS: require/loadfile/pcall/xpcall replaced for GC64.\n");

	static char const* const kMacLoadfileWrap =
	    "function MacLoadfile(path)\n"
	    "  __mac_loadfile_stash_c(path)\n"
	    "  local c = __mac_loadfile_chunk\n"
	    "  local e = __mac_loadfile_err\n"
	    "  __mac_loadfile_chunk = nil\n"
	    "  __mac_loadfile_err = nil\n"
	    "  return c, e\n"
	    "end\n"
	    "function MacDofile(path)\n"
	    "  __mac_dofile_c(path)\n"
	    "  local r = __mac_dofile_result\n"
	    "  __mac_dofile_result = nil\n"
	    "  return r\n"
	    "end\n";
	if (luaL_dostring(L, kMacLoadfileWrap) != LUA_OK) {
		sys->con->Printf("Warning: macOS MacLoadfile/MacDofile wrap failed: %s\n",
		                  lua_tostring(L, -1));
		lua_pop(L, 1);
	}

	// Re-return C API results from Lua functions (bytecode cannot capture C returns on arm64 GC64). (#8)
	static char const* const kMacReturnApiWraps =
	    "do\n"
	    "  local function wrap1(c, g, stash)\n"
	    "    _G[c] = _G[g]\n"
	    "    _G[g] = function(...)\n"
	    "      _G[c](...)\n"
	    "      local r = _G[stash]\n"
	    "      _G[stash] = nil\n"
	    "      return r\n"
	    "    end\n"
	    "  end\n"
	    "  local function wrap2(c, g, stash)\n"
	    "    _G[c] = _G[g]\n"
	    "    _G[g] = function(...)\n"
	    "      _G[c](...)\n"
	    "      local r = _G[stash]\n"
	    "      _G[stash] = nil\n"
	    "      return r[1], r[2]\n"
	    "    end\n"
	    "  end\n"
	    "  local function wrapPCall(c, g, stash)\n"
	    "    _G[c] = _G[g]\n"
	    "    _G[g] = function(...)\n"
	    "      _G[c](...)\n"
	    "      local r = _G[stash]\n"
	    "      _G[stash] = nil\n"
	    "      return r[1], r[2], r[3], r[4], r[5]\n"
	    "    end\n"
	    "  end\n"
	    "  wrap1('__mac_gettime_c', 'GetTime', '__mac_api_result')\n"
	    "  wrap2('__mac_getscreensize_c', 'GetScreenSize', '__mac_api_result')\n"
	    "  wrap1('__mac_getscreenscale_c', 'GetScreenScale', '__mac_api_result')\n"
	    "  local function wrapLoadModule(c, g, stash)\n"
	    "    _G[c] = _G[g]\n"
	    "    _G[g] = function(name, ...)\n"
	    "      if __mac_in_pload_flag then\n"
	    "        -- yield: C runs module on root L, then resumes co; no return value\n"
	    "        __mac_lm_yield_c({ tag = \"__mac_lm\", a1 = name })\n"
	    "        return\n"
	    "      end\n"
	    "      _G[c](name, ...)\n"
	    "      local r = _G[stash]\n"
	    "      _G[stash] = nil\n"
	    "      if not r then return end\n"
	    "      local n = r.n or 0\n"
	    "      if n <= 0 then return end\n"
	    "      if n == 1 then return r.r1 end\n"
	    "      if n == 2 then return r.r1, r.r2 end\n"
	    "      return r.r1, r.r2, r.r3\n"
	    "    end\n"
	    "  end\n"
	    "  wrapLoadModule('__mac_loadmodule_c', 'LoadModule', '__mac_loadmodule_result')\n"
	    "  wrapPCall('__mac_pcall_c', 'PCall', '__mac_api_result')\n"
	    "end\n";
	if (luaL_dostring(L, kMacReturnApiWraps) != LUA_OK) {
		sys->con->Printf("Warning: macOS return API wraps failed: %s\n", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	lua_getglobal(L, "coroutine");
	lua_getfield(L, -1, "yield");
	sys->con->Printf("macOS: coroutine.yield is %s (isc=%d)\n", luaL_typename(L, -1),
	                 lua_iscfunction(L, -1) ? 1 : 0);
	lua_pop(L, 2);
	lua_getglobal(L, "LoadModule");
	sys->con->Printf("macOS: LoadModule is %s (isc=%d)\n", luaL_typename(L, -1),
	                 lua_iscfunction(L, -1) ? 1 : 0);
	lua_pop(L, 1);

	// bit.* in LuaJIT 2.1 are LJLIB_ASM (lib_bit.c confirms this) — assembly-dispatch
	// fastfunctions with a broken dispatch path on arm64 GC64. They SIGSEGV during
	// Data/Global.lua loading when the GC runs a finalizer that calls bit.* with a
	// cdata argument. Replace all with plain C (LIGHTFUNC) implementations.
	// 32-bit semantics only; sha2.lua int64 cdata branch is not exercised on macOS
	// because the update check is disabled (launch._isMacOS). (#8)
	{
		auto tobit = [](lua_State* L) -> int {
			lua_pushnumber(L, (int32_t)luaL_checknumber(L, 1));
			return 1;
		};
		auto bnot = [](lua_State* L) -> int {
			lua_pushnumber(L, (int32_t)(~(uint32_t)(int32_t)luaL_checknumber(L, 1)));
			return 1;
		};
		auto bor = [](lua_State* L) -> int {
			uint32_t r = (uint32_t)(int32_t)luaL_checknumber(L, 1);
			for (int i = 2, n = lua_gettop(L); i <= n; i++)
				r |= (uint32_t)(int32_t)luaL_checknumber(L, i);
			lua_pushnumber(L, (int32_t)r);
			return 1;
		};
		auto band = [](lua_State* L) -> int {
			uint32_t r = (uint32_t)(int32_t)luaL_checknumber(L, 1);
			for (int i = 2, n = lua_gettop(L); i <= n; i++)
				r &= (uint32_t)(int32_t)luaL_checknumber(L, i);
			lua_pushnumber(L, (int32_t)r);
			return 1;
		};
		auto bxor = [](lua_State* L) -> int {
			uint32_t r = (uint32_t)(int32_t)luaL_checknumber(L, 1);
			for (int i = 2, n = lua_gettop(L); i <= n; i++)
				r ^= (uint32_t)(int32_t)luaL_checknumber(L, i);
			lua_pushnumber(L, (int32_t)r);
			return 1;
		};
		auto lshift = [](lua_State* L) -> int {
			uint32_t v = (uint32_t)(int32_t)luaL_checknumber(L, 1);
			uint32_t n = (uint32_t)luaL_checknumber(L, 2) & 31;
			lua_pushnumber(L, (int32_t)(v << n));
			return 1;
		};
		auto rshift = [](lua_State* L) -> int {
			uint32_t v = (uint32_t)(int32_t)luaL_checknumber(L, 1);
			uint32_t n = (uint32_t)luaL_checknumber(L, 2) & 31;
			lua_pushnumber(L, (int32_t)(v >> n));
			return 1;
		};
		auto arshift = [](lua_State* L) -> int {
			int32_t v = (int32_t)luaL_checknumber(L, 1);
			uint32_t n = (uint32_t)luaL_checknumber(L, 2) & 31;
			lua_pushnumber(L, (int32_t)(v >> n));
			return 1;
		};
		auto rol = [](lua_State* L) -> int {
			uint32_t v = (uint32_t)(int32_t)luaL_checknumber(L, 1);
			uint32_t n = (uint32_t)luaL_checknumber(L, 2) & 31;
			lua_pushnumber(L, (int32_t)((v << n) | (v >> (32 - n))));
			return 1;
		};
		auto ror = [](lua_State* L) -> int {
			uint32_t v = (uint32_t)(int32_t)luaL_checknumber(L, 1);
			uint32_t n = (uint32_t)luaL_checknumber(L, 2) & 31;
			lua_pushnumber(L, (int32_t)((v >> n) | (v << (32 - n))));
			return 1;
		};
		auto bswap = [](lua_State* L) -> int {
			uint32_t v = (uint32_t)(int32_t)luaL_checknumber(L, 1);
			v = ((v & 0xFF000000) >> 24) | ((v & 0x00FF0000) >> 8) |
			    ((v & 0x0000FF00) << 8)  | ((v & 0x000000FF) << 24);
			lua_pushnumber(L, (int32_t)v);
			return 1;
		};
		struct { const char* name; lua_CFunction fn; } ops[] = {
			{"tobit", tobit}, {"bnot", bnot}, {"bor", bor}, {"band", band},
			{"bxor", bxor}, {"lshift", lshift}, {"rshift", rshift},
			{"arshift", arshift}, {"rol", rol}, {"ror", ror}, {"bswap", bswap},
		};
		lua_createtable(L, 0, 11);
		for (auto& op : ops) {
			lua_pushcfunction(L, op.fn);
			lua_setfield(L, -2, op.name);
		}
		// Install as bit global and in package.loaded so require('bit') returns it.
		lua_pushvalue(L, -1);
		lua_setglobal(L, "bit");
		lua_getglobal(L, "package");
		lua_getfield(L, -1, "loaded");
		lua_pushvalue(L, -3); // bit table
		lua_setfield(L, -2, "bit");
		lua_pop(L, 2); // loaded, package
		lua_pop(L, 1); // bit table
		sys->con->Printf("macOS: bit.* replaced with LIGHTFUNC (32-bit, arm64 GC64 safe).\n");
	}
	lua_pushcfunction(L, l_mac_setmetatable);
	lua_setglobal(L, "setmetatable");
	lua_getglobal(L, "coroutine");
	lua_getfield(L, -1, "create");
	lua_setfield(L, LUA_REGISTRYINDEX, "mac_co_create");
	lua_getfield(L, LUA_REGISTRYINDEX, "mac_co_create");
	lua_setglobal(L, "__mac_co_raw");
	lua_pushcfunction(L, l_mac_coroutine_create);
	lua_setglobal(L, "__mac_co_engine");
	lua_pushcfunction(L, l_mac_coroutine_create);
	lua_setfield(L, -2, "create");
	lua_pop(L, 1); // coroutine table

#endif

	// Setup debug system
	debug = ui_IDebug::GetHandle(this);

	// Setup subscript system
	subScriptSize = 16;
	subScriptList = new ui_ISubScript*[subScriptSize];
	for (dword i = 0; i < subScriptSize; i++) {
		subScriptList[i] = NULL;
	}
	
	// Load the script file (traceback handler may still be on the stack from InitAPI)
	lua_settop(L, 0);
	sys->SetWorkDir(scriptWorkDir);
	err = luaL_loadfile(L, scriptName.generic_u8string().c_str());
	if (err) {
		DoError("Error loading", lua_tostring(L, -1));
		return;
	}
	sys->SetWorkDir();

	// Run the script
#if __APPLE__ && __MACH__
	// Replace pairs/ipairs with LIGHTFUNC equivalents — the standard LuaJIT versions are
	// CClosures with upvalues, and CClosure calls hang on arm64 GC64. LIGHTFUNC pairs()
	// returns (next, t, nil). LIGHTFUNC ipairs() uses ipairs_aux from registry. (#8)
	lua_pushcfunction(L, [](lua_State* L) -> int {
		luaL_checktype(L, 1, LUA_TTABLE);
		lua_getglobal(L, "next");
		lua_pushvalue(L, 1);
		lua_pushnil(L);
		return 3;
	});
	lua_setglobal(L, "pairs");

	lua_pushcfunction(L, [](lua_State* L) -> int {
		lua_Integer i = luaL_checkinteger(L, 2) + 1;
		lua_pushinteger(L, i);
		lua_rawgeti(L, 1, i);
		return lua_isnil(L, -1) ? 0 : 2;
	});
	lua_setfield(L, LUA_REGISTRYINDEX, "mac_ipairs_aux");

	lua_pushcfunction(L, [](lua_State* L) -> int {
		luaL_checktype(L, 1, LUA_TTABLE);
		lua_getfield(L, LUA_REGISTRYINDEX, "mac_ipairs_aux");
		lua_pushvalue(L, 1);
		lua_pushinteger(L, 0);
		return 3;
	});
	lua_setglobal(L, "ipairs");
	// type() is also a CClosure in LuaJIT 2.1 — replace with LIGHTFUNC. (#8)
	lua_pushcfunction(L, [](lua_State* L) -> int {
		luaL_checkany(L, 1);
		lua_pushstring(L, luaL_typename(L, 1));
		return 1;
	});
	lua_setglobal(L, "type");
	sys->con->Printf("macOS: pairs/ipairs/type replaced with LIGHTFUNCs for GC64.\n");
	// Scan ALL globals and common library tables for remaining CClosures (#8)
	{
		auto checkCClosure = [](lua_State* L, ui_main_c* ui, const char* prefix, int tableIdx) {
			lua_pushnil(L);
			while (lua_next(L, tableIdx) != 0) {
				if (lua_type(L, -1) == LUA_TFUNCTION && lua_iscfunction(L, -1)) {
					const char* upname = lua_getupvalue(L, -1, 1);
					if (upname) {
						const char* keyName = lua_isstring(L, -3) ? lua_tostring(L, -3) : "?";
						ui->sys->con->Printf("WARNING: %s.%s is CClosure (upvalue '%s')\n",
							prefix, keyName, upname);
						lua_pop(L, 1); // pop upvalue
					}
				}
				lua_pop(L, 1); // pop value, keep key
			}
		};
		// Scan global table
		lua_pushglobaltable(L);
		checkCClosure(L, this, "_G", lua_gettop(L));
		lua_pop(L, 1);
		// Scan common library tables
		const char* libs[] = {"string", "table", "math", "io", "os", "debug", "coroutine", "bit", nullptr};
		for (int i = 0; libs[i]; i++) {
			lua_getglobal(L, libs[i]);
			if (lua_istable(L, -1)) {
				checkCClosure(L, this, libs[i], lua_gettop(L));
			}
			lua_pop(L, 1);
		}
	}
	// JIT left enabled — setmode OFF breaks arm64 GC64 interpreter (#8)
#endif
	sys->con->Printf("Running script...\n");
	lua_createtable(L, scriptArgc > 0 ? scriptArgc - 1 : 0, 1);
	for (int i = 0; i < scriptArgc; i++) {
		lua_pushstring(L, scriptArgv[i]);
		lua_rawseti(L, -2, i);
	}
	lua_setglobal(L, "arg");
	lua_settop(L, 1);
#if __APPLE__ && __MACH__
	if (!lua_isfunction(L, 1)) {
		sys->con->Printf("macOS: launch chunk is %s, expected function\n", luaL_typename(L, 1));
	} else {
		mac_restore_raw_coroutine_create(L);
		lua_State* co = lua_newthread(L);
		lua_pushvalue(L, 1);
		lua_xmove(L, co, 1);
		lua_pop(L, 1);
		const int status = lua_resume(co, nullptr, 0);
		if (status != 0 && status != LUA_YIELD) {
			const char* msg = lua_tostring(co, -1);
			sys->con->Printf("Launch.lua failed: %s\n", msg ? msg : "unknown error");
			lua_settop(L, 0);
		} else {
			mac_sync_main_object_from_co(L, co);
			lua_pop(L, 1);
			lua_settop(L, 0);
			sys->con->Printf("macOS: Launch.lua loaded OK\n");
		}
	}
	lua_settop(L, 0);
#else
	for (int i = 0; i < scriptArgc; i++) {
		lua_pushstring(L, scriptArgv[i]);
	}
	lua_getfield(L, LUA_REGISTRYINDEX, "traceback");
	lua_insert(L, 1);
	PCall(scriptArgc, 0);
#endif

	if ( !didExit && !restartFlag ) {
		// Run initialisation callback
		int extraArgs = PushCallback("OnInit");
		if (extraArgs >= 0) {
#if __APPLE__ && __MACH__
			if (lua_pcall(L, extraArgs, 0, 0) != LUA_OK) {
				sys->con->Printf("OnInit failed: %s\n", lua_tostring(L, -1));
				lua_settop(L, 0);
			} else {
				lua_settop(L, 0);
			}
			mac_run_after_main_if_requested(L, this);
#else
			PCall(extraArgs, 0);
#endif
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
#if __APPLE__ && __MACH__
	// solState is empty on macOS (we bypassed sol::state to install the custom
	// allocator via lua_newstate). Close the state directly. (#8)
	if (L && !solState.has_value()) lua_close(L);
#endif
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
