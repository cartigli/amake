#include <assert.h>
#include <stdint.h>
#include <unistd.h>

#include "bmk.h"

#define CLASS_LIST \
    X(LOWER)       \
    X(UPPER)       \
    X(DIGIT)       \
    X(PUNC)        \
    X(SPEC)        \
    X(OPER)        \
    X(ALPHA)       \
    X(ALNUM)       \
    X(SPACE)       \
    X(QUOTE)       \
    X(NEWLINE)     \
    X(_GLOBAL_)

#define X(class) class##_##internal,
typedef enum { CLASS_LIST CLASS_COUNT } BMK_Shifted_Indeces;
#undef X

typedef enum {
    DummyMask = 0,
#define X(class) class = (1U << class##_##internal),
    CLASS_LIST DummyFin
} BMK_Classes;
#undef X

#define CLASSBITTREE                         \
    X(0, 0)                                  \
    X(1, 0)                                  \
    X(2, 0)                                  \
    X(3, 0)                                  \
    X(4, 0)                                  \
    X(5, 0)                                  \
    X(6, 0)                                  \
    X(7, 0)                                  \
    X(8, 0)                                  \
    X(9, SPACE)                              \
    X(10, NEWLINE)                           \
    X(11, 0)                                 \
    X(12, 0)                                 \
    X(13, 0)                                 \
    X(14, 0)                                 \
    X(15, 0)                                 \
    X(16, 0)                                 \
    X(17, 0)                                 \
    X(18, 0)                                 \
    X(19, 0)                                 \
    X(20, 0)                                 \
    X(21, 0)                                 \
    X(22, 0)                                 \
    X(23, 0)                                 \
    X(24, 0)                                 \
    X(25, 0)                                 \
    X(26, 0)                                 \
    X(27, 0)                                 \
    X(28, 0)                                 \
    X(29, 0)                                 \
    X(30, 0)                                 \
    X(31, 0)                                 \
    X(' ', _GLOBAL_ | SPACE)                 \
    X('!', _GLOBAL_ | PUNC)                  \
    X('"', _GLOBAL_ | QUOTE)                 \
    X('#', _GLOBAL_ | SPEC)                  \
    X('$', _GLOBAL_ | SPEC | ALNUM)          \
    X('%', _GLOBAL_ | OPER)                  \
    X('&', _GLOBAL_ | SPEC)                  \
    X('\'', _GLOBAL_ | QUOTE)                \
    X('(', _GLOBAL_ | PUNC)                  \
    X(')', _GLOBAL_ | PUNC)                  \
    X('*', _GLOBAL_ | OPER)                  \
    X('+', _GLOBAL_ | OPER)                  \
    X(',', _GLOBAL_ | PUNC)                  \
    X('-', _GLOBAL_ | OPER)                  \
    X('.', _GLOBAL_ | PUNC)                  \
    X('/', _GLOBAL_ | OPER)                  \
    X('0', _GLOBAL_ | DIGIT | ALNUM)         \
    X('1', _GLOBAL_ | DIGIT | ALNUM)         \
    X('2', _GLOBAL_ | DIGIT | ALNUM)         \
    X('3', _GLOBAL_ | DIGIT | ALNUM)         \
    X('4', _GLOBAL_ | DIGIT | ALNUM)         \
    X('5', _GLOBAL_ | DIGIT | ALNUM)         \
    X('6', _GLOBAL_ | DIGIT | ALNUM)         \
    X('7', _GLOBAL_ | DIGIT | ALNUM)         \
    X('8', _GLOBAL_ | DIGIT | ALNUM)         \
    X('9', _GLOBAL_ | DIGIT | ALNUM)         \
    X(':', _GLOBAL_ | PUNC)                  \
    X(';', _GLOBAL_ | PUNC)                  \
    X('<', _GLOBAL_ | OPER)                  \
    X('=', _GLOBAL_ | OPER)                  \
    X('>', _GLOBAL_ | OPER)                  \
    X('?', _GLOBAL_ | PUNC)                  \
    X('@', _GLOBAL_ | SPEC)                  \
    X('A', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('B', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('C', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('D', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('E', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('F', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('G', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('H', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('I', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('J', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('K', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('L', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('M', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('N', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('O', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('P', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('Q', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('R', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('S', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('T', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('U', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('V', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('W', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('X', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('Y', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('Z', _GLOBAL_ | UPPER | ALPHA | ALNUM) \
    X('[', _GLOBAL_ | PUNC)                  \
    X('\\', _GLOBAL_ | SPEC)                 \
    X(']', _GLOBAL_ | PUNC)                  \
    X('^', _GLOBAL_ | OPER)                  \
    X('_', _GLOBAL_ | SPEC | ALNUM)          \
    X('`', _GLOBAL_ | PUNC)                  \
    X('a', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('b', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('c', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('d', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('e', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('f', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('g', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('h', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('i', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('j', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('k', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('l', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('m', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('n', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('o', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('p', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('q', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('r', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('s', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('t', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('u', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('v', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('w', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('x', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('y', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('z', _GLOBAL_ | LOWER | ALPHA | ALNUM) \
    X('{', _GLOBAL_ | PUNC)                  \
    X('|', _GLOBAL_ | SPEC)                  \
    X('}', _GLOBAL_ | PUNC)                  \
    X('~', _GLOBAL_ | PUNC)

#define X(char, classes) classes,
static const us_t bmk[256] = {CLASSBITTREE};
#undef X

uint32_t is_lower(const uc_t x)
{
    if (bmk[x] & LOWER) return 1;
    return 0;
}

uint32_t is_upper(const uc_t x)
{
    if (bmk[x] & UPPER) return 1;
    return 0;
}

uint32_t is_digit(const uc_t x)
{
    if (bmk[x] & DIGIT) return 1;
    return 0;
}

uint32_t is_alnum(const uc_t x)
{
    if (bmk[x] & ALNUM) return 1;
    return 0;
}

uint32_t is_space(const uc_t x)
{
    if (bmk[x] & SPACE) return 1;
    return 0;
}
