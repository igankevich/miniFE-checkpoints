This repository contains the implementation of _application-level_ checkpoint
and restart for modified miniFE benchmark. This implementation was tested and
is known to produce the same residual when the application is restarted from
the checkpoint. Checkpoints are implemented only for the reference version of
the benchmark. Check the git log for all the modifications and programming
effort that was needed to reach the goal.

Implementation: `ref/src/mpi_checkpoint.c`
Benchmark script: `benchmark.sh`
