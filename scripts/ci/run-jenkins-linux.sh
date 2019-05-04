#!/bin/bash -e

RUN_JENKINS_LINUX_SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

echo "ENVIRONMENT:"
env

$RUN_JENKINS_LINUX_SCRIPT_DIR/package.sh
