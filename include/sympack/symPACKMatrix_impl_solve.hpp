#ifndef _SYMPACK_MATRIX_IMPL_SOLVE_HPP_
#define _SYMPACK_MATRIX_IMPL_SOLVE_HPP_

#include <sympack/symPACKMatrix.hpp>




//template <typename T > class DataDistribution {
//  public:
//    DataDistribution();
//
//  protected:
//    std::vector<Int> dimensionSizes;
//    std::vector<std::vector<Int> > dimensionDist;
//    T * data_;
//
//  public:
//    void setDimension
//    void SetDimensions( std::vector<Int> & pdimensionSizes ){
//
//    }
//
//    void SetDimensionDist( Int pdimension, std::vector<Int> & pdist );
//
//    //optional    
//    void allocate();
//  
//
//}





////New class for a distributed right hand side
//template <typename T > class RightHandSide {
//  public:
//  //global size
//  Idx nrhs;
//  Idx n;
//  //distribution info
//  //1D or 2D
//  enum distribution {1D,2D,1DCYCLIC,2DCYCLIC};
//
//  
//  
//  
//
//}




  template <typename T>
template< typename Task> void symPACKMatrix<T>::CheckIncomingMessages_Solve(supernodalTaskGraph<Task> & taskGraph, std::shared_ptr<Scheduler<Task> > scheduler)
{
  scope_timer(a,CHECK_MESSAGE);

  SYMPACK_TIMER_START(UPCXX_ADVANCE);
  upcxx::advance();
  SYMPACK_TIMER_STOP(UPCXX_ADVANCE);

  bool comm_found = false;
  IncomingMessage * msg = NULL;
  Task * curTask = NULL;

  do{
    msg=NULL;

    //if we have some room, turn blocking comms into async comms
    if(gIncomingRecvAsync.size() < gMaxIrecv || gMaxIrecv==-1){
      scope_timer(b,MV_MSG_SYNC);
      while((gIncomingRecvAsync.size() < gMaxIrecv || gMaxIrecv==-1) && !gIncomingRecv.empty()){
        bool success = false;
        auto it = gIncomingRecv.begin();
        //find one which is not done
        while((*it)->IsDone()){it++;}

        success = (*it)->AllocLocal();
        if(success){
          (*it)->AsyncGet();
          gIncomingRecvAsync.push_back(*it);
          gIncomingRecv.erase(it);
#ifdef _DEBUG_PROGRESS_
          logfileptr->OFS()<<"TRANSFERRED TO ASYNC COMM"<<std::endl;
#endif
        }
        else{
          //TODO handle out of memory
          abort();
          break;
        }
      }
    }

    {
      //find if there is some finished async comm
      auto it = TestAsyncIncomingMessage();
      if(it!=gIncomingRecvAsync.end()){
        scope_timer(b,RM_MSG_ASYNC);
        msg = *it;
        gIncomingRecvAsync.erase(it);
      }
      else if(scheduler->done() && !gIncomingRecv.empty()){
        scope_timer(c,RM_MSG_ASYNC);
        //find a "high priority" task
        //find a task we would like to process
        auto it = gIncomingRecv.begin();
        for(auto cur_msg = gIncomingRecv.begin(); 
            cur_msg!= gIncomingRecv.end(); cur_msg++){
          //look at the meta data
          //TODO check if we can parametrize that
          if((*cur_msg)->meta.tgt < (*it)->meta.tgt){
            it = cur_msg;
          }
        }
        msg = *it;
        gIncomingRecv.erase(it);
      }
    }

    if(msg!=NULL){
      scope_timer(a,WAIT_AND_UPDATE_DEPS);
      bool success = msg->Wait(); 
      //TODO what are the reasons of failure ?
      bassert(success);

      Int tgtOwner = Mapping_->Map(msg->meta.tgt-1,msg->meta.tgt-1);
      Int srcOwner = Mapping_->Map(msg->meta.src-1,msg->meta.src-1);
      //Int iUpdater = Mapping_->Map(msg->meta.tgt-1,msg->meta.src-1);


      typename std::list<Task>::iterator taskit;
      //this is a forward contribution
      if (msg->meta.tgt>msg->meta.src){
        taskit = taskGraph.find_task(msg->meta.src,msg->meta.tgt,Solve::op_type::FU);
      }
      //this is a back contribution
      else{
        assert(msg->meta.tgt<msg->meta.src);
        taskit = taskGraph.find_task(msg->meta.src,msg->meta.tgt,Solve::op_type::BU);
      }

      taskit->remote_deps--;
      taskit->data.push_back(msg);

      if(taskit->remote_deps==0 && taskit->local_deps==0){
        scheduler->push(*taskit);    
        taskGraph.removeTask(taskit);
      }
    }
  }while(msg!=NULL);
}









