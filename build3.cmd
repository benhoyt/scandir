@echo off
set PYTHON=C:\Users\tim\work-in-progress\python\scandir34\python.bat

echo ====================
echo Remove existing .pyd
del *.pyd
IF ERRORLEVEL 1 pause

echo ===============
echo Build extension
%PYTHON% setup.py build_ext --debug --inplace
IF ERRORLEVEL 1 pause

goto finish

echo =============
echo Run extension
%PYTHON% -c "import _scandir; print([name for name, _ in _scandir.scandir_helper(u'./*')])"
IF ERRORLEVEL 1 pause

echo ==============
echo Test extension
IF ERRORLEVEL 1 pause
%PYTHON% tests\run_tests.py
IF ERRORLEVEL 1 pause

:finish