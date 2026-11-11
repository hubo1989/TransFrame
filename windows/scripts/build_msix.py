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
import sys
import tempfile
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


def check_stage_content(stage, arch, exe=None):
    """Fail before makeappx when the staged package is incomplete.

    PRD §11: the package must carry the EXE, the matching-architecture
    Windows App SDK bootstrap DLL, the declared logo assets and the
    localization tables (Assets\Strings\*.json) — a silent gap turns
    into a runtime crash or untranslated UI, not a packaging error.
    """
    exe = stage / 'TransFrame.App.exe'
    if exe is not None and not exe.is_file():
        raise ValueError(f'EXE does not exist: {exe}')
    if exe is not None and exe.stat().st_size == 0:
        raise ValueError('TransFrame.App.exe is empty; did the build fail?')
    bootstrap = stage / 'Microsoft.WindowsAppRuntime.Bootstrap.dll'
    if not bootstrap.is_file():
        raise ValueError(
            'Staged package is missing Microsoft.WindowsAppRuntime.Bootstrap.dll; '
            'copy the win-' + arch + ' runtime DLL next to the EXE before packaging')
    if bootstrap.stat().st_size == 0:
        raise ValueError('Staged bootstrap DLL is empty; restore the Windows App SDK package')
    if exe is not None:
        import struct
        with exe.open('rb') as fh:
            if fh.read(2) != b'MZ':
                raise ValueError(f'{exe.name} is not a PE binary (missing MZ header)')
    manifest_root = ET.parse(stage / 'AppxManifest.xml').getroot()
    logos = set()
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
    strings = sorted(p.name for p in (stage / 'Assets' / 'Strings').glob('*.json'))
    if not strings:
        raise ValueError('Staged package is missing localization tables (Assets\\Strings\\*.json)')
    declared = set()
    for dep in manifest_root.findall(f'{{{NS}}}Dependencies/{{{NS}}}PackageDependency'):
        declared.add((dep.get('Name'), dep.get('MinVersion')))
    if (VCLIBS_NAME, VCLIBS_MIN_VERSION) not in declared:
        raise ValueError('Manifest is missing the Microsoft.VCLibs Desktop dependency')


def create_self_signed_test_cert(publisher):
    """Create a disposable code-signing cert and trust it (TEST ONLY).

    The cert lives in CurrentUser\\My with a marker friendly name; the public
    portion is pushed into CurrentUser\\Root so `signtool verify /pa` passes
    on this machine. Nothing is exported — the private key never leaves the
    OS store, so the MSIX fails signature validation everywhere else.
    """
    name = 'TransFrameMSIXTestCert'
    # Built with concatenation — not an f-string — because the PowerShell
    # Where-Object script blocks contain literal braces.
    script = (
        "$ErrorActionPreference='Stop';"
        + "$name='" + name + "';"
        + "$existing=Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq '"
        + publisher
        + "' -and $_.FriendlyName -eq $name };"
        + "if (-not $existing) {"
        + "$cert=New-SelfSignedCertificate -Subject '"
        + publisher
        + "' -Type Custom -KeyUsage DigitalSignature"
        + " -FriendlyName $name -CertStoreLocation Cert:\CurrentUser\My"
        + " -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3');"
        + "if (-not $cert) { throw 'New-SelfSignedCertificate returned no certificate' }"
        + "} else { $cert=$existing[0] };"
        + "$root=Get-ChildItem Cert:\CurrentUser\Root | Where-Object { $_.Thumbprint -eq $cert.Thumbprint };"
        + "if (-not $root) {"
        + "$store=New-Object System.Security.Cryptography.X509Certificates.X509Store('Root','CurrentUser');"
        + "$store.Open('ReadWrite');"
        + "$store.Add((New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($cert)));"
        + "$store.Close();"
        + "};"
        + "Write-Output $cert.Thumbprint"
    )
    # Prefer pwsh (PowerShell 7): on machines where the Certificate PSDrive
    # fails to load under Windows PowerShell 5.1 (observed in some MSYS2/
    # automation contexts), pwsh is the reliable host. Fall back to
    # powershell.exe for CI runners where only 5.1 exists but the provider
    # is healthy.
    for shell in ('pwsh.exe', 'powershell.exe'):
        if shutil.which(shell):
            result = subprocess.run([shell, '-NoProfile', '-NonInteractive', '-Command', script],
                                    capture_output=True, text=True, check=True)
            break
    else:
        raise RuntimeError('Neither pwsh.exe nor powershell.exe is available')
    thumbprint = result.stdout.strip().splitlines()[-1].strip()
    if not re.fullmatch(r'[0-9a-fA-F]{40}', thumbprint):
        raise ValueError(f'Unexpected certificate thumbprint from PowerShell: {thumbprint!r}')
    return thumbprint.lower()


def trust_test_cert_in_root(thumbprint):
    """Trust the test cert in CurrentUser\Root so signtool /pa passes.

    CI runners accept this non-interactively; interactive desktop sessions
    may block on the Windows trust warning dialog — that is why the helper
    exists in the script (CI is the primary caller)."""
    script = (
        "$ErrorActionPreference='Stop';"
        + "$cert=Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Thumbprint -eq '"
        + thumbprint + "' };"
        + "$store=New-Object System.Security.Cryptography.X509Certificates.X509Store('Root','CurrentUser');"
        + "$store.Open('ReadWrite');"
        + "$store.Add((New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($cert)));"
        + "$store.Close();"
        + "Write-Output 'trusted'"
    )
    for shell in ('pwsh.exe', 'powershell.exe'):
        if shutil.which(shell):
            subprocess.run([shell, '-NoProfile', '-NonInteractive', '-Command', script],
                           capture_output=True, text=True, check=True)
            return


def sign_with_test_cert(package, publisher):
    thumbprint = create_self_signed_test_cert(publisher)
    trust_test_cert_in_root(thumbprint)
    try:
        subprocess.run(['signtool.exe', 'sign', '/sha1', thumbprint,
                        '/fd', 'SHA256', '/td', 'SHA256', '/tr', 'http://timestamp.digicert.com',
                        str(package)], check=True)
        subprocess.run(['signtool.exe', 'verify', '/pa', str(package)], check=True)
    except subprocess.CalledProcessError:
        print('NOTE: timestamping with the test cert can fail when the TSA is '
              'unreachable; retrying without a timestamp (test packages only).',
              file=sys.stderr)
        subprocess.run(['signtool.exe', 'sign', '/sha1', thumbprint,
                        '/fd', 'SHA256', str(package)], check=True)
        subprocess.run(['signtool.exe', 'verify', '/pa', str(package)], check=True)


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
                        help='Generate a disposable self-signed cert, trust it in CurrentUser/Root and sign. '
                             'TEST PACKAGES ONLY: the cert never leaves this machine, so the MSIX fails '
                             'signature validation everywhere else.')
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
    subprocess.run(['makeappx.exe', 'pack', '/d', str(stage), '/p', str(package)], check=True)
    if args.self_signed_test_cert:
        sign_with_test_cert(package, args.publisher)
    else:
        subprocess.run(['signtool.exe', 'sign', '/sha1', args.certificate_thumbprint,
                        '/fd', 'SHA256', '/tr', 'http://timestamp.digicert.com', '/td', 'SHA256', str(package)],
                       check=True)
    subprocess.run(['signtool.exe', 'verify', '/pa', str(package)], check=True)
    print(f'Created and verified {package}. Upload MSIX and VCLibs APPX first, then the .appinstaller feed.')


if __name__ == '__main__':
    main()
