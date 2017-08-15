#!/bin/bash
set -e
set -o xtrace

TAG_OR_BRANCH=${TRAVIS_TAG:-$TRAVIS_BRANCH}

# build the new webpage

# extract the HTML manpages from the source tarball
mkdir -p www/assets/man
tar -xzf ltsmin-$TAG_OR_BRANCH-source.tgz --wildcards --no-anchored 'doc/*.html'
cp ltsmin-$LTSMIN_VERSION/doc/*.html www/assets/man

# generate an index of all the manpages
ls www/assets/man | \
    perl -e 'print "<html><body><ul>"; while(<>) { chop $_; print "<li><a href=\"./$_\">$_</a></li>";} print "</ul></body></html>"' \
    > www/assets/man/index.html

# copy the README that will become the index.html page
cp README.md www

# copy the changelog
cp changelog.md www

# use jekyll to convert all the markdown pages in the www folder to HTML
pushd www
gem install jekyll bundler
bundle install
bundle exec jekyll b
popd

