#!/bin/sh

profile() {
    time --format='%e' --append --output $output "$@"
}

profile2() {
    time --format='%e' --append --output ${output}2 "$@"
}

set -e

# update dependencies
manifest=$(realpath manifest.scm)
eval $(guix environment --manifest=$manifest --search-paths)
nodes=$(scontrol show hostnames "$SLURM_JOB_NODELIST")
for i in $nodes
do
    ssh $i "guix environment --manifest=$manifest --search-paths" &
done
wait

# build
root=$(pwd)
cd ref/src
make clean
make -j
export MPI_CHECKPOINT_CONFIG=config.tmp
cat > $MPI_CHECKPOINT_CONFIG << EOF
checkpoint-interval = 0
verbose = 1
EOF
rm -f *.checkpoint *.yaml *.dmtcp dmtcp*sh

# run
nx=300
ny=300
nz=300
nprocs=$(expr $SLURM_JOB_NUM_NODES \* 16)
args="-nx $nx -ny $ny -nz $nz"
how=dmtcp
output=$(mktemp)
echo -n "$nx,$ny,$nz,$nprocs,," >> $output
export HYDRA_IFACE=enp6s0
export UCX_NET_DEVICES=enp6s0
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
            echo -n "$nx,$ny,$nz,$nprocs,$(stat --format='%s' $i)," >> $output
            profile mpiexec -n $nprocs $PWD/miniFE.x $args
        done
        ;;
    dmtcp)
        export MPI_CHECKPOINT=dmtcp
        pkill -f dmtcp || true
        dmtcp_coordinator --exit-on-last --daemon
        profile dmtcp_launch --join-coordinator --coord-host $(hostname) mpiexec -n $nprocs -f $PWD/hosts $PWD/miniFE.x $args
        echo RESTORE
        pkill -f dmtcp || true
        sleep 1
        dmtcp_coordinator --exit-on-last --daemon
        cat $output >> ${output}2
        echo -n "$nx,$ny,$nz,$nprocs,$(stat --format='%s' *.dmtcp | awk '{s+=$1} END {print s}')," >> ${output}2
        sed -i -e 's/ibrun_path=.*/ibrun_path=garbage/' dmtcp_restart_script.sh
        sed -i -e 's:/usr/bin/::g' dmtcp_restart_script.sh
        set +e
        profile2 sh -c './dmtcp_restart_script.sh || true'
        set -e
        cat ${output}2 > $output
        rm ${output}2
        ;;
esac
output_dir=$root/output/miniFE/$how
mkdir -p $output_dir
column -t -s, $output
mv -v $output $output_dir/$(date +%s).csv
