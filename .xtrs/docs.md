# AMAKER - DOCUMENTATION

`./amake [make/run/clean] [debug] [@header]`

Amake parses the <make> file in the CWD & runs* the specified command
  - without a command specified, the default is `make`
    - if run with: `debug`, print the command without executing it
    - if no: `make:` set or specified, quit w.erro msg

(DESIGN NOTE) without token-types, there is no type-checking or assertions on tokens
within a build; the only checks done are on the directives called to avoid
duplicative 'make:'s, 'clean:'s, or 'run:'s within the same header
    `./amake`:
    `./amake make`:
        assert that there is a make directive && there is exactly one of them
    `./amake run`:
        assert that there is a command to run && there is exactly one of them
    `./amake clean`:
        assert that there is a clean directive && there is exactly one of them

## make syntax:
`series: [pre, each, sep, fin] a, b, c, ...`

### Format:
- all the strings (w.caveats) accept token references inside them:
    - `{m}` references the name an entry was listed under, i.e.:
        `foo` from `foo: a, b, c`
    - `{t}` references the name of the tokens of an entry - limited to each/sep
        *** since any/all types can have more than 1 entry, referencing
            the token in the prefix is guarded and effectively a no-op;
            if the entries *did* have more than 1 entry, doing so would
            silently truncate `tokens[1:n_tokens - 1]` which is just not
            worth the benefit of a weird obfuscated paste. ***
    *** `=""` is allowed and a valid no-op for all fields ***
    *** leaving a field empty leaves the default set from: ***
            `[pre="", each="{t}", sep=" ", fin=""]` (*<= default Format*)
    pre:
        emitted once at the start of the series' entries
        *not affected by tokens pasted into the series*
    each:
        emitted once per n-tokens of the series
        pasted tokens retain their original each & sep
        but adopt their series pre & fin (*if not a directive*)
    sep:
        emitted once per n-tokens - 1
        *master to first token gets no sep and the last token*
        *to fin gets the series' fin*
    fin:
        emitted once at the end of the series in-place of sep
        *not affected by tokens pasted into the series*

Examples:
"""
HEADER: [pre="[ ", each="{m}_{t}", sep=", ", fin=" ]"] a, b, c
make: $(HEADER)
"""
... yields:
    `[ HEADER_a, HEADER_b, HEADER_c ]`

"""
bar: [pre="-I{m} ", each="{m}/{t}.c", sep=" ", fin=""] buffer stringops compilation
make: $(bar)
"""
... yields:
    `-Ibar bar/buffer.c bar/stringops.c bar/compilation.c`


### Pasting:
    when a series is pasted into another, the formatting of its tokens
    is saved per token, so native tokens retain their specified paste
    and the pasted tokens retain their formatting -&&- their original
    {m} reference

Example:
"""
foo: [each="{m}/{t}.c", sep=" | "] AAA, BBB, CCC
bar: [each="{m}/{t}.o", sep=" "] XXX, YYY, ZZZ $(foo)
make: $(bar)
"""
... yields:
    `bar/XXX.o bar/YYY.o bar/ZZZ.o foo/AAA.c | foo/BBB.c | foo/CCC.c`
  -- ** && ** --
"""
bar: [each="{m}/{t}.o", sep=" "] XXX, YYY, ZZZ
foo: [each="{m}/{t}.c", sep=" | "] AAA, BBB, CCC $(bar)
make: $(foo)
"""
... yields:
    `foo/AAA.c | foo/BBB.c | foo/CCC.c | bar/XXX.o bar/YYY.o bar/ZZZ.o`

** original formatting & {m} references retained through
   paste-expansion, defaulting to the native sep on transition **


### Format Pasting
    predefine a format & apply it to series by name
"""
^foo[pre="rm -f ", each="{m}/{t}.o", sep=" && rm -f ", fin=";"]
src: [^foo] main src
clean: $(src)
"""
... yields:
    `rm -f src/main.o && rm -f src/src.o;`


### Headers
    define multiple builds in a single file by tagging each w.a header

Example:
"""
cc: gcc
targ: lfoo
src: [pre="-I{m} ", each="{m}/{t}.c", sep=" "] main charm
make: $(cc) -o $(targ) $(src)

@static
cc: gcc
targ: libfoo.a
cflag: -c
src: [pre="-I{m} ", each="{m}/{t}.o", sep=" "] main charm
make: $(cc) $(cflag) $(src) -o $(targ)
"""
`./amake make` yields:
    `gcc -o lfoo -Isrc src/main.c src/charm.c`
`./amake make @static` yields:
    `gcc -c -Isrc src/main.o src/charm.o -o libfoo.a`


#### Example Makefile-like make:
"""
cc: gcc
targ: light
bar: [pre="-I{m} ", each="{m}/{t}.c", sep=" "] buffer stringops compilation
src: [pre="-I{m} ", each="{m}/{t}.c", sep=" "] main charm
make: $(cc) -o $(targ) $(src) $(bar)
"""
... yields:
    `gcc -o light -Isrc src/main.c src/charm.c -Ibar bar/buffer.c bar/stringops.c bar/compilation.c`
