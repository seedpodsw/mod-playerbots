@echo off
REM Clears LFG dungeon deserter + random dungeon cooldown for a character.
REM Best results when the character is LOGGED OUT (auras are in DB then).
REM If they are online, log out / relog after running this.
REM
REM Usage:
REM   clear-lfg-blocks.bat
REM   clear-lfg-blocks.bat Toppa
REM
REM Spells:
REM   71041 = LFG dungeon deserter
REM   71328 = random dungeon cooldown (RDF)

set CHAR_NAME=%~1
if "%CHAR_NAME%"=="" set CHAR_NAME=Toppa

set MYSQL="C:\Program Files\MySQL\MySQL Server 8.4\bin\mysql.exe"
set HOST=127.0.0.1
set PORT=3336
set USER=admin
set PASS=root
set DB=acore_characters

set MYSQL_PWD=%PASS%

echo Clearing LFG blocks for %CHAR_NAME% ...
%MYSQL% --host=%HOST% --port=%PORT% --user=%USER% --database=%DB% -e "SELECT c.guid, c.name, c.online, ca.spell, ca.remainTime FROM characters c LEFT JOIN character_aura ca ON ca.guid = c.guid AND ca.spell IN (71041, 71328) WHERE c.name = '%CHAR_NAME%'; DELETE ca FROM character_aura ca INNER JOIN characters c ON c.guid = ca.guid WHERE c.name = '%CHAR_NAME%' AND ca.spell IN (71041, 71328); SELECT ROW_COUNT() AS deleted; SELECT c.guid, c.name, c.online, ca.spell FROM characters c LEFT JOIN character_aura ca ON ca.guid = c.guid AND ca.spell IN (71041, 71328) WHERE c.name = '%CHAR_NAME%';"

if errorlevel 1 (
    echo FAILED
    pause
    exit /b 1
)

echo Done. If the character was online, relog for it to take effect.
pause
