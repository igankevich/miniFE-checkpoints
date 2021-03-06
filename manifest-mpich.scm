(packages->manifest
  (list
    (@ (gnu packages commencement) gcc-toolchain)
    (@ (gnu packages base) gnu-make)
    ;;(@ (stables packages mpi) openmpi-4.0.2)
    ;;(@ (gnu packages mpi) openmpi)
    (@ (gnu packages mpi) mpich)
    ;;(@ (stables packages mpi) mpich-3.4.2)
    (@ (gnu packages dmtcp) dmtcp) ;; checkpoints
    (@ (gnu packages compression) gzip) ;; for dmtcp
    (list (@ (gnu packages dns) isc-bind) "utils") ;; for dmtcp
    (@ (gnu packages time) time) ;; benchmarks
    (@ (gnu packages python) python-3) ;; benchmarks
    ))
