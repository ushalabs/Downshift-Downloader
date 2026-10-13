"""Publish only artifacts from a successful, matching Windows verification run."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import time
import urllib.error
import urllib.request
import zipfile


def validate(directory, source):
    report = json.loads((directory / 'portable-test-report.json').read_text(encoding='utf-8-sig'))
    if report['sourceCommit'] != source or report['checks'] != 13:
        raise ValueError('Portable verification report does not match the release source.')
    version = report['version']
    names = [f'Downshift-{version}-{suffix}.zip' for suffix in
             ('windows-x64', 'chrome-extension', 'dependency-sources', 'application-source')]
    checksums = {}
    for line in (directory / 'SHA256SUMS.txt').read_text().splitlines():
        digest, name = line.split('  ', 1)
        if name not in names or not re.fullmatch('[a-f0-9]{64}', digest):
            raise ValueError('Unexpected checksum entry.')
        checksums[name] = digest
    if set(checksums) != set(names):
        raise ValueError('All four archive checksums are required.')
    for name, digest in checksums.items():
        with (directory / name).open('rb') as file:
            actual_digest = hashlib.file_digest(file, 'sha256').hexdigest()
        if actual_digest != digest:
            raise ValueError(f'Archive checksum mismatch: {name}')
        with zipfile.ZipFile(directory / name) as archive:
            if archive.testzip() is not None:
                raise ValueError(f'Archive CRC failure: {name}')
    prefix = f'Downshift-{version}-windows-x64/'
    with zipfile.ZipFile(directory / names[0]) as archive:
        manifest = json.loads(archive.read(prefix + 'package-manifest.json').decode('utf-8-sig'))
        if manifest['sourceCommit'] != source or manifest['sourceDirty'] or manifest['version'] != version:
            raise ValueError('Package must identify the exact clean release source.')
        declared = {prefix + 'package-manifest.json'}
        for file in manifest['files']:
            name = file['path']
            if name.startswith('/') or '..' in Path(name).parts or '\\' in name:
                raise ValueError('Unsafe packaged path.')
            data = archive.read(prefix + name)
            if hashlib.sha256(data).hexdigest() != file['sha256'] or len(data) != file['bytes']:
                raise ValueError(f'Package file differs: {name}')
            declared.add(prefix + name)
        actual = {name for name in archive.namelist() if not name.endswith('/')}
        if declared != actual or any('/media-tools/' in name for name in actual):
            raise ValueError('Unexpected binary package contents.')
    if checksums[names[0]] != report['packageSha256']:
        raise ValueError('Portable startup was not tested against this exact archive.')
    with zipfile.ZipFile(directory / names[3]) as archive:
        if archive.comment.decode() != source:
            raise ValueError('Git source archive does not match the release commit.')
    with zipfile.ZipFile(directory / names[2]) as archive:
        resources = json.loads(archive.read('dependency-sources/sources.json').decode('utf-8-sig'))
        if len(resources) != 11:
            raise ValueError('Expected the complete corresponding library resource inventory.')
        for item in resources:
            if Path(item['file']).name != item['file']:
                raise ValueError('Unsafe source resource path.')
            if hashlib.sha512(archive.read('dependency-sources/' + item['file'])).hexdigest() != item['sha512']:
                raise ValueError('Corresponding library source checksum differs.')
        license_text = archive.read('dependency-sources/licenses/qtbase/copyright').decode()
        if 'GNU LESSER GENERAL PUBLIC LICENSE' not in license_text or 'GNU GENERAL PUBLIC LICENSE' not in license_text:
            raise ValueError('Qt license texts are missing.')
    names.append('SHA256SUMS.txt')
    digests = {name: 'sha256:' + hashlib.sha256((directory / name).read_bytes()).hexdigest() for name in names}
    return version, names, digests, report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, default=Path('dist'))
    parser.add_argument('--source', required=True)
    parser.add_argument('--run', type=int)
    parser.add_argument('--draft', type=int, default=0)
    parser.add_argument('--validate-only', action='store_true')
    args = parser.parse_args()
    if not re.fullmatch('[a-f0-9]{40}', args.source):
        raise ValueError('An exact source commit is required.')
    version, names, digests, report = validate(args.directory, args.source)
    print(f'Validated {version}: archive hashes, all package files, tested clean source and 11 library sources.')
    if args.validate_only:
        return
    repository = os.environ['GITHUB_REPOSITORY']
    if repository != 'ushalabs/Downshift-Download-Manager':
        raise ValueError('This publisher is scoped to the Downshift repository.')
    token = os.environ['GITHUB_TOKEN']
    api = f'https://api.github.com/repos/{repository}'

    def request(url, method='GET', payload=None, file=None):
        headers = {'Authorization': 'Bearer ' + token, 'Accept': 'application/vnd.github+json',
                   'User-Agent': 'Downshift-verified-publisher'}
        stream = None
        try:
            if file:
                stream = file.open('rb'); data = stream
                headers.update({'Content-Type': 'application/octet-stream', 'Content-Length': str(file.stat().st_size)})
            else:
                data = json.dumps(payload).encode() if payload is not None else None
                if data is not None:
                    headers['Content-Type'] = 'application/json'
            req = urllib.request.Request(url, data=data, headers=headers, method=method)
            with urllib.request.urlopen(req, timeout=600) as response:
                body = response.read()
                return json.loads(body) if body else None
        finally:
            if stream:
                stream.close()

    run = request(f'{api}/actions/runs/{args.run}')
    if run['head_sha'] != args.source or run['status'] != 'completed' or run['conclusion'] != 'success' or run['head_branch'] != 'main':
        raise ValueError('Only a matching successful main Windows run may be published.')
    jobs = request(f'{api}/actions/runs/{args.run}/jobs')['jobs']
    steps = {step['name']: step['conclusion'] for job in jobs for step in job['steps']}
    for step in ('Test Release', 'Test Debug', 'Test extracted portable package with built-in Windows PowerShell', 'Preserve verified distribution'):
        if steps.get(step) != 'success':
            raise ValueError(f'Required verification did not pass: {step}')
    tag = 'v' + version
    def tag_reference():
        try:
            reference = request(f'{api}/git/ref/tags/{tag}')['object']
        except urllib.error.HTTPError as error:
            if error.code == 404:
                return None
            raise
        if reference['type'] == 'tag':
            reference = request(f"{api}/git/tags/{reference['sha']}")['object']
        if reference['type'] != 'commit' or reference['sha'] != args.source:
            raise ValueError('Existing version tag points elsewhere; it will not be replaced.')
        return reference

    tag_reference()
    release = request(f'{api}/releases/{args.draft}') if args.draft else next(
        (r for r in request(f'{api}/releases?per_page=50') if r['tag_name'] == tag), None)
    if release and release['target_commitish'] != args.source:
        raise ValueError('Existing release source differs; it will not be replaced.')
    if release and not release['draft']:
        raise ValueError('An existing public release will not be modified.')
    body = f'''Downshift {version} is an unsigned Windows 10/11 x64 portable preview for ordinary files and supported videos.

Extract the entire **windows-x64 ZIP**, run **Setup.cmd**, and open the Desktop shortcut. In Chrome, choose **Developer mode → Load unpacked** and select the extracted **extension** folder. Run **Enable-video.cmd** once for optional pinned upstream video tools. Developer tools are not required to run the app.

- Compact responsive pink/plum window and persistent encrypted file/video history.
- Downloads/Videos, Audios, Zip and Programs defaults; choose a destination for every download.
- Chrome opens Downshift and a save window. Enabled handoff failures stay paused until retry or explicit disable.
- Public streaming/verified segmented transfers, supported player actions, quality selection and video merging.
- App-local runtimes, ownership-aware per-user setup/uninstall, complete source/license bundles.

**Verified release producer:** [Windows CI]({run['html_url']}) built Release/Debug, passed all 14 groups in both with video tools installed, verified 13 extracted-package checks and preserved these exact artifacts. Separate local actual Chrome checks passed in both configurations. This publication independently verified archive/per-file hashes and the corresponding library source inventory.

Chrome can receive initial bytes or complete files before extension events. Cookies/authentication are not imported. Browser-only, private/protected/DRM, unsafe and unsupported resources are not universally downloadable. Media helper concurrency/rate limits are separate. No Internet-speed or IDM superiority is claimed; the recorded 180-sample benchmark matrix is cached loopback HTTP/1.1. This preview has no signature, store publication, automatic updater or clean-VM claim.

[Setup/uninstall](https://github.com/{repository}/blob/{tag}/docs/PACKAGING.md) · [Behavior/limits](https://github.com/{repository}/blob/{tag}/docs/GENERAL_DOWNLOADS.md) · [Licenses/source](https://github.com/{repository}/blob/{tag}/docs/THIRD_PARTY_NOTICES.md) · [Measured benchmarks](https://github.com/{repository}/blob/{tag}/docs/PERFORMANCE.md)

Compare the four ZIPs to SHA256SUMS.txt. Original Downshift source is MIT; shared Qt uses LGPLv3 with complete notices and matching source/patches/build recipes provided. Optional media executables are downloaded directly from upstream, not redistributed in the binary archive.
'''
    payload = {'tag_name': tag, 'target_commitish': args.source, 'name': f'Downshift {version} preview',
               'body': body, 'draft': True, 'prerelease': True}
    if release:
        release = request(f"{api}/releases/{release['id']}", 'PATCH', payload)
    else:
        release = request(api + '/releases', 'POST', payload)
    release_id = release['id']
    for name in names:
        for attempt in range(3):
            current = request(f'{api}/releases/{release_id}')
            existing = next((a for a in current['assets'] if a['name'] == name), None)
            if existing and existing['state'] == 'uploaded' and existing.get('digest') == digests[name]:
                break
            if existing:
                request(f"{api}/releases/assets/{existing['id']}", 'DELETE')
            print('Uploading verified artifact:', name, flush=True)
            try:
                asset = request(f'https://uploads.github.com/repos/{repository}/releases/{release_id}/assets?name={name}',
                                'POST', file=args.directory / name)
                if asset.get('digest') != digests[name] or asset['size'] != (args.directory / name).stat().st_size:
                    raise ValueError('Server asset checksum or size differs.')
                break
            except (OSError, ValueError):
                if attempt == 2:
                    raise
                time.sleep(3)
        print('Server checksum verified:', name, flush=True)
    release = request(f'{api}/releases/{release_id}')
    if len(release['assets']) != len(names) or any(a.get('digest') != digests.get(a['name']) for a in release['assets']):
        raise ValueError('Draft asset set differs from verified distribution.')
    if tag_reference() is None:
        request(api + '/git/refs', 'POST', {'ref': 'refs/tags/' + tag, 'sha': args.source})
    tag_reference()
    payload.update({'draft': False, 'make_latest': 'false'})
    release = request(f'{api}/releases/{release_id}', 'PATCH', payload)
    reference = request(f'{api}/git/ref/tags/{tag}')['object']
    if reference['type'] == 'tag':
        reference = request(f"{api}/git/tags/{reference['sha']}")['object']
    if reference['sha'] != args.source or release['draft']:
        raise ValueError('Published tag/source verification failed.')
    evidence = {'sourceCommit': args.source, 'producer': 'GitHub Windows CI artifacts',
                'ci': {'id': args.run, 'url': run['html_url'], 'conclusion': run['conclusion']},
                'portableTest': report, 'release': {k: release[k] for k in
                    ('id', 'html_url', 'tag_name', 'draft', 'prerelease')},
                'assets': [{k: a[k] for k in ('name', 'size', 'digest', 'browser_download_url')} for a in release['assets']]}
    (args.directory / 'release-evidence.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
    print('Published and verified:', release['html_url'])


if __name__ == '__main__':
    main()
