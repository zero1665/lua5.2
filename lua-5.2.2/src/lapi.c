/*
** $Id: lapi.c,v 2.171 2013/03/16 21:10:18 roberto Exp $
** Lua API
** See Copyright Notice in lua.h
*/


#include <stdarg.h>
#include <string.h>

#define lapi_c
#define LUA_CORE

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lundump.h"
#include "lvm.h"



const char lua_ident[] =
  "$LuaVersion: " LUA_COPYRIGHT " $"
  "$LuaAuthors: " LUA_AUTHORS " $";


/* value at a non-valid index */
#define NONVALIDVALUE		cast(TValue *, luaO_nilobject)

/* corresponding test */
#define isvalid(o)	((o) != luaO_nilobject)

/* test for pseudo index */
#define ispseudo(i)		((i) <= LUA_REGISTRYINDEX)

/* test for valid but not pseudo index */
#define isstackindex(i, o)	(isvalid(o) && !ispseudo(i))

#define api_checkvalidindex(L, o)  api_check(L, isvalid(o), "invalid index")

#define api_checkstackindex(L, i, o)  \
	api_check(L, isstackindex(i, o), "index not in the stack")


static TValue *index2addr (lua_State *L, int idx) {
  CallInfo *ci = L->ci;
  if (idx > 0) {
    TValue *o = ci->func + idx;
    api_check(L, idx <= ci->top - (ci->func + 1), "unacceptable index");
    if (o >= L->top) return NONVALIDVALUE;
    else return o;
  }
  else if (!ispseudo(idx)) {  /* negative index */
    api_check(L, idx != 0 && -idx <= L->top - (ci->func + 1), "invalid index");
    return L->top + idx;
  }
  else if (idx == LUA_REGISTRYINDEX)
    return &G(L)->l_registry;
  else {  /* upvalues */
    idx = LUA_REGISTRYINDEX - idx;
    api_check(L, idx <= MAXUPVAL + 1, "upvalue index too large");
    if (ttislcf(ci->func))  /* light C function? */
      return NONVALIDVALUE;  /* it has no upvalues */
    else {
      CClosure *func = clCvalue(ci->func);
      return (idx <= func->nupvalues) ? &func->upvalue[idx-1] : NONVALIDVALUE;
    }
  }
}


/*
** to be called by 'lua_checkstack' in protected mode, to grow stack
** capturing memory errors
*/
static void growstack (lua_State *L, void *ud) {
  int size = *(int *)ud;
  luaD_growstack(L, size);
}

/*
确保栈中有足够的空闲空间。如果它因为请求空间过大（通常最至少支持好几千个元素）
或无法分配足够的内存，那么它就会返回false。该函数不会缩小栈空间。如果栈的大小已
经大于请求的大小，那么它会保持不变。

*/
LUA_API int lua_checkstack (lua_State *L, int size) {
  int res;
  CallInfo *ci = L->ci;
  lua_lock(L);
  if (L->stack_last - L->top > size)  /* stack large enough? */
    res = 1;  /* yes; check is OK */
  else {  /* no; need to grow stack */
    int inuse = cast_int(L->top - L->stack) + EXTRA_STACK;
    if (inuse > LUAI_MAXSTACK - size)  /* can grow without overflow? */
      res = 0;  /* no */
    else  /* try to grow stack */
      res = (luaD_rawrunprotected(L, &growstack, &size) == LUA_OK);
  }
  if (res && ci->top < L->top + size)
    ci->top = L->top + size;  /* adjust frame top */
  lua_unlock(L);
  return res;
}

/*
在同一个状态机中的不同线程之间交换值。
此函数将线程from 栈中的n 个值弹出并将它们压入线程to 的栈。
*/
LUA_API void lua_xmove (lua_State *from, lua_State *to, int n) {
  int i;
  if (from == to) return;
  lua_lock(to);
  api_checknelems(from, n);
  api_check(from, G(from) == G(to), "moving among independent states");
  api_check(from, to->ci->top - to->top >= n, "not enough elements to move");
  from->top -= n;
  for (i = 0; i < n; i++) {
    setobj2s(to, to->top++, from->top + i);
  }
  lua_unlock(to);
}
/* 设置一个新的恐慌函数并且返回旧的恐慌函数*/
LUA_API lua_CFunction lua_atpanic (lua_State *L, lua_CFunction panicf) {
  lua_CFunction old;
  lua_lock(L);
  old = G(L)->panic;
  G(L)->panic = panicf;
  lua_unlock(L);
  return old;
}

/*
返回保存在 Lua核心中的版本号数据的地址。当 L 是个合法的 lua_State时，返回创建
状态机时使用的版本号数据地址。当L 是NULL 时，返回执行当前调用的状态机的版本号
数据地址。

*/
LUA_API const lua_Number *lua_version (lua_State *L) {
  static const lua_Number version = LUA_VERSION_NUM;
  if (L == NULL) return &version;
  else return G(L)->version;
}



/*
** basic stack manipulation
*/


/*
** convert an acceptable stack index into an absolute index
将可接受的索引idx 转换为绝对索引（即不由栈顶决定的索引）。
*/
LUA_API int lua_absindex (lua_State *L, int idx) {
  return (idx > 0 || ispseudo(idx))
         ? idx
         : cast_int(L->top - L->ci->func + idx);
}

/*返回栈顶元素的索引。因此索引是从1 开始的，所以返回的结果就是栈中元素的个数（因
此返回0 表示栈是个空栈）。 */
LUA_API int lua_gettop (lua_State *L) {
  return cast_int(L->top - (L->ci->func + 1));
}

/*接受任意的索引值或0，将该索引设为栈顶的索引。如果新的栈顶大于旧的，那么新元
素的值都会初始化为nil。如果index 为0，那么栈中的所有元素都会被删除。 */
LUA_API void lua_settop (lua_State *L, int idx) {
  StkId func = L->ci->func;
  lua_lock(L);
  if (idx >= 0) {
    api_check(L, idx <= L->stack_last - (func + 1), "new top too large");
    while (L->top < (func + 1) + idx)
      setnilvalue(L->top++);
    L->top = (func + 1) + idx;
  }
  else {
    api_check(L, -(idx+1) <= (L->top - (func + 1)), "invalid new top");
    L->top += idx+1;  /* `subtract' index (index is negative) */
  }
  lua_unlock(L);
}

