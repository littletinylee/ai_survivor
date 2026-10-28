import os
from SCons.Script import ARGUMENTS, Alias, Default, Environment, Glob, SConscript

ARGUMENTS.setdefault('custom_api_file', os.path.abspath('tools/extension_api-4.7.2.json'))
ARGUMENTS.setdefault('build_profile', os.path.abspath('tools/build_profile.json'))
ARGUMENTS.setdefault('target', 'template_debug')
os.makedirs('build', exist_ok=True)
open('build/.gdignore', 'a').close()
toolchain = {'MSVC_TOOLSET_VERSION': '14.50.35717'} if os.name == 'nt' else {'CXX': 'g++-13'}
binding_env = Environment(tools=['default'], PLATFORM='', **toolchain)
env = SConscript('godot-cpp/SConstruct', exports={'env': binding_env})
env.Append(CPPPATH=['src'])
sources = Glob('src/core/*.cpp') + Glob('src/gameplay/*.cpp')
library = env.SharedLibrary('bin/ai_survivors' + env['suffix'] + env['SHLIBSUFFIX'], source=sources)

tests = Environment(ENV=os.environ.copy(), **toolchain)
if tests['PLATFORM'] == 'win32':
    tests.Append(CXXFLAGS=['/std:c++17', '/EHsc', '/MT', '/utf-8'])
else:
    tests.Append(CXXFLAGS=['-std=c++17'], LIBS=['pthread'])
tests.Append(CPPPATH=['src', 'third_party/googletest/googletest/include',
                      'third_party/googletest/googletest'])
test_sources = Glob('src/core/*.cpp') + Glob('tests/unit/*.cpp') + Glob('tests/simulation/*.cpp')
test_sources += ['third_party/googletest/googletest/src/gtest-all.cc',
                 'third_party/googletest/googletest/src/gtest_main.cc']
# Separate object targets prevent the extension's compilation flags leaking into tests.
objects = [tests.Object('build/tests/' + str(s).replace('/', '_').replace('\\', '_') + tests['OBJSUFFIX'], s)
           for s in test_sources]
program = tests.Program('build/tests/ai_survivors_tests', objects)
Alias('unit', program)
Default(library, program)

