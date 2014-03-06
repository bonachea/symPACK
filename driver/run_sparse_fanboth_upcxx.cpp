/// @file run_sparse_fanboth_upcxx.cpp
/// @brief Test for the interface of SuperLU_DIST and SelInv.
/// @author Mathias Jacquelin
/// @date 2014-01-23
//#define _DEBUG_


#include <upcxx.h>

#include <time.h>
#include <random>
#include <omp.h>

#include  "Environment.hpp"
#include  "DistSparseMatrix.hpp"
#include  "NumVec.hpp"
#include  "NumMat.hpp"
#include  "utility.hpp"
#include  "ETree.hpp"
#include  "blas.hpp"
#include  "lapack.hpp"
#include  "FBMatrix_upcxx.hpp"
#include  "NZBlock.hpp"
#include  "SuperNode.hpp"
#include  "SupernodalMatrix.hpp"

#include <async.h>
#include  "LogFile.hpp"
#include  "upcxx_additions.hpp"


extern "C" {
#include <bebop/util/config.h>
#include <bebop/smc/sparse_matrix.h>
#include <bebop/smc/csr_matrix.h>
#include <bebop/smc/csc_matrix.h>
#include <bebop/smc/sparse_matrix_ops.h>

#include <bebop/util/get_options.h>
#include <bebop/util/init.h>
#include <bebop/util/malloc.h>
#include <bebop/util/timer.h>
#include <bebop/util/util.h>
}




#ifdef USE_TAU
#include "TAU.h"
#elif defined (PROFILE) || defined(PMPI)
#define TAU
#include "timer.hpp"
#endif



#ifdef USE_TAU 
#define TIMER_START(a) TAU_START(TOSTRING(a));
#define TIMER_STOP(a) TAU_STOP(TOSTRING(a));
#elif defined (PROFILE)
#define TIMER_START(a) TAU_FSTART(a);
#define TIMER_STOP(a) TAU_FSTOP(a);
#else
#define TIMER_START(a)
#define TIMER_STOP(a)
#endif

using namespace LIBCHOLESKY;
using namespace std;





