// clang-format off
/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   https://www.lammps.org/, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

#include "pair_inv_poly.h"

#include <cmath>
#include "atom.h"
#include "force.h"
#include "comm.h"
#include "neigh_list.h"
#include "memory.h"
#include "error.h"


using namespace LAMMPS_NS;

/* ---------------------------------------------------------------------- */

PairInvPoly::PairInvPoly(LAMMPS *lmp) : Pair(lmp)
{
  writedata = 1;
}

/* ---------------------------------------------------------------------- */

PairInvPoly::~PairInvPoly()
{
  if (copymode) return;

  if (allocated) {
    memory->destroy(setflag);
    memory->destroy(cutsq);

    memory->destroy(cut);
    memory->destroy(sigma);
    memory->destroy(a2);
    memory->destroy(a4);
    memory->destroy(a6);
    memory->destroy(a8);
    memory->destroy(a10);
    memory->destroy(a12);
    memory->destroy(offset);
  }
}

/* ---------------------------------------------------------------------- */

void PairInvPoly::compute(int eflag, int vflag)
{
  int i,j,ii,jj,inum,jnum,itype,jtype;
  double xtmp,ytmp,ztmp,delx,dely,delz,evdwl,fpair;
  double rsq,r2inv,r4inv,r6inv,r8inv,r10inv,r12inv,forceinvpoly,factor;
  int *ilist,*jlist,*numneigh,**firstneigh;

  evdwl = 0.0;
  ev_init(eflag,vflag);

  double **x = atom->x;
  double **f = atom->f;
  int *type = atom->type;
  int nlocal = atom->nlocal;
  double *special_lj = force->special_lj;
  int newton_pair = force->newton_pair;

  inum = list->inum;
  ilist = list->ilist;
  numneigh = list->numneigh;
  firstneigh = list->firstneigh;

  // loop over neighbors of my atoms

  for (ii = 0; ii < inum; ii++) {
    i = ilist[ii];
    xtmp = x[i][0];
    ytmp = x[i][1];
    ztmp = x[i][2];
    itype = type[i];
    jlist = firstneigh[i];
    jnum = numneigh[i];

    for (jj = 0; jj < jnum; jj++) {
      j = jlist[jj];
      factor = special_lj[sbmask(j)];
      j &= NEIGHMASK;

      delx = xtmp - x[j][0];
      dely = ytmp - x[j][1];
      delz = ztmp - x[j][2];
      rsq = delx*delx + dely*dely + delz*delz;
      jtype = type[j];

      if (rsq < cutsq[itype][jtype]) {
	//compute factors of r
	r2inv  = 1.0/rsq;
	r4inv  = r2inv  * r2inv;
	r6inv  = r4inv  * r2inv;
	r8inv  = r6inv  * r2inv;
	r10inv = r8inv  * r2inv;
	r12inv = r10inv * r2inv;
	/*
	  NOTE 
	  neither forceinvpoly nor fpair represent the full radial force -dE(r)/dr
	  but fpair * r_vec = fpair * [delx dely delz] = -dE(r)/dr * r_vec_norm (r_vec is vector distance between two atoms, r_vec_norm is the normalized r_vec)
	  this formulations helps to reduce floating point operations (especially the square root) for even polynomial potentials
	*/
	//compute the negative derivative of E(r), but do not reduce powers of r (so forceinvpoly is equal to: -(dE(r)/dr) * r)
	forceinvpoly = dinv_poly2[itype][jtype]*r2inv + dinv_poly4[itype][jtype]*r4inv + dinv_poly6[itype][jtype]*r6inv + dinv_poly8[itype][jtype]*r8inv + dinv_poly10[itype][jtype]*r10inv + dinv_poly12[itype][jtype]*r12inv;
	//include factor for excluding bonded atoms, and inlclude 1/(r**2) term; fpair = -(dE(r)/r)*(1/r)
	fpair=factor * forceinvpoly * r2inv; 
	  

        f[i][0] += delx*fpair;
        f[i][1] += dely*fpair;
        f[i][2] += delz*fpair;
        if (newton_pair || j < nlocal) {
          f[j][0] -= delx*fpair;
          f[j][1] -= dely*fpair;
          f[j][2] -= delz*fpair;
        }

        if (eflag) {
	  evdwl = inv_poly2[itype][jtype]*r2inv + inv_poly4[itype][jtype]*r4inv + inv_poly6[itype][jtype]*r6inv + inv_poly8[itype][jtype]*r8inv + inv_poly10[itype][jtype]*r10inv + inv_poly12[itype][jtype]*r12inv - offset[itype][jtype];
          evdwl *= factor;
        }

        if (evflag) ev_tally(i,j,nlocal,newton_pair,
                             evdwl,0.0,fpair,delx,dely,delz);
      }
    }
  }

  if (vflag_fdotr) virial_fdotr_compute();
}

