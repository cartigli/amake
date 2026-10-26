// #include <assert.h>
// #include <errno.h>
// #include <fcntl.h>
#include <spawn.h>
// #include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// #include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "_fsio.h"
#include "bmk.h"

/* AMAKER - DOCUMENTATION
./amake [make/run/clean] [_dry/debug] [@header]

with no arguments, the default is [make]; without a make spec, the prog will exit

with a make spec, the default is to run the make

*/

/* -std=c89 */

typedef u_int32_t len_t;

#define __ASSIGNMENT__ ':'
#define __COMMENT__ '+'
#define __HEADER__ '@'
#define __FORMAT__ '['
#define __SUBSTITUTION__ '$'
/* #define __NONFORMATTED__ '%' */

#define FSRC "make"

#define _FD_ 1
#define _out_(str, len)        \
    {                          \
        write(_FD_, str, len); \
    }

#define print(msg)                            \
    {                                         \
        _out_(msg "\n", sizeof msg "\n" - 1); \
    }

/* without PPREFACE (_dry), whole command is printed as one block/line */
static uc_t PPREFACE = 0;
#define DEBUGBUILD(type, n_type, f, _)                      \
    { /* label build blocks */                              \
        if (PPREFACE && n_type) {                           \
            f("\nattr: ", sizeof "\nattr: " - 1, _);        \
            f(token_ref[type].str, token_ref[type].len, _); \
            f("\n", 1, _);                                  \
        }                                                   \
    }

#define ITOKEN_CAP 16

extern char **environ;

/* token_t:
_gcc_:    compiler (e.g., gcc, gcc-16)
_targ_:   output binary name; -o
_cflags_: compiler flags (e.g., -O0)
_lflags_: linking/library flags (e.g., -lncurses)
_drcv_:   directory to include, possibly w.source code
_var_:    unformatted input (irrelevant/defunct)
_cmd_:    string to run instead of the default gcc compilation style
_clean_:  directive for clearing previous builds
*/

#define TOKENTYPES   \
    X(gcc, 0x01)     \
    X(targ, 0x02)    \
    X(cflags, 0x04)  \
    X(drcv, 0x08)    \
    X(lflags, 0x010) \
    X(cmd, 0x040)    \
    X(clean, 0x080)

#define X(token, bit) _##token##_,
typedef enum { TOKENTYPES N_TOKENT } token_t;
#undef X

/* cannot repeat specs, i.e.,
    '_targ_: a' && '_targ_: b' fail
    but '_targ_: a b' is not specified in these bits */
#define UNIQUE_TYPES (0x01 | 0x02)
/* cannot repeat tokens, i.e.,
   '_gcc_: a b' fails
   but '_gcc_: a' && '_gcc_: b' is not specified in these bits */
#define UNIQUE_TOKENS (0x01)

/*
#define COMPILE_BUILD (0x01 | 0x02 | 0x04 | 0x08 | 0x010)
#define COMMAND_BUILD (0x040) * raw; no per-token or master parsing *
#define CLEAN_BUILD   (0x080)
*/

typedef struct { const char *str; len_t len; } string;

#define string0(string) { string.str = 0; string.len = 0; }

// typedef struct { char *buf; len_t len; } bufstr;

typedef struct {
    string header;
    uc_t specd;
    uc_t global_specd;
    uc_t faultd;
} Header;

typedef struct {
    string prefix;
    string each;
    string sep;
    string fin;
} Format;

#define DefaultFormat(fmt)                 \
    {                                      \
        fmt.prefix.str = 0;                \
        fmt.prefix.len = 0;                \
        fmt.each.str   = "{t}";            \
        fmt.each.len   = sizeof "{t}" - 1; \
        fmt.sep.str    = " ";              \
        fmt.sep.len    = 1;                \
        fmt.fin.str    = 0;                \
        fmt.fin.len    = 0;                \
    }

typedef struct {
    string tk;
    string origin;
    string pt_sep;
    string pt_each;
} token;

#define token0(token)           \
    {                           \
        string0(token.tk);      \
        string0(token.origin);  \
        string0(token.pt_sep);  \
        string0(token.pt_each); \
    }

typedef struct {
    token *_token_;
    len_t n_tok;
    /* len_t cap; */
    token_t type;
    string master;
    /* string build; */ /* header * would be more accurate/align w.main */
    Format fmt; /* source-of-truth for per-token expansion */
} Dset;

typedef struct {
    /* bufstr cmd; */
    Dset *series;
    len_t ns;
} BB;

#define X(token, bit) bit,
static const uc_t setbits[] = {TOKENTYPES};
#undef X

#define STR(str) #str

#define X(token, bit) {STR(_##token##_), sizeof STR(_##token##_) - 1},
static const string token_ref[] = {TOKENTYPES};
#undef X

#define MCONTINUE \
    {             \
        ns++;     \
        continue; \
    }

