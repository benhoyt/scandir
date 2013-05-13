BetterWalk, a better and faster os.walk() for Python
====================================================

BetterWalk is a somewhat better and significantly faster version of Python's
`os.walk()`, as well as a generator version of `os.listdir()`.

This GitHub repo is where I'm developing it, but it's also available as a
[library on PyPI](TODO).


Background
----------

Python's built-in `os.walk()` is significantly slower than it needs to be,
because -- in addition to calling `listdir()` on each directory -- it calls
`stat()` on each file to determine whether the filename is a directory or not.
But both `FindFirstFile` / `FindNextFile` on Windows and `readdir` on Linux/OS
X/BSD already tell you whether the files returned are directories or not, so
no further `stat` system calls are needed. In short, you can reduce the number
of system calls from about 2N to N, where N is the total number of files and
directories in the tree.

**In practice, removing all those extra system calls makes walking about 2-3
times as fast on Windows, and about 1.5 times as fast on Linux and Mac OS X.**
So we're not talking about micro-optimizations. [See more benchmarks
below.](#benchmarks)

Somewhat relatedly, many people have also asked for a version of
`os.listdir()` that yields filenames as it iterates instead of returning them
as one big list.

So as well as a faster `walk()`, BetterWalk adds `iterdir_stat()` and
`iterdir()`. They're pretty easy to use, but [see below](#the-api) for the
full API docs.


Why you should care
-------------------

I'd love for these incremental (but significant!) improvements to be added to
the Python standard library. BetterWalk was released to help test the concept
and get it in shape for inclusion in the standard `os` module.

There are various third-party "path" and "walk directory" libraries available,
but Python's `os` module isn't going away anytime soon. So we might as well
speed it up and add small improvements where possible.

**So I'd love it if you could help test BetterWalk, report bugs, suggest
improvement, or comment on the API.** And perhaps you'll see these speed-ups
and API additions in Python 3.4 ... :-)


Benchmarks
----------

Below are results showing how many times as fast `betterwalk.walk()` is than
`os.walk()` on various systems, found by running `benchmark.py` with no
arguments as well as with the `-s` argument (which totals the directory size).

```
System version              Python version    Speed ratio    With -s
--------------------------------------------------------------------
Windows 7 64 bit            2.6 64 bit        2.5            4.5
Windows 7 64 bit            2.7 64 bit        2.2            4.2
Windows 7 64 bit            3.2 64 bit        3.0            6.2
Windows XP 32 bit           2.7 32 bit        1.3            2.4
Windows XP 32 bit           3.3 32 bit        2.0            4.8

Debian 2.6.32 32 bit        2.6 32 bit        1.6            1.5
Ubuntu 12.04 64 bit VBox    2.7 64 bit        1.5            1.3
Ubuntu 12.04 64 bit VBox    3.2 64 bit        1.7            1.4

Mac OS X 10.7.5             2.7 64 bit        1.6            1.3
```

Originally I was benchmarking against the actual `os.walk()`, but that uses
`listdir()` which is written in C, whereas `iterdir_stat()` is written in pure
Python using ctypes. So (on Linux) the cost of ctypes was outweighing the
speed increase due to not doing the stat. So I've changed the benchmark to use
a ctypes version of `listdir()` so it's comparing apples with apples.

Some of these are systems I have running on VirtualBox -- if you can benchmark
it on your own similar system on real hardware, send in the results and I'll
replace these with your results.

Note that the gains are less than the above on smaller directories and greater
on larger directories. This is why `benchmark.py` creates a test directory
tree with a standardized size.


The API
-------

### walk()

The API for `betterwalk.walk()` is exactly the same as `os.walk()`, so just
[read the Python docs](http://docs.python.org/2/library/os.html#os.walk).

### iterdir_stat()

The `iterdir_stat()` function is BetterWalk's main workhorse. It's defined as
follows:

```python
iterdir_stat(path='.', pattern='*', fields=None)
```

It yield tuples of (filename, stat_result) for each filename that matches
`pattern` in the directory given by `path`. Like os.listdir(), `.` and `..`
are skipped, and the values are yielded in system-dependent order.

Pattern matching is done as per fnmatch.fnmatch(), but is more efficient if
the system's directory iteration supports pattern matching (like Windows).

The `fields` parameter specifies which fields to provide in each stat_result.
If None, only the fields the operating system can get "for free" are present
in stat_result. Otherwise "fields" must be an iterable of `st_*` attribute
names that the caller wants in each stat_result. The only special attribute
name is `st_mode_type`, which means the type bits in the st_mode field.

In practice, all fields are provided for free on Windows; whereas only the
st_mode_type information is provided for free on Linux, Mac OS X, and BSD.

Here's a good usage pattern for `iterdir_stat`. This is in fact almost exactly
how the faster `os.walk()` implementation uses it:

```python
dirs = []
nondirs = []
for name, st in betterwalk.iterdir_stat(top, fields=['st_mode_type']):
    if stat.S_ISDIR(st.st_mode):
        dirs.append(name)
    else:
        nondirs.append(name)
```

### iterdir()

The `iterdir()` function is similar to iterdir_stat(), except it doesn't
provide any stat information, but simply yields a list of filenames.


Further reading
---------------

* [Thread I started on the python-ideas list about speeding up os.walk()](http://mail.python.org/pipermail/python-ideas/2012-November/017770.html)
* [Question on StackOverflow about why os.walk() is slow and pointers to fix it](http://stackoverflow.com/questions/2485719/very-quickly-getting-total-size-of-folder)
* [Question on StackOverflow asking about iterating over a directory](http://stackoverflow.com/questions/4403598/list-files-in-a-folder-as-a-stream-to-begin-process-immediately)


To-do
-----

* Windows FindFirst/Next wildcard matching is quirky (compared to fnmatch). From Random832 on python-ideas: it matches short filenames, the behavior you noted of "?" at the end of patterns also applies to the end of the 'filename portion' (e.g. foo?.txt can match foo.txt), and the behavior of patterns ending in ".*" or "." isn't like fnmatch. [This](http://digital.ni.com/public.nsf/allkb/0DBE16907A17717B86256F7800169797) and [this](http://blogs.msdn.com/b/oldnewthing/archive/2007/12/17/6785519.aspx) might be helpful.
* From John Mulligan on python-ideas: there is a potential race condition between calling the readdir and the stat, like if the object is removed between calls. Consider what to do here, maybe return None for all st_* fields on stat() error? See also "This race isn't the only reason that stat can fail" message from Andrew Barnert.
* Test performance of pattern param on Windows versus fnmatch filtering afterwards.
* Add tests, especially for [reparse points / Win32 symbolic links](http://mail.python.org/pipermail/python-ideas/2012-November/017794.html)
* Consider adding "pattern" parameter to `walk()`, per Todd Whiteman

Flames, comments, bug reports
-----------------------------

Please send flames, comments, and questions about BetterWalk to Ben Hoyt:

http://benhoyt.com/

File bug reports or feature requests at the GitHub project page:

https://github.com/benhoyt/betterwalk
