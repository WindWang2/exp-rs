@echo off
rem Compatibility alias: keep one R8 build implementation.
call "%~dp0build_r8sci.cmd" %*
exit /b %errorlevel%