static int strs_match(const char *const str_a, const len_t len_a,
    const char *const str_b, const len_t len_b)
{
    if (len_a != len_b) return 0;
    if (memcmp(str_a, str_b, len_a)) return 0;
    return 1;
}

static token_t token_type(const char *const tok, const len_t length)
{
    len_t ns = 0;
    /* if (length && tok[0] == __NONFORMATTED__) return _cmd_; */
    while (ns < N_TOKENT) {
        if (!strs_match(tok, length, token_ref[ns].str, token_ref[ns].len)) {
            MCONTINUE
        }
        return (token_t)ns;
    }
    return _drcv_; /* not recognized */
}

static int valtok(token **tokens, len_t *cap, const len_t need,
    const char *const str, const len_t len)
{
    token *repl;
    len_t newcap;
    token new;
    token0(new);
    new.tk.str = str;
    new.tk.len = len;
    if (need + 1 > *cap) {
        newcap = *cap ? *cap * 2 : ITOKEN_CAP;
        repl   = realloc(*tokens, newcap * sizeof(token));
        if (!repl) {
            print("NOMEM");
            return 1;
        }
        *tokens = repl;
        *cap    = newcap;
    }
    (*tokens)[need] = new;
    return 0;
}

static int skip_ws(const char **str)
{
    while (is_space((const uc_t) * *str)) (*str)++;
    return **str ? 0 : 1;
    /* if (!**str) return 1;
    return 0; */
}

static void skip_tok(const char **str)
{
    while (is_alnum((const uc_t) * *str) || **str == '/' || **str == '.') (*str)++;
}

static int skip_rest_ofl(const char **text)
{
    while (**text && **text != '\n') (*text)++;
    while (**text == '\n') (*text)++;
    if (!**text) return 1;
    return 0;
}

#define fnext_char(text, esig, rt)                    \
    {                                                 \
        (*text)++;                                    \
        if (!**text) {                                \
            print("parser hit EOF; bad config/make"); \
            (esig) |= CONFIG_ERRO;                    \
            return rt;                                \
        }                                             \
    }

#define char_assert(need, have, esig, rt)                         \
    {                                                             \
        if ((have) != need) {                                     \
            fprintf(stderr, "needed %c have %c\n", need, (have)); \
            (esig) |= PARSING_ERRO;                               \
            return rt;                                            \
        }                                                         \
    }

typedef enum { CONFIG_ERRO = 0x01, MEMORY_ERRO = 0x02, PARSING_ERRO = 0x04 } erro_t;

static int append_tok(token **tokens, len_t *cap, len_t *have, const char **str,
    uc_t *erro_b)
{
    /* returns 1 for token appended, 0 for no token and/or error */
    const char *off = 0;
    if (skip_ws(str)) return 0;
    /*
    skip_ws(str);
    if (!**str) return 0;
    */
    off = *str;
    while (**str) {
        if (**str == ' ' || **str == ',' || **str == '\n') break;
        (*str)++;
    }
    if (*str == off) {
        print("0 WIDTH TOKEN");
        return 0;
    }
    if (valtok(tokens, cap, *have, off, (len_t)(*str - off))) {
        *erro_b |= MEMORY_ERRO;
        return 0;
    }
    if (**str == ',') (*str)++; /* optional */
    (*have)++;
    return 1;
}

static int build_header_specd(const string *const header,
    const string *const current)
{
    /* returns true if the input token == runtime header spec */
    return (header->len == current->len &&
        !memcmp(header->str, current->str, header->len));
}

static int find_sub_token(const char *tstr, const len_t tlen, token **tokbuf,
    len_t *tcap, len_t *have, const Dset *const gset, const len_t nsets, uc_t *err)
{
    len_t ns     = 0;
    len_t wt     = 0;
    len_t pasted = 0;
    while (ns < nsets) {
        if (gset[ns].master.len != tlen - 3 ||
            memcmp(gset[ns].master.str, tstr + 2, tlen - 3)) {
            MCONTINUE
        }
        wt = 0;
        while (wt < gset[ns].n_tok) {
            /* handled by formatting, if need be
            if (gset[ns].type == _targ_ && gset[ns].n_tok == 2 && wt == 0) {
                wt++; * skip past the first -flag of the targ, if present *
                continue;
            } */
            if (valtok(tokbuf, tcap, pasted - 1 + *have, /* compensate */
                    gset[ns]._token_[wt].tk.str, gset[ns]._token_[wt].tk.len)) {
                *err |= MEMORY_ERRO;
                print("failed to validate pasted-token write in");
                return 1;
            }
            /* redundant with next token's format; always copy the token's original format */
            /* should be all types, & should get the fmt'ing copied in PER TOKEN */
            /* (*tokbuf)[pasted - 1 + *have].pasted_dcrv = 1; */
            (*tokbuf)[pasted - 1 + *have].pt_each = gset[ns]._token_[wt].pt_each;
            (*tokbuf)[pasted - 1 + *have].pt_sep  = gset[ns]._token_[wt].pt_sep;
            (*tokbuf)[pasted - 1 + *have].origin  = gset[ns]._token_[wt].origin;
            /*
            if (!gset[ns]._token_[wt].pasted_dcrv) {
                * if the token has not been pasted/nested, copy from the Dset *
                (*tokbuf)[pasted - 1 + *have].pt_sep  = gset[ns].fmt.sep;
                (*tokbuf)[pasted - 1 + *have].pt_each = gset[ns].fmt.each;
                (*tokbuf)[pasted - 1 + *have].origin  = gset[ns].master;
            } else {
                * else, save from the token & use the original format & master *
                (*tokbuf)[pasted - 1 + *have].pt_sep  = gset[ns]._token_[wt].pt_sep;
                (*tokbuf)[pasted - 1 + *have].pt_each = gset[ns]._token_[wt].pt_each;
                (*tokbuf)[pasted - 1 + *have].origin  = gset[ns]._token_[wt].origin;
            }
            */
            pasted++;
            wt++;
        }
        ns++;
    }
    if (!pasted) {
        *err |= PARSING_ERRO;
        print("SUBSTITUTE TOKEN IS NOT DEFINED");
        return 1;
    }
    --(*have);
    *have += pasted;
    return 0;
}