/* ----------------------------------------------------------------------
   allocate all arrays
------------------------------------------------------------------------- */

void PairInvPoly::allocate()
{
  allocated = 1;
  int n = atom->ntypes;

  memory->create(setflag,n+1,n+1,"pair:setflag");
  for (int i = 1; i <= n; i++)
    for (int j = i; j <= n; j++)
      setflag[i][j] = 0;

  memory->create(cutsq,n+1,n+1,"pair:cutsq");

  //allocate memory for defining coefficient tables
  memory->create(cut,n+1,n+1,"pair:cut");
  memory->create(sigma,n+1,n+1,"pair:sigma");
  memory->create(a2,n+1,n+1,"pair:a2");
  memory->create(a4,n+1,n+1,"pair:a4");
  memory->create(a6,n+1,n+1,"pair:a6");
  memory->create(a8,n+1,n+1,"pair:a8");
  memory->create(a10,n+1,n+1,"pair:a10");
  memory->create(a12,n+1,n+1,"pair:a12");

  //and for precomputed quantity tables
  memory->create(inv_poly2,n+1,n+1,"pair:inv_poly2"); //precomputed parts of potential
  memory->create(inv_poly4,n+1,n+1,"pair:inv_poly4");
  memory->create(inv_poly6,n+1,n+1,"pair:inv_poly6");
  memory->create(inv_poly8,n+1,n+1,"pair:inv_poly8");
  memory->create(inv_poly10,n+1,n+1,"pair:inv_poly10");
  memory->create(inv_poly12,n+1,n+1,"pair:inv_poly12");
  memory->create(dinv_poly2,n+1,n+1,"pair:dinv_poly2"); //precomputed parts of force
  memory->create(dinv_poly4,n+1,n+1,"pair:dinv_poly4");
  memory->create(dinv_poly6,n+1,n+1,"pair:dinv_poly6");
  memory->create(dinv_poly8,n+1,n+1,"pair:dinv_poly8");
  memory->create(dinv_poly10,n+1,n+1,"pair:dinv_poly10");
  memory->create(dinv_poly12,n+1,n+1,"pair:dinv_poly12");
  memory->create(offset,n+1,n+1,"pair:offset");

}

/* ----------------------------------------------------------------------
   global settings
------------------------------------------------------------------------- */

void PairInvPoly::settings(int narg, char **arg)
{
  if (narg != 1) error->all(FLERR,"Illegal pair_style command");

  cut_global = utils::numeric(FLERR,arg[0],false,lmp);

  // reset cutoffs that have been explicitly set

  if (allocated) {
    int i,j;
    for (i = 1; i <= atom->ntypes; i++)
      for (j = i; j <= atom->ntypes; j++)
        if (setflag[i][j]) cut[i][j] = cut_global;
  }
}

/* ----------------------------------------------------------------------
   set coeffs for one or more type pairs
------------------------------------------------------------------------- */

