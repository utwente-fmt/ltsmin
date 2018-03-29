#!/bin/bash

grep "HAVE_SYLVAN_FALSE='#'" config.log > /dev/null
if [ $? -ne 0 ]; then
    >&2 echo "Build cache not successfully installed, please restart build."
    exit 1
fi

