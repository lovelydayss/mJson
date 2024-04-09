#include "leptjson.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 此处定义而非头文件中实现封装 */
/* static 函数只有当前文件可见 */

/* Json 解析栈 */
#ifndef LEPT_PARSE_STACK_INIT_SIZE
#define LEPT_PARSE_STACK_INIT_SIZE 256
#endif

/* Json 生成缓冲区定义 */
#ifndef LEPT_PARSE_STRINGIFY_INIT_SIZE
#define LEPT_PARSE_STRINGIFY_INIT_SIZE 256
#endif

#define EXPECT(c, ch)             \
	do {                          \
		assert(*c->json == (ch)); \
		c->json++;                \
	} while (0)

#define ISDIGIT(ch) ((ch) >= '0' && (ch) <= '9')
#define ISDIGIT1TO9(ch) ((ch) >= '1' && (ch) <= '9')
#define PUTC(c, ch)                                        \
	do {                                                   \
		*(char*)lept_context_push(c, sizeof(char)) = (ch); \
	} while (0)
#define PUTS(c, s, len)                            \
	do {                                           \
		memcpy(lept_context_push(c, len), s, len); \
	} while (0)
#define STRING_ERROR(ret) \
	do {                  \
		c->top = head;    \
		return ret;       \
	} while (0)

typedef struct {
	const char* json;
	char* stack;
	size_t size, top;
} lept_context;

/* 释放 stack 空间 */
static void lept_context_free(lept_context* c);

/* 入栈 */
static void* lept_context_push(lept_context* c, size_t size);

/* 出栈 */
static void* lept_context_pop(lept_context* c, size_t size);

/* ws = *(%x20 / %x09 / %x0A / %x0D) */
static void lept_parse_whitespace(lept_context* c);

/* 重构 null false true */
static int lept_parse_literal(lept_context* c, lept_value* v,
                              const char* literal, lept_type type);

/* if 0 ...... #endif 禁用代码 */
#if 0
/* null  = "null" */
static int lept_parse_null(lept_context* c, lept_value* v);

/* false  = "false" */
static int lept_parse_false(lept_context* c, lept_value* v);

/* true  = "true" */
static int lept_parse_true(lept_context* c, lept_value* v);
#endif

/* number = [ "-" ] int [ frac ] [ exp ] */
static int lept_parse_number(lept_context* c, lept_value* v);

/* 解析十六进制编码，转为十进制数值  */
static const char* lept_parse_hex4(const char* p, unsigned* u);

/* unicode 编码解析为 utf8 */
static void lept_encode_utf8(lept_context* c, unsigned u);

/* 重构 string 解析函数 */
/* 将 string 解析和装载分离 */
static int lept_parse_string_raw(lept_context* c, char** str, size_t* len);

/* string = "\"......\"" */
static int lept_parse_string(lept_context* c, lept_value* v);

/* array = "[......]" */
static int lept_parse_array(lept_context* c, lept_value* v);

/* object = "{......}"" */
static int lept_parse_object(lept_context* c, lept_value* v);

/* value = null / false / true / number / string / array / object */
static int lept_parse_value(lept_context* c, lept_value* v);

/* 生成字符串 string */
static void lept_stringify_string(lept_context* c, const char* s, size_t len);

/* 生成 Json */
static void lept_stringify_value(lept_context* c, const lept_value* v);

/*******************************/
/* 此后为头文件中定义函数具体实现 */
/*******************************/

int lept_parse(lept_value* v, const char* json) {

	assert(v != NULL);
	lept_value_init(v);

	lept_context* c = (lept_context*)malloc(sizeof(lept_context));
	c->json = json;
	c->stack = NULL;
	c->size = c->top = 0;

	lept_parse_whitespace(c);
	int ret = lept_parse_value(c, v);

	/* 完成解析后处理，对 LEPT_PARSE_ROOT_NOT_SINGULAR 情况进行判断 */
	if (ret == LEPT_PARSE_OK) {
		lept_parse_whitespace(c);
		if (*(c->json) != '\0') {
			/* 此时解析已经完成，需要将 v 的值置空处理掉 */
			ret = LEPT_PARSE_ROOT_NOT_SINGULAR;
			v->type = LEPT_NULL;
		}
	}

	lept_context_free(c);

	return ret;
}

char* lept_stringify(const lept_value* v, size_t* length) {
	lept_context c;
	assert(v != NULL);
	c.stack = (char*)malloc(c.size = LEPT_PARSE_STRINGIFY_INIT_SIZE);
	c.top = 0;
	lept_stringify_value(&c, v);
	if (length)
		*length = c.top;
	PUTC(&c, '\0');
	return c.stack;
}

