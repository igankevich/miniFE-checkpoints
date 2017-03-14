#ifndef _Vector_functions_hpp_
#define _Vector_functions_hpp_

//@HEADER
// ************************************************************************
// 
//               HPCCG: Simple Conjugate Gradient Benchmark Code
//                 Copyright (2006) Sandia Corporation
// 
// Under terms of Contract DE-AC04-94AL85000, there is a non-exclusive
// license for use of this work by or on behalf of the U.S. Government.
// 
// This library is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.
//  
// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//  
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
// USA
// Questions? Contact Michael A. Heroux (maherou@sandia.gov) 
// 
// ************************************************************************
//@HEADER

#include <vector>
#include <sstream>
#include <fstream>

#ifdef HAVE_MPI
#include <mpi.h>
#endif

#include <TypeTraits.hpp>
#include <Vector.hpp>

#include <qthread/qloop.h>
#include <qthread.h>
#include "qthreads_loop_type.h"

namespace miniFE {


template<typename VectorType>
void write_vector(const std::string& filename,
                  const VectorType& vec)
{
  int numprocs = 1, myproc = 0;
#ifdef HAVE_MPI
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
  MPI_Comm_rank(MPI_COMM_WORLD, &myproc);
#endif

  std::ostringstream osstr;
  osstr << filename << "." << numprocs << "." << myproc;
  std::string full_name = osstr.str();
  std::ofstream ofs(full_name.c_str());

  typedef typename VectorType::ScalarType ScalarType;

  const std::vector<ScalarType>& coefs = vec.coefs;
  for(int p=0; p<numprocs; ++p) {
    if (p == myproc) {
      if (p == 0) {
        ofs << vec.local_size << std::endl;
      }
  
      typename VectorType::GlobalOrdinalType first = vec.startIndex;
      for(size_t i=0; i<vec.local_size; ++i) {
        ofs << first+i << " " << coefs[i] << std::endl;
      }
    }
#ifdef HAVE_MPI
    MPI_Barrier(MPI_COMM_WORLD);
#endif
  }
}

template<typename VectorType>
void sum_into_vector(size_t num_indices,
                     const typename VectorType::GlobalOrdinalType* indices,
                     const typename VectorType::ScalarType* coefs,
                     VectorType& vec)
{
  typedef typename VectorType::GlobalOrdinalType GlobalOrdinal;
  typedef typename VectorType::ScalarType Scalar;

  GlobalOrdinal first = vec.startIndex;
  GlobalOrdinal last = first + vec.local_size - 1;

  std::vector<Scalar>& vec_coefs = vec.coefs;

  for(size_t i=0; i<num_indices; ++i) {
    if (indices[i] < first || indices[i] > last) continue;
    size_t idx = indices[i] - first;
//    vec_coefs[idx] += coefs[i];
    qthread_dincr(&vec_coefs[idx], coefs[i]);
  }
}

#ifdef MINIFE_HAVE_TBB
template<typename VectorType>
void sum_into_vector(size_t num_indices,
                     const typename VectorType::GlobalOrdinalType* indices,
                     const typename VectorType::ScalarType* coefs,
                     LockingVector<VectorType>& vec)
{
  vec.sum_in(num_indices, indices, coefs);
}
#endif

//------------------------------------------------------------
// Internal elemental function for qthreads
typedef struct {
	      MINIFE_SCALAR alpha;
              MINIFE_SCALAR beta;
	const MINIFE_SCALAR* x;
	const MINIFE_SCALAR* y;
	      MINIFE_SCALAR* w;
} waxpby_thread;

void waxpby_thread_func(const size_t start, const size_t stop, void* thr_args) {
	waxpby_thread* thread_args = (waxpby_thread*) thr_args;

	const MINIFE_SCALAR  alpha = thread_args->alpha;
	const MINIFE_SCALAR  beta  = thread_args->beta;
        const MINIFE_SCALAR* x     = thread_args->x;
	const MINIFE_SCALAR* y     = thread_args->y;
              MINIFE_SCALAR* w     = thread_args->w;

	for(size_t i = start; i < stop; i++) {
		w[i] = alpha * x[i] + beta * y[i];
	}
}

//------------------------------------------------------------
//Compute the update of a vector with the sum of two scaled vectors where:
//
// w = alpha*x + beta*y
//
// x,y - input vectors
//
// alpha,beta - scalars applied to x and y respectively
//
// w - output vector
//
template<typename VectorType>
void
  waxpby(typename VectorType::ScalarType alpha, const VectorType& x,
         typename VectorType::ScalarType beta, const VectorType& y,
         VectorType& w)
{
  typedef typename VectorType::ScalarType ScalarType;

#ifdef MINIFE_DEBUG
  if (y.local_size < x.local_size || w.local_size < x.local_size) {
    std::cerr << "miniFE::waxpby ERROR, y and w must be at least as long as x." << std::endl;
    return;
  }
#endif

  int n = x.coefs.size();
  const ScalarType* xcoefs = &x.coefs[0];
  const ScalarType* ycoefs = &y.coefs[0];
        ScalarType* wcoefs = &w.coefs[0];

  waxpby_thread thread_arguments = { alpha, beta, xcoefs, ycoefs, wcoefs };

  // hand control to qthreads
  QLOOP(0, n, waxpby_thread_func, &thread_arguments);
}

//Like waxpby above, except operates on two sets of arguments.
//In other words, performs two waxpby operations in one loop.
template<typename VectorType>
void
  fused_waxpby(typename VectorType::ScalarType alpha, const VectorType& x,
         typename VectorType::ScalarType beta, const VectorType& y,
         VectorType& w,
         typename VectorType::ScalarType alpha2, const VectorType& x2,
         typename VectorType::ScalarType beta2, const VectorType& y2,
         VectorType& w2)
{
  typedef typename VectorType::ScalarType ScalarType;

#ifdef MINIFE_DEBUG
  if (y.local_size < x.local_size || w.local_size < x.local_size) {
    std::cerr << "miniFE::waxpby ERROR, y and w must be at least as long as x." << std::endl;
    return;
  }
#endif

  int n = x.coefs.size();
  const ScalarType* xcoefs = &x.coefs[0];
  const ScalarType* ycoefs = &y.coefs[0];
        ScalarType* wcoefs = &w.coefs[0];

  const ScalarType* x2coefs = &x2.coefs[0];
  const ScalarType* y2coefs = &y2.coefs[0];
        ScalarType* w2coefs = &w2.coefs[0];

  for(int i=0; i<n; ++i) {
    wcoefs[i] = alpha*xcoefs[i] + beta*ycoefs[i];
    w2coefs[i] = alpha2*x2coefs[i] + beta2*y2coefs[i];
  }
}

typedef struct {
	const MINIFE_SCALAR* x;
	const MINIFE_SCALAR* y;
} dotprod_thread;

void dotprod_sum_thread_func(void* part_sum, const void* value) {
	*(MINIFE_SCALAR*) part_sum += *(MINIFE_SCALAR*) value;
}

void dotprod_thread_func(const size_t start, const size_t stop, void* thr_args, void* val_ret) {
	dotprod_thread* thread_args = (dotprod_thread*) thr_args;
	const MINIFE_SCALAR* x = thread_args->x;
	const MINIFE_SCALAR* y = thread_args->y;
	MINIFE_SCALAR sum = 0;

	for(size_t i = start; i < stop; i++) {
		sum += x[i] * y[i];
	}

	*(MINIFE_SCALAR*) val_ret = sum;
}

//-----------------------------------------------------------
//Compute the dot product of two vectors where:
//
// x,y - input vectors
//
// result - return-value
//
template<typename Vector>
typename TypeTraits<typename Vector::ScalarType>::magnitude_type
  dot(const Vector& x,
      const Vector& y)
{
  int n = x.coefs.size();

#ifdef MINIFE_DEBUG
  if (y.local_size < n) {
    std::cerr << "miniFE::dot ERROR, y must be at least as long as x."<<std::endl;
    n = y.local_size;
  }
#endif

  typedef typename Vector::ScalarType Scalar;
  typedef typename TypeTraits<typename Vector::ScalarType>::magnitude_type magnitude;

  const Scalar* xcoefs = &x.coefs[0];
  const Scalar* ycoefs = &y.coefs[0];
  magnitude result = 0;

  dotprod_thread thread_args = { xcoefs, ycoefs };

  // Pass control to qthreads
  qt_loopaccum_balance(0, n, sizeof(MINIFE_SCALAR), &result, dotprod_thread_func, 
	&thread_args, dotprod_sum_thread_func);

#ifdef HAVE_MPI
  magnitude local_dot = result, global_dot = 0;
  MPI_Datatype mpi_dtype = TypeTraits<magnitude>::mpi_type();  
  MPI_Allreduce(&local_dot, &global_dot, 1, mpi_dtype, MPI_SUM, MPI_COMM_WORLD);
  return global_dot;
#else
  return result;
#endif
}

}//namespace miniFE

#endif