static int specify_format(const char **text, Format *const fmt, uc_t *errosig)
{
    string need;
    const char *have;
    string *set;
    while (**text) {
        switch (**text) {
        case 'p':
            need.str = "prefix";
            need.len = sizeof "prefix" - 1;
            set      = &fmt->prefix;
            break;
        case 'e':
            need.str = "each";
            need.len = sizeof "each" - 1;
            set      = &fmt->each;
            break;
        case 's':
            need.str = "sep";
            need.len = sizeof "sep" - 1;
            set      = &fmt->sep;
            break;
        case 'f':
            need.str = "fin";
            need.len = sizeof "fin" - 1;
            set      = &fmt->fin;
            break;
        default:
            fprintf(stderr, "unknown formatting spec=%c\n", **text);
            *errosig |= PARSING_ERRO;
            return 1;
        }
        have = *text;
        skip_tok(text);
        if (!strs_match(have, (len_t)(*text - have), need.str, need.len)) {
            *errosig |= PARSING_ERRO;
            fprintf(stderr, "unknown formatting spec=%.5s\n", have);
            return 1;
        }
        char_assert('=', **text, *errosig, 1);

        fnext_char(text, *errosig, 1);
        char_assert('"', **text, *errosig, 1);

        fnext_char(text, *errosig, 1);

        set->str = *text;
        while (**text && **text != '"') (*text)++;
        char_assert('"', **text, *errosig, 1);

        set->len = *text - set->str;
        fnext_char(text, *errosig, 1);

        /* no check; ="" allowed */
        if (skip_ws(text)) return 1;
        /* skip_ws(text); */
        if (**text && **text == ']') {
            (*text)++;
            return 0;
        }
        if (**text == ',') fnext_char(text, *errosig, 1);
        if (skip_ws(text)) return 1;
        /* skip_ws(text); */
    }
    *errosig |= PARSING_ERRO;
    print("formatting spec hit EO-input before parsing finished");
    return 1;
}

