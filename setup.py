"""Run "python setup.py install" to install scandir."""

try:
    from setuptools import setup, Extension
    from setuptools.command.build_ext import build_ext as base_build_ext
except ImportError:
    import warnings
    import sys
    val = sys.exc_info()[1]

    warnings.warn("import of setuptools failed %r" % val)
    from distutils.core import setup, Extension
    from distutils.command.build_ext import build_ext as base_build_ext

import os
import re
import sys
import logging

if '--require-scandir-extension' in sys.argv:
    sys.argv = [arg for arg in sys.argv if arg != '--require-scandir-extension']
    require_scandir_extension = True
else:
    require_scandir_extension = False

# Get version without importing scandir because that will lock the
# .pyd file (if scandir is already installed) so it can't be
# overwritten during the install process
with open(os.path.join(os.path.dirname(__file__), 'scandir.py')) as f:
    for line in f:
        match = re.match(r"__version__.*'([0-9.]+)'", line)
        if match:
            version = match.group(1)
            break
    else:
        raise Exception("Couldn't find version in setup.py")

with open('README.rst') as f:
    long_description = f.read()


class BuildExt(base_build_ext):

    # the extension is optional since in case of lack of c the api
    # there is a ctypes fallback and a slow python fallback

    def build_extension(self, ext):
        try:
            base_build_ext.build_extension(self, ext)
        except Exception:
            if require_scandir_extension:
                logging.error('--require-scandir-extension is set, not falling back to Python implementation')
                raise
            info = sys.exc_info()
            logging.warn("building %s failed with %s: %s", ext.name, info[0], info[1])

extension = Extension('_scandir', ['_scandir.c'], optional=not require_scandir_extension)


setup(
    name='scandir',
    version=version,
    author='Ben Hoyt',
    author_email='benhoyt@gmail.com',
    url='https://github.com/benhoyt/scandir',
    license='New BSD License',
    description='scandir, a better directory iterator and faster os.walk()',
    long_description=long_description,
    py_modules=['scandir'],
    ext_modules=[extension],
    classifiers=[
        'Development Status :: 5 - Production/Stable',
        'Intended Audience :: Developers',
        'Operating System :: OS Independent',
        'License :: OSI Approved :: BSD License',
        'Programming Language :: Python',
        'Topic :: System :: Filesystems',
        'Topic :: System :: Operating System',
        'Programming Language :: Python',
        'Programming Language :: Python :: 2',
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.4',
        'Programming Language :: Python :: 3.5',
        'Programming Language :: Python :: 3.6',
        'Programming Language :: Python :: 3.7',
        'Programming Language :: Python :: Implementation :: CPython',
    ], cmdclass={'build_ext': BuildExt},
)
