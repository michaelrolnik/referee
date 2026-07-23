#
#   MIT License
#
#   Copyright (c) 2022-2026 Michael Rolnik
#
#   Permission is hereby granted, free of charge, to any person obtaining a copy
#   of this software and associated documentation files (the "Software"), to deal
#   in the Software without restriction, including without limitation the rights
#   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#   copies of the Software, and to permit persons to whom the Software is
#   furnished to do so, subject to the following conditions:
#
#   The above copyright notice and this permission notice shall be included in all
#   copies or substantial portions of the Software.
#
project(
    'referee',
    ['c', 'cpp'],
    version         : '0.2.0',
    license         : 'MIT',
    meson_version   : '>=1.1',
    default_options : [
        'cpp_std=c++20',
        'buildtype=debug',
        'warning_level=1',
        'b_ndebug=false',
    ],
)

cpp                 = meson.get_compiler('cpp')

add_project_arguments('-Wno-multichar', language : 'cpp')

fs                  = import('fs')

host_system         = host_machine.system()
is_darwin           = host_system == 'darwin'

# ANTLR4 generator: `antlr` launcher or `java -jar <antlr-complete.jar>`
antlr4_jar_option   = get_option('antlr4_jar')
antlr4_jar_guesses  = []
if antlr4_jar_option != ''
    antlr4_jar_guesses += antlr4_jar_option
endif
if is_darwin
    antlr4_jar_guesses += [
        '/opt/homebrew/Cellar/antlr/4.10.1/antlr-4.10.1-complete.jar',
    ]
endif
antlr4_jar_guesses += [
    '/usr/local/lib/antlr-4.10.1-complete.jar',
    '/usr/local/share/antlr/antlr-4.10.1-complete.jar',
    '/usr/share/java/antlr-complete.jar',
]

antlr4_jar          = ''
foreach candidate : antlr4_jar_guesses
    if antlr4_jar == '' and fs.is_file(candidate)
        antlr4_jar  = candidate
    endif
endforeach

# dependencies
fmt_dep             = dependency('fmt')
spdlog_dep          = dependency('spdlog')
cli11_dep           = dependency('CLI11', required : false)
threads_dep         = dependency('threads')
yamlcpp_dep         = dependency('yaml-cpp')

