@echo off
chcp 936 >nul
setlocal EnableExtensions EnableDelayedExpansion

rem ==============================================================
rem   Keil 工程 -> GitHub 同步脚本
rem   流程：暂存提交本机改动 -> 拉取远端 -> 合并 -> 推送
rem   安全：全程不使用 push --force，不改写远端历史；
rem         与别人的改动冲突时立即停止，绝不覆盖远端文件
rem ==============================================================

set "REPO_DIR=D:\git-repositories"
set "REMOTE=origin"
set "BRANCH=main"

echo.
echo ==============================================================
echo   Keil 工程同步到 GitHub
echo   仓库目录 : %REPO_DIR%
echo   目标分支 : %REMOTE%/%BRANCH%
echo ==============================================================
echo.

pushd "%REPO_DIR%" 2>nul
if errorlevel 1 (
    echo [错误] 找不到仓库目录 "%REPO_DIR%"
    goto :END_FAIL
)

where git >nul 2>nul
if errorlevel 1 (
    echo [错误] 系统 PATH 中找不到 git
    goto :END_FAIL
)

if exist ".git\index.lock" (
    echo [错误] 检测到 .git\index.lock，上次 git 操作可能被强行中断
    echo        请确认没有其它 git 窗口在运行，删除该文件后重试
    goto :END_FAIL
)

if exist ".git\rebase-merge" goto :UNFINISHED
if exist ".git\rebase-apply" goto :UNFINISHED
if exist ".git\MERGE_HEAD" goto :UNFINISHED

git remote get-url %REMOTE% >nul 2>nul
if errorlevel 1 (
    echo [错误] 未配置远程仓库 %REMOTE%
    goto :END_FAIL
)

for /f "delims=" %%b in ('git rev-parse --abbrev-ref HEAD 2^>nul') do set "CUR_BRANCH=%%b"
if /i not "!CUR_BRANCH!"=="%BRANCH%" (
    echo [提示] 当前位于分支 !CUR_BRANCH!，切换到 %BRANCH%
    git checkout %BRANCH%
    if errorlevel 1 (
        echo [错误] 切换分支失败
        goto :END_FAIL
    )
)

echo ---- 1/4 收集并提交本机改动 ----
git add -A
if errorlevel 1 (
    echo [错误] 暂存改动失败
    goto :END_FAIL
)

git diff --cached --quiet
if errorlevel 1 (
    for /f "delims=" %%t in ('powershell -NoProfile -Command "Get-Date -Format \"yyyy-MM-dd HH:mm:ss\""') do set "TS=%%t"
    echo        检测到改动，正在提交 ...
    git commit -m "Keil 工程更新 !TS!" >nul
    if errorlevel 1 (
        echo [错误] 提交失败
        goto :END_FAIL
    )
    for /f %%c in ('git diff-tree --no-commit-id --name-only -r HEAD ^| find /c /v ""') do echo        已提交，涉及文件 %%c 个
) else (
    echo        没有新的改动需要提交
)

echo.
echo ---- 2/4 拉取远端最新内容 ----
git fetch %REMOTE% %BRANCH%
if errorlevel 1 (
    echo [错误] 拉取失败，通常是网络或代理不可用
    echo        本仓库代理配置：请确认 Clash 已启动并监听 127.0.0.1:7897
    goto :END_FAIL
)

set "BEHIND=0"
for /f "delims=" %%n in ('git rev-list --count HEAD..%REMOTE%/%BRANCH% 2^>nul') do set "BEHIND=%%n"
if not "!BEHIND!"=="0" (
    echo        远端比你多 !BEHIND! 个提交，正在合并 ...
    git rebase --autostash %REMOTE%/%BRANCH%
    if errorlevel 1 (
        echo.
        echo [冲突] 本机改动与远端改动冲突，已停止操作
        echo         远端仓库内容未被覆盖，你的本机改动也还在
        echo.
        echo         冲突文件：
        git diff --name-only --diff-filter=U
        echo.
        echo         处理方法：
        echo           放弃本次合并，回到同步前状态 : git rebase --abort
        echo           手动改好冲突文件后继续         : git add 文件名 然后 git rebase --continue
        goto :END_FAIL
    )
) else (
    echo        远端没有新内容
)

echo.
echo ---- 3/4 推送到 GitHub ----
git push %REMOTE% %BRANCH%
if errorlevel 1 (
    echo        推送被拒绝，可能有人刚提交，重新同步后再试一次 ...
    git fetch %REMOTE% %BRANCH%
    if errorlevel 1 (
        echo [错误] 拉取失败，放弃推送
        goto :END_FAIL
    )
    git rebase --autostash %REMOTE%/%BRANCH%
    if errorlevel 1 (
        echo [冲突] 重新合并时发生冲突，已停止，远端未被覆盖
        git diff --name-only --diff-filter=U
        echo         执行 git rebase --abort 可回到同步前状态
        goto :END_FAIL
    )
    git push %REMOTE% %BRANCH%
    if errorlevel 1 (
        echo [错误] 推送失败，请检查网络、代理或仓库权限
        goto :END_FAIL
    )
)

echo.
echo ---- 4/4 完成 ----
echo        远端地址 : 
git remote get-url %REMOTE%
echo        最近提交 :
git -c i18n.logOutputEncoding=GBK --no-pager log --oneline -3
echo        跟踪状态 :
git status -sb
echo.
echo [成功] 已同步到 GitHub
popd
echo.
pause
exit /b 0

:UNFINISHED
echo [错误] 仓库中存在未完成的合并或变基操作
echo        请先执行 git rebase --abort 或 git merge --abort 清理后再运行本脚本
goto :END_FAIL

:END_FAIL
echo.
echo [失败] 同步未完成，远端仓库内容没有被覆盖。
popd
echo.
pause
exit /b 1
