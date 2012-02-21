/*
 * AssembleBEMMatrixBlock.cc -- libscuff routine for assembling a single 
 *                           -- block of the BEM matrix (i.e. the       
 *                           -- interactions of two objects in the geometry)
 *                           --
 *                           -- (cf. 'libscuff Implementation and Technical
 *                           --  Details', section 8.3, 'Structure of the BEM
 *                           --  Matrix.')
 *                           --
 * homer reid                -- 10/2006 -- 10/2011
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

#include <libhmat.h>
#include <libhrutil.h>

#include "libscuff.h"
#include "libscuffInternals.h"

#define II cdouble(0,1)

/***************************************************************/
/***************************************************************/
/***************************************************************/
typedef struct ThreadData
 { 
   ABMBArgStruct *Args;
   int nt, nThread;

 } ThreadData;

/***************************************************************/
/* 'AssembleBMatrixBlockThread'                                */
/***************************************************************/
void *ABMBThread(void *data)
{ 
  /***************************************************************/
  /* extract local copies of fields in argument structure */
  /***************************************************************/
  ThreadData *TD=(ThreadData *)data;
  ABMBArgStruct *Args  = TD->Args;
  RWGGeometry *G       = Args->G;
  RWGObject *Oa        = Args->Oa;
  RWGObject *Ob        = Args->Ob;
  cdouble Omega        = Args->Omega;
  int NumTorqueAxes    = Args->NumTorqueAxes;
  double *GammaMatrix  = Args->GammaMatrix;
  int RowOffset        = Args->RowOffset;
  int ColOffset        = Args->ColOffset;
  int Symmetric        = Args->Symmetric;
  HMatrix *B           = Args->B;
  HMatrix **GradB      = Args->GradB;
  HMatrix **dBdTheta   = Args->dBdTheta;
  double Sign          = Args->Sign;
  cdouble EpsA         = Args->EpsA;
  cdouble EpsB         = Args->EpsB;
  double MuA           = Args->MuA;
  double MuB           = Args->MuB;
  int OaIsPEC          = Args->OaIsPEC;
  int ObIsPEC          = Args->ObIsPEC;

  /*--------------------------------------------------------------*/
  /*- hack to force all threads to run on separate CPU cores     -*/
  /*--------------------------------------------------------------*/
  #ifdef _GNU_SOURCE
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(TD->nt,&cpuset);
  pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
  #endif
  /*--------------------------------------------------------------*/
  /*- end hack ---------------------------------------------------*/
  /*--------------------------------------------------------------*/

  /***************************************************************/
  /* initialize an argument structure to be passed to            */
  /* GetEdgeEdgeInteractions() below                             */
  /***************************************************************/
  GetEEIArgStruct MyGetEEIArgs, *GetEEIArgs=&MyGetEEIArgs;
  InitGetEEIArgs(GetEEIArgs);

  GetEEIArgs->Oa=Oa;
  GetEEIArgs->Ob=Ob;
  GetEEIArgs->NumGradientComponents = GradB ? 3 : 0;
  GetEEIArgs->NumTorqueAxes=NumTorqueAxes;
  GetEEIArgs->GammaMatrix=GammaMatrix;

  /* pointers to arrays inside the structure */
  cdouble *GC=GetEEIArgs->GC;
  cdouble *GradGC=GetEEIArgs->GradGC;
  cdouble *dGCdT=GetEEIArgs->dGCdT;

  /***************************************************************/
  /* precompute the constant prefactors that multiply the        */
  /* integrals returned by GetEdgeEdgeInteractions()             */
  /***************************************************************/
  cdouble kA, kB;
  cdouble PreFac1A, PreFac2A, PreFac3A;
  cdouble PreFac1B, PreFac2B, PreFac3B;

  kA=csqrt2(EpsA*MuA)*Omega;
  PreFac1A = Sign*II*MuA*Omega;
  PreFac2A = -Sign*II*kA;
  PreFac3A = -1.0*Sign*II*EpsA*Omega;

  if (EpsB!=0.0)
   { // note: Sign==1 for all cases in which EpsB is nonzero
     kB=csqrt2(EpsB*MuB)*Omega;
     PreFac1B = II*MuB*Omega;
     PreFac2B = -II*kB;
     PreFac3B = -1.0*II*EpsB*Omega;
   };

  /***************************************************************/
  /* loop over all internal edges on both objects.               */
  /***************************************************************/
  int nea, NEa=Oa->NumEdges;
  int neb, NEb=Ob->NumEdges;
  int X, Y, Mu, nt=0;
  int NumGradientComponents = GradB ? 3 : 0;
  for(nea=0; nea<NEa; nea++)
   for(neb=Symmetric*nea; neb<NEb; neb++)
    { 
      nt++;
      if (nt==TD->nThread) nt=0;
      if (nt!=TD->nt) continue;

      /*--------------------------------------------------------------*/
      /*- contributions of first medium (EpsA, MuA)  -----------------*/
      /*--------------------------------------------------------------*/
      GetEEIArgs->nea  = nea;
      GetEEIArgs->neb  = neb;
      GetEEIArgs->k    = kA;
      GetEdgeEdgeInteractions(GetEEIArgs);

      if ( OaIsPEC && ObIsPEC )
       { 
         X=RowOffset + nea;
         Y=ColOffset + neb;  

         B->SetEntry( X, Y, PreFac1A*GC[0] );

         for(Mu=0; Mu<NumGradientComponents; Mu++)
          GradB[Mu]->SetEntry( X, Y, PreFac1A*GradGC[2*Mu+0]);

         for(Mu=0; Mu<NumTorqueAxes; Mu++)
          dBdTheta[Mu]->SetEntry( X, Y, PreFac1A*dGCdT[2*Mu+0]);
       }
      else if ( OaIsPEC && !ObIsPEC )
       { 
         X=RowOffset + nea;
         Y=ColOffset + 2*neb;  

         B->SetEntry( X, Y,   PreFac1A*GC[0] );
         B->SetEntry( X, Y+1, PreFac2A*GC[1] );

         for(Mu=0; Mu<NumGradientComponents; Mu++)
          { GradB[Mu]->SetEntry( X, Y,   PreFac1A*GradGC[2*Mu+0]);
            GradB[Mu]->SetEntry( X, Y+1, PreFac2A*GradGC[2*Mu+1]);
          };

         for(Mu=0; Mu<NumTorqueAxes; Mu++)
          { dBdTheta[Mu]->SetEntry( X, Y, PreFac1A*dGCdT[2*Mu+0]);
            dBdTheta[Mu]->SetEntry( X, Y+1, PreFac2A*dGCdT[2*Mu+0]);
          };
       }
      else if ( !OaIsPEC && ObIsPEC )
       {
         X=RowOffset + 2*nea;
         Y=ColOffset + neb;  

         B->SetEntry( X,   Y, PreFac1A*GC[0] );
         B->SetEntry( X+1, Y, PreFac2A*GC[1] );

         for(Mu=0; Mu<NumGradientComponents; Mu++)
          { GradB[Mu]->SetEntry( X, Y,   PreFac1A*GradGC[2*Mu+0]);
            GradB[Mu]->SetEntry( X+1, Y, PreFac2A*GradGC[2*Mu+1]);
          };

         for(Mu=0; Mu<NumTorqueAxes; Mu++)
          { dBdTheta[Mu]->SetEntry( X, Y,   PreFac1A*dGCdT[2*Mu+0]);
            dBdTheta[Mu]->SetEntry( X+1, Y, PreFac2A*dGCdT[2*Mu+1]);
          };
       }
      else if ( !OaIsPEC && !ObIsPEC )
       { 
         X=RowOffset + 2*nea;
         Y=ColOffset + 2*neb;  

         B->SetEntry( X, Y,   PreFac1A*GC[0]);
         B->SetEntry( X, Y+1, PreFac2A*GC[1]);
         if ( !Symmetric || (nea!=neb) )
          B->SetEntry( X+1, Y, PreFac2A*GC[1]);
         B->SetEntry( X+1, Y+1, PreFac3A*GC[0]);

         for(Mu=0; Mu<NumGradientComponents; Mu++)
          { 
            GradB[Mu]->SetEntry( X, Y,   PreFac1A*GradGC[2*Mu+0]);
            GradB[Mu]->SetEntry( X, Y+1, PreFac2A*GradGC[2*Mu+1]);
            if ( !Symmetric || (nea!=neb) )
             GradB[Mu]->SetEntry( X+1, Y, PreFac2A*GradGC[2*Mu+1]);
            GradB[Mu]->SetEntry( X+1, Y+1, PreFac3A*GradGC[2*Mu+0]);
          };

         for(Mu=0; Mu<NumTorqueAxes; Mu++)
          { 
            dBdTheta[Mu]->SetEntry( X, Y,   PreFac1A*dGCdT[2*Mu+0]);
            dBdTheta[Mu]->SetEntry( X, Y+1, PreFac2A*dGCdT[2*Mu+1]);
            if ( !Symmetric || (nea!=neb) )
             dBdTheta[Mu]->SetEntry( X+1, Y, PreFac2A*dGCdT[2*Mu+1]);
            dBdTheta[Mu]->SetEntry( X+1, Y+1, PreFac3A*dGCdT[2*Mu+0]);
          };

       }; // if ( OaIsPEC && ObIsPEC ) ... else ... 

      /*--------------------------------------------------------------*/
      /*- contributions of second medium if objects are identical.    */
      /*- note this case we already know we are in the fourth case    */
      /*- of the above if...else statement.                           */
      /*--------------------------------------------------------------*/
      if (EpsB!=0.0)
       { 
         GetEEIArgs->k = kB;
         GetEdgeEdgeInteractions(GetEEIArgs);

         X=RowOffset + 2*nea;
         Y=ColOffset + 2*neb;

         B->AddEntry( X, Y,   PreFac1B*GC[0]);
         B->AddEntry( X, Y+1, PreFac2B*GC[1]);
         if ( !Symmetric || (nea!=neb) )
          B->AddEntry( X+1, Y, PreFac2B*GC[1]);
         B->AddEntry( X+1, Y+1, PreFac3B*GC[0]);

         for(Mu=0; Mu<NumGradientComponents; Mu++)
          { 
            GradB[Mu]->AddEntry( X, Y,   PreFac1B*GradGC[2*Mu+0]);
            GradB[Mu]->AddEntry( X, Y+1, PreFac2B*GradGC[2*Mu+1]);
            if ( !Symmetric || (nea!=neb) )
             GradB[Mu]->AddEntry( X+1, Y, PreFac2B*GradGC[2*Mu+1]);
            GradB[Mu]->AddEntry( X+1, Y+1, PreFac3B*GradGC[2*Mu+0]);
          };

         for(Mu=0; Mu<NumTorqueAxes; Mu++)
          { 
            dBdTheta[Mu]->AddEntry( X, Y,   PreFac1B*dGCdT[2*Mu+0]);
            dBdTheta[Mu]->AddEntry( X, Y+1, PreFac2B*dGCdT[2*Mu+1]);
            if ( !Symmetric || (nea!=neb) )
             dBdTheta[Mu]->AddEntry( X+1, Y, PreFac2B*dGCdT[2*Mu+1]);
            dBdTheta[Mu]->AddEntry( X+1, Y+1, PreFac3B*dGCdT[2*Mu+0]);
          };
       }; // if (EpsB!=0.0)

    }; // for(nea=0; nea<NEa; nea++), for(neb=Symmetric*nea; neb<NEb; neb++) ... 

  return 0;

}

/***************************************************************/  
/***************************************************************/  
/***************************************************************/
void AssembleBEMMatrixBlock(ABMBArgStruct *Args)
{ 
  /***************************************************************/
  /* look at the containership relation between the objects to   */
  /* to figure out how to assign values to Sign, EpsA, MuA,      */
  /* EpsB, MuB.                                                  */
  /*                                                             */
  /* note: EpsA, MuA are the material properties of the medium   */
  /*       through which the two objects interact.               */
  /*       if the two objects are identical, then EpsB, MuB are  */
  /*       the material properties of the medium interior to the */
  /*       object; otherwise, EpsB is set to 0.0.                */
  /***************************************************************/
  RWGGeometry *G=Args->G;
  RWGObject *Oa=Args->Oa;
  RWGObject *Ob=Args->Ob;
  cdouble Omega=Args->Omega;
  if ( Ob->ContainingObject == Oa )
   { Args->Sign=-1.0;
     Oa->MP->GetEpsMu(Omega, &(Args->EpsA), &(Args->MuA) );
   }
  else if ( Oa->ContainingObject == Ob )
   { Args->Sign=-1.0;
     Ob->MP->GetEpsMu(Omega, &(Args->EpsA), &(Args->MuA) );
   } 
  else if ( Oa->ContainingObject == Ob->ContainingObject )
   { Args->Sign=1.0;
     if (Oa->ContainingObject==0)
      G->ExteriorMP->GetEpsMu(Omega, &(Args->EpsA), &(Args->MuA) );
     else 
      Oa->ContainingObject->MP->GetEpsMu(Omega, &(Args->EpsA), &(Args->MuA) );
   };

  Args->OaIsPEC = Oa->MP->IsPEC();
  Args->ObIsPEC = Ob->MP->IsPEC();

  if ( !(Args->OaIsPEC) && !(Args->ObIsPEC) && Oa==Ob ) 
   Oa->MP->GetEpsMu(Omega, &(Args->EpsB), &(Args->MuB) );
  else
   Args->EpsB=0.0;

  /***************************************************************/
  /* fire off threads ********************************************/
  /***************************************************************/
  int nt, nThread=Args->nThread;
  ThreadData TDs[nThread], *TD;
  pthread_t Threads[nThread];

  for(nt=0; nt<nThread; nt++)
   { 
     TD=&(TDs[nt]);
     TD->nt=nt;
     TD->nThread=nThread;
     TD->Args=Args;

     pthread_create( &(Threads[nt]), 0, ABMBThread, (void *)TD);
   };

  /***************************************************************/
  /* await thread completion *************************************/
  /***************************************************************/
  for(nt=0; nt<nThread; nt++)
   pthread_join(Threads[nt],0);

}


/***************************************************************/
/* initialize an argument structure for AssembleBEMMatrixBlock.*/
/*                                                             */
/* note: what this routine does is to fill in default values   */
/* for some of the lesser-used fields in the structure, while  */
/* leaving several other fields uninitialized; any caller of   */
/* AssembleBEMMatrixBlock must fill in those remaining fields  */
/* before the call.                                            */
/***************************************************************/
void InitABMBArgs(ABMBArgStruct *Args)
{
  Args->nThread=1;

  Args->NumTorqueAxes=0;
  Args->GammaMatrix;
  
  Args->RowOffset=0;
  Args->ColOffset=0;

  Args->Symmetric=0;

  Args->GradB=0;
  Args->dBdTheta=0;

}

/***************************************************************/
/* the actual API-exposed routine for assembling the BEM matrix*/
/* is pretty simple, and really just calls the routine above   */
/* to do all the dirty work.                                   */
/***************************************************************/
void RWGGeometry::AssembleBEMMatrix(cdouble Omega, int nThread, HMatrix *M)
{ 
  /***************************************************************/
  /* preinitialize an argument structure for the matrix-block    */
  /* assembly routine                                            */
  /***************************************************************/
  ABMBArgStruct MyABMBArgStruct, *Args=&MyABMBArgStruct;

  InitABMBArgs(Args);
  Args->G=this;
  Args->Omega=Omega;
  Args->nThread=nThread;

  Args->NumTorqueAxes=0;
  Args->GammaMatrix=0;

  Args->B=M;
  Args->GradB=0;
  Args->dBdTheta=0;

  if (LogLevel>=SCUFF_TERSELOGGING)
   Log(" Assembling the BEM matrix at Omega=%g+%gi...",real(Omega),imag(Omega));

  /***************************************************************/
  /* loop over all pairs of objects to assemble the diagonal and */
  /* above-diagonal blocks of the matrix                         */
  /***************************************************************/
  int no, nop;
  for(no=0; no<NumObjects; no++)
   for(nop=no; nop<NumObjects; nop++)
    { 
      if (LogLevel>=SCUFF_VERBOSELOGGING)
       Log("  ...(%i,%i) block...",no,nop);

      Args->Oa=Objects[no];
      Args->RowOffset=BFIndexOffset[no];

      Args->Ob=Objects[nop];
      Args->ColOffset=BFIndexOffset[nop];

      Args->Symmetric = (no==nop) ? 1 : 0;

      AssembleBEMMatrixBlock(Args);
    };

  /***************************************************************/
  /* if the matrix uses normal (not packed) storage, fill in its */
  /* below-diagonal blocks. note that the BEM matrix is complex  */
  /* symmetric, not hermitian, so the below-diagonals are equal  */
  /* to the above-diagonals, not to their complex conjugates.    */
  /***************************************************************/
  if (M->StorageType==LHM_NORMAL)
   { int nr, nc;
     for(nr=1; nr<TotalBFs; nr++)
      for(nc=0; nc<nr; nc++)
       M->SetEntry(nr, nc, M->GetEntry(nc, nr) );
   };

}

/***************************************************************/
/***************************************************************/
/***************************************************************/
HMatrix *RWGGeometry::AllocateBEMMatrix(int PureImagFreq)
{
  if (PureImagFreq)
   return new HMatrix(TotalBFs, TotalBFs, LHM_REAL);
  else
   return new HMatrix(TotalBFs, TotalBFs, LHM_COMPLEX);
    
}