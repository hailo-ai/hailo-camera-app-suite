#!/bin/bash
rm -f ./video*
start_index=0
num_of_src=60
for ((n = $start_index; n < $num_of_src; n++)); do
    ln -s $TAPPAS_WORKSPACE/$1 ./video$n.mp4
done
