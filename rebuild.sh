#!/bin/bash
export HOME='/home/gtwang'
rm -r build &&
mkdir build &&
cd build &&
cmake .. &&
make