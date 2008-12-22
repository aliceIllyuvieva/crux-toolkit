/*************************************************************************//**
 * \file sorter.c
 * $Revision: 1.15 $
 * \brief: Object to sort objects
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


/**
 * \struct sorted_peptide_iterator
 * \brief Object to iterate over the peptides in an
 * specified sorted order.(mass, length, lexical)
 */
struct sorted_peptide_iterator {
  BOOLEAN_T use_database; ///< are we using the database or bin? TRUE if database, use linklist else array
  
  /**for sorting with linklist ****/
  // use linklist for database fasta peptide sorting
  PEPTIDE_WRAPPER_T* peptide_wrapper; ///< a linklist of peptide wrappers

  /***for sorting with array***/
  // use array for bin sorting
  PEPTIDE_T** peptide_array; ///< an array of peptides
  
  unsigned int peptide_count; ///< number of peptides in peptide_array  
  unsigned int current_idx; ///< the next peptide idx to return
};

/**
 * \struct peptide_wrapper
 * \brief Object to wrap the peptide to build a linklist
 */
struct peptide_wrapper{
  PEPTIDE_WRAPPER_T* next_wrapper; ///< the next peptide wrapper
  PEPTIDE_T* peptide;   ///< the core, the peptide
};    

/**
 *wrap the peptide up in the a peptide_wrapper object
 *\returns a peptide_wrapper object that contains the peptide
 */
PEPTIDE_WRAPPER_T* wrap_peptide(
  PEPTIDE_T* peptide ///< peptide to be wrapped -in
  )
{
  PEPTIDE_WRAPPER_T* new_wrapper = (PEPTIDE_WRAPPER_T*)mymalloc(sizeof(PEPTIDE_WRAPPER_T));
  new_wrapper->peptide = peptide;
  new_wrapper->next_wrapper = NULL;
  return new_wrapper;    
}

/**
 * compares two peptides with the give sort type (length, mass, lexical)
 * /returns 1 if peptide_one has lower priority, 0 if equal, -1 if greater priority
 */
int compareTo(
  PEPTIDE_T* peptide_one, ///< peptide to compare one -in
  PEPTIDE_T* peptide_two, ///< peptide to compare two -in
  SORT_TYPE_T sort_type  ///< sort type(LENGTH, MASS, LEXICAL) -in
  )
{
  // length order
  if(sort_type == LENGTH){
    if(get_peptide_length(peptide_one) >
       get_peptide_length(peptide_two)){
      return 1;
    }
    else if(get_peptide_length(peptide_one) ==
            get_peptide_length(peptide_two)){
      return 0;
    }
    else{
      return -1;
    }
  }
  // mass order
  else if(sort_type == MASS){
    return compare_float(get_peptide_peptide_mass(peptide_one), 
                         get_peptide_peptide_mass(peptide_two));
    
    /*  This should break ties, sorting by seq 
    int mass_compared = compare_float(get_peptide_peptide_mass(peptide_one),
                                      get_peptide_peptide_mass(peptide_two));
    if( mass_compared == 0 ){
      mass_compared = compareTo(peptide_one, peptide_two, LEXICAL);
    }
    return mass_compared;
    */
  }
  // lexicographic order
  else if(sort_type == LEXICAL){
    
    // convert the protein to heavy if needed
    protein_to_heavy(get_peptide_parent_protein(peptide_one));
    protein_to_heavy(get_peptide_parent_protein(peptide_two));
          
    char* peptide_one_sequence = get_peptide_sequence_pointer(peptide_one);
    char* peptide_two_sequence = get_peptide_sequence_pointer(peptide_two);
    int peptide_one_length = get_peptide_length(peptide_one);
    int peptide_two_length = get_peptide_length(peptide_two);
    int current_idx = 0;
    int result = 0;

    // check if all alphabetically identical
    while(current_idx < peptide_one_length &&
          current_idx < peptide_two_length){
      if(peptide_one_sequence[current_idx] > peptide_two_sequence[current_idx]){        
        result = 1;
        goto EXIT;
      }
      else if(peptide_one_sequence[current_idx] < peptide_two_sequence[current_idx]){
        result = -1;
        goto EXIT;
      }
      ++current_idx;
    }
    
    // alphabetically identical, check if same length
    if(peptide_one_length > peptide_two_length){
      result = 1;
    }
    else if(peptide_one_length < peptide_two_length){
      result = -1;
    }
    else{
      result = 0;
    }
    
  EXIT:
    /** 
     * uncomment this code if you want to restore a protein to 
     * light after converted to heavy
    // convert the protein back to light if needed
    if(get_database_use_light_protein(get_protein_database(get_peptide_parent_protein(peptide_one)))){
      protein_to_light(get_peptide_parent_protein(peptide_one));
      protein_to_light(get_peptide_parent_protein(peptide_two));
    }
    */
    return result;
  }
  
  die("ERROR: no matching sort_type");
  // quiet compiler
  return 0;
}