static int next_set(const char **text, Dset *set_, len_t *ns,
    const Dset *const gset, Header *header, const string *const build_header)
{
    token *_token_ = 0;
    const char *ms = 0;
    len_t mlen     = 0;
    len_t cap      = 0;
    len_t n_tk     = 0;
    token_t type   = _drcv_;
    Format fmt;

    DefaultFormat(fmt);
    /*
    fmt.prefix.str = 0; * needs default *
    fmt.prefix.len = 0;
    fmt.each.str   = 0;
    fmt.each.len   = 0;
    fmt.sep.str    = 0;
    fmt.sep.len    = 0;
    fmt.fin.str    = 0;
    fmt.fin.len    = 0;
    */

    if (!text || !*text || !**text) return 0;
    while (**text == ' ' || **text == '\n') (*text)++;
    if (!**text) return 0;

    switch (**text) {
    /* comment - don't read line -- invert to kill parsing at EOF */
    case __COMMENT__:
        return skip_rest_ofl(text) ? 0 : 1;

    /* specify alternative build directive */
    case __HEADER__:
        (*text)++;
        ms = *text;
        skip_tok(text);
        if (ms == *text) {
            print("0-width header");
            header->faultd |= CONFIG_ERRO;
            free(_token_);
            return 0;
        }
        /* if no current build header, then under the global state;
           when exiting this state into a new build header, save the
           global build state, then for every build, start it at the
           global state instead of from scratch */
        if (!header->header.len) header->global_specd = header->specd;
        header->specd      = header->global_specd;
        header->header.len = (len_t)(*text - ms);
        header->header.str = ms;
        if (skip_rest_ofl(text)) {
            print("HEADER SPECIFIED WITH NO BUILD DIRECTIVE");
            header->faultd |= CONFIG_ERRO;
            return 0;
        }
        _out_("header found & recorded: ",
            sizeof "header found & recorded: " - 1);
        _out_(header->header.str, header->header.len);
        print("");
        return 1;

    /* specify non-file/non-parsed type
    case __NONFORMATTED__:
        type = _var_;
        fnext_char(text, header->faultd, 0);
        break;
    */

    default:
        break;
    }

    if (!build_header_specd(build_header, &header->header)) {
        return skip_rest_ofl(text) ? 0 : 1; /* invert to kill parsing */
    }

    ms = *text;
    skip_tok(text);
    if (!**text || *text == ms) return 0;
    mlen = (len_t)(*text - ms);
    type = token_type(ms, mlen);
    if ((setbits[type] & header->specd) && (setbits[type] & UNIQUE_TYPES)) {
        print("UNREPEATABLE TOKEN RE-SPECIFIED");
        header->faultd |= CONFIG_ERRO;
        return 0;
    }
    header->specd |= setbits[type];

    /* set must be prepended with > 0 alnum's followed by ':'
       & must be at BOL or preceeded only by whitespace */
    /*
    skip_ws(text);
    if (!**text || **text != __ASSIGNMENT__) {
        print("series opened & unclosed");
        header->faultd |= CONFIG_ERRO;
        return 0;
    }
    */
    if (skip_ws(text)) return 0;
    char_assert(__ASSIGNMENT__, **text, header->faultd, 0);

    fnext_char(text, header->faultd, 0);

    /*
    skip_ws(text);
    if (!**text) {
    */
    if (skip_ws(text)) {
        header->faultd |= CONFIG_ERRO;
        print("hit EOF; parsing failed");
        return 0;
    }

    /* specify the formatting to a series after defining it */
    if (**text == __FORMAT__) {
        (*text)++;
        if (specify_format(text, &fmt, &header->faultd)) {
            print("specifying formatting failed");
            return 0;
        }
    }

    cap     = ITOKEN_CAP;
    _token_ = calloc(cap, sizeof(token));
    if (!_token_) {
        header->faultd |= MEMORY_ERRO;
        print("NOMEM");
        return 0;
    }

    while (append_tok(&_token_, &cap, &n_tk, text, &header->faultd)) {
        /* guard non-repeatables definining multiples at collection */
        if (n_tk > 1 && (setbits[type] & UNIQUE_TOKENS)) {
            header->faultd |= CONFIG_ERRO;
            print("multiple tokens on non-repeatable collected");
            return 0;
        }
        /* assign EVERY token its original format & master */
        _token_[n_tk - 1].pt_sep     = fmt.sep;
        _token_[n_tk - 1].pt_each    = fmt.each;
        _token_[n_tk - 1].origin.str = ms;
        _token_[n_tk - 1].origin.len = mlen;
        if (_token_[n_tk - 1].tk.len > 3 && /* need atleast '$(+)' */
                _token_[n_tk - 1].tk.str[0] == __SUBSTITUTION__ &&
                _token_[n_tk - 1].tk.str[1] == '(' &&
                _token_[n_tk - 1].tk.str[_token_[n_tk - 1].tk.len - 1] == ')') {
            if (find_sub_token(_token_[n_tk - 1].tk.str, _token_[n_tk - 1].tk.len,
                    &_token_, &cap, &n_tk, gset, *ns, &header->faultd)) {
                free(_token_);
                print("failed to substitute token");
                return 0;
            }
        }
        skip_ws(text);
        if (!**text || **text == '\n') break;
    }
    if (header->faultd) {
        free(_token_);
        return 0;
    }
    /* skip from potentially corrupted line till next BOL (if corrupted) */
    skip_rest_ofl(text);
    set_->_token_    = _token_;
    set_->n_tok      = n_tk;
    /* set_->cap        = cap; */
    set_->master.str = ms;
    set_->master.len = mlen;
    set_->type       = type;
    /*
    set_->build      = header->header; * str & len = 0 == generic *
    */
    set_->fmt        = fmt;
    (*ns)++;
    return 1;
}

static void free_sets(Dset *sets, const len_t n_sets)
{
    len_t i = 0;
    while (i < n_sets) {
        free(sets[i]._token_);
        i++;
    }
    free(sets);
}