template <typename T> void symPACKMatrix<T>::solveNew_(T * RHS, int nrhs,  T * Xptr) {
  scope_timer(a,SPARSE_SOLVE_INTERNAL);
  Int n = iSize_;

  if(iam<np){

    Int nsuper = TotalSupernodeCnt();
    auto SupETree = ETree_.ToSupernodalETree(Xsuper_,SupMembership_,Order_);

    //compute children size
    std::vector<Int> rem_children(nsuper,0);
    std::vector<Int> loc_children(nsuper,0);
    for(Int I=1;I<nsuper;I++){
      Int parent = SupETree.PostParent(I-1);
      if(parent!=0){
        Int childOwner = this->Mapping_->Map(I-1,I-1);
        Int parentOwner = this->Mapping_->Map(parent-1,parent-1);

        if(parentOwner == childOwner){
          ++loc_children[parent-1];
        }
        else{
          ++rem_children[parent-1];
        }
      }
    }

    Contributions2_.resize(LocalSupernodes_.size());
    for(Int I=1;I<=nsuper;I++){
      Int iOwner = this->Mapping_->Map(I-1,I-1);
      //If I own the column, factor it
      if( iOwner == iam ){
        //Create the blocks of my contrib with the same nz structure as L
        //and the same width as the final solution
        //MEMORY CONSUMPTION TOO HIGH ?
        Int iLocalI = snodeLocalIndex(I);
        SuperNode<T> * cur_snode = this->LocalSupernodes_[iLocalI-1];
        Contributions2_[iLocalI-1].reset(CreateSuperNode<UpcxxAllocator>(options_.decomposition,I,1,nrhs, cur_snode->NRowsBelowBlock(0) ,iSize_));
        auto contrib = std::dynamic_pointer_cast< SuperNode<T,UpcxxAllocator> >(Contributions2_[iLocalI-1]);

        for(Int blkidx = 0; blkidx<cur_snode->NZBlockCnt();++blkidx){
          NZBlockDesc & cur_desc = cur_snode->GetNZBlockDesc(blkidx);
          contrib->AddNZBlock(cur_snode->NRows(blkidx),nrhs,cur_desc.GIndex);

          //Copy RHS into contrib if first block
          if(blkidx==0){

            T * diag_nzval = contrib->GetNZval(0);

            for(Int kk = 0; kk<cur_snode->Size(); ++kk){
              //First, copy the RHS into the contribution
              Int srcRow = this->Order_.perm[cur_desc.GIndex+kk-1];
              for(Int j = 0; j<nrhs;++j){
                diag_nzval[kk*nrhs+j] = RHS[srcRow-1 + j*n];
              }
            }

          }
        }
        contrib->Shrink();
          if (contrib->NZBlockCnt()>1){
            NZBlockDesc & cur_desc = contrib->GetNZBlockDesc(1);
            Int nRows = contrib->NRowsBelowBlock(1);
            std::fill(contrib->GetNZval(cur_desc.Offset),contrib->GetNZval(cur_desc.Offset)+nRows*nrhs,ZERO<T>());
          }

      }
    }

    //std::shared_ptr<Scheduler<CompTask> > scheduler(new FIFOScheduler<CompTask>( ));
    std::shared_ptr<Scheduler<CompTask> > scheduler(new DLScheduler<CompTask>( ));

    supernodalTaskGraph<CompTask> taskGraph;
    taskGraph.taskLists_.resize(nsuper,NULL);
    //Build the graph
    //TODO

    //Do a bottom up traversal
    for (Int I = 1; I<=nsuper; I++){
      //Add the FUC task
      {
        Int iOwner = Mapping_->Map(I-1,I-1);
        if(iam==iOwner){
          CompTask FUCtask;
          FUCtask.meta.resize(2*sizeof(Int)+sizeof(Solve::op_type));
          Int * meta = reinterpret_cast<Int*>(FUCtask.meta.data());
          meta[0] = I;
          meta[1] = I;
          Solve::op_type & type = *reinterpret_cast<Solve::op_type*>(&meta[2]);
          type = Solve::op_type::FUC;
          FUCtask.local_deps = loc_children[I-1]+rem_children[I-1];

          taskGraph.addTask(FUCtask);
        }
      }


      //Add the BUC task
      {
        Int iOwner = Mapping_->Map(I-1,I-1);
        if(iam==iOwner){
          CompTask BUCtask;
          BUCtask.meta.resize(2*sizeof(Int)+sizeof(Solve::op_type));
          Int * meta = reinterpret_cast<Int*>(BUCtask.meta.data());
          meta[0] = I;
          meta[1] = I;
          Solve::op_type & type = *reinterpret_cast<Solve::op_type*>(&meta[2]);
          type = Solve::op_type::BUC;

          BUCtask.local_deps = 1;

          taskGraph.addTask(BUCtask);
        }
      }

      Int parent = SupETree.PostParent(I-1);
      if(parent!=0){
        //Add the FU task
        {
          Int iOwner = Mapping_->Map(parent-1,parent-1);
          if(iam==iOwner){

            CompTask FUtask;
            FUtask.meta.resize(2*sizeof(Int)+sizeof(Solve::op_type));
            Int * meta = reinterpret_cast<Int*>(FUtask.meta.data());
            meta[0] = I;
            meta[1] = parent;
            Solve::op_type & type = *reinterpret_cast<Solve::op_type*>(&meta[2]);
            type = Solve::op_type::FU;

            Int childOwner = this->Mapping_->Map(I-1,I-1);
            if(childOwner==iam){
              FUtask.local_deps = 1;
            }
            else{
              FUtask.remote_deps = 1;
            }

            taskGraph.addTask(FUtask);
          }
        }
        //Add the BU task
        {
          Int iOwner = Mapping_->Map(I-1,I-1);
          if(iam==iOwner){

            CompTask BUtask;
            BUtask.meta.resize(2*sizeof(Int)+sizeof(Solve::op_type));
            Int * meta = reinterpret_cast<Int*>(BUtask.meta.data());
            meta[0] = parent;
            meta[1] = I;
            Solve::op_type & type = *reinterpret_cast<Solve::op_type*>(&meta[2]);
            type = Solve::op_type::BU;

            Int parentOwner = this->Mapping_->Map(parent-1,parent-1);
            if(parentOwner==iam){
              BUtask.local_deps = 1;
            }
            else{
              BUtask.remote_deps = 1;
            }

            taskGraph.addTask(BUtask);
          }
        }
      }


    }

    size_t gTaskCnt = taskGraph.taskLists_.size();

    ////Initialize the scheduler with the task graph
    //for(auto queue: taskGraph.taskLists_){
    //  if(queue != nullptr){
    //    for(auto taskit = queue->begin(); taskit!=queue->end(); taskit++){
    //      if(taskit->remote_deps==0 && taskit->local_deps==0){
    //        scheduler->push(*taskit);
    //        taskGraph.removeTask(taskit);
    //      }
    //    }
    //  }
    //}
  for(int i = 0; i<gTaskCnt; ++i){
    if(taskGraph.taskLists_[i] != NULL){
      auto taskit = taskGraph.taskLists_[i]->begin();
      while (taskit != taskGraph.taskLists_[i]->end())
      {
        if(taskit->remote_deps==0 && taskit->local_deps==0){
          scheduler->push(*taskit);
          auto it = taskit++;
          taskGraph.removeTask(it);
        }
        else
        {
          ++taskit;
        }
      }
    }
  }




    auto dec_ref = [&scheduler,&taskGraph] (std::list<CompTask>::iterator taskit, Int loc, Int rem) {
      taskit->local_deps-= loc;
      taskit->remote_deps-= rem;
      if(taskit->remote_deps==0 && taskit->local_deps==0){
        scheduler->push(*taskit);
        auto it = taskit++;
        taskGraph.removeTask(it);
      }
    };

    bool doPrint = true;
    Int prevCnt = -1;
    while(taskGraph.getTaskCount()>0){
      CheckIncomingMessages_Solve(taskGraph,scheduler);

      if(!scheduler->done())
      {
        //Pick a ready task
        auto curTask = scheduler->top();
        scheduler->pop();

        //apply the computation
        Int * meta = reinterpret_cast<Int*>(curTask.meta.data());
        Int src = meta[0];
        Int tgt = meta[1];

        const Solve::op_type & type = *reinterpret_cast<Solve::op_type*>(&meta[2]);
        switch(type){
          case Solve::op_type::FUC:
            {
            Int src_local = snodeLocalIndex(src); 
            auto cur_snode = LocalSupernodes_[src_local-1];
            auto contrib = std::dynamic_pointer_cast< SuperNode<T,UpcxxAllocator> >(Contributions2_[src_local-1]);
            contrib->forward_update_contrib(cur_snode);

            //may have to send to parent

            Int parent = SupETree.PostParent(src-1);
            if(parent!=0){
              Int parentOwner = this->Mapping_->Map(parent-1,parent-1);
              if(parentOwner!=iam){
                //Send to the parent
                {

                  Int src_nzblk_idx = 1;
                  NZBlockDesc & pivot_desc = contrib->GetNZBlockDesc(src_nzblk_idx);
                  Int src_first_row = pivot_desc.GIndex;
                  T* nzval_ptr = contrib->GetNZval(pivot_desc.Offset);
                  upcxx::global_ptr<char> sendPtr((char*)nzval_ptr);

                  MsgMetadata meta;
                  meta.src = src;
                  meta.tgt = parent;
                  meta.GIndex = src_first_row;

                  char * last_byte_ptr = (char*)&pivot_desc + sizeof(NZBlockDesc);
                  size_t msgSize = last_byte_ptr - (char*)nzval_ptr;

                  signal_data(sendPtr, msgSize, parentOwner, meta);

                }
              }
              else{
                //unlock the BU task
                auto taskit = taskGraph.find_task(src,parent,Solve::op_type::FU);
                dec_ref(taskit,1,0);
              }
            }
            else{
              //unlock the BUC task
              auto taskit = taskGraph.find_task(tgt,tgt,Solve::op_type::BUC);
              dec_ref(taskit,1,0);
            }
            }
            break;
          case Solve::op_type::BUC:
            {
            Int src_local = snodeLocalIndex(src); 
            auto cur_snode = LocalSupernodes_[src_local-1];
            auto contrib = std::dynamic_pointer_cast< SuperNode<T,UpcxxAllocator> >(Contributions2_[src_local-1]);
            contrib->back_update_contrib(cur_snode);

            //send to my children
            std::vector<char> is_sent(np,false);
            Int child_snode_id = src - 1;
            if(child_snode_id>0){
              Int children_found = 0;
              while(child_snode_id>0 && children_found<loc_children[src-1]+rem_children[src-1]){
                Int parent = SupETree.PostParent(child_snode_id-1);
                if(parent==src){
                  Int iTarget = this->Mapping_->Map(child_snode_id-1,child_snode_id-1);
                  if(iTarget!=iam){
                    if(!is_sent[iTarget]){

                      Int src_nzblk_idx = 0;
                      NZBlockDesc & pivot_desc = contrib->GetNZBlockDesc(src_nzblk_idx);
                      Int src_first_row = pivot_desc.GIndex;
                      T* nzval_ptr = contrib->GetNZval(pivot_desc.Offset);
                      upcxx::global_ptr<char> sendPtr((char*)nzval_ptr);

                      MsgMetadata meta;
                      meta.src = src;
                      meta.tgt = child_snode_id;
                      meta.GIndex = src_first_row;

                      char * last_byte_ptr = (char*)&pivot_desc + sizeof(NZBlockDesc);
                      size_t msgSize = last_byte_ptr - (char*)nzval_ptr;

                      signal_data(sendPtr, msgSize, iTarget, meta);

                      is_sent[iTarget] = true;
                    }
                  }
                  else{
                    auto taskit = taskGraph.find_task(src,child_snode_id,Solve::op_type::BU);
                    dec_ref(taskit,1,0);
                  }
                  children_found++;
                }
                //last column of the prev supernode
                child_snode_id--;
              }
            }






            }
            break;
          case Solve::op_type::FU:
            {
            Int tgt_local = snodeLocalIndex(tgt); 
            auto contrib = std::dynamic_pointer_cast< SuperNode<T,UpcxxAllocator> >(Contributions2_[tgt_local-1]);

            Int iOwner = this->Mapping_->Map(src-1,src-1);
            if(iOwner==iam){
              //local
              Int src_local = snodeLocalIndex(src); 
              auto dist_contrib = std::dynamic_pointer_cast< SuperNode<T,UpcxxAllocator> >(Contributions2_[src_local-1]);
              contrib->forward_update(&*dist_contrib,iOwner,iam);
            }
            else{
              //remote
              assert(curTask.data.size()==1);
              IncomingMessage * msgPtr = *curTask.data.begin();
              assert(msgPtr->IsDone());
              char* dataPtr = msgPtr->GetLocalPtr();
              auto dist_contrib = CreateSuperNode(options_.decomposition,dataPtr,msgPtr->Size());
              dist_contrib->InitIdxToBlk();
              contrib->forward_update(&*dist_contrib,iOwner,iam);
              delete dist_contrib;
              delete msgPtr;
            }
            auto taskit = taskGraph.find_task(tgt,tgt,Solve::op_type::FUC);
            dec_ref(taskit,1,0);

            }
            break;
          case Solve::op_type::BU:
            {
            Int tgt_local = snodeLocalIndex(tgt); 
            auto contrib = std::dynamic_pointer_cast< SuperNode<T,UpcxxAllocator> >(Contributions2_[tgt_local-1]);

            Int iOwner = this->Mapping_->Map(src-1,src-1);
            if(iOwner==iam){
              //local
              Int src_local = snodeLocalIndex(src); 
              auto dist_contrib = std::dynamic_pointer_cast< SuperNode<T,UpcxxAllocator> >(Contributions2_[src_local-1]);
              contrib->back_update(&*dist_contrib);
            }
            else{
              //remote
              assert(curTask.data.size()==1);
              IncomingMessage * msgPtr = *curTask.data.begin();
              assert(msgPtr->IsDone());
              char* dataPtr = msgPtr->GetLocalPtr();
              auto dist_contrib = CreateSuperNode(options_.decomposition,dataPtr,msgPtr->Size());
              dist_contrib->InitIdxToBlk();
              contrib->back_update(&*dist_contrib);
              delete dist_contrib;

              //do I own another children ? if so, put msgPtr in its data and decrement remote_deps
              bool child_found = false;
              Int parent = src;
              if(parent!=0){
                Int children_count = loc_children[parent-1];
                Int child = tgt+1;
                while(children_count>0 && child<parent){
                  if(SupETree.PostParent(child-1)==src){
                    Int iTarget = this->Mapping_->Map(child-1,child-1);
                    if(iTarget==iam){
                      children_count--;
                      auto taskit = taskGraph.find_task(src,child,Solve::op_type::BU);
                      if(taskit!=taskGraph.taskLists_[child-1]->end()){
                        child_found = true;
                        taskit->data.push_back(msgPtr);
                        dec_ref(taskit,0,1);
                        curTask.data.clear();
                        break;
                      }
                    }
                  }
                  child++;
                }
              }

              if(!child_found){
                //delete otherwise
                delete msgPtr;
              }
            }
            auto taskit = taskGraph.find_task(tgt,tgt,Solve::op_type::BUC);
            dec_ref(taskit,1,0);
            }
            break;
          default:
            break;

        }

        taskGraph.decreaseTaskCount();
      }
    }
  }

  upcxx::async_wait();
  MPI_Barrier(CommEnv_->MPI_GetComm());





}


