//
// Created by kazem on 7/17/17.
//

#ifndef TRIANGOPENMP_TRIANGULAR_H
#define TRIANGOPENMP_TRIANGULAR_H

#include <cassert>
#include "BLAS.h"
#include "mkl.h"

namespace nasoq {
#define MKL_BLAS

/*
 * Forward solve blocked
 */
 int blockedLsolve(int n, size_t *Lp, int *Li, double *Lx, int NNZ,
                   size_t *Li_ptr, int *col2sup, int *sup2col, int supNo, double *x) {
  int p, j;
  double one[2], zero[2];
  one[0] = 1.0;    /* ALPHA for *syrk, *herk, *gemm, and *trsm */
  one[1] = 0.;
  zero[0] = 0.;     /* BETA for *syrk, *herk, and *gemm */
  zero[1] = 0.;
  int ione = 1;
  double *tempVec = new double[n]();
  if (!Lp || !Li || !x) return (0);                     /* check inputs */
  for (int i = 1; i <= supNo; ++i) {// for each supernode
   int curCol = i != 0 ? sup2col[i - 1] : 0;
   int nxtCol = sup2col[i];
   int supWdt = nxtCol - curCol;
   int nSupR = Li_ptr[nxtCol] - Li_ptr[curCol];//row size of supernode
   double *Ltrng = &Lx[Lp[curCol]];//first nnz of current supernode
   //lSolve_dense(nSupR,supWdt,Ltrng,&x[curCol]);
   dlsolve_blas_nonUnit(nSupR, supWdt, Ltrng, &x[curCol]);
   Ltrng = &Lx[Lp[curCol] + supWdt];//first nnz of below diagonal
   //matVector(nSupR,nSupR-supWdt,supWdt,Ltrng,&x[curCol],tempVec);
   dmatvec_blas(nSupR, nSupR - supWdt, supWdt, Ltrng, &x[curCol], tempVec);
   for (int l = Li_ptr[curCol] + supWdt, k = 0; l < Li_ptr[nxtCol]; ++l, ++k) {
    x[Li[l]] -= tempVec[k];
    tempVec[k] = 0;
   }
#if 0
   for (int k = 0; k < 200; ++k) {
             std::cout<<","<<x[k];
         }
         std::cout<<"\n";
#endif
  }
  delete[]tempVec;
  return (1);
 }


/*
 * Backward solve blocked, unit triangular
 */
 int blockedLTsolve(int n, size_t *Lp, int *Li, double *Lx, int NNZ, size_t *Li_ptr,
                    int *col2sup, int *sup2col, int supNo, double *x) {
  int p, j;
  double one[2], zero[2];
  one[0] = 1.0;
  one[1] = 0.;
  zero[0] = 0.;
  zero[1] = 0.;
  double minus_one = -1;
  int ione = 1;
  double *tempVec = new double[n]();
  if (!Lp || !Li || !x) return (0);                     /* check inputs */
  for (int i = supNo; i > 0; --i) {// for each supernode
   int curCol = i != 0 ? sup2col[i - 1] : 0;
   int nxtCol = sup2col[i];
   int supWdt = nxtCol - curCol;
   int nSupR = Li_ptr[nxtCol] - Li_ptr[curCol];//row size of supernode

   double *Ltrng = &Lx[Lp[curCol] + supWdt];//first nnz of below diagonal
   for (int l = 0; l < nSupR - supWdt; ++l) {
    tempVec[l] = x[Li[Li_ptr[curCol] + supWdt + l]];
   }
#ifdef BLAS1  //FIXME
   dmatvec_blas(nSupR,nSupR-supWdt,supWdt,Ltrng,&x[curCol],tempVec);
#endif
#ifdef MKL_BLAS
   int tmpRow = nSupR - supWdt;
   dgemv("T", &tmpRow, &supWdt, &minus_one, Ltrng, &nSupR, tempVec, &ione,
         one, &x[curCol], &ione);
#endif
   Ltrng = &Lx[Lp[curCol]];//first nnz of current supernode
#ifdef BLAS1//FIXME
   dlsolve_blas_nonUnit(nSupR,supWdt,Ltrng,&x[curCol]);//FIXME make it for transpose
#endif
#ifdef MKL_BLAS
   dtrsm("L", "L", "T", "N", &supWdt, &ione, one, Ltrng,
         &nSupR, &x[curCol], &n);
#endif

#if 0
   for (int k = 0; k < 200; ++k) {
             std::cout<<","<<x[k];
         }
         std::cout<<"\n";
#endif
  }
  delete[]tempVec;
  return (1);
 }

/*
 *
 */
 int LeveledBlockedLTsolve_update(int n, size_t *Lp, int *Li, double *Lx,
                                  int NNZ, size_t *Li_ptr,
                                  int *col2sup, int *sup2col,
                                  int supNo, double *x,
                                  int levels, int *levelPtr,
                                  int *levelSet,
                                  int chunk,
                                  bool *marked, double *ws_dbl = NULL) {
  //int chunk = 70;
  double one[2], zero[2];
  one[0] = 1.0;    /* ALPHA for *syrk, *herk, *gemm, and *trsm */
  one[1] = 0.;
  zero[0] = 0.;     /* BETA for *syrk, *herk, and *gemm */
  zero[1] = 0.;
  int ione = 1;
  double minus_one = -1;
  //double *tempVec = new double[n]();
  if (!Lp || !Li || !x) return (0);                     /* check inputs */
  for (int i1 = levels - 1; i1 >= 0; --i1) {
   int li = 0;
#pragma omp parallel private(li)
   {
    //tempVec = new double[n]();
    double *tempVec;
    if (ws_dbl == NULL) {
     tempVec = (double *) calloc(n, sizeof(double));
    } else {//FIXME: the else part is not right
     //std::cout<<"-> "<<omp_get_thread_num()<<"\n";
     tempVec = ws_dbl + omp_get_thread_num() * n;
    }
#pragma omp for \
            schedule(static)
    for (li = levelPtr[i1]; li < levelPtr[i1 + 1]; ++li) {
     int i = levelSet[li];
     if (!marked[i])
      continue;
     int curCol = sup2col[i];
     int nxtCol = sup2col[i + 1];
     int supWdt = nxtCol - curCol;
     int nSupR = Li_ptr[nxtCol] - Li_ptr[curCol];//row size of supernode

     double *Ltrng = &Lx[Lp[curCol] + supWdt];//first nnz of below diagonal
     for (int l = 0; l < nSupR - supWdt; ++l) {
      tempVec[l] = x[Li[Li_ptr[curCol] + supWdt + l]];
     }
#ifdef BLAS1  //FIXME
     dmatvec_blas(nSupR,nSupR-supWdt,supWdt,Ltrng,&x[curCol],tempVec);
#endif
#ifdef MKL_BLAS
     int tmpRow = nSupR - supWdt;
     dgemv("T", &tmpRow, &supWdt, &minus_one, Ltrng, &nSupR, tempVec, &ione,
           one, &x[curCol], &ione);
#endif
     Ltrng = &Lx[Lp[curCol]];//first nnz of current supernode
#ifdef BLAS1//FIXME
     dlsolve_blas_nonUnit(nSupR,supWdt,Ltrng,&x[curCol]);//FIXME make it for transpose
#endif
#ifdef MKL_BLAS
     dtrsm("L", "L", "T", "N", &supWdt, &ione, one, Ltrng,
           &nSupR, &x[curCol], &n);
#endif
     for (int l = 0; l < nSupR - supWdt; ++l) {
      tempVec[l] = 0;
     }
    }
    if (ws_dbl == NULL) {
     free(tempVec);
    }
   }
  }

  return (1);
 }


/*
 * Blocked Serial code
 */
//#define BLAS2
 int blockedPrunedLSolve(int n, int *Lp, int *Li, double *Lx, int NNZ, int *Li_ptr,
                         int *BPSet, int PBSetSize, int *sup2col, int supNo, double *x) {
  int p, j, i;
  double tmp = 0;
  double one[2], zero[2];
  one[0] = 1.0;    /* ALPHA for *syrk, *herk, *gemm, and *trsm */
  one[1] = 0.;
  zero[0] = 0.;     /* BETA for *syrk, *herk, and *gemm */
  zero[1] = 0.;
  int ione = 1;
  double *tempVec = new double[n]();
  if (!Lp || !Li || !x) return (0);                     /* check inputs */
  // for (int i = 2530; i < supNo; ++i) {// for each supernode
  for (int ps = 0; ps < PBSetSize; ++ps) {// for each supernode
   i = BPSet[ps];
   int curCol = i != 0 ? sup2col[i - 1] : 0;
   int nxtCol = sup2col[i];
   int supWdt = nxtCol - curCol;
   int nSupR = Li_ptr[nxtCol] - Li_ptr[curCol];//row size of supernode
   double *Ltrng = &Lx[Lp[curCol]];//first nnz of current supernode
   //lSolve_dense(nSupR,supWdt,Ltrng,&x[curCol]);
#ifdef BLAS1

   dlsolve_blas_nonUnit(nSupR,supWdt,Ltrng,&x[curCol]);

#endif
#ifdef BLAS2
   dtrsm("L", "L", "N", "N", &supWdt,&ione,one,Ltrng,
                &nSupR,&x[curCol],&n);
#endif
   Ltrng = &Lx[Lp[curCol] + supWdt];//first nnz of below diagonal
   //matVector(nSupR,nSupR-supWdt,supWdt,Ltrng,&x[curCol],tempVec);
#ifdef BLAS1

   dmatvec_blas(nSupR,nSupR-supWdt,supWdt,Ltrng,&x[curCol],tempVec);
#endif
#ifdef BLAS2
   int tmpRow=nSupR - supWdt;
      dgemv("N",&tmpRow,&supWdt,one,Ltrng,&nSupR,&x[curCol],&ione,
                zero,tempVec,&ione);
#endif
   for (int l = Li_ptr[curCol] + supWdt, k = 0; l < Li_ptr[nxtCol]; ++l, ++k) {
    x[Li[l]] -= tempVec[k];
    tempVec[k] = 0;
   }
#if 0
   for (int k = 0; k < 200; ++k) {
             std::cout<<","<<x[k];
         }
         std::cout<<"\n";
#endif
  }
  delete[]tempVec;
  return (1);
 }

/*
 * Parallel Blocked
 */

