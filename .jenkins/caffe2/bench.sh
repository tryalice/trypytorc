#!/bin/bash

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

# Anywhere except $ROOT_DIR should work. This is so the python import doesn't
# get confused by any 'caffe2' directory in cwd
cd "$INSTALL_PREFIX"

if [[ $BUILD_ENVIRONMENT == *-cuda* ]]; then
    num_gpus=$(nvidia-smi -L | wc -l)
elif [[ $BUILD_ENVIRONMENT == *-rocm* ]]; then
    num_gpus=$(rocminfo | grep 'Device Type.*GPU' | wc -l)
else
    num_gpus=0
fi

caffe2_pypath="$(cd /usr && python -c 'import os; import caffe2; print(os.path.dirname(os.path.realpath(caffe2.__file__)))')"
cmd="$PYTHON $caffe2_pypath/python/examples/resnet50_trainer.py --train_data null --batch_size 64 --epoch_size 6400 --num_epochs 2"
if (( $num_gpus == 0 )); then
    cmd="$cmd --use_cpu"
else
    cmd="$cmd --num_gpus 1"
fi

eval "$cmd"
