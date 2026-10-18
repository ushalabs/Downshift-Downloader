"""Opt-in full public transfers through an installed copy of the Qt media dialog."""
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    driver, output = map(lambda value: pathlib.Path(value).resolve(), sys.argv[1:])
    tools = driver.parent / 'media-tools'
    lock = json.loads((driver.parent / 'scripts/media-tools-lock.json').read_text(encoding='utf-8-sig'))
    for entry in lock['files']:
        assert digest(tools / entry['name']) == entry['sha256'], entry['name']
    output.parent.mkdir(parents=True, exist_ok=True)
    # Keep the fixture comparable to a normal Downloads destination. A long
    # repository path plus helper fragment suffixes can exceed Windows limits.
    root = pathlib.Path(tempfile.mkdtemp(prefix='ds1-')).resolve()
    (root / '.downshift-public-fixture').write_text('Generated isolated acceptance data\n')
    reports = []
    for index, (video, height) in enumerate([('FQmqRVK3kHg', 720), ('jNQXAC9IVRw', 240)]):
        destination = root / str(index)
        destination.mkdir()
        command = [str(driver), str(destination), 'https://www.youtube.com/watch?v=' + video, str(height)]
        if index == 0:
            command.append('--unusable-browser')
        started = time.monotonic()
        result = subprocess.run(command, cwd=driver.parent, capture_output=True, timeout=1050,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        error = re.sub(r'https?://\S+', '[address]', result.stderr.decode('utf-8', errors='replace'))
        (destination / 'native-dialog-log.txt').write_text(error, encoding='utf-8')
        assert result.returncode == 0, error[-3000:]
        report = json.loads(result.stdout)
        assert report['completed'] and report['privateQueue'] and report['browserSource'] is False
        file = pathlib.Path(report['file'])
        assert file.is_relative_to(destination)
        probe = json.loads(subprocess.run([str(tools / 'ffprobe.exe'), '-v', 'error', '-show_streams',
                            '-show_format', '-of', 'json', str(file)], capture_output=True,
                            check=True, timeout=30).stdout)
        duration = float(probe['format']['duration'])
        assert {'audio', 'video'} <= {stream['codec_type'] for stream in probe['streams']}
        assert any(stream.get('height') == height for stream in probe['streams'])
        assert abs(duration - report['expectedDuration']) < max(1, report['expectedDuration'] * .01)
        samples = []
        for label, second in [('beginning', min(1, duration / 10)), ('middle', duration / 2), ('ending', max(0, duration - 2))]:
            frame = destination / (label + '.png')
            subprocess.run([str(tools / 'ffmpeg.exe'), '-nostdin', '-v', 'error', '-ss', str(second),
                            '-i', str(file), '-frames:v', '1', '-vf', 'scale=480:-1', '-threads', '2', str(frame)],
                           capture_output=True, check=True, timeout=40)
            assert frame.stat().st_size > 1000
            retained_frame = output.parent / 'youtube-frames' / str(index) / frame.name
            retained_frame.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(frame, retained_frame)
            samples.append(str(retained_frame))
        audio = subprocess.run([str(tools / 'ffmpeg.exe'), '-nostdin', '-v', 'error', '-i', str(file),
                                '-map', '0:a:0', '-t', '1', '-f', 'md5', '-'], capture_output=True,
                               check=True, timeout=30)
        assert audio.stdout.startswith(b'MD5=')
        report.update(video=video, elapsed=round(time.monotonic() - started, 2), bytes=file.stat().st_size,
                      sha256=digest(file), actualDuration=duration, audioDecoded=True, decodedFrames=samples,
                      offeredUnusableBrowserDescriptor=index == 0, installedHelperHashesMatched=True)
        reports.append(report)
        output.write_text(json.dumps(dict(publicTransfers=reports, normalQueueUsed=False,
                          normalRegistrationUsed=False, privateRoot=str(root)), indent=2) + '\n', encoding='utf-8')
        print(json.dumps(report), flush=True)
    assert root.parent == pathlib.Path(tempfile.gettempdir()).resolve() and root.name.startswith('ds1-')
    assert (root / '.downshift-public-fixture').read_text() == 'Generated isolated acceptance data\n'
    shutil.rmtree(root)
    output.write_text(json.dumps(dict(publicTransfers=reports, normalQueueUsed=False,
                      normalRegistrationUsed=False, privateRoot=str(root), generatedPayloadsCleaned=True),
                      indent=2) + '\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    sys.exit(main())
