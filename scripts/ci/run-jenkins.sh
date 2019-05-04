#!/bin/bash -e

RUN_JENKINS_SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

host_uname="$(uname)"
case "$host_uname" in
    CYGWIN*)
        $RUN_JENKINS_SCRIPT_DIR/run-jenkins-windows.sh
        ;;
    Linux)
        host_uname="$(uname -a)"
        case "$host_uname" in
            *Microsoft*)
                $RUN_JENKINS_SCRIPT_DIR/run-jenkins-windows.sh
                ;;
            *)
                $RUN_JENKINS_SCRIPT_DIR/run-jenkins-linux.sh
                ;;
        esac
        ;;
    Darwin)
        $RUN_JENKINS_SCRIPT_DIR/run-jenkins-osx.sh
        ;;
esac