/* 将指定的合法索引处的元素删除，该索引之上的值都向下移动一个位置以填补空缺。调
用此函数时不能使用伪索引，因为伪索引并不是真正的栈位置*/
LUA_API void lua_remove (lua_State *L, int idx) {
  StkId p;
  lua_lock(L);
  p = index2addr(L, idx);
  api_checkstackindex(L, idx, p);
  while (++p < L->top) setobjs2s(L, p-1, p);
  L->top--;
  lua_unlock(L);
}

/* 将栈顶的元素移动到栈中的index 处并且将index 处及以上的元素向上移动一个位置。
调用此函数时不能使用伪索引，因为伪索引所指示的位置不在栈中。*/
LUA_API void lua_insert (lua_State *L, int idx) {
  StkId p;
  StkId q;
  lua_lock(L);
  p = index2addr(L, idx);
  api_checkstackindex(L, idx, p);
  for (q = L->top; q > p; q--)  /* use L->top as a temporary */
    setobjs2s(L, q, q - 1);
  setobjs2s(L, p, L->top);
  lua_unlock(L);
}


static void moveto (lua_State *L, TValue *fr, int idx) {
  TValue *to = index2addr(L, idx);
  api_checkvalidindex(L, to);
  setobj(L, to, fr);
  if (idx < LUA_REGISTRYINDEX)  /* function upvalue? */
    luaC_barrier(L, clCvalue(L->ci->func), fr);
  /* LUA_REGISTRYINDEX does not need gc barrier
     (collector revisits it before finishing collection) */
}

/* 将index 处的元素替换为栈顶的元素并将栈顶元素出栈。*/
LUA_API void lua_replace (lua_State *L, int idx) {
  lua_lock(L);
  api_checknelems(L, 1);
  moveto(L, L->top - 1, idx);
  L->top--;
  lua_unlock(L);
}

/* 将fromidx 索引处的值复制到toidx 索引处，此操作不会改变其它位置的元素（只替换
掉那个位置的值）*/
LUA_API void lua_copy (lua_State *L, int fromidx, int toidx) {
  TValue *fr;
  lua_lock(L);
  fr = index2addr(L, fromidx);
  moveto(L, fr, toidx);
  lua_unlock(L);
}

/*将index 位置的元素复制一份并将其压入栈 */
LUA_API void lua_pushvalue (lua_State *L, int idx) {
  lua_lock(L);
  setobj2s(L, L->top, index2addr(L, idx));
  api_incr_top(L);
  lua_unlock(L);
}



/*
** access functions (stack -> C)
*/

/*返回index 处的值的类型，如果索引是个非法索引（但可接受的）则返回LUA_TNONE */
LUA_API int lua_type (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return (isvalid(o) ? ttypenv(o) : LUA_TNONE);
}

/*返回类型为 tp 的类型名字，tp 必须是 lua_type的返回值 */
LUA_API const char *lua_typename (lua_State *L, int t) {
  UNUSED(L);
  return ttypename(t);
}

/*如果index 处的值是个C 函数则返回1，否则返回0 */
LUA_API int lua_iscfunction (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return (ttislcf(o) || (ttisCclosure(o)));
}

/* 如果index 处的值是个数字或是一个可转为数字的字符串则返回1，否则返回0*/
LUA_API int lua_isnumber (lua_State *L, int idx) {
  TValue n;
  const TValue *o = index2addr(L, idx);
  return tonumber(o, &n);
}

/*如果index 处的值是一个字符串或是一个数字（总是可转为字符串的）则返回1，否则
返回0 */
LUA_API int lua_isstring (lua_State *L, int idx) {
  int t = lua_type(L, idx);
  return (t == LUA_TSTRING || t == LUA_TNUMBER);
}

/*如果index 处的值是userdata（full 或者light）则返回1，否则返回0 */
LUA_API int lua_isuserdata (lua_State *L, int idx) {
  const TValue *o = index2addr(L, idx);
  return (ttisuserdata(o) || ttislightuserdata(o));
}

/*如果索引index1 和索引index2 处的值是原始相等（即不调用元方法的情况下）的就返
回1。否则返回0。如果有一个索引是非法的也会返回0 */
LUA_API int lua_rawequal (lua_State *L, int index1, int index2) {
  StkId o1 = index2addr(L, index1);
  StkId o2 = index2addr(L, index2);
  return (isvalid(o1) && isvalid(o2)) ? luaV_rawequalobj(o1, o2) : 0;
}

/*对栈顶的两个值（如果是做求反运算则是一个值）做数学运算，栈顶的那个值会作为第
二个操作数。运算后这些值会被抛出栈，运算的结果会被压入栈。 */
LUA_API void lua_arith (lua_State *L, int op) {
  StkId o1;  /* 1st operand */
  StkId o2;  /* 2nd operand */
  lua_lock(L);
  if (op != LUA_OPUNM) /* all other operations expect two operands */
    api_checknelems(L, 2);
  else {  /* for unary minus, add fake 2nd operand */
    api_checknelems(L, 1);
    setobjs2s(L, L->top, L->top - 1);
    L->top++;
  }
  o1 = L->top - 2;
  o2 = L->top - 1;
  if (ttisnumber(o1) && ttisnumber(o2)) {
    setnvalue(o1, luaO_arith(op, nvalue(o1), nvalue(o2)));
  }
  else
    luaV_arith(L, o1, o1, o2, cast(TMS, op - LUA_OPADD + TM_ADD));
  L->top--;
  lua_unlock(L);
}

