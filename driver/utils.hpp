#ifndef _DRIVER_UTILS_HEADER_
#define _DRIVER_UTILS_HEADER_

#include <vector>
#include <map>
#include "sympack.hpp"

template<typename SCALAR>
void generate_rhs( symPACK::DistSparseMatrix<SCALAR> & HMat, std::vector<SCALAR> & RHS, std::vector<SCALAR> & XTrue, int nrhs){
  using namespace symPACK;
  if(nrhs>0){
    MPI_Comm & worldcomm = HMat.comm;
    int np = 1;
    MPI_Comm_size(worldcomm,&np);
    int iam = 0;
    MPI_Comm_rank(worldcomm,&iam);
    int n = HMat.size;
    RHS.resize(n*nrhs);
    XTrue.resize(n*nrhs);

    //Initialize XTrue;
    Int val = 1.0;
    for(Int i = 0; i<n;++i){ 
      for(Int j=0;j<nrhs;++j){
        XTrue[i+j*n] = val;
        val = -val;
      }
    }



    if(iam==0){
      std::cout<<"Starting spGEMM"<<std::endl;
    }

    double timeSta = get_time();
    {
      const DistSparseMatrixGraph & Local = HMat.GetLocalGraph();
      Int baseval = Local.GetBaseval();
      Idx firstCol = Local.LocalFirstVertex()+(1-Local.GetBaseval());//1-based
      Idx lastCol = Local.LocalFirstVertex()+Local.LocalVertexCount()+(1-Local.GetBaseval());//1-based

      RHS.assign(n*nrhs,0.0);
      std::vector<Idx> rrowind;
      std::vector<SCALAR> rnzval;
      std::vector<SCALAR> ts(nrhs);
      for(Idx j = 1; j<=n; ++j){
        Int iOwner = 0; for(iOwner = 0; iOwner<np;iOwner++){ if(Local.vertexDist[iOwner]+(1-baseval)<=j && j<Local.vertexDist[iOwner+1]+(1-baseval)){ break; } }


        Ptr nnz = 0;
        Idx * rowind = nullptr;
        SCALAR * nzval = nullptr;

        //do I own the column ?

        if(iam==iOwner){
          Idx iLocal = j-firstCol;//0-based
          Ptr colbeg = Local.colptr[iLocal]-(1-Local.GetBaseval());//1-based
          Ptr colend = Local.colptr[iLocal+1]-(1-Local.GetBaseval());//1-based
          nnz = colend-colbeg;
          nzval = &HMat.nzvalLocal[colbeg-1];
          rowind = const_cast<Idx*>(&Local.rowind[colbeg-1]);
        }

        if(iam==iOwner){
          for(Int k = 0; k<nrhs; ++k){
            ts[k] = XTrue[j-1+k*n];
          }
          //do a dense mat mat mul ?
          for(Ptr ii = 1; ii<= nnz;++ii){
            Idx row = rowind[ii-1]-(1-Local.GetBaseval());//1-based
            for(Int k = 0; k<nrhs; ++k){
              RHS[row-1+k*n] += ts[k]*nzval[ii-1];
              if(row>j){
                RHS[j-1+k*n] += XTrue[row-1+k*n]*nzval[ii-1];
              }
            }
          }
        }
      }
    }

    mpi::Allreduce( (SCALAR*)MPI_IN_PLACE, &RHS[0],  n*nrhs, MPI_SUM, worldcomm);

    double timeEnd = get_time();
    if(iam==0){
      std::cout<<"spGEMM time: "<<timeEnd-timeSta<<std::endl;
    }

  }
}


template<typename SCALAR>
void check_solution( symPACK::DistSparseMatrix<SCALAR> & HMat, std::vector<SCALAR> & RHS, std::vector<SCALAR> & XFinal ) {
  using namespace symPACK;
  if(XFinal.size()>0) {
    MPI_Comm & worldcomm = HMat.comm;
    int iam = 0;
    MPI_Comm_rank(worldcomm,&iam);
    int n = HMat.size;
    int nrhs = XFinal.size()/n;
    const DistSparseMatrixGraph & Local = HMat.GetLocalGraph();
    Idx firstCol = Local.LocalFirstVertex()+(1-Local.GetBaseval());//1-based
    Idx lastCol = Local.LocalFirstVertex()+Local.LocalVertexCount()+(1-Local.GetBaseval());//1-based

    std::vector<SCALAR> AX(n*nrhs,SCALAR(0.0));

    for(Int k = 0; k<nrhs; ++k){
      for(Int j = 1; j<=n; ++j){
        //do I own the column ?
        if(j>=firstCol && j<lastCol){
          Int iLocal = j-firstCol;//0-based
          Int tgtCol = j;
          SCALAR t = XFinal[tgtCol-1+k*n];
          Ptr colbeg = Local.colptr[iLocal]-(1-Local.GetBaseval());//1-based
          Ptr colend = Local.colptr[iLocal+1]-(1-Local.GetBaseval());//1-based
          //do a dense mat mat mul ?
          for(Ptr ii = colbeg; ii< colend;++ii){
            Int row = Local.rowind[ii-1]-(1-Local.GetBaseval());
            Int tgtRow = row;
            AX[tgtRow-1+k*n] += t*HMat.nzvalLocal[ii-1];
            if(row>j){
              AX[tgtCol-1+k*n] += XFinal[tgtRow-1+k*n]*HMat.nzvalLocal[ii-1];
            }
          }
        }
      }
    }

    //Do a reduce of RHS
    mpi::Allreduce((SCALAR*)MPI_IN_PLACE,&AX[0],AX.size(),MPI_SUM,worldcomm);

    if(iam==0){
      blas::Axpy(AX.size(),-1.0,&RHS[0],1,&AX[0],1);
      double normAX = lapack::Lange('F',n,nrhs,&AX[0],n);
      double normRHS = lapack::Lange('F',n,nrhs,&RHS[0],n);
      std::cout<<"Norm of residual after SPCHOL is "<<normAX/normRHS<<std::endl;
    }
  }
};


