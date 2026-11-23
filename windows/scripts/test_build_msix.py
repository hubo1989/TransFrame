import tempfile
import unittest
import zipfile
import struct
import shutil
from unittest.mock import patch, Mock
from pathlib import Path
import xml.etree.ElementTree as ET
from build_msix import (write_descriptors, validate, read_vclibs,
                        check_stage_content, check_appinstaller, check_package_content,
                        create_self_signed_test_cert, APP, NS, FEED_NS, VCLIBS_NAME,
                        VCLIBS_PUBLISHER)


class StageContentTests(unittest.TestCase):
    """check_stage_content: packaging must fail loudly on incomplete content."""

    def _stage(self, root, with_bootstrap=True, with_strings=True):
        stage = root / 'stage'
        (stage / 'Assets' / 'Strings').mkdir(parents=True)
        exe = stage / 'TransFrame.App.exe'
        pe = bytearray(88)
        pe[:2] = b'MZ'
        struct.pack_into('<I', pe, 60, 64)
        pe[64:68] = b'PE\0\0'
        struct.pack_into('<H', pe, 68, 0x8664)
        exe.write_bytes(pe)
        if with_bootstrap:
            (stage / 'Microsoft.WindowsAppRuntime.Bootstrap.dll').write_bytes(pe)
        for name in ('Square150x150Logo.png', 'Square44x44Logo.png', 'StoreLogo.png'):
            (stage / 'Assets' / name).write_bytes(b'png')
        if with_strings:
            shutil.copytree(APP / 'Assets' / 'Strings', stage / 'Assets' / 'Strings', dirs_exist_ok=True)
        manifest = (Path(__file__).resolve().parents[1] / 'App' / 'Package.appxmanifest'
                    ).read_text(encoding='utf-8')
        (stage / 'AppxManifest.xml').write_text(manifest, encoding='utf-8')
        return stage

    def test_complete_stage_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = self._stage(Path(directory))
            check_stage_content(stage, 'x64')  # must not raise

    def test_missing_bootstrap_dll_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = self._stage(Path(directory), with_bootstrap=False)
            with self.assertRaises(ValueError):
                check_stage_content(stage, 'x64')

    def test_missing_localization_tables_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = self._stage(Path(directory), with_strings=False)
            with self.assertRaises(ValueError):
                check_stage_content(stage, 'x64')

    def test_declared_logo_missing_from_stage_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = self._stage(Path(directory))
            (stage / 'Assets' / 'StoreLogo.png').unlink()
            with self.assertRaises(ValueError):
                check_stage_content(stage, 'x64')


    def test_wrong_architecture_rejected_for_each_binary(self):
        for name in ('TransFrame.App.exe', 'Microsoft.WindowsAppRuntime.Bootstrap.dll'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                stage = self._stage(Path(directory))
                data = bytearray((stage / name).read_bytes())
                struct.pack_into('<H', data, 68, 0xAA64)
                (stage / name).write_bytes(data)
                with self.assertRaisesRegex(ValueError, 'not a x64 PE'):
                    check_stage_content(stage, 'x64')

    def test_partial_locale_loss_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = self._stage(Path(directory))
            next((stage / 'Assets' / 'Strings').glob('*.json')).unlink()
            with self.assertRaisesRegex(ValueError, 'localization'):
                check_stage_content(stage, 'x64')

    def test_packed_archive_must_contain_staged_files(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = self._stage(Path(directory))
            package = Path(directory) / 'test.msix'
            with zipfile.ZipFile(package, 'w') as archive:
                archive.writestr('AppxManifest.xml', (stage / 'AppxManifest.xml').read_bytes())
            with self.assertRaisesRegex(ValueError, 'missing or truncated'):
                check_package_content(package, stage)


class InstallerTests(unittest.TestCase):
    def test_feed_and_package_identity_match_for_both_architectures(self):
        for arch in ('x64', 'arm64'):
            with self.subTest(arch=arch), tempfile.TemporaryDirectory() as directory:
                output = Path(directory)
                stage, msix = write_descriptors(output, '1.2.3.0', arch, 'CN=Test & Company', 'https://example.org/windows/', ('14.0.33728.0', f'vclibs-{arch}.appx'))
                check_appinstaller(output, stage, msix, arch, '1.2.3.0', 'CN=Test & Company',
                                   'https://example.org/windows/', ('14.0.33728.0', f'vclibs-{arch}.appx'))
                namespaces = dict(pair for _, pair in ET.iterparse(stage / 'AppxManifest.xml', events=['start-ns']))
                for prefix in ET.parse(stage / 'AppxManifest.xml').getroot().get('IgnorableNamespaces').split():
                    self.assertIn(prefix, namespaces)
                identity = ET.parse(stage / 'AppxManifest.xml').find(f'{{{NS}}}Identity')
                feed = ET.parse(output / f'TransFrame-{arch}.appinstaller').getroot()
                package = feed.find(f'{{{FEED_NS}}}MainPackage')
                for field in ('Name', 'Publisher', 'Version', 'ProcessorArchitecture'):
                    self.assertEqual(identity.get(field), package.get(field))
                self.assertEqual(package.get('Uri'), f'https://example.org/windows/{msix.name}')
                runtime = feed.find(f'{{{FEED_NS}}}Dependencies/{{{FEED_NS}}}Package')
                self.assertEqual(runtime.get('Version'), '14.0.33728.0')
                self.assertEqual(runtime.get('Uri'), f'https://example.org/windows/vclibs-{arch}.appx')
                settings = feed.find(f'{{{FEED_NS}}}UpdateSettings')
                self.assertIsNotNone(settings.find(f'{{{FEED_NS}}}AutomaticBackgroundTask'))
                self.assertEqual(settings.find(f'{{{FEED_NS}}}OnLaunch').get('HoursBetweenUpdateChecks'), '0')
                for element in ET.parse(stage / 'AppxManifest.xml').iter():
                    for key, value in element.attrib.items():
                        if key in ('Logo', 'Square150x150Logo', 'Square44x44Logo'):
                            self.assertTrue((stage / value.replace('\\', '/')).is_file())
                self.assertTrue((stage / 'Assets/TransFrame.ico').is_file())

    def test_feed_self_check_rejects_broken_structure(self):
        mutations = (
            lambda root: root.set('Version', '9.0.0.0'),
            lambda root: root.find(f'{{{FEED_NS}}}MainPackage').set('ProcessorArchitecture', 'arm64'),
            lambda root: root.find(f'{{{FEED_NS}}}Dependencies/{{{FEED_NS}}}Package').set('Uri', 'http://wrong'),
            lambda root: root.remove(root.find(f'{{{FEED_NS}}}UpdateSettings')),
        )
        for mutation in mutations:
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as directory:
                output = Path(directory)
                dep = ('14.0.33728.0', 'vclibs-x64.appx')
                stage, msix = write_descriptors(output, '1.2.3.0', 'x64', 'CN=Test', 'https://example.org', dep)
                path = output / 'TransFrame-x64.appinstaller'
                tree = ET.parse(path)
                mutation(tree.getroot())
                tree.write(path, encoding='utf-8', xml_declaration=True)
                with self.assertRaises(ValueError):
                    check_appinstaller(output, stage, msix, 'x64', '1.2.3.0', 'CN=Test', 'https://example.org', dep)

    def test_certificate_subject_is_a_powershell_literal(self):
        with patch('build_msix.shutil.which', return_value='pwsh.exe'), \
                patch('build_msix.subprocess.run', return_value=Mock(stdout='A' * 40)) as run:
            create_self_signed_test_cert("CN=O'Brien; Write-Output evil")
            script = run.call_args.args[0][-1]
            self.assertIn("-Subject 'CN=O''Brien; Write-Output evil'", script)
            self.assertIn('-KeyExportPolicy NonExportable', script)
            self.assertNotIn('Where-Object', script)

    def test_vclibs_reads_exact_version_and_rejects_wrong_architecture(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'runtime.appx'
            package = ET.Element(f'{{{NS}}}Package')
            ET.SubElement(package, f'{{{NS}}}Identity', {
                'Name': VCLIBS_NAME, 'Publisher': VCLIBS_PUBLISHER,
                'Version': '14.0.33728.0', 'ProcessorArchitecture': 'x64',
            })
            with zipfile.ZipFile(path, 'w') as archive:
                archive.writestr('AppxManifest.xml', ET.tostring(package))
            self.assertEqual(read_vclibs(path, 'x64')[0], '14.0.33728.0')
            with self.assertRaises(ValueError):
                read_vclibs(path, 'arm64')

    def test_rejects_invalid_update_configuration(self):
        defaults = ['1.2.3.0', 'x64', 'CN=Test', 'https://example.org/windows']
        for index, invalid in ((0, '1.2.3'), (0, '1.2.65536.0'), (0, '0.1.2.0'),
                               (1, '../x64'), (2, 'CN=TransFrame Placeholder'),
                               (3, 'http://example.org'), (3, 'https://user:pw@example.org'),
                               (3, 'https://example.org/?query=1'), (3, 'https://example.org/a b')):
            args = defaults.copy()
            args[index] = invalid
            with self.subTest(args=args), self.assertRaises(ValueError):
                validate(*args)


if __name__ == '__main__':
    unittest.main()