static int parse_tokens(const char *text, BB *bb,
    const string *const build_header)
{
    Header head;
    Dset *repl;
    len_t Dc     = 0;
    len_t setcap = 16;
    Dset *sets   = calloc(setcap, sizeof(Dset));
    if (!sets) {
        print("NOMEM");
        return 1;
    }
    string0(head.header);
    head.faultd       = 0;
    head.specd        = 0;
    head.global_specd = 0;
    while (next_set(&text, &sets[Dc], &Dc, sets, &head, build_header) &&
        !head.faultd) {
        if (Dc >= setcap) {
            repl = realloc(sets, setcap * 2 * sizeof(Dset));
            if (!repl) {
                print("NOMEM");
                goto fcleanup;
            }
            sets = repl;
            setcap *= 2;
        }
    }
    if (head.faultd) {
        switch (head.faultd & (MEMORY_ERRO | CONFIG_ERRO | PARSING_ERRO)) {
        case MEMORY_ERRO:
            print("Out-Of-Memory while parsing build");
            goto fcleanup;
        case CONFIG_ERRO:
            print("build directive is misconfigured");
            goto fcleanup;
        case PARSING_ERRO:
            print("make config has a syntax error");
            goto fcleanup;
        default:
            print("multiple errors while parsing build");
            goto fcleanup;
        }
    }
    if (!Dc) {
        print("no directives or tokens collected; no build to run");
        goto fcleanup;
    }
    bb->series = sets;
    bb->ns     = Dc;
    return 0;
fcleanup:
    free_sets(sets, Dc);
    return 1;
}

static void to_buf(const char *str, const len_t n, char **buf)
{
    memcpy(*buf, str, n);
    *buf += n;
}

static len_t GLOBAL_COUNT = 0;
static void skipcount(const char *str, const len_t n, char **buf)
{
    (void)str;
    (void)buf;
    GLOBAL_COUNT += n;
}

typedef void (*copier_fn)(const char *, const len_t, char **buf);

/* EX:
-I. ./common_attrs.c ./fsio.c *
*typedef struct {
    token m_prefix; * '-I{m}' *
     * master token printed (+ a space) *
    token t_prefix; * '{m}/{token}' (copied from the master) *
    token t_tail; * .c

EX - target = -I. ./common_attrs.c ./fsio.c
[prefix="-I{m}", each="{m}/{t}", sep="", fin=""]
*/

#define fcurs_pplus(cursor, len)              \
    {                                         \
        cursor++;                             \
        if (cursor >= len) {                  \
            print("token expansion hit EOF"); \
            return 1;                         \
        }                                     \
    }

static int _ps_(const string *const master, const token *const tok,
    const string *const fmt, /* const string *const alt, */ copier_fn func, char **_)
{
    char sig;
    len_t cursor    = 0;
    /*
    const char *str = alt && alt->str ? alt->str : fmt->str;
    const len_t len = alt && alt->str ? alt->len : fmt->len; * gated on str, not len *
    if (!str) return 0;
    */
    while (cursor < fmt->len) {
        if (fmt->str[cursor] == '{') { /* should write literal if not {t/m} */
            fcurs_pplus(cursor, fmt->len);
            sig = fmt->str[cursor];
            if (sig != 't' && sig != 'm') {
                print("unknown spec");
                return 1;
            }
            fcurs_pplus(cursor, fmt->len);
            if (fmt->str[cursor] != '}') {
                print("unclosed bracket in formatting spec");
                return 1;
            }
            cursor++;
            if (sig == 'm') {
                func(master->str, master->len, _);
            } else if (sig == 't' && tok && tok->tk.str) {
                func(tok->tk.str, tok->tk.len, _);
            }
            continue;
        }
        func(&fmt->str[cursor], 1, _);
        cursor++;
    }
    return 0;
}
/*     while (cursor < len) {
//         if (str[cursor] == '{') {
//             fcurs_pplus(cursor, len);
//             sig = str[cursor];
//             if (sig != 't' && sig != 'm') {
//                 print("unknown spec");
//                 return 1;
//             }
//             fcurs_pplus(cursor, len);
//             if (str[cursor] != '}') {
//                 print("unclosed bracket in formatting spec");
//                 return 1;
//             }
//             cursor++;
//             if (sig == 'm') {
//                 func(master->str, master->len, _);
//             } else if (sig == 't' && tok && tok->tk.str) {
//                 func(tok->tk.str, tok->tk.len, _);
//             }
//             continue;
//         }
//         func(&str[cursor], 1, _);
//         cursor++;
//     }
//     return 0;
// } */

/* static int _x_(const BB *const bb, const len_t index, const len_t BUILDMASK,
    copier_fn func, char **_)
{
    const Dset *series   = &bb->series[index]; */
static int _x_(const Dset *const series, /* const len_t BUILDMASK, */
    copier_fn func, char **_)
{
/*    const Dset *series   = &bb->series[index]; */
    len_t n_tokens       = 0;
    const string *master = 0;
    /* if (!(setbits[series->type] & BUILDMASK)) return 0; */
    DEBUGBUILD(series->type, series->n_tok, func, _);
    /* prefix & fin get no alternatives for pastes; only sep & each apply per-token */
    /* null deref if prefix refernces token ({t}), but otherwise only reference-able
       token in the prefix is token[0]; would not be clear if it was implemented */
    if (_ps_(&series->master, 0, &series->fmt.prefix, func, _)) return 1;
    while (n_tokens < series->n_tok) {
        /* preference: pasted-origin's master, default: master *
        master = series->_token_[n_tokens].pasted_dcrv
            ? &series->_token_[n_tokens].origin
            : &series->master;
        now it is always the tokens' master, pasted or not
        */
        master = &series->_token_[n_tokens].origin;
        if (_ps_(master, &series->_token_[n_tokens], /* &series->fmt.each, */
                &series->_token_[n_tokens].pt_each, func, _)) {
            return 1;
        }
        if  (n_tokens < series->n_tok - 1) {
            if (_ps_(master, &series->_token_[n_tokens], /* &series->fmt.sep, */
                    &series->_token_[n_tokens].pt_sep, func, _)) {
                return 1;
            }
        } else {
            if (_ps_(master, &series->_token_[n_tokens], &series->fmt.fin,
                /* has no pasted-in alternative (same as head) */ func, _)) {
                return 1;
            }
        }
        n_tokens++;
    }
    return 0;
}