# ANTLR4 C++ runtime: pkg-config, with manual fallback for Homebrew layouts
antlr4_runtime_dep  = dependency(
    'antlr4-runtime',
    required : false,
)
if not antlr4_runtime_dep.found()
    antlr4_lib_candidates   = is_darwin \
        ? ['/opt/homebrew/opt/antlr4-cpp-runtime/lib',
           '/opt/homebrew/lib'] \
        : ['/usr/local/lib', '/usr/lib']
    antlr4_inc_candidates   = is_darwin \
        ? ['/opt/homebrew/opt/antlr4-cpp-runtime/include',
           '/opt/homebrew/opt/antlr4-cpp-runtime/include/antlr4-runtime',
           '/opt/homebrew/include',
           '/opt/homebrew/include/antlr4-runtime'] \
        : ['/usr/local/include',
           '/usr/local/include/antlr4-runtime',
           '/usr/include',
           '/usr/include/antlr4-runtime']

    antlr4_lib_dirs         = []
    foreach d : antlr4_lib_candidates
        if fs.is_dir(d)
            antlr4_lib_dirs += d
        endif
    endforeach

    antlr4_runtime_lib      = cpp.find_library('antlr4-runtime',
        dirs : antlr4_lib_dirs,
    )

    antlr4_inc_existing     = []
    foreach d : antlr4_inc_candidates
        if fs.is_dir(d)
            antlr4_inc_existing += d
        endif
    endforeach

    antlr4_runtime_incdirs  = include_directories(
        antlr4_inc_existing,
        is_system : true,
    )

    # Detect runtime version when pkg-config did not supply one.
    # Brew layout (darwin): /opt/homebrew/opt/antlr4-cpp-runtime is a relative
    # symlink whose final path component is the version (e.g. "4.13.2").
    # Generic layout: scan antlr4_lib_dirs for libantlr4-runtime.<X.Y.Z>.dylib
    # (darwin) or libantlr4-runtime.so.<X.Y.Z> (linux).
    antlr4_rt_detected_ver  = ''
    if is_darwin
        foreach _link : ['/opt/homebrew/opt/antlr4-cpp-runtime',
                         '/usr/local/opt/antlr4-cpp-runtime']
            if antlr4_rt_detected_ver == '' and fs.exists(_link)
                _rl = run_command('readlink', _link, check : false)
                if _rl.returncode() == 0
                    _segs = _rl.stdout().strip().split('/')
                    if _segs.length() >= 1
                        _tail = _segs[_segs.length() - 1]
                        if _tail.contains('.') and not _tail.contains(' ')
                            antlr4_rt_detected_ver = _tail
                        endif
                    endif
                endif
            endif
        endforeach
    endif
    if antlr4_rt_detected_ver == '' and antlr4_lib_dirs.length() > 0
        # `ls` the lib dirs and look for a versioned soname/dylib filename.
        _pat = is_darwin ? 'libantlr4-runtime.*.dylib' : 'libantlr4-runtime.so.*'
        _lsargs = ['sh', '-c',
                   'for d in "$@"; do ls "$d"/' + _pat + ' 2>/dev/null; done',
                   'sh'] + antlr4_lib_dirs
        _ls = run_command(_lsargs, check : false)
        foreach _line : _ls.stdout().strip().split('\n')
            if antlr4_rt_detected_ver == '' and _line != ''
                _segs = _line.split('/')
                _base = _segs[_segs.length() - 1]
                # darwin:  libantlr4-runtime.4.13.2.dylib -> 4.13.2
                # linux:   libantlr4-runtime.so.4.13.2    -> 4.13.2
                if is_darwin
                    _v = _base.replace('libantlr4-runtime.', '').replace('.dylib', '')
                else
                    _v = _base.replace('libantlr4-runtime.so.', '')
                endif
                if _v.contains('.') and not _v.contains('/')
                    antlr4_rt_detected_ver = _v
                endif
            endif
        endforeach
    endif

    if antlr4_rt_detected_ver != ''
        antlr4_runtime_dep  = declare_dependency(
            dependencies        : antlr4_runtime_lib,
            include_directories : antlr4_runtime_incdirs,
            version             : antlr4_rt_detected_ver,
        )
    else
        antlr4_runtime_dep  = declare_dependency(
            dependencies        : antlr4_runtime_lib,
            include_directories : antlr4_runtime_incdirs,
        )
    endif
endif

# LLVM components
llvm_modules        = [
    'support',
    'core',
    'irreader',
    'orcjit',
    'aarch64asmparser',
    'aarch64codegen',
    'aarch64info',
    'x86asmparser',
    'x86codegen',
    'x86desc',
    'x86disassembler',
    'x86info',
]

llvm_dep            = dependency(
    'llvm',
    version  : '>=19',
    modules  : llvm_modules,
    required : false,
)

# fallback: invoke llvm-config directly (Homebrew llvm is keg-only)
if not llvm_dep.found()
    llvm_config_dirs    = []
    if is_darwin and fs.is_dir('/opt/homebrew/opt/llvm/bin')
        llvm_config_dirs += '/opt/homebrew/opt/llvm/bin'
    endif

    llvm_config         = find_program(
        'llvm-config',
        dirs     : llvm_config_dirs,
        required : true,
    )

    llvm_cppflags_raw   = run_command(
        llvm_config, '--cppflags',
        check : true,
    ).stdout().strip().split()
    llvm_ldflags_raw    = run_command(
        llvm_config, '--ldflags',
        check : true,
    ).stdout().strip().split()
    llvm_libs_raw       = run_command(
        llvm_config, '--libs', llvm_modules,
        check : true,
    ).stdout().strip().split()
    llvm_syslibs_raw    = run_command(
        llvm_config, '--system-libs',
        check : true,
    ).stdout().strip().split()

    llvm_dep            = declare_dependency(
        compile_args : llvm_cppflags_raw,
        link_args    : llvm_ldflags_raw + llvm_libs_raw + llvm_syslibs_raw,
    )
