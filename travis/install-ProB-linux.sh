#!/bin/bash
set -e
set -o xtrace
export PROB_NAME="ProB.linux64.tar.gz"
export PROB_URL="https://raw.githubusercontent.com/utwente-fmt/ltsmin-travis/master/linux/$PROB_NAME"

travis/install-ProB-generic.sh