 int leveledBlockedLsolve(int n, size_t *Lp, int *Li, double *Lx, int NNZ, size_t *Li_ptr,
                          int *col2sup, int *sup2col, int supNo, double *x,
                          int levels, int *levelPtr, int *levelSet, int chunk) {
  //int chunk = 70;
  double one[2], zero[2];
  one[0] = 1.0;    /* ALPHA for *syrk, *herk, *gemm, and *trsm */
  one[1] = 0.;
  zero[0] = 0.;     /* BETA for *syrk, *herk, and *gemm */
  zero[1] = 0.;
  int ione = 1;
  //double *tempVec = new double[n]();
  double *tempVec;
  if (!Lp || !Li || !x) return (0);                     /* check inputs */
  for (int l = 0; l < levels; ++l) {
   int li = 0;
#pragma omp parallel private(li, tempVec)
   {
    //tempVec = new double[n]();
    tempVec = (double *) calloc(n, sizeof(double));
#pragma omp for \
            schedule(static)
    for (li = levelPtr[l]; li < levelPtr[l + 1]; ++li) {
     int i = levelSet[li];

     int curCol = sup2col[i];
     int nxtCol = sup2col[i + 1];
     int supWdt = nxtCol - curCol;
     assert(supWdt > 0);
     int nSupR = Li_ptr[nxtCol] - Li_ptr[curCol];//row size of supernode
     double *Ltrng = &Lx[Lp[curCol]];//first nnz of current supernode
     //lSolve_dense_col_sync(nSupR,supWdt,Ltrng,&x[curCol]);
     dlsolve_blas_nonUnit(nSupR, supWdt, Ltrng, &x[curCol]);
     Ltrng = &Lx[Lp[curCol] + supWdt];//first nnz of below diagonal
     //matVector(nSupR,nSupR-supWdt,supWdt,Ltrng,&x[curCol],tempVec);
     dmatvec_blas(nSupR, nSupR - supWdt, supWdt, Ltrng, &x[curCol],
                  tempVec);
//     #pragma omp critical
     for (int l = Li_ptr[curCol] + supWdt, k = 0;
          l < Li_ptr[nxtCol]; ++l, ++k) {
#pragma omp atomic
      x[Li[l]] -= tempVec[k];
      tempVec[k] = 0;
     }
    }
    free(tempVec);
   }
  }

  return (1);
 }

/*
 * Parallel Blocked with masked cols
 */