/**
 *check if any duplicate peptides exist to the first peptide on the wrapper
 *then, merge duplicate peptides to the first peptide 
 *assumes that the list is sorted by mass
 *\returns the wrapper list which first peptide is unique in the list
 */
PEPTIDE_WRAPPER_T* merge_duplicates_same_list(
  PEPTIDE_WRAPPER_T* wrapper_list  ///< peptide wrapper list examine -mod
  )
{
  PEPTIDE_WRAPPER_T* current = wrapper_list;
  // for all peptides that have equal mass
  while(current->next_wrapper != NULL &&
        compareTo(wrapper_list->peptide, current->next_wrapper->peptide, MASS)==0){  
    // check identical peptide
    if(compareTo(wrapper_list->peptide, current->next_wrapper->peptide, LEXICAL)==0){
      PEPTIDE_WRAPPER_T* wrapper_to_delete = current->next_wrapper;
      // merge peptides
      merge_peptides(wrapper_list->peptide, wrapper_to_delete->peptide);
      current->next_wrapper = current->next_wrapper->next_wrapper;
      free(wrapper_to_delete);
    }
    else{
      current = current->next_wrapper;
    }
  }
  return wrapper_list;
}

/**
 *check if any duplicate peptides exist in the wrapper_list_second
 *compared to the first peptide of the wrapper_list_first
 *then, remove the peptide from seond list and merge 
 *duplicate peptides to the first peptide in the first list
 *assumes that the list is sorted by mass
 *\returns the modified second wrapper list
 */
PEPTIDE_WRAPPER_T* merge_duplicates_different_list(
  PEPTIDE_WRAPPER_T* wrapper_list_first,  ///< fist peptide wrapper list examine -mod
  PEPTIDE_WRAPPER_T* wrapper_list_second  ///<second peptide wrapper list examine -mod
  )
{
  PEPTIDE_WRAPPER_T* current = wrapper_list_second;
  PEPTIDE_WRAPPER_T* previous = wrapper_list_second;
  
  while(current != NULL &&
        compareTo(wrapper_list_first->peptide, current->peptide, MASS) == 0){
    
    if(compareTo(wrapper_list_first->peptide, current->peptide, LEXICAL) == 0){
      PEPTIDE_WRAPPER_T* wrapper_to_delete = current;
      // merge peptides
      merge_peptides(wrapper_list_first->peptide, wrapper_to_delete->peptide);
      // for first wrapper in the list 
      if(previous == current){
        current = current->next_wrapper;
        previous = current;
        wrapper_list_second = current;
      }
      else{
        current = current->next_wrapper;
        previous->next_wrapper = current;
      }
      free(wrapper_to_delete);
    }
    else{
      if(previous == current){
        current = current->next_wrapper;
      }
      else{
        previous = current;
        current = current->next_wrapper;
      }
    }
  }
  return wrapper_list_second;
}

/**
 * merge wrapper list one and list two by sort type
 * assumes that each list one and two are sorted by the same sort type
 *\returns a sorted wrapper list result of merging list one and two
 */   
