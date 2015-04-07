/*
** $Id: lmathlib.c,v 1.83 2013/03/07 18:21:32 roberto Exp $
** Standard mathematical library
** See Copyright Notice in lua.h
*/


#include <stdlib.h>
#include <math.h>

#define lmathlib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"


#undef PI
#define PI	((lua_Number)(3.1415926535897932384626433832795))
#define RADIANS_PER_DEGREE	((lua_Number)(PI/180.0))


/*返回x 的绝对值 */
static int math_abs (lua_State *L) {
  lua_pushnumber(L, l_mathop(fabs)(luaL_checknumber(L, 1)));
  return 1;
}
/* 返回x（单位为弧度）的正弦值*/
static int math_sin (lua_State *L) {
  lua_pushnumber(L, l_mathop(sin)(luaL_checknumber(L, 1)));
  return 1;
}
/*返回x 的双曲线正弦值 */
static int math_sinh (lua_State *L) {
  lua_pushnumber(L, l_mathop(sinh)(luaL_checknumber(L, 1)));
  return 1;
}
/*返回x（单位为弧度）的余弦值 */
static int math_cos (lua_State *L) {
  lua_pushnumber(L, l_mathop(cos)(luaL_checknumber(L, 1)));
  return 1;
}
/* 返回x 的双曲线余弦值*/
static int math_cosh (lua_State *L) {
  lua_pushnumber(L, l_mathop(cosh)(luaL_checknumber(L, 1)));
  return 1;
}
/*返回x（单位为弧度）的正切值 */
static int math_tan (lua_State *L) {
  lua_pushnumber(L, l_mathop(tan)(luaL_checknumber(L, 1)));
  return 1;
}
/*返回x 的双曲线正切值 */
static int math_tanh (lua_State *L) {
  lua_pushnumber(L, l_mathop(tanh)(luaL_checknumber(L, 1)));
  return 1;
}
/* 返回x（单位为弧度）的反正弦值*/
static int math_asin (lua_State *L) {
  lua_pushnumber(L, l_mathop(asin)(luaL_checknumber(L, 1)));
  return 1;
}
/*返回x（单位为弧度）的反余弦值 */
static int math_acos (lua_State *L) {
  lua_pushnumber(L, l_mathop(acos)(luaL_checknumber(L, 1)));
  return 1;
}
/* 返回x（单位为弧度）的反正切值*/
static int math_atan (lua_State *L) {
  lua_pushnumber(L, l_mathop(atan)(luaL_checknumber(L, 1)));
  return 1;
}
/* 利用两个参数的正负符号在对应象限中找到y/x（单位都为弧度）
的反正切值。（当x为0 时也可以正确处理。）*/
static int math_atan2 (lua_State *L) {
  lua_pushnumber(L, l_mathop(atan2)(luaL_checknumber(L, 1),
                                luaL_checknumber(L, 2)));
  return 1;
}
/* 返回大于或等于x 的最小整数*/
static int math_ceil (lua_State *L) {
  lua_pushnumber(L, l_mathop(ceil)(luaL_checknumber(L, 1)));
  return 1;
}
/* 返回小于或等于x 的最大整数*/
static int math_floor (lua_State *L) {
  lua_pushnumber(L, l_mathop(floor)(luaL_checknumber(L, 1)));
  return 1;
}
/*返回x 除以y 的余数 */
static int math_fmod (lua_State *L) {
  lua_pushnumber(L, l_mathop(fmod)(luaL_checknumber(L, 1),
                               luaL_checknumber(L, 2)));
  return 1;
}
/*返回x 的整数部分与小数部分 */
static int math_modf (lua_State *L) {
  lua_Number ip;
  lua_Number fp = l_mathop(modf)(luaL_checknumber(L, 1), &ip);
  lua_pushnumber(L, ip);
  lua_pushnumber(L, fp);
  return 2;
}

static int math_sqrt (lua_State *L) {
  lua_pushnumber(L, l_mathop(sqrt)(luaL_checknumber(L, 1)));
  return 1;
}
/* 返回x 的y 次方。（你也可以用表达式x^y 来计算该值。）*/
static int math_pow (lua_State *L) {
  lua_Number x = luaL_checknumber(L, 1);
  lua_Number y = luaL_checknumber(L, 2);
  lua_pushnumber(L, l_mathop(pow)(x, y));
  return 1;
}
/*返回以base 为底数的x 的对数。
base 的默认值为e（此时函数返回x 的自然对数） */
static int math_log (lua_State *L) {
  lua_Number x = luaL_checknumber(L, 1);
  lua_Number res;
  if (lua_isnoneornil(L, 2))
    res = l_mathop(log)(x);
  else {
    lua_Number base = luaL_checknumber(L, 2);
    if (base == (lua_Number)10.0) res = l_mathop(log10)(x);
    else res = l_mathop(log)(x)/l_mathop(log)(base);
  }
  lua_pushnumber(L, res);
  return 1;
}

#if defined(LUA_COMPAT_LOG10)
static int math_log10 (lua_State *L) {
  lua_pushnumber(L, l_mathop(log10)(luaL_checknumber(L, 1)));
  return 1;
}
#endif
/*返回ex，，即e 的x 次方 */
static int math_exp (lua_State *L) {
  lua_pushnumber(L, l_mathop(exp)(luaL_checknumber(L, 1)));
  return 1;
}
/*返回弧度x 对应的角度 */
static int math_deg (lua_State *L) {
  lua_pushnumber(L, luaL_checknumber(L, 1)/RADIANS_PER_DEGREE);
  return 1;
}
/*返回x（单位为角度）对应的弧度 */
static int math_rad (lua_State *L) {
  lua_pushnumber(L, luaL_checknumber(L, 1)*RADIANS_PER_DEGREE);
  return 1;
}
/*返回两个值m 与e，这两个值使x = m2^e 成立。
e 是一个整数，m 的绝对值在[0.5,1)之间（当x 为0 时，m 为0）。 */
static int math_frexp (lua_State *L) {
  int e;
  lua_pushnumber(L, l_mathop(frexp)(luaL_checknumber(L, 1), &e));
  lua_pushinteger(L, e);
  return 2;
}
/*返回m2^e 的值（e 应该是整数） */
static int math_ldexp (lua_State *L) {
  lua_Number x = luaL_checknumber(L, 1);
  int ep = luaL_checkint(L, 2);
  lua_pushnumber(L, l_mathop(ldexp)(x, ep));
  return 1;
}



static int math_min (lua_State *L) {
  int n = lua_gettop(L);  /* number of arguments */
  lua_Number dmin = luaL_checknumber(L, 1);
  int i;
  for (i=2; i<=n; i++) {
    lua_Number d = luaL_checknumber(L, i);
    if (d < dmin)
      dmin = d;
  }
  lua_pushnumber(L, dmin);
  return 1;
}


static int math_max (lua_State *L) {
  int n = lua_gettop(L);  /* number of arguments */
  lua_Number dmax = luaL_checknumber(L, 1);
  int i;
  for (i=2; i<=n; i++) {
    lua_Number d = luaL_checknumber(L, i);
    if (d > dmax)
      dmax = d;
  }
  lua_pushnumber(L, dmax);
  return 1;
}

/* 此函数是标准C 提供的简单伪随机函数rand 的一个接口。
（无法保证该函数的稳定值。）不传递任何参数去调用此函数时，
返回一个范围为[0,1)的实数。当只传递了参数m 时，
math.random 返回范围为[1,m]的整数。当m，n 参数都传递时，
math.random 返回范围为[m,n]的整数。*/
static int math_random (lua_State *L) {
  /* the `%' avoids the (rare) case of r==1, and is needed also because on
     some systems (SunOS!) `rand()' may return a value larger than RAND_MAX */
  lua_Number r = (lua_Number)(rand()%RAND_MAX) / (lua_Number)RAND_MAX;
  switch (lua_gettop(L)) {  /* check number of arguments */
    case 0: {  /* no arguments */
      lua_pushnumber(L, r);  /* Number between 0 and 1 */
      break;
    }
    case 1: {  /* only upper limit */
      lua_Number u = luaL_checknumber(L, 1);
      luaL_argcheck(L, (lua_Number)1.0 <= u, 1, "interval is empty");
      lua_pushnumber(L, l_mathop(floor)(r*u) + (lua_Number)(1.0));  /* [1, u] */
      break;
    }
    case 2: {  /* lower and upper limits */
      lua_Number l = luaL_checknumber(L, 1);
      lua_Number u = luaL_checknumber(L, 2);
      luaL_argcheck(L, l <= u, 2, "interval is empty");
      lua_pushnumber(L, l_mathop(floor)(r*(u-l+1)) + l);  /* [l, u] */
      break;
    }
    default: return luaL_error(L, "wrong number of arguments");
  }
  return 1;
}

/*将x 设为伪随机产生器的“种子”。相同的种子产生相同的数字序列 */
static int math_randomseed (lua_State *L) {
  srand(luaL_checkunsigned(L, 1));
  (void)rand(); /* discard first value to avoid undesirable correlations */
  return 0;
}


static const luaL_Reg mathlib[] = {
  {"abs",   math_abs},
  {"acos",  math_acos},
  {"asin",  math_asin},
  {"atan2", math_atan2},
  {"atan",  math_atan},
  {"ceil",  math_ceil},
  {"cosh",   math_cosh},
  {"cos",   math_cos},
  {"deg",   math_deg},
  {"exp",   math_exp},
  {"floor", math_floor},
  {"fmod",   math_fmod},
  {"frexp", math_frexp},
  {"ldexp", math_ldexp},
#if defined(LUA_COMPAT_LOG10)
  {"log10", math_log10},
#endif
  {"log",   math_log},
  {"max",   math_max},
  {"min",   math_min},
  {"modf",   math_modf},
  {"pow",   math_pow},
  {"rad",   math_rad},
  {"random",     math_random},
  {"randomseed", math_randomseed},
  {"sinh",   math_sinh},
  {"sin",   math_sin},
  {"sqrt",  math_sqrt},
  {"tanh",   math_tanh},
  {"tan",   math_tan},
  {NULL, NULL}
};


/*
** Open math library
数学函数注册
*/
LUAMOD_API int luaopen_math (lua_State *L) {
  luaL_newlib(L, mathlib);/*将数学函数注入table */
  lua_pushnumber(L, PI);
  lua_setfield(L, -2, "pi");
  lua_pushnumber(L, HUGE_VAL);
  lua_setfield(L, -2, "huge");
  return 1;
}

