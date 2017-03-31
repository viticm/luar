/*
** $Id: lgc.h,v 2.91 2015/12/21 13:02:14 roberto Exp $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#ifndef lgc_h
#define lgc_h


#include "lobject.h"
#include "lstate.h"

/*
** Collectable objects may have one of three colors: white, which
** means the object is not marked; gray, which means the
** object is marked, but its references may be not marked; and
** black, which means that the object and all its references are marked.
** The main invariant of the garbage collector, while marking objects,
** is that a black object can never point to a white one. Moreover,
** any gray object must be in a "gray list" (gray, grayagain, weak,
** allweak, ephemeron) so that it can be visited again before finishing
** the collection cycle. These lists have no meaning when the invariant
** is not being enforced (e.g., sweep phase).
*/



/* how much to allocate before next GC step */
#if !defined(GCSTEPSIZE)
/* ~100 small strings */
#define GCSTEPSIZE	(cast_int(100 * sizeof(TString)))
#endif


/*
** Possible states of the Garbage Collector
*/
#define GCSpropagate	0
#define GCSatomic	1
#define GCSswpallgc	2
#define GCSswpfinobj	3
#define GCSswptobefnz	4
#define GCSswpend	5
#define GCScallfin	6
#define GCSpause	7

/* 是否在清除阶段 */
#define issweepphase(g)  \
	(GCSswpallgc <= (g)->gcstate && (g)->gcstate <= GCSswpend)


/*
** macro to tell when main invariant (white objects cannot point to black
** ones) must be kept. During a collection, the sweep
** phase may break the invariant, as objects turned white may point to
** still-black objects. The invariant is restored when sweep ends and
** all objects are white again.
*/

/* cn: 保持不变 */
#define keepinvariant(g)	((g)->gcstate <= GCSatomic)


/*
** some useful bit tricks
** cn: 常用位运算操作（手法）
*/
//重置位，m取反与需要重置的变量与运算后，可以把x的位中与m中不相同的位重置
//example: 0010(m) -> 1101(~m)  1010(x) & ~m -> 1000
//1、需注意m在这里取反后转成了byte，即8位有效
//2、位标记重置的意义是原本标记的转为0，如标记位1000表示第3位标记，取反0111
#define resetbits(x,m)		((x) &= cast(lu_byte, ~(m)))
#define setbits(x,m)		((x) |= (m)) //同重置位的作用，将标记位标记为1
#define testbits(x,m)		((x) & (m)) //只要含有m中的任意一位标记返回大于0的数
#define bitmask(b)		(1<<(b)) //b表示第几位标记（0-n）
#define bit2mask(b1,b2)		(bitmask(b1) | bitmask(b2)) //同时标记两个位
#define l_setbit(x,b)		setbits(x, bitmask(b)) //标记某个位
#define resetbit(x,b)		resetbits(x, bitmask(b)) //重置某个位（取消标记）
#define testbit(x,b)		testbits(x, bitmask(b)) //测试某位是否被标记


/* Layout for bit use in 'marked' field: */
/* cn: 在'marked'字段中使用到的位含义 */
#define WHITE0BIT	0  /* object is white (type 0) */
                        /* cn: 颜色为白色0类型的对象 */
#define WHITE1BIT	1  /* object is white (type 1) */
                        /* cn: 颜色为白色1类型的对象 */
#define BLACKBIT	2  /* object is black */
                        /* cn: 颜色为黑色的对象 */
#define FINALIZEDBIT	3  /* object has been marked for finalization */
                            /* 对象被标记为结束 */
/* bit 7 is currently used by tests (luaL_checkmemory) */
/* 第7位现在使用在测试中 */

#define WHITEBITS	bit2mask(WHITE0BIT, WHITE1BIT) //白色的位数据（0、1）


#define iswhite(x)      testbits((x)->marked, WHITEBITS) //是否为白色对象
#define isblack(x)      testbit((x)->marked, BLACKBIT) //是否为黑色对象
//除去0 1 2之外的所有对象都是灰色对象
#define isgray(x)  /* neither white nor black */  \
	(!testbits((x)->marked, WHITEBITS | bitmask(BLACKBIT))) //是否为灰色对象

