/**
 * \file sorter.h
 * $Revision: 1.2 $
 * \brief Object to sort objects
 ****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "utils.h"
#include "crux-utils.h"
#include "peptide.h"
#include "protein.h"
#include "index.h"
#include "carp.h"
#include "objects.h"
#include "peptide_constraint.h"
#include "database.h"



/***********************************
 * sorted peptide iterator
 ***********************************/

/**
 * Instantiates a new sorted_peptide_iterator from a database_peptide_iterator
 * \returns a SORTED_PEPTIDE_ITERATOR_T object.
 */
SORTED_PEPTIDE_ITERATOR_T* new_sorted_peptide_iterator_database(
  DATABASE_PEPTIDE_ITERATOR_T* database_peptide_iterator, ///< the peptide iterator to extend -in
  SORT_TYPE_T sort_type, ///< the sort type for this iterator -in
  BOOLEAN_T unique ///< only return unique peptides? -in
  );

/**
 * Instantiates a new sorted_peptide_iterator from a bin_peptide_iterator
 * \returns a SORTED_PEPTIDE_ITERATOR_T object.
 */
SORTED_PEPTIDE_ITERATOR_T* new_sorted_peptide_iterator_bin(
  BIN_PEPTIDE_ITERATOR_T* bin_peptide_iterator, ///< the peptide iterator to extend -in
  SORT_TYPE_T sort_type, ///< the sort type for this iterator -in
  BOOLEAN_T unique ///< only return unique peptides? -in
  );

/**
 * Frees an allocated database_sorted_peptide_iterator object.
 */
void free_sorted_peptide_iterator(
  SORTED_PEPTIDE_ITERATOR_T* database_peptide_iterator ///< the iterator to free -in
  );

/**
 * The basic iterator functions.
 * \returns TRUE if there are additional peptides to iterate over, FALSE if not.
 */
BOOLEAN_T sorted_peptide_iterator_has_next(
  SORTED_PEPTIDE_ITERATOR_T* peptide_iterator ///< the iterator of interest -in
  );

/**
 * returns each peptide in sorted order
 * \returns The next peptide in the database.
 */
PEPTIDE_T* sorted_peptide_iterator_next(
  SORTED_PEPTIDE_ITERATOR_T* peptide_iterator ///< the iterator of interest -in
  );

