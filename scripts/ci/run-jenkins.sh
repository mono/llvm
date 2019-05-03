#!/bin/bash -e

host_uname="$(uname)"
case "$host_uname" in
    CYGWIN*)
        ./run-jenkins-windows.sh
        ;;
    Linux)
        host_uname="$(uname -a)"
        case "$host_uname" in
            *Microsoft*)
                ./run-jenkins-windows.sh
                ;;
            *)
                ./run-jenkins-linux.sh
                ;;
        esac
        ;;
    Darwin)
        ./run-jenkins-osx.sh
        ;;
esac