int main(int argc, char **argv) 
{
  upcxx::init(&argc,&argv);
//  MPI_Init(&argc,&argv);


#if defined(PROFILE) || defined(PMPI)
  TAU_PROFILE_INIT(argc, argv);
#endif


  int np = THREADS;
  int iam = MYTHREAD;

  try{


    logfileptr = new LogFile(iam);

    logfileptr->OFS()<<"********* LOGFILE OF P"<<iam<<" *********"<<endl;
    logfileptr->OFS()<<"**********************************"<<endl;



    // *********************************************************************
    // Input parameter
    // *********************************************************************
    std::map<std::string,std::string> options;

    OptionsCreate(argc, argv, options);

    std::string filename;
    if( options.find("-in") != options.end() ){
      filename= options["-in"];
    }
    std::string informatstr;
    if( options.find("-inf") != options.end() ){
      informatstr= options["-inf"];
    }
    Int blksize = 1;
    if( options.find("-b") != options.end() ){
      blksize= atoi(options["-b"].c_str());
    }


    upcxx::shared_array<upcxx::global_ptr<FBMatrix_upcxx> > Aobjects;
    Aobjects.init(np);

    upcxx::global_ptr<FBMatrix_upcxx> AfactGptr = upcxx::Create<FBMatrix_upcxx>();
    FBMatrix_upcxx * Afactptr = AfactGptr;

    Aobjects[iam] = AfactGptr;
    upcxx::barrier();
    upcxx::wait();
  

    //upcxx::global_ptr<FBMatrix> Afactptr = upcxx::Create<FBMatrix>();

    Afactptr->Initialize(&Aobjects);
    Afactptr->pcol = np;
    Afactptr->np = np;
    Afactptr->prow = sqrt(np);
    Afactptr->blksize = 1;

    Real timeSta, timeEnd;

      DblNumMat RHS;
      DblNumMat XTrue;

    //Read the input matrix
    sparse_matrix_file_format_t informat;
    informat = sparse_matrix_file_format_string_to_enum (informatstr.c_str());
    sparse_matrix_t* Atmp = load_sparse_matrix (informat, filename.c_str());
    //sparse_matrix_expand_symmetric_storage (Atmp);
    sparse_matrix_convert (Atmp, CSC);

    const csc_matrix_t * cscptr = (const csc_matrix_t *) Atmp->repr;
    DistSparseMatrix<Real> HMat(cscptr);

    Afactptr->n = HMat.size;
    if(MYTHREAD==0){ cout<<"Matrix order is "<<Afactptr->n<<endl; }


    destroy_sparse_matrix (Atmp);


    //do the symbolic factorization

//{
//
//    IntNumVec rowind, colptr;
//    HMat.Global_.ExpandSymmetric(colptr,rowind);
//
//    logfileptr->OFS()<<"expanded colptr: "<<colptr<<endl;
//    logfileptr->OFS()<<"original colptr: "<<HMat.Global_.colptr<<endl;
//
//    logfileptr->OFS()<<"expanded rowind: "<<rowind<<endl;
//    logfileptr->OFS()<<"original rowind: "<<HMat.Global_.rowind<<endl;
//
//}



//     ETree elimTree;
//     HMat.ConstructETree(elimTree);
////     logfileptr->OFS()<<"etree "<<elimTree<<std::endl;
//
//     upcxx::barrier();
//     elimTree.PostOrderTree();
////     logfileptr->OFS()<<"po etree"<<elimTree<<std::endl;
//     upcxx::barrier();
//
//     IntNumVec cc,rc;
//     HMat.GetLColRowCount(elimTree,cc,rc);
//     logfileptr->OFS()<<"Col count "<<cc<<std::endl;
//     logfileptr->OFS()<<"Row count "<<rc<<std::endl;
//
//
//
//    IntNumVec xsuper; 
//    HMat.FindSupernodes(elimTree,cc,xsuper);
//    logfileptr->OFS()<<"supernode indexes "<<xsuper<<std::endl;
//
//
//
//
//
//
//
//
//
//
//
//    IntNumVec xlindx,lindx,xlnz;
//    DblNumVec lnz;
//    HMat.SymbolicFactorization(elimTree,cc,xsuper,xlindx,xlnz,lindx,lnz);
// 
//    {
//      logfileptr->OFS()<<"xlindx contains the starting index in lindx of each supernode"<<std::endl; 
//      logfileptr->OFS()<<"xlindx"<<xlindx<<std::endl;
//      logfileptr->OFS()<<"lindx contains consecutive row indices of all supernodes"<<std::endl; 
////      for(int i =0;i<lindx.m();++i){logfileptr->OFS()<<i+1<<" ";} logfileptr->OFS()<<std::endl;
//      logfileptr->OFS()<<"lindx"<<lindx<<std::endl;
//      //TODO XLNZ might be discarded and we should use a first row to last row loop instead...
//      logfileptr->OFS()<<"xlnz contains the index of the first nz in col i"<<std::endl; 
////      logfileptr->OFS()<<"xlnz"<<xlnz<<std::endl;
////      logfileptr->OFS()<<"lnz contains the consecutive nz values of all supernodes"<<std::endl; 
//    }
//
//    //TODO: Create a Supernode structure and then use it to do a conversion from DistSparseMatrix to DistSupernodalMatrix
//
//
//
//
//
//
//
//
//
//
///*
//    //Filling L with A and distribute according to supernodal partition
//    //parsing the data structure
//    for(Int I=1;I<xsuper.m();I++){
//      Int fc = xsuper(I-1);
//      Int lc = xsuper(I)-1;
//      Int fi = xlindx(I-1);
//
//      logfileptr->OFS()<<"Supernode "<<I<<" in ["<<fc<<" ... "<<lc<<"]"<<std::endl;
//
//
//      Int iDest = Afactptr->MAP(I-1,I-1);
//
//      for(Int i = fc;i<=lc;i++){
//        Int first_nz = xlnz(i-1);
//        Int last_nz = xlnz(i)-1;
//
//        //who owns column i ?
//
//        Int numColFirst = HMat.size / np;
//        Int iOwner = std::min((i-1)/numColFirst,np-1);
//
//#ifdef _DEBUG_
//        logfileptr->OFS()<<"Column "<<i<<" is owned by P"<<iOwner<<" and should go on P"<<iDest<<std::endl;
//#endif
//
//        if(iam == iDest){
//          //Get column i from A
//          if(iOwner!=iam){
//            //Compute buffer size
//            Int nrows = HMat.Global_.colptr(i) - HMat.Global_.colptr(i-1);
//            DblNumVec nzcol(nrows);
//            MPI_Recv(nzcol.Data(),nrows*sizeof(double),MPI_BYTE,iOwner,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
//#ifdef _DEBUG_
//            logfileptr->OFS()<<"Received col "<<i<<" from P"<<iOwner<<std::endl;
//#endif
//            //copy A into L
//            Int gcolptr = HMat.Global_.colptr(i-1);
//            Int rowind = HMat.Global_.rowind(gcolptr-1);
//#ifdef _DEBUG_
//            logfileptr->OFS()<<"Column "<<i<<" is remote; gcolptr = "<<gcolptr<<" row "<<i<<" at rowind "<<rowind<<std::endl;
//#endif
//            Int idxA = 0;
//
//            Int idx = fi;
//            Int iLRow = 0;
//            for(Int s = first_nz;s<=last_nz;s++){
//              iLRow = lindx(idx-1);
//              if( iLRow == rowind){
//                double elem = nzcol(idxA);
//                lnz(s-1) = elem;
//                if(idxA<nzcol.m()){
//                  idxA++;
//                  rowind = HMat.Global_.rowind(gcolptr+idxA-1);
//                }
//              }
//#ifdef _DEBUG_
//              logfileptr->OFS()<<"    "<<idx<<"     L("<<lindx(idx-1)<<","<<i<<") = "<<lnz(s-1)<<std::endl;
//#endif
//              idx++;
//            }
//
//
//          }
//          else{
//          
//            Int local_i = (i-(numColFirst)*iam);
//            //do a local copy
//            //copy A into L
//            Int colptr = HMat.Local_.colptr(local_i-1);
//            Int rowind = HMat.Local_.rowind(colptr-1);
//            Int nextColptr = HMat.Local_.colptr(local_i);
//
//            Int idx = fi; 
//            for(Int s = first_nz;s<=last_nz;s++){
//              Int iLRow = lindx(idx-1);
//              if( iLRow == rowind){
//                double elem = HMat.nzvalLocal(colptr-1);
//                lnz(s-1) = elem;
//                if(colptr<nextColptr){
//                  colptr++;
//                  rowind = HMat.Local_.rowind(colptr-1);
//                }
//              }
//
//#ifdef _DEBUG_
//              logfileptr->OFS()<<"    "<<idx<<"     L("<<lindx(idx-1)<<","<<i<<") = "<<lnz(s-1)<<std::endl;
//#endif
//              idx++;
//            }
//
//          }
//
//
//        }
//        else{
//          //Get column i from A
//          if(iOwner==iam){
//            Int local_i = (i-(numColFirst)*iam);
//            Int colptr =  HMat.Local_.colptr(local_i-1);
//            Int nrows = HMat.Local_.colptr(local_i) - HMat.Local_.colptr(local_i-1);
//            MPI_Send(&HMat.nzvalLocal(colptr-1),nrows*sizeof(double),MPI_BYTE,iDest,0,MPI_COMM_WORLD);
//
//#ifdef _DEBUG_
//            logfileptr->OFS()<<"Sent col "<<i<<" to P"<<iDest<<std::endl;
//#endif
//          }
//        }
//        fi++;
//
//      }
//    }
//*/
// 




    SupernodalMatrix<double> SMat(HMat,Afactptr);



    for(Int i=0;i<SMat.LocalSupernodes_.size();++i){
      SuperNode & snode = SMat.LocalSupernodes_[i];
      

//      logfileptr->OFS()<<"Supernode "<<snode.id<<" owns columns "<<snode.firstCol<<" to "<<snode.lastCol<<std::endl;
      logfileptr->OFS()<<"Supernode "<<snode.Id()<<" owns blocks:"<<std::endl;


      for(int blkidx=0;blkidx<snode.NZBlockCnt();++blkidx){
        NZBlock<double> & nzblk = snode.GetNZBlock(blkidx);
        Int lastRow = nzblk.GIndex() + nzblk.NRows() -1;
//        logfileptr->OFS()<<nzblk<<std::endl;
        logfileptr->OFS()<<"L("<<nzblk.GIndex()<<".."<<lastRow<<" , "<<snode.FirstCol()<<".."<<snode.LastCol()<<")"<<std::endl;
      }


      logfileptr->OFS()<<"--------------------------------------------------"<<std::endl;
    }





    delete logfileptr;
    upcxx::finalize();
    return;



    //Alloc RHS
    //Call PDGEMM to compute XTrue

    upcxx::barrier();



    //TODO This needs to be fixed
    Afactptr->prow = 1;//sqrt(np);
    Afactptr->pcol = np;//Afact.prow;
    np = Afactptr->prow*Afactptr->pcol;
    if(MYTHREAD==0){
      cout<<"Number of cores to be used: "<<np<<endl;
//      cout<<"Matrix order is "<<Afact.n<<endl;
    }



    //Allocate chunks of the matrix on each processor
    Afactptr->Allocate(np,Afactptr->n,blksize);
    //TODO: recode this
    //Afactptr->Distribute(HMat);

    upcxx::barrier();
    upcxx::wait();
    //deallocate HMat
    //A.Clear();

    logfileptr->OFS()<<"distributed"<<endl;

      timeSta =  omp_get_wtime( );
    TIMER_START(FANBOTH);
    if(MYTHREAD==Afactptr->MAP(Afactptr->n-1,Afactptr->n-1)){
#ifdef _DEBUG_
      cout<<"LOCK IS TAKEN BY"<<MYTHREAD<<endl;
#endif
    }

#ifdef _DEBUG_
    logfileptr->OFS()<<"initializing"<<endl;
#endif

    upcxx::barrier();

    Afactptr->NumericalFactorization();
    Afactptr->WaitFactorization();
    timeEnd =  omp_get_wtime( );

    TIMER_STOP(FANBOTH);

#ifdef _DEBUG_
    logfileptr->OFS()<<"gathering"<<endl;
#endif

    //gather all data on P0
 //   DblNumMat_upcxx Afinished;
 //   upcxx::barrier();
 //   if(MYTHREAD==0)
 //   {
 //     cout<<"FAN-BOTH: "<<timeEnd-timeSta<<endl;
 //     Afactptr->Gather(Afinished);
 //   }

 //   upcxx::wait();




delete logfileptr;
    upcxx::finalize();
    return;

    DblNumMat A(Afactptr->n,Afactptr->n);
    //csc_matrix_expand_to_dense (A.Data(), 0, Afact.n, (const csc_matrix_t *) Atmp->repr);
    destroy_sparse_matrix (Atmp);



      Int n = Afactptr->n;
      Int nrhs = 5;

    if(MYTHREAD==0){

      RHS.Resize(n,nrhs);
      XTrue.Resize(n,nrhs);
      UniformRandom(XTrue);
      blas::Gemm('N','N',n,nrhs,n,1.0,&A(0,0),n,&XTrue(0,0),n,0.0,&RHS(0,0),n);
    }

    upcxx::barrier();

    //TODO This needs to be fixed
    Afactptr->prow = 1;//sqrt(np);
    Afactptr->pcol = np;//Afact.prow;
    np = Afactptr->prow*Afactptr->pcol;
    if(MYTHREAD==0){
      cout<<"Number of cores to be used: "<<np<<endl;
    }



    //Allocate chunks of the matrix on each processor
    Afactptr->Allocate(np,Afactptr->n,blksize);
    Afactptr->Distribute(A);

    upcxx::barrier();
    upcxx::wait();
    A.Clear();

    logfileptr->OFS()<<"distributed"<<endl;

      timeSta =  omp_get_wtime( );
    TIMER_START(FANBOTH);
    if(MYTHREAD==Afactptr->MAP(Afactptr->n-1,Afactptr->n-1)){
#ifdef _DEBUG_
      cout<<"LOCK IS TAKEN BY"<<MYTHREAD<<endl;
#endif
    }

#ifdef _DEBUG_
    logfileptr->OFS()<<"initializing"<<endl;
#endif

    upcxx::barrier();

    Afactptr->NumericalFactorization();
    Afactptr->WaitFactorization();
    timeEnd =  omp_get_wtime( );

    TIMER_STOP(FANBOTH);

#ifdef _DEBUG_
    logfileptr->OFS()<<"gathering"<<endl;
#endif

    //gather all data on P0
    DblNumMat_upcxx Afinished;
    upcxx::barrier();
    if(MYTHREAD==0)
    {
      cout<<"FAN-BOTH: "<<timeEnd-timeSta<<endl;
      Afactptr->Gather(Afinished);
    }

    upcxx::wait();


#ifdef _DEBUG_
    logfileptr->OFS()<<"quitting "<<iam<<endl;
#endif

    if(iam==0)
    {
    logfileptr->OFS()<<"testing"<<endl;
      //check the result
      double norm = 0;

      //do a solve
      DblNumMat X = RHS;
      // X = RHS;
      lapack::Potrs('L',n,nrhs,&Afinished(0,0),n,&X(0,0),n);

      blas::Axpy(n*nrhs,-1.0,&XTrue(0,0),1,&X(0,0),1);
      norm = lapack::Lange('F',n,nrhs,&X(0,0),n);
      printf("Norm of residual after SOLVE for FAN-BOTH is %10.2e\n",norm);
    }

    delete logfileptr;

    upcxx::finalize();
    upcxx::Destroy<FBMatrix_upcxx>(AfactGptr);

  }
  catch( std::exception& e )
  {
    std::cerr << "Exception with message: "
      << e.what() << std::endl;
#ifndef _RELEASE_
    DumpCallStack();
#endif
  }


  return 0;
}

