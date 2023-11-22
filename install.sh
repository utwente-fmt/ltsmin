#!/bin/bash

# Update
sudo apt update
sudo apt upgrade

# Required installs from github:
sudo apt install git -y
sudo apt install make -y
sudo apt install automake -y
sudo apt install libtool -y
sudo apt install flex -y
sudo apt install bison -y
sudo apt install pkgconf -y
sudo apt install cmake -y

# 
sudo apt install libpopt-dev -y
sudo apt install zlib1g-dev -y
sudo apt install openjdk-11-jre -y
sudo apt install ant -y


########################### install sylvan   TODO OUTCOMMENT WHEN IT WORKS #############################
sudo apt install libgmp-dev -y
sudo apt install libhwloc-dev -y
git clone https://github.com/trolando/sylvan.git
cd sylvan
mkdir build
cd build
cmake ..
make
sudo make install
cd ../..

# spins? [OK]
# DVE 					TODO [OK - dve2lts-mc]
# PNML 					TODO [OK - pnml2lts-sym]
# pins2lts-sym [ok]
# pins2lts-mc [ok]
# sylvan 				[okish]
# ltl2ba [ok]

########################## PNML ##############################
sudo apt install xml2 -y
sudo apt install libxml2-dev -y




################################# LTSMIN  #################################


# Get ltsmin from github, configure and intall
git clone https://github.com/utwente-fmt/ltsmin.git
cd ltsmin
git submodule update --init
	# Hack to fix error in ltl2ba TODO: fix it in git-submodule
	#rm -r ./ltl2ba
	#git clone https://github.com/utwente-fmt/ltl2ba.git	
./ltsminreconf	

	########################## SPINS INITIALIZE  ##########################
	cd spins/src
	ant
	cd ../..

./configure --disable-dependency-tracking --with-spins
make
sudo make install