/*比较两个Lua 数值。当index1 处的值与index2 处的值满足op 的比较规则（规则与对应
的Lua 运算符的规则，即有可能会触发元方法）时返回1，否则返回0。如果索引是非法的，
那也会返回0。 */
LUA_API int lua_compare (lua_State *L, int index1, int index2, int op) {
  StkId o1, o2;
  int i = 0;
  lua_lock(L);  /* may call tag method */
  o1 = index2addr(L, index1);
  o2 = index2addr(L, index2);
  if (isvalid(o1) && isvalid(o2)) {
    switch (op) {
      case LUA_OPEQ: i = equalobj(L, o1, o2); break;
      case LUA_OPLT: i = luaV_lessthan(L, o1, o2); break;
      case LUA_OPLE: i = luaV_lessequal(L, o1, o2); break;
      default: api_check(L, 0, "invalid option");
    }
  }
  lua_unlock(L);
  return i;
}


LUA_API lua_Number lua_tonumberx (lua_State *L, int idx, int *isnum) {
  TValue n;
  const TValue *o = index2addr(L, idx);
  if (tonumber(o, &n)) {
    if (isnum) *isnum = 1;
    return nvalue(o);
  }
  else {
    if (isnum) *isnum = 0;
    return 0;
  }
}


LUA_API lua_Integer lua_tointegerx (lua_State *L, int idx, int *isnum) {
  TValue n;
  const TValue *o = index2addr(L, idx);
  if (tonumber(o, &n)) {
    lua_Integer res;
    lua_Number num = nvalue(o);
    lua_number2integer(res, num);
    if (isnum) *isnum = 1;
    return res;
  }
  else {
    if (isnum) *isnum = 0;
    return 0;
  }
}


LUA_API lua_Unsigned lua_tounsignedx (lua_State *L, int idx, int *isnum) {
  TValue n;
  const TValue *o = index2addr(L, idx);
  if (tonumber(o, &n)) {
    lua_Unsigned res;
    lua_Number num = nvalue(o);
    lua_number2unsigned(res, num);
    if (isnum) *isnum = 1;
    return res;
  }
  else {
    if (isnum) *isnum = 0;
    return 0;
  }
}

/*将index 处的值转为C 的布尔类型的值（0 或1） */
LUA_API int lua_toboolean (lua_State *L, int idx) {
  const TValue *o = index2addr(L, idx);
  return !l_isfalse(o);
}

/*将index 处的值转为C 字符串 */
LUA_API const char *lua_tolstring (lua_State *L, int idx, size_t *len) {
  StkId o = index2addr(L, idx);
  if (!ttisstring(o)) {
    lua_lock(L);  /* `luaV_tostring' may create a new string */
    if (!luaV_tostring(L, o)) {  /* conversion failed? */
      if (len != NULL) *len = 0;
      lua_unlock(L);
      return NULL;
    }
    luaC_checkGC(L);
    o = index2addr(L, idx);  /* previous call may reallocate the stack */
    lua_unlock(L);
  }
  if (len != NULL) *len = tsvalue(o)->len;
  return svalue(o);
}

/* 返回index 位置的值的原生“长度”：对于字符串，它是字符串的长度；对于表，它是
原生长度操作符（“#”）的运算结果；对于userdata，它是userdata 所占内存空间的大小；对
于其它值，它是0*/
LUA_API size_t lua_rawlen (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  switch (ttypenv(o)) {
    case LUA_TSTRING: return tsvalue(o)->len;
    case LUA_TUSERDATA: return uvalue(o)->len;
    case LUA_TTABLE: return luaH_getn(hvalue(o));
    default: return 0;
  }
}

/* 将index 处的值转为C 函数。该值必须为C 函数，否则返回NULL*/
LUA_API lua_CFunction lua_tocfunction (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  if (ttislcf(o)) return fvalue(o);
  else if (ttisCclosure(o))
    return clCvalue(o)->f;
  else return NULL;  /* not a C function */
}

/*如果index 处的值是一个full userdata 则返回它的块地址。如果该值是个light userdata
则返回它的指针。否则返回NULL */
LUA_API void *lua_touserdata (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  switch (ttypenv(o)) {
    case LUA_TUSERDATA: return (rawuvalue(o) + 1);
    case LUA_TLIGHTUSERDATA: return pvalue(o);
    default: return NULL;
  }
}

/*将index 处的值转为Lua 线程（实际是一个lua_State*）。该值必须为线程，否则此函数
返回NULL */
LUA_API lua_State *lua_tothread (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return (!ttisthread(o)) ? NULL : thvalue(o);
}

/* 将index 处的值转为普通的C 指针（void*）*/
LUA_API const void *lua_topointer (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  switch (ttype(o)) {
    case LUA_TTABLE: return hvalue(o);
    case LUA_TLCL: return clLvalue(o);
    case LUA_TCCL: return clCvalue(o);
    case LUA_TLCF: return cast(void *, cast(size_t, fvalue(o)));
    case LUA_TTHREAD: return thvalue(o);
    case LUA_TUSERDATA:
    case LUA_TLIGHTUSERDATA:
      return lua_touserdata(L, idx);
    default: return NULL;
  }
}



/*
** push functions (C -> stack)
*/

/*将一个nil 值压入栈 */
LUA_API void lua_pushnil (lua_State *L) {
  lua_lock(L);
  setnilvalue(L->top);
  api_incr_top(L);
  lua_unlock(L);
}

/*将一个数字n 压入栈 */
LUA_API void lua_pushnumber (lua_State *L, lua_Number n) {
  lua_lock(L);
  setnvalue(L->top, n);
  luai_checknum(L, L->top,
    luaG_runerror(L, "C API - attempt to push a signaling NaN"));
  api_incr_top(L);
  lua_unlock(L);
}

/*将一个整型数字n 压入栈 */
LUA_API void lua_pushinteger (lua_State *L, lua_Integer n) {
  lua_lock(L);
  setnvalue(L->top, cast_num(n));
  api_incr_top(L);
  lua_unlock(L);
}

/*将无符号整型数字n 压入栈 */
LUA_API void lua_pushunsigned (lua_State *L, lua_Unsigned u) {
  lua_Number n;
  lua_lock(L);
  n = lua_unsigned2number(u);
  setnvalue(L->top, n);
  api_incr_top(L);
  lua_unlock(L);
}

