/* -*- c++ -*- ----------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   https://www.lammps.org/, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

#ifdef PAIR_CLASS
// clang-format off
PairStyle(inv/poly,PairInvPoly);
// clang-format on
#else

#ifndef LMP_PAIR_INV_POLY_H
#define LMP_PAIR_INV_POLY_H

#include "pair.h"

namespace LAMMPS_NS {

class PairInvPoly : public Pair {
 public:
  PairInvPoly(class LAMMPS *);
  virtual ~PairInvPoly();
  virtual void compute(int, int);
  void settings(int, char **);
  void coeff(int, char **);
  virtual double init_one(int, int);
  void write_restart(FILE *);
  void read_restart(FILE *);
  void write_restart_settings(FILE *);
  void read_restart_settings(FILE *);
  void write_data(FILE *);
  void write_data_all(FILE *);
  virtual double single(int, int, int, int, double, double, double, double &);

 protected:
  double cut_global;
  double **cut;
  double **sigma; //characteristic distance
  double **offset;
  double **a2, **a4, **a6, **a8, **a10, **a12; //coefficients
  double **inv_poly2, **inv_poly4, **inv_poly6, **inv_poly8, **inv_poly10, **inv_poly12; //precalculated potential terms
  double **dinv_poly2, **dinv_poly4, **dinv_poly6, **dinv_poly8, **dinv_poly10, **dinv_poly12; //precalculated force terms

  virtual void allocate();
};

}    // namespace LAMMPS_NS

#endif
#endif

/* ERROR/WARNING messages:

E: Illegal ... command

Self-explanatory.  Check the input script syntax and compare to the
documentation for the command.  You can use -echo screen as a
command-line option when running LAMMPS to see the offending line.

E: Incorrect args for pair coefficients

Self-explanatory.  Check the input script or data file.

*/
