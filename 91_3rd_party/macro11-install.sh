#! /bin/bash

# get MACRO11 repository from hithub

git clone https://github.com/j-hoppe/MACRO11
# mail fail, if exists

cd MACRO11
git pull
cd src
make
echo Publish macro11 under 3rd/party/MACRO11
cp -f macro11 ..