void lept_copy(lept_value* dst, const lept_value* src) {
	assert(src != NULL && dst != NULL && src != dst);
	size_t i;
	switch (src->type) {
	case LEPT_STRING:
		lept_set_string(dst, src->u.s.s, src->u.s.len);
		break;
	case LEPT_ARRAY:
		lept_set_array(dst, src->u.a.capacity);

		/* 递归处理 */
		/* 逐个调用 push_back 函数插入 */
		for (i = 0; i < src->u.a.size; i++)
			lept_pushback_array_element(dst, &src->u.a.e[i]);

		break;
	case LEPT_OBJECT:
		lept_set_object(dst, src->u.o.capacity);

		/* 递归处理 */
		/* 调用 set_object_value_by_key 函数插入 */
		for (i = 0; i < src->u.o.size; i++) {
			lept_value* val = (lept_value*)malloc(sizeof(lept_value));
			lept_value_init(val);

			/* key-value */
			lept_copy(val, &src->u.o.m[i].v);
			lept_set_object_value_by_key(dst, src->u.o.m[i].k,
			                             src->u.o.m[i].klen, val);

			/* lept_free 只进行了内部空间的释放 */
			lept_free(val);
			free_ptr(val);
		}
		break;
	default:
		lept_free(dst);
		memcpy(dst, src, sizeof(lept_value));
		break;
	}
}
void lept_move(lept_value* dst, lept_value* src) {
	assert(dst != NULL && src != NULL && src != dst);
	lept_free(dst);
	memcpy(dst, src, sizeof(lept_value));
	lept_value_init(src);
}
void lept_swap(lept_value* lhs, lept_value* rhs) {
	assert(lhs != NULL && rhs != NULL);
	if (lhs != rhs) {
		lept_value temp;
		memcpy(&temp, lhs, sizeof(lept_value));
		memcpy(lhs, rhs, sizeof(lept_value));
		memcpy(rhs, &temp, sizeof(lept_value));
	}
}

void lept_free(lept_value* v) {
	/* 保证释放对象经过初始化 */
	assert(v != NULL && v->type >= LEPT_NULL);

	size_t i;
	switch (v->type) {
	/* string 处理 */
	case LEPT_STRING:
		free_ptr(v->u.s.s);
		break;
	/* array 处理 */
	case LEPT_ARRAY:
		/* 只有在 size 范围内元素才需要递归处理 */
		for (i = 0; i < v->u.a.size; i++)
			lept_free(&v->u.a.e[i]);

		free_ptr(v->u.a.e);
		break;
	case LEPT_OBJECT:
		/* 只有在 size 范围内元素才需要递归处理 */
		for (i = 0; i < v->u.o.size; i++) {
			free_ptr(v->u.o.m[i].k);
			lept_free(&v->u.o.m[i].v);
		}
		free_ptr(v->u.o.m);
		break;
	default:
		break;
	}

	v->type = LEPT_NULL;
}

lept_type lept_get_type(const lept_value* v) { return v->type; }

int lept_is_equal(const lept_value* lhs, const lept_value* rhs) {
	size_t i;
	assert(lhs != NULL && rhs != NULL);
	if (lhs->type != rhs->type)
		return 0;
	switch (lhs->type) {
	case LEPT_STRING:
		return lhs->u.s.len == rhs->u.s.len &&
		       memcmp(lhs->u.s.s, rhs->u.s.s, lhs->u.s.len) == 0;
	case LEPT_NUMBER:
		return lhs->u.n == rhs->u.n;
	case LEPT_ARRAY:
		if (lhs->u.a.size != rhs->u.a.size)
			return 0;
		for (i = 0; i < lhs->u.a.size; i++)
			if (!lept_is_equal(&lhs->u.a.e[i], &rhs->u.a.e[i]))
				return 0;
		return 1;
	case LEPT_OBJECT:
		/* size comp */
		if (lhs->u.o.size != rhs->u.o.size)
			return 0;

		/* key-value comp */
		for (i = 0; i < lhs->u.o.size; i++) {
			size_t index = lept_find_object_index(rhs, lhs->u.o.m[i].k,
			                                      lhs->u.o.m[i].klen);
			if (index == LEPT_KEY_NOT_EXIST)
				return 0;

			if (!lept_is_equal(&lhs->u.o.m[i].v, &rhs->u.o.m[index].v))
				return 0;
		}
		return 1;
	default:
		return 1;
	}
}

/* bollean */