 int leveledBlockedLsolve_update(int n, size_t *Lp, int *Li, double *Lx,
                                 int NNZ, size_t *Li_ptr,
                                 int *col2sup, int *sup2col,
                                 int supNo, double *x,
                                 int levels, int *levelPtr, int *levelSet,
                                 int chunk, bool *mask, double *ws_dbl = NULL) {
  //int chunk = 70;
  double one[2], zero[2];
  one[0] = 1.0;    /* ALPHA for *syrk, *herk, *gemm, and *trsm */
  one[1] = 0.;
  zero[0] = 0.;     /* BETA for *syrk, *herk, and *gemm */
  zero[1] = 0.;
  int ione = 1;
  //double *tempVec = new double[n]();
  if (!Lp || !Li || !x) return (0);                     /* check inputs */
  for (int l = 0; l < levels; ++l) {
   int li = 0;
#pragma omp parallel private(li)
   {
    double *tempVec;
    if (ws_dbl == NULL) {
     tempVec = (double *) calloc(n, sizeof(double));
    } else {//FIXME: the else part is not right
     //std::cout<<"-> "<<omp_get_thread_num()<<"\n";
     tempVec = ws_dbl + omp_get_thread_num() * n;
    }
#pragma omp for \
            schedule(static)
    for (li = levelPtr[l]; li < levelPtr[l + 1]; ++li) {
     int i = levelSet[li];
     if (!mask[i])
      continue;
     int curCol = sup2col[i];
     int nxtCol = sup2col[i + 1];
     int supWdt = nxtCol - curCol;
     assert(supWdt > 0);
     int nSupR = Li_ptr[nxtCol] - Li_ptr[curCol];//row size of supernode
     double *Ltrng = &Lx[Lp[curCol]];//first nnz of current supernode
     //lSolve_dense_col_sync(nSupR,supWdt,Ltrng,&x[curCol]);
     dlsolve_blas_nonUnit(nSupR, supWdt, Ltrng, &x[curCol]);
     Ltrng = &Lx[Lp[curCol] + supWdt];//first nnz of below diagonal
     //matVector(nSupR,nSupR-supWdt,supWdt,Ltrng,&x[curCol],tempVec);
     dmatvec_blas(nSupR, nSupR - supWdt, supWdt, Ltrng, &x[curCol],
                  tempVec);
//     #pragma omp critical
     for (int l = Li_ptr[curCol] + supWdt, k = 0;
          l < Li_ptr[nxtCol]; ++l, ++k) {
#pragma omp atomic
      x[Li[l]] -= tempVec[k];
      tempVec[k] = 0;
     }

    }
    free(tempVec);
   }
  }

  return (1);
 }


/*
 * Parallel H2 Blocked
 */
//#define MKL_BLAS
 int H2LeveledBlockedLsolve(int n, size_t *Lp, int *Li, double *Lx, int NNZ, size_t
 *Li_ptr,
                            int *col2sup, int *sup2col, int supNo, double *x,
                            int levels, int *levelPtr, int *levelSet,
                            int parts, int *parPtr, int *partition, int chunk) {
  //int chunk = 70;
  double one[2], zero[2];
  one[0] = 1.0;    /* ALPHA for *syrk, *herk, *gemm, and *trsm */
  one[1] = 0.;
  zero[0] = 0.;     /* BETA for *syrk, *herk, and *gemm */
  zero[1] = 0.;
  int ione = 1;
  //double *tempVec = new double[n]();
  double *tempVec;
  if (!Lp || !Li || !x) return (0);                     /* check inputs */
  for (int i1 = 0; i1 < levels; ++i1) {
   int j1 = 0;
#pragma omp parallel //shared(lValues)//private(map, contribs)
   {
#pragma omp  for schedule(static) private(j1, tempVec)
    for (j1 = levelPtr[i1]; j1 < levelPtr[i1 + 1]; ++j1) {
     //tempVec = new double[n]();
     tempVec = (double *) calloc(n, sizeof(double));
     for (int k1 = parPtr[j1]; k1 < parPtr[j1 + 1]; ++k1) {
      int i = partition[k1];
      int curCol = sup2col[i];
      int nxtCol = sup2col[i + 1];
      int supWdt = nxtCol - curCol;
      int nSupR = Li_ptr[nxtCol] - Li_ptr[curCol];//row size of supernode
      double *Ltrng = &Lx[Lp[curCol]];//first nnz of current supernode
      //lSolve_dense(nSupR,supWdt,Ltrng,&x[curCol]);
#ifdef MKL_BLAS
      dtrsm("L", "L", "N", "N", &supWdt, &ione, one, Ltrng,
            &nSupR, &x[curCol], &n);
#else
      dlsolve_blas_nonUnit(nSupR, supWdt, Ltrng, &x[curCol]);
#endif
      Ltrng = &Lx[Lp[curCol] + supWdt];//first nnz of below diagonal
      //matVector(nSupR,nSupR-supWdt,supWdt,Ltrng,&x[curCol],tempVec);
#ifdef MKL_BLAS
      int tmpRow = nSupR - supWdt;
      dgemv("N", &tmpRow, &supWdt, one, Ltrng, &nSupR, &x[curCol], &ione,
            zero, tempVec, &ione);
#else
      dmatvec_blas(nSupR, nSupR - supWdt, supWdt, Ltrng, &x[curCol],
                   tempVec);
#endif
      // #pragma omp critical //FIXME: I don't thnink we need this
      for (int l = Li_ptr[curCol] + supWdt, k = 0;
           l < Li_ptr[nxtCol]; ++l, ++k) {
#pragma omp atomic
       x[Li[l]] -= tempVec[k];
       tempVec[k] = 0;
      }
     }
     free(tempVec);
    }
   }

  }
  return (1);
 }

