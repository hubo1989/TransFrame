#!/usr/bin/env python3
"""Stage/sign MSIX plus a stable App Installer feed; run in a Windows SDK shell.

No upload, certificate creation, or certificate trust changes are performed.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import zipfile
from urllib.parse import urlsplit
import xml.etree.ElementTree as ET
from xml.sax.saxutils import quoteattr

APP = Path(__file__).resolve().parents[1] / 'App'
NS = 'http://schemas.microsoft.com/appx/manifest/foundation/windows10'
FEED_NS = 'http://schemas.microsoft.com/appx/appinstaller/2018'
VCLIBS_NAME = 'Microsoft.VCLibs.140.00.UWPDesktop'
VCLIBS_PUBLISHER = 'CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US'


def validate(version, arch, publisher, base_url):
    if not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', version):
        raise ValueError('Version must have four numeric components')
    if any(int(part) > 65535 for part in version.split('.')) or int(version.split('.')[0]) == 0:
        raise ValueError('MSIX version components must be 0..65535, with major > 0')
    if arch not in ('x64', 'arm64'):
        raise ValueError('Architecture must be x64 or arm64')
    if not publisher.startswith('CN=') or 'Placeholder' in publisher:
        raise ValueError('Provide the real signing certificate subject as Publisher')
    url = urlsplit(base_url)
    if url.scheme != 'https' or not url.hostname or url.username or url.password or url.query or url.fragment:
        raise ValueError('Base URL must be HTTPS without credentials, query or fragment')
    if any(c.isspace() for c in base_url) or '\\' in base_url:
        raise ValueError('Base URL must be URL encoded')


def read_vclibs(path, arch):
    # Read only the manifest; do not extract arbitrary archive paths.
    with zipfile.ZipFile(path) as package:
        entry = package.getinfo('AppxManifest.xml')
        if entry.file_size > 1024 * 1024:
            raise ValueError('VCLibs manifest is too large')
        identity = ET.fromstring(package.read(entry)).find(f'{{{NS}}}Identity')
    if identity is None or identity.get('Name') != VCLIBS_NAME or identity.get('Publisher') != VCLIBS_PUBLISHER:
        raise ValueError('Expected the Microsoft VCLibs Desktop package')
    if identity.get('ProcessorArchitecture') != arch:
        raise ValueError('VCLibs architecture does not match the application')
    version = identity.get('Version', '')
    validate(version, arch, 'CN=Validation', 'https://example.org')
    if tuple(map(int, version.split('.'))) < (14, 0, 30704, 0):
        raise ValueError('VCLibs version is below the application minimum')
    return version, f'Microsoft.VCLibs.Desktop-{version}-{arch}.appx'


def write_descriptors(output, version, arch, publisher, base_url, dependency):
    validate(version, arch, publisher, base_url)
    base_url = base_url.rstrip('/')
    stage = output / 'stage'
    stage.mkdir(parents=True, exist_ok=False)
    shutil.copytree(APP / 'Assets', stage / 'Assets')
    # Keep all declared prefixes: IgnorableNamespaces also references prefixes
    # that ElementTree would otherwise remove as unused.
    manifest = (APP / 'Package.appxmanifest').read_text(encoding='utf-8')
    identity = '<Identity ' + ' '.join(
        f'{key}={quoteattr(value)}' for key, value in {
            'Name': 'TransFrame', 'Version': version,
            'Publisher': publisher, 'ProcessorArchitecture': arch,
        }.items()) + ' />'
    manifest = re.sub(r'<Identity\b[^>]*/>', lambda _: identity, manifest, count=1)
    (stage / 'AppxManifest.xml').write_text(manifest, encoding='utf-8')

    package_name = f'TransFrame-{version}-{arch}.msix'
    feed_name = f'TransFrame-{arch}.appinstaller'
    tag = lambda name: f'{{{FEED_NS}}}{name}'
    ET.register_namespace('', FEED_NS)
    root = ET.Element(tag('AppInstaller'), {'Uri': f'{base_url}/{feed_name}', 'Version': version})
    ET.SubElement(root, tag('MainPackage'), {
        'Name': 'TransFrame', 'Publisher': publisher, 'Version': version,
        'ProcessorArchitecture': arch, 'Uri': f'{base_url}/{package_name}',
    })
    dependencies = ET.SubElement(root, tag('Dependencies'))
    ET.SubElement(dependencies, tag('Package'), {
        'Name': VCLIBS_NAME, 'Publisher': VCLIBS_PUBLISHER,
        'Version': dependency[0], 'ProcessorArchitecture': arch,
        'Uri': f'{base_url}/{dependency[1]}',
    })
    settings = ET.SubElement(root, tag('UpdateSettings'))
    ET.SubElement(settings, tag('OnLaunch'), {
        'HoursBetweenUpdateChecks': '0', 'ShowPrompt': 'true', 'UpdateBlocksActivation': 'false',
    })
    ET.SubElement(settings, tag('AutomaticBackgroundTask'))
    ET.indent(root)
    ET.ElementTree(root).write(output / feed_name, encoding='utf-8', xml_declaration=True)
    return stage, output / package_name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='Fresh output directory')
    parser.add_argument('--vclibs', type=Path, required=True, help='Microsoft VCLibs Desktop APPX for this architecture')
    parser.add_argument('--version', required=True)
    parser.add_argument('--arch', choices=['x64', 'arm64'], required=True)
    parser.add_argument('--publisher', required=True)
    parser.add_argument('--base-url', required=True)
    parser.add_argument('--certificate-thumbprint', required=True, help='Existing certificate in CurrentUser/My')
    args = parser.parse_args()
    validate(args.version, args.arch, args.publisher, args.base_url)
    if not args.exe.is_file():
        parser.error('EXE does not exist; build the Release application first')
    if not re.fullmatch(r'[0-9a-fA-F]{40}', args.certificate_thumbprint):
        parser.error('Certificate thumbprint must be 40 hexadecimal characters')
    for tool in ('makeappx.exe', 'signtool.exe'):
        if not shutil.which(tool):
            parser.error(f'{tool} is required; run in a Windows SDK developer shell')
    dependency = read_vclibs(args.vclibs, args.arch)
    subprocess.run(['signtool.exe', 'verify', '/pa', str(args.vclibs)], check=True)
    args.output.mkdir(parents=True, exist_ok=False)
    stage, package = write_descriptors(args.output, args.version, args.arch, args.publisher, args.base_url, dependency)
    shutil.copy2(args.vclibs, args.output / dependency[1])
    shutil.copy2(args.exe, stage / 'TransFrame.App.exe')
    subprocess.run(['makeappx.exe', 'pack', '/d', str(stage), '/p', str(package)], check=True)
    subprocess.run(['signtool.exe', 'sign', '/sha1', args.certificate_thumbprint,
                    '/fd', 'SHA256', '/tr', 'http://timestamp.digicert.com', '/td', 'SHA256', str(package)], check=True)
    subprocess.run(['signtool.exe', 'verify', '/pa', str(package)], check=True)
    print(f'Created and verified {package}. Upload MSIX and VCLibs APPX first, then the .appinstaller feed.')


if __name__ == '__main__':
    main()