inline void process_options(int argc, char **argv, symPACK::symPACKOptions & optionsFact,std::string & filename, std::string & informatstr, bool & complextype, int & nrhs){
  using namespace symPACK;
  using option_t = std::map<std::string,std::vector<std::string> > ;
  // *********************************************************************
  // Input parameter
  // *********************************************************************
  option_t options;
  OptionsCreate(argc, argv, options);

  if( options.find("-in") != options.end() ){
    filename= options["-in"].front();
  }

  informatstr = "HARWELL_BOEING";
  if( options.find("-inf") != options.end() ){
    informatstr= options["-inf"].front();
  }

  complextype=false;
  if (options.find("-z") != options.end()){
    complextype = true;
  }

  //-----------------------------------------------------------------
  optionsFact.memory_limit=-1.0;
  if (options.find("-mem") != options.end()){
    optionsFact.memory_limit = atof(options["-mem"].front().c_str()) ;
  }

  optionsFact.print_stats=false;
  if (options.find("-ps") != options.end()){
    optionsFact.print_stats = atoi(options["-ps"].front().c_str()) == 1;
  }

  if( options.find("-dumpPerm") != options.end() ){
    optionsFact.dumpPerm = atoi(options["-dumpPerm"].front().c_str());
  }
  //-----------------------------------------------------------------

  optionsFact.orderingStr = "MMD" ;
  if( options.find("-ordering") != options.end() ){
    optionsFact.orderingStr = options["-ordering"].front();
  }

  if( options.find("-npord") != options.end() ){
    optionsFact.NpOrdering= atoi(options["-npord"].front().c_str());
  }

  if( options.find("-refine") != options.end() ){
    optionsFact.order_refinement_str = options["-refine"].front();
  }

  //-----------------------------------------------------------------
  Int verbose = 0;
  if( options.find("-v") != options.end() ){
    verbose= atoi(options["-v"].front().c_str());
  }
  optionsFact.verbose = verbose;

  Int maxSnode = 0;
  if( options.find("-b") != options.end() ){
    if(options["-b"].front() != "inf"){
      maxSnode= atoi(options["-b"].front().c_str());
    }
  }
  optionsFact.relax.SetMaxSize(maxSnode);

  if( options.find("-relax") != options.end() ){
    if(options["-relax"].size()==3){
      optionsFact.relax.SetNrelax0(atoi(options["-relax"][0].c_str()));
      optionsFact.relax.SetNrelax1(atoi(options["-relax"][1].c_str()));
      optionsFact.relax.SetNrelax2(atoi(options["-relax"][2].c_str()));
    }
    else{
      //disable relaxed supernodes
      optionsFact.relax.SetNrelax0(0);
    }
  }

  //-----------------------------------------------------------------

  Int numThreads = 1;
  if( options.find("-t") != options.end() ){
    numThreads = atoi(options["-t"].front().c_str());
  }

  Int maxIsend = -1;
  if( options.find("-is") != options.end() ){
    if(options["-is"].front() != "inf"){
      maxIsend= atoi(options["-is"].front().c_str());
    }
  }

  Int maxIrecv = -1;
  if( options.find("-ir") != options.end() ){
    if(options["-ir"].front() != "inf"){
      maxIrecv= atoi(options["-ir"].front().c_str());
    }
  }

  if( options.find("-lb") != options.end() ){
    optionsFact.load_balance_str = options["-lb"].front();
  }

  if( options.find("-scheduler") != options.end() ){
    if(options["-scheduler"].front()=="MCT"){
      optionsFact.scheduler = MCT;
    }
    if(options["-scheduler"].front()=="FIFO"){
      optionsFact.scheduler = FIFO;
    }
    else if(options["-scheduler"].front()=="PR"){
      optionsFact.scheduler = PR;
    }
    else{
      optionsFact.scheduler = DL;
    }
  }

  optionsFact.mappingTypeStr = "ROW2D";
  if( options.find("-map") != options.end() ){
    optionsFact.mappingTypeStr = options["-map"].front();
  }

  //-----------------------------------------------------------------

  optionsFact.factorization = FANBOTH;
  if( options.find("-alg") != options.end() ){
    if(options["-alg"].front()=="FANBOTH"){
      optionsFact.factorization = FANBOTH;
    }
    else if(options["-alg"].front()=="FANOUT"){
      optionsFact.factorization = FANOUT;
    }
  }

  if( options.find("-fact") != options.end() ){
    if(options["-fact"].front()=="LL"){
      optionsFact.decomposition = DecompositionType::LL;
    }
    else if(options["-fact"].front()=="LDL"){
      optionsFact.decomposition = DecompositionType::LDL;
    }
  }

  //-----------------------------------------------------------------

  nrhs = 0;
  if( options.find("-nrhs") != options.end() ){
    nrhs= atoi(options["-nrhs"].front().c_str());
  }

  //-----------------------------------------------------------------
  optionsFact.maxIsend = maxIsend;
  optionsFact.maxIrecv = maxIrecv;
  optionsFact.numThreads = numThreads;

  //Set up the threading environment
  Multithreading::NumThread = numThreads;
}


#endif // _DRIVER_UTILS_HEADER_