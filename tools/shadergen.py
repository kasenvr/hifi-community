import re
import subprocess
import sys
import time
import os
import json
import argparse
import concurrent
from os.path import expanduser
from concurrent.futures import ThreadPoolExecutor
from argparse import ArgumentParser
from pathlib import Path
from threading import Lock

    # # Target dependant Custom rule on the SHADER_FILE
    # if (ANDROID)
    #     set(GLPROFILE LINUX_GL)
    # else()
    #     if (APPLE)
    #         set(GLPROFILE MAC_GL)
    #     elseif(UNIX)
    #         set(GLPROFILE LINUX_GL)
    #     else()
    #         set(GLPROFILE PC_GL)
    #     endif()
    # endif()

def getTypeForScribeFile(scribefilename):
    last = scribefilename.rfind('.')
    extension = scribefilename[last:]
    switcher = {
        '.slv': 'vert',
        '.slf': 'frag',
        '.slg': 'geom',
        '.slc': 'comp',
    }
    if not extension in switcher:
        raise ValueError("Unknown scribe file type for " + scribefilename)
    return switcher.get(extension)

def getCommonScribeArgs(scribefile, includeLibs):
    scribeArgs = [os.path.join(args.tools_dir, 'scribe')]
    # FIXME use the sys.platform to set the correct value
    scribeArgs.extend(['-D', 'GLPROFILE', 'PC_GL'])
    scribeArgs.extend(['-T', getTypeForScribeFile(scribefile)])
    for lib in includeLibs:
        scribeArgs.extend(['-I', args.source_dir + '/libraries/' + lib + '/src/' + lib + '/'])
        scribeArgs.extend(['-I', args.source_dir + '/libraries/' + lib + '/src/'])
    scribeArgs.append(scribefile)
    return scribeArgs

extensionsHeaderMutex = Lock()

def getExtensionsHeader(dialect, variant, extensions):
    extensionHeader = '{}/extensions_{}_{}.glsl'.format(args.build_dir, dialect, variant)
    global extensionsHeaderMutex
    extensionsHeaderMutex.acquire()
    if not os.path.exists(extensionHeader):
        extensionsDefines = []
        for extension in extensions:
            extensionsDefines.append('#define HAVE_{}'.format(extension))
        # make sure we end with a line feed
        extensionsDefines.append("\r\n")
        with open(extensionHeader, "w") as f:
            f.write('\r\n'.join(extensionsDefines))
    extensionsHeaderMutex.release()
    return extensionHeader

def getDialectAndVariantHeaders(dialect, variant, extensions=None):
    result = []
    headerPath = args.source_dir + '/libraries/shaders/headers/'
    versionHeader = headerPath + dialect + '/version.glsl'
    result.append(versionHeader)
    if extensions is not None:
        result.append(getExtensionsHeader(dialect, variant, extensions))
    dialectHeader = headerPath + dialect + '/header.glsl'
    result.append(dialectHeader)
    variantHeader = headerPath + ('stereo.glsl' if (variant == 'stereo') else 'mono.glsl')
    result.append(variantHeader)
    return result

def getDefines(defines):
    definesList = []
    if defines:
        definesSplit = defines.split("_")
        for define in definesSplit:
            definesList.append('HIFI_USE_{} 1'.format(define.upper()))
    return definesList

