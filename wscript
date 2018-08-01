# -*- Mode: python; py-indent-offset: 4; indent-tabs-mode: nil; coding: utf-8; -*-

from waflib import Logs, Utils, Context
import os

VERSION = '0.1.0'
APPNAME = 'PSync'

def options(opt):
    opt.load(['compiler_c', 'compiler_cxx', 'gnu_dirs'])
    opt.load(['default-compiler-flags', 'boost', 'doxygen', 'sphinx_build',
              'sanitizers', 'coverage', 'pch'],
               tooldir=['.waf-tools'])

    opt.add_option('--with-tests', action='store_true', default=False, dest='with_tests',
                   help='''build unit tests''')

def configure(conf):
    conf.load(['compiler_c', 'compiler_cxx', 'gnu_dirs', 'default-compiler-flags',
               'boost', 'pch', 'doxygen', 'sphinx_build'])

    if not os.environ.has_key('PKG_CONFIG_PATH'):
        os.environ['PKG_CONFIG_PATH'] = ':'.join([
            '/usr/lib/pkgconfig',
            '/usr/local/lib/pkgconfig',
            '/opt/local/lib/pkgconfig'])

    conf.check_cfg(package='libndn-cxx', args=['--cflags', '--libs'],
                   uselib_store='NDN_CXX', mandatory=True)

    boost_libs = 'system iostreams thread log log_setup unit_test_framework'
    if conf.options.with_tests:
        conf.env['WITH_TESTS'] = 1
        conf.define('WITH_TESTS', 1);
        boost_libs += ' unit_test_framework'

    conf.check_boost(lib=boost_libs, mt=True)

    conf.load('coverage')

    conf.load('sanitizers')

def build(bld):
    libpsync = bld(
        target='PSync',
        features=['cxx', 'cxxshlib'],
        source =  bld.path.ant_glob(['src/**/*.cpp']),
        use = 'BOOST NDN_CXX',
        includes = ['src', '.'],
        export_includes=['src', '.'],
        )

    bld.install_files(
        dest = "%s/PSync" % bld.env['INCLUDEDIR'],
        files = bld.path.ant_glob(['src/**/*.hpp', 'src/**/*.h']),
        cwd = bld.path.find_dir("src"),
        relative_trick = False,
        )

    pc = bld(
        features = "subst",
        source='PSync.pc.in',
        target='PSync.pc',
        install_path = '${LIBDIR}/pkgconfig',
        PREFIX       = bld.env['PREFIX'],
        INCLUDEDIR   = "%s/PSync" % bld.env['INCLUDEDIR'],
        VERSION      = VERSION,
        )

    bld.program(
        features = 'cxx',
        target = 'psync-full',
        source = 'tools/full-sync.cpp',
        use = 'NDN_CXX PSync'
        )

    if bld.env['WITH_TESTS']:
        bld.recurse('tests')
