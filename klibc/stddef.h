#pragma once

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;


typedef decltype(nullptr) nullptr_t;


#undef NULL
#define NULL __null


#undef offsetof
#define offsetof(s, m) __builtin_offsetof(s, m)
