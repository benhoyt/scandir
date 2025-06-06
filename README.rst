
scandir, a better directory iterator and faster os.walk()
=========================================================

**Note: This repository is archived, because the "scandir" feature
has been included in the Python standard library since Python 3.5.**
You can still download scandir from PyPI if you want, but I won't
be making any more updates, and this repo has been archived. You're
better off using `os.scandir() <https://docs.python.org/3/library/os.html#os.scandir>`_
and `os.walk() <https://docs.python.org/3/library/os.html#os.walk>`_ directly from the Python
standard library.

You can read the story of `how I contributed scandir to Python <https://benhoyt.com/writings/scandir/>`_, and I've kept the old README below.

----------

``scandir()`` is a directory iteration function like ``os.listdir()``,
except that instead of returning a list of bare filenames, it yields
``DirEntry`` objects that include file type and stat information along
with the name. Using ``scandir()`` increases the speed of ``os.walk()``
by 2-20 times (depending on the platform and file system) by avoiding
unnecessary calls to ``os.stat()`` in most cases.


Now included in a Python near you!
----------------------------------

``scandir`` has been included in the Python 3.5 standard library as
``os.scandir()``, and the related performance improvements to
``os.walk()`` have also been included. So if you're lucky enough to be
using Python 3.5 (release date September 13, 2015) you get the benefit
immediately, otherwise just
`download this module from PyPI <https://pypi.python.org/pypi/scandir>`_,
install it with ``pip install scandir``, and then do something like
this in your code:

.. code-block:: python

    # Use the built-in version of scandir/walk if possible, otherwise
    # use the scandir module version
    try:
        from os import scandir, walk
    except ImportError:
        from scandir import scandir, walk

`PEP 471 <https://www.python.org/dev/peps/pep-0471/>`_, which is the
PEP that proposes including ``scandir`` in the Python standard library,
was `accepted <https://mail.python.org/pipermail/python-dev/2014-July/135561.html>`_
in July 2014 by Victor Stinner, the BDFL-delegate for the PEP.

This ``scandir`` module is intended to work on Python 2.7+ and Python
3.4+ (and it has been tested on those versions).


Background
----------

Python's built-in ``os.walk()`` is significantly slower than it needs to be,
because -- in addition to calling ``listdir()`` on each directory -- it calls
``stat()`` on each file to determine whether the filename is a directory or not.
But both ``FindFirstFile`` / ``FindNextFile`` on Windows and ``readdir`` on Linux/OS
X already tell you whether the files returned are directories or not, so
no further ``stat`` system calls are needed. In short, you can reduce the number
of system calls from about 2N to N, where N is the total number of files and
directories in the tree.

In practice, removing all those extra system calls makes ``os.walk()`` about
**7-50 times as fast on Windows, and about 3-10 times as fast on Linux and Mac OS
X.** So we're not talking about micro-optimizations. See more benchmarks
in the "Benchmarks" section below.

Somewhat relatedly, many people have also asked for a version of
``os.listdir()`` that yields filenames as it iterates instead of returning them
as one big list. This improves memory efficiency for iterating very large
directories.

So as well as a faster ``walk()``, scandir adds a new ``scandir()`` function.
They're pretty easy to use, but see "The API" below for the full docs.


Benchmarks
----------

Below are results showing how many times as fast ``scandir.walk()`` is than
``os.walk()`` on various systems, found by running ``benchmark.py`` with no
arguments:

====================   ==============   =============
System version         Python version   Times as fast
====================   ==============   =============
Windows 7 64-bit       2.7.7 64-bit     10.4
Windows 7 64-bit SSD   2.7.7 64-bit     10.3
Windows 7 64-bit NFS   2.7.6 64-bit     36.8
Windows 7 64-bit SSD   3.4.1 64-bit     9.9
Windows 7 64-bit SSD   3.5.0 64-bit     9.5
Ubuntu 14.04 64-bit    2.7.6 64-bit     5.8
Mac OS X 10.9.3        2.7.5 64-bit     3.8
====================   ==============   =============

All of the above tests were done using the fast C version of scandir
(source code in ``_scandir.c``).