 int H2LeveledBlockedLsolve_update(int n, size_t *Lp, int *Li, double *Lx, int NNZ, size_t
 *Li_ptr,
                                   int *col2sup, int *sup2col, int supNo, double *x,
                                   int levels, int *levelPtr, int *levelSet,
                                   int parts, int *parPtr, int *partition,
                                   int chunk, bool *mask, double *ws_dbl = NULL) {
  //int chunk = 70;
  double one[2], zero[2];
  one[0] = 1.0;    /* ALPHA for *syrk, *herk, *gemm, and *trsm */
  one[1] = 0.;
  zero[0] = 0.;     /* BETA for *syrk, *herk, and *gemm */
  zero[1] = 0.;
  int ione = 1;

  //double *tempVec = new double[n]();
  //double *tempVec;
  if (!Lp || !Li || !x) return (0);                     /* check inputs */
  for (int i1 = 0; i1 < levels; ++i1) {
   int j1 = 0;
#pragma omp parallel //shared(lValues)//private(map, contribs)
   {
#pragma omp  for schedule(static) private(j1)
    for (j1 = levelPtr[i1]; j1 < levelPtr[i1 + 1]; ++j1) {
     //tempVec = new double[n]();
     double *tempVec;
     if (ws_dbl == NULL) {
      tempVec = (double *) calloc(n, sizeof(double));
     } else {//FIXME: the else part is not right
      //std::cout<<"-> "<<omp_get_thread_num()<<"\n";
      tempVec = ws_dbl + omp_get_thread_num() * n;
     }
     for (int k1 = parPtr[j1]; k1 < parPtr[j1 + 1]; ++k1) {
      int i = partition[k1];
      if (!mask[i])
       continue;
      int curCol = sup2col[i];
      int nxtCol = sup2col[i + 1];
      int supWdt = nxtCol - curCol;
      int nSupR = Li_ptr[nxtCol] - Li_ptr[curCol];//row size of supernode
      double *Ltrng = &Lx[Lp[curCol]];//first nnz of current supernode
      //lSolve_dense(nSupR,supWdt,Ltrng,&x[curCol]);
#ifdef MKL_BLAS
      dtrsm("L", "L", "N", "N", &supWdt, &ione, one, Ltrng,
            &nSupR, &x[curCol], &n);
#else
      dlsolve_blas_nonUnit(nSupR, supWdt, Ltrng, &x[curCol]);
#endif
      Ltrng = &Lx[Lp[curCol] + supWdt];//first nnz of below diagonal
      //matVector(nSupR,nSupR-supWdt,supWdt,Ltrng,&x[curCol],tempVec);
#ifdef MKL_BLAS
      int tmpRow = nSupR - supWdt;
      dgemv("N", &tmpRow, &supWdt, one, Ltrng, &nSupR, &x[curCol], &ione,
            zero, tempVec, &ione);
#else
      dmatvec_blas(nSupR, nSupR - supWdt, supWdt, Ltrng, &x[curCol],
                   tempVec);
#endif
      // #pragma omp critical
      for (int l = Li_ptr[curCol] + supWdt, k = 0;
           l < Li_ptr[nxtCol]; ++l, ++k) {
#pragma omp atomic
       x[Li[l]] -= tempVec[k];
       tempVec[k] = 0;
      }
     }
     if (ws_dbl == NULL) {
      free(tempVec);
     }
    }
   }

  }
  return (1);
 }
/*
 * Backward solve blocked, unit triangular
 */
//#define MKL_BLAS
 int H2LeveledBlockedLTsolve(int n, size_t *Lp, int *Li, double *Lx, int NNZ, size_t *Li_ptr,
                             int *col2sup, int *sup2col, int supNo, double *x,
                             int levels, int *levelPtr, int *levelSet,
                             int parts, int *parPtr, int *partition, int chunk) {
  //int chunk = 70;
  double one[2], zero[2];
  one[0] = 1.0;    /* ALPHA for *syrk, *herk, *gemm, and *trsm */
  one[1] = 0.;
  zero[0] = 0.;     /* BETA for *syrk, *herk, and *gemm */
  zero[1] = 0.;
  int ione = 1;
  double minus_one = -1;
  //double *tempVec = new double[n]();
  double *tempVec;
  if (!Lp || !Li || !x) return (0);                     /* check inputs */
  for (int i1 = levels - 1; i1 >= 0; --i1) {
   int j1 = 0;
#pragma omp parallel //shared(lValues)//private(map, contribs)
   {
#pragma omp  for schedule(static) private(j1, tempVec)
    for (j1 = levelPtr[i1]; j1 < levelPtr[i1 + 1]; ++j1) {
     //tempVec = new double[n]();
     tempVec = (double *) calloc(n, sizeof(double));
     for (int k1 = parPtr[j1 + 1] - 1; k1 >= parPtr[j1]; --k1) {
      int i = partition[k1];
      int curCol = sup2col[i];
      int nxtCol = sup2col[i + 1];
      int supWdt = nxtCol - curCol;
      int nSupR = Li_ptr[nxtCol] - Li_ptr[curCol];//row size of supernode

      double *Ltrng = &Lx[Lp[curCol] + supWdt];//first nnz of below diagonal
      for (int l = 0; l < nSupR - supWdt; ++l) {
       tempVec[l] = x[Li[Li_ptr[curCol] + supWdt + l]];
      }
#ifdef BLAS1  //FIXME
      dmatvec_blas(nSupR,nSupR-supWdt,supWdt,Ltrng,&x[curCol],tempVec);
#endif
#ifdef MKL_BLAS
      int tmpRow = nSupR - supWdt;
      dgemv("T", &tmpRow, &supWdt, &minus_one, Ltrng, &nSupR, tempVec, &ione,
            one, &x[curCol], &ione);
#endif
      Ltrng = &Lx[Lp[curCol]];//first nnz of current supernode
#ifdef BLAS1//FIXME
      dlsolve_blas_nonUnit(nSupR,supWdt,Ltrng,&x[curCol]);//FIXME make it for transpose
#endif
#ifdef MKL_BLAS
      dtrsm("L", "L", "T", "N", &supWdt, &ione, one, Ltrng,
            &nSupR, &x[curCol], &n);
#endif

     }
     free(tempVec);
    }
   }

  }
  return (1);
 }

 int H2LeveledBlockedLTsolve_update(int n, size_t *Lp, int *Li,
                                    double *Lx, int NNZ, size_t *Li_ptr,
                                    int *col2sup, int *sup2col, int supNo,
                                    double *x,
                                    int levels, int *levelPtr, int *levelSet,
                                    int parts, int *parPtr, int *partition,
                                    int chunk, bool *mask, double *ws_dbl = NULL) {
  //int chunk = 70;
  double one[2], zero[2];
  one[0] = 1.0;    /* ALPHA for *syrk, *herk, *gemm, and *trsm */
  one[1] = 0.;
  zero[0] = 0.;     /* BETA for *syrk, *herk, and *gemm */
  zero[1] = 0.;
  int ione = 1;
  double minus_one = -1;
  //double *tempVec = new double[n]();
  //double *tempVec;
  if (!Lp || !Li || !x) return (0);                     /* check inputs */
  for (int i1 = levels - 1; i1 >= 0; --i1) {
   int j1 = 0;
#pragma omp parallel //shared(lValues)//private(map, contribs)
   {
#pragma omp  for schedule(static) private(j1)
    for (j1 = levelPtr[i1]; j1 < levelPtr[i1 + 1]; ++j1) {
     //tempVec = new double[n]();
     //tempVec = (double *) calloc(n , sizeof(double));
     double *tempVec;
     if (ws_dbl == NULL) {
      tempVec = (double *) calloc(n, sizeof(double));
     } else {//FIXME: the else part is not right
      //std::cout<<"-> "<<omp_get_thread_num()<<"\n";
      tempVec = ws_dbl + omp_get_thread_num() * n;
     }
     for (int k1 = parPtr[j1 + 1] - 1; k1 >= parPtr[j1]; --k1) {
      int i = partition[k1];
      if (!mask[i]) {
       continue;
      }
      int curCol = sup2col[i];
      int nxtCol = sup2col[i + 1];
      int supWdt = nxtCol - curCol;
      int nSupR = Li_ptr[nxtCol] - Li_ptr[curCol];//row size of supernode

      double *Ltrng = &Lx[Lp[curCol] + supWdt];//first nnz of below diagonal
      for (int l = 0; l < nSupR - supWdt; ++l) {
       tempVec[l] = x[Li[Li_ptr[curCol] + supWdt + l]];
      }
#ifdef BLAS1  //FIXME
      dmatvec_blas(nSupR,nSupR-supWdt,supWdt,Ltrng,&x[curCol],tempVec);
#endif
#ifdef MKL_BLAS
      int tmpRow = nSupR - supWdt;
      dgemv("T", &tmpRow, &supWdt, &minus_one, Ltrng, &nSupR, tempVec, &ione,
            one, &x[curCol], &ione);
#endif
      Ltrng = &Lx[Lp[curCol]];//first nnz of current supernode
#ifdef BLAS1//FIXME
      dlsolve_blas_nonUnit(nSupR,supWdt,Ltrng,&x[curCol]);//FIXME make it for transpose
#endif
#ifdef MKL_BLAS
      dtrsm("L", "L", "T", "N", &supWdt, &ione, one, Ltrng,
            &nSupR, &x[curCol], &n);
#endif
      for (int l = 0; l < nSupR - supWdt; ++l) {
       tempVec[l] = 0;
      }
     }
     if (ws_dbl == NULL) {
      free(tempVec);
     }

    }
   }

  }
  return (1);
 }



/*
 * Parallel H2 Blocked with peeling
 */
#undef MKL_BLAS

 int H2LeveledBlockedLsolve_Peeled(int n, size_t *Lp, int *Li, double *Lx, int NNZ, size_t
 *Li_ptr,
                                   int *col2sup, int *sup2col, int supNo, double *x,
                                   int levels, int *levelPtr, int *levelSet,
                                   int parts, int *parPtr, int *partition, int chunk, int threads) {
  //int chunk = 70;
  double one[2], zero[2];
  one[0] = 1.0;    /* ALPHA for *syrk, *herk, *gemm, and *trsm */
  one[1] = 0.;
  zero[0] = 0.;     /* BETA for *syrk, *herk, and *gemm */
  zero[1] = 0.;
  int ione = 1;
  //double *tempVec = new double[n]();
  //MKL_Domain_Set_Num_Threads(1, MKL_DOMAIN_BLAS);
  double *tempVec;
  if (!Lp || !Li || !x) return (0);                     /* check inputs */
  for (int i1 = 0; i1 < levels - 1; ++i1) {
   int j1 = 0;
#pragma omp parallel //shared(lValues)//private(map, contribs)
   {
#pragma omp  for schedule(static) private(j1, tempVec)
    for (j1 = levelPtr[i1]; j1 < levelPtr[i1 + 1]; ++j1) {
     //tempVec = new double[n]();
     tempVec = (double *) calloc(n, sizeof(double));
     for (int k1 = parPtr[j1]; k1 < parPtr[j1 + 1]; ++k1) {
      int i = partition[k1];
      int curCol = sup2col[i];
      int nxtCol = sup2col[i + 1];
      int supWdt = nxtCol - curCol;
      int nSupR = Li_ptr[nxtCol] - Li_ptr[curCol];//row size of supernode
      double *Ltrng = &Lx[Lp[curCol]];//first nnz of current supernode
      //lSolve_dense(nSupR,supWdt,Ltrng,&x[curCol]);
#ifdef MKL_BLAS
      dtrsm("L", "L", "N", "N", &supWdt,&ione,one,Ltrng,
            &nSupR,&x[curCol],&n);
#else
      dlsolve_blas_nonUnit(nSupR, supWdt, Ltrng, &x[curCol]);
#endif
      Ltrng = &Lx[Lp[curCol] + supWdt];//first nnz of below diagonal
      //matVector(nSupR,nSupR-supWdt,supWdt,Ltrng,&x[curCol],tempVec);
#ifdef MKL_BLAS
      int tmpRow=nSupR - supWdt;
      dgemv("N",&tmpRow,&supWdt,one,Ltrng,&nSupR,&x[curCol],&ione,
            zero,tempVec,&ione);
#else
      dmatvec_blas(nSupR, nSupR - supWdt, supWdt, Ltrng, &x[curCol],
                   tempVec);
#endif
#pragma omp critical
      for (int l = Li_ptr[curCol] + supWdt, k = 0;
           l < Li_ptr[nxtCol]; ++l, ++k) {
#pragma omp atomic
       x[Li[l]] -= tempVec[k];
       tempVec[k] = 0;
      }
     }
     free(tempVec);
    }
   }

  }
#define MKL_BLAS
  MKL_Domain_Set_Num_Threads(threads, MKL_DOMAIN_BLAS);
  //for (int i1 = 0; i1 < levels ; ++i1) {
  int i1 = levels - 1;
  int j1 = 0;
//#pragma omp parallel //shared(lValues)//private(map, contribs)
//  {
//#pragma omp  for schedule(auto) private(j1, tempVec)
  for (j1 = levelPtr[i1]; j1 < levelPtr[i1 + 1]; ++j1) {
   //tempVec = new double[n]();
   tempVec = (double *) calloc(n, sizeof(double));
   for (int k1 = parPtr[j1]; k1 < parPtr[j1 + 1]; ++k1) {
    int i = partition[k1];
    int curCol = sup2col[i];
    int nxtCol = sup2col[i + 1];
    int supWdt = nxtCol - curCol;
    int nSupR = Li_ptr[nxtCol] - Li_ptr[curCol];//row size of supernode
    double *Ltrng = &Lx[Lp[curCol]];//first nnz of current supernode
    //lSolve_dense(nSupR,supWdt,Ltrng,&x[curCol]);
#ifdef MKL_BLAS
    dtrsm("L", "L", "N", "N", &supWdt, &ione, one, Ltrng,
          &nSupR, &x[curCol], &n);
#else
    dlsolve_blas_nonUnit(nSupR, supWdt, Ltrng, &x[curCol]);
#endif
    Ltrng = &Lx[Lp[curCol] + supWdt];//first nnz of below diagonal
    //matVector(nSupR,nSupR-supWdt,supWdt,Ltrng,&x[curCol],tempVec);
#ifdef MKL_BLAS
    int tmpRow = nSupR - supWdt;
    dgemv("N", &tmpRow, &supWdt, one, Ltrng, &nSupR, &x[curCol], &ione,
          zero, tempVec, &ione);
#else
    dmatvec_blas(nSupR, nSupR - supWdt, supWdt, Ltrng, &x[curCol],
                 tempVec);
#endif
    // #pragma omp critical
    for (int l = Li_ptr[curCol] + supWdt, k = 0;
         l < Li_ptr[nxtCol]; ++l, ++k) {
//#pragma omp atomic
     x[Li[l]] -= tempVec[k];
     tempVec[k] = 0;
    }
   }
   free(tempVec);
  }
//  }

  //}
  return (1);
 }
}
#endif //TRIANGOPENMP_TRIANGULAR_H