int lept_get_boolean(const lept_value* v) {
	assert(v != NULL && (v->type == LEPT_FALSE || v->type == LEPT_TRUE));
	return v->type == LEPT_TRUE;
}
void lept_set_boolean(lept_value* v, int b) {

	lept_free(v);
	v->type = (b > 0 ? LEPT_TRUE : LEPT_FALSE); /* 此处设置非 0 即为 true */
}

/* double */

double lept_get_number(const lept_value* v) {
	assert(v != NULL && v->type == LEPT_NUMBER);
	return v->u.n;
}
void lept_set_number(lept_value* v, double n) {

	lept_free(v);
	v->type = LEPT_NUMBER;
	v->u.n = n;
}

/* string */

const char* lept_get_string(const lept_value* v) {
	assert(v != NULL && v->type == LEPT_STRING);
	return v->u.s.s;
}
size_t lept_get_string_length(const lept_value* v) {
	assert(v != NULL && v->type == LEPT_STRING);
	return v->u.s.len;
}
void lept_set_string(lept_value* v, const char* s, size_t len) {

	assert(v != NULL && (s != NULL || len == 0));
	lept_free(v);

	/* 执行深拷贝 */
	v->u.s.s = (char*)malloc(len + 1);
	memcpy(v->u.s.s, s, len);

	/* 补充尾部 '\0' 字符 */
	v->u.s.s[len] = '\0';
	v->u.s.len = len;
	v->type = LEPT_STRING;
}

/* array */

void lept_set_array(lept_value* v, size_t capacity) {

	assert(v != NULL);
	lept_free(v);

	v->type = LEPT_ARRAY;
	v->u.a.size = 0;
	v->u.a.capacity = capacity;
	v->u.a.e = capacity > 0 ? (lept_value*)malloc(capacity * sizeof(lept_value))
	                        : NULL;
	return;
}

size_t lept_get_array_size(const lept_value* v) {
	assert(v != NULL && v->type == LEPT_ARRAY);
	return v->u.a.size;
}
size_t lept_get_array_capacity(const lept_value* v) {
	assert(v != NULL && v->type == LEPT_ARRAY);
	return v->u.a.capacity;
}

void lept_reserve_array(lept_value* v, size_t capacity) {
	assert(v != NULL && v->type == LEPT_ARRAY);
	if (v->u.a.capacity < capacity) {
		v->u.a.capacity = capacity;
		v->u.a.e =
		    (lept_value*)realloc(v->u.a.e, capacity * sizeof(lept_value));
	}
}
/* 这直接把 capacity 设置成 size 大小 */
void lept_shrink_array(lept_value* v) {
	assert(v != NULL && v->type == LEPT_ARRAY);
	if (v->u.a.capacity > v->u.a.size) {
		v->u.a.capacity = v->u.a.size;
		v->u.a.e = (lept_value*)realloc(v->u.a.e,
		                                v->u.a.capacity * sizeof(lept_value));
	}
}
void lept_clear_array(lept_value* v) {
	assert(v != NULL && v->type == LEPT_ARRAY);
	lept_erase_array_element(v, 0, v->u.a.size);

	/* clear 时容量剩余值不会改变 */
	/* 需使用 lept_shrink_array 手动释放剩余内存 */

	/* lept_shrink_array(v); */
}

const lept_value* lept_get_array_element(const lept_value* v, size_t index) {
	assert(v != NULL && v->type == LEPT_ARRAY);
	assert(index < v->u.a.size);
	return &v->u.a.e[index];
}
void lept_pushback_array_element(lept_value* v, const lept_value* e) {
	assert(v != NULL && e != NULL && v->type == LEPT_ARRAY);
	if (v->u.a.size == v->u.a.capacity)
		lept_reserve_array(v, v->u.a.capacity == 0 ? 1 : v->u.a.capacity * 2);

	/* copy 前执行 init */
	lept_value_init((v->u.a.e) + v->u.a.size);
	lept_copy((v->u.a.e) + v->u.a.size, e);
	v->u.a.size++;
}
void lept_popback_array_element(lept_value* v) {
	assert(v != NULL && v->type == LEPT_ARRAY && v->u.a.size > 0);
	v->u.a.size--;
	lept_free((v->u.a.e) + v->u.a.size);
}
void lept_insert_array_element(lept_value* v, const lept_value* e,
                               size_t index) {
	assert(v != NULL && v->type == LEPT_ARRAY && index <= v->u.a.size);

	/* 首先插入尾部，再进行调整 */
	/* 性能较差，但便于实现吧 */
	lept_pushback_array_element(v, e);

	/* 当插入后元素数目为 1，不需要调整 */
	if (v->u.a.size == 1)
		return;

	size_t i;
	/* 调整位置 */
	for (i = v->u.a.size - 1; i != index; i--)
		lept_swap((v->u.a.e) + i - 1, (v->u.a.e) + i);
}
void lept_erase_array_element(lept_value* v, size_t index, size_t count) {

	assert(v != NULL && v->type == LEPT_ARRAY && index + count <= v->u.a.size);

	/* 分配删除后数组大小两倍的空间，如果删除后 size 为 0 则分配一个空间 */
	size_t new_size = v->u.a.size - count;
	size_t new_capacity = 2 * new_size + 1;

	size_t i;

	/* 释放删除元素堆空间 */
	for (i = index; i < index + count; i++)
		lept_free(v->u.a.e + i);

	/* 调整右侧区间值位置 */
	/* 只需将右侧区间逐一移过来即可，无需考虑其他 */
	for (i = index + count; i < v->u.a.size; i++)
		memcpy((v->u.a.e) + i - count, (v->u.a.e) + i, sizeof(lept_value));

	v->u.a.size = new_size;

	/* 调整容量值 */
	if (new_capacity < v->u.a.capacity) {
		v->u.a.capacity = new_capacity;
		v->u.a.e =
		    (lept_value*)realloc(v->u.a.e, new_capacity * sizeof(lept_value));
	}
}

