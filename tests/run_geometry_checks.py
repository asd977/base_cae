"""Build geometry checks and unlocked preview DLLs from a configured MSVC/Ninja build.

python tests/run_geometry_checks.py --build-dir build/Desktop_Qt_6_7_3_MSVC2022_64bit_Release
Optional customer regression: --customer-dxf C:/D_Drive/m1.dxf
The original running application and its DLLs are never replaced.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import shutil
import sys
sys.stdout.reconfigure(encoding='utf-8')
sys.stderr.reconfigure(encoding='utf-8')


def run_logged(command, cwd, log, env=None):
    with log.open('w', encoding='utf-8') as stream:
        result = subprocess.run(command, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT, env=env)
    lines = log.read_text(encoding='utf-8', errors='replace').splitlines()
    meaningful = [line for line in lines if '包含文件:' not in line and 'including file:' not in line and ' -external:I' not in line]
    print('\n'.join(meaningful[-40:]), flush=True)
    if result.returncode:
        raise RuntimeError(f'Command failed ({result.returncode}); see {log}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--customer-dxf', type=Path)
    parser.add_argument('--mesh-only', action='store_true',
                        help='Run geometry plugin, meshing and display checks without unrelated sketch UI checks')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    build = args.build_dir.resolve()
    stage = build.parent / 'terrain_feature_preview'
    (stage / 'plugins').mkdir(parents=True, exist_ok=True)
    shutil.copy2(build / 'plugins/CAE_SMESHBridge.dll', stage / 'plugins/CAE_SMESHBridge.dll')
    cache = (build / 'CMakeCache.txt').read_text(encoding='utf-8')
    compiler_path = re.search(r'^CMAKE_CXX_COMPILER:\w+=(.*)$', cache, re.M).group(1).replace('\\', '/')
    vcvars = Path(compiler_path.split('/VC/Tools/')[0]) / 'VC/Auxiliary/Build/vcvars64.bat'
    qt_dir = Path(re.search(r'^Qt6_DIR:\w+=(.*)$', cache, re.M).group(1)).parents[2]
    ninja_text = (build / 'build.ninja').read_text(encoding='utf-8')
    object_targets = []
    link_commands = []
    geometry_rsp = None
    geometry_libraries = None
    geometry_compile = None
    linker = None
    mesh_libraries = mesh_object = mesh_compile = None
    for name in ('Base_Gemo', 'Base_UiFrame', 'Base_Mesh', 'Base_Visualization'):
        match = re.search(r'^build plugins[\\/]' + name + r'\.dll .*?: CXX_SHARED_LIBRARY_LINKER[^\n]+', ninja_text, re.M)
        if not match:
            raise RuntimeError(f'No Ninja link rule for {name}')
        objects = match.group().split(': ', 1)[1].split(' ', 1)[1].split(' |')[0].split()
        block = ninja_text[match.end():ninja_text.index('\n\n', match.end())]
        libraries = re.search(r'  LINK_LIBRARIES = (.*)', block).group(1)
        rsp = ' '.join(objects) + ' ' + libraries
        rsp_path = stage / (name + '.rsp')
        rsp_path.write_text(rsp, encoding='utf-8')
        object_targets.extend(objects)
        commands = subprocess.check_output(['ninja', '-t', 'commands', 'plugins/' + name + '.dll'], cwd=build, text=True, encoding='utf-8', errors='replace').splitlines()
        linker = re.search(r'-- (\S*link.exe)', commands[-1]).group(1)
        link_commands.append(f'{linker} /nologo @"{rsp_path}" /out:"{stage / "plugins" / (name + ".dll")}" /implib:"{stage / (name + ".lib")}" /dll /machine:x64 /INCREMENTAL:NO')
        if name == 'Base_Gemo':
            geometry_rsp = rsp
            geometry_libraries = libraries
            geometry_compile = next(c for c in commands if '/TP' in c and 'SketchPanel.cpp' in c and ' -c ' in c)
        if name == 'Base_Mesh':
            mesh_libraries = libraries
            mesh_object = next(o for o in objects if o.endswith('SmeshMeshGenerator.cpp.obj'))
            mesh_compile = next(c for c in commands if '/TP' in c and 'SmeshMeshGenerator.cpp' in c and ' -c ' in c)
    script = stage / 'build-preview.cmd'
    content = '@echo off\ncall "' + str(vcvars) + '"\nninja -j4 ' + ' '.join(object_targets) + '\nif errorlevel 1 exit /b 1\n'
    content += '\nif errorlevel 1 exit /b 1\n'.join(link_commands) + '\nif errorlevel 1 exit /b 1\n'
    script.write_text(content, encoding='utf-8')
    run_logged(['cmd', '/c', str(script)], build, stage / 'build-preview.log')
    test_object = stage / 'GeometryWorkflowChecks.obj'
    compile_command = re.sub(r'/Fo\S+', '/Fo"' + str(test_object).replace('\\', '/') + '"', geometry_compile)
    compile_command = re.sub(r'/Fd\S+', '/Fd"' + str(stage / 'checks.pdb').replace('\\', '/') + '"', compile_command)
    compile_command = compile_command[:compile_command.index(' -c ')] + ' -c "' + str(repo / 'tests/GeometryWorkflowChecks.cpp') + '"'
    original = next(o for o in object_targets if o.endswith('sketch\\SketchPanel.cpp.obj') or o.endswith('sketch/SketchPanel.cpp.obj'))
    test_rsp = stage / 'GeometryWorkflowChecks.rsp'
    test_rsp.write_text(geometry_rsp.replace(original, '"' + str(test_object) + '"'), encoding='utf-8')
    exe = stage / 'GeometryWorkflowChecks.exe'
    content = '@echo off\ncall "' + str(vcvars) + '"\n' + compile_command + '\nif errorlevel 1 exit /b 1\n'
    content += f'{linker} /nologo @"{test_rsp}" /out:"{exe}" /SUBSYSTEM:CONSOLE /machine:x64 /INCREMENTAL:NO\n'
    script = stage / 'build-checks.cmd'
    script.write_text(content, encoding='utf-8')
    run_logged(['cmd', '/c', str(script)], build, stage / 'checks-build.log')
    env = os.environ.copy()
    env['PATH'] = str(build) + os.pathsep + str(qt_dir / 'bin') + os.pathsep + env.get('PATH', '')
    env.update(QT_PLUGIN_PATH=str(qt_dir / 'plugins'), QT_QPA_PLATFORM='offscreen', QT_QPA_FONTDIR=str(Path(env.get('WINDIR', 'C:/Windows')) / 'Fonts'), QT_FORCE_STDERR_LOGGING='1')
    if args.customer_dxf:
        env['CAE_DXF_TEST_FILE'] = str(args.customer_dxf.resolve())
    if not args.mesh_only:
        run_logged([str(exe)], build, stage / 'checks-run.log', env)

    # This test imports the actual geometry DLL instead of linking its objects
    # into the executable. It covers actions, page navigation and stale caches
    # through the same service interface used by the mesh plugin.
    plugin_test_object = stage / 'PluginTerrainChecks.obj'
    plugin_command = re.sub(r'(?:-D|/D)GEOMETRY_LIBRARY\b', '', geometry_compile)
    plugin_command = re.sub(r'/Fo\S+', '/Fo"' + str(plugin_test_object).replace('\\', '/') + '"', plugin_command)
    plugin_command = re.sub(r'/Fd\S+', '/Fd"' + str(stage / 'plugin-checks.pdb').replace('\\', '/') + '"', plugin_command)
    plugin_command = plugin_command[:plugin_command.index(' -c ')] + ' -c "' + str(repo / 'tests/PluginTerrainChecks.cpp') + '"'
    plugin_rsp = stage / 'PluginTerrainChecks.rsp'
    plugin_rsp.write_text('"' + str(plugin_test_object) + '" "' + str(stage / 'Base_Gemo.lib') + '" ' + geometry_libraries, encoding='utf-8')
    plugin_exe = stage / 'PluginTerrainChecks.exe'
    content = '@echo off\ncall "' + str(vcvars) + '"\n' + plugin_command + '\nif errorlevel 1 exit /b 1\n'
    content += f'{linker} /nologo @"{plugin_rsp}" /out:"{plugin_exe}" /SUBSYSTEM:CONSOLE /machine:x64 /INCREMENTAL:NO\n'
    script = stage / 'build-plugin-checks.cmd'
    script.write_text(content, encoding='utf-8')
    run_logged(['cmd', '/c', str(script)], build, stage / 'plugin-checks-build.log')
    plugin_env = env.copy()
    plugin_env['PATH'] = str(stage / 'plugins') + os.pathsep + env['PATH']
    run_logged([str(plugin_exe)], build, stage / 'plugin-checks-run.log', plugin_env)
    mesh_test_object = stage / 'SmeshTerrainChecks.obj'
    mesh_command = re.sub(r'/Fo\S+', '/Fo"' + str(mesh_test_object).replace('\\', '/') + '"', mesh_compile)
    mesh_command = re.sub(r'/Fd\S+', '/Fd"' + str(stage / 'mesh-checks.pdb').replace('\\', '/') + '"', mesh_command)
    mesh_command = mesh_command[:mesh_command.index(' -c ')] + ' -c "' + str(repo / 'tests/SmeshTerrainChecks.cpp') + '"'
    mesh_rsp = stage / 'SmeshTerrainChecks.rsp'
    mesh_rsp.write_text('"' + str(mesh_test_object) + '" ' + mesh_object + ' ' + mesh_libraries, encoding='utf-8')
    mesh_exe = stage / 'SmeshTerrainChecks.exe'
    content = '@echo off\ncall "' + str(vcvars) + '"\n' + mesh_command + '\nif errorlevel 1 exit /b 1\n'
    content += f'{linker} /nologo @"{mesh_rsp}" /out:"{mesh_exe}" /SUBSYSTEM:CONSOLE /machine:x64 /INCREMENTAL:NO\n'
    script = stage / 'build-mesh-checks.cmd'
    script.write_text(content, encoding='utf-8')
    run_logged(['cmd', '/c', str(script)], build, stage / 'mesh-checks-build.log')
    run_logged([str(mesh_exe), str(stage / 'example_project/plugin-meshing-input.brep')], build, stage / 'mesh-checks-run.log', env)
    # Exercise the actual Mesh DLL and its display panel, including the source
    # regions after a DAT/mesh_map project round trip.
    display_object = stage / 'PluginMeshDisplayChecks.obj'
    display_command = re.sub(r'/Fo\S+', '/Fo"' + str(display_object).replace('\\', '/') + '"', mesh_compile)
    display_command = re.sub(r'/Fd\S+', '/Fd"' + str(stage / 'display-checks.pdb').replace('\\', '/') + '"', display_command)
    display_command = display_command[:display_command.index(' -c ')] + ' -c "' + str(repo / 'tests/PluginMeshDisplayChecks.cpp') + '"'
    display_rsp = stage / 'PluginMeshDisplayChecks.rsp'
    display_rsp.write_text('"' + str(display_object) + '" ' + mesh_libraries, encoding='utf-8')
    display_exe = stage / 'PluginMeshDisplayChecks.exe'
    content = '@echo off\ncall "' + str(vcvars) + '"\n' + display_command + '\nif errorlevel 1 exit /b 1\n'
    content += f'{linker} /nologo @"{display_rsp}" /out:"{display_exe}" /SUBSYSTEM:CONSOLE /machine:x64 /INCREMENTAL:NO\n'
    script = stage / 'build-display-checks.cmd'
    script.write_text(content, encoding='utf-8')
    run_logged(['cmd', '/c', str(script)], build, stage / 'display-checks-build.log')
    run_logged([str(display_exe), str(stage / 'example_project/terrain-computational-mesh.vtk')], build, stage / 'display-checks-run.log', plugin_env)
    print(f'Preview DLLs, logs and example project: {stage}')


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