class ScribeDependenciesCache:
    cache = {}
    lock = Lock()
    filename = ''

    def __init__(self, filename):
        self.filename = filename

    def load(self):
        jsonstr = '{}'
        if (os.path.exists(self.filename)):
            with open(self.filename) as f:
                jsonstr = f.read()
        self.cache = json.loads(jsonstr)

    def save(self):
        with open(self.filename, "w") as f:
            f.write(json.dumps(self.cache))

    def get(self, scribefile, dialect, variant, defines):
        self.lock.acquire()
        key = self.key(scribefile, dialect, variant, defines)
        try:
            if key in self.cache:
                return self.cache[key].copy()
        finally:
            self.lock.release()
        return None

    def key(self, scribeFile, dialect, variant, defines):
        return ':'.join([scribeFile, dialect, variant, defines])

    def getOrGen(self, scribefile, includeLibs, dialect, variant, defines):
        result = self.get(scribefile, dialect, variant, defines)
        if result is None:
            result = self.gen(scribefile, includeLibs, dialect, variant, defines)
        return result

    def gen(self, scribefile, includeLibs, dialect, variant, defines):
        scribeArgs = getCommonScribeArgs(scribefile, includeLibs)
        scribeArgs.extend(['-M'])
        processResult = subprocess.run(scribeArgs, stdout=subprocess.PIPE)
        if (0 != processResult.returncode):
            raise RuntimeError("Unable to parse scribe dependencies for file {} with defines: {}".format(scribefile, defines))
        result = processResult.stdout.decode("utf-8").splitlines(False)
        result.append(scribefile)
        result.extend(getDialectAndVariantHeaders(dialect, variant))
        result.extend(getDefines(defines))
        key = self.key(scribefile, dialect, variant, defines)
        self.lock.acquire()
        self.cache[key] = result.copy()
        self.lock.release()
        return result

def getFileTimes(files):
    if isinstance(files, str):
        files = [files]
    return list(map(lambda f: os.path.getmtime(f) if os.path.isfile(f) else -1, files))

def outOfDate(inputs, output):
    oldestInput = max(getFileTimes(inputs))
    youngestOutput = min(getFileTimes(output))
    diff = youngestOutput - oldestInput
    return oldestInput >= youngestOutput

