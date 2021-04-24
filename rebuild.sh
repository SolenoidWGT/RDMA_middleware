#!/bin/bash
export HOME='/home/gtwang'
sudo rm -r build &&
mkdir build &&
cd build &&
cmake .. &&
make