endif

# GoogleTest
gtest_dep           = dependency('gtest', required : false)
gtest_main_dep      = dependency('gtest_main', required : false)
if not gtest_dep.found()
    gtest_dep       = declare_dependency(
        dependencies : cpp.find_library('gtest'),
    )
endif
if not gtest_main_dep.found()
    gtest_main_dep  = declare_dependency(
        dependencies : cpp.find_library('gtest_main'),
    )
endif

# ANTLR4 parser generation
antlr4_outputs      = [
    'refereeLexer.cpp',
    'refereeLexer.h',
    'refereeParser.cpp',
    'refereeParser.h',
    'refereeVisitor.cpp',
    'refereeVisitor.h',
    'refereeBaseVisitor.cpp',
    'refereeBaseVisitor.h',
    'refereeListener.cpp',
    'refereeListener.h',
    'refereeBaseListener.cpp',
    'refereeBaseListener.h',
]

antlr4_common_args  = [
    '-Dlanguage=Cpp',
    '-visitor',
    '-listener',
    '-package', 'referee',
    '-Xexact-output-dir',
    '-o', '@OUTDIR@',
    '@INPUT@',
]

antlr4_launcher     = find_program(
    'antlr4', 'antlr', 'antlr4.sh',
    required : false,
)

# Prefer an explicit jar over a distro launcher: the `antlr4` packaged by many
# distros is older than the installed `antlr4-runtime`, and generates code that
# no longer compiles against it (e.g. removed `getTokenNames`, `SerializedATNView`
# changes). Jars can be pinned to match the runtime (4.10+ recommended).
if antlr4_jar != ''
    java_prog       = find_program('java', required : true)
    antlr4_command  = [java_prog, '-jar', antlr4_jar] + antlr4_common_args
elif antlr4_launcher.found()
    antlr4_command  = [antlr4_launcher] + antlr4_common_args
else
    error(
        'Could not find an ANTLR4 generator. Install an `antlr`/`antlr4` ' +
        'launcher in PATH (e.g. `apt install antlr4`, `dnf install antlr4`, ' +
        '`brew install antlr`) or set ' +
        '`-Dantlr4_jar=/path/to/antlr-<version>-complete.jar`.',
    )
endif

# ── ANTLR4 generator / runtime version check ─────────────────────────────────
_gen_ver = 'unknown'
if antlr4_jar != ''
    # antlr-4.10.1-complete.jar  →  4.10.1
    _gen_ver = antlr4_jar.split('antlr-').get(1, '').split('-complete').get(0, 'unknown')
    if not _gen_ver.contains('.') or _gen_ver.contains('/')
        _gen_ver = 'unknown'
    endif
elif antlr4_launcher.found()
    # "ANTLR Parser Generator  Version 4.9.2"  →  4.9.2
    foreach _tok : run_command(antlr4_launcher, '--version', check : false).stdout().strip().split(' ')
        if _tok.contains('.') and not _tok.contains('/')
            _gen_ver = _tok
        endif
    endforeach
endif

_rt_ver = antlr4_runtime_dep.version()
# Meson returns 'undefined' for declare_dependency() without a version keyword
# and may otherwise leak the *project* version through. Treat both as unknown
# so we don't compare the ANTLR generator against referee's own version.
if _rt_ver == 'undefined' or _rt_ver == meson.project_version()
    _rt_ver = 'unknown'
endif

message('ANTLR4: generator=' + _gen_ver + '  runtime=' + _rt_ver)
if _gen_ver != 'unknown' and _rt_ver != 'unknown'
    _gen_mm = _gen_ver.split('.').get(0, '') + '.' + _gen_ver.split('.').get(1, '')
    _rt_mm  = _rt_ver .split('.').get(0, '') + '.' + _rt_ver .split('.').get(1, '')
    if _gen_mm != _rt_mm
        error(
            'ANTLR4 generator (@0@) does not match C++ runtime (@1@).\n'.format(_gen_ver, _rt_ver) +
            'Generated parser code will not compile against the installed runtime.\n' +
            'Fix: download a matching jar from https://www.antlr.org/download.html and either:\n' +
            '  * place it at /usr/local/lib/antlr-@0@-complete.jar  (auto-detected)\n'.format(_rt_ver) +
            '  * or pass -Dantlr4_jar=/path/to/antlr-@0@-complete.jar to meson setup'.format(_rt_ver),
        )
    endif