def executeSubprocess(processArgs):
    processResult = subprocess.run(processArgs, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if (0 != processResult.returncode):
        raise RuntimeError('Call to "{}" failed.\n\narguments:\n{}\n\nstdout:\n{}\n\nstderr:\n{}'.format(
            processArgs[0],
            ' '.join(processArgs[1:]),
            processResult.stdout.decode('utf-8'),
            processResult.stderr.decode('utf-8')))

folderMutex = Lock()

def processCommand(line):
    global args
    global scribeDepCache
    glslangExec = args.tools_dir + '/glslang/glslang'
    spirvCrossExec = args.tools_dir + '/spirv-cross/spirv-cross'
    spirvOptExec = args.tools_dir + '/spirv-opt'
    params = line.split(';')
    dialect = params.pop(0)
    variant = params.pop(0)
    scribeFile  = args.source_dir + '/' + params.pop(0)
    unoptGlslFile = args.source_dir + '/' + params.pop(0)
    defines = ""
    if len(params) > 1 and params[0].startswith("defines:"):
        defines = params.pop(0)
        defines = defines[len("defines:"):]
    libs = params

    upoptSpirvFile = unoptGlslFile + '.spv'
    spirvFile = unoptGlslFile + '.opt.spv'
    reflectionFile  = unoptGlslFile + '.json'
    glslFile = unoptGlslFile + '.glsl'
    outputFiles = [unoptGlslFile, spirvFile, reflectionFile, glslFile]

    scribeOutputDir = os.path.abspath(os.path.join(unoptGlslFile, os.pardir))

    # Serialize checking and creation of the output directory to avoid occasional
    # crashes
    global folderMutex
    folderMutex.acquire()
    if not os.path.exists(scribeOutputDir):
        os.makedirs(scribeOutputDir)
    folderMutex.release()

    scribeDeps = scribeDepCache.getOrGen(scribeFile, libs, dialect, variant, defines)

    # if the scribe sources (slv, slf, slh, etc), or the dialect/ variant headers are out of date
    # regenerate the scribe GLSL output
    if args.force or outOfDate(scribeDeps, outputFiles):
        print('Processing file {} dialect {} variant {} defines {}'.format(scribeFile, dialect, variant, defines))
        if args.dry_run:
            return True

        scribeDepCache.gen(scribeFile, libs, dialect, variant, defines)
        scribeArgs = getCommonScribeArgs(scribeFile, libs)
        for header in getDialectAndVariantHeaders(dialect, variant, args.extensions):
            scribeArgs.extend(['-H', header])
        for define in getDefines(defines):
            defineArgs = ['-D']
            defineArgs.extend(define.split(' '))
            scribeArgs.extend(defineArgs)
        scribeArgs.extend(['-o', unoptGlslFile])
        executeSubprocess(scribeArgs)

        # Generate the un-optimized output
        executeSubprocess([glslangExec, '-V100', '-o', upoptSpirvFile, unoptGlslFile])

        # Optimize the SPIRV
        executeSubprocess([spirvOptExec, '-O', '-o', spirvFile, upoptSpirvFile])

        # Generation JSON reflection
        executeSubprocess([spirvCrossExec, '--reflect', 'json', '--output', reflectionFile, spirvFile])

        # Generate the optimized GLSL output
        spirvCrossDialect = dialect
        # 310es causes spirv-cross to inject "#extension GL_OES_texture_buffer : require" into the output
        if (dialect == '310es'): spirvCrossDialect = '320es'
        spirvCrossArgs = [spirvCrossExec, '--output', glslFile, spirvFile, '--version', spirvCrossDialect]
        if (dialect == '410'): spirvCrossArgs.append('--no-420pack-extension')
        executeSubprocess(spirvCrossArgs)
    else:
        # This logic is necessary because cmake will agressively keep re-executing the shadergen
        # code otherwise
        Path(unoptGlslFile).touch()
        Path(upoptSpirvFile).touch()
        Path(spirvFile).touch()
        Path(glslFile).touch()
        Path(reflectionFile).touch()
    return True



def main():
    commands = args.commands.read().splitlines(False)
    if args.debug:
        for command in commands:
            processCommand(command)
    else:
        workers = max(1, os.cpu_count() - 2)
        with ThreadPoolExecutor(max_workers=workers) as executor:
            for result in executor.map(processCommand, commands):
                if not result:
                    raise RuntimeError("Failed to execute all subprocesses")
            executor.shutdown()


parser = ArgumentParser(description='Generate shader artifacts.')
parser.add_argument('--extensions', type=str, nargs='*', help='Available extensions for the shaders')
parser.add_argument('--commands', type=argparse.FileType('r'), help='list of commands to execute')
parser.add_argument('--tools-dir', type=str, help='location of the host compatible binaries')
parser.add_argument('--build-dir', type=str, help='The build directory base path')
parser.add_argument('--source-dir', type=str, help='The root directory of the git repository')
parser.add_argument('--debug', action='store_true')
parser.add_argument('--force', action='store_true', help='Ignore timestamps and force regeneration of all files')
parser.add_argument('--dry-run', action='store_true', help='Report the files that would be process, but do not output')

args = None
if len(sys.argv) == 1:
    # for debugging
    sourceDir = expanduser('~/git/hifi')
    toolsDir = 'd:/hifi/vcpkg/android/fd82f0a8/installed/x64-windows/tools'
    buildPath = sourceDir + '/build_android'
    commandsPath = buildPath + '/libraries/shaders/shadergen.txt'
    shaderDir = buildPath + '/libraries/shaders'
    testArgs = '--commands {} --tools-dir {} --build-dir {} --source-dir {}'.format(
        commandsPath, toolsDir, shaderDir, sourceDir
    ).split()
    testArgs.append('--debug')
    testArgs.append('--force')
    testArgs.extend('--extensions EXT_clip_cull_distance'.split())
    #testArgs.append('--dry-run')
    args = parser.parse_args(testArgs)
else:
    args = parser.parse_args()

scribeDepCache = ScribeDependenciesCache(args.build_dir + '/shaderDeps.json')
scribeDepCache.load()
main()
scribeDepCache.save()