PEPTIDE_WRAPPER_T* merge(
  PEPTIDE_WRAPPER_T* wrapper_one, ///< fist peptide wrapper list -in
  PEPTIDE_WRAPPER_T* wrapper_two, ///< second peptide wrapper list -in
  SORT_TYPE_T sort_type, ///< the sort type of the new merged list -in
  BOOLEAN_T unique ///< do you merge two lists into a unique list? -in
  )
{
  PEPTIDE_WRAPPER_T* wrapper_final = NULL;
  PEPTIDE_WRAPPER_T* wrapper_current = wrapper_final;
  int compared;

  // loop around until there are no more wrappers to merge
 LOOP:

  if(wrapper_one != NULL || wrapper_two != NULL){
    if(wrapper_one == NULL){
      // there are wrappers on the current list
      if(wrapper_current != NULL){
        wrapper_current->next_wrapper = wrapper_two;
      }
      // there are no wrappers on the current list
      else{
        wrapper_final = wrapper_two;
      }
      wrapper_two = NULL;
    }
    else if(wrapper_two == NULL){
      // there are wrappers on the current list
      if(wrapper_current != NULL){
        wrapper_current->next_wrapper = wrapper_one;        
      }
      // there are no wrappers on the current list
      else{
        wrapper_final = wrapper_one;
      }
      wrapper_one = NULL;
    }
    
    else if((compared = 
             compareTo(wrapper_one->peptide, wrapper_two->peptide, sort_type)) == 1){
      // there are wrappers on the current list
      if(wrapper_current != NULL){
        wrapper_current->next_wrapper = wrapper_two;        
        wrapper_current = wrapper_current->next_wrapper;
      }
      // there are no wrappers on the current list
      else{
        wrapper_current = wrapper_two;
        wrapper_final = wrapper_current;
      }
      wrapper_two = wrapper_two->next_wrapper;
    }
    else{
      int compared_mass = -1;

      // if duplicate peptide, merge into one peptide only if unique is TRUE
      if(compared == 0 && unique){
        // only check if same peptide if mass is identical
        if(sort_type == MASS ||
           (compared_mass = compareTo(wrapper_one->peptide, wrapper_two->peptide, MASS))==0){
          // must be identical peptide, since mass & lexicographically same
          if(sort_type == LEXICAL ||
             compareTo(wrapper_one->peptide, wrapper_two->peptide, LEXICAL)==0){            
            PEPTIDE_WRAPPER_T* wrapper_to_delete = wrapper_two;
            // merge peptides
            merge_peptides(wrapper_one->peptide, wrapper_two->peptide);
            wrapper_two = wrapper_two->next_wrapper;
            free(wrapper_to_delete);
          }          
        }
        // merge all other instances of the same peptide in the list before adding to master list
        if(sort_type == MASS || sort_type == LENGTH){
          // wrapper_one = merge_duplicates_same_list(wrapper_one);
          wrapper_two = merge_duplicates_different_list(wrapper_one, wrapper_two);
        }
      }

      // when sorting by length and unique, sort also by mass to speed up the unique check
      // thus, when identical length add the peptide with smaller mass to the list first
      if(unique && sort_type == LENGTH && compared == 0 && compared_mass == 1){
        // there are wrappers on the current list
        if(wrapper_current != NULL){
          wrapper_current->next_wrapper = wrapper_two;        
          wrapper_current = wrapper_current->next_wrapper;
        }
        // there are no wrappers on the current list
        else{
          wrapper_current = wrapper_two;
          wrapper_final = wrapper_current;
        }
        wrapper_two = wrapper_two->next_wrapper;
      }
      else{
        // add wrapper to the merged list
        // there are wrappers on the current list
        if(wrapper_current != NULL){
          wrapper_current->next_wrapper = wrapper_one; 
          wrapper_current = wrapper_current->next_wrapper;
        }
        // there are no wrappers on the current list
        else{
          wrapper_current = wrapper_one;
          wrapper_final = wrapper_current;
        }
        wrapper_one = wrapper_one->next_wrapper;
      }
    }
    goto LOOP;
  }
  return wrapper_final;
}


/**
 * spilt a wrapper list into two equal or almost equal size lists
 * \returns array of two pointers of each split array
 * user must free the array, heap allocated
 */

