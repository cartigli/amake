<!DOCTYPE html>

<h1 class="low_profile_header">AMAKER - DOCUMENTATION</h1>
<span class="inline_code">
    ./amake [make/run/clean] [debug] [@header]
</span>
<div>
    <a href="https://cartigli.github.io/amake/">Styled Documentation</a>
</div>
<div>
    <h3 class="low_profile_header">
        Amake parses the <code>make</code> file in the CWD & runs the specified command
    </h3>
    <p style="margin:0 0 2px">without a command specified, the default is <span class="tiny_code">make</span></p>
    <p class="indented_text">
        - if run with <span class="tiny_code"
            >debug</span>, print the command without executing it<br>
        - if no <span class="tiny_code"
            >make:</span> set or specified, quit w.erro msg<br>
    </p>
</div>

<div>
    <p>
        <b>DESIGN NOTE</b><br>
        Without token-types, there is no type-checking or assertions on tokens<br>
        within a build; the only checks done are on the directives called to avoid<br>
        duplicative <span class="tiny_code">make:</span>'s, <span class="tiny_code">clean:</span>'s, or <span class="tiny_code">run:</span>'s
        under the same header<br>
    </p>
    <div style="margin-top: 4px; margin-left: 4px;">
        Therefore, the only assertions or validations of the command specified is:<br>
        <div class="indented_text">
            1. The existence of the requested directive (i.e., assert <span class="tiny_code">make</span> exists if <span class="tiny_code">make</span> was run)<br>
            2. The requested directive is not defined more than once under the given header.
        </div>
    </div>
</div>

<h2 class="low_profile_header">make syntax:</h2>
<div><span class="tiny_code">series: [pre, each, sep, fin] a, b, c, ...</span></div>

<h3 class="low_profile_header">Formatting:</h3>
<div>
    All the strings (w.caveats) accept token references inside them:<br>
    <div class="indented_text">
        - <span class="tiny_code">{m}</span>
            references the name an entry was listed under, i.e.:<br>
        <span class="xtra_indented_text">
            <span class="tiny_code">
                foo
            </span> from <span class="tiny_code">foo: a, b, c</span>
        </span><br>
        - <span class="tiny_code">{t}</span> references the name of the tokens of an entry (limited to each/sep)<br>
        <div class="xtra_indented_text">
            <i>since any/all types can have more than 1 entry, referencing</i><br>
            <i>the token in the prefix is guarded and effectively a no-op;</i><br>
            <i>if the entries <b>did</b> have more than 1 entry, doing so would</i><br>
            <i>silently truncate </i><span class="inline_code">tokens[1:n_tokens - 1]</span><i> which is just not</i><br>
            <i>worth the benefit of a weird obfuscated paste.</i><br>
        </div>
        <span class="tiny_code">=""</span> is allowed and a valid no-op for all fields<br>
        <div class="xtra_indented_text">
            leaving a field empty leaves the default set from:<br>
        </div>
        <div class="xxtra_indent">
            <span class="tiny_code">[pre="", each="{t}", sep=" ", fin=""]</span> (<i><= default Format</i>)<br>
        </div>
        <span class="tiny_code">pre</span>:<br>
        <div class="xtra_indented_text">
                emitted once at the start of the series' entries<br>
                <i>not affected by tokens pasted into the series</i><br>
        </div>
            <span class="tiny_code">each</span>:<br>
        <div class="xtra_indented_text">
                emitted once per n-tokens of the series<br>
                pasted tokens retain their original each & sep<br>
                but adopt their series pre & fin (<i>if not a directive</i>)<br>
        </div>
            <span class="tiny_code">sep</span>:<br>
        <div class="xtra_indented_text">
                emitted once per n-tokens - 1<br>
                <i><span class="tiny_code">pre</span> ==> <span class="tiny_code">token[0]</span> gets no <span class="tiny_code">sep</span> and the</i><br>
                <i><span class="tiny_code">token[n_tokens - 1]</span> ==> <span class="tiny_code">fin</span> gets the series' <span class="tiny_code">fin</span> instead of a <span class="tiny_code">sep</span></i><br>
        </div>
            <span class="tiny_code">fin</span>:<br>
        <div class="xtra_indented_text">
                emitted once at the end of the series in-place of sep<br>
                <i>not affected by tokens pasted into the series</i><br>
        </div>
    </div>
