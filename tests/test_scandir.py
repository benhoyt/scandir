"""Tests for scandir.scandir()."""

import os
import sys
import unittest

import scandir

test_path = os.path.join(os.path.dirname(__file__), 'dir')

class TestScandir(unittest.TestCase):
    def test_basic(self):
        entries = sorted(scandir.scandir(test_path), key=lambda e: e.name)
        self.assertEqual([(e.name, e.isdir()) for e in entries],
                         [('file1.txt', False), ('file2.txt', False), ('subdir', True)])

    def test_dir_entry(self):
        entries = {e.name: e for e in scandir.scandir(test_path)}
        e = entries['file1.txt']
        self.assertEquals([e.isdir(), e.isfile(), e.islink()], [False, True, False])
        e = entries['file2.txt']
        self.assertEquals([e.isdir(), e.isfile(), e.islink()], [False, True, False])
        e = entries['subdir']
        self.assertEquals([e.isdir(), e.isfile(), e.islink()], [True, False, False])

        self.assertEquals(entries['file1.txt'].lstat().st_size, 4)
        self.assertEquals(entries['file2.txt'].lstat().st_size, 8)

        if sys.platform == 'win32':
            assert entries['file1.txt'].dirent is None
        else:
            assert entries['file1.txt'].dirent is not None
            assert isinstance(entries['file1.txt'].dirent.d_ino, (int, long))
            assert isinstance(entries['file1.txt'].dirent.d_type, int)

    def test_returns_iter(self):
        it = scandir.scandir(test_path)
        it.next()