elif _gen_ver == 'unknown'
    warning('ANTLR4: could not determine generator version — version check skipped.')
endif

antlr4_gen          = custom_target(
    'antlr4-generate',
    input           : 'core/referee.g4',
    output          : antlr4_outputs,
    command         : antlr4_command,
)

# include directories
project_inc_dirs    = ['.', 'core']
if fs.is_dir(meson.project_source_root() / 'submodules/rapidcsv/src')
    project_inc_dirs += 'submodules/rapidcsv/src'
else
    warning(
        'submodules/rapidcsv not checked out; run `git submodule update --init --recursive` before building.',
    )
endif
project_inc         = include_directories(project_inc_dirs)

# core static library
core_sources        = [
    'core/visitors/compile.cpp',
    'core/visitors/canonic.cpp',
    'core/visitors/negated.cpp',
    'core/visitors/typecalc.cpp',
    'core/visitors/printer.cpp',
    'core/visitors/rewrite.cpp',
    'core/visitors/csvHeaders.cpp',
    'core/visitors/loader.cpp',
    'core/loaders/row.cpp',
    'core/loaders/csv.cpp',
    'core/loaders/yml.cpp',
    'core/antlr2ast.cpp',
    'core/syntax.cpp',
    'core/strings.cpp',
    'core/module.cpp',
    'core/utils.cpp',
    'rdb/database.cpp',
    'rdb/ingest.cpp',
    'rdb/merge.cpp',
    'referee.cpp',
]

core_lib            = static_library(
    'core',
    core_sources,
    antlr4_gen,
    include_directories : project_inc,
    dependencies        : [fmt_dep, antlr4_runtime_dep, llvm_dep, yamlcpp_dep],
)

core_dep            = declare_dependency(
    link_with           : core_lib,
    sources             : antlr4_gen,
    include_directories : project_inc,
    dependencies        : [fmt_dep, antlr4_runtime_dep, llvm_dep, yamlcpp_dep],
)

# referee CLI
referee_exe         = executable(
    'referee',
    'main.cpp',
    dependencies : [
        core_dep,
        fmt_dep,
        spdlog_dep,
        cli11_dep,
        antlr4_runtime_dep,
        llvm_dep,
    ],
    install      : true,
)

# rdb helper CLI
rdb_exe             = executable(
    'rdb',
    'rdb/main.cpp',
    dependencies        : [
        core_dep,
        fmt_dep,
        spdlog_dep,
        cli11_dep,
        antlr4_runtime_dep,
        llvm_dep,
    ],
    install             : false,
)

# tests

# The .so behind test/logic/extfunc.ref's `func` declarations. Built as a
# plain shared library and handed to the tests by directory, which is exactly
# how a user's plugin reaches referee: via -L.
# The header for the test plugin is generated by referee itself, from the
# same .ref the tests run. That is the point: the plugin's symbols carry a
# structural hash of each signature, so a hand-written declaration would drift
# the moment a type changed. Generating it means the build fails instead.
test_plugin_hdr     = custom_target(
    'extfunc-header',
    input   : 'test/logic/extfunc.ref',
    output  : 'extfunc_gen.h',
    command : [referee_exe, 'header', '@INPUT@', '-o', '@OUTPUT@'],
)

test_plugin         = shared_library(
    'helpers',
    'test/plugins/helpers.c',
    test_plugin_hdr,
    install : false,
)

test_data_dir       = meson.project_source_root() / 'test' / 'logic'