Note that the gains are less than the above on smaller directories and greater
on larger directories. This is why ``benchmark.py`` creates a test directory
tree with a standardized size.


The API
-------

walk()
~~~~~~

The API for ``scandir.walk()`` is exactly the same as ``os.walk()``, so just
`read the Python docs <https://docs.python.org/3.5/library/os.html#os.walk>`_.

scandir()
~~~~~~~~~

The full docs for ``scandir()`` and the ``DirEntry`` objects it yields are
available in the `Python documentation here <https://docs.python.org/3.5/library/os.html#os.scandir>`_. 
But below is a brief summary as well.

    scandir(path='.') -> iterator of DirEntry objects for given path

Like ``listdir``, ``scandir`` calls the operating system's directory
iteration system calls to get the names of the files in the given
``path``, but it's different from ``listdir`` in two ways:

* Instead of returning bare filename strings, it returns lightweight
  ``DirEntry`` objects that hold the filename string and provide
  simple methods that allow access to the additional data the
  operating system may have returned.

* It returns a generator instead of a list, so that ``scandir`` acts
  as a true iterator instead of returning the full list immediately.

``scandir()`` yields a ``DirEntry`` object for each file and
sub-directory in ``path``. Just like ``listdir``, the ``'.'``
and ``'..'`` pseudo-directories are skipped, and the entries are
yielded in system-dependent order. Each ``DirEntry`` object has the
following attributes and methods:

* ``name``: the entry's filename, relative to the scandir ``path``
  argument (corresponds to the return values of ``os.listdir``)

* ``path``: the entry's full path name (not necessarily an absolute
  path) -- the equivalent of ``os.path.join(scandir_path, entry.name)``

* ``is_dir(*, follow_symlinks=True)``: similar to
  ``pathlib.Path.is_dir()``, but the return value is cached on the
  ``DirEntry`` object; doesn't require a system call in most cases;
  don't follow symbolic links if ``follow_symlinks`` is False

* ``is_file(*, follow_symlinks=True)``: similar to
  ``pathlib.Path.is_file()``, but the return value is cached on the
  ``DirEntry`` object; doesn't require a system call in most cases; 
  don't follow symbolic links if ``follow_symlinks`` is False

* ``is_symlink()``: similar to ``pathlib.Path.is_symlink()``, but the
  return value is cached on the ``DirEntry`` object; doesn't require a
  system call in most cases

* ``stat(*, follow_symlinks=True)``: like ``os.stat()``, but the
  return value is cached on the ``DirEntry`` object; does not require a
  system call on Windows (except for symlinks); don't follow symbolic links
  (like ``os.lstat()``) if ``follow_symlinks`` is False

* ``inode()``: return the inode number of the entry; the return value
  is cached on the ``DirEntry`` object

Here's a very simple example of ``scandir()`` showing use of the
``DirEntry.name`` attribute and the ``DirEntry.is_dir()`` method:

.. code-block:: python

    def subdirs(path):
        """Yield directory names not starting with '.' under given path."""
        for entry in os.scandir(path):
            if not entry.name.startswith('.') and entry.is_dir():
                yield entry.name

This ``subdirs()`` function will be significantly faster with scandir
than ``os.listdir()`` and ``os.path.isdir()`` on both Windows and POSIX
systems, especially on medium-sized or large directories.


Further reading
---------------

* `The Python docs for scandir <https://docs.python.org/3.5/library/os.html#os.scandir>`_
* `PEP 471 <https://www.python.org/dev/peps/pep-0471/>`_, the
  (now-accepted) Python Enhancement Proposal that proposed adding
  ``scandir`` to the standard library -- a lot of details here,
  including rejected ideas and previous discussion


Flames, comments, bug reports
-----------------------------

Please send flames, comments, and questions about scandir to Ben Hoyt:

http://benhoyt.com/

File bug reports for the version in the Python 3.5 standard library
`here <https://docs.python.org/3.5/bugs.html>`_, or file bug reports
or feature requests for this module at the GitHub project page:

https://github.com/benhoyt/scandir
