/// @file sparse_matrix_decl.hpp
/// @brief Sparse matrix and Distributed sparse matrix in compressed
/// column format.
/// @author Lin Lin
/// @date 2012-11-10
#ifndef _DIST_SPARSE_MATRIX_DECL_HPP_
#define _DIST_SPARSE_MATRIX_DECL_HPP_

#include "ngchol/Environment.hpp"
#include "ngchol/NumVec.hpp"
#include "ngchol/ETree.hpp"
#include "ngchol/SparseMatrixStructure.hpp"

#include <mpi.h>

//extern "C" {
//#include <bebop/util/config.h>
//#include <bebop/smc/sparse_matrix.h>
//#include <bebop/smc/csr_matrix.h>
//#include <bebop/smc/csc_matrix.h>
//#include <bebop/smc/sparse_matrix_ops.h>
//
//#include <bebop/util/get_options.h>
//#include <bebop/util/init.h>
//#include <bebop/util/malloc.h>
//#include <bebop/util/timer.h>
//#include <bebop/util/util.h>
//}





namespace LIBCHOLESKY{


template <typename T> class SupernodalMatrix;
template <typename T> class SupernodalMatrix2;



/// @class DistSparseMatrix
///
/// @brief DistSparseMatrix describes a Sparse matrix in the compressed
/// sparse column format (CSC) and distributed with column major partition. 
///
/// Note
/// ----
/// 
/// Since in PEXSI and PPEXSI only symmetric matrix is considered, the
/// compressed sparse row format will also be represented by the
/// compressed sparse column format.
///
/// TODO Add the parameter of numColLocal
template <typename F> class DistSparseMatrix{
  friend class SupernodalMatrix<F>;
  friend class SupernodalMatrix2<F>;
  //friend functions
  friend void ReadDistSparseMatrixFormatted ( const char* filename, DistSparseMatrix<Real>& pspmat, MPI_Comm comm );
  friend void ReadDistSparseMatrix ( const char* filename, DistSparseMatrix<Real>& pspmat, MPI_Comm comm );
  friend void ParaWriteDistSparseMatrix ( const char* filename, DistSparseMatrix<Real>& pspmat, MPI_Comm comm );
  friend void ParaReadDistSparseMatrix ( const char* filename, DistSparseMatrix<Real>& pspmat, MPI_Comm comm );

  friend void ReadDistSparseMatrixFormatted ( const char* filename, DistSparseMatrix<Complex>& pspmat, MPI_Comm comm );
  friend void ReadDistSparseMatrix ( const char* filename, DistSparseMatrix<Complex>& pspmat, MPI_Comm comm );
  friend void ParaWriteDistSparseMatrix ( const char* filename, DistSparseMatrix<Complex>& pspmat, MPI_Comm comm );
  friend void ParaReadDistSparseMatrix ( const char* filename, DistSparseMatrix<Complex>& pspmat, MPI_Comm comm );
  protected:
  bool globalAllocated;
  SparseMatrixStructure Local_;
  SparseMatrixStructure Global_;


  public:
	/// @brief Matrix dimension.
	Int          size;

	/// @brief Total number of nonzeros elements.
	//Idx64          nnz;                             
	Int          nnz;                             

	/// @brief Dimension nnzLocal, storing the nonzero values.
	NumVec<F,Int>    nzvalLocal;                      

	/// @brief MPI communicator
	MPI_Comm     comm;        


  DistSparseMatrix(MPI_Comm aComm){globalAllocated=false; comm = aComm;};
  void CopyData(const int n, const int nnz, const int * colptr, const int * rowidx, const F * nzval,bool onebased=false);
  DistSparseMatrix(const int n, const int nnz, const int * colptr, const int * rowidx, const F * nzval , MPI_Comm oComm);
  
  template <typename T> void ConvertData(const int n, const int nnz, const int * colptr, const int * rowidx, const T * nzval,bool onebased=false);

  SparseMatrixStructure  GetGlobalStructure();
  SparseMatrixStructure  GetLocalStructure() const;
  //const SparseMatrixStructure & GetLocalStructure() const;

};

// Commonly used
typedef DistSparseMatrix<Real>       DblDistSparseMatrix;
typedef DistSparseMatrix<Complex>    CpxDistSparseMatrix;



} // namespace LIBCHOLESKY

#include "ngchol/DistSparseMatrix_impl.hpp"

#endif // _DIST_SPARSE_MATRIX_DECL_HPP_