/* object */
/* 必须初始化后才允许调用 */
void lept_set_object(lept_value* v, size_t capacity) {

	assert(v != NULL);
	lept_free(v);

	v->type = LEPT_OBJECT;
	v->u.o.size = 0;
	v->u.o.capacity = capacity;
	v->u.o.m = capacity > 0
	               ? (lept_member*)malloc(capacity * sizeof(lept_member))
	               : NULL;
	return;
}

size_t lept_get_object_size(const lept_value* v) {
	assert(v != NULL && v->type == LEPT_OBJECT);
	return v->u.o.size;
}
size_t lept_get_object_capacity(const lept_value* v) {
	assert(v != NULL && v->type == LEPT_OBJECT);
	return v->u.o.capacity;
	return 0;
}

void lept_reserve_object(lept_value* v, size_t capacity) {
	assert(v != NULL && v->type == LEPT_OBJECT);
	if (v->u.o.capacity < capacity) {
		v->u.o.capacity = capacity;
		v->u.o.m =
		    (lept_member*)realloc(v->u.o.m, capacity * sizeof(lept_member));
	}
}

void lept_shrink_object(lept_value* v) {
	assert(v != NULL && v->type == LEPT_OBJECT);
	if (v->u.o.capacity > v->u.o.size) {
		v->u.o.capacity = v->u.o.size;
		v->u.o.m = (lept_member*)realloc(v->u.o.m,
		                                 v->u.o.capacity * sizeof(lept_member));
	}
}
void lept_clear_object(lept_value* v) {
	assert(v != NULL && v->type == LEPT_OBJECT);

	/* 懒得去实现了，就逐个删了，可能对性能有影响  */
	/* 每次均删除最后值，降低元素移动次数 */

	size_t i;
	while (v->u.o.size != 0)
		lept_remove_object_value_by_index(v, v->u.o.size - 1);

	/* clear 函数只清空，对于容量等不做处理 */
	/* lept_shrink_object(v); */
}

const char* lept_get_object_key(const lept_value* v, size_t index) {
	assert(v != NULL && v->type == LEPT_OBJECT);
	assert(index < v->u.o.size);
	return v->u.o.m[index].k;
}
size_t lept_get_object_key_length(const lept_value* v, size_t index) {
	assert(v != NULL && v->type == LEPT_OBJECT);
	assert(index < v->u.o.size);
	return v->u.o.m[index].klen;
}

const lept_value* lept_get_object_value_by_index(const lept_value* v,
                                                 size_t index) {
	assert(v != NULL && v->type == LEPT_OBJECT);
	assert(index < v->u.o.size);
	return &v->u.o.m[index].v;
}
const lept_value* lept_get_object_value_by_key(const lept_value* v,
                                               const char* key, size_t klen) {
	return lept_find_object_value(v, key, klen);
}

