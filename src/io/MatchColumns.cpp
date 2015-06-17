/*************************************************************************//**
 * \file MatchColumns.cpp
 * \brief Just keeps track of column names for match files.
 ****************************************************************************/

#include "MatchColumns.h"
#include "io/carp.h"
#include <string.h>

static const char* match_column_strings[NUMBER_MATCH_COLUMNS] = {
  "file",
  "file_idx",
  "scan",
  "charge",
  "spectrum precursor m/z",
  "spectrum neutral mass",
  "peptide mass",
  "delta_cn",
  "sp score",
  "sp rank",
  "xcorr score",
  "xcorr rank",
  "exact p-value",
  "refactored xcorr",
  "Sidak adjusted p-value",  
  "e-value",
  "p-value",
  "smoothed p-value",
  "distinct matches/peptide",
#ifdef NEW_COLUMNS
  "Weibull PSM q-value",
  "Weibull peptide q-value",    // NEW
  "decoy PSM q-value",
  "decoy peptide q-value",      // NEW
  "percolator score",
  "percolator rank",
  "percolator PSM q-value",
  "percolator peptide q-value", // NEW
  "q-ranker score",
  "q-ranker PSM q-value",
  "q-ranker peptide q-value",   // NEW
#else
  "Weibull est. q-value",
  "Weibull est. PEP",
  "decoy q-value (xcorr)",
  "decoy PEP (xcorr)",
  "decoy q-value (e-value)",
  "decoy PEP (e-value)",
  "percolator score",
  "percolator rank",
  "percolator q-value",
  "percolator PEP",
  "q-ranker score",
  "q-ranker q-value",
  "q-ranker PEP",
  "barista score",
  "barista q-value",
#endif
  "tdc q-value",
  "mix-max q-value",
  "b/y ions matched",
  "b/y ions total",
  "total matches/spectrum",
  "distinct matches/spectrum",
  "sequence",
  "cleavage type",
  "protein id",
  "peptides",
  "flanking aa",
  "original target sequence",
  "eta",
  "beta",
  "shift",
  "corr",
  "RAW",
  "SIN",
  "NSAF",
  "dNSAF",
  "EMPAI",
  "parsimony rank",
  "decoy matches/spectrum",
  "product type",
  "ppm",
  "xcorr 1",
  "xcorr 2",
  "protein id x"
};

/**
 * Get the name of a given column, by index.
 */
const char* get_column_header(
  int columnIndex
) {
  if ((columnIndex < 0) || (columnIndex >= NUMBER_MATCH_COLUMNS)) {
    carp(CARP_FATAL, "Cannot access output column %d.\n", columnIndex);
  }
  carp(CARP_DETAILED_DEBUG, "get_column_header: %d/%d %s", columnIndex, 
    NUMBER_MATCH_COLUMNS, match_column_strings[columnIndex]);
  return(match_column_strings[columnIndex]);
}

int get_column_idx(
  const char* column_name
) {
  for (int i = FILE_COL; i < NUMBER_MATCH_COLUMNS; ++i) {
    if (strcmp(column_name, match_column_strings[i]) == 0) {
      return i;
    }
  }
  return INVALID_COL;
}