PEPTIDE_WRAPPER_T** split(
  PEPTIDE_WRAPPER_T* wrapper_src ///< wrapper list to split -in
  )
{
  PEPTIDE_WRAPPER_T** split_wrapper = 
    (PEPTIDE_WRAPPER_T**)mycalloc(2, sizeof(PEPTIDE_WRAPPER_T*));

  PEPTIDE_WRAPPER_T* split1 = NULL;
  PEPTIDE_WRAPPER_T* split1_current = split1;
  PEPTIDE_WRAPPER_T* split2 = NULL;
  PEPTIDE_WRAPPER_T* split2_current = split2;
  BOOLEAN_T one = TRUE;

  // split wrapper until no more wrapper left in wrapper_src
  while(wrapper_src != NULL){
    // do i add it to the first list?
    if(one){
      // first time adding wrapper
      if(split1_current == NULL){
        split1_current = wrapper_src;
        split1 = split1_current;       
      }
      // add one wrapper to the end of the first list 
      else{
        split1_current->next_wrapper = wrapper_src;
        split1_current = split1_current->next_wrapper;
      }
      one = FALSE;
    }
    // add to second list
    else{
      // first time adding wrapper
      if(split2_current == NULL){
        split2_current = wrapper_src;
        split2 = split2_current;
      }
      // add one wrapper to the end of the second list 
      else{
        split2_current->next_wrapper = wrapper_src;
        split2_current = split2_current->next_wrapper;
      }
      one = TRUE;
    }
    wrapper_src = wrapper_src->next_wrapper;
  }
  
  // add list one and two to thesplit_wrapper to return
  if(split1 != NULL){
    split1_current->next_wrapper = NULL;
    split_wrapper[0] = split1;
  }

  if(split2 != NULL){
    split2_current->next_wrapper = NULL;
    split_wrapper[1] = split2;
  }

  return split_wrapper;
}

/**
 * merge sort wrapper list by the sort type
 *\returns a sorted wrapper list
 */   
PEPTIDE_WRAPPER_T* merge_sort(
  PEPTIDE_WRAPPER_T* wrapper_list, ///< the list of wrappers to sort -in
  SORT_TYPE_T sort_type, ///<the sort type (length, mass, lexicographical) -in
  BOOLEAN_T unique ///< return a list of unique peptides? -in
  )
{
  PEPTIDE_WRAPPER_T* final_sorted_list = NULL;
  PEPTIDE_WRAPPER_T* first_half = NULL;
  PEPTIDE_WRAPPER_T* second_half = NULL;
  
  // split wrapper list into two equal lists
  PEPTIDE_WRAPPER_T** wrapper_split = 
    split(wrapper_list);

  first_half = wrapper_split[0];
  second_half = wrapper_split[1];
  free(wrapper_split);


  // recursivelly sort each half
  if(first_half != NULL && second_half != NULL){
    first_half = merge_sort(first_half, sort_type, unique);
    second_half = merge_sort(second_half, sort_type, unique);
    final_sorted_list = merge(first_half, second_half, sort_type, unique);
  }
  else if(first_half != NULL || second_half != NULL){
    final_sorted_list = merge(first_half, second_half, sort_type, unique);
  }
  return final_sorted_list;
}

/**
 * Frees an allocated sorted_peptide_iterator object.
 * does not free the peptide it contains
 */
void free_peptide_wrapper(
  PEPTIDE_WRAPPER_T* peptide_wrapper ///< the wrapper to free -in
  )
{
  free(peptide_wrapper);
}

/**
 * Frees an allocated sorted_peptide_iterator object.
 * This frees the peptide the wrapper contains as well
 */
void free_peptide_wrapper_all(
  PEPTIDE_WRAPPER_T* peptide_wrapper ///< the wrapper to free -in
  )
{
  free_peptide(peptide_wrapper->peptide);
  free(peptide_wrapper);
}

/*********************************
 * sort peptide array
 **********************************/
void merge_peptide_array(
  PEPTIDE_T** peptide_array, ///< the list of peptide to sort -in/out
  unsigned int peptide_count, ///< peptide count in array(array size) -in
  unsigned int* unique_peptide_count  ///< peptide count in array(array size) -out
  )
{
  unsigned int current_idx = 0;
  unsigned int peptide_idx = 1;
  unsigned int move_idx = peptide_idx;

  // check for identical peptides and merge
  while(peptide_idx < peptide_count){
    // check if identical peptide
    if(compare_peptide_lexical_qsort(&(peptide_array[current_idx]), &(peptide_array[peptide_idx])) == 0){
      merge_peptides(peptide_array[current_idx], peptide_array[peptide_idx]);
      ++peptide_idx;
      --*unique_peptide_count;
    }
    else{
      // shift over peptide
      peptide_array[move_idx] = peptide_array[peptide_idx]; 
      current_idx = move_idx;
      ++peptide_idx;
      ++move_idx;      
    }
  }
}