int lept_remove_object_value_by_index(lept_value* v, size_t index) {
	assert(v != NULL && v->type == LEPT_OBJECT);

	if (index >= v->u.o.size)
		return OBJECT_INDEX_WRONG;

	v->u.o.size--;
	size_t new_capacity = 2 * v->u.o.size + 1;

	/* 当前位置元素空间释放 */
	lept_member* ptr = v->u.o.m + index;
	free_ptr(ptr->k);
	lept_free(&(ptr->v));

	/* 移动各元素值 */
	size_t i;
	for (i = index; i < v->u.o.size; i++) {
		memcpy(v->u.o.m + i, v->u.o.m + i + 1, sizeof(lept_member));
	}

	/* 调整容量值 */
	if (new_capacity < v->u.o.capacity) {
		v->u.o.capacity = new_capacity;
		v->u.o.m =
		    (lept_member*)realloc(v->u.o.m, new_capacity * sizeof(lept_member));
	}

	return REMOVE_OBJECT_OK;
}
int lept_remove_object_value_by_key(lept_value* v, const char* key,
                                    size_t klen) {
	size_t index = lept_find_object_index(v, key, klen);

	if (index == LEPT_KEY_NOT_EXIST)
		return OBJECT_INDEX_WRONG;

	lept_remove_object_value_by_index(v, index);
	return REMOVE_OBJECT_OK;
}

int lept_set_object_value_by_index(lept_value* v, size_t index,
                                   const lept_value* s_v) {
	assert(v != NULL && v->type == LEPT_OBJECT);

	if (index >= v->u.o.size)
		return OBJECT_INDEX_WRONG;

	lept_copy(&((v->u.o.m + index)->v), s_v);
	return MODIFY_OBJECT_OK;
}
int lept_set_object_value_by_key(lept_value* v, const char* key, size_t klen,
                                 const lept_value* s_v) {
	size_t index = lept_find_object_index(v, key, klen);

	/* 当不存在时应该执行插入 */
	if (index == LEPT_KEY_NOT_EXIST) {

		/* 扩容 */
		if (v->u.o.size == v->u.o.capacity)
			lept_reserve_object(v,
			                    v->u.o.capacity == 0 ? 1 : v->u.o.capacity * 2);

		/* member 类型的复制 */

		lept_member* ptr = (v->u.o.m) + v->u.o.size;

		/* 此处必为未初始化 ptr->k，直接分配即可 */
		ptr->k = (char*)malloc(klen + 1);

		memcpy(ptr->k, key, klen);
		ptr->k[klen] = '\0';
		ptr->klen = klen;

		/* copy 前执行 init */
		lept_value_init(&(ptr->v));
		lept_copy(&(ptr->v), s_v);
		v->u.o.size++;

		return INSERT_OBJECT_OK;
	}

	return lept_set_object_value_by_index(v, index, s_v);
}

size_t lept_find_object_index(const lept_value* v, const char* key,
                              size_t klen) {
	assert(v != NULL && v->type == LEPT_OBJECT && key != NULL);

	size_t i;
	for (i = 0; i < v->u.o.size; i++)
		if (v->u.o.m[i].klen == klen && memcmp(v->u.o.m[i].k, key, klen) == 0)
			return i;

	return LEPT_KEY_NOT_EXIST;
}
const lept_value* lept_find_object_value(const lept_value* v, const char* key,
                                         size_t klen) {
	size_t index = lept_find_object_index(v, key, klen);
	return index != LEPT_KEY_NOT_EXIST ? &v->u.o.m[index].v : NULL;
}

/*******************************/
/* 此后为本文件处定义函数具体实现 */
/*******************************/

static void lept_context_free(lept_context* c) {
	if (c != NULL && c->stack != NULL) {
		free_ptr(c->stack);
	}

	if (c != NULL) {
		free_ptr(c);
	}
}

static void* lept_context_push(lept_context* c, size_t size) {
	void* ret;
	assert(size > 0);

	/* 栈空间不足时执行 1.5 倍扩充 */
	if (c->top + size >= c->size) {
		if (c->size == 0)
			c->size = LEPT_PARSE_STACK_INIT_SIZE;
		while (c->top + size >= c->size)
			c->size += c->size >> 1;
		c->stack = (char*)realloc(c->stack, c->size);
	}

	ret = c->stack + c->top;
	c->top += size;
	return ret;
}

static void* lept_context_pop(lept_context* c, size_t size) {
	assert(c->top >= size);
	return c->stack + (c->top -= size);
}

static void lept_parse_whitespace(lept_context* c) {
	const char* p = c->json;
	while (*p == ' ' || *p == '\t' || *p == '\r')
		p++;
	c->json = p;
}

static int lept_parse_literal(lept_context* c, lept_value* v,
                              const char* literal, lept_type type) {
	size_t i;
	EXPECT(c, literal[0]);

	for (i = 0; literal[i + 1]; i++)
		if (c->json[i] != literal[i + 1])
			return LEPT_PARSE_INVALID_VALUE;

	c->json += i;
	v->type = type;
	return LEPT_PARSE_OK;
}

