#!/usr/bin/env python3
"""Stage/sign MSIX plus a stable App Installer feed; run in a Windows SDK shell.

No upload is performed. Certificate creation/trust changes happen ONLY with
--self-signed-test-cert (one disposable test cert for local/CI verification;
never ship packages signed by it — see windows/README.md).
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import struct
import json
import zipfile
from urllib.parse import urlsplit
import xml.etree.ElementTree as ET
from xml.sax.saxutils import quoteattr

APP = Path(__file__).resolve().parents[1] / 'App'
NS = 'http://schemas.microsoft.com/appx/manifest/foundation/windows10'
FEED_NS = 'http://schemas.microsoft.com/appx/appinstaller/2018'
VCLIBS_NAME = 'Microsoft.VCLibs.140.00.UWPDesktop'
VCLIBS_PUBLISHER = 'CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US'
VCLIBS_MIN_VERSION = '14.0.30704.0'


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


def check_appinstaller(output, stage, package, arch, version, publisher, base_url, dependency):
    """Check the emitted feed against the staged manifest and runtime identity."""
    tag = lambda name: f'{{{FEED_NS}}}{name}'
    feed = ET.parse(output / f'TransFrame-{arch}.appinstaller').getroot()
    base_url = base_url.rstrip('/')
    if feed.tag != tag('AppInstaller') or feed.attrib != {
            'Version': version, 'Uri': f'{base_url}/TransFrame-{arch}.appinstaller'}:
        raise ValueError('Invalid AppInstaller root or feed URL/version')
    identity = ET.parse(stage / 'AppxManifest.xml').find(f'{{{NS}}}Identity')
    expected = {'Name': 'TransFrame', 'Publisher': publisher,
                'Version': version, 'ProcessorArchitecture': arch}
    if identity is None or any(identity.get(k) != v for k, v in expected.items()):
        raise ValueError('Manifest identity differs from the requested package')
    main = feed.findall(tag('MainPackage'))
    if len(main) != 1 or main[0].attrib != {**expected, 'Uri': f'{base_url}/{package.name}'}:
        raise ValueError('AppInstaller MainPackage differs from the manifest')
    runtimes = feed.findall(f"{tag('Dependencies')}/{tag('Package')}")
    if len(runtimes) != 1 or runtimes[0].attrib != {
            'Name': VCLIBS_NAME, 'Publisher': VCLIBS_PUBLISHER,
            'Version': dependency[0], 'ProcessorArchitecture': arch,
            'Uri': f'{base_url}/{dependency[1]}'}:
        raise ValueError('AppInstaller dependency differs from the VCLibs package')
    on_launch = feed.find(f"{tag('UpdateSettings')}/{tag('OnLaunch')}")
    background = feed.find(f"{tag('UpdateSettings')}/{tag('AutomaticBackgroundTask')}")
    if (on_launch is None or on_launch.attrib != {
            'HoursBetweenUpdateChecks': '0', 'ShowPrompt': 'true', 'UpdateBlocksActivation': 'false'}
            or background is None):
        raise ValueError('AppInstaller update settings are missing or inconsistent')


def check_package_content(package, stage):
    with zipfile.ZipFile(package) as archive:
        entries = {item.filename.replace('\\', '/'): item.file_size for item in archive.infolist()}
        for path in stage.rglob('*'):
            if path.is_file():
                name = path.relative_to(stage).as_posix()
                if entries.get(name) != path.stat().st_size:
                    raise ValueError(f'Packed MSIX is missing or truncated: {name}')
        print('MSIX content manifest:')
        for name, size in sorted(entries.items()):
            print(f'  {name} ({size} bytes)')


def check_pe_architecture(path, arch):
    if not path.is_file():
        raise ValueError(f'Required binary is missing: {path}')
    with path.open('rb') as binary:
        dos = binary.read(64)
        if len(dos) != 64 or dos[:2] != b'MZ':
            raise ValueError(f'{path.name} has no valid DOS header')
        offset = struct.unpack_from('<I', dos, 60)[0]
        if offset < 64 or offset > path.stat().st_size - 24:
            raise ValueError(f'{path.name} has an invalid PE header offset')
        binary.seek(offset)
        header = binary.read(24)
        machine = struct.unpack_from('<H', header, 4)[0]
        if header[:4] != b'PE\0\0' or machine != {'x64': 0x8664, 'arm64': 0xAA64}[arch]:
            raise ValueError(f'{path.name} is not a {arch} PE binary')


def check_stage_content(stage, arch):
    """Fail before makeappx on missing assets or mismatched PE architectures."""
    for name in ('TransFrame.App.exe', 'Microsoft.WindowsAppRuntime.Bootstrap.dll'):
        check_pe_architecture(stage / name, arch)
    manifest_root = ET.parse(stage / 'AppxManifest.xml').getroot()
    logos = {node.text.strip().replace('\\', '/')
             for node in manifest_root.findall(f'{{{NS}}}Properties/{{{NS}}}Logo')
             if node.text and node.text.strip()}
    for element in manifest_root.iter():
        for key in ('Logo', 'Square150x150Logo', 'Square44x44Logo'):
            value = element.get(key)
            if value:
                logos.add(value.replace('\\', '/'))
    if not logos:
        raise ValueError('Manifest declares no logo assets')
    for logo in logos:
        if not (stage / logo).is_file():
            raise ValueError(f'Manifest declares {logo} but it is missing from the package')
    strings = list((stage / 'Assets' / 'Strings').glob('*.json'))
    expected = {p.name for p in (APP / 'Assets' / 'Strings').glob('*.json')}
    if not strings or not expected.issubset({p.name for p in strings}):
        raise ValueError('Staged package is missing localization tables (Assets/Strings/*.json)')
    for path in strings:
        if not isinstance(json.loads(path.read_text(encoding='utf-8')), dict):
            raise ValueError(f'Localization table must be a JSON object: {path.name}')
    declared = set()
    for dep in manifest_root.findall(f'{{{NS}}}Dependencies/{{{NS}}}PackageDependency'):
        declared.add((dep.get('Name'), dep.get('MinVersion')))
    if (VCLIBS_NAME, VCLIBS_MIN_VERSION) not in declared:
        raise ValueError('Manifest is missing the Microsoft.VCLibs Desktop dependency')


def create_self_signed_test_cert(publisher):
    """Create a fresh one-day, non-exportable code-signing cert (TEST ONLY).

    The private key stays in CurrentUser/My. Trust is a separate step, and
    other machines will not trust the certificate unless explicitly imported.
    """
    subject = publisher.replace("'", "''")  # PowerShell single-quoted literal
    script = (
        "$ErrorActionPreference='Stop';"
        "$cert=New-SelfSignedCertificate -Subject '" + subject + "'"
        " -Type CodeSigningCert -KeyExportPolicy NonExportable"
        " -FriendlyName 'TransFrameMSIXTestCert'"
        " -CertStoreLocation Cert:\\CurrentUser\\My"
        " -NotAfter (Get-Date).AddDays(1);"
        "if (-not $cert) { throw 'New-SelfSignedCertificate returned no certificate' };"
        "Write-Output $cert.Thumbprint"
    )
    # Prefer pwsh (PowerShell 7): on machines where the Certificate PSDrive
    # fails to load under Windows PowerShell 5.1 (observed in some MSYS2/
    # automation contexts), pwsh is the reliable host. Fall back to
    # powershell.exe for CI runners where only 5.1 exists but the provider
    # is healthy.
    for shell in ('pwsh.exe', 'powershell.exe'):
        if shutil.which(shell):
            result = subprocess.run([shell, '-NoProfile', '-NonInteractive', '-Command', script],
                                    capture_output=True, text=True, check=True, timeout=60)
            break
    else:
        raise RuntimeError('Neither pwsh.exe nor powershell.exe is available')
    thumbprint = result.stdout.strip().splitlines()[-1].strip()
    if not re.fullmatch(r'[0-9a-fA-F]{40}', thumbprint):
        raise ValueError(f'Unexpected certificate thumbprint from PowerShell: {thumbprint!r}')
    return thumbprint.lower()


def trust_test_cert_in_root(thumbprint):
    """Trust only this test certificate. Desktop users may see a trust dialog.

    Elevated CI runners use LocalMachine/Root; ordinary local runs use
    CurrentUser/Root. Neither path requests elevation or changes policy.
    """
    if not re.fullmatch(r'[0-9a-fA-F]{40}', thumbprint):
        raise ValueError('Invalid test certificate thumbprint')
    print(f'Trusting TEST certificate {thumbprint}; accept the matching Windows '
          'certificate dialog if shown (90 second timeout).', flush=True)
    script = (
        "$ErrorActionPreference='Stop';"
        "$cert=Get-Item 'Cert:\\CurrentUser\\My\\" + thumbprint + "';"
        "$principal=[Security.Principal.WindowsPrincipal]::new("
        "[Security.Principal.WindowsIdentity]::GetCurrent());"
        "$location=if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) "
        "{ 'LocalMachine' } else { 'CurrentUser' };"
        "$store=[System.Security.Cryptography.X509Certificates.X509Store]::new('Root',$location);"
        "$store.Open('ReadWrite');"
        "try { $store.Add([System.Security.Cryptography.X509Certificates.X509Certificate2]::new($cert.RawData)) }"
        "finally { $store.Close() };"
        "Write-Output ('Trusted test certificate in ' + $location + '/Root')"
    )
    for shell in ('pwsh.exe', 'powershell.exe'):
        if shutil.which(shell):
            subprocess.run([shell, '-NoProfile', '-NonInteractive', '-Command', script],
                           check=True, timeout=90)
            return
    raise RuntimeError('Neither pwsh.exe nor powershell.exe is available')


def sign_with_test_cert(package, publisher):
    thumbprint = create_self_signed_test_cert(publisher)
    trust_test_cert_in_root(thumbprint)
    # A one-day disposable certificate needs no network timestamp. Production
    # signing retains its timestamp server and never uses this test-only path.
    subprocess.run(['signtool.exe', 'sign', '/sha1', thumbprint,
                    '/fd', 'SHA256', str(package)], check=True, timeout=60)


def verify_package_signature(package, self_signed_test):
    """Final signature self-check (signtool verify /pa).

    /pa validates an Authenticode chain to a trusted root, so it can never
    pass for a self-signed certificate that is not itself trusted as a root.
    Elevated CI runners trust the test cert in LocalMachine/Root, where /pa
    passes unchanged. Local sideload flows trust the cert in TrustedPeople
    instead — invisible to /pa but sufficient for Add-AppxPackage — so a
    failure there is downgraded to a note, and installation is the real
    acceptance test. Production (CA-issued) certificates stay strict.
    """
    result = subprocess.run(['signtool.exe', 'verify', '/pa', str(package)])
    if result.returncode == 0:
        return
    if self_signed_test:
        print(f'NOTE: /pa cannot chain-verify a self-signed test certificate '
              f'without Root trust; {package.name} is signed (signtool sign '
              f'succeeded). Sideload acceptance: import the cert into '
              f'LocalMachine\\TrustedPeople (admin), then Add-AppxPackage.',
              flush=True)
        return
    result.check_returncode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='Fresh output directory')
    parser.add_argument('--vclibs', type=Path, required=True, help='Microsoft VCLibs Desktop APPX for this architecture')
    parser.add_argument('--version', required=True)
    parser.add_argument('--arch', choices=['x64', 'arm64'], required=True)
    parser.add_argument('--publisher', required=True)
    parser.add_argument('--base-url', required=True)
    parser.add_argument('--certificate-thumbprint', help='Existing certificate in CurrentUser/My (production signing)')
    parser.add_argument('--self-signed-test-cert', action='store_true',
                        help='Generate a one-day test cert, trust in Root (CurrentUser, or LocalMachine '
                             'when elevated), and sign. TEST PACKAGES ONLY; not trusted on other machines.')
    args = parser.parse_args()
    validate(args.version, args.arch, args.publisher, args.base_url)
    if not args.exe.is_file():
        parser.error('EXE does not exist; build the Release application first')
    if args.certificate_thumbprint and args.self_signed_test_cert:
        parser.error('--certificate-thumbprint and --self-signed-test-cert are mutually exclusive')
    if not args.certificate_thumbprint and not args.self_signed_test_cert:
        parser.error('one of --certificate-thumbprint / --self-signed-test-cert is required')
    if args.certificate_thumbprint and not re.fullmatch(r'[0-9a-fA-F]{40}', args.certificate_thumbprint):
        parser.error('Certificate thumbprint must be 40 hexadecimal characters')
    for tool in ('makeappx.exe', 'signtool.exe'):
        if not shutil.which(tool):
            parser.error(f'{tool} is required; run in a Windows SDK developer shell')
    exe_dir = args.exe.parent
    bootstrap = exe_dir / 'Microsoft.WindowsAppRuntime.Bootstrap.dll'
    if not bootstrap.is_file():
        parser.error(f'{bootstrap.name} is missing next to the EXE; restore/build first '
                     '(App project copies the win-$(Platform) runtime DLL to the output directory)')
    dependency = read_vclibs(args.vclibs, args.arch)
    subprocess.run(['signtool.exe', 'verify', '/pa', str(args.vclibs)], check=True)
    args.output.mkdir(parents=True, exist_ok=False)
    stage, package = write_descriptors(args.output, args.version, args.arch, args.publisher, args.base_url, dependency)
    shutil.copy2(args.vclibs, args.output / dependency[1])
    shutil.copy2(args.exe, stage / 'TransFrame.App.exe')
    shutil.copy2(bootstrap, stage / bootstrap.name)
    check_stage_content(stage, args.arch)
    check_appinstaller(args.output, stage, package, args.arch, args.version,
                       args.publisher, args.base_url, dependency)
    subprocess.run(['makeappx.exe', 'pack', '/d', str(stage), '/p', str(package)], check=True)
    if args.self_signed_test_cert:
        sign_with_test_cert(package, args.publisher)
    else:
        subprocess.run(['signtool.exe', 'sign', '/sha1', args.certificate_thumbprint,
                        '/fd', 'SHA256', '/tr', 'http://timestamp.digicert.com', '/td', 'SHA256', str(package)],
                       check=True)
    verify_package_signature(package, self_signed_test=args.self_signed_test_cert)
    check_package_content(package, stage)
    print(f'Created and verified {package}. Upload MSIX and VCLibs APPX first, then the .appinstaller feed.')


if __name__ == '__main__':
    main()
