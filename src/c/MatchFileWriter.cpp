/**
 * \file MatchFileWriter.cpp
 * DATE: October 26, 2010
 * AUTHOR: Barbara Frewen
 * \brief Object for writing tab-deliminted text files of PSMs (matches).
 * This class is the extension of DelimitedFileWriter where the
 * columns are those in MatchColumns.  Which columns are written to
 * file depend on the COMMAND_TYPE_T.  For some commands, they also
 * depend on the columns found in the input file as given by a
 * MatchFileReader. 
 */

#include "MatchFileWriter.h"
#include "parameter.h"
#include <iostream>

using namespace std;

/**
 * \returns A blank MatchFileWriter object.
 */
MatchFileWriter::MatchFileWriter() 
  : DelimitedFileWriter(),
    num_columns_(0){
  for(int col_type = 0; col_type < NUMBER_MATCH_COLUMNS; col_type++){
    match_to_print_[col_type] = false;
  }
  setPrecision();
} 

/**
 * \returns A blank MatchFileWriter object and opens a file for
 * writing.
 */
MatchFileWriter::MatchFileWriter(const char* filename) 
  : DelimitedFileWriter(filename),
    num_columns_(0){
  for(int col_type = 0; col_type < NUMBER_MATCH_COLUMNS; col_type++){
    match_to_print_[col_type] = false;
  }
  setPrecision();
}

/**
 * Destructor
 */
MatchFileWriter::~MatchFileWriter(){
}

/**
 * Set the correct level of precision for each MATCH_COLUMNS_T type.
 */
void MatchFileWriter::setPrecision(){
  for(int col_idx = 0; col_idx < NUMBER_MATCH_COLUMNS; col_idx++){
    switch(col_idx){
      // integer and string fields
    case SCAN_COL:
    case CHARGE_COL:
    case SP_RANK_COL:
    case XCORR_RANK_COL:
    case BY_IONS_MATCHED_COL:
    case BY_IONS_TOTAL_COL:
    case MATCHES_SPECTRUM_COL:
    case SEQUENCE_COL:
    case CLEAVAGE_TYPE_COL:
    case PROTEIN_ID_COL:
    case FLANKING_AA_COL:
    case UNSHUFFLED_SEQUENCE_COL:
      match_precision_[col_idx] = 0;
      break;

      // mass fields
    case SPECTRUM_PRECURSOR_MZ_COL:
    case SPECTRUM_NEUTRAL_MASS_COL:
    case PEPTIDE_MASS_COL:
      match_precision_[col_idx] = 4; // add a --mass-precision option?
      break;

      // score fields
    case DELTA_CN_COL:
    case SP_SCORE_COL:
    case XCORR_SCORE_COL:
    case PVALUE_COL:
    case WEIBULL_QVALUE_COL:
    case DECOY_XCORR_QVALUE_COL:
    case PERCOLATOR_SCORE_COL:
    case PERCOLATOR_RANK_COL:
    case PERCOLATOR_QVALUE_COL:
    case QRANKER_SCORE_COL:
    case QRANKER_QVALUE_COL:
    case ETA_COL:
    case BETA_COL:
    case SHIFT_COL:
    case CORR_COL:
#ifdef NEW_COLUMNS
    case WEIBULL_PEPTIDE_QVALUE_COL:      // NEW
    case DECOY_XCORR_PEPTIDE_QVALUE_COL:  // NEW
    case PERCOLATOR_PEPTIDE_QVALUE_COL:   // NEW
    case QRANKER_PEPTIDE_QVALUE_COL:      // NEW
#endif
      match_precision_[col_idx] = get_int_parameter("precision");
      break;


    case NUMBER_MATCH_COLUMNS:
    case INVALID_COL:
      carp(CARP_FATAL, "Invalid match column type for setting precision.");
      break;
    }
  }
}

/**
 * Defines the columns to print based on the vector of flags
 * indiciating if the MATCH_COLUMN_T should be printed.
 */
void MatchFileWriter::addColumnNames(const std::vector<bool>& col_is_printed){

  // for each column, if we should print it, mark as true
  for(size_t col_idx = 0; col_idx < col_is_printed.size(); col_idx++){
    bool print_it = col_is_printed[col_idx];
    if( print_it ){
      match_to_print_[col_idx] = true;
    } 
  }

}

/**
 * Adds another columns to print.  Printed in order the names are set.
 */
void MatchFileWriter::addColumnName(MATCH_COLUMNS_T column_type){
  match_to_print_[column_type] = true;
}

/**
 * Adds which columns to print based on the COMMAND_TYPE_T. Only for
 * search-for-matches and sequest-search.
 */
