#include "AssignIonsMPSMSpectrum.h"
#include <algorithm>
#include "carp.h"
#include "crux-utils.h"
#include "objects.h"
#include "parameter.h"
#include "Ion.h"
#include "IonSeries.h"
#include "IonConstraint.h"
#include "Peak.h"

#include "DelimitedFile.h"
#include "SpectrumCollectionFactory.h"
#include "MPSM_Scorer.h"

#include <fstream>

using namespace std;

AssignIonsMPSMSpectrum::AssignIonsMPSMSpectrum() {

}

AssignIonsMPSMSpectrum::~AssignIonsMPSMSpectrum() {
}


int AssignIonsMPSMSpectrum::main(int argc, char** argv) {
  /* Define optional and required command line arguments */
  const char* option_list[] = {
    "version",
    "max-ion-charge",
    "fragment-mass",
    "mz-bin-width"
  };
  int num_options = sizeof(option_list) / sizeof(char*);

  const char* argument_list[] = {
    "ms2 file",
    "scan number",
    "peptide sequences",
    "charge states"
  };
  int num_arguments = sizeof(argument_list) / sizeof(char*);

  /* for debugging of parameter processing */
  //set_verbosity_level( CARP_DETAILED_DEBUG );
  set_verbosity_level( CARP_ERROR );

  /* Set default values for parameters in parameter.c */
  initialize_parameters();

  /* Define optional and required command line arguments */
  select_cmd_line_options( option_list, num_options );
  select_cmd_line_arguments( argument_list, num_arguments);

  /* Parse the command line, including the optional params file */
  /* does sytnax, type, bounds checking and dies if neccessessary */
  parse_cmd_line_into_params_hash(argc, argv, "score-mpsm-spectrum");

  /* Set verbosity */
  set_verbosity_level(get_int_parameter("verbosity"));

  /* Get Arguments */

  vector<string> peptide_sequences = get_string_vector_parameter("peptide sequences");
  vector<int> charge_states = get_int_vector_parameter("charge states");

  const char* ms2_filename = get_string_parameter("ms2 file");
  int scan_number = get_int_parameter("scan number");

  /* Get Options */
  const char* max_ion_charge = get_string_parameter_pointer("max-ion-charge");

  // check peptide sequence
  for (int idx=0;idx<peptide_sequences.size();idx++) {
    if(!valid_peptide_sequence(peptide_sequences[idx].c_str())){
      carp(CARP_FATAL, "The peptide sequence '%s' is not valid", 
           peptide_sequences[idx].c_str());
    }
  }

  FLOAT_T mz_tolerance = get_double_parameter("mz-bin-width");

  int max_charge = charge_states[0];
  for (unsigned int idx=1;idx < charge_states.size();idx++) {
    max_charge = max(charge_states[idx], max_charge);
  }

  max_charge = min(max_charge, get_max_ion_charge_parameter("max-ion-charge"));

  //get spectrum
  
  SpectrumCollection* collection = 
    SpectrumCollectionFactory::create(ms2_filename);

  Spectrum* spectrum = collection->getSpectrum(scan_number);

  if (spectrum == NULL) {
    carp(CARP_FATAL, "Could not find scan number %i", scan_number);
  }


  vector<IonSeries*> ion_series_vec;
  vector<IonConstraint*> ion_constraints;

  cerr <<"Creating ion serieses"<<endl;

  for (int seq_idx=0;seq_idx < peptide_sequences.size();seq_idx++) {

    IonConstraint* ion_constraint = new IonConstraint(
      get_mass_type_parameter("fragment-mass"),
      min(charge_states[seq_idx], max_charge),
      BY_ION,
      false);

    ion_constraints.push_back(ion_constraint);

    IonSeries* ion_series = new IonSeries(
      peptide_sequences[seq_idx].c_str(), 
      charge_states[seq_idx], 
      ion_constraints[seq_idx]);

    ion_series->predictIons();

    ion_series_vec.push_back(ion_series);
  }


  vector<map<Peak*, vector<Ion*> > > matched_peak_to_ion_vec;

  map<Peak*, vector<Ion*> >::iterator find_iter;

  vector<int> num_matched_ions;

  cerr <<"Assigning ions"<<endl;

  for (int seq_idx = 0; seq_idx < peptide_sequences.size();seq_idx++) {

    cerr <<"Sequence:"<<peptide_sequences[seq_idx]<<endl;

    num_matched_ions.push_back(0);
    //For each ion, find the max_intensity peak with in the the 
    map<Peak*, vector<Ion*> > temp;
    matched_peak_to_ion_vec.push_back(temp);
    map<Peak*, vector<Ion*> > &matched_peak_to_ion = matched_peak_to_ion_vec[seq_idx];

    IonSeries* current_ion_series = ion_series_vec[seq_idx];

    for (IonIterator ion_iter = current_ion_series -> begin();
      ion_iter != current_ion_series->end();
      ++ion_iter) {

      Ion* ion = *ion_iter;

      Peak* peak = spectrum->getMaxIntensityPeak(ion->getMassZ(), mz_tolerance);
      if (peak != NULL) {  
        num_matched_ions[seq_idx]++;
        find_iter = matched_peak_to_ion.find(peak);

        if (find_iter == matched_peak_to_ion.end()) {
          vector<Ion*> temp_ions;
          matched_peak_to_ion[peak] = temp_ions;
        }

        matched_peak_to_ion[peak].push_back(ion);
      }
    }
  }

  //now iterate through all of the peaks and print out the matched ones.
  cerr <<"Printing results"<<endl;

  ofstream fout("assign.txt");

  fout << "m/z\tintensity";
  
  for (int seq_idx=0;seq_idx < peptide_sequences.size();seq_idx++) {
    fout << "\tmatched_"<<seq_idx;
  }
  fout <<"\toverlap"<<endl;

  for (PeakIterator peak_iter = spectrum->begin();
    peak_iter != spectrum->end();
    ++peak_iter) {

    Peak* peak = *peak_iter;
    if (peak->getLocation() < 400 || peak->getLocation() > 1400) {
      continue;
    }
    fout << peak->getLocation() << "\t" <<
            peak->getIntensity();

    //cerr << get_peak_location(peak) << endl;


    
    int num_matched = 0;
    for (int seq_idx = 0;seq_idx < peptide_sequences.size();seq_idx++) {
      //cerr <<"finding matched peak in seq:"<<seq_idx<<endl;
      fout << "\t";
      find_iter = matched_peak_to_ion_vec[seq_idx].find(peak);
      if (find_iter != matched_peak_to_ion_vec[seq_idx].end()) {
        num_matched++;
        fout << peak->getIntensity();
      } else {
        fout << "0" ;
      }
    }

    if (num_matched > 1) {
      fout << "\t" <<  peak->getIntensity() << endl;
    }  else {
      fout << "\t" <<  "0" << endl;
    }

  }

  fout.close();

  for (int seq_idx = 0;seq_idx<peptide_sequences.size();seq_idx++) {
    cerr << "Number of ions assigned to "<<peptide_sequences[seq_idx]<<":"<<num_matched_ions[seq_idx]<<endl;
    cerr << "Total ions for "<<peptide_sequences[seq_idx]<<":"<<ion_series_vec[seq_idx]->getNumIons()<<endl;
  }

  ofstream gnuplot("annotate.gnuplot");
  gnuplot << "set terminal png" << endl;
  gnuplot << "set xrange[300:*]"<<endl;
  gnuplot << "plot \"assign.txt\" using 1:2 with impulses lc rgb \"grey\" lt 1 notitle";
  for (int seq_idx=0;seq_idx<peptide_sequences.size();seq_idx++) {
    string sequence = peptide_sequences[seq_idx];
    int charge = charge_states[seq_idx];
    int index = seq_idx+3;
    string color = "";
    switch(seq_idx) {
      case 0: 
        color = "red";
        break;
      case 1:
        color = "blue";
        break;
      case 2: 
        color = "purple";
        break;
      case 3:
        color = "orange";
        break;
      case 4:
        color = "cyan";
        break;
      case 5:
        color = "violet";
        break;
      
    }

    gnuplot << ", \"\" using 1:"<<index<<" with impulses lc rgb \""<<color<<"\" title \"" << sequence << "(+"<<charge<<")\"";

  }
  gnuplot << ", \"\" using 1:"<<(peptide_sequences.size()+3)<<" with impulses lc rgb \"green\" title \"Overlap\"" << endl;

  gnuplot.close();


  return 0;
}

string AssignIonsMPSMSpectrum::getName() {
  return "assign-ions-mpsm-spectrum";
}

string AssignIonsMPSMSpectrum::getDescription() {
  return 
    "Given a list a peptides, charges, tries to match ions to peaks in a scan in an ms2file";
}
