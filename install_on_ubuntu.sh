#!/bin/bash

# Update
sudo apt update -y
sudo apt upgrade -y

# Required dependencies when installing from github:
sudo apt install git -y
sudo apt install make -y
sudo apt install automake -y
sudo apt install libtool -y
sudo apt install flex -y
sudo apt install bison -y
sudo apt install pkgconf -y
sudo apt install cmake -y

# Required dependencies
sudo apt install libpopt-dev -y
sudo apt install zlib1g-dev -y
sudo apt install openjdk-11-jdk -y
sudo apt install ant -y

########################### install sylvan & sylvan dependencies #############################
#TODO: make sylvan a submodule
sudo apt install libgmp-dev -y
sudo apt install libhwloc-dev -y
git clone https://github.com/Codermann63/sylvan.git
cd sylvan
mkdir build
cd build
cmake ..
make
sudo make install
cd ../..

########################## PNML dependencies ##############################
sudo apt install xml2 -y
sudo apt install libxml2-dev -y

################################# install LTSMIN from github #################################
git clone https://github.com/utwente-fmt/ltsmin.git
cd ltsmin
git submodule update --init
./ltsminreconf
./configure
make
sudo make install