//标记对象为最终的
#define tofinalize(x)	testbit((x)->marked, FINALIZEDBIT)

//其他白色的标记，注意这里是异或（两个位同为1标记去除）
//这个宏是来检测是否为白色0和1其中一个类型，屏蔽了两种类型混合的情况
#define otherwhite(g)	((g)->currentwhite ^ WHITEBITS)
//m是否是一个死亡的标记，ow表示白色标记，m异或白色标记？ m == ow
#define isdeadm(ow,m)	(!(((m) ^ WHITEBITS) & (ow)))
//变量是否死亡
#define isdead(g,v)	isdeadm(otherwhite(g), (v)->marked)

#define changewhite(x)	((x)->marked ^= WHITEBITS) //白色改变
#define gray2black(x)	l_setbit((x)->marked, BLACKBIT) //灰色转黑色

#define luaC_white(g)	cast(lu_byte, (g)->currentwhite & WHITEBITS) //白色标记位


/*
** Does one step of collection when debt becomes positive. 'pre'/'pos'
** allows some adjustments to be done only when needed. macro
** 'condchangemem' is used only for heavy tests (forcing a full
** GC cycle on every opportunity) 
** cn:
** 当debt为正时进行一次收集步凑
** 'pre'/'pos'可以在必要时允许进行一些调整
** 宏'condchangemem'仅仅使用在一些重量级的测试中
** (强制一个完整的回收在每一个尽可能的时机中)
*/
#define luaC_condGC(L,pre,pos) \
	{ if (G(L)->GCdebt > 0) { pre; luaC_step(L); pos;}; \
	  condchangemem(L,pre,pos); }

/* more often than not, 'pre'/'pos' are empty */
/* cn: 通常之下'pre'/'pos'为空 */
#define luaC_checkGC(L)		luaC_condGC(L,(void)0,(void)0)

/* cn: 界定相关的一些宏定义，这些定义比较复杂，因为它们牵涉了其他的许多模块 */

//cn: 分界定义
// p 表示一个参数 v 表示关联在p上的某个值
#define luaC_barrier(L,p,v) (  \
	(iscollectable(v) && isblack(p) && iswhite(gcvalue(v))) ?  \
	luaC_barrier_(L,obj2gco(p),gcvalue(v)) : cast_void(0))

//cn: 反分界定义
#define luaC_barrierback(L,p,v) (  \
	(iscollectable(v) && isblack(p) && iswhite(gcvalue(v))) ? \
	luaC_barrierback_(L,p) : cast_void(0))

//cn: 对象分界
#define luaC_objbarrier(L,p,o) (  \
	(isblack(p) && iswhite(o)) ? \
	luaC_barrier_(L,obj2gco(p),obj2gco(o)) : cast_void(0))

//cn: 上值分界
#define luaC_upvalbarrier(L,uv) ( \
	(iscollectable((uv)->v) && !upisopen(uv)) ? \
         luaC_upvalbarrier_(L,uv) : cast_void(0))

LUAI_FUNC void luaC_fix (lua_State *L, GCObject *o); //对象修复，将o从L中移除
LUAI_FUNC void luaC_freeallobjects (lua_State *L); //释放L中所有对象
LUAI_FUNC void luaC_step (lua_State *L); //步凑处理，主要是在循环中进行回收处理
LUAI_FUNC void luaC_runtilstate (lua_State *L, int statesmask);
LUAI_FUNC void luaC_fullgc (lua_State *L, int isemergency);
LUAI_FUNC GCObject *luaC_newobj (lua_State *L, int tt, size_t sz);
LUAI_FUNC void luaC_barrier_ (lua_State *L, GCObject *o, GCObject *v);
LUAI_FUNC void luaC_barrierback_ (lua_State *L, Table *o);
LUAI_FUNC void luaC_upvalbarrier_ (lua_State *L, UpVal *uv);
LUAI_FUNC void luaC_checkfinalizer (lua_State *L, GCObject *o, Table *mt);
LUAI_FUNC void luaC_upvdeccount (lua_State *L, UpVal *uv); //上值数量减少


#endif
