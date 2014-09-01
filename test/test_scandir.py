"""Tests for scandir.scandir()."""

import os
import sys
import unittest

try:
    import scandir
    has_scandir = True
except ImportError:
    has_scandir = False

FILE_ATTRIBUTE_DIRECTORY = 16

test_path = os.path.join(os.path.dirname(__file__), 'dir')

IS_PY3 = sys.version_info >= (3, 0)

if IS_PY3:
    int_types = int
else:
    int_types = (int, long)
    str = unicode


class TestMixin(object):
    def _show_skipped(self):
        sys.stdout.write('[skipped] ')

    def test_basic(self):
        entries = sorted(self.scandir_func(test_path), key=lambda e: e.name)
        self.assertEqual([(e.name, e.is_dir()) for e in entries],
                         [('file1.txt', False), ('file2.txt', False), ('subdir', True)])

        # TODO ben: test .path attribute

    def test_dir_entry(self):
        entries = dict((e.name, e) for e in self.scandir_func(test_path))
        e = entries['file1.txt']
        self.assertEqual([e.is_dir(), e.is_file(), e.is_symlink()], [False, True, False])
        e = entries['file2.txt']
        self.assertEqual([e.is_dir(), e.is_file(), e.is_symlink()], [False, True, False])
        e = entries['subdir']
        self.assertEqual([e.is_dir(), e.is_file(), e.is_symlink()], [True, False, False])

        self.assertEqual(entries['file1.txt'].stat().st_size, 4)
        self.assertEqual(entries['file2.txt'].stat().st_size, 8)

    def test_stat(self):
        entries = list(self.scandir_func(test_path))
        for entry in entries:
            os_stat = os.stat(os.path.join(test_path, entry.name))
            scandir_stat = entry.stat()
            self.assertEqual(os_stat.st_mode, scandir_stat.st_mode)
            self.assertEqual(int(os_stat.st_mtime), int(scandir_stat.st_mtime))
            self.assertEqual(int(os_stat.st_ctime), int(scandir_stat.st_ctime))
            self.assertEqual(os_stat.st_size, scandir_stat.st_size)

    def test_returns_iter(self):
        it = self.scandir_func(test_path)
        entry = next(it)
        assert hasattr(entry, 'name')

    def check_file_attributes(self, result):
        self.assertTrue(hasattr(result, 'st_file_attributes'))
        self.assertTrue(isinstance(result.st_file_attributes, int_types))
        self.assertTrue(0 <= result.st_file_attributes <= 0xFFFFFFFF)

    def test_file_attributes(self):
        if sys.platform != 'win32' or not self.has_file_attributes:
            # st_file_attributes is Win32 specific (but can't use
            # unittest.skipUnless on Python 2.6)
            self._show_skipped()
            return

        entries = dict((e.name, e) for e in self.scandir_func(test_path))

        # test st_file_attributes on a file (FILE_ATTRIBUTE_DIRECTORY not set)
        result = entries['file1.txt'].stat()
        self.check_file_attributes(result)
        self.assertEqual(result.st_file_attributes & FILE_ATTRIBUTE_DIRECTORY, 0)

        # test st_file_attributes on a directory (FILE_ATTRIBUTE_DIRECTORY set)
        result = entries['subdir'].stat()
        self.check_file_attributes(result)
        self.assertEqual(result.st_file_attributes & FILE_ATTRIBUTE_DIRECTORY,
                         FILE_ATTRIBUTE_DIRECTORY)

    def test_path(self):
        entries = sorted(self.scandir_func(test_path), key=lambda e: e.name)
        self.assertEqual([(os.path.basename(e.name), e.is_dir()) for e in entries],
                         [('file1.txt', False), ('file2.txt', False), ('subdir', True)])
        self.assertEqual([os.path.normpath(os.path.join(test_path, e.name)) for e in entries],
                         [os.path.normpath(e.path) for e in entries])

    def _symlink_setup(self):
        try:
            os.symlink(os.path.join(test_path, 'file1.txt'),
                       os.path.join(test_path, 'link_to_file'))
        except NotImplementedError:
            # Windows versions before Vista don't support symbolic links
            return False

        dir_name = os.path.join(test_path, 'subdir')
        dir_link = os.path.join(test_path, 'link_to_dir')
        if sys.version_info >= (3, 3):
            # "target_is_directory" was only added in Python 3.3
            os.symlink(dir_name, dir_link, target_is_directory=True)
        else:
            os.symlink(dir_name, dir_link)

        return True

    def _symlink_teardown(self):
        os.remove(os.path.join(test_path, 'link_to_file'))
        os.remove(os.path.join(test_path, 'link_to_dir'))

    def test_symlink(self):
        if not hasattr(os, 'symlink') or not self._symlink_setup():
            self._show_skipped()
            return
        try:
            entries = sorted(self.scandir_func(test_path), key=lambda e: e.name)
            self.assertEqual([(e.name, e.is_symlink()) for e in entries],
                             [('file1.txt', False), ('file2.txt', False),
                              ('link_to_dir', True), ('link_to_file', True),
                              ('subdir', False)])
        finally:
            self._symlink_teardown()

    def test_bytes(self):
        # Check that unicode filenames are returned correctly as bytes in output
        path = os.path.join(test_path, 'subdir').encode(sys.getfilesystemencoding(), 'replace')
        self.assertIsInstance(path, bytes)
        entries = [e for e in self.scandir_func(path) if e.name.startswith(b'unicod')]
        self.assertEqual(len(entries), 1)
        entry = entries[0]

        self.assertIsInstance(entry.name, bytes)
        self.assertIsInstance(entry.path, bytes)

        # b'unicod?.txt' on Windows, b'unicod\xc6\x8f.txt' (UTF-8) or similar on POSIX
        entry_name = u'unicod\u018f.txt'.encode(sys.getfilesystemencoding(), 'replace')
        self.assertEqual(entry.name, entry_name)
        self.assertEqual(entry.path, os.path.join(path, entry_name))

    def test_unicode(self):
        # Check that unicode filenames are returned correctly as (unicode) str in output
        path = os.path.join(test_path, 'subdir')
        if not IS_PY3:
            path = path.decode(sys.getfilesystemencoding(), 'replace')
        self.assertIsInstance(path, str)
        entries = [e for e in self.scandir_func(path) if e.name.startswith('unicod')]
        self.assertEqual(len(entries), 1)
        entry = entries[0]

        self.assertIsInstance(entry.name, str)
        self.assertIsInstance(entry.path, str)

        entry_name = u'unicod\u018f.txt'
        self.assertEqual(entry.name, entry_name)
        self.assertEqual(entry.path, os.path.join(path, u'unicod\u018f.txt'))

        # Check that it handles unicode input properly
        path = os.path.join(test_path, 'subdir', u'unidir\u018f')
        self.assertIsInstance(path, str)
        entries = list(self.scandir_func(path))
        self.assertEqual(len(entries), 1)
        entry = entries[0]

        self.assertIsInstance(entry.name, str)
        self.assertIsInstance(entry.path, str)
        self.assertEqual(entry.name, 'file1.txt')
        self.assertEqual(entry.path, os.path.join(path, 'file1.txt'))

    # TODO ben: add tests for follow_symlinks parameters
    # TODO ben: add tests for file not found is_dir/is_file/stat


if has_scandir:
    class TestScandirGeneric(unittest.TestCase, TestMixin):
        def setUp(self):
            self.scandir_func = scandir.scandir_generic
            self.has_file_attributes = False


    if hasattr(scandir, 'scandir_python'):
        class TestScandirPython(unittest.TestCase, TestMixin):
            def setUp(self):
                self.scandir_func = scandir.scandir_python
                self.has_file_attributes = True


    if hasattr(scandir, 'scandir_c'):
        class TestScandirC(unittest.TestCase, TestMixin):
            def setUp(self):
                self.scandir_func = scandir.scandir_c
                self.has_file_attributes = True


if hasattr(os, 'scandir'):
    class TestScandirOS(unittest.TestCase, TestMixin):
        def setUp(self):
            self.scandir_func = os.scandir
            self.has_file_attributes = True
