"""Create a deterministic release containing only our bridge and launcher."""
import gzip
import hashlib
import io
from pathlib import Path
import tarfile

ROOT = Path(__file__).resolve().parent
NAME = 'dlssnr-proton-0.1.0-alpha.1-linux-x86_64'
FILES = {'nr-run': ROOT / 'nr-run', 'runtime.json': ROOT / 'runtime.json',
         'nr-backend': ROOT / 'build/nr-backend',
         'amdhip64_7.dll': ROOT / 'build/amdhip64_7.dll',
         'README.md': ROOT / 'README.md', 'LICENSE': ROOT / 'LICENSE',
         'NOTICE': ROOT / 'NOTICE'}
FILES.update({'licenses/' + p.name: p for p in (ROOT / 'licenses').glob('*.txt')})
for source in FILES.values():
    if not source.is_file():
        raise SystemExit('Missing release input: ' + str(source))
if FILES['nr-backend'].read_bytes()[:4] != b'\x7fELF':
    raise SystemExit('The backend must be a Linux ELF build.')
if FILES['amdhip64_7.dll'].read_bytes()[:2] != b'MZ':
    raise SystemExit('The bridge must be a Windows PE build.')
dist = ROOT / 'dist'
dist.mkdir(exist_ok=True)
archive = dist / (NAME + '.tar.gz')
checksums = []
with archive.open('wb') as raw, gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=0) as zipped:
    with tarfile.open(fileobj=zipped, mode='w') as tar:
        for name, source in sorted(FILES.items()):
            data = source.read_bytes()
            checksums.append(hashlib.sha256(data).hexdigest() + '  ' + name)
            info = tarfile.TarInfo(NAME + '/' + name)
            info.size = len(data)
            info.mode = 0o755 if name in ('nr-run', 'nr-backend') else 0o644
            tar.addfile(info, io.BytesIO(data))
        data = ('\n'.join(checksums) + '\n').encode()
        info = tarfile.TarInfo(NAME + '/SHA256SUMS')
        info.size, info.mode = len(data), 0o644
        tar.addfile(info, io.BytesIO(data))
sha = hashlib.sha256(archive.read_bytes()).hexdigest()
(dist / 'SHA256SUMS').write_text(sha + '  ' + archive.name + '\n')
print(str(archive) + '\n' + sha)
