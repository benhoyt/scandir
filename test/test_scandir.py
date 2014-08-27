"""Tests for scandir.scandir()."""

import os
import sys
import unittest

import scandir

test_path = os.path.join(os.path.dirname(__file__), 'dir')

# longs are just ints on Python 3
try:
    long
except NameError:
    long = int


class TestMixin(object):
    def _show_skipped(self):
        sys.stdout.write('[skipped] ')

    def test_basic(self):
        entries = sorted(self.scandir_func(test_path), key=lambda e: e.name)
        self.assertEqual([(e.name, e.is_dir()) for e in entries],
                         [('file1.txt', False), ('file2.txt', False), ('subdir', True)])

    def test_dir_entry(self):
        entries = dict((e.name, e) for e in self.scandir_func(test_path))
        e = entries['file1.txt']
        self.assertEquals([e.is_dir(), e.is_file(), e.is_symlink()], [False, True, False])
        e = entries['file2.txt']
        self.assertEquals([e.is_dir(), e.is_file(), e.is_symlink()], [False, True, False])
        e = entries['subdir']
        self.assertEquals([e.is_dir(), e.is_file(), e.is_symlink()], [True, False, False])

        self.assertEquals(entries['file1.txt'].stat().st_size, 4)
        self.assertEquals(entries['file2.txt'].stat().st_size, 8)

    def test_stat(self):
        entries = list(self.scandir_func(test_path))
        for entry in entries:
            os_stat = os.stat(os.path.join(test_path, entry.name))
            scandir_stat = entry.stat()
            self.assertEquals(os_stat.st_mode, scandir_stat.st_mode)
            self.assertEquals(int(os_stat.st_mtime), int(scandir_stat.st_mtime))
            self.assertEquals(int(os_stat.st_ctime), int(scandir_stat.st_ctime))
            self.assertEquals(os_stat.st_size, scandir_stat.st_size)

    def test_returns_iter(self):
        it = self.scandir_func(test_path)
        entry = next(it)
        assert hasattr(entry, 'name')

    def check_file_attributes(self, result):
        self.assertTrue(hasattr(result, 'st_file_attributes'))
        self.assertTrue(isinstance(result.st_file_attributes, (int, long)))
        self.assertTrue(0 <= result.st_file_attributes <= 0xFFFFFFFF)

    def test_file_attributes(self):
        if sys.platform != 'win32' or self.scandir_func == scandir.scandir_generic:
            # st_file_attributes is Win32 specific (but can't use
            # unittest.skipUnless on Python 2.6)
            self._show_skipped()
            return

        entries = dict((e.name, e) for e in self.scandir_func(test_path))

        # test st_file_attributes on a file (FILE_ATTRIBUTE_DIRECTORY not set)
        result = entries['file1.txt'].stat()
        self.check_file_attributes(result)
        self.assertEqual(result.st_file_attributes & scandir.FILE_ATTRIBUTE_DIRECTORY, 0)

        # test st_file_attributes on a directory (FILE_ATTRIBUTE_DIRECTORY set)
        result = entries['subdir'].stat()
        self.check_file_attributes(result)
        self.assertEqual(result.st_file_attributes & scandir.FILE_ATTRIBUTE_DIRECTORY,
                         scandir.FILE_ATTRIBUTE_DIRECTORY)

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

    # TODO ben: add tests for follow_symlinks parameters
    # TODO ben: add tests for bytes/unicode
    # TODO ben: add tests for file not found is_dir/is_file/stat


class TestScandirGeneric(unittest.TestCase, TestMixin):
    def setUp(self):
        self.scandir_func = scandir.scandir_generic


if hasattr(scandir, 'scandir_python'):
    class TestScandirPython(unittest.TestCase, TestMixin):
        def setUp(self):
            self.scandir_func = scandir.scandir_python


if hasattr(scandir, 'scandir_c'):
    class TestScandirC(unittest.TestCase, TestMixin):
        def setUp(self):
            self.scandir_func = scandir.scandir_c


if hasattr(os, 'scandir'):
    class TestScandirOS(unittest.TestCase, TestMixin):
        def setUp(self):
            self.scandir_func = os.scandir