/*将由参数s 指定的字符串的长度为len 的部分压入栈。Lua 会生成（或重用）这个字符
串的一个内部的拷贝，因此，在此函数返回后，s 的内存可以被立刻释放或重用
。字符串可 以包含任意的二进制数据，包括内嵌的0。 */
LUA_API const char *lua_pushlstring (lua_State *L, const char *s, size_t len) {
  TString *ts;
  lua_lock(L);
  luaC_checkGC(L);
  ts = luaS_newlstr(L, s, len);
  setsvalue2s(L, L->top, ts);
  api_incr_top(L);
  lua_unlock(L);
  return getstr(ts);
}

/*
将由参数s 指定的以0 结尾的字符串压入栈。Lua 会生成（或重用）这个字符串的一个
内部的拷贝，因此，在此函数返回后，s 的内存可以被立刻释放或重用。
返回该字符串的内部拷贝的指针。
如果 s 是 NULL，那就会压入 nil和返回 NULL

*/
LUA_API const char *lua_pushstring (lua_State *L, const char *s) {
  if (s == NULL) {
    lua_pushnil(L);
    return NULL;
  }
  else {
    TString *ts;
    lua_lock(L);
    luaC_checkGC(L);
    ts = luaS_new(L, s);
    setsvalue2s(L, L->top, ts);
    api_incr_top(L);
    lua_unlock(L);
    return getstr(ts);
  }
}

/*除了用一个 va_list 类型的参数代替不定数量的参数外，此函数等同于lua_pushfstring */
LUA_API const char *lua_pushvfstring (lua_State *L, const char *fmt,
                                      va_list argp) {
  const char *ret;
  lua_lock(L);
  luaC_checkGC(L);
  ret = luaO_pushvfstring(L, fmt, argp);
  lua_unlock(L);
  return ret;
}

/*将一个格式化的字符串压入栈并将该字符串的指针返回 */
LUA_API const char *lua_pushfstring (lua_State *L, const char *fmt, ...) {
  const char *ret;
  va_list argp;
  lua_lock(L);
  luaC_checkGC(L);
  va_start(argp, fmt);
  ret = luaO_pushvfstring(L, fmt, argp);
  va_end(argp);
  lua_unlock(L);
  return ret;
}

/* 将一个新的C 闭包压入栈*/
LUA_API void lua_pushcclosure (lua_State *L, lua_CFunction fn, int n) {
  lua_lock(L);
  if (n == 0) {
    setfvalue(L->top, fn);
  }
  else {
    Closure *cl;
    api_checknelems(L, n);
    api_check(L, n <= MAXUPVAL, "upvalue index too large");
    luaC_checkGC(L);
    cl = luaF_newCclosure(L, n);
    cl->c.f = fn;
    L->top -= n;
    while (n--)
      setobj2n(L, &cl->c.upvalue[n], L->top + n);
    setclCvalue(L, L->top, cl);
  }
  api_incr_top(L);
  lua_unlock(L);
}

/*根据b 的值将一个布尔类型的值压入栈 */
LUA_API void lua_pushboolean (lua_State *L, int b) {
  lua_lock(L);
  setbvalue(L->top, (b != 0));  /* ensure that true is 1 */
  api_incr_top(L);
  lua_unlock(L);
}

/* 
将一个light userdata 压入栈。
在Lua 里，Userdata 代表C 数据。一个light userdata 代表一个指针，一个void*指针。
它是一个值（像一个数字）：你没有创建它，它没有自己的元表，不会被回收（因为它从没
被创建）。如果两个light userdata 它们的C 地址一样，那就认为它们是相等的。

*/
LUA_API void lua_pushlightuserdata (lua_State *L, void *p) {
  lua_lock(L);
  setpvalue(L->top, p);
  api_incr_top(L);
  lua_unlock(L);
}

/*将参数L 所代表的线程压入栈。如果该线程是主线程则返回1 */
LUA_API int lua_pushthread (lua_State *L) {
  lua_lock(L);
  setthvalue(L, L->top, L);
  api_incr_top(L);
  lua_unlock(L);
  return (G(L)->mainthread == L);
}



/*
** get functions (Lua -> stack)
*/

/* 将名字为name 的全局变量的值压入栈*/
LUA_API void lua_getglobal (lua_State *L, const char *var) {
  Table *reg = hvalue(&G(L)->l_registry);
  const TValue *gt;  /* global table */
  lua_lock(L);
  gt = luaH_getint(reg, LUA_RIDX_GLOBALS);
  setsvalue2s(L, L->top++, luaS_new(L, var));
  luaV_gettable(L, gt, L->top - 1, L->top - 1);
  lua_unlock(L);
}

/* 将t[k]的值压入栈，t 是由参数index 指向的值，k 是栈顶的值。*/
LUA_API void lua_gettable (lua_State *L, int idx) {
  StkId t;
  lua_lock(L);
  t = index2addr(L, idx);
  luaV_gettable(L, t, L->top - 1, L->top - 1);
  lua_unlock(L);
}

/* 将t[k]的值压入栈，t 是由参数index 指向的值。和在Lua 代码中一样
，此函数会触发“index”事件的元方法*/
LUA_API void lua_getfield (lua_State *L, int idx, const char *k) {
  StkId t;
  lua_lock(L);
  t = index2addr(L, idx);
  setsvalue2s(L, L->top, luaS_new(L, k));
  api_incr_top(L);
  luaV_gettable(L, t, L->top - 1, L->top - 1);
  lua_unlock(L);
}

/*和 lua_gettable类似，但这是一个原生的访问（即不触发元方法） */
LUA_API void lua_rawget (lua_State *L, int idx) {
  StkId t;
  lua_lock(L);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  setobj2s(L, L->top - 1, luaH_get(hvalue(t), L->top - 1));
  lua_unlock(L);
}

/*将t[n]的值压入栈，t 是索引index 处的表。这个访问是原生的
，也就是说不会触发元方法 */
LUA_API void lua_rawgeti (lua_State *L, int idx, int n) {
  StkId t;
  lua_lock(L);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  setobj2s(L, L->top, luaH_getint(hvalue(t), n));
  api_incr_top(L);
  lua_unlock(L);
}