// static len_t count_linklen(const BB *const bb, const len_t BUILD_MASK)
// {
//     len_t ns     = 0;
//     uc_t spec    = 0;
//     token_t type = _drcv_;
//     GLOBAL_COUNT = 0;
//     while (ns < bb->ns) {
//         type = bb->series[ns].type;
//         if (!(setbits[type] & BUILD_MASK)) MCONTINUE
//         if (setbits[type] & spec && setbits[type] & UNIQUE_TYPES) { 
//             print("double listed attr; skipping count") MCONTINUE
//         }
//         spec |= setbits[type];
//         if (_x_(&bb->series[ns], skipcount, 0)) return 0;
//         GLOBAL_COUNT++;
//         ns++;
//     }
//     return GLOBAL_COUNT;
// }

/* static char *run_sl_series(const Dset *const series) */
static char *bbuild_sl(const Dset *const series)
{
    char *buf    = 0;
    char *build  = 0;
    GLOBAL_COUNT = 0;
    if (_x_(series, skipcount, 0)) return 0;
    build = malloc(GLOBAL_COUNT + 1);
    if (!build) {
        print("NOMEM");
        return 0;
    }
    buf = build;
    if (_x_(series, to_buf, &buf)) {
        free(build);
        print("parsing single series failed");
        return 0;
    }
    build[buf - build] = '\0';
    return build;
}

// static int bbuild(BB *const bb, copier_fn todo, const len_t BUILD_MASK)
// {
//     char *buf          = 0;
//     char *build_linked = 0;
//     uc_t spec          = 0;
//     len_t ns           = 0;
//     token_t type       = _drcv_;
//     const len_t length = count_linklen(bb, BUILD_MASK);
//     if (!length) {
//         print("no directive");
//         return 1;
//     }
//     build_linked = malloc(length + 1);
//     if (!build_linked) {
//         print("NOMEM");
//         return 1;
//     }
//     buf = build_linked;
//     while (ns < bb->ns) {
//         type = bb->series[ns].type;
//         if (!(setbits[type] & BUILD_MASK)) {
//             ns++;
//             continue;
//         }
//         if (setbits[type] & spec && setbits[type] & UNIQUE_TYPES) {
//             fprintf(stderr, "\nbuild component '%.*s' respecified\n",
//                 token_ref[bb->series[ns].type].len,
//                 token_ref[bb->series[ns].type].str);
//             ns++;
//             continue;
//         }
//         spec |= setbits[bb->series[ns].type];
//         if (_x_(&bb->series[ns], todo, &buf)) {
//             free(build_linked);
//             return 1;
//         }
//         /* will consider masked-out components & emit an extra space (not ideal!) */
//         if (ns < bb->ns - 1) todo(" ", 1, &buf);
//         ns++;
//     }
//     bb->cmd.buf              = build_linked;
//     bb->cmd.len              = (len_t)(buf - build_linked);
//     bb->cmd.buf[bb->cmd.len] = '\0';
//     return 0;
// }

static void run_cmd(char *const command)
{
    pid_t pid;
    char *argv[4];
    int rc;
    argv[0] = "sh";
    argv[1] = "-c";
    argv[2] = command;
    argv[3] = 0;
    /* rc = posix_spawn(&pid, "/bin/sh", 0, 0, argv, environ);
    if (rc) { */
    if (posix_spawn(&pid, "/bin/sh", 0, 0, argv, environ)) {
        print("FAILED TO EXECUTE");
        return;
    }
    if (waitpid(pid, &rc, 0) == -1) {
        print("FAILED TO WAIT FOR PROCESS FIN");
        return;
    }
    if (WIFEXITED(rc) && !WEXITSTATUS(rc)) {
        print("proc OK");
        return;
    }
    print("SYSTEM ERRO WHILE RUNNING COMMAND");
}

typedef enum {
    __MAKE__  = 0x01,
    __RUN__   = 0x02,
    __CLEAN__ = 0x04
} build_t;

// #define BUILDBITS (__MAKE__ | __RUN__ | __CLEAN__)

// #define DEBUGBITS (__DEBUG__ | __DRYRUN__)

static const string buildstrs[] = {
    { "make", sizeof "make" - 1},
    { "run", sizeof "run" - 1},
    { "clean", sizeof "clean" - 1},
};

