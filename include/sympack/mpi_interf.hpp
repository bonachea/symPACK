#ifndef _SYMPACK_MPI_HPP_
#define _SYMPACK_MPI_HPP_

#include <iostream>

#include  "sympack/Environment.hpp"
#include  "sympack/CommTypes.hpp"

#include <mpi.h>

namespace SYMPACK{

  /// @namespace mpi
  ///
  /// @brief Interface with MPI to facilitate communication.
  namespace mpi{

    inline void
      Gatherv ( 
          Icomm& localIcomm, 
          Icomm& allIcomm,
          Int root,
          MPI_Comm          comm )
      {
        Int mpirank, mpisize;
        MPI_Comm_rank( comm, &mpirank );
        MPI_Comm_size( comm, &mpisize );

        Int localSize = localIcomm.size();
        SYMPACK::vector<Int>  localSizeVec( mpisize );
        SYMPACK::vector<Int>  localSizeDispls( mpisize );
        MPI_Gather( &localSize, sizeof(Int), MPI_BYTE, &localSizeVec[0], sizeof(Int), MPI_BYTE,root, comm );
        localSizeDispls[0] = 0;
        for( Int ip = 1; ip < mpisize; ip++ ){
          localSizeDispls[ip] = (localSizeDispls[ip-1] + localSizeVec[ip-1]);
        }
        Int totalSize = (localSizeDispls[mpisize-1] + localSizeVec[mpisize-1]);

        allIcomm.clear();
        allIcomm.resize( totalSize );

        MPI_Gatherv( localIcomm.front(), localSize, MPI_BYTE, allIcomm.front(), 
            &localSizeVec[0], &localSizeDispls[0], MPI_BYTE,root, comm	);

        //Mark the Icomm as "full"
        allIcomm.setHead(totalSize);
        return ;
      };		// -----  end of function Gatherv  ----- 



template<typename T>
void Allreduce( T* sendbuf, T* recvbuf, Int count, MPI_Op op, MPI_Comm comm){};

template<>
void Allreduce<double>( double* sendbuf, double* recvbuf, Int count, MPI_Op op, MPI_Comm comm){
      MPI_Allreduce( sendbuf, recvbuf, count, MPI_DOUBLE, op, comm );
}

template<>
void Allreduce<float>( float* sendbuf, float* recvbuf, Int count, MPI_Op op, MPI_Comm comm){
      MPI_Allreduce( sendbuf, recvbuf, count, MPI_FLOAT, op, comm );
}

template<>
void Allreduce<std::complex<float> >( std::complex<float>* sendbuf, std::complex<float>* recvbuf, Int count, MPI_Op op, MPI_Comm comm){
      MPI_Allreduce( sendbuf, recvbuf, count, MPI_COMPLEX, op, comm );
}

template<>
void Allreduce<std::complex<double> >( std::complex<double>* sendbuf, std::complex<double>* recvbuf, Int count, MPI_Op op, MPI_Comm comm){
      MPI_Allreduce( sendbuf, recvbuf, count, MPI_DOUBLE_COMPLEX, op, comm );
}

  }
}



#endif

