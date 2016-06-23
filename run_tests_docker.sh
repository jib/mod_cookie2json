#!/bin/bash

# this script leaves the node and apache2 processes running,
# in a non-Docker/container environment, those would need to
# be cleaned up.

# in support of our docker config
if [ -f ~/.profile ]; then
    . ~/.profile
fi
perl build.pl
./test/run_backend.sh &
./test/run_httpd.sh &
sleep 1
perl test/*t
