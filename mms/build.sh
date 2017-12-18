#!/bin/bash
cd libmms-0.6.4
./autogen.sh
./configure
make
cp src/.libs/libmms.so.0.0.2 ../
