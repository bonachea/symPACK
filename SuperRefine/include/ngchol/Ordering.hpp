#ifndef _ORDERING_HPP_ 
#define _ORDERING_HPP_

#include  <stdlib.h>

#include <vector>
#include "ngchol/SparseMatrixStructure.hpp"

using namespace std;

namespace LIBCHOLESKY{
class SparseMatrixStructure;
class Ordering{
  public:
    vector<int> perm;
    vector<int> invp;
    SparseMatrixStructure * pStructure;

    Ordering():pStructure(NULL){};
    
    void SetStructure(SparseMatrixStructure & aGlobal);
    void MMD();
    void AMD();
    void METIS();
    void Compose(vector<int> & invp2);
};

}
#endif // _ORDERING_HPP_
