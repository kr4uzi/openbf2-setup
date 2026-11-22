@rem Inno Setup's SignTool hook (OpenBF2.iss, ISCC /Sopenbf2=...): signs the given files with sign.ps1
@powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0sign.ps1" %*
@exit /b %errorlevel%
