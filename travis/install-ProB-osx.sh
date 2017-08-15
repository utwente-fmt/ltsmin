#!/bin/bash
set -e
set -o xtrace
export PROB_NAME="ProB.mac_os.x86_64.tar.gz"
export PROB_URL="https://raw.githubusercontent.com/utwente-fmt/ltsmin-travis/master/osx/$PROB_NAME"

travis/install-ProB-generic.sh