/** 
 * add this code to compare methods if you want to restore a protein to 
 * light after converted to heavy
 // convert the protein back to light if needed
 if(get_database_use_light_protein(get_protein_database(get_peptide_parent_protein(peptide_one)))){
 protein_to_light(get_peptide_parent_protein(peptide_one));
 protein_to_light(get_peptide_parent_protein(peptide_two));
 }
*/


/**
 * sort the peptide array by given sort type
 * peptide_count is current peptide count, unique_peptide_count is the new peptide count after
 * merge redundatn peptides
 *\return the sorted peptide array, merged redundant peptides if required
 */
PEPTIDE_T** sort_peptide_array(
  PEPTIDE_T** peptide_array, ///< the list of peptide to sort -in/out
  unsigned int peptide_count, ///< peptide count in array(array size) -in
  SORT_TYPE_T sort_type, ///<the sort type (length, mass, lexicographical) -in
  BOOLEAN_T unique, ///< return a list of unique peptides? -in
  unsigned int* unique_peptide_count ///< The unique peptide count in array and/or new peptide count -out
  )
{
  // sort the peptide array by sort_type
  switch (sort_type){
  case MASS:
    qsort(peptide_array, peptide_count, sizeof(PEPTIDE_T*), (void*)compare_peptide_mass_qsort);
    break;
  case LEXICAL:
    qsort(peptide_array, peptide_count, sizeof(PEPTIDE_T*), (void*)compare_peptide_lexical_qsort);
    break;
  case LENGTH:
    qsort(peptide_array, peptide_count, sizeof(PEPTIDE_T*), (void*)compare_peptide_length_qsort);
    break;
  case NONE:
    break;
  }
  
  // merge peptides if unique
  if(unique){
    merge_peptide_array(peptide_array, peptide_count, unique_peptide_count);
  }

  return peptide_array;
}


/***********************************
 * sorted peptide iterator
 ***********************************/

unsigned long total_number_peptide = 0;

/**
 * Instantiates a new sorted_peptide_iterator from a database_peptide_iterator
 * \returns a SORTED_PEPTIDE_ITERATOR_T object.
 */
SORTED_PEPTIDE_ITERATOR_T* new_sorted_peptide_iterator_database(
  DATABASE_PEPTIDE_ITERATOR_T* database_peptide_iterator, 
    ///< the peptide iterator to extend -in
  SORT_TYPE_T sort_type, ///< the sort type for this iterator -in
  BOOLEAN_T unique ///< only return unique peptides? -in
  )
{
  PEPTIDE_WRAPPER_T* master_list_wrapper = NULL;
  PEPTIDE_WRAPPER_T* list_wrapper = NULL;
  PEPTIDE_WRAPPER_T* current_wrapper = NULL;
  BOOLEAN_T start = TRUE;
  
  SORTED_PEPTIDE_ITERATOR_T* sorted_peptide_iterator =
    (SORTED_PEPTIDE_ITERATOR_T*)mycalloc(1, sizeof(SORTED_PEPTIDE_ITERATOR_T));

  // we are using database sorting
  sorted_peptide_iterator->use_database = TRUE;


  // iterate over all peptides in a protein
  while(database_peptide_iterator_has_next(database_peptide_iterator)){
    // debug purpuse
    ++total_number_peptide;
    if(total_number_peptide % 1000000 == 0){
      carp(CARP_INFO, "Number of peptides (not unique): %u", 
          total_number_peptide); 
    }
    
    if(start){
      start = FALSE;
      current_wrapper =
        wrap_peptide(database_peptide_iterator_next(database_peptide_iterator));
      list_wrapper = current_wrapper;
    }
    else{
      // wrap the next peptide
      current_wrapper->next_wrapper =
        wrap_peptide(database_peptide_iterator_next(database_peptide_iterator));
      current_wrapper = current_wrapper->next_wrapper;
    }
  }
  // add all peptides to the complied master list
  master_list_wrapper = list_wrapper;
  
  // sort the master list using merge sort
  master_list_wrapper = merge_sort(master_list_wrapper, sort_type, unique);

  sorted_peptide_iterator->peptide_wrapper = master_list_wrapper;
  
  carp(CARP_DETAILED_DEBUG, "total number of peptides(not unique): %u", 
      total_number_peptide); 
  
  return sorted_peptide_iterator;
}

