#include <lua.h>
#include <lauxlib.h>

#include <time.h>

#define DAY_SEC(x,z) (( x + z ) %  (24 * 3600 ))
#define ZERO_TIME(x,z) ( x - DAY_SEC(x,z) )


static int TimeZone = 8;

int gettimezone()
{
	time_t t;
	struct tm _ltm = { 0 };
	struct tm _gtm = { 0 };

	time(&t);
	
#ifdef _WIN32
	localtime_s(&_ltm, &t);
	gmtime_s(&_gtm, &t);
#else
	localtime_r(&t, &_ltm);
	gmtime_r(&t, &_gtm);
#endif

	int time_zone = _ltm.tm_hour - _gtm.tm_hour;
	if (time_zone < -12) {
		time_zone += 24;
	} else if (time_zone > 12) {
		time_zone -= 24;
	}
	return time_zone;
}

static int
getdaysec(lua_State *L) 
{
	int t = luaL_checkinteger(L,1);
	t = DAY_SEC(t, TimeZone);
	lua_pushinteger(L, t);
	return 1;
}

static int
getdayzero(lua_State *L) 
{
	int t = luaL_checkinteger(L,1);
	t = ZERO_TIME(t, TimeZone);
	lua_pushinteger(L, t);
	return 1;
}

static int
getnextweek(lua_State *L) 
{
	time_t tt = (time_t)luaL_checkinteger(L,1);
	struct tm _tm = { 0 };
	
#ifdef _WIN32
	localtime_s(&_tm, &tt);
#else
	localtime_r(&tt, &_tm);
#endif
	int d = (7 - ((_tm.tm_wday + 6) % 7));
	int t = (int)ZERO_TIME(tt, TimeZone) + 24 * 3600 * d;
	lua_pushinteger(L, t);
	return 1;
}

static int
getweekday(lua_State *L) 
{
	time_t t = time(0);
	struct tm _tm = { 0 };
#ifdef _WIN32
	localtime_s(&_tm, &t);
#else
	localtime_r(&t, &_tm);
#endif
	if( _tm.tm_wday == 0 ) 
		lua_pushinteger(L, 7);
	else
		lua_pushinteger(L, _tm.tm_wday);
	return 1;
}

static int
getuday(lua_State *L) 
{
	int t = luaL_checkinteger(L,1);
	t = (int)((t + TimeZone) / (24 * 3600));
	lua_pushinteger(L, t);
	return 1;
}

static int
getuweek(lua_State *L) 
{
	int t = luaL_checkinteger(L,1);
	t = (int)((((t + TimeZone) / (24 * 3600)) - 4) / 7);
	lua_pushinteger(L, t);
	return 1;
}

static int
getumonth(lua_State *L) 
{
	time_t t = luaL_checkinteger(L,1);

	struct tm _tm = { 0 };
	
#ifdef _WIN32
	localtime_s(&_tm, &t);
#else
	localtime_r(&t, &_tm);
#endif

	t = _tm.tm_year * 12 + _tm.tm_mon + 1;
	
	lua_pushinteger(L, (int)t);
	return 1;
}

int
luaopen_ltime(lua_State *L)
{
	TimeZone = gettimezone()*3600;
	
	luaL_Reg l[] = {
		{ "getdaysec", getdaysec },
		{ "getdayzero", getdayzero },
		{ "getnextweek", getnextweek },
		{ "getweekday", getweekday },
		{ "getuday", getuday },
		{ "getuweek", getuweek },
		{ "getumonth", getumonth },
		{ NULL,  NULL },
	};
	luaL_newlib(L,l);
	return 1;
}

