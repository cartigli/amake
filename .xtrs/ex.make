
_gcc_: _gcc_
_targ_: light_

+ comment testing
_cflags_: Wall Wextra Werror Wundef Wshadow Wpedantic Wcast-qual
_cflags_: Wconversion Wswitch-enum Wswitch-default Wpointer-arith
_cflags_: Wunreachable-code Wno-dollar-in-identifier-extension
_cflags_: Wmisleading-indentation fdollars-in-identifiers
_cflags_: std=c11 O0 D_GNU_SOURCE

src: main, charm
key: key, term
edit: edit, indent, keypress, kp_arrow, kp_comment, kp_cut_paste
buf: buf_load, buf_mem
buf/ops: cut_paste primitive std_ops val_curs

utils/logg: err
utils/sys: fsio os_param
utils/text: str strmath bmk

foo: bar

common/api: hlite lexer
common/cpu: common_attrs core
common/colors: colors color_validation
common/lang: language lang_c lang_py lang_md
common/lifetime: allocation compiler parser teardown
common/resolution: dependencies rare scope sort_matches sthread

incl: $(foo)

_clean_: _targ_

@alternative
_gcc_: _gcc_
_targ_: hash

_cflags_: Wall Wextra Werror Wundef Wshadow Wpedantic Wcast-qual
_cflags_: Wconversion Wswitch-enum Wswitch-default Wpointer-arith
_cflags_: Wunreachable-code Wno-dollar-in-identifier-extension
_cflags_: Wmisleading-indentation fdollars-in-identifiers
_cflags_: std=c11 O0 D_GNU_SOURCE

common/colors: colors
common/lang: 
key: key hashmap
utils/logg: err
utils/sys: fsio os_param
utils/text: bmk str strmath

+ -I includes w.no files to link directly
buf: 
src: 

_clean_: _targ_