void MatchFileWriter::addColumnNames(COMMAND_T command, bool has_decoys){

  switch (command){
  // commands with no tab files
  case INDEX_COMMAND:        ///< create-index
  case PROCESS_SPEC_COMMAND: ///< print-processed-spectra
  case VERSION_COMMAND:      ///< just print the version number
  // invalid
  case NUMBER_COMMAND_TYPES:
  case INVALID_COMMAND:
    carp(CARP_FATAL, "Invalid command (%s) for creating a MatchFileWriter.",
         command_type_to_command_line_string_ptr(command));
    return;

  // commands that also require list of cols to print
  case QVALUE_COMMAND:       ///< compute-q-values
  case PERCOLATOR_COMMAND:
  case QRANKER_COMMAND:
    carp(CARP_FATAL, 
         "Post-search command %s requires a list of columns to print.",
         command_type_to_command_line_string_ptr(command));
    return;

  // valid commands
  case SEARCH_COMMAND:       ///< search-for-matches
    if( get_boolean_parameter("compute-p-values") ){
      addColumnName(PVALUE_COL);
      addColumnName(ETA_COL);
      addColumnName(BETA_COL);
      addColumnName(SHIFT_COL);
      addColumnName(CORR_COL);
    }
    if( get_boolean_parameter("compute-sp") 
        && get_int_parameter("max-rank-preliminary") > 0 ){
      addColumnName(SP_SCORE_COL);
      addColumnName(SP_RANK_COL);
      addColumnName(BY_IONS_MATCHED_COL);
      addColumnName(BY_IONS_TOTAL_COL);
    }
    break;

  case SEQUEST_COMMAND:      ///< sequest-search
    if( get_int_parameter("max-rank-preliminary") > 0 ){
      addColumnName(SP_SCORE_COL);
      addColumnName(SP_RANK_COL);
      addColumnName(BY_IONS_MATCHED_COL);
      addColumnName(BY_IONS_TOTAL_COL);
    }
    break;

  case XLINK_SEARCH_COMMAND:
    // TODO: does search-for-xlinks use MatchFileWriter?
    break;
  }

  // All search commands have these columns
  addColumnName(SCAN_COL);
  addColumnName(CHARGE_COL);
  addColumnName(SPECTRUM_PRECURSOR_MZ_COL);
  addColumnName(SPECTRUM_NEUTRAL_MASS_COL);
  addColumnName(PEPTIDE_MASS_COL);
  addColumnName(DELTA_CN_COL);
  addColumnName(XCORR_SCORE_COL);
  addColumnName(XCORR_RANK_COL);
  addColumnName(MATCHES_SPECTRUM_COL);
  addColumnName(SEQUENCE_COL);
  addColumnName(CLEAVAGE_TYPE_COL);
  addColumnName(PROTEIN_ID_COL);
  addColumnName(FLANKING_AA_COL);
  if( has_decoys ){
    addColumnName(UNSHUFFLED_SEQUENCE_COL);
  }

}

/**
 * Adds which columns to print based on the COMMAND_TYPE_T and a list
 * of columns to print. For all post-search commands.
 */
void MatchFileWriter::addColumnNames
  (COMMAND_T command, 
   bool has_decoys,
   const vector<bool>& cols_to_print){

  switch (command){
  // commands with no tab files
  case INDEX_COMMAND:        ///< create-index
  case PROCESS_SPEC_COMMAND: ///< print-processed-spectra
  case VERSION_COMMAND:      ///< just print the version number
  // invalid
  case NUMBER_COMMAND_TYPES:
  case INVALID_COMMAND:
    carp(CARP_FATAL, "Invalid command (%s) for creating a MatchFileWriter.",
         command_type_to_command_line_string_ptr(command));
    return;

  // search commands handled elsewhere
  case SEARCH_COMMAND:       ///< search-for-matches
  case SEQUEST_COMMAND:      ///< sequest-search
  case XLINK_SEARCH_COMMAND:
    addColumnNames(command, has_decoys);
    return;

  // valid commands
  case QVALUE_COMMAND:       ///< compute-q-values
    if( cols_to_print[PVALUE_COL] ){
      addColumnName(WEIBULL_QVALUE_COL);
      //addColumnName(WEIBULL_PEPTIDE_QVALUE_COL);
    } else {
      addColumnName(DECOY_XCORR_QVALUE_COL);
      //addColumnName(DECOY_XCORR_PEPTIDE_QVALUE_COL);
    }
    break;

  case PERCOLATOR_COMMAND:
    addColumnName(PERCOLATOR_SCORE_COL);
    addColumnName(PERCOLATOR_RANK_COL);
    addColumnName(PERCOLATOR_QVALUE_COL);
    break;

  case QRANKER_COMMAND:
    addColumnName(QRANKER_SCORE_COL);
    addColumnName(QRANKER_QVALUE_COL);
    break;
  }

  if( has_decoys ){
    addColumnName(UNSHUFFLED_SEQUENCE_COL);
  }

  // now add remaining columns from the input file
  addColumnNames(cols_to_print);

  // FIXME (BF 10-27-10): where do these go?
  //  PERCOLATOR_PEPTIDE_QVALUE_COL,   
  //  QRANKER_PEPTIDE_QVALUE_COL,      

}

/**
 * Write header to file using column names that have been set.
 */
void MatchFileWriter::writeHeader(){

  // set file position index for all columns being printed
  for(unsigned int col_type = 0; col_type < NUMBER_MATCH_COLUMNS; col_type++){
    if( match_to_print_[col_type] == true ){
      match_indices_[col_type] = num_columns_++;
    } else {
      match_indices_[col_type] = -1;
    }
  }

  // set all the names for which we have match_indices_
  column_names_.assign(num_columns_, "");
  for(unsigned int col_type = 0; col_type < NUMBER_MATCH_COLUMNS; col_type++){
    if( match_indices_[col_type] > -1 ){
      DelimitedFileWriter::setColumnName(get_column_header(col_type), 
                                         match_indices_[col_type]);
    }
  }

  DelimitedFileWriter::writeHeader();

  // every line will be this length, reserve space in current row for speed
  current_row_.assign(num_columns_, "");
}


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 2
 * End:
 */