static void run_sl(const Dset *const series, const int debug)
{
    char *cmd = bbuild_sl(series);
    if (!cmd) {
        print("building single-series command failed");
        return;
    }
    if (!debug) run_cmd(cmd);
    else {
        _out_(cmd, strlen(cmd));
        print("");
    }
    free(cmd);
}

// static __attribute__((unused)) void run(BB *const bb, const len_t BUILD) /*, const int debug) */
// {
//     if (bbuild(bb, to_buf, BUILD)) return;
//     /* if (!debug) run_cmd(bb->cmd.buf);
//     else { */
//         _out_(bb->cmd.buf, bb->cmd.len);
//         print("");
//         /* _out_("\n", 1); */
// }

static Dset *find_spec(const BB *const bb, const char *const arg, const len_t arglen)
{
    len_t ns = 0;
    while (ns < bb->ns) {
        if (strs_match(bb->series[ns].master.str, bb->series[ns].master.len,
                arg, arglen)) {
            return &bb->series[ns];
        }
        ns++;
    }
    return 0;
}

static Dset *find_parsed_spec(const BB *const bb, const build_t bt)
{
    switch (bt) {
    case __MAKE__:
        return find_spec(bb, buildstrs[0].str, buildstrs[0].len);
    case __RUN__:
        return find_spec(bb, buildstrs[1].str, buildstrs[1].len);
    case __CLEAN__:
        return find_spec(bb, buildstrs[2].str, buildstrs[2].len);
    default:
        print("unknown runtime spec; defaulting to make");
        return find_spec(bb, buildstrs[0].str, buildstrs[0].len);
    }
}

// static void __attribute__((unused)) sort_build(BB *const bb)
// {
//     Dset _set;
//     int shift;
//     token_t key;
//     len_t i = 0;
//     while (i < bb->ns) {
//         _set  = bb->series[i];
//         key   = bb->series[i].type;
//         shift = (int)i - 1;
//         while (shift >= 0 && bb->series[shift].type > key) {
//             bb->series[shift + 1] = bb->series[shift];
//             shift--;
//         }
//         bb->series[shift + 1] = _set;
//         i++;
//     }
// }

static void free_b(BB *b)
{
    /*
    len_t i = 0;
    while (i < b->ns) {
        free(b->series[i]._token_);
        b->series[i]._token_ = 0;
        i++;
    }
    */
    free_sets(b->series, b->ns);
    /* free(b->series); */
    b->series = 0;
    b->ns     = 0;
    /*
    if (b->cmd.buf && b->cmd.len) {
        free(b->cmd.buf);
        b->cmd.buf = 0;
        b->cmd.len = 0;
    }
    */
}

/*
static int linkedfs_exist(const BB *const bb)
{
    len_t i = 0;
    len_t f = 0;
    return 1; * made a no-op for now *
    *
    len_t tf = 0;
    struct timespec end;
    struct timespec beg;
    if (PPREFACE) {
        if (clock_gettime(CLOCK_MONOTONIC_RAW, &beg)) {
            print("couldn't start timer");
        }
    } *
    while (i < bb->ns) {
        if (bb->series[i].type != _drcv_) {
            i++;
            continue;
        }
        f = 0;
        while (f < bb->series[i].n_tok) {
            if (!fexists(bb->series[i].master.str, bb->series[i].master.len,
                    bb->series[i]._token_[f].str,
                    bb->series[i]._token_[f].len)) {
                return 0;
            }
            f++;
        }
        i++;
    }
    * if (PPREFACE) {
        if (clock_gettime(CLOCK_MONOTONIC_RAW, &end)) {
            print("could not end timer")
        }
        fprintf(stderr, "time to scan for all build files=%.6f\n",
            (float)((end.tv_sec - beg.tv_sec) +
                (end.tv_nsec - beg.tv_nsec) / 1000000000.0));
        fprintf(stderr, "total files confirmed=%i\n", tf);
    } *
    return 1;
}
*/

// static int __attribute__((unused)) validate_directive(BB *const bb, const len_t BUILD_TYPE)
// {
//     if (BUILD_TYPE & COMPILE_BUILD) {
//         if (bb->series[0].type != _gcc_) {
//             print("no compiler spec");
//             return 1;
//         }
//         if (bb->series[0].n_tok != 1) {
//             print("no compiler spec");
//             return 1;
//         }
//         if (bb->series[1].type != _targ_) {
//             /* if (bb->series[1].type == _cflags_) {
//                 if (bb->series[1]._token_[0].str[0] == 'c' &&
//                     bb->series[1]._token_[0].len == 1) {
//                     goto linkedfs;
//                 }
//             } */
//             print("no target spec");
//             return 1;
//         }
//         if (bb->series[1].n_tok > 2 || !bb->series[1].n_tok) {
//             print("no target spec");
//             return 1;
//         }
// /*    linkedfs:
//         if (!linkedfs_exist(bb)) {
//             print("not all linked files exist!");
//             return 1;
//         } */
//     } else if (BUILD_TYPE & CLEAN_BUILD) {
//         if (bb->series[bb->ns - 1].type != _clean_) return 1;
//         if (bb->series[bb->ns - 1].n_tok < 1) return 1;
//     }
//     return 0;
// }

