import sys

try:
  from ambuild2 import run
except ImportError:
  sys.stderr.write('AMBuild 2.2 or higher is required: https://github.com/alliedmodders/ambuild\n')
  sys.exit(1)

if getattr(run, 'CURRENT_API', '2.1').startswith('2.1'):
  sys.stderr.write('AMBuild 2.2 or higher is required\n')
  sys.exit(1)

parser = run.BuildParser(sourcePath=sys.path[0], api='2.2')
parser.options.add_argument('--hl2sdk-root', type=str, dest='hl2sdk_root', default=None,
                            help='Root folder containing hl2sdk-cs2')
parser.options.add_argument('--hl2sdk-manifests', type=str, dest='hl2sdk_manifests', default=None,
                            help='hl2sdk-manifests folder')
parser.options.add_argument('--mms_path', type=str, dest='mms_path', default=None,
                            help='Metamod:Source source folder')
parser.options.add_argument('--enable-debug', action='store_const', const='1', dest='debug',
                            help='Enable debug symbols')
parser.options.add_argument('--enable-optimize', action='store_const', const='1', dest='opt',
                            help='Enable optimization')
parser.options.add_argument('-s', '--sdks', default='cs2', dest='sdks',
                            help='SDKs to build against (default: cs2)')
parser.Configure()
