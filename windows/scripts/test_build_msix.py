import tempfile
import unittest
import zipfile
from pathlib import Path
import xml.etree.ElementTree as ET
from build_msix import write_descriptors, validate, read_vclibs, NS, FEED_NS, VCLIBS_NAME, VCLIBS_PUBLISHER


class InstallerTests(unittest.TestCase):
    def test_feed_and_package_identity_match_for_both_architectures(self):
        for arch in ('x64', 'arm64'):
            with self.subTest(arch=arch), tempfile.TemporaryDirectory() as directory:
                output = Path(directory)
                stage, msix = write_descriptors(output, '1.2.3.0', arch, 'CN=Test & Company', 'https://example.org/windows/', ('14.0.33728.0', f'vclibs-{arch}.appx'))
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