void PairInvPoly::coeff(int narg, char **arg)
{
  /*
    **ARGS:
    typei typej sigma a2 a4 a6 a8 a10 a12 rcut
    ?
  */
  
  if (narg < 9 || narg > 10)
    error->all(FLERR,"Incorrect args for pair coefficients");
  if (!allocated) allocate();

  //read i,j atom types
  int ilo,ihi,jlo,jhi;
  utils::bounds(FLERR,arg[0],1,atom->ntypes,ilo,ihi,error);
  utils::bounds(FLERR,arg[1],1,atom->ntypes,jlo,jhi,error);

  //read characteristic length
  double sigma_one = utils::numeric(FLERR,arg[2],false,lmp);

  //read coefficients of inverse polynomial from **args
  double a2_one = utils::numeric(FLERR,arg[3],false,lmp);
  double a4_one = utils::numeric(FLERR,arg[4],false,lmp);
  double a6_one = utils::numeric(FLERR,arg[5],false,lmp);
  double a8_one = utils::numeric(FLERR,arg[6],false,lmp);
  double a10_one = utils::numeric(FLERR,arg[7],false,lmp);
  double a12_one = utils::numeric(FLERR,arg[8],false,lmp);

  //if i,j cutoff radius not specified, assign it global cutoff radius
  double cut_one = cut_global;
  if (narg == 10) cut_one = utils::numeric(FLERR,arg[9],false,lmp);

  //fill "interaction tables" specifying how atom types interact
  //"tables" exist for all parameters of the potential (inlcuding cutoff)
  int count = 0;
  for (int i = ilo; i <= ihi; i++) {
    for (int j = MAX(jlo,i); j <= jhi; j++) {
      sigma[i][j] = sigma_one;
      cut[i][j] = cut_one;
      a2[i][j] = a2_one; 
      a4[i][j] = a4_one;
      a6[i][j] = a6_one;
      a8[i][j] = a8_one;
      a10[i][j] = a10_one;
      a12[i][j] = a12_one;
      setflag[i][j] = 1;
      count++;
    }
  }

  if (count == 0) error->all(FLERR,"Incorrect args for pair coefficients");
}

/* ----------------------------------------------------------------------
   init for one type pair i,j and corresponding j,i
------------------------------------------------------------------------- */

double PairInvPoly::init_one(int i, int j)
{
  //assign mixed parameters if not set
  if (setflag[i][j] == 0) {
    sigma[i][j] = mix_distance(sigma[i][i],sigma[j][j]);
    cut[i][j] = mix_distance(cut[i][i],cut[j][j]);
  }

  //precalculate constant parts of potential once, since they are r-independent
  inv_poly2[i][j]  = a2[i][j]  * pow(sigma[i][j],2.0);
  inv_poly4[i][j]  = a4[i][j]  * pow(sigma[i][j],4.0);
  inv_poly6[i][j]  = a6[i][j]  * pow(sigma[i][j],6.0);
  inv_poly8[i][j]  = a8[i][j]  * pow(sigma[i][j],8.0);
  inv_poly10[i][j] = a10[i][j] * pow(sigma[i][j],10.0);
  inv_poly12[i][j] = a12[i][j] * pow(sigma[i][j],12.0);

  //precalculate constant parts of positive first derivative (for force)
  dinv_poly2[i][j]  = 2  * a2[i][j]  * pow(sigma[i][j],2.0);
  dinv_poly4[i][j]  = 4  * a4[i][j]  * pow(sigma[i][j],4.0);
  dinv_poly6[i][j]  = 6  * a6[i][j]  * pow(sigma[i][j],6.0);
  dinv_poly8[i][j]  = 8  * a8[i][j]  * pow(sigma[i][j],8.0);
  dinv_poly10[i][j] = 10 * a10[i][j] * pow(sigma[i][j],10.0);
  dinv_poly12[i][j] = 12 * a12[i][j] * pow(sigma[i][j],12.0);

  //precalculate constants parts of derivative once
  

  if (offset_flag && (cut[i][j] > 0.0)) {
    double ratio = sigma[i][j] / cut[i][j];
    offset[i][j] = pow(ratio,2.0) + pow(ratio,4.0) + pow(ratio,6.0) + pow(ratio,8.0) + pow(ratio,10.0) + pow(ratio,12.0); //THIS LINE IS A TOTAL GUESS BASED ON pair_lj_cut.cpp
  } else offset[i][j] = 0.0;

  //symmetrize interaction
  inv_poly2[j][i]  = inv_poly2[i][j];
  inv_poly4[j][i]  = inv_poly4[i][j];
  inv_poly6[j][i]  = inv_poly6[i][j];
  inv_poly8[j][i]  = inv_poly8[i][j];
  inv_poly10[j][i] = inv_poly10[i][j];
  inv_poly12[j][i] = inv_poly12[i][j];

  dinv_poly2[j][i]  = dinv_poly2[i][j];
  dinv_poly4[j][i]  = dinv_poly4[i][j];
  dinv_poly6[j][i]  = dinv_poly6[i][j];
  dinv_poly8[j][i]  = dinv_poly8[i][j];
  dinv_poly10[j][i] = dinv_poly10[i][j];
  dinv_poly12[j][i] = dinv_poly12[i][j];

  offset[j][i] = offset[i][j];

  return cut[i][j];
}