/* 将t[k]的值压入栈，t 是索引index 处的表，k 是指针p 代表的light userdata
。这个访问是原生的，也就是说不会触发元方法*/
LUA_API void lua_rawgetp (lua_State *L, int idx, const void *p) {
  StkId t;
  TValue k;
  lua_lock(L);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  setpvalue(&k, cast(void *, p));
  setobj2s(L, L->top, luaH_get(hvalue(t), &k));
  api_incr_top(L);
  lua_unlock(L);
}

/*
创建一个空的table 并将其压入栈。参数narray 指明这个table 的数组部分有多少个元素；
参数nrec 指明这个table 的其它部分有多少个元素。Lua 可以使用这两个参数来为一个新的
table 预分配内存。这种预分配在你预先知道table 有多少个元素时很有用，不然的话你可以
使用 lua_newtable。
*/
LUA_API void lua_createtable (lua_State *L, int narray, int nrec) {
  Table *t;
  lua_lock(L);
  luaC_checkGC(L);
  t = luaH_new(L);
  sethvalue(L, L->top, t);
  api_incr_top(L);
  if (narray > 0 || nrec > 0)
    luaH_resize(L, t, narray, nrec);
  lua_unlock(L);
}

/* 将index 指向的值的元表压入栈。如果该值没有元表，则此函数返回0 且不压任何东西
入栈*/
LUA_API int lua_getmetatable (lua_State *L, int objindex) {
  const TValue *obj;
  Table *mt = NULL;
  int res;
  lua_lock(L);
  obj = index2addr(L, objindex);
  switch (ttypenv(obj)) {
    case LUA_TTABLE:
      mt = hvalue(obj)->metatable;
      break;
    case LUA_TUSERDATA:
      mt = uvalue(obj)->metatable;
      break;
    default:
      mt = G(L)->mt[ttypenv(obj)];
      break;
  }
  if (mt == NULL)
    res = 0;
  else {
    sethvalue(L, L->top, mt);
    api_incr_top(L);
    res = 1;
  }
  lua_unlock(L);
  return res;
}

/* 将与 index处的 userdata相关联的 Lua 值压入栈。Lua 的值必须是表或 nil*/
LUA_API void lua_getuservalue (lua_State *L, int idx) {
  StkId o;
  lua_lock(L);
  o = index2addr(L, idx);
  api_check(L, ttisuserdata(o), "userdata expected");
  if (uvalue(o)->env) {
    sethvalue(L, L->top, uvalue(o)->env);
  } else
    setnilvalue(L->top);
  api_incr_top(L);
  lua_unlock(L);
}


/*
** set functions (stack -> Lua)
*/

/*将栈顶元素弹出并将它的值赋值给名字为name 的全局变量中 */
LUA_API void lua_setglobal (lua_State *L, const char *var) {
  Table *reg = hvalue(&G(L)->l_registry);
  const TValue *gt;  /* global table */
  lua_lock(L);
  api_checknelems(L, 1);
  gt = luaH_getint(reg, LUA_RIDX_GLOBALS);
  setsvalue2s(L, L->top++, luaS_new(L, var));
  luaV_settable(L, gt, L->top - 1, L->top - 2);
  L->top -= 2;  /* pop value and key */
  lua_unlock(L);
}

/*等同于t[k] = v 的操作，t 是索引index 处的表，v 是栈顶的值，k 是栈顶下面的一个值。
此函数会将键和值都弹出栈。和Lua 的代码一样，此函数会触发“newindex”事件的元
方法
*/
LUA_API void lua_settable (lua_State *L, int idx) {
  StkId t;
  lua_lock(L);
  api_checknelems(L, 2);
  t = index2addr(L, idx);
  luaV_settable(L, t, L->top - 2, L->top - 1);
  L->top -= 2;  /* pop index and value */
  lua_unlock(L);
}

/*等同于t[k] = v 的操作，t 是索引index 处的表，v 是栈顶的值。
此函数会将栈顶的值弹出栈。和Lua 的代码一样，此函数会触发“newindex”事件的元
方法 */
LUA_API void lua_setfield (lua_State *L, int idx, const char *k) {
  StkId t;
  lua_lock(L);
  api_checknelems(L, 1);
  t = index2addr(L, idx);
  setsvalue2s(L, L->top++, luaS_new(L, k));
  luaV_settable(L, t, L->top - 1, L->top - 2);
  L->top -= 2;  /* pop value and key */
  lua_unlock(L);
}

/* 和 lua_settable类似，但这是一个原生的赋值操作*/
LUA_API void lua_rawset (lua_State *L, int idx) {
  StkId t;
  lua_lock(L);
  api_checknelems(L, 2);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  setobj2t(L, luaH_set(L, hvalue(t), L->top-2), L->top-1);
  invalidateTMcache(hvalue(t));
  luaC_barrierback(L, gcvalue(t), L->top-1);
  L->top -= 2;
  lua_unlock(L);
}

/* 等同于t[n] = v 的操作，t 是索引index 处的表，v 是栈顶的值。
此函数会将栈顶的值弹出栈
。此赋值操作是原生的，也就是说不会触发元方法
*/
LUA_API void lua_rawseti (lua_State *L, int idx, int n) {
  StkId t;
  lua_lock(L);
  api_checknelems(L, 1);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  luaH_setint(L, hvalue(t), n, L->top - 1);
  luaC_barrierback(L, gcvalue(t), L->top-1);
  L->top--;
  lua_unlock(L);
}

/*等同于t[k] = v 的操作，t 是索引index 处的表，k 是指针p 代表的light userdata
，v 是栈顶的值。此函数会将栈顶的值弹出栈。
此赋值操作是原生的，也就是说不会触发元方法。 */
LUA_API void lua_rawsetp (lua_State *L, int idx, const void *p) {
  StkId t;
  TValue k;
  lua_lock(L);
  api_checknelems(L, 1);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  setpvalue(&k, cast(void *, p));
  setobj2t(L, luaH_set(L, hvalue(t), &k), L->top - 1);
  luaC_barrierback(L, gcvalue(t), L->top - 1);
  L->top--;
  lua_unlock(L);
}

