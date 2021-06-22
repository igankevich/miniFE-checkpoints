#!/bin/sh

profile() {
    time --format='%e' --append --output $output "$@"
}

set -e
root=$(pwd)
cd ref/src
make clean
make -j
export MPI_CHECKPOINT_CONFIG=/tmp/config
cat > $MPI_CHECKPOINT_CONFIG << EOF
checkpoint-interval = 0
verbose = 1
EOF
rm -f *.checkpoint

nx=100
ny=100
nz=100
nprocs=12
args="-nx $nx -ny $ny -nz $nz"
how=mpi
output=$(mktemp)
echo -n "$nx,$ny,$nz,$nprocs," >> $output
case "$how" in
    no)
        export MPI_NO_CHECKPOINT=1
        profile mpiexec -n $nprocs $PWD/miniFE.x $args
        ;;
    mpi)
        profile mpiexec -n $nprocs $PWD/miniFE.x $args
        checkpoints=$(find . -name '*.checkpoint' | sort)
        for i in $checkpoints
        do
            export MPI_CHECKPOINT=$i
            echo -n "$nx,$ny,$nz,$nprocs," >> $output
            profile mpiexec -n $nprocs $PWD/miniFE.x $args
        done
        ;;
    dmtcp)
        ;;
esac
output_dir=$root/output/miniFE/$how
mkdir -p $output_dir
column -t -s, $output
mv -v $output $output_dir/$(date +%s).csv