/* ----------------------------------------------------------------------
   proc 0 writes to restart file
------------------------------------------------------------------------- */

void PairInvPoly::write_restart(FILE *fp)
{
  write_restart_settings(fp);

  int i,j;
  for (i = 1; i <= atom->ntypes; i++)
    for (j = i; j <= atom->ntypes; j++) {
      fwrite(&setflag[i][j],sizeof(int),1,fp);
      if (setflag[i][j]) {
        fwrite(&sigma[i][j],sizeof(double),1,fp);
        fwrite(&cut[i][j],sizeof(double),1,fp);
      }
    }
}

/* ----------------------------------------------------------------------
   proc 0 reads from restart file, bcasts
------------------------------------------------------------------------- */

void PairInvPoly::read_restart(FILE *fp)
{
  read_restart_settings(fp);

  allocate();

  int i,j;
  int me = comm->me;
  for (i = 1; i <= atom->ntypes; i++)
    for (j = i; j <= atom->ntypes; j++) {
      if (me == 0) utils::sfread(FLERR,&setflag[i][j],sizeof(int),1,fp,nullptr,error);
      MPI_Bcast(&setflag[i][j],1,MPI_INT,0,world);
      if (setflag[i][j]) {
        if (me == 0) {
          utils::sfread(FLERR,&sigma[i][j],sizeof(double),1,fp,nullptr,error);
          utils::sfread(FLERR,&a2[i][j],sizeof(double),1,fp,nullptr,error);
          utils::sfread(FLERR,&a4[i][j],sizeof(double),1,fp,nullptr,error);
          utils::sfread(FLERR,&a6[i][j],sizeof(double),1,fp,nullptr,error);
          utils::sfread(FLERR,&a8[i][j],sizeof(double),1,fp,nullptr,error);
          utils::sfread(FLERR,&a10[i][j],sizeof(double),1,fp,nullptr,error);
          utils::sfread(FLERR,&a12[i][j],sizeof(double),1,fp,nullptr,error);
          utils::sfread(FLERR,&cut[i][j],sizeof(double),1,fp,nullptr,error);
        }
        MPI_Bcast(&sigma[i][j],1,MPI_DOUBLE,0,world);
        MPI_Bcast(&a2[i][j],1,MPI_DOUBLE,0,world);
        MPI_Bcast(&a4[i][j],1,MPI_DOUBLE,0,world);
        MPI_Bcast(&a6[i][j],1,MPI_DOUBLE,0,world);
        MPI_Bcast(&a8[i][j],1,MPI_DOUBLE,0,world);
        MPI_Bcast(&a10[i][j],1,MPI_DOUBLE,0,world);
        MPI_Bcast(&a12[i][j],1,MPI_DOUBLE,0,world);
        MPI_Bcast(&cut[i][j],1,MPI_DOUBLE,0,world);
      }
    }
}

