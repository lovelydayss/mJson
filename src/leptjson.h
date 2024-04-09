#ifndef LEPTJSON_H__
#define LEPTJSON_H__

#include <stddef.h> /* size_t */

/* Json 数值类型 */
typedef enum {
	LEPT_NULL = 1,
	LEPT_FALSE,
	LEPT_TRUE,
	LEPT_NUMBER,
	LEPT_STRING,
	LEPT_ARRAY,
	LEPT_OBJECT,
} lept_type;

/* Json 对象操作返回 */
typedef enum {
	INSERT_OBJECT_OK,
	MODIFY_OBJECT_OK,
	REMOVE_OBJECT_OK,
	OBJECT_INDEX_WRONG
} lept_object_operate;

/* 键值不存在 */
#define LEPT_KEY_NOT_EXIST ((size_t)-1)

/* 结构体预定义 */
/* 处理递归定义问题 */
typedef struct lept_value lept_value;
typedef struct lept_member lept_member;

/* Json 值结构 */
struct lept_value {
	union {
		struct {
			lept_member* m;
			size_t size, capacity;
		} o; /* object 对象类型 */
		struct {
			lept_value* e;
			size_t size, capacity;
		} a; /* array 数组类型 */
		struct {
			char* s;
			size_t len;
		} s; /* string 类型存储字符串 */

		double n; /* 双精度浮点数存储数字 */
	} u;

	lept_type type; /* Json 值类型 */
};

/* Json 对象基本元素类型 */
struct lept_member {
	char* k;
	size_t klen;  /* key string*/
	lept_value v; /* value */
};

/* 指针释放 */
#define free_ptr(p)      \
	do {                 \
		if (p != NULL) { \
			free(p);     \
			p = NULL;    \
		}                \
	} while (0)

/* Json value 值类型初始化 */
#define lept_value_init(v)     \
	do {                       \
		(v)->type = LEPT_NULL; \
	} while (0)

/* Json 解析返回类型 */
enum {
	LEPT_PARSE_OK,                /* 成功解析 */
	LEPT_PARSE_EXPECT_VALUE,      /* 空的 Json */
	LEPT_PARSE_INVALID_VALUE,     /* 非合法字面值 */
	LEPT_PARSE_ROOT_NOT_SINGULAR, /* 空白值后还有其他字符 */
	LEPT_PARSE_NUMBER_TOO_BIG, /* 数值过大双精度浮点数无法表示 */
	LEPT_PARSE_MISS_QUOTATION_MARK,          /* 缺少字符串封闭标记 */
	LEPT_PARSE_INVALID_STRING_ESCAPE,        /* 非法转义标志 */
	LEPT_PARSE_INVALID_STRING_CHAR,          /* 非法字符 */
	LEPT_PARSE_INVALID_UNICODE_HEX,          /* 非法十六进制编码  */
	LEPT_PARSE_INVALID_UNICODE_SURROGATE,    /* 非法代理对范围 */
	LEPT_PARSE_MISS_COMMA_OR_SQUARE_BRACKET, /* 数组未闭合 */
	LEPT_PARSE_MISS_KEY,                     /* 缺少键值 */
	LEPT_PARSE_MISS_COLON,                   /* 缺少中间 : */
	LEPT_PARSE_MISS_COMMA_OR_CURLY_BRACKET   /* 对象未闭合 */
};

/* Json 解析函数 */
int lept_parse(lept_value* v, const char* json);

/* Json 生成函数 */
char* lept_stringify(const lept_value* v, size_t* length);

/* 拷贝，移动，交换 */
void lept_copy(lept_value* dst, const lept_value* src);
void lept_move(lept_value* dst, lept_value* src);
void lept_swap(lept_value* lhs, lept_value* rhs);

/* Json 值类型释放 */
void lept_free(lept_value* v);

/* 获取 Json 值类型 */
lept_type lept_get_type(const lept_value* v);

/* 判断值两 Json 对象相等 */
int lept_is_equal(const lept_value* lhs, const lept_value* rhs);

/* Json 置空 */
/* null 类型不存在构造问题 */
#define lept_set_null(v) lept_free(v)

/* 获取和构造 bollean 值 */
int lept_get_boolean(const lept_value* v);
void lept_set_boolean(lept_value* v, int b);

/* 获取和构造 double 值 */
double lept_get_number(const lept_value* v);
void lept_set_number(lept_value* v, double n);

/* 获取和构造 string 值 */
const char* lept_get_string(const lept_value* v);
size_t lept_get_string_length(const lept_value* v);

void lept_set_string(lept_value* v, const char* s, size_t len);

/* 数组 */

/* 初始化 array 类型值 */
void lept_set_array(lept_value* v, size_t capacity);

/* 获取数组容量信息 */
size_t lept_get_array_size(const lept_value* v);
size_t lept_get_array_capacity(const lept_value* v);

/* 动态数组操作 */
/* 扩张容量，降低容量以适应 size ，清空数组 */
void lept_reserve_array(lept_value* v, size_t capacity);
void lept_shrink_array(lept_value* v);
void lept_clear_array(lept_value* v);

/* 数组操作 */
/* 获得数组，数组尾部插入删除，指定 index 插入删除 */
const lept_value* lept_get_array_element(const lept_value* v, size_t index);
void lept_pushback_array_element(lept_value* v, const lept_value* e);
void lept_popback_array_element(lept_value* v);
void lept_insert_array_element(lept_value* v, const lept_value* e,
                               size_t index);
void lept_erase_array_element(lept_value* v, size_t index, size_t count);

/* Json 对象操作 */

/* 初始化 Json 对象 */
void lept_set_object(lept_value* v, size_t capacity);

/* 获取 Json 对象数组容量信息 */
size_t lept_get_object_size(const lept_value* v);
size_t lept_get_object_capacity(const lept_value* v);

/* 扩张容量，降低容量以适应 size ，清空数组 */
void lept_reserve_object(lept_value* v, size_t capacity);
void lept_shrink_object(lept_value* v);
void lept_clear_object(lept_value* v);

/* Json 单个对象键操作 */
/* 对于对象常用键值进行操作，这个从 index 获得键值函数注定意义不大 */
const char* lept_get_object_key(const lept_value* v, size_t index);
size_t lept_get_object_key_length(const lept_value* v, size_t index);

/* Json 对象元素操作 */
/* 根据索引、键值获取、删除、修改 Json 对象元素值 */
const lept_value* lept_get_object_value_by_index(const lept_value* v,
                                                 size_t index);
const lept_value* lept_get_object_value_by_key(const lept_value* v,
                                               const char* key, size_t klen);

int lept_remove_object_value_by_index(lept_value* v, size_t index);
int lept_remove_object_value_by_key(lept_value* v, const char* key,
                                    size_t klen);

int lept_set_object_value_by_index(lept_value* v, size_t index,
                                   const lept_value* s_v);
int lept_set_object_value_by_key(lept_value* v, const char* key, size_t klen,
                                 const lept_value* s_v);

/* Json 对象中的查找 */
size_t lept_find_object_index(const lept_value* v, const char* key,
                              size_t klen);
const lept_value* lept_find_object_value(const lept_value* v, const char* key,
                                         size_t klen);

#endif /* LEPTJSON_H__ */