template <typename T> void symPACKMatrix<T>::solveNew2_(T * RHS, int nrhs,  T * Xptr) {
  scope_timer(a,SPARSE_SOLVE_INTERNAL);
  Int n = iSize_;

  if(iam<np){

    Int nsuper = TotalSupernodeCnt();
    auto SupETree = ETree_.ToSupernodalETree(Xsuper_,SupMembership_,Order_);

    //compute children size
    std::vector<Int> rem_children(nsuper,0);
    std::vector<Int> loc_children(nsuper,0);
    for(Int I=1;I<nsuper;I++){
      Int parent = SupETree.PostParent(I-1);


      if(parent!=0){
        Int childOwner = this->Mapping_->Map(I-1,I-1);
        Int parentOwner = this->Mapping_->Map(parent-1,parent-1);

        if(parentOwner == childOwner){
          ++loc_children[parent-1];
        }
        else{
          ++rem_children[parent-1];
        }
      }
    }

    Contributions2_.resize(LocalSupernodes_.size());
    for(Int I=1;I<=nsuper;I++){
      Int iOwner = this->Mapping_->Map(I-1,I-1);
      //If I own the column, factor it
      if( iOwner == iam ){
        //Create the blocks of my contrib with the same nz structure as L
        //and the same width as the final solution
        //MEMORY CONSUMPTION TOO HIGH ?
        Int iLocalI = snodeLocalIndex(I);
        SuperNode<T> * cur_snode = this->LocalSupernodes_[iLocalI-1];
        Contributions2_[iLocalI-1].reset(CreateSuperNode<UpcxxAllocator>(options_.decomposition,I,1,nrhs, cur_snode->NRowsBelowBlock(0) ,iSize_));
        auto contrib = std::dynamic_pointer_cast< SuperNode<T,UpcxxAllocator> >(Contributions2_[iLocalI-1]);

        for(Int blkidx = 0; blkidx<cur_snode->NZBlockCnt();++blkidx){
          NZBlockDesc & cur_desc = cur_snode->GetNZBlockDesc(blkidx);
          contrib->AddNZBlock(cur_snode->NRows(blkidx),nrhs,cur_desc.GIndex);

          //Copy RHS into contrib if first block
          if(blkidx==0){

            T * diag_nzval = contrib->GetNZval(0);

            for(Int kk = 0; kk<cur_snode->Size(); ++kk){
              //First, copy the RHS into the contribution
              Int srcRow = this->Order_.perm[cur_desc.GIndex+kk-1];
              for(Int j = 0; j<nrhs;++j){
                diag_nzval[kk*nrhs+j] = RHS[srcRow-1 + j*n];
              }
            }

          }
        }
        contrib->Shrink();
        if (contrib->NZBlockCnt()>1){
          NZBlockDesc & cur_desc = contrib->GetNZBlockDesc(1);
          Int nRows = contrib->NRowsBelowBlock(1);
          std::fill(contrib->GetNZval(cur_desc.Offset),contrib->GetNZval(cur_desc.Offset)+nRows*nrhs,ZERO<T>());
        }

      }
    }

    std::shared_ptr<Scheduler< std::shared_ptr<GenericTask> > > scheduler(new FIFOScheduler< std::shared_ptr<GenericTask> >( ));
    //std::shared_ptr<Scheduler< std::shared_ptr<GenericTask> > > scheduler(new DLScheduler< std::shared_ptr<GenericTask> >( ));
    //std::shared_ptr<Scheduler<SparseTask> > scheduler(new DLScheduler<SparseTask>( ));

    taskGraph graph;


    std::hash<std::string> hash_fn;

    auto dec_ref = [&] ( taskGraph::task_iterator taskit, Int loc, Int rem) {
#ifdef SP_THREADS
        std::lock_guard<std::mutex> lock(scheduler->list_mutex_);
#endif
      taskit->second->local_deps_cnt-= loc;
      taskit->second->remote_deps_cnt-= rem;
      if(taskit->second->remote_deps_cnt==0 && taskit->second->local_deps_cnt==0){
        scheduler->push(taskit->second);
        graph.removeTask(taskit->second->id);
      }
    };

    auto log_task = [&] (taskGraph::task_iterator taskit){
        SparseTask * tmp = ((SparseTask*)taskit->second.get());
        Int * meta = reinterpret_cast<Int*>(tmp->meta.data());
        Solve::op_type & type = *reinterpret_cast<Solve::op_type*>(&meta[2]);
        std::string name;
        switch(type){
          case Solve::op_type::FUC:
            name="FUC";
            break;
          case Solve::op_type::BUC:
            name="BUC";
            break;
          case Solve::op_type::BU:
            name="BU";
            break;
          case Solve::op_type::FU:
            name="FU";
            break;
        }
        logfileptr->OFS()<<"  updating "<<name<<" "<<meta[0]<<"_"<<meta[1]<<std::endl;
    };

    auto log_msg = [&] (Int src, Int tgt, Solve::op_type type){
        std::string name;
        switch(type){
          case Solve::op_type::FUC:
            name="FUC";
            break;
          case Solve::op_type::BUC:
            name="BUC";
            break;
          case Solve::op_type::BU:
            name="BU";
            break;
          case Solve::op_type::FU:
            name="FU";
            break;
        }
        logfileptr->OFS()<<"  sending to "<<name<<" "<<src<<"_"<<tgt<<std::endl;
    };




    //Do a bottom up traversal
    for (Int I = 1; I<=nsuper; I++){

      //Add the FUC task
      {
        Int iOwner = Mapping_->Map(I-1,I-1);
        if(iam==iOwner){
          std::shared_ptr<GenericTask> pFUCtask(new SparseTask);
          SparseTask & FUCtask = *(SparseTask*)pFUCtask.get();

          FUCtask.meta.resize(2*sizeof(Int)+sizeof(Solve::op_type));
          Int * meta = reinterpret_cast<Int*>(FUCtask.meta.data());
          meta[0] = I;
          meta[1] = I;
          Solve::op_type & type = *reinterpret_cast<Solve::op_type*>(&meta[2]);
          type = Solve::op_type::FUC;

          FUCtask.local_deps_cnt = loc_children[I-1]+rem_children[I-1];

          //Loop through my children to get the dependencies

          FUCtask.execute = [&,this,I,pFUCtask](){
            Int src = I;
            Int tgt = I;
            SparseTask & FUCtask = *(SparseTask*)pFUCtask.get();

            //std::stringstream sstr;
            //sstr<<I<<"_"<<I<<"_"<<(Int)Solve::op_type::FUC;
            //auto id = hash_fn(sstr.str());

            Int src_local = this->snodeLocalIndex(src); 
            auto cur_snode = this->LocalSupernodes_[src_local-1];
            auto contrib = std::dynamic_pointer_cast< SuperNode<T,UpcxxAllocator> >(this->Contributions2_[src_local-1]);
            contrib->forward_update_contrib(cur_snode);

            //may have to send to parent

            Int parent = SupETree.PostParent(src-1);
            if(parent!=0){
              Int parentOwner = this->Mapping_->Map(parent-1,parent-1);
              if(parentOwner!=iam){
                //Send to the parent
                {
                  Int src_nzblk_idx = 1;
                  NZBlockDesc & pivot_desc = contrib->GetNZBlockDesc(src_nzblk_idx);
                  Int src_first_row = pivot_desc.GIndex;
                  T* nzval_ptr = contrib->GetNZval(pivot_desc.Offset);
                  upcxx::global_ptr<char> sendPtr((char*)nzval_ptr);

                  MsgMetadata meta;
                  meta.src = src;
                  meta.tgt = parent;
                  meta.GIndex = src_first_row;

                  std::stringstream sstr;
                  sstr<<src<<"_"<<parent<<"_"<<(Int)Solve::op_type::FU;
                  meta.id = hash_fn(sstr.str());

                  char * last_byte_ptr = (char*)&pivot_desc + sizeof(NZBlockDesc);
                  size_t msgSize = last_byte_ptr - (char*)nzval_ptr;

                  //log_msg(src,parent,Solve::op_type::FU);

                  {
#ifdef SP_THREADS
        std::lock_guard<std::mutex> lock(scheduler->upcxx_mutex_);
#endif
                  signal_data(sendPtr, msgSize, parentOwner, meta);
                  }
                }
              }
              else{
                //unlock the FU task
                std::stringstream sstr;
                sstr<<src<<"_"<<parent<<"_"<<(Int)Solve::op_type::FU;
                auto id = hash_fn(sstr.str());
                auto taskit = graph.find_task(id);
                //log_task(taskit);
                dec_ref(taskit,1,0);
              }
            }
            else{
              //unlock the BUC task
              std::stringstream sstr;
              sstr<<tgt<<"_"<<tgt<<"_"<<(Int)Solve::op_type::BUC;
              auto id = hash_fn(sstr.str());
              auto taskit = graph.find_task(id);
              //log_task(taskit);
              dec_ref(taskit,1,0);
            }
          };

          std::stringstream sstr;
          sstr<<I<<"_"<<I<<"_"<<(Int)type;
          FUCtask.id = hash_fn(sstr.str());

          graph.addTask(pFUCtask);
        }
      }


      //Add the BUC task
      {
        Int iOwner = Mapping_->Map(I-1,I-1);
        if(iam==iOwner){
          std::shared_ptr<GenericTask> pBUCtask(new SparseTask);
          SparseTask & BUCtask = *(SparseTask*)pBUCtask.get();
          BUCtask.meta.resize(2*sizeof(Int)+sizeof(Solve::op_type));
          Int * meta = reinterpret_cast<Int*>(BUCtask.meta.data());
          meta[0] = I;
          meta[1] = I;
          Solve::op_type & type = *reinterpret_cast<Solve::op_type*>(&meta[2]);
          type = Solve::op_type::BUC;

          BUCtask.local_deps_cnt = 1;

          BUCtask.execute = [&,this,I,pBUCtask] () {
            Int src = I;
            Int tgt = I;
            SparseTask & BUCtask = *(SparseTask*)pBUCtask.get();

          Int * meta = reinterpret_cast<Int*>(BUCtask.meta.data());
          Solve::op_type & type = *reinterpret_cast<Solve::op_type*>(&meta[2]);
          bassert(src==meta[0]);
          bassert(tgt==meta[1]);

  

            Int src_local = snodeLocalIndex(src); 
            auto cur_snode = LocalSupernodes_[src_local-1];
            auto contrib = std::dynamic_pointer_cast< SuperNode<T,UpcxxAllocator> >(Contributions2_[src_local-1]);
            contrib->back_update_contrib(cur_snode);

            //send to my children
            std::vector<char> is_sent(np,false);
            Int child_snode_id = src - 1;
            if(child_snode_id>0){
              Int children_found = 0;
              while(child_snode_id>0 && children_found<loc_children[src-1]+rem_children[src-1]){
                Int parent = SupETree.PostParent(child_snode_id-1);
                if(parent==src){
                  Int iTarget = this->Mapping_->Map(child_snode_id-1,child_snode_id-1);
                  if(iTarget!=iam){
                    if(!is_sent[iTarget]){

                      Int src_nzblk_idx = 0;
                      NZBlockDesc & pivot_desc = contrib->GetNZBlockDesc(src_nzblk_idx);
                      Int src_first_row = pivot_desc.GIndex;
                      T* nzval_ptr = contrib->GetNZval(pivot_desc.Offset);
                      upcxx::global_ptr<char> sendPtr((char*)nzval_ptr);

                      MsgMetadata meta;
                      meta.src = src;
                      meta.tgt = child_snode_id;
                      meta.GIndex = src_first_row;

                      std::stringstream sstr;
                      sstr<<src<<"_"<<child_snode_id<<"_"<<(Int)Solve::op_type::BU;
                      meta.id = hash_fn(sstr.str());

                      char * last_byte_ptr = (char*)&pivot_desc + sizeof(NZBlockDesc);
                      size_t msgSize = last_byte_ptr - (char*)nzval_ptr;

                  //log_msg(src,child_snode_id,Solve::op_type::BU);
                  {
#ifdef SP_THREADS
        std::lock_guard<std::mutex> lock(scheduler->upcxx_mutex_);
#endif
                      signal_data(sendPtr, msgSize, iTarget, meta);
                  }
                      is_sent[iTarget] = true;
                    }
                  }
                  else{
                    std::stringstream sstr;
                    sstr<<src<<"_"<<child_snode_id<<"_"<<(Int)Solve::op_type::BU;
                    auto id = hash_fn(sstr.str());
                    auto taskit = graph.find_task(id);
                //log_task(taskit);
                    dec_ref(taskit,1,0);
                  }
                  children_found++;
                }
                //last column of the prev supernode
                child_snode_id--;
              }
            }
          };


          std::stringstream sstr;
          sstr<<I<<"_"<<I<<"_"<<(Int)type;
          BUCtask.id = hash_fn(sstr.str());

          graph.addTask(pBUCtask);
        }
      }

      Int parent = SupETree.PostParent(I-1);
      if(parent!=0){
        //Add the FU task
        {
          Int iOwner = Mapping_->Map(parent-1,parent-1);
          if(iam==iOwner){
          std::shared_ptr<GenericTask> pFUtask(new SparseTask);
          SparseTask & FUtask = *(SparseTask*)pFUtask.get();

            FUtask.meta.resize(2*sizeof(Int)+sizeof(Solve::op_type));
            Int * meta = reinterpret_cast<Int*>(FUtask.meta.data());
            meta[0] = I;
            meta[1] = parent;
            Solve::op_type & type = *reinterpret_cast<Solve::op_type*>(&meta[2]);
            type = Solve::op_type::FU;

            Int childOwner = this->Mapping_->Map(I-1,I-1);
            if(childOwner==iam){
              FUtask.local_deps_cnt = 1;
            }
            else{
              FUtask.remote_deps_cnt = 1;
            }

            FUtask.execute = [&,this,I,parent,pFUtask] (){
              SparseTask & FUtask = *(SparseTask*)pFUtask.get();

              Int src = I;
              Int tgt = parent;
              Int tgt_local = snodeLocalIndex(tgt); 
              auto contrib = std::dynamic_pointer_cast< SuperNode<T,UpcxxAllocator> >(Contributions2_[tgt_local-1]);

              Int iOwner = this->Mapping_->Map(src-1,src-1);
              if(iOwner==iam){
                //local
                Int src_local = snodeLocalIndex(src); 
                auto dist_contrib = std::dynamic_pointer_cast< SuperNode<T,UpcxxAllocator> >(Contributions2_[src_local-1]);
                contrib->forward_update(&*dist_contrib,iOwner,iam);
              }
              else{
                //remote
                assert(FUtask.data.size()==1);
                IncomingMessage * msgPtr = *FUtask.data.begin();
                assert(msgPtr->IsDone());
                char* dataPtr = msgPtr->GetLocalPtr();
                auto dist_contrib = CreateSuperNode(options_.decomposition,dataPtr,msgPtr->Size());
                dist_contrib->InitIdxToBlk();
                contrib->forward_update(&*dist_contrib,iOwner,iam);
                delete dist_contrib;
                delete msgPtr;
              }

              std::stringstream sstr;
              sstr<<tgt<<"_"<<tgt<<"_"<<(Int)Solve::op_type::FUC;
              auto id = hash_fn(sstr.str());
              auto taskit = graph.find_task(id);
                //log_task(taskit);
              dec_ref(taskit,1,0);
            };

            std::stringstream sstr;
            sstr<<I<<"_"<<parent<<"_"<<(Int)type;
            FUtask.id = hash_fn(sstr.str());

            graph.addTask(pFUtask);
          }
        }
        //Add the BU task
        {
          Int iOwner = Mapping_->Map(I-1,I-1);
          if(iam==iOwner){
          std::shared_ptr<GenericTask> pBUtask(new SparseTask);
          SparseTask & BUtask = *(SparseTask*)pBUtask.get();

            BUtask.meta.resize(2*sizeof(Int)+sizeof(Solve::op_type));
            Int * meta = reinterpret_cast<Int*>(BUtask.meta.data());
            meta[0] = parent;
            meta[1] = I;
            Solve::op_type & type = *reinterpret_cast<Solve::op_type*>(&meta[2]);
            type = Solve::op_type::BU;

            Int parentOwner = this->Mapping_->Map(parent-1,parent-1);
            if(parentOwner==iam){
              BUtask.local_deps_cnt = 1;
            }
            else{
              BUtask.remote_deps_cnt = 1;
            }

            BUtask.execute = [&,this,I,parent,pBUtask] () {
            SparseTask & BUtask = *(SparseTask*)pBUtask.get();
              Int src = parent;
              Int tgt = I;
              Int tgt_local = snodeLocalIndex(tgt); 
              auto contrib = std::dynamic_pointer_cast< SuperNode<T,UpcxxAllocator> >(Contributions2_[tgt_local-1]);

              Int iOwner = this->Mapping_->Map(src-1,src-1);
              if(iOwner==iam){
                //local
                Int src_local = snodeLocalIndex(src); 
                auto dist_contrib = std::dynamic_pointer_cast< SuperNode<T,UpcxxAllocator> >(Contributions2_[src_local-1]);
                contrib->back_update(&*dist_contrib);
              }
              else{
                //remote
                assert(BUtask.data.size()==1);
                IncomingMessage * msgPtr = *BUtask.data.begin();
                assert(msgPtr->IsDone());
                char* dataPtr = msgPtr->GetLocalPtr();
                auto dist_contrib = CreateSuperNode(options_.decomposition,dataPtr,msgPtr->Size());
                dist_contrib->InitIdxToBlk();
                contrib->back_update(&*dist_contrib);
                delete dist_contrib;

                //do I own another children ? if so, put msgPtr in its data and decrement remote_deps
                bool child_found = false;
                Int parent = src;
                if(parent!=0){

                  Int children_count = rem_children[parent-1];
                  Int child = src -1 ;
                  while(children_count>0 && child>0){
                    if(SupETree.PostParent(child-1)==src){
                      Int iTarget = this->Mapping_->Map(child-1,child-1);
                      if(iTarget==iam){
                        children_count--;

                        //if(child!=tgt){
                        std::stringstream sstr;
                        sstr<<src<<"_"<<child<<"_"<<(Int)Solve::op_type::BU;
                        auto id = hash_fn(sstr.str());
                        auto taskit = graph.find_task(id);

                        if(child==tgt){
                          bassert(taskit==graph.tasks_.end());
                        }

                        if(taskit!=graph.tasks_.end()){
                          child_found = true;
                          msgPtr->meta.id = id;
                          taskit->second->data.push_back(msgPtr);
                //log_task(taskit);
                          dec_ref(taskit,0,1);
                          BUtask.data.clear();
                          break;
                        }
                        //}
                      }
                    }
                    child--;
                  }
                }

                if(!child_found){
                  //delete otherwise
                  delete msgPtr;
                }
              }

              std::stringstream sstr;
              sstr<<tgt<<"_"<<tgt<<"_"<<(Int)Solve::op_type::BUC;
              auto id = hash_fn(sstr.str());
              auto taskit = graph.find_task(id);
                //log_task(taskit);
              dec_ref(taskit,1,0);
            };

            std::stringstream sstr;
            sstr<<parent<<"_"<<I<<"_"<<(Int)type;
            BUtask.id = hash_fn(sstr.str());

            graph.addTask(pBUtask);
          }
        }
      }


    }

    scheduler->run(CommEnv_->MPI_GetComm(),graph);

  }

  upcxx::async_wait();
  MPI_Barrier(CommEnv_->MPI_GetComm());





}









#endif // _SYMPACK_MATRIX_IMPL_SOLVE_HPP_