/**
 * Instantiates a new sorted_peptide_iterator from a bin_peptide_iterator
 * \returns a SORTED_PEPTIDE_ITERATOR_T object.
 */
SORTED_PEPTIDE_ITERATOR_T* new_sorted_peptide_iterator_bin(
  BIN_PEPTIDE_ITERATOR_T* bin_peptide_iterator, ///< the peptide iterator to extend -in
  SORT_TYPE_T sort_type, ///< the sort type for this iterator -in
  BOOLEAN_T unique, ///< only return unique peptides? -in
  unsigned int peptide_count ///< the total peptide count in the bin -in
  )
{  
  unsigned int peptide_idx = 0;
  unsigned int unique_peptide_count = peptide_count;

  SORTED_PEPTIDE_ITERATOR_T* sorted_peptide_iterator =
    (SORTED_PEPTIDE_ITERATOR_T*)mycalloc(1, sizeof(SORTED_PEPTIDE_ITERATOR_T));

  // we are using bin sorting
  sorted_peptide_iterator->use_database = FALSE;

  // create an array of peptides to sort
  sorted_peptide_iterator->peptide_array =
    (PEPTIDE_T**)mycalloc(peptide_count, sizeof(PEPTIDE_T*));

  // iterate over all peptides in a protein
  while(bin_peptide_iterator_has_next(bin_peptide_iterator)){
    // debug purpuse
    ++total_number_peptide;
    if(total_number_peptide % 1000000 == 0){
      carp(CARP_INFO, "number of peptides(not unique): %u", total_number_peptide); 
    }
    
    // store peptide in peptide array
    sorted_peptide_iterator->peptide_array[peptide_idx] = 
      bin_peptide_iterator_next(bin_peptide_iterator);
    
    ++peptide_idx;
  }
  
  // sort the peptide array
  sorted_peptide_iterator->peptide_array = 
    sort_peptide_array(sorted_peptide_iterator->peptide_array, peptide_count, 
                       sort_type, unique, &unique_peptide_count);

  // set peptide count of the array
  sorted_peptide_iterator->peptide_count = unique_peptide_count;

  carp(CARP_DETAILED_DEBUG, "Number of peptides in bin: unique %d, total %d", 
       unique_peptide_count, total_number_peptide); 
  //  carp(CARP_DEBUG, "total number of peptides(not unique): %u", total_number_peptide); 

  return sorted_peptide_iterator;
}


/*
SORTED_PEPTIDE_ITERATOR_T* new_sorted_peptide_iterator_bin(
  BIN_PEPTIDE_ITERATOR_T* bin_peptide_iterator, ///< the peptide iterator to extend -in
  SORT_TYPE_T sort_type, ///< the sort type for this iterator -in
  BOOLEAN_T unique, ///< only return unique peptides? -in
  unsigned int peptide_count ///< the total peptide count in the bin -in
  )
{
  PEPTIDE_WRAPPER_T* master_list_wrapper = NULL;
  PEPTIDE_WRAPPER_T* list_wrapper = NULL;
  PEPTIDE_WRAPPER_T* current_wrapper = NULL;
  BOOLEAN_T start = TRUE;
  
  SORTED_PEPTIDE_ITERATOR_T* sorted_peptide_iterator =
    (SORTED_PEPTIDE_ITERATOR_T*)mycalloc(1, sizeof(SORTED_PEPTIDE_ITERATOR_T));
  
  // iterate over all peptides in a protein
  while(bin_peptide_iterator_has_next(bin_peptide_iterator)){
    // debug purpuse
    ++total_number_peptide;
    if(total_number_peptide % 1000000 == 0){
      carp(CARP_INFO, "number of peptides(not unique): %u", total_number_peptide); 
    }
    
    if(start){
      start = FALSE;
      current_wrapper =
        wrap_peptide(bin_peptide_iterator_next(bin_peptide_iterator));
      list_wrapper = current_wrapper;
    }
    else{
      // wrap the next peptide
      current_wrapper->next_wrapper =
        wrap_peptide(bin_peptide_iterator_next(bin_peptide_iterator));
      current_wrapper = current_wrapper->next_wrapper;
    }
       
  }
  
  // add all peptides to the complied master list
  master_list_wrapper = list_wrapper;
  
  carp(CARP_INFO, "total number of peptides(not unique): %u", total_number_peptide); 

  // sort the master list using merge sort
  master_list_wrapper = merge_sort(master_list_wrapper, sort_type, unique);

  sorted_peptide_iterator->peptide_wrapper = master_list_wrapper;
    
  return sorted_peptide_iterator;
}
*/