</div>
<h4 class="low_profile_header">Examples:</h4>
<div class="code_block">
HEADER: [pre="[ ", each="{m}_{t}", sep=", ", fin=" ]"] a, b, c<br>
make: $(HEADER)<br>
</div>
yields:<span class="inline_code">[ HEADER_a, HEADER_b, HEADER_c ]</span><br>
<div class="indented_text">
    <p style="margin-top: 6px; margin-bottom: 6px;">
        &&
    </p>
</div>
<div class="code_block">
bar: [pre="-I{m} ", each="{m}/{t}.c", sep=" ", fin=""] buf strops comp<br>
make: $(bar)<br>
</div>
yields:
<span class="inline_code">
    -Ibar bar/buf.c bar/strops.c bar/comp.c
</span></br>
<br>
<h3 class="low_profile_header">Pasting:</h3>
<div class="indented_text">
    when a series is pasted into another, the formatting of its tokens<br>
    is saved per token, so native tokens retain their specified paste<br>
    and the pasted tokens retain their formatting -&&- their original<br>
    <span class="tiny_code">{m}</span> reference
</div>
<div>
    <p>
        <b>
            Example:
        </b>
    </p>
</div>
<div class="code_block">
foo: [each="{m}/{t}.c", sep=" | "] AAA, BBB, CCC<br>
bar: [each="{m}/{t}.o", sep=" "] XXX, YYY, ZZZ $(foo)<br>
make: $(bar)<br>
</div>
yields:<span class="inline_code"
    >bar/XXX.o bar/YYY.o bar/ZZZ.o foo/AAA.c | foo/BBB.c | foo/CCC.c
</span>
<div class="indented_text"><p>&&</p></div>
<div class="code_block">
bar: [each="{m}/{t}.o", sep=" "] XXX, YYY, ZZZ<br>
foo: [each="{m}/{t}.c", sep=" | "] AAA, BBB, CCC $(bar)<br>
make: $(foo)<br>
</div>
yields:<span class="inline_code"
    >foo/AAA.c | foo/BBB.c | foo/CCC.c | bar/XXX.o bar/YYY.o bar/ZZZ.o<br>
</span>
<div class="indented_text"><i>
    original formatting & <span class="tiny_code">{m}</span> references retained through<br>
    paste-expansion, defaulting to the native <span class="tiny_code">sep</span> on transition<br></i>
</div>
<br>
<h3 class="low_profile_header">Format Pasting</h3>
<div class="indented_text">
    predefine a format & apply it to series by name</div>
<div class="code_block">
^foo[pre="rm -f ", each="{m}/{t}.o", sep=" && rm -f ", fin=";"]<br>
src: [^foo] main src<br>
clean: $(src)<br>
</div>
yields:<span class="inline_code">rm -f src/main.o && rm -f src/src.o;</span>
<br>
<h3 class="low_profile_header">Headers</h3>
<div class="indented_text">
    define multiple builds in a single file by tagging each w.a header
</div>
<h5 class="low_profile_header">Example:</h5>
<div class="code_block">
cc: gcc<br>
targ: lfoo<br>
src: [pre="-I{m} ", each="{m}/{t}.c", sep=" "] main charm<br>
make: $(cc) -o $(targ) $(src)<br>
<br>
@static<br>
cc: gcc<br>
targ: libfoo.a<br>
cflag: -c<br>
src: [pre="-I{m} ", each="{m}/{t}.o", sep=" "] main charm<br>
make: $(cc) $(cflag) $(src) -o $(targ)<br>
</div>
<div class="indented_text">
    <span class="tiny_code">./amake make</span> yields:<br>
    <div class="xtra_indented_text">
        <div class="inline_code">
            <div style="margin-bottom: 2px; margin-top: 2px;">
                gcc -o lfoo -Isrc src/main.c src/charm.c<br>
            </div>
        </div>
    </div>
    <span class="tiny_code">./amake make @static</span> yields:<br>
    <div class="xtra_indented_text">
        <div class="inline_code">
            <div style="margin-bottom: 2px; margin-top: 2px;">
                gcc -c -Isrc src/main.o src/charm.o -o libfoo.a<br>
            </div>
        </div>
    </div>
