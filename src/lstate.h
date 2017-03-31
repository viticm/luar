/*
** $Id: lstate.h,v 2.133 2016/12/22 13:08:50 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#ifndef lstate_h
#define lstate_h

#include "lua.h"

#include "lobject.h"
#include "ltm.h"
#include "lzio.h"


/*

** Some notes about garbage-collected objects: All objects in Lua must
** be kept somehow accessible until being freed, so all objects always
** belong to one (and only one) of these lists, using field 'next' of
** the 'CommonHeader' for the link:
**
** 'allgc': all objects not marked for finalization;
** 'finobj': all objects marked for finalization;
** 'tobefnz': all objects ready to be finalized;
** 'fixedgc': all objects that are not to be collected (currently
** only small strings, such as reserved words).

*/


struct lua_longjmp;  /* defined in ldo.c */


/*
** Atomic type (relative to signals) to better ensure that 'lua_sethook'
** is thread safe
*/
#if !defined(l_signalT)
#include <signal.h>
#define l_signalT	sig_atomic_t
#endif


/* extra stack space to handle TM calls and some other extras */
#define EXTRA_STACK   5


#define BASIC_STACK_SIZE        (2*LUA_MINSTACK)


/* kinds of Garbage Collection */
#define KGC_NORMAL	0
#define KGC_EMERGENCY	1	/* gc was forced by an allocation failure */


typedef struct stringtable {
  TString **hash;
  int nuse;  /* number of elements */
  int size;
} stringtable;


/*
** Information about a call.
** When a thread yields, 'func' is adjusted to pretend that the
** top function has only the yielded values in its stack; in that
** case, the actual 'func' value is saved in field 'extra'.
** When a function calls another with a continuation, 'extra' keeps
** the function index so that, in case of errors, the continuation
** function can be called with the correct top.
*/
typedef struct CallInfo {
  StkId func;  /* function index in the stack */
  StkId	top;  /* top for this function */
  struct CallInfo *previous, *next;  /* dynamic call link */
  union {
    struct {  /* only for Lua functions */
      StkId base;  /* base for this function */
      const Instruction *savedpc;
    } l;
    struct {  /* only for C functions */
      lua_KFunction k;  /* continuation in case of yields */
      ptrdiff_t old_errfunc;
      lua_KContext ctx;  /* context info. in case of yields */
    } c;
  } u;
  ptrdiff_t extra;
  short nresults;  /* expected number of results from this function */
  unsigned short callstatus;
} CallInfo;


/*
** Bits in CallInfo status
*/
#define CIST_OAH	(1<<0)	/* original value of 'allowhook' */
#define CIST_LUA	(1<<1)	/* call is running a Lua function */
#define CIST_HOOKED	(1<<2)	/* call is running a debug hook */
#define CIST_FRESH	(1<<3)	/* call is running on a fresh invocation
                                   of luaV_execute */
#define CIST_YPCALL	(1<<4)	/* call is a yieldable protected call */
#define CIST_TAIL	(1<<5)	/* call was tail called */
#define CIST_HOOKYIELD	(1<<6)	/* last hook called yielded */
#define CIST_LEQ	(1<<7)  /* using __lt for __le */
#define CIST_FIN	(1<<8)  /* call is running a finalizer */

#define isLua(ci)	((ci)->callstatus & CIST_LUA)

/* assume that CIST_OAH has offset 0 and that 'v' is strictly 0/1 */
#define setoah(st,v)	((st) = ((st) & ~CIST_OAH) | (v))
#define getoah(st)	((st) & CIST_OAH)


/*
** 'global state', shared by all threads of this state
** cn:
** 全局状态（全局共享数据）
** 所有的线程都共享了该状态
*/
typedef struct global_State {
  lua_Alloc frealloc;  /* function to reallocate memory */
                        /* cn: 分配内存方法指针 */
  void *ud;         /* auxiliary data to 'frealloc' */
                    /* cn: 为'frealloc'备用的内存 */
  l_mem totalbytes;  /* number of bytes currently allocated - GCdebt */
                     /* cn: 当前已使用的内存，为allocated - GCDebt */
  l_mem GCdebt;  /* bytes allocated not yet compensated by the collector */
                    /* cn: 分配的内存还没有被收集器补偿 */

  lu_mem GCmemtrav;  /* memory traversed by the GC */
                    /* cn: 已经通过GC的内存数量 */
  lu_mem GCestimate;  /* an estimate of the non-garbage memory in use */
                        /* cn: 预估使用的非垃圾内存大小 */
  stringtable strt;  /* hash table for strings */
                        /* cn: 字符串哈希 */
  TValue l_registry;
  unsigned int seed;  /* randomized seed for hashes */
                        /* cn: 哈希随机种子 */
  lu_byte currentwhite;
  lu_byte gcstate;  /* state of garbage collector */
                    /* cn: 垃圾回收器的状态 */
  lu_byte gckind;  /* kind of GC running */ /* cn: GC运行的方式 */
  lu_byte gcrunning;  /* true if GC is running */ /* cn: GC是否在运行 */
  GCObject *allgc;  /* list of all collectable objects */ /* cn: 所有可回收对象列表 */
  GCObject **sweepgc;  /* current position of sweep in list */ /* 列表中现在清除的位置 */
  GCObject *finobj;  /* list of collectable objects with finalizers */
                        /* cn: 被终结的回收对象列表 */
  GCObject *gray;  /* list of gray objects */ /* cn: 灰色对象列表 */
  GCObject *grayagain;  /* list of objects to be traversed atomically */
                        /* cn: 固定操作对象列表 */

  GCObject *weak;  /* list of tables with weak values */
                    /* cn: 虚值表列表 */
  GCObject *ephemeron;  /* list of ephemeron tables (weak keys) */
                        /* cn: 临时表列表（虚键） */
  GCObject *allweak;  /* list of all-weak tables */ /* cn: 所有虚表列表 */
  GCObject *tobefnz;  /* list of userdata to be GC */ /* cn: 被GC的用户数据列表 */
  GCObject *fixedgc;  /* list of objects not to be collected */
                        /* cn: 不需要被收集的对象列表 */
  struct lua_State *twups;  /* list of threads with open upvalues */
                            /* cn: 拥有开放的上值的线程列表 */
  unsigned int gcfinnum;  /* number of finalizers to call in each GC step */
                        /* cn: 其他GC步凑中调用到的终结者数量 */
  int gcpause;  /* size of pause between successive GCs */
                /* cn: 在连续的GC过程两次连续间的停顿时间 */
  int gcstepmul;  /* GC 'granularity' */ /* cn: GC程度 */
  lua_CFunction panic;  /* to be called in unprotected errors */
                        /* cn: 在未保护的错误下调用 */
  struct lua_State *mainthread;
  const lua_Number *version;  /* pointer to version number */
                            /* cn: 版本号指针 */
  TString *memerrmsg;  /* memory-error message */ /* cn: 内存错误消息 */
  TString *tmname[TM_N];  /* array with tag-method names */
                        /* cn: 标签方法名数组 */
  struct Table *mt[LUA_NUMTAGS];  /* metatables for basic types */
                                /* cn: 基本类型的元表 */
  TString *strcache[STRCACHE_N][STRCACHE_M];  /* cache for strings in API */
                                            /* cn: api中缓存的字符串 */
} global_State;