/**
 * The basic iterator functions.
 * \returns TRUE if there are additional peptides to iterate over, FALSE if not.
 */
BOOLEAN_T sorted_peptide_iterator_has_next(
  SORTED_PEPTIDE_ITERATOR_T* sorted_peptide_iterator ///< the iterator of interest -in
  )
{
  // are we using peptides from database or bin(aka, linklist or array implementation)?
  switch (sorted_peptide_iterator->use_database){
    
  case FALSE: // use array implementation
    if(sorted_peptide_iterator->current_idx < sorted_peptide_iterator->peptide_count){
      return TRUE;
    }
    break;
  case TRUE:  // use linklist implementation
    if(sorted_peptide_iterator->peptide_wrapper != NULL){
      return TRUE;
    }
    break;
  }
  
  return FALSE;
}

/**
 * returns each peptide in sorted order
 * \returns The next peptide in the sorted peptide iterator.
 */
PEPTIDE_T* sorted_peptide_iterator_next(
  SORTED_PEPTIDE_ITERATOR_T* sorted_peptide_iterator ///< the iterator of interest -in
  )
{
  PEPTIDE_T* next_peptide = NULL;

  // are we using peptides from database or bin(aka, linklist or array implementation)?
  switch (sorted_peptide_iterator->use_database){
  
  case FALSE: // use array implementation
    next_peptide = sorted_peptide_iterator->peptide_array[sorted_peptide_iterator->current_idx];
    ++sorted_peptide_iterator->current_idx;
    break;
  case TRUE:  // use linklist implementation
    // strip the peptide out of the peptide wrapper
    next_peptide = sorted_peptide_iterator->peptide_wrapper->peptide;
    // free the empty peptide wrapper
    PEPTIDE_WRAPPER_T* old_wrapper =  sorted_peptide_iterator->peptide_wrapper;
    sorted_peptide_iterator->peptide_wrapper = 
      sorted_peptide_iterator->peptide_wrapper->next_wrapper;
    free_peptide_wrapper(old_wrapper);
    break;
  }
  return next_peptide;
}

/**
 * Frees an allocated sorted_peptide_iterator object.
 */
void free_sorted_peptide_iterator(
  SORTED_PEPTIDE_ITERATOR_T* sorted_peptide_iterator ///< the iterator to free -in
  )
{
  PEPTIDE_WRAPPER_T* old_wrapper = NULL;
  
  // are we using peptides from database or bin(aka, linklist or array implementation)?
  switch (sorted_peptide_iterator->use_database){

  case FALSE: // use array implementation
    // free all unused peptides
    while(sorted_peptide_iterator->current_idx < sorted_peptide_iterator->peptide_count){
      free_peptide(sorted_peptide_iterator->peptide_array[sorted_peptide_iterator->current_idx++]);
    }
    // free peptide array
    free(sorted_peptide_iterator->peptide_array);
    break;
  case TRUE:  // use linklist implementation    
    // free all unused peptide wrappers the iterator contains
    while(sorted_peptide_iterator->peptide_wrapper != NULL){
      old_wrapper = sorted_peptide_iterator->peptide_wrapper;
      sorted_peptide_iterator->peptide_wrapper = 
        sorted_peptide_iterator->peptide_wrapper->next_wrapper;
      free_peptide_wrapper_all(old_wrapper);
    }
    break;
  }
  // free the sorted_peptide_iterator itself
  free(sorted_peptide_iterator);
}