tests_exe           = executable(
    'tests',
    'test/main.cpp',
    'test/strings.cpp',
    'test/canonic.cpp',
    'test/logic.cpp',
    'test/rdb.cpp',
    'test/visitors.cpp',
    'test/cli.cpp',
    'test/diagnostics.cpp',
    'test/json.cpp',
    cpp_args     : [
        '-DREFEREE_TEST_DATA_DIR="' + test_data_dir + '"',
        # The CLI tests drive the real binaries as subprocesses, which is the
        # only way to reach main() -- and, under a coverage build, the only way
        # those translation units are ever counted.
        '-DREFEREE_BIN="' + referee_exe.full_path() + '"',
        '-DRDB_BIN="' + rdb_exe.full_path() + '"',
        '-DREFEREE_TEST_PLUGIN_DIR="' + meson.project_build_root() + '"',
    ],
    dependencies : [
        core_dep,
        fmt_dep,
        antlr4_runtime_dep,
        llvm_dep,
        gtest_dep,
        gtest_main_dep,
        threads_dep,
    ],
)

test('tests', tests_exe,
     depends  : [referee_exe, rdb_exe],
     workdir  : meson.project_source_root(),
     timeout  : 300)

# gcovr-driven coverage targets (portable replacement for the stock
# `ninja coverage-html`, which goes through lcov and fails on Apple Clang)
gcovr_prog          = find_program('gcovr', required : false)
if gcovr_prog.found()
    _cov_report_dir = meson.project_build_root() / 'meson-logs' / 'coveragereport'
    _gcovr_common   = [
        gcovr_prog,
        '-r', meson.project_source_root(),
        meson.project_build_root(),
        '--exclude', meson.project_build_root() / 'meson-private',
        '--exclude', meson.project_build_root() / 'refereeLexer.cpp',
        '--exclude', meson.project_build_root() / 'refereeLexer.h',
        '--exclude', meson.project_build_root() / 'refereeParser.cpp',
        '--exclude', meson.project_build_root() / 'refereeParser.h',
        '--exclude', meson.project_build_root() / 'refereeVisitor.cpp',
        '--exclude', meson.project_build_root() / 'refereeVisitor.h',
        '--exclude', meson.project_build_root() / 'refereeBaseVisitor.cpp',
        '--exclude', meson.project_build_root() / 'refereeBaseVisitor.h',
        '--exclude', meson.project_build_root() / 'refereeListener.cpp',
        '--exclude', meson.project_build_root() / 'refereeListener.h',
        '--exclude', meson.project_build_root() / 'refereeBaseListener.cpp',
        '--exclude', meson.project_build_root() / 'refereeBaseListener.h',
        # Report on the product only. An include-filter is used rather than
        # excludes because gcovr's exclude patterns did not reliably match the
        # test tree, which quietly folded gtest's own branches into the number
        # (they alone dragged branch coverage from 86% down to 72%).
        '--filter', meson.project_source_root() / 'core' / '',
        '--filter', meson.project_source_root() / 'rdb' / '',
        '--filter', meson.project_source_root() / 'referee.cpp',
        '--filter', meson.project_source_root() / 'main.cpp',
        # Every C++ statement that can throw carries a hidden branch to its
        # cleanup path. Counting those buries the real conditionals: they alone
        # take branch coverage from ~67% to ~39% while saying nothing about how
        # well the code is exercised.
        '--exclude-throw-branches',
        '--exclude-unreachable-branches',
    ]

    run_target(
        'coverage-gcovr-html',
        command : [
            'sh', '-c',
            'mkdir -p "' + _cov_report_dir + '" && exec "$@"',
            'gcovr-html',
        ] + _gcovr_common + [
            '--html', '--html-nested',
            '-o', _cov_report_dir / 'index.html',
        ],
    )

    run_target(
        'coverage-gcovr-text',
        command : _gcovr_common + [
            '-o', meson.project_build_root() / 'meson-logs' / 'coverage.txt',
        ],
    )

    run_target(
        'coverage-gcovr-xml',
        command : _gcovr_common + [
            '--xml',
            '-o', meson.project_build_root() / 'meson-logs' / 'coverage.xml',
        ],
    )
endif
