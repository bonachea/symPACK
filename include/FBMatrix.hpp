/// @file FBMatrix.hpp
/// @brief Matrix structure and methods for the FAN-BOTH method.
/// @author Mathias Jacquelin
/// @date 2014-01-21
#ifndef _FBMATRIX_HPP_ 
#define _FBMATRIX_HPP_

#include "Environment.hpp"
#include "NumVec.hpp"
#include "NumMat.hpp"

#include <vector>

namespace LIBCHOLESKY{


  class FBMatrix;

  void Update_Async(upcxx::global_ptr<FBMatrix> Aptr, Int j, upcxx::global_ptr<double> remoteFactorPtr);
  void Aggregate_Async(upcxx::global_ptr<FBMatrix> Aptr, Int j, global_ptr<double> remoteAggregatePtr);
  void Factor_Async(upcxx::global_ptr<FBMatrix> Aptr, Int j);
  void Gathercopy(upcxx::global_ptr<FBMatrix> Objptr, global_ptr<double> Adest, Int j);



  class FBMatrix{
    public:



      //  DblNumMat W;
      //  DblNumMat Achunk;
      std::vector<DblNumMat> AchunkLower;
      std::vector<DblNumMat> WLower;
      std::vector< upcxx::global_ptr<FBMatrix> > RemoteObjPtrs;
  
      //lock should be initialized with the number of contribution a block column is receiving
      IntNumVec AggLock;

      Int n;
      Int blksize;
      Int pcol, prow;
      Int np, iam;

      FBMatrix();
      void Initialize(upcxx::shared_array<upcxx::global_ptr<FBMatrix> > * RemoteObjPtr);

      ~FBMatrix();


      inline Int row2D(Int i, Int j) {return (i/blksize)%np;}
      inline Int col2D(Int i, Int j) {return (j/blksize)%np;}

      inline Int chevron2D(Int i, Int j) {return (min(i,j)/blksize)%np;}
      inline Int antichevron2D(Int i, Int j) {return (max(i,j)/blksize)%np;}

      inline Int diag2D(Int i, Int j) {return (abs(i-j)/blksize)%np;}
      inline Int antidiag2D(Int i, Int j) {return ((i+j)/blksize)%np;}


      inline Int modwrap2D(Int i, Int j) {return min(i/blksize,j/blksize)%prow + prow*floor((double)(max(i/blksize,j/blksize)%np)/(double)prow);}

      //inline Int MAP(Int i, Int j) { return chevron2D(i,j);}
//      inline Int MAP(Int i, Int j) { return modwrap2D(i,j);}
//      inline Int MAP(Int i, Int j) { return col2D(i,j);}
      inline Int MAP(Int i, Int j) { return row2D(i,j);}

      inline Int global_col_to_local(Int j){ return ((j)/(pcol*blksize))*blksize; }

      void Allocate(Int pn, Int pblksize);

      void Distribute( DblNumMat & Aorig);

      void Gather( DblNumMat & Adest);

      void Aggregate(Int j, DblNumMat &DistW);

      void Factor(Int j);

      void Update(Int j, Int i, DblNumMat & Factor);

      void WaitFactorization();

      void NumericalFactorization();

  };




}

#endif