// typedef void (*build_fn)(const BB *const, const len_t);

/* syntax: amake [make/clean/run] [debug/_dry] [@(build)header] -- default build is make */

/* #define TESTMAKE                   \
    "\n_gcc_: build-prog\n"        \
    "_targ_: X99-86.64.o\n\n"      \
    "utilities: len bingo foo\n\n" \
    "bin: $(utilities)\n\n"        \
    "_clean_: $(_targ_) $(utilities)\n\n"

#define TESTMAKE_COMPNEED                \
    "build-prog -o X99-86.64.o "         \
    "-Iutilities utilities/len.c "       \
    "utilities/bingo.c utilities/foo.c " \
    "-Ibin bin/len.c bin/bingo.c bin/foo.c"

#define TESTMAKE_CLEANNEED                 \
    "rm -f X99-86.64.o && rm -f len.c && " \
    "rm -f bingo.c && rm -f foo.c;" */

typedef struct {
    /* len_t BUILD; */
    build_t _build_;
    uc_t debug;
    string header;
} Args;

static int parse_args(const int argc, const char *argv[], Args *args)
{
    int a = 1;
    len_t need_arglen;
    const char *need_arg;
    while (a < argc) {
        switch (argv[a][0]) {
        case '@':
            if (args->header.len || args->header.str) return 1;
            args->header.str = argv[a] + 1;
            args->header.len = strlen(argv[a]) - 1;
            a++;
            continue;
        /* set as if successful, full bail on any error */
        case 'm':
            if (args->_build_) return 1;
            need_arg      = "make";
            need_arglen   = sizeof "make" - 1;
            args->_build_ = __MAKE__;
            /*
            args->BUILD = COMPILE_BUILD;
            */
            break;
        case 'c':
            if (args->_build_) return 1;
            need_arg      = "clean";
            need_arglen   = sizeof "clean" - 1;
            args->_build_ = __CLEAN__;
            break;
        case 'r':
            if (args->_build_) return 1;
            need_arg      = "run";
            need_arglen   = sizeof "run" - 1;
            args->_build_ = __RUN__;
            break;
        case 'd':
            if (args->debug) return 1;
            need_arg    = "debug";
            need_arglen = sizeof "debug" - 1;
            args->debug = 1;
            /* args->debug = 1; */
            PPREFACE    = 1;
            break;
        case '_':
            if (args->debug) return 1;
            need_arg    = "_dry";
            need_arglen = sizeof "_dry" - 1;
            args->debug = 1; /* no PPREFACE; dry run */
            break;
        default:
            fprintf(stderr, "unknown=%s\n", argv[a]);
            return 1;
        }
        if (strlen(argv[a]) != need_arglen ||
                memcmp(argv[a], need_arg, need_arglen)) {
            return 1;
        }
        a++;
    }
    if (!args->_build_) args->_build_ = __MAKE__; /* default */
    return 0;
}

#define cleanup               \
    {                         \
        if (make) free(make); \
        free_b(&b);           \
        free_fbuf();          \
    }

int main(const int argc, const char *argv[])
{
    BB b;
    Args args;
    Dset *spec = 0;
    char *make = 0;
    /*
    char *cmdstr = 0;
    args.BUILD = 0; default *
    b.cmd.buf = 0;
    b.cmd.len = 0;
    */
    args.debug   = 0;
    args._build_ = 0;
    string0(args.header);
    b.series  = 0;
    b.ns      = 0;

    if (parse_args(argc, argv, &args)) {
        print("parsing arguments failed");
        return 1;
    }

    make = mann_mm(FSRC);
    if (!make) return 1;

    if (parse_tokens(make, &b, &args.header)) {
        free(make);
        return 1;
    }
    spec = find_parsed_spec(&b, args._build_);
    if (!spec) {
        print("(WARNING) found no specifications for requested build in series");
        free(make);
        return 1;
    }
    print("spec found & matched OK");
    /*
    cmdstr = bbuild_sl(spec);
    if (!cmdstr) {
        print("failed to build command from series");
        return 1;
    } */
    run_sl(spec, args.debug);
    /*
    run_sl(spec, args.debug);
    fprintf(stderr, "cmd='%s'\n", cmdstr);
    free(cmdstr);
    */
    cleanup;
    return 0;
    /* sort_build(&b);

    if (validate_directive(&b, args.BUILD)) {
        print("build failed validation");
        if (!args.debug) goto cleanup;
    }
    run(&b, args.BUILD); */

/* cleanup:
    free_fbuf();
    if (b.cmd.buf && b.cmd.len) free(b.cmd.buf);
    free_b(&b);
    free(make);
    return 0; */
}
