#!/bin/bash

# Required: bzr, wget, tar, Python (tested with 2.7), py-mpi4pi, boost, swig, gcc

#extra search path for libraries in OSX, ignore on other OSs
OSX="/opt/local"
REMOVE=0

set -e

BASEDIR=`pwd`
OPAAL_DIR="$BASEDIR/opaal/"
INSTALL_DIR="$BASEDIR/opaal/install/"



#Install opaal
bzr branch lp:opaal



#Install DBM
wget http://mcc.uppaal.org/opaal/UPPAAL-dbm-2.0.8-opaal3.tar.gz
tar -xpzf UPPAAL-dbm-2.0.8-opaal3.tar.gz

cd UPPAAL-dbm-2.0.8-opaal3/modules
DIR=`pwd`
# THE FOLLOWING BREAKS IF MULTIPLE COMPILERS ARE AVAILABLE:
echo "$DIR" > input
echo "$INSTALL_DIR" >> input
echo "2" >> input
echo "6 11" >> input
./setup.sh < input
#pushd "build/$INSTALL_DIR" > /dev/null
make libs
make install
#popd > /dev/null
cd ../..

rm UPPAAL-dbm-2.0.8-opaal3.tar.gz
if [ "$REMOVE" == "1" ]; then
    rm -rf UPPAAL-dbm-2.0.8-opaal3
fi



#install python dbm bindings 
bzr branch lp:pydbm
cd pydbm
#setting the include and lib dir did not work with scientific linux + epd. The line below fixes this.
sed -i -e "s/\/usr\/local/${INSTALL_DIR//\//\\/}/g" setup.py
if [ "`uname`" == "Darwin" ]; then
    sed -i -e 's/"-fpermissive"]/"-fpermissive","-stdlib=libstdc++"],\
            extra_link_args=["-stdlib=libstdc++"]/g' setup.py
fi
python ./setup.py build_ext -I$INSTALL_DIR/uppaal/include -L$INSTALL_DIR/uppaal/lib
python ./setup.py install --prefix $INSTALL_DIR/pydbm/

cd ..
if [ "$REMOVE" == "1" ]; then
    rm -rf pydbm
fi



#Install pyuppall
bzr branch lp:pyuppaal "$INSTALL_DIR/pyuppaal"



echo -e "\n\n\n\n
Please add the following to your .bashrc:
export PYTHONPATH=$OPAAL_DIR:$INSTALL_DIR/pyuppaal/:$INSTALL_DIR/pydbm/lib/python2.7/site-packages/:.:\$PYTHONPATH

If the LTSmin binaries are installed (in your path), Opaal can start LTSmin as follows:
opaal/bin/opaal_ltsmin tests/vikingtests/viking4.xml
"
