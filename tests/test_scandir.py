"""Tests for scandir.scandir()."""

import os
import sys
import unittest

import scandir

test_path = os.path.join(os.path.dirname(__file__), 'dir')

class TestScandir(unittest.TestCase):
    def test_basic(self):
        entries = sorted(scandir.scandir(test_path), key=lambda e: e.name)
        self.assertEqual([(e.name, e.is_dir()) for e in entries],
                         [('file1.txt', False), ('file2.txt', False), ('subdir', True)])

    def test_dir_entry(self):
        entries = dict((e.name, e) for e in scandir.scandir(test_path))
        e = entries['file1.txt']
        self.assertEquals([e.is_dir(), e.is_file(), e.is_symlink()], [False, True, False])
        e = entries['file2.txt']
        self.assertEquals([e.is_dir(), e.is_file(), e.is_symlink()], [False, True, False])
        e = entries['subdir']
        self.assertEquals([e.is_dir(), e.is_file(), e.is_symlink()], [True, False, False])

        self.assertEquals(entries['file1.txt'].lstat().st_size, 4)
        self.assertEquals(entries['file2.txt'].lstat().st_size, 8)

    def test_stat(self):
        entries = list(scandir.scandir(test_path))
        for entry in entries:
            os_stat = os.lstat(os.path.join(test_path, entry.name))
            scandir_stat = entry.lstat()
            self.assertEquals(os_stat.st_mode, scandir_stat.st_mode)
            self.assertEquals(int(os_stat.st_mtime), int(scandir_stat.st_mtime))
            self.assertEquals(int(os_stat.st_ctime), int(scandir_stat.st_ctime))
            self.assertEquals(os_stat.st_size, scandir_stat.st_size)

    def test_returns_iter(self):
        it = scandir.scandir(test_path)
        entry = next(it)
        assert hasattr(entry, 'name')