#if 0
static int lept_parse_null(lept_context* c, lept_value* v) {
	EXPECT(c, 'n');
	if (c->json[0] != 'u' || c->json[1] != 'l' || c->json[2] != 'l')
		return LEPT_PARSE_INVALID_VALUE;

	c->json += 3;
	v->type = LEPT_NULL;
	return LEPT_PARSE_OK;
}

static int lept_parse_false(lept_context* c, lept_value* v) {
	EXPECT(c, 'f');
	if (c->json[0] != 'a' || c->json[1] != 'l' || c->json[2] != 's' ||
	    c->json[3] != 'e')
		return LEPT_PARSE_INVALID_VALUE;

	c->json += 4;
	v->type = LEPT_FALSE;
	return LEPT_PARSE_OK;
}

static int lept_parse_true(lept_context* c, lept_value* v) {
	EXPECT(c, 't');
	if (c->json[0] != 'r' || c->json[1] != 'u' || c->json[2] != 'e')
		return LEPT_PARSE_INVALID_VALUE;

	c->json += 3;
	v->type = LEPT_TRUE;
	return LEPT_PARSE_OK;
}
#endif

static int lept_parse_number(lept_context* c, lept_value* v) {
	const char* p = c->json;
	if (*p == '-')
		p++;
	if (*p == '0')
		p++;
	else {
		if (!ISDIGIT1TO9(*p))
			return LEPT_PARSE_INVALID_VALUE;
		for (p++; ISDIGIT(*p); p++)
			;
	}
	if (*p == '.') {
		p++;
		if (!ISDIGIT(*p))
			return LEPT_PARSE_INVALID_VALUE;
		for (p++; ISDIGIT(*p); p++)
			;
	}
	if (*p == 'E' || *p == 'e') {
		p++;
		if (*p == '+' || *p == '-')
			p++;
		if (!ISDIGIT(*p))
			return LEPT_PARSE_INVALID_VALUE;
		for (p++; ISDIGIT(*p); p++)
			;
	}

	/* strtod endptr 指向转换后数字字符串后一个位置 */
	errno = 0;
	v->u.n = strtod(c->json, NULL);

	/* 数字上界和下界 */
	if (errno == ERANGE && (v->u.n == HUGE_VAL || v->u.n == -HUGE_VAL))
		return LEPT_PARSE_NUMBER_TOO_BIG;

	c->json = p;
	v->type = LEPT_NUMBER;
	return LEPT_PARSE_OK;
}

static const char* lept_parse_hex4(const char* p, unsigned* u) {
	int i = 0;
	*u = 0;

	/* 4 位 16 进制数字 */
	for (i = 0; i < 4; i++) {
		char ch = *p++;
		*u <<= 4;

		/* 使用位操作求解 */
		if (ch >= '0' && ch <= '9')
			*u |= ch - '0';
		else if (ch >= 'A' && ch <= 'F')
			*u |= ch - ('A' - 10);
		else if (ch >= 'a' && ch <= 'f')
			*u |= ch - ('a' - 10);
		else
			return NULL;
	}

	return p;
}

static void lept_encode_utf8(lept_context* c, unsigned u) {
	if (u <= 0x7F)
		PUTC(c, u & 0xFF);
	else if (u <= 0x7FF) {
		PUTC(c, 0xC0 | ((u >> 6) & 0xFF));
		PUTC(c, 0x80 | (u & 0x3F));
	} else if (u <= 0xFFFF) {
		PUTC(c, 0xE0 | ((u >> 12) & 0xFF));
		PUTC(c, 0x80 | ((u >> 6) & 0x3F));
		PUTC(c, 0x80 | (u & 0x3F));
	} else {
		assert(u <= 0x10FFFF);
		PUTC(c, 0xF0 | ((u >> 18) & 0xFF));
		PUTC(c, 0x80 | ((u >> 12) & 0x3F));
		PUTC(c, 0x80 | ((u >> 6) & 0x3F));
		PUTC(c, 0x80 | (u & 0x3F));
	}
}