/*
** 'per thread' state
** cn: 每个线程的状态
*/
struct lua_State {
  CommonHeader;
  unsigned short nci;  /* number of items in 'ci' list */ 
                        /* cn: ci列表中成员数量 */
  lu_byte status;
  StkId top;  /* first free slot in the stack */
                /* cn: 栈上开始的空闲点 */
  global_State *l_G;
  CallInfo *ci;  /* call info for current function */
                    /* cn: 当前函数的调用信息 */
  const Instruction *oldpc;  /* last pc traced */ /* cn: 上次追溯的点 */
  StkId stack_last;  /* last free slot in the stack */
                    /* cn: 栈最末的空闲点 */
  StkId stack;  /* stack base */
  UpVal *openupval;  /* list of open upvalues in this stack */
                    /* cn: 在当前栈中开放的上值列表 */
  GCObject *gclist;
  struct lua_State *twups;  /* list of threads with open upvalues */
                            /* cn: 拥有开放上值的线程列表 */
  struct lua_longjmp *errorJmp;  /* current error recover point */
                                /* cn: 当前错误恢复点 */
  CallInfo base_ci;  /* CallInfo for first level (C calling Lua) */
                    /* cn: 优先的调用信息（C调用lua） */
  volatile lua_Hook hook;
  ptrdiff_t errfunc;  /* current error handling function (stack index) */
                    /* cn: 当前出错的处理方法（栈的索引） */
  int stacksize;
  int basehookcount;
  int hookcount;
  unsigned short nny;  /* number of non-yieldable calls in stack */
  unsigned short nCcalls;  /* number of nested C calls */
  l_signalT hookmask;
  lu_byte allowhook;
};


#define G(L)	(L->l_G)


/*
** Union of all collectable objects (only for conversions)
*/
union GCUnion {
  GCObject gc;  /* common header */
  struct TString ts;
  struct Udata u;
  union Closure cl;
  struct Table h;
  struct Proto p;
  struct lua_State th;  /* thread */
};


#define cast_u(o)	cast(union GCUnion *, (o))

/* macros to convert a GCObject into a specific value */
#define gco2ts(o)  \
	check_exp(novariant((o)->tt) == LUA_TSTRING, &((cast_u(o))->ts))
#define gco2u(o)  check_exp((o)->tt == LUA_TUSERDATA, &((cast_u(o))->u))
#define gco2lcl(o)  check_exp((o)->tt == LUA_TLCL, &((cast_u(o))->cl.l))
#define gco2ccl(o)  check_exp((o)->tt == LUA_TCCL, &((cast_u(o))->cl.c))
#define gco2cl(o)  \
	check_exp(novariant((o)->tt) == LUA_TFUNCTION, &((cast_u(o))->cl))
#define gco2t(o)  check_exp((o)->tt == LUA_TTABLE, &((cast_u(o))->h))
#define gco2p(o)  check_exp((o)->tt == LUA_TPROTO, &((cast_u(o))->p))
#define gco2th(o)  check_exp((o)->tt == LUA_TTHREAD, &((cast_u(o))->th))


/* macro to convert a Lua object into a GCObject */
/* cn: 将一个lua对象转换为一个GCObject的宏定义 */
#define obj2gco(v) \
	check_exp(novariant((v)->tt) < LUA_TDEADKEY, (&(cast_u(v)->gc)))


/* actual number of total bytes allocated */
#define gettotalbytes(g)	cast(lu_mem, (g)->totalbytes + (g)->GCdebt)

LUAI_FUNC void luaE_setdebt (global_State *g, l_mem debt);
LUAI_FUNC void luaE_freethread (lua_State *L, lua_State *L1);
LUAI_FUNC CallInfo *luaE_extendCI (lua_State *L);
LUAI_FUNC void luaE_freeCI (lua_State *L);
LUAI_FUNC void luaE_shrinkCI (lua_State *L);


#endif

