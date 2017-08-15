#!/bin/bash
set -e
set -o xtrace

if [ ! -f "$HOME/ProB/probcli" ]; then
    wget "$PROB_URL" -P /tmp
    tar -xf "/tmp/$PROB_NAME" -C "$HOME"
fi