static int lept_parse_string_raw(lept_context* c, char** str, size_t* len) {
	size_t head = c->top;
	unsigned u, u2;
	const char* p;
	EXPECT(c, '\"');
	p = c->json;
	for (;;) {
		char ch = *p++;
		switch (ch) {
		case '\"':
			*len = c->top - head;
			*str = lept_context_pop(c, *len);
			c->json = p;
			return LEPT_PARSE_OK;
		case '\\':
			/* 转义字符处理 */
			switch (*p++) {
			case '\"':
				PUTC(c, '\"');
				break;
			case '\\':
				PUTC(c, '\\');
				break;
			case '/':
				PUTC(c, '/');
				break;
			case 'b':
				PUTC(c, '\b');
				break;
			case 'f':
				PUTC(c, '\f');
				break;
			case 'n':
				PUTC(c, '\n');
				break;
			case 'r':
				PUTC(c, '\r');
				break;
			case 't':
				PUTC(c, '\t');
				break;
			case 'u':
				if (!(p = lept_parse_hex4(p, &u)))
					STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_HEX);
				if (u >= 0xD800 && u <= 0xDBFF) {
					if (*p++ != '\\')
						STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
					if (*p++ != 'u')
						STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
					if (!(p = lept_parse_hex4(p, &u2)))
						STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_HEX);
					if (u2 < 0xDC00 || u2 > 0xDFFF)
						STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
					u = (((u - 0xD800) << 10) | (u2 - 0xDC00)) + 0x10000;
				}
				lept_encode_utf8(c, u);
				break;
			default:
				STRING_ERROR(LEPT_PARSE_INVALID_STRING_ESCAPE);
			}
			break;
		case '\0':
			STRING_ERROR(LEPT_PARSE_MISS_QUOTATION_MARK);
		default:
			/* 不合法字符处理 */
			/* unescaped = %x20-21 / %x23-5B / %x5D-10FFFF */
			if ((unsigned char)ch < 0x20)
				STRING_ERROR(LEPT_PARSE_INVALID_STRING_CHAR);

			PUTC(c, ch);
		}
	}
}

static int lept_parse_string(lept_context* c, lept_value* v) {
	char* s;
	size_t len;
	int ret = lept_parse_string_raw(c, &s, &len);
	if (ret == LEPT_PARSE_OK)
		lept_set_string(v, s, len);
	return ret;
}

static int lept_parse_array(lept_context* c, lept_value* v) {
	size_t i, size = 0;
	int ret;
	EXPECT(c, '[');
	lept_parse_whitespace(c);

	/* 空类型数组解析 */
	if (*c->json == ']') {
		c->json++;
		lept_set_array(v, 0);
		return LEPT_PARSE_OK;
	}

	for (;;) {
		lept_value e;
		lept_value_init(&e);
		ret = lept_parse_value(c, &e);
		if (ret != LEPT_PARSE_OK)
			break;

		memcpy(lept_context_push(c, sizeof(lept_value)), &e,
		       sizeof(lept_value));
		size++;
		lept_parse_whitespace(c);
		if (*c->json == ',') {
			c->json++;
			lept_parse_whitespace(c);
		} else if (*c->json == ']') {
			c->json++;
			lept_set_array(v, size);
			memcpy(v->u.a.e, lept_context_pop(c, size * sizeof(lept_value)),
			       size * sizeof(lept_value));
			v->u.a.size = size;

			return LEPT_PARSE_OK;
		} else {
			ret = LEPT_PARSE_MISS_COMMA_OR_SQUARE_BRACKET;
			break;
		}
	}

	/* 此处释放栈空间主要目的在于对栈进行复原 */
	for (i = 0; i < size; i++)
		lept_free((lept_value*)lept_context_pop(c, sizeof(lept_value)));

	return ret;
}

static int lept_parse_object(lept_context* c, lept_value* v) {
	size_t i, size;
	lept_member m;

	int ret;
	EXPECT(c, '{');
	lept_parse_whitespace(c);

	/* 空对象处理 */
	/* 可以考虑将空初始化抽象成函数 */
	if (*c->json == '}') {
		c->json++;
		lept_set_object(v, 0);
		return LEPT_PARSE_OK;
	}

	m.k = NULL;
	size = 0;
	for (;;) {
		char* str;
		lept_value_init(&m.v);

		/* 解析 key */
		if (*c->json != '"') {
			ret = LEPT_PARSE_MISS_KEY;
			break;
		}
		ret = lept_parse_string_raw(c, &str, &m.klen);
		if (ret != LEPT_PARSE_OK)
			break;

		m.k = (char*)malloc(m.klen + 1);

		memcpy(m.k, str, m.klen);
		m.k[m.klen] = '\0';

		/* 解析中间 : */
		lept_parse_whitespace(c);
		if (*c->json != ':') {
			ret = LEPT_PARSE_MISS_COLON;
			break;
		}
		c->json++;
		lept_parse_whitespace(c);

		/* 解析对象值 */
		ret = lept_parse_value(c, &m.v);
		if (ret != LEPT_PARSE_OK)
			break;
		memcpy(lept_context_push(c, sizeof(lept_member)), &m,
		       sizeof(lept_member));
		size++;
		m.k = NULL; /* ownership is transferred to member on stack */

		/* parse ws [comma | right-curly-brace] ws */
		lept_parse_whitespace(c);
		if (*c->json == ',') {
			c->json++;
			lept_parse_whitespace(c);
		} else if (*c->json == '}') {
			size_t s = sizeof(lept_member) * size;
			c->json++;
			lept_set_object(v, size);
			memcpy(v->u.o.m, lept_context_pop(c, sizeof(lept_member) * size),
			       sizeof(lept_member) * size);
			v->u.o.size = size;
			return LEPT_PARSE_OK;
		} else {
			ret = LEPT_PARSE_MISS_COMMA_OR_CURLY_BRACKET;
			break;
		}
	}
	/* Pop and free members on the stack */
	free_ptr(m.k);
	for (i = 0; i < size; i++) {
		lept_member* m = (lept_member*)lept_context_pop(c, sizeof(lept_member));
		free_ptr(m->k);

		lept_free(&m->v);
	}
	v->type = LEPT_NULL;
	return ret;
}

static int lept_parse_value(lept_context* c, lept_value* v) {
	switch (*c->json) {
	case 'n':
		return lept_parse_literal(c, v, "null", LEPT_NULL);
	case 'f':
		return lept_parse_literal(c, v, "false", LEPT_FALSE);
	case 't':
		return lept_parse_literal(c, v, "true", LEPT_TRUE);
	case '"':
		return lept_parse_string(c, v);
	case '[':
		return lept_parse_array(c, v);
	case '{':
		return lept_parse_object(c, v);
	case '\0':
		return LEPT_PARSE_EXPECT_VALUE;
	default:
		return lept_parse_number(c, v);
	}
}

static void lept_stringify_string(lept_context* c, const char* s, size_t len) {
	static const char hex_digits[] = {'0', '1', '2', '3', '4', '5', '6', '7',
	                                  '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
	size_t i, size;
	char *head, *p;
	assert(s != NULL);
	p = head = lept_context_push(c, size = len * 6 + 2); /* "\u00xx..." */
	*p++ = '"';
	for (i = 0; i < len; i++) {
		unsigned char ch = (unsigned char)s[i];
		switch (ch) {
		case '\"':
			*p++ = '\\';
			*p++ = '\"';
			break;
		case '\\':
			*p++ = '\\';
			*p++ = '\\';
			break;
		case '\b':
			*p++ = '\\';
			*p++ = 'b';
			break;
		case '\f':
			*p++ = '\\';
			*p++ = 'f';
			break;
		case '\n':
			*p++ = '\\';
			*p++ = 'n';
			break;
		case '\r':
			*p++ = '\\';
			*p++ = 'r';
			break;
		case '\t':
			*p++ = '\\';
			*p++ = 't';
			break;
		default:
			if (ch < 0x20) {
				*p++ = '\\';
				*p++ = 'u';
				*p++ = '0';
				*p++ = '0';
				*p++ = hex_digits[ch >> 4];
				*p++ = hex_digits[ch & 15];
			} else
				*p++ = s[i];
		}
	}
	*p++ = '"';
	c->top -= size - (p - head);
}

static void lept_stringify_value(lept_context* c, const lept_value* v) {
	size_t i;
	switch (v->type) {
	case LEPT_NULL:
		PUTS(c, "null", 4);
		break;
	case LEPT_FALSE:
		PUTS(c, "false", 5);
		break;
	case LEPT_TRUE:
		PUTS(c, "true", 4);
		break;
	case LEPT_NUMBER:
		c->top -= 32 - sprintf(lept_context_push(c, 32), "%.17g", v->u.n);
		break;
	case LEPT_STRING:
		lept_stringify_string(c, v->u.s.s, v->u.s.len);
		break;
	case LEPT_ARRAY:
		PUTC(c, '[');
		for (i = 0; i < v->u.a.size; i++) {
			if (i > 0)
				PUTC(c, ',');
			lept_stringify_value(c, &v->u.a.e[i]);
		}
		PUTC(c, ']');
		break;
	case LEPT_OBJECT:
		PUTC(c, '{');
		for (i = 0; i < v->u.o.size; i++) {
			if (i > 0)
				PUTC(c, ',');
			lept_stringify_string(c, v->u.o.m[i].k, v->u.o.m[i].klen);
			PUTC(c, ':');
			lept_stringify_value(c, &v->u.o.m[i].v);
		}
		PUTC(c, '}');
		break;
	default:
		assert(0 && "invalid type");
	}
}