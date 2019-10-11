#!/bin/bash

PATH=$HOME
PLOT_PATH="$PATH/sdhm/benchmark/X-Mem/scripts_postprocessing/"
cd $PLOT_PATH

./plot_xmem.py seq r
./plot_xmem.py seq w
./plot_xmem.py rand r
./plot_xmem.py rand w

echo "DONE"
