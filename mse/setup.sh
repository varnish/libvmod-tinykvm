#!/bin/bash
MSEFOLDER=/usr/local/mse

mkdir -p $MSEFOLDER
mkdir -p $MSEFOLDER/sata
mkdir -p $MSEFOLDER/nand
mkdir -p $MSEFOLDER/optane
mkdir -p $MSEFOLDER/fourth

fallocate -l 16M $MSEFOLDER/sata/store.dat
fallocate -l 16M $MSEFOLDER/nand/store.dat
fallocate -l 16M $MSEFOLDER/optane/store.dat
fallocate -l 16M $MSEFOLDER/fourth/store.dat
