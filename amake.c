#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "_fsio.h"
#include "bmk.h"

/* c89 */

typedef u_int32_t len_t;

#define __ASSIGNMENT__ ':'
#define __COMMENT__ '+'
#define __HEADER__ '@'
#define __FORMAT__ '['
#define __FORMAT_PASTE__ '^'
#define __SUBSTITUTION__ '$'

#define FSRC "make"

#define _FD_ 1
#define _out_(str, len) { write(_FD_, str, len); }

#define print(msg) { _out_(msg "\n", sizeof msg "\n" - 1); }

#define ITOKEN_CAP 16

extern char **environ;

typedef struct {
    const char *str;
    len_t len;
} string;

#define string0(string) \
    {                   \
        string.str = 0; \
        string.len = 0; \
    }

typedef struct {
    char *buf;
    len_t len;
} buffer;

/* the specifiable runtime directives (per @Header) */
#define BUILD_FLAGS(X)  \
    X(__MAKE__, "make") \
    X(__RUN__, "run")   \
    X(__CLEAN__, "clean")

#define _ENUM_(flag, str) flag,
#define _STRT_(flag, str) {str, sizeof str - 1},

typedef enum { BUILD_FLAGS(_ENUM_) N_DIRECTIVE } build_t;

static const string buildstrs[] = { BUILD_FLAGS(_STRT_) };

static uc_t is_directive(const char *const tstr, const len_t tlen)
{
    if (tlen < 3 || tlen > 5) return 0;
    switch (tlen) {
    case 3:
        if (!memcmp((tstr), buildstrs[__RUN__].str, tlen)) return 1;
        return 0;
    case 4:
        if (!memcmp((tstr), buildstrs[__MAKE__].str, tlen)) return 1;
        return 0;
    case 5:
        if (!memcmp((tstr), buildstrs[__CLEAN__].str, tlen)) return 1;
        return 0;
    default:
        return 0;
    }
}

typedef struct {
    string header;
    uc_t faultd;
} Header;

typedef struct {
    string pre;
    string each;
    string sep;
    string fin;
} Format;

static const string def_sep  = {" ", sizeof " " - 1};
static const string def_each = {"{t}", sizeof "{t}" - 1};

typedef struct {
    string tk;
    string origin;
    string pt_sep;
    string pt_each;
    char *allocated;
} token;

#define token_0(token) { memset(&token, 0, sizeof(token)); }

typedef struct {
    token *_token_;
    len_t n_tok;
    string master;
    Format fmt; /* source-of-truth for per-token expansion */
} Dset;

typedef struct {
    string name;
    Format fmt;
} NamedFormat;

typedef struct {
    NamedFormat *formats;
    len_t n_formats;
    len_t formatcap;
} NFormatBuf;

typedef struct {
    Dset *series;
    len_t ns;
    NFormatBuf nfbuf;
} BB;

#define MCONTINUE \
    {             \
        ns++;     \
        continue; \
    }

static uc_t strs_match(const char *const str_a, const len_t len_a,
    const char *const str_b, const len_t len_b)
{
    if (len_a != len_b) return 0;
    if (memcmp(str_a, str_b, len_a)) return 0;
    return 1;
}

static uc_t valtok(token **tokens, len_t *cap, const len_t need,
    const char *const str, const len_t len)
{
    token *repl;
    len_t newcap;
    token new;
    token_0(new);
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

static uc_t skip_ws(const char **str)
{
    while (is_space((const uc_t) * *str)) (*str)++;
    return **str ? 0 : 1;
}

static void skip_tok(const char **str)
{
    /* allow 'dir/subdir' && '.' (for -I. or ./incl.h) as referenceable series */
    while (is_alnum((const uc_t) * *str) || **str == '/' || **str == '.') {
        (*str)++;
    }
}

static uc_t skip_rest_ofl(const char **text)
{
    /* consumes '\\\n' as a non-line break; treats '\\\\' as an error */
    while (**text && **text != '\n') {
        if (**text == '\\') {
            (*text)++;
            if (**text == '\n') {
                (*text)++;
                continue;
            } else if (**text == '\\') {
                print("double line break in make config");
                return 1;
            }
        } else (*text)++;
    }
    while (**text == '\n') (*text)++;
    if (!**text) return 1;
    return 0;
}

#define fnext_char(text, esig, rt)                    \
    {                                                 \
        (*text)++;                                    \
        if (!**text) {                                \
            print("parser unexpectedly hit the EOF"); \
            (esig) |= CONFIG_ERRO;                    \
            return rt;                                \
        }                                             \
    }

#define char_assert(need, have, esig, rt)                     \
    {                                                         \
        if ((have) != need) {                                 \
            fprintf(stderr, "need=%c have=%c\n", need, have); \
            (esig) |= PARSING_ERRO;                           \
            return rt;                                        \
        }                                                     \
    }

typedef enum {
    CONFIG_ERRO  = 0x01,
    MEMORY_ERRO  = 0x02,
    PARSING_ERRO = 0x04
} erro_t;

static uc_t append_tok(token **tokens, len_t *cap, len_t *have, const char **str,
    uc_t *erro_b)
{
    /* returns 1 for token appended, 0 for no token and/or error */
    const char *off = 0;
    if (skip_ws(str)) return 0;
    if (**str == '\\') {
        (*str)++;
        char_assert('\n', **str, *erro_b, 0);
        (*str)++;
        skip_ws(str);
    }
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

static uc_t build_header_specd(const string *const header,
    const string *const current)
{
    /* returns true if the input token == runtime header spec */
    return (header->len == current->len &&
        !memcmp(header->str, current->str, header->len));
}

static char *bbuild_sl(const Dset *const series);

typedef uc_t (*pasted_tokens)(const char *, const len_t,
    token **, len_t *, len_t *, const Dset *const, const len_t, uc_t *,
    const Format *, const char *, const len_t);

static uc_t expand_sub_token(const char *tstr, const len_t tlen, token **tokbuf,
    len_t *tcap, len_t *have, const Dset *const gset, const len_t nsets, uc_t *err,
    const Format *fmt, const char *mast, const len_t mlen)
{
    /* parses & pastes directive sequences                                 *
       unlike non-directive series, series/tokens pasted in directives are *
       fully expanded before being 'pasted' into the directive's output    */
    len_t ns     = 0;
    len_t pasted = 0;
    char *paste_expanded;
    (void)fmt;
    (void)mlen;
    (void)mast;
    while (ns < nsets) {
        if (*(tstr + 2) == '^') {
            if (gset[ns].master.len != tlen - 4) MCONTINUE;
            if (memcmp(gset[ns].master.str, tstr + 3, tlen - 4)) {
                MCONTINUE;
            }
        } else if (gset[ns].master.len != tlen - 3 ||
            memcmp(gset[ns].master.str, tstr + 2, tlen - 3)) {
            MCONTINUE
        }
        /* use the native expander to parse the pasted series */
        paste_expanded = bbuild_sl(&gset[ns]);
        if (!paste_expanded) {
            print("failed to build pasted tokens expandion");
            *err |= PARSING_ERRO;
            return 1;
        }
        if (valtok(tokbuf, tcap, pasted - 1 + *have,
            paste_expanded, (len_t)strlen(paste_expanded))) {
            print("failed to validate write in of expanded paste");
            *err |= MEMORY_ERRO;
            return 1;
        }
        /* free(paste_expanded); */
        (*tokbuf)[pasted - 1 + *have].pt_each   = def_each;
        (*tokbuf)[pasted - 1 + *have].pt_sep    = def_sep;
        (*tokbuf)[pasted - 1 + *have].allocated = paste_expanded;
        pasted++;
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

static uc_t find_sub_token(const char *tstr, const len_t tlen, token **tokbuf,
    len_t *tcap, len_t *have, const Dset *const gset, const len_t nsets, uc_t *err,
    const Format *fmt, const char *mast, const len_t mlen)
{
    /* parses & pastes non-directive sequences - find referenced directive & copy over formatting */
    len_t ns          = 0;
    len_t wt          = 0;
    len_t pasted      = 0;
    uc_t override_fmt = 0;
    while (ns < nsets) {
        if (*(tstr + 2) == '^') {
            if (gset[ns].master.len != tlen - 4) MCONTINUE;
            if (memcmp(gset[ns].master.str, tstr + 3, tlen - 4)) {
                MCONTINUE;
            }
            override_fmt = 1;
        } else {
            if (gset[ns].master.len != tlen - 3) MCONTINUE;
            if (memcmp(gset[ns].master.str, tstr + 2, tlen - 3)) {
                MCONTINUE
            }
        }
        wt = 0;
        while (wt < gset[ns].n_tok) {
            if (valtok(tokbuf, tcap, pasted - 1 + *have, /* compensate */
                    gset[ns]._token_[wt].tk.str, gset[ns]._token_[wt].tk.len)) {
                *err |= MEMORY_ERRO;
                print("failed to validate pasted-token write in");
                return 1;
            }
            /* write the original tokens' each, sep, & origin into the result */
            if (!override_fmt) {
                (*tokbuf)[pasted - 1 + *have].pt_each = gset[ns]._token_[wt].pt_each;
                (*tokbuf)[pasted - 1 + *have].pt_sep  = gset[ns]._token_[wt].pt_sep;
                (*tokbuf)[pasted - 1 + *have].origin  = gset[ns]._token_[wt].origin;
            } else {
                (*tokbuf)[pasted - 1 + *have].pt_each     = fmt->each;
                (*tokbuf)[pasted - 1 + *have].pt_sep      = fmt->sep;
                (*tokbuf)[pasted - 1 + *have].origin.str  = mast;
                (*tokbuf)[pasted - 1 + *have].origin.len  = mlen;
            }
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

static uc_t assign_named_fmts(Format *const fmt,
    NFormatBuf *const nfbuf, const string *const name)
{
    /* search known-named-formats against referenced schema */
    len_t ns = 0;
    while (ns < nfbuf->n_formats) {
        if (strs_match(name->str, name->len,
                nfbuf->formats[ns].name.str,
                nfbuf->formats[ns].name.len)) {
            *fmt = nfbuf->formats[ns].fmt;
            return 0;
        }
        ns++;
    }
    fprintf(stderr, "found no named formats of: '%.*s'\n",
        name->len, name->str);
    return 1;
}

static uc_t specify_format(const char **text, Format *const fmt,
    NFormatBuf *const nfbuf, uc_t *errosig)
{
    /* digests a string ("[pre="-I{m}", ...]") into a Format obj */
    string need;
    const char *have;
    string *set;
    if (**text == __FORMAT_PASTE__) {
        fnext_char(text, *errosig, 1);
        need.str = *text;
        skip_tok(text);
        if (!**text || !(*text - need.str)) {
            *errosig |= PARSING_ERRO;
            print("0-width named formatting");
            return 1;
        }
        need.len = (len_t)(*text - need.str);
        if (assign_named_fmts(fmt, nfbuf, &need)) {
            *errosig |= PARSING_ERRO;
            return 1;
        }
        char_assert(']', **text, *errosig, 1);
        fnext_char(text, *errosig, 1);
        return 0;
    }
    while (**text) {
        switch (**text) {
        case 'p':
            need.str = "pre";
            need.len = sizeof "pre" - 1;
            set      = &fmt->pre;
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

        /* no length/width check; ="" allowed */
        set->len = (len_t)(*text - set->str);
        fnext_char(text, *errosig, 1);
        if (skip_ws(text)) return 1;
        if (**text && **text == ']') {
            (*text)++;
            return 0;
        }
        if (**text == ',') fnext_char(text, *errosig, 1);
        if (skip_ws(text)) return 1;
    }
    *errosig |= PARSING_ERRO;
    print("formatting spec hit EO-input before parsing finished");
    return 1;
}

static uc_t next_set(const char **text, Dset *set_, len_t *ns,
    const Dset *const gset, NFormatBuf *nmformat, Header *header,
    const string *const build_header)
{
    /* walks input make; scans & stores each `series`; handles formatting, *
       dispatch, paste-calling, comment-skipping, & @header tracking       */
    Format fmt;
    string fname;
    pasted_tokens paste_fn;
    token *_token_   = 0;
    NamedFormat *nf_ = 0;
    const char *ms   = 0;
    len_t mlen       = 0;
    len_t cap        = 0;
    len_t n_tk       = 0;
    uc_t directive   = 0;

    string0(fmt.pre);
    string0(fmt.fin);
    fmt.each = def_each;
    fmt.sep  = def_sep;

    if (!text || !*text || !**text) return 0;
    while (**text == ' ' || **text == '\n') (*text)++;
    if (!**text) return 0;

    switch (**text) {
    /* comment - don't read line -- invert to kill parsing if at EOF */
    case __COMMENT__:
        return skip_rest_ofl(text) ? 0 : 1;

    /* formatting pre-defined; ^format[pre="... */
    case __FORMAT_PASTE__:
        fnext_char(text, header->faultd, 0);
        fname.str = *text;
        skip_tok(text);
        if (!**text || *text == fname.str) {
            print("0-width named format");
            header->faultd |= PARSING_ERRO;
            return 0;
        }
        fname.len = (len_t)(*text - fname.str);
        char_assert('[', **text, header->faultd, 0);
        fnext_char(text, header->faultd, 0);
        if (specify_format(text, &fmt,
                nmformat, /* <= reachable w.: ^a[^b] */
                &header->faultd)) {
            header->faultd |= PARSING_ERRO;
            return 0;
        }
        if (nmformat->n_formats >= nmformat->formatcap) {
            nf_ = realloc(nmformat->formats,
                nmformat->formatcap * 2 * sizeof(NamedFormat));
            if (!nf_) {
                header->faultd |= MEMORY_ERRO;
                print("NOMEM");
                return 1;
            }
            nmformat->formats = nf_;
            nmformat->formatcap *= 2;
        }
        nmformat->formats[nmformat->n_formats].fmt  = fmt;
        nmformat->formats[nmformat->n_formats].name = fname;
        nmformat->n_formats++;
        return 1;

    /* specify alternative build header */
    case __HEADER__:
        /* ATP - should only apply to the literal 'make's, 'clean's, and 'runs'; *
           everything else should be globally referenceable                      */
        (*text)++;
        ms = *text;
        skip_tok(text);
        if (ms == *text) {
            print("0-width header");
            header->faultd |= CONFIG_ERRO;
            free(_token_);
            return 0;
        }
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

    default:
        break;
    }

    ms = *text;
    skip_tok(text);
    if (!**text || *text == ms) return 0;
    mlen = (len_t)(*text - ms);
    directive = is_directive(ms, mlen);
    paste_fn  = directive ? expand_sub_token : find_sub_token;

    /* only duplicate guard over series' names are the 3-directive types */
    if (directive && !build_header_specd(build_header, &header->header)) {
        return skip_rest_ofl(text) ? 0 : 1; /* invert to kill parsing */
    }

    /* set must be prepended with > 0 alnum's (or '/' or '.') followed by ':'
       & must be at BOL or preceeded only by whitespace */
    if (skip_ws(text)) return 0;
    char_assert(__ASSIGNMENT__, **text, header->faultd, 0);
    fnext_char(text, header->faultd, 0);
    if (skip_ws(text)) {
        header->faultd |= CONFIG_ERRO;
        print("hit EOF; parsing failed");
        return 0;
    }
    /* specify the formatting to a series after defining it */
    if (**text == __FORMAT__) {
        (*text)++;
        if (specify_format(text, &fmt, nmformat, &header->faultd)) {
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
        /* assign EVERY token its original format & master */
        _token_[n_tk - 1].pt_sep     = fmt.sep;
        _token_[n_tk - 1].pt_each    = fmt.each;
        _token_[n_tk - 1].origin.str = ms;
        _token_[n_tk - 1].origin.len = mlen;
        if (_token_[n_tk - 1].tk.len > 3 && /* need atleast '$(+)' */
                _token_[n_tk - 1].tk.str[0] == __SUBSTITUTION__ &&
                _token_[n_tk - 1].tk.str[1] == '(' &&
                _token_[n_tk - 1].tk.str[_token_[n_tk - 1].tk.len - 1] == ')') {
            if (paste_fn(_token_[n_tk - 1].tk.str, _token_[n_tk - 1].tk.len,
                    &_token_, &cap, &n_tk, gset, *ns, &header->faultd,
                    &fmt, ms, mlen)) {
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
    set_->master.str = ms;
    set_->master.len = mlen;
    set_->fmt        = fmt;
    (*ns)++;
    return 1;
}

static void free_sets(Dset *sets, const len_t n_sets)
{
    len_t i = 0;
    len_t t = 0;
    if (!sets) return;
    while (i < n_sets) {
        t = 0;
        while (t < sets[i].n_tok) {
            free(sets[i]._token_[t].allocated);
            t++;
        }
        /* free(sets[i]._token_->allocated); */
        /* if (sets[i]._token_->allocated) free(sets[i]._token_->allocated); */
        free(sets[i]._token_);
        i++;
    }
    free(sets);
}

static uc_t parse_tokens(const char *text, BB *bb,
    const string *const build_header)
{
    /* next_set() wrapper / allocation handler                              *
       parses make into sets of series assigned to their respective headers */
    Header head;
    NFormatBuf nfbuf;
    Dset *repl;
    len_t Dc     = 0;
    len_t setcap = 16;
    Dset *sets   = calloc(setcap, sizeof(Dset));
    if (!sets) {
        print("NOMEM");
        return 1;
    }
    nfbuf.n_formats = 0;
    nfbuf.formatcap = 4;
    nfbuf.formats   = malloc(nfbuf.formatcap * sizeof(NamedFormat));
    if (!nfbuf.formats) {
        free(sets);
        print("NOMEM");
        return 1;
    }
    string0(head.header);
    head.faultd = 0;
    while (next_set(&text, &sets[Dc], &Dc, sets, &nfbuf, &head, build_header) &&
        !head.faultd) {
        if (Dc >= setcap) {
            repl = realloc(sets, setcap * 2 * sizeof(Dset));
            if (!repl) {
                free(nfbuf.formats);
                print("NOMEM");
                goto fcleanup;
            }
            sets = repl;
            setcap *= 2;
        }
    }
    free(nfbuf.formats);
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

#define fcurs_pplus(cursor, len)              \
    {                                         \
        cursor++;                             \
        if (cursor >= len) {                  \
            print("token expansion hit EOF"); \
            return 1;                         \
        }                                     \
    }

static uc_t _ps_(const string *const master, const token *const tok,
    const string *const fmt, copier_fn func, char **_)
{
    /* expands the input string (i.e., "{m}/{t}.c" to "foo/bar.c") */
    char sig;
    len_t cursor = 0;
    while (cursor < fmt->len) {
        if (fmt->str[cursor] == '{') {
            /* if not a valid reference, copy verbatim (don't expand or skip) */
            if (cursor + 1 >= fmt->len ||
                    (fmt->str[cursor + 1] != 'm' && fmt->str[cursor + 1] != 't') ||
                    cursor + 2 >= fmt->len || fmt->str[cursor + 2] != '}') {
                func("{", 1, _);
                cursor++;
                continue;
            }
            fcurs_pplus(cursor, fmt->len);
            sig = fmt->str[cursor];
            fcurs_pplus(cursor, fmt->len);
            if (fmt->str[cursor] != '}') {
                print("unclosed bracket in formatting spec");
                return 1;
            }
            cursor++; /* allowed to hit eof */
            if (sig == 'm') func(master->str, master->len, _);
            else if (tok && tok->tk.str)
                func(tok->tk.str, tok->tk.len, _);
            continue;
        }
        func(&fmt->str[cursor], 1, _);
        cursor++;
    }
    return 0;
}

static uc_t _x_(const Dset *const series, copier_fn func, char **_)
{
    /* iterates over a directive and expands their tokens &/or formatting           *
     * prefix & fin get no alternatives for pastes; only sep & each apply per-token *
     * null deref if pre/fin refernces token ({t}) (is guarded), but otherwise only *
       reference-able token in the prefix is token[0]; would not be clear if it was *
       implemented                                                                  */
    len_t n_tokens       = 0;
    const string *master = 0;
    if (_ps_(&series->master, 0, &series->fmt.pre, func, _)) return 1;
    while (n_tokens < series->n_tok) {
        master = &series->_token_[n_tokens].origin;
        if (_ps_(master, &series->_token_[n_tokens],
                &series->_token_[n_tokens].pt_each, func, _)) {
            return 1;
        }
        if  (n_tokens < series->n_tok - 1) {
            if (_ps_(master, &series->_token_[n_tokens],
                    &series->_token_[n_tokens].pt_sep, func, _)) {
                return 1;
            }
        } else {
            if (_ps_(&series->master, 0, &series->fmt.fin, func, _)) {
                return 1;
            }
        }
        n_tokens++;
    }
    return 0;
}

static char *bbuild_sl(const Dset *const series)
{
    /* count + allocate + expand + copy a directive *
       returns an allocated buffer                  */
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

static uc_t run_cmd(char *const command)
{
    int rc;
    pid_t pid;
    char *argv[4];
    argv[0] = "sh";
    argv[1] = "-c";
    argv[2] = command;
    argv[3] = 0;
    if (posix_spawn(&pid, "/bin/sh", 0, 0, argv, environ)) {
        print("FAILED TO EXECUTE");
        return 1;
    }
    if (waitpid(pid, &rc, 0) == -1) {
        print("FAILED TO WAIT FOR PROCESS FIN");
        return 1;
    }
    if (WIFEXITED(rc) && !WEXITSTATUS(rc)) {
        print("proc OK");
        return 0;
    }
    print("SYSTEM ERRO WHILE RUNNING COMMAND");
    return 1;
}

static uc_t run_sl(const Dset *const series, const int debug)
{
    /* run a single command (return 1 on error) */
    uc_t rc;
    char *cmd = bbuild_sl(series);
    if (!cmd) {
        print("building single-series command failed");
        return 1;
    }
    if (!debug) {
        rc = run_cmd(cmd);
        free(cmd);
        return rc;
    } else {
        _out_(cmd, strlen(cmd));
        print("");
    }
    free(cmd);
    return 0;
}

static Dset *find_spec(const BB *const bb, const char *const arg, const len_t arglen)
{
    /* require one match of requested directive; if 0 or > 1 found, bail */
    len_t ns  = 0;
    int found = -1;
    while (ns < bb->ns) {
        if (strs_match(bb->series[ns].master.str, bb->series[ns].master.len,
                arg, arglen)) {
            if (found != -1) {
                print("DUPLICATE DIRECTIVES FOUND IN BUILD");
                return 0;
            }
            found = (int)ns;
            MCONTINUE;
        }
        ns++;
    }
    if (found == -1) {
        print("could not find specified directive in parsed build");
        return 0;
    }
    return &bb->series[found];
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
    default: /* unreachable */
        print("unknown runtime spec; defaulting to make");
        return find_spec(bb, buildstrs[0].str, buildstrs[0].len);
    }
}

typedef struct {
    int _build_;
    uc_t debug;
    string header;
} Args;

#define UNK_ARG(arg) { fprintf(stderr, "unknown argument='%s'\n", arg); }

static uc_t parse_args(const int argc, const char *argv[], Args *args)
{
    int a = 1;
    len_t need_arglen;
    const char *need_arg;
    char *dir_spec = 0;
    while (a < argc) {
        switch (argv[a][0]) {
        case '-':
            if (dir_spec) {
                print("multiple alternate directories specified");
                return 1;
            }
            if (argv[a][1] != 'C' || argv[a][2] != ':' || !argv[a][3]) return 1;
            need_arglen = (len_t)strlen(argv[a]) - 3;
            /* let '-C:' (0-length directory) fallthrough to no-op chdir */
            dir_spec = malloc(need_arglen + 1 + 1); /* + '/' + '\0' */
            if (!dir_spec) {
                print("NOMEM");
                return 1;
            }
            memcpy(dir_spec, argv[a] + 3, need_arglen);
            dir_spec[need_arglen] = '\0';

            fprintf(stderr, "alternate directory specified: %s\n", dir_spec);
            if (chdir(dir_spec)) {
                free(dir_spec);
                print("failed to change directories; can't run requested build");
                return 1;
            }
            free(dir_spec);
            dir_spec = "a"; /* NOT NULL! check if re-specified */
            a++;
            continue;
        case '@':
            if (args->header.len || args->header.str) return 1;
            args->header.str = argv[a] + 1;
            args->header.len = (len_t)strlen(argv[a]) - 1;
            a++;
            continue;
        /* set as if successful, full bail on any error */
        case 'm':
            if (args->_build_ != -1) return 1;
            need_arg      = "make";
            need_arglen   = sizeof "make" - 1;
            args->_build_ = __MAKE__;
            break;
        case 'c':
            if (args->_build_ != -1) return 1;
            need_arg      = "clean";
            need_arglen   = sizeof "clean" - 1;
            args->_build_ = __CLEAN__;
            break;
        case 'r':
            if (args->_build_ != -1) return 1;
            need_arg      = "run";
            need_arglen   = sizeof "run" - 1;
            args->_build_ = __RUN__;
            break;
        case 'd':
            if (args->debug) return 1;
            need_arg    = "debug";
            need_arglen = sizeof "debug" - 1;
            args->debug = 1;
            break;
        default:
            UNK_ARG(argv[a]);
            return 1;
        }
        if (strlen(argv[a]) != need_arglen ||
                memcmp(argv[a], need_arg, need_arglen)) {
            UNK_ARG(argv[a]);
            return 1;
        }
        a++;
    }
    if (args->_build_ == -1) args->_build_ = __MAKE__; /* default */
    return 0;
}

#define cleanup                    \
    {                              \
        free(make);                \
        make = 0;                  \
        free_sets(b.series, b.ns); \
        b.series = 0;              \
        b.ns     = 0;              \
        free_fbuf();               \
    }

int main(const int argc, const char *argv[])
{
    BB b;
    Args args;
    int rt      = 0;
    char *make  = 0;
    Dset *spec  = 0;

    args._build_     = -1;
    args.debug       = 0;
    b.series         = 0;
    b.ns             = 0;
    string0(args.header);

    if (parse_args(argc, argv, &args)) {
        cleanup;
        print("parsing arguments failed");
        return 1;
    }
    make = mann_mm(FSRC);

    if (!make) { cleanup; return 1; }
    if (parse_tokens(make, &b, &args.header)) {
        cleanup;
        return 1;
    }
    spec = find_parsed_spec(&b, args._build_);
    if (!spec) {
        cleanup;
        print("(WARNING) found no specifications for requested build in make");
        return 1;
    }
    rt = run_sl(spec, args.debug);
    cleanup;
    return rt;
}
