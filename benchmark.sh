#!/bin/sh

mpi=openmpi
hosts="-f"
if test "$mpi" = "openmpi"
then
    #hosts="--prefix /gnu/store/ba2p8bpz3y701vnifafm7gf3gbprwvzl-openmpi-4.0.3 -machinefile"
    hosts="-machinefile"
fi

profile() {
    time --format='%e' --append --output $output "$@"
}

profile2() {
    time --format='%e' --append --output ${output}2 "$@"
}

set -e

# update dependencies
if test -f manifest-$mpi.scm
then
    manifest=$(realpath manifest-$mpi.scm)
else
    manifest=$(realpath manifest.scm)
fi
nodes=$(scontrol show hostnames "$SLURM_JOB_NODELIST")
for i in $nodes
do
    ssh $i "guix environment --manifest=$manifest --search-paths" &
done
wait
eval $(guix environment --manifest=$manifest --search-paths)

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
how=no
nx=300
ny=300
nz=300
nprocs=$(expr $SLURM_JOB_NUM_NODES \* 16)
args="-nx $nx -ny $ny -nz $nz"
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
        profile mpiexec -n $nprocs $hosts $PWD/hosts $PWD/miniFE.x $args
        checkpoints=$(find . -name '*.checkpoint' | sort)
        for i in $checkpoints
        do
            export MPI_CHECKPOINT=$i
            echo -n "$nx,$ny,$nz,$nprocs,$(stat --format='%s' $i)," >> $output
            profile mpiexec -n $nprocs $hosts $PWD/hosts $PWD/miniFE.x $args
        done
        ;;
    dmtcp)
        export MPI_CHECKPOINT=dmtcp
        pkill -f dmtcp || true
        dmtcp_coordinator --exit-on-last --daemon
        profile dmtcp_launch --no-gzip --join-coordinator --coord-host $(hostname) mpiexec -n $nprocs $hosts $PWD/hosts $PWD/miniFE.x $args
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
output_dir=$root/output/miniFE/$how/$mpi
mkdir -p $output_dir
column -t -s, $output
mv -v $output $output_dir/$(date +%s).csv