/* 将栈顶的表弹出并将它设置为index 处的值的元表*/
LUA_API int lua_setmetatable (lua_State *L, int objindex) {
  TValue *obj;
  Table *mt;
  lua_lock(L);
  api_checknelems(L, 1);
  obj = index2addr(L, objindex);
  if (ttisnil(L->top - 1))
    mt = NULL;
  else {
    api_check(L, ttistable(L->top - 1), "table expected");
    mt = hvalue(L->top - 1);
  }
  switch (ttypenv(obj)) {
    case LUA_TTABLE: {
      hvalue(obj)->metatable = mt;
      if (mt) {
        luaC_objbarrierback(L, gcvalue(obj), mt);
        luaC_checkfinalizer(L, gcvalue(obj), mt);
      }
      break;
    }
    case LUA_TUSERDATA: {
      uvalue(obj)->metatable = mt;
      if (mt) {
        luaC_objbarrier(L, rawuvalue(obj), mt);
        luaC_checkfinalizer(L, gcvalue(obj), mt);
      }
      break;
    }
    default: {
      G(L)->mt[ttypenv(obj)] = mt;
      break;
    }
  }
  L->top--;
  lua_unlock(L);
  return 1;
}

/* 将栈顶的表或 nil弹出栈并将其设为 index处的 userdata的关联值。*/
LUA_API void lua_setuservalue (lua_State *L, int idx) {
  StkId o;
  lua_lock(L);
  api_checknelems(L, 1);
  o = index2addr(L, idx);
  api_check(L, ttisuserdata(o), "userdata expected");
  if (ttisnil(L->top - 1))
    uvalue(o)->env = NULL;
  else {
    api_check(L, ttistable(L->top - 1), "table expected");
    uvalue(o)->env = hvalue(L->top - 1);
    luaC_objbarrier(L, gcvalue(o), hvalue(L->top - 1));
  }
  L->top--;
  lua_unlock(L);
}


/*
** `load' and `call' functions (run Lua code)
*/


#define checkresults(L,na,nr) \
     api_check(L, (nr) == LUA_MULTRET || (L->ci->top - L->top >= (nr) - (na)), \
	"results from function overflow current stack size")

/*此函数由“继续函数（continuation function）”调用以获得线程的状态和上
下文信息 */
LUA_API int lua_getctx (lua_State *L, int *ctx) {
  if (L->ci->callstatus & CIST_YIELDED) {
    if (ctx) *ctx = L->ci->u.c.ctx;
    return L->ci->u.c.status;
  }
  else return LUA_OK;
}

/*这个函数的行为和 lua_call完全一样，但是它允许被调用函数 yield */
LUA_API void lua_callk (lua_State *L, int nargs, int nresults, int ctx,
                        lua_CFunction k) {
  StkId func;
  lua_lock(L);
  api_check(L, k == NULL || !isLua(L->ci),
    "cannot use continuations inside hooks");
  api_checknelems(L, nargs+1);
  api_check(L, L->status == LUA_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  func = L->top - (nargs+1);
  if (k != NULL && L->nny == 0) {  /* need to prepare continuation? */
    L->ci->u.c.k = k;  /* save continuation */
    L->ci->u.c.ctx = ctx;  /* save context */
    luaD_call(L, func, nresults, 1);  /* do the call */
  }
  else  /* no continuation or no yieldable */
    luaD_call(L, func, nresults, 0);  /* just do the call */
  adjustresults(L, nresults);
  lua_unlock(L);
}



/*
** Execute a protected call.
*/
struct CallS {  /* data to `f_call' */
  StkId func;
  int nresults;
};


static void f_call (lua_State *L, void *ud) {
  struct CallS *c = cast(struct CallS *, ud);
  luaD_call(L, c->func, c->nresults, 0);
}


/* 此函数除了允许被调函数可以 yield外，其它行为和 lua_pcall完全一致*/
LUA_API int lua_pcallk (lua_State *L, int nargs, int nresults, int errfunc,
                        int ctx, lua_CFunction k) {
  struct CallS c;
  int status;
  ptrdiff_t func;
  lua_lock(L);
  api_check(L, k == NULL || !isLua(L->ci),
    "cannot use continuations inside hooks");
  api_checknelems(L, nargs+1);
  api_check(L, L->status == LUA_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  if (errfunc == 0)
    func = 0;
  else {
    StkId o = index2addr(L, errfunc);
    api_checkstackindex(L, errfunc, o);
    func = savestack(L, o);
  }
  c.func = L->top - (nargs+1);  /* function to be called */
  if (k == NULL || L->nny > 0) {  /* no continuation or no yieldable? */
    c.nresults = nresults;  /* do a 'conventional' protected call */
    status = luaD_pcall(L, f_call, &c, savestack(L, c.func), func);
  }
  else {  /* prepare continuation (call is already protected by 'resume') */
    CallInfo *ci = L->ci;
    ci->u.c.k = k;  /* save continuation */
    ci->u.c.ctx = ctx;  /* save context */
    /* save information for error recovery */
    ci->extra = savestack(L, c.func);
    ci->u.c.old_allowhook = L->allowhook;
    ci->u.c.old_errfunc = L->errfunc;
    L->errfunc = func;
    /* mark that function may do error recovery */
    ci->callstatus |= CIST_YPCALL;
    luaD_call(L, c.func, nresults, 1);  /* do the call */
    ci->callstatus &= ~CIST_YPCALL;
    L->errfunc = ci->u.c.old_errfunc;
    status = LUA_OK;  /* if it is here, there were no errors */
  }
  adjustresults(L, nresults);
  lua_unlock(L);
  return status;
}

/*加载Lua 代码块（不执行它）。如果没有错误，lua_load 将编译后的代码块作为一个函
数放在栈顶，否则将错误信息压入栈。 */
LUA_API int lua_load (lua_State *L, lua_Reader reader, void *data,
                      const char *chunkname, const char *mode) {
  ZIO z;
  int status;
  lua_lock(L);
  if (!chunkname) chunkname = "?";
  luaZ_init(L, &z, reader, data);
  status = luaD_protectedparser(L, &z, chunkname, mode);
  if (status == LUA_OK) {  /* no errors? */
    LClosure *f = clLvalue(L->top - 1);  /* get newly created function */
    if (f->nupvalues == 1) {  /* does it have one upvalue? */
      /* get global table from registry */
      Table *reg = hvalue(&G(L)->l_registry);
      const TValue *gt = luaH_getint(reg, LUA_RIDX_GLOBALS);
      /* set global table as 1st upvalue of 'f' (may be LUA_ENV) */
      setobj(L, f->upvals[0]->v, gt);
      luaC_barrier(L, f->upvals[0], gt);
    }
  }
  lua_unlock(L);
  return status;
}

/*
将一个函数转化为一个二进制代码块。它根据栈顶的Lua 函数产生一个二进制代码块，
如果加载这个代码块，那么它作为一个函数是等同于转化前的那个函数的。由于它是代码块，
lua_dump可以调用函数 writer（参考 lua_Writer）来将它们写到 data 中。
返回值是一个错误码，它由最后一次调用的writer 返回。0 表示没有错误。
这个函数不会将Lua 函数抛出栈。

*/
LUA_API int lua_dump (lua_State *L, lua_Writer writer, void *data) {
  int status;
  TValue *o;
  lua_lock(L);
  api_checknelems(L, 1);
  o = L->top - 1;
  if (isLfunction(o))
    status = luaU_dump(L, getproto(o), writer, data, 0);
  else
    status = 1;
  lua_unlock(L);
  return status;
}

/*返回线程L 的状态 */
LUA_API int lua_status (lua_State *L) {
  return L->status;
}


/*
** Garbage-collection function
*/
/* 控制垃圾回收*/
LUA_API int lua_gc (lua_State *L, int what, int data) {
  int res = 0;
  global_State *g;
  lua_lock(L);
  g = G(L);
  switch (what) {
    case LUA_GCSTOP: {
      g->gcrunning = 0;
      break;
    }
    case LUA_GCRESTART: {
      luaE_setdebt(g, 0);
      g->gcrunning = 1;
      break;
    }
    case LUA_GCCOLLECT: {
      luaC_fullgc(L, 0);
      break;
    }
    case LUA_GCCOUNT: {
      /* GC values are expressed in Kbytes: #bytes/2^10 */
      res = cast_int(gettotalbytes(g) >> 10);
      break;
    }
    case LUA_GCCOUNTB: {
      res = cast_int(gettotalbytes(g) & 0x3ff);
      break;
    }
    case LUA_GCSTEP: {
      if (g->gckind == KGC_GEN) {  /* generational mode? */
        res = (g->GCestimate == 0);  /* true if it will do major collection */
        luaC_forcestep(L);  /* do a single step */
      }
      else {
       lu_mem debt = cast(lu_mem, data) * 1024 - GCSTEPSIZE;
       if (g->gcrunning)
         debt += g->GCdebt;  /* include current debt */
       luaE_setdebt(g, debt);
       luaC_forcestep(L);
       if (g->gcstate == GCSpause)  /* end of cycle? */
         res = 1;  /* signal it */
      }
      break;
    }
    case LUA_GCSETPAUSE: {
      res = g->gcpause;
      g->gcpause = data;
      break;
    }
    case LUA_GCSETMAJORINC: {
      res = g->gcmajorinc;
      g->gcmajorinc = data;
      break;
    }
    case LUA_GCSETSTEPMUL: {
      res = g->gcstepmul;
      g->gcstepmul = data;
      break;
    }
    case LUA_GCISRUNNING: {
      res = g->gcrunning;
      break;
    }
    case LUA_GCGEN: {  /* change collector to generational mode */
      luaC_changemode(L, KGC_GEN);
      break;
    }
    case LUA_GCINC: {  /* change collector to incremental mode */
      luaC_changemode(L, KGC_NORMAL);
      break;
    }
    default: res = -1;  /* invalid option */
  }
  lua_unlock(L);
  return res;
}



/*
** miscellaneous functions
*/

/*生成一个Lua 错误。这个错误消息（可以是Lua 的值或者其它类型）一定要放在栈顶。
此函数会执行一个长跳转，因此这里没有返回值 */
LUA_API int lua_error (lua_State *L) {
  lua_lock(L);
  api_checknelems(L, 1);
  luaG_errormsg(L);
  /* code unreachable; will unlock when control actually leaves the kernel */
  return 0;  /* to avoid warnings */
}

/*将一个键从栈中弹出并将参数index 处的表中的一“键-值对”（指定键之后的“下一对”）
压入栈。如果表中不再有“下一对”了，那么 lua_next返回0（不压任何东西入栈）。 */
LUA_API int lua_next (lua_State *L, int idx) {
  StkId t;
  int more;
  lua_lock(L);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  more = luaH_next(L, hvalue(t), L->top - 1);
  if (more) {
    api_incr_top(L);
  }
  else  /* no more elements */
    L->top -= 1;  /* remove key */
  lua_unlock(L);
  return more;
}

/* 连接栈顶处的n 个值，从栈中抛出它们并且将结果压入栈。如果n 是1，那么结果就是
栈顶的值（也就是函数什么都不做）；如果n 是0，那么结果就是空字符串。连接操作遵守
Lua的一般语法规则*/
LUA_API void lua_concat (lua_State *L, int n) {
  lua_lock(L);
  api_checknelems(L, n);
  if (n >= 2) {
    luaC_checkGC(L);
    luaV_concat(L, n);
  }
  else if (n == 0) {  /* push empty string */
    setsvalue2s(L, L->top, luaS_newlstr(L, "", 0));
    api_incr_top(L);
  }
  /* else n == 1; nothing to do */
  lua_unlock(L);
}

/*获得 index处的值的“长度”。在 Lua 代码中等同于“#”操作符（参考 3.4.6）。结果会
被压入栈。 */
LUA_API void lua_len (lua_State *L, int idx) {
  StkId t;
  lua_lock(L);
  t = index2addr(L, idx);
  luaV_objlen(L, L->top, t);
  api_incr_top(L);
  lua_unlock(L);
}

/*返回指定Lua 状态机的内存分配函数。如果ud 不是NULL，那么Lua 会将传给
lua_newstate 的指针存入*ud 中。 */
LUA_API lua_Alloc lua_getallocf (lua_State *L, void **ud) {
  lua_Alloc f;
  lua_lock(L);
  if (ud) *ud = G(L)->ud;
  f = G(L)->frealloc;
  lua_unlock(L);
  return f;
}

/*改变指定状态机的内存分配函数为f 及用户数据为ud */
LUA_API void lua_setallocf (lua_State *L, lua_Alloc f, void *ud) {
  lua_lock(L);
  G(L)->ud = ud;
  G(L)->frealloc = f;
  lua_unlock(L);
}

/*
创建一个在新的独立的状态机运行的新线程。如果不能创建线程或状态机（由于内存不
足）就返回NULL。参数f 是分配器函数。该状态机中所有与内存分配相关的操作都通过这
个分配器函数来完成。在每次调用分配器函数时都会将第二个参数ud 传给分配器函数。
*/
LUA_API void *lua_newuserdata (lua_State *L, size_t size) {
  Udata *u;
  lua_lock(L);
  luaC_checkGC(L);
  u = luaS_newudata(L, size, NULL);
  setuvalue(L, L->top, u);
  api_incr_top(L);
  lua_unlock(L);
  return u + 1;
}



static const char *aux_upvalue (StkId fi, int n, TValue **val,
                                GCObject **owner) {
  switch (ttype(fi)) {
    case LUA_TCCL: {  /* C closure */
      CClosure *f = clCvalue(fi);
      if (!(1 <= n && n <= f->nupvalues)) return NULL;
      *val = &f->upvalue[n-1];
      if (owner) *owner = obj2gco(f);
      return "";
    }
    case LUA_TLCL: {  /* Lua closure */
      LClosure *f = clLvalue(fi);
      TString *name;
      Proto *p = f->p;
      if (!(1 <= n && n <= p->sizeupvalues)) return NULL;
      *val = f->upvals[n-1]->v;
      if (owner) *owner = obj2gco(f->upvals[n - 1]);
      name = p->upvalues[n-1].name;
      return (name == NULL) ? "" : getstr(name);
    }
    default: return NULL;  /* not a closure */
  }
}

/*获得闭包的upvalue 信息。（对于Lua 的函数来说，upvalue 指的是函数使用的外部的局
部变量。）lua_getupvalue 获得索引为n 的upvalue 的信息，将它的值压入栈并返回它的名字。
参数funcindex 指向栈中的闭包。（因为upvalue 在整个函数中都是活动的，所以它没有什么
特定的顺序。因此它们被随意地编号。）
当索引大于upvalue 的个数时，函数返回NULL（没有东西被压入栈）。对于C 函数来
说，用空字符串来给所有的upvalue 命名。 */
LUA_API const char *lua_getupvalue (lua_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = NULL;  /* to avoid warnings */
  lua_lock(L);
  name = aux_upvalue(index2addr(L, funcindex), n, &val, NULL);
  if (name) {
    setobj2s(L, L->top, val);
    api_incr_top(L);
  }
  lua_unlock(L);
  return name;
}

/*设置闭包的upvalue 值。它将栈顶的值赋给指定的upvalue 并返回upvalue 的名字。同时
它也会将栈顶的值弹出。参数 funcindex、n 的含义和 lua_getupvalue 中的一样。
当索引大于upvalue 的个数时，函数返回NULL（没有东西被弹出栈）。
*/
LUA_API const char *lua_setupvalue (lua_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = NULL;  /* to avoid warnings */
  GCObject *owner = NULL;  /* to avoid warnings */
  StkId fi;
  lua_lock(L);
  fi = index2addr(L, funcindex);
  api_checknelems(L, 1);
  name = aux_upvalue(fi, n, &val, &owner);
  if (name) {
    L->top--;
    setobj(L, val, L->top);
    luaC_barrier(L, owner, L->top);
  }
  lua_unlock(L);
  return name;
}


static UpVal **getupvalref (lua_State *L, int fidx, int n, LClosure **pf) {
  LClosure *f;
  StkId fi = index2addr(L, fidx);
  api_check(L, ttisLclosure(fi), "Lua function expected");
  f = clLvalue(fi);
  api_check(L, (1 <= n && n <= f->p->sizeupvalues), "invalid upvalue index");
  if (pf) *pf = f;
  return &f->upvals[n - 1];  /* get its upvalue pointer */
}

/* 返回funcindex 处的闭包的第n 个upvalue 的唯一标记。参数funcindex 和n 的含义和
lua_getupvalue中的一样（参考 lua_getupvalue）（n不能大于 upvalue 的个数）。
程序使用这个唯一标记可以检查不同的闭包是否共享了upvalue。共享同一个upvalue
（也就是访问了相同的外部的局部变量）的闭包会，它们返回的这个upvalue 的标识是一样
的。*/
LUA_API void *lua_upvalueid (lua_State *L, int fidx, int n) {
  StkId fi = index2addr(L, fidx);
  switch (ttype(fi)) {
    case LUA_TLCL: {  /* lua closure */
      return *getupvalref(L, fidx, n, NULL);
    }
    case LUA_TCCL: {  /* C closure */
      CClosure *f = clCvalue(fi);
      api_check(L, 1 <= n && n <= f->nupvalues, "invalid upvalue index");
      return &f->upvalue[n - 1];
    }
    default: {
      api_check(L, 0, "closure expected");
      return NULL;
    }
  }
}

/*
将funcindex1 处的闭包的第n1 个upvalue 引用为funcindex2 处的闭包的第n2 个upvalue。
（即前一个upvalue 指向后一个upvalue。）

*/
LUA_API void lua_upvaluejoin (lua_State *L, int fidx1, int n1,
                                            int fidx2, int n2) {
  LClosure *f1;
  UpVal **up1 = getupvalref(L, fidx1, n1, &f1);
  UpVal **up2 = getupvalref(L, fidx2, n2, NULL);
  *up1 = *up2;
  luaC_objbarrier(L, f1, *up2);
}

