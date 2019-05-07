#!/bin/bash -e

function win32_format_path {
    local formatted_path=$1
    local host_win32_wsl=0
    local host_win32_cygwin=0

    host_uname="$(uname -a)"
    case "$host_uname" in
        *Microsoft*)
            host_win32_wsl=1
            ;;
        CYGWIN*)
            host_win32_cygwin=1
            ;;
	esac

    if  [[ $host_win32_wsl = 1 ]] && [[ $1 == "/mnt/"* ]]; then
        formatted_path="$(wslpath -a -w "$1")"
    elif [[ $host_win32_cygwin = 1 ]] && [[ $1 == "/cygdrive/"* ]]; then
        formatted_path="$(cygpath -a -w "$1")"
    fi

    echo "$formatted_path"
}

echo "ENVIRONMENT:"
env

RUN_JENKINS_WINDOWS_SCRIPT_PATH=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
RUN_JENKINS_WINDOWS_SCRIPT_PATH_WINDOWS=$(win32_format_path "$RUN_JENKINS_WINDOWS_SCRIPT_PATH/run-jenkins-windows.bat")

WINDOWS_CMD=$(which cmd.exe)
if [ ! -f $WINDOWS_CMD ]; then
    WINDOWS_CMD=$WINDIR/System32/cmd.exe
fi

"$WINDOWS_CMD" /c "$RUN_JENKINS_WINDOWS_SCRIPT_PATH_WINDOWS"

GIT_COMMIT=$(git rev-parse HEAD)
tar cvzf llvm-llvmwin64-msvc-$GIT_COMMIT-Windows.tar.gz -C $RUN_JENKINS_WINDOWS_SCRIPT_PATH/out/llvm-llvmwin64-msvc .
