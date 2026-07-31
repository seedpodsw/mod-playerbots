@echo off
REM Sets honor to 0 for ALL Death Knight characters (class = 6).
REM IMPORTANT: run this while no DKs are online (or with the server stopped).
REM If a DK is online, logout will write their in-memory honor back and undo this.
REM
REM Usage:
REM   zero-dk-honor.bat

set MYSQL="C:\Program Files\MySQL\MySQL Server 8.4\bin\mysql.exe"
set HOST=127.0.0.1
set PORT=3336
set USER=admin
set PASS=root
set DB=acore_characters

set MYSQL_PWD=%PASS%

echo Zeroing honor for Death Knights (class 6) ...
%MYSQL% --host=%HOST% --port=%PORT% --user=%USER% --database=%DB% -e "SELECT SUM(online) AS dk_online, COUNT(*) AS dk_total, COALESCE(SUM(totalHonorPoints),0) AS honor_before FROM characters WHERE class = 6; UPDATE characters SET totalHonorPoints = 0, todayHonorPoints = 0, yesterdayHonorPoints = 0 WHERE class = 6 AND online = 0; SELECT ROW_COUNT() AS updated_offline; SELECT COUNT(*) AS still_with_honor FROM characters WHERE class = 6 AND totalHonorPoints > 0;"

if errorlevel 1 (
    echo FAILED
    pause
    exit /b 1
)

echo Done. If still_with_honor ^> 0, those DKs were online - kick/logout them and re-run.
pause