</div>
<br>
<h4 class="low_profile_header">Example Makefile-like make:</h4>
<div class="code_block">
cc: gcc<br>
targ: light<br>
bar: [pre="-I{m} ", each="{m}/{t}.c", sep=" "] buffer stringops compilation<br>
src: [pre="-I{m} ", each="{m}/{t}.c", sep=" "] main charm<br>
make: $(cc) -o $(targ) $(src) $(bar)<br>
</div>
<div>
    yields:
    <div class="indented_text">
        <div class="inline_code">
            gcc -o light -Isrc src/main.c src/charm.c -Ibar bar/buffer.c bar/stringops.c bar/compilation.c
        </div>
    </div>
</div>
<br>
<h4 class="low_profile_header">Another example - genuine implementation</h4>
<div class="code_block">
cc: gcc<br>
targ: light<br>
<br>
^f[pre="-I{m} ", each="{m}/{t}.c", sep=" "]<br>
^F[each="-{t}", sep=" "]<br>
<br>
cflags: [^F] std=c99 O0 Wall Wextra Werror Wpedantic Wconversion D_GNU_SOURCE<br>
<br>
lflags: [^F] lbmk<br>
<br>
src: [^f] main charm<br>
<br>
utils/logg: [^f] err<br>
utils/sys:  [^f] fsio os_param<br>
utils/text: [^f] str strmath<br>
<br>
buf:     [^f] buf_load buf_mem<br>
buf/ops: [^f] cut_paste primitive std_ops val_curs<br>
<br>
key:  [^f] key term<br>
edit: [^f] edit indent keypress kp_arrow kp_comment kp_cut_paste<br>
<br>
common/api:        [^f] hlite lexer<br>
common/colors:     [^f] colors color_validation<br>
common/cpu:        [^f] common_attrs core<br>
common/lang:       [^f] lang_c lang_md lang_py language<br>
common/lifetime:   [^f] allocation compiler parser teardown<br>
common/resolution: [^f] dependencies rare scope sort_matches sthread<br>
<br>
make: $(cc) -o $(targ) $(cflags) $(src) $(utils/logg) $(utils/sys) $(utils/text) $(key) $(edit) $(common/api) $(common/colors) $(common/cpu) $(common/lang) $(common/lifetime) $(common/resolution) $(buf) $(buf/ops) $(lflags)<br>
<br>
clean: rm -f $(targ)
</div>

<div class="code_block">
    maker make debug
</div> yields:
<div class="code_block">
    gcc -o light -std=c99 -O0 -Wall -Wextra -Werror -Wpedantic -Wconversion -D_GNU_SOURCE -Isrc src/main.c src/charm.c -Iutils/logg utils/logg/err.c -Iutils/sys utils/sys/fsio.c utils/sys/os_param.c -Iutils/text utils/text/str.c utils/text/strmath.c -Ikey key/key.c key/term.c -Iedit edit/edit.c edit/indent.c edit/keypress.c edit/kp_arrow.c edit/kp_comment.c edit/kp_cut_paste.c -Icommon/api common/api/hlite.c common/api/lexer.c -Icommon/colors common/colors/colors.c common/colors/color_validation.c -Icommon/cpu common/cpu/common_attrs.c common/cpu/core.c -Icommon/lang common/lang/lang_c.c common/lang/lang_md.c common/lang/lang_py.c common/lang/language.c -Icommon/lifetime common/lifetime/allocation.c common/lifetime/compiler.c common/lifetime/parser.c common/lifetime/teardown.c -Icommon/resolution common/resolution/dependencies.c common/resolution/rare.c common/resolution/scope.c common/resolution/sort_matches.c common/resolution/sthread.c -Ibuf buf/buf_load.c buf/buf_mem.c -Ibuf/ops buf/ops/cut_paste.c buf/ops/primitive.c buf/ops/std_ops.c buf/ops/val_curs.c -lbmk
</div>
</doctype>
