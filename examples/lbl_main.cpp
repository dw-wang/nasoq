//
// Created by kazem on 8/15/20.
//

#include "linear_solver_wrapper.h"

/*
 * Solving Hx = q
 * H is a sparse matrix stored in CSC format
 * q is a dense array
 */

int main(int argc, char *argv[]){

 /// Declaring input matrices
 size_t sizeH;
 size_t nnzH;
 sizeH = 2;
 nnzH = 2;
 auto *q = new double[sizeH];
 auto *Hp = new int[sizeH+1];
 auto *Hi = new int[nnzH];
 auto *Hx = new double[nnzH];

 q[0] = -4; q[1] = -4;

 Hp[0]=0;Hp[1]=1;Hp[2]=2;
 Hi[0]=0;Hi[1]=1;
 Hx[0]=2;Hx[1]=2;

 auto *H = new nasoq::CSC; H->nzmax = nnzH; H->ncol= H->nrow =sizeH;
 H->p = Hp; H->i = Hi; H->x = Hx; H->stype=-1; H->packed=1;
/// Solving the linear system
 auto *lbl = new nasoq::SolverSettings(H,q);
 lbl->ldl_variant = 4;
 lbl->req_ref_iter = 2;
 lbl->solver_mode = 0;
 lbl->reg_diag = pow(10,-9);
 lbl->symbolic_analysis();
 lbl->numerical_factorization();
 double *x = lbl->solve_only();


 /// Printing results
 // expected x={-2,-2};
 std::cout<<"Solution: ";
 for (int i = 0; i < sizeH; ++i) {
  std::cout<<x[i]<<",";
 }

 delete lbl;
 delete []Hp;
 delete []Hi;
 delete []Hx;
 delete []q;
 delete []x;
 delete H;
 return 0;
}
