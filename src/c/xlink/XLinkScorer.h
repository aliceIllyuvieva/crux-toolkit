#ifndef XLINKSCORER_H_
#define XLINKSCORER_H_
#include "objects.h"
#include "MatchCandidate.h"

class XLinkScorer {
 protected:
  Spectrum* spectrum_;
  SCORER_T* scorer_xcorr_;
  SCORER_T* scorer_sp_;
  IonConstraint* ion_constraint_xcorr_;
  IonConstraint* ion_constraint_sp_;
  MatchCandidate* candidate_;
  int charge_;
  IonSeries* ion_series_xcorr_;
  IonSeries* ion_series_sp_;
  
 public:
  XLinkScorer();
  XLinkScorer(Spectrum* spectrum, int charge);
  virtual ~XLinkScorer();

  FLOAT_T scoreCandidate(MatchCandidate* candidate);


};


#endif