/* ----------------------------------------------------------------------
   proc 0 writes to restart file
------------------------------------------------------------------------- */

void PairInvPoly::write_restart_settings(FILE *fp)
{
  fwrite(&cut_global,sizeof(double),1,fp);
  fwrite(&offset_flag,sizeof(int),1,fp);
  fwrite(&mix_flag,sizeof(int),1,fp);
}

/* ----------------------------------------------------------------------
   proc 0 reads from restart file, bcasts
------------------------------------------------------------------------- */

void PairInvPoly::read_restart_settings(FILE *fp)
{
  if (comm->me == 0) {
    utils::sfread(FLERR,&cut_global,sizeof(double),1,fp,nullptr,error);
    utils::sfread(FLERR,&offset_flag,sizeof(int),1,fp,nullptr,error);
    utils::sfread(FLERR,&mix_flag,sizeof(int),1,fp,nullptr,error);
  }
  
  MPI_Bcast(&cut_global,1,MPI_DOUBLE,0,world);
  MPI_Bcast(&offset_flag,1,MPI_INT,0,world);
  MPI_Bcast(&mix_flag,1,MPI_INT,0,world);
}

/* ----------------------------------------------------------------------
   proc 0 writes to data file
------------------------------------------------------------------------- */

void PairInvPoly::write_data(FILE *fp)
{
  for (int i = 1; i <= atom->ntypes; i++)
    //only write same atom type interactions, no cutoff radius
    fprintf(fp,"%d %g %g %g %g %g %g %g\n",i,sigma[i][i],a2[i][i],a4[i][i],a6[i][i],a8[i][i],a10[i][i],a12[i][i]);
}

/* ----------------------------------------------------------------------
   proc 0 writes all pairs to data file
------------------------------------------------------------------------- */

void PairInvPoly::write_data_all(FILE *fp)
{
  for (int i = 1; i <= atom->ntypes; i++)
    for (int j = i; j <= atom->ntypes; j++)
      //write all interactions (including different atom types), all info
      fprintf(fp,"%d %d %g %g %g %g %g %g %g %g\n",i,j,sigma[i][j],a2[i][j],a4[i][j],a6[i][j],a8[i][j],a10[i][j],a12[i][j],cut[i][j]);
}

/* ---------------------------------------------------------------------- */

double PairInvPoly::single(int /*i*/, int /*j*/, int itype, int jtype, double rsq,
                          double /*factor_coul*/, double factor_lj,
                          double &fforce)
{
  double r2inv,r4inv,r6inv,r8inv,r10inv,r12inv,forceinvpoly,phi,factor;

  factor = factor_lj; //do away with LJ naming

  r2inv = 1.0/rsq;
  r4inv = r2inv * r2inv;
  r6inv= r4inv * r2inv;
  r8inv= r6inv * r2inv;
  r10inv= r8inv * r2inv;
  r12inv= r10inv * r2inv;

  //forces, these are nonobvious and not necessarily equal to the analtyical radial force, see compute() for full comments
  forceinvpoly = dinv_poly2[itype][jtype]*r2inv + dinv_poly4[itype][jtype]*r4inv + dinv_poly6[itype][jtype]*r6inv + dinv_poly8[itype][jtype]*r8inv + dinv_poly10[itype][jtype]*r10inv + dinv_poly12[itype][jtype]*r12inv;
  fforce = factor * forceinvpoly * r2inv; 

  //offset potential
  phi = inv_poly2[itype][jtype]*r2inv + inv_poly4[itype][jtype]*r4inv + inv_poly6[itype][jtype]*r6inv + inv_poly8[itype][jtype]*r8inv + inv_poly10[itype][jtype]*r10inv + inv_poly12[itype][jtype]*r12inv - offset[itype][jtype];
  return factor*phi;
}