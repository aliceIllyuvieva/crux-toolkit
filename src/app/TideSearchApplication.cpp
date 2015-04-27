#include <cstdio>
#include "app/tide/abspath.h"
#include "app/tide/records_to_vector-inl.h"

#include "io/carp.h"
#include "parameter.h"
#include "io/SpectrumRecordWriter.h"
#include "TideIndexApplication.h"
#include "TideSearchApplication.h"
#include "app/tide/mass_constants.h"
#include "TideMatchSet.h"
#include "util/Params.h"
#include "util/FileUtils.h"

extern AA_MOD_T* list_of_mods[MAX_AA_MODS];
extern int num_mods;

bool TideSearchApplication::HAS_DECOYS = false;

/* This constant is the product of the original "magic number" (10000,
 * on line 4622 of search28.c) that was used to rescale the XCorr
 * score, and the integerization constant used by Benjamin Diament in
 * Tide.  In the Tide publication, that constant was reported as 10^7.
 * However, here it appears to be only 10^4.
 *
 * --WSN, 10 March 2015 */
const double TideSearchApplication::XCORR_SCALING = 100000000.0;

/* This constant is used to put the refactored XCorr back into the
 * same range as the original XCorr score.  It is the XCorr "magic
 * number" (10000) divided by the EVIDENCE_SCALE_INT (defined in
 * tide/spectrum_preprocess2.cc). */
const double TideSearchApplication::RESCALE_FACTOR = 20.0;

TideSearchApplication::TideSearchApplication() {
  exact_pval_search_ = false;
}

TideSearchApplication::~TideSearchApplication() {
}

int TideSearchApplication::main(int argc, char** argv) {
  initialize(argc, argv);

  carp(CARP_INFO, "Running tide-search...");

  string cmd_line = "crux tide-search";
  for (int i = 1; i < argc; ++i) {
    cmd_line += " ";
    cmd_line += argv[i];
  }

  string index_dir = Params::GetString("tide database index");
  string peptides_file = index_dir + "/pepix";
  string proteins_file = index_dir + "/protix";
  string auxlocs_file = index_dir + "/auxlocs";
  vector<string> input_files = Params::GetStrings("tide spectra file");

  double window = Params::GetDouble("precursor-window");
  WINDOW_TYPE_T window_type = string_to_window_type(Params::GetString("precursor-window-type"));

  // Check spectrum-charge parameter
  string charge_string = Params::GetString("spectrum-charge");
  int charge_to_search;
  if (charge_string == "all") {
    carp(CARP_DEBUG, "Searching all charge states");
    charge_to_search = 0;
  } else {
    charge_to_search = atoi(charge_string.c_str());
    if (charge_to_search < 1 || charge_to_search > 6) {
      carp(CARP_FATAL, "Invalid spectrum-charge value %s", charge_string.c_str());
    }
    carp(CARP_DEBUG, "Searching charge state %d", charge_to_search);
  }

  // Check scan-number parameter
  string scan_range = Params::GetString("scan-number");
  int min_scan, max_scan;
  if (scan_range.empty()) {
    min_scan = 0;
    max_scan = BILLION;
    carp(CARP_DEBUG, "Searching all scans");
  }
  else if (scan_range.find('-') == string::npos) {
    // Single scan
    min_scan = max_scan = atoi(scan_range.c_str());
    carp(CARP_DEBUG, "Searching single scan %d", min_scan);
  } else {
    if (!get_range_from_string(scan_range.c_str(), min_scan, max_scan)) {
      carp(CARP_FATAL, "The scan number range '%s' is invalid. "
           "Must be of the form <first>-<last>", scan_range.c_str());
    } else {
      if (min_scan > max_scan) {
        int tmp_scan = min_scan;
        min_scan = max_scan;
        max_scan = tmp_scan;
        carp(CARP_DEBUG, "Switched scan range min and max");
      }
      carp(CARP_DEBUG, "Searching scan range %d-%d", min_scan, max_scan);
    }
  }
  //check to compute exact p-value 
  exact_pval_search_ = Params::GetBool("exact-p-value");
  bin_width_  = Params::GetDouble("mz-bin-width");
  bin_offset_ = Params::GetDouble("mz-bin-offset");
  // for now don't allow XCorr p-value searches with variable bin width
  if (exact_pval_search_ && abs(bin_width_ - BIN_WIDTH_MONO) > 0.000001) {
    carp(CARP_FATAL, "tide-search with XCorr p-values and variable bin width "
                     "is not allowed in this version of Crux.");
  }

  // Check concat parameter
  bool concat = Params::GetBool("concat");
  if (concat) {
    OutputFiles::setConcat();
  }

  // Check compute-sp parameter
  bool compute_sp = Params::GetBool("compute-sp");
  if (Params::GetBool("sqt-output") && !compute_sp){
    compute_sp = true;
    carp(CARP_INFO, "Enabling parameter compute-sp since SQT output is enabled "
                    " (this will increase runtime).");
  }

  carp(CARP_INFO, "Reading index %s", index_dir.c_str());
  // Read proteins index file
  ProteinVec proteins;
  pb::Header protein_header;
  if (!ReadRecordsToVector<pb::Protein, const pb::Protein>(&proteins,
      proteins_file, &protein_header)) {
    carp(CARP_FATAL, "Error reading index (%s)", proteins_file.c_str());
  }
  carp(CARP_DEBUG, "Read %d proteins", proteins.size());

  //open a copy of peptide buffer for Amino Acid Frequency (AAF) calculation.
  double* aaFreqN = NULL;
  double* aaFreqI = NULL;
  double* aaFreqC = NULL;
  int* aaMass = NULL;
  int nAA = 0;
  
  if (exact_pval_search_) {
    pb::Header aaf_peptides_header;
    HeadedRecordReader aaf_peptide_reader(peptides_file, &aaf_peptides_header);

    if (!aaf_peptides_header.file_type() == pb::Header::PEPTIDES ||
        !aaf_peptides_header.has_peptides_header()) {
      carp(CARP_FATAL, "Error reading index (%s)", peptides_file.c_str());
    }
    MassConstants::Init(&aaf_peptides_header.peptides_header().mods(),
                        bin_width_, bin_offset_);
    ActivePeptideQueue* active_peptide_queue =
      new ActivePeptideQueue(aaf_peptide_reader.Reader(), proteins);
    nAA = active_peptide_queue->CountAAFrequency(bin_width_, bin_offset_,
                                                 &aaFreqN, &aaFreqI, &aaFreqC, &aaMass);
    delete active_peptide_queue;
  } // End calculation AA frequencies

  // Read auxlocs index file
  vector<const pb::AuxLocation*> locations;
  if (!ReadRecordsToVector<pb::AuxLocation>(&locations, auxlocs_file)) {
    carp(CARP_FATAL, "Error reading index (%s)", auxlocs_file.c_str());
  }
  carp(CARP_DEBUG, "Read %d auxlocs", locations.size());

  // Read peptides index file
  pb::Header peptides_header;
  HeadedRecordReader* peptide_reader =
    new HeadedRecordReader(peptides_file, &peptides_header);
  if (!peptides_header.file_type() == pb::Header::PEPTIDES ||
      !peptides_header.has_peptides_header()) {
    carp(CARP_FATAL, "Error reading index (%s)", peptides_file.c_str());
  }
  
  const pb::Header::PeptidesHeader& pepHeader = peptides_header.peptides_header();
  DECOY_TYPE_T headerDecoyType = (DECOY_TYPE_T)pepHeader.decoys();
  if (headerDecoyType != NO_DECOYS) {
    HAS_DECOYS = true;
    if (headerDecoyType == PROTEIN_REVERSE_DECOYS) {
      OutputFiles::setProteinLevelDecoys();
    }
  }

  MassConstants::Init(&pepHeader.mods(), bin_width_, bin_offset_);
  TideMatchSet::initModMap(pepHeader.mods());

  OutputFiles* output_files = NULL;
  ofstream* target_file = NULL;
  ofstream* decoy_file = NULL;
  if (Params::GetBool("sqt-output") || Params::GetBool("pepxml-output") ||
      Params::GetBool("mzid-output") || Params::GetBool("pin-output")) {
    carp(CARP_DEBUG, "Using OutputFiles to write matches");
    output_files = new OutputFiles(this);
  } else {
    carp(CARP_DEBUG, "Using TideMatchSet to write matches");
    bool overwrite = Params::GetBool("overwrite");
    stringstream ss;
    ss << Params::GetString("enzyme") << '-' << Params::GetString("digestion");
    TideMatchSet::setCleavageType(ss.str());
    if (!concat) {
      string target_file_name = make_file_path("tide-search.target.txt");
      target_file = create_stream_in_path(target_file_name.c_str(), NULL, overwrite);
      if (HAS_DECOYS) {
        string decoy_file_name = make_file_path("tide-search.decoy.txt");
        decoy_file = create_stream_in_path(decoy_file_name.c_str(), NULL, overwrite);
      }
    } else {
      string concat_file_name = make_file_path("tide-search.txt");
      target_file = create_stream_in_path(concat_file_name.c_str(), NULL, overwrite);
    }
  }

  if (output_files) {
    output_files->exact_pval_search_ = exact_pval_search_;
    output_files->writeHeaders();
  } else if (target_file) {
    TideMatchSet::writeHeaders(target_file, false, compute_sp);
    TideMatchSet::writeHeaders(decoy_file, true, compute_sp);
  }

  // Try to read all spectrum files as spectrumrecords, convert those that fail
  vector<InputFile> input_sr;
  for (vector<string>::const_iterator f = input_files.begin(); f != input_files.end(); f++) {
    SpectrumCollection spectra;
    pb::Header spectrum_header;
    string spectrumrecords = *f;
    bool keepSpectrumrecords = true;
    if (!spectra.ReadSpectrumRecords(spectrumrecords, &spectrum_header)) {
      // Failed, try converting to spectrumrecords file
      carp(CARP_INFO, "Converting %s to spectrumrecords format", f->c_str());
      spectrumrecords = Params::GetString("store-spectra");
      keepSpectrumrecords = !spectrumrecords.empty();
      if (!keepSpectrumrecords) {
        spectrumrecords = make_file_path(FileUtils::BaseName(*f) + ".spectrumrecords.tmp");
      } else if (input_files.size() > 1) {
        carp(CARP_FATAL, "Cannot use store-spectra option with multiple input "
                         "spectrum files");
      }
      carp(CARP_DEBUG, "New spectrumrecords filename: %s", spectrumrecords.c_str());
      if (!SpectrumRecordWriter::convert(*f, spectrumrecords)) {
        carp(CARP_FATAL, "Error converting %s to spectrumrecords format", f->c_str());
      }
      carp(CARP_DEBUG, "Reading converted spectra file %s", spectrumrecords.c_str());
      // Re-read converted file as spectrumrecords file
      if (!spectra.ReadSpectrumRecords(spectrumrecords, &spectrum_header)) {
        carp(CARP_DEBUG, "Deleting %s", spectrumrecords.c_str());
        remove(spectrumrecords.c_str());
        carp(CARP_FATAL, "Error reading spectra file %s", spectrumrecords.c_str());
      }
    }
    input_sr.push_back(InputFile(*f, spectrumrecords, keepSpectrumrecords));
  }

  // Loop through spectrum files
  for (vector<InputFile>::const_iterator f = input_sr.begin();
       f != input_sr.end();
       f++) {
    if (!peptide_reader) {
      peptide_reader = new HeadedRecordReader(peptides_file, &peptides_header);
    }
    ActivePeptideQueue* active_peptide_queue =
      new ActivePeptideQueue(peptide_reader->Reader(), proteins);
    active_peptide_queue->SetBinSize(bin_width_, bin_offset_);

    string spectra_file = f->SpectrumRecords;
    carp(CARP_INFO, "Reading spectra file %s", spectra_file.c_str());
    // Try to read file as spectrumrecords file
    SpectrumCollection spectra;
    pb::Header spectrum_header;
    if (!spectra.ReadSpectrumRecords(spectra_file, &spectrum_header)) {
      // This should never happen since we would have failed earlier
      carp(CARP_FATAL, "Error reading spectra file %s", spectra_file.c_str());
    }

    carp(CARP_INFO, "Sorting spectra");
    if (window_type != WINDOW_MZ) {
      spectra.Sort();
    } else {
      spectra.Sort<ScSortByMz>(ScSortByMz(window));
    }

    double highest_mz = spectra.FindHighestMZ();
    unsigned int spectrum_num = spectra.SpecCharges()->size();
    if (spectrum_num > 0 && exact_pval_search_) {
      highest_mz = spectra.SpecCharges()->at(spectrum_num - 1).neutral_mass;
    }
    carp(CARP_DEBUG, "Max m/z %f", highest_mz);
    MaxBin::SetGlobalMax(highest_mz);

    // Do the search
    carp(CARP_INFO, "Running search");
    cleanMods();
    search(f->OriginalName, spectra.SpecCharges(), active_peptide_queue, proteins,
           locations, window, window_type, Params::GetDouble("spectrum-min-mz"),
           Params::GetDouble("spectrum-max-mz"), min_scan, max_scan,
           Params::GetInt("min-peaks"), charge_to_search,
           Params::GetInt("top-match"), spectra.FindHighestMZ(),
           output_files, target_file, decoy_file, compute_sp,
           nAA, aaFreqN, aaFreqI, aaFreqC, aaMass);
    // Delete temporary spectrumrecords file
    if (!f->Keep) {
      carp(CARP_DEBUG, "Deleting %s", spectra_file.c_str());
      remove(spectra_file.c_str());
    }

    // Clean up
    delete active_peptide_queue;
    delete peptide_reader;
    peptide_reader = NULL;
  } // End of spectrum file loop

  for (ProteinVec::iterator i = proteins.begin(); i != proteins.end(); ++i) {
    delete *i;
  }
  if (output_files) {
    delete output_files;
  }
  if (target_file) {
    delete target_file;
    if (decoy_file) {
      delete decoy_file;
    }
  }
  delete[] aaFreqN;
  delete[] aaFreqI;
  delete[] aaFreqC;
  delete[] aaMass;

  return 0;
}

/**
 * Free all existing mods
 */
void TideSearchApplication::cleanMods() {
  for (int i = 0; i < MAX_AA_MODS; ++i) {
    free_aa_mod(list_of_mods[i]);
    list_of_mods[i] = NULL;
  }
  num_mods = 0;
}

void TideSearchApplication::search(
  const string& spectrum_filename,
  const vector<SpectrumCollection::SpecCharge>* spec_charges,
  ActivePeptideQueue* active_peptide_queue,
  const ProteinVec& proteins,
  const vector<const pb::AuxLocation*>& locations,
  double precursor_window,
  WINDOW_TYPE_T window_type,
  double spectrum_min_mz,
  double spectrum_max_mz,
  int min_scan,
  int max_scan,
  int min_peaks,
  int search_charge,
  int top_matches,
  double highest_mz,
  OutputFiles* output_files,
  ofstream* target_file,
  ofstream* decoy_file,
  bool compute_sp,
  int nAA, 
  double* aaFreqN,
  double* aaFreqI,
  double* aaFreqC,
  int* aaMass
) {
  int elution_window = Params::GetInt("elution-window-size");
  bool peptide_centric = Params::GetBool("peptide-centric-search");
  if (peptide_centric == false) {
    elution_window = 0;
  }
  active_peptide_queue->setElutionWindow(elution_window);
  active_peptide_queue->setPeptideCentric(peptide_centric);

  if (elution_window > 0 && elution_window % 2 == 0) {
     active_peptide_queue->setElutionWindow(elution_window+1);
  }
  
  if (!peptide_centric || !exact_pval_search_){
      active_peptide_queue->setElutionWindow(0);
  }

  active_peptide_queue->SetOutputs(output_files, &locations, top_matches, compute_sp, target_file, decoy_file,highest_mz);  

  // This is the main search loop.
  ObservedPeakSet observed(bin_width_, bin_offset_,
                           Params::GetBool("use-neutral-loss-peaks"),
                           Params::GetBool("use-flanking-peaks"));
  int max_charge = Params::GetInt("max-precursor-charge");    

  // cycle through spectrum-charge pairs, sorted by neutral mass
  unsigned sc_index = 0;
  FLOAT_T sc_total = (FLOAT_T)spec_charges->size();
  int print_interval = Params::GetInt("print-search-progress");
  int total_candidate_peptides = 0;
  for (vector<SpectrumCollection::SpecCharge>::const_iterator sc = spec_charges->begin();
       sc != spec_charges->end();
       ++sc) {
    Spectrum* spectrum = sc->spectrum;

    double precursor_mz = spectrum->PrecursorMZ();
    int charge = sc->charge;
    int scan_num = spectrum->SpectrumNumber();
    if (precursor_mz < spectrum_min_mz || precursor_mz > spectrum_max_mz ||
        scan_num < min_scan || scan_num > max_scan ||
        spectrum->Size() < min_peaks ||
        (search_charge != 0 && charge != search_charge) || charge > max_charge) {
      continue;
    }

    // The active peptide queue holds the candidate peptides for spectrum.
    // Calculate and set the window, depending on the window type.
    double min_mass, max_mass, min_range, max_range;
    computeWindow(*sc, window_type, precursor_window, max_charge, &min_mass, &max_mass, &min_range, &max_range);
    if (!exact_pval_search_) {  //execute original tide-search program

      // Normalize the observed spectrum and compute the cache of
      // frequently-needed values for taking dot products with theoretical
      // spectra.
      observed.PreprocessSpectrum(*spectrum, charge);
      int nCandPeptide = active_peptide_queue->SetActiveRange(min_mass, max_mass, min_range, max_range);
      total_candidate_peptides += nCandPeptide;
      TideMatchSet::Arr2 match_arr2(nCandPeptide); // Scored peptides will go here.

      // Programs for taking the dot-product with the observed spectrum are laid
      // out in memory managed by the active_peptide_queue, one program for each
      // candidate peptide. The programs will store the results directly into
      // match_arr. We now pass control to those programs.
      collectScoresCompiled(active_peptide_queue, spectrum, observed, &match_arr2,
                            nCandPeptide, charge);

      // matches will arrange the results in a heap by score, return the top
      // few, and recover the association between counter and peptide. We output
      // the top matches.
      if (peptide_centric) {
          deque<Peptide*>::const_iterator iter_ = active_peptide_queue->iter_;
          TideMatchSet::Arr2::iterator it = match_arr2.begin();
          for (; it != match_arr2.end(); ++iter_, ++it) {
                   (*iter_)->AddHit(spectrum, it->first,0.0,it->second,charge);
          }
      } else {  //spectrum centric match report.
        TideMatchSet::Arr match_arr(nCandPeptide);
        for (TideMatchSet::Arr2::iterator it = match_arr2.begin();
             it != match_arr2.end();
             ++it) {
          TideMatchSet::Pair pair;
          pair.first.first = (double)(it->first / XCORR_SCALING);
          pair.first.second = 0.0;
          pair.second = it->second;
          match_arr.push_back(pair);
        }
        TideMatchSet matches(&match_arr, highest_mz);
        matches.exact_pval_search_ = exact_pval_search_;
        if (output_files) {
          matches.report(output_files, top_matches, spectrum_filename, spectrum, charge,
                         active_peptide_queue, proteins, locations, compute_sp, true);
        } else {
          matches.report(target_file, decoy_file, top_matches, spectrum_filename,
                         spectrum, charge, active_peptide_queue, proteins,
                         locations, compute_sp, true);
        }
      }  //end peptide_centric == true
    } else {  // execute exact-pval-search

      const int minDeltaMass = aaMass[0];
      const int maxDeltaMass = aaMass[nAA - 1];

      int maxPrecurMass = floor(MaxBin::Global().CacheBinEnd() + 50.0); // TODO works, but is this the best way to get?
      int nCandPeptide = active_peptide_queue->SetActiveRangeBIons(min_mass, max_mass, min_range, max_range);
      total_candidate_peptides += nCandPeptide;
      TideMatchSet::Arr match_arr(nCandPeptide); // scored peptides will go here.
  
      // iterators needed at multiple places in following code
      deque<Peptide*>::const_iterator iter_ = active_peptide_queue->iter_;
      deque<TheoreticalPeakSetBIons>::const_iterator iter1_ = active_peptide_queue->iter1_;
      vector<int>::const_iterator iter_int;
      vector<unsigned int>::const_iterator iter_uint;

      //************************************************************************
      /* For one observed spectrum, calculates:
       *  - vector of cleavage evidence
       *  - score count vectors for a range of integer masses
       *  - p-values of XCorr match scores between spectrum and all selected candidate target and decoy peptides
       * Written by Jeff Howbert, October, 2013.
       * Ported to and integrated with Tide by Jeff Howbert, November, 2013.
       */
      int pe;
      int ma;
      int pepMaInt;
      int* pepMassInt = new int[nCandPeptide];
      vector<int> pepMassIntUnique;
      pepMassIntUnique.reserve(nCandPeptide);
      pe = 0;
      for (iter_ = active_peptide_queue->iter_;
           iter_ != active_peptide_queue->end_;
           ++iter_) {
        double pepMass = (*iter_)->Mass();
        pepMaInt = MassConstants::mass2bin(pepMass);
        pepMassInt[pe] = pepMaInt;
        pepMassIntUnique.push_back(pepMaInt);
        pe++;
      }
      std::sort(pepMassIntUnique.begin(), pepMassIntUnique.end());
      vector<int>::iterator last = std::unique(pepMassIntUnique.begin(),
                                               pepMassIntUnique.end());
      pepMassIntUnique.erase(last, pepMassIntUnique.end());
      int nPepMassIntUniq = (int)pepMassIntUnique.size();

      int** evidenceObs = new int*[nPepMassIntUniq];
      int* scoreOffsetObs = new int[nPepMassIntUniq];
      double** pValueScoreObs = new double*[nPepMassIntUniq];
      int* intensArrayTheor = new int [maxPrecurMass]; // initialized later in loop
      for (pe = 0; pe < nPepMassIntUniq; pe++) { // TODO should probably instead use iterator over pepMassIntUnique
        evidenceObs[pe] = new int[maxPrecurMass];
        for (ma = 0; ma < maxPrecurMass; ma++) {
          evidenceObs[pe][ma] = 0;
        }
        scoreOffsetObs[pe] = 0;
        pepMaInt = pepMassIntUnique[pe]; // TODO should be accessed with an iterator
        // preprocess to create one integerized evidence vector for each cluster of masses among selected peptides
        double pepMassMonoMean = (pepMaInt - 0.5 + bin_offset_) * bin_width_;
        observed.CreateEvidenceVector(*spectrum, bin_width_, bin_offset_, charge,
                                      pepMassMonoMean, maxPrecurMass, evidenceObs[pe]);
        // NOTE: will have to go back to separate dynamic programming for
        //       target and decoy if they have different probNI and probC
        int maxEvidence = *std::max_element(evidenceObs[pe], evidenceObs[pe] + maxPrecurMass);
        int minEvidence = *std::min_element(evidenceObs[pe], evidenceObs[pe] + maxPrecurMass);
        // estimate maxScore and minScore
        int maxNResidue = (int)floor((double)pepMaInt / (double)minDeltaMass);
        vector<int> sortEvidenceObs (evidenceObs[pe], evidenceObs[pe] + maxPrecurMass);
        std::sort(sortEvidenceObs.begin(), sortEvidenceObs.end(), greater<int>());
        int maxScore = 0;
        int minScore = 0;
        for (int sc = 0; sc < maxNResidue; sc++) {
          maxScore += sortEvidenceObs[sc];
        }
        for (int sc = maxPrecurMass - maxNResidue; sc < maxPrecurMass; sc++) {
          minScore += sortEvidenceObs[sc];
        }
        int bottomRowBuffer = maxEvidence + 1;
        int topRowBuffer = -minEvidence;
        int nRowDynProg = bottomRowBuffer - minScore + 1 + maxScore + topRowBuffer;
        pValueScoreObs[pe] = new double[nRowDynProg];

        scoreOffsetObs[pe] = calcScoreCount(maxPrecurMass, evidenceObs[pe], pepMaInt,
                                            maxEvidence, minEvidence, maxScore, minScore, 
                                            nAA, aaFreqN, aaFreqI, aaFreqC, aaMass,
                                            pValueScoreObs[pe]);
      }

      // ***** calculate p-values for peptide-spectrum matches ***********************************
      iter_ = active_peptide_queue -> iter_;
      iter1_ = active_peptide_queue -> iter1_;
      for (pe = 0; pe < nCandPeptide; pe++) { // TODO should probably use iterator instead
        int pepMassIntIdx = 0;
        for (ma = 0; ma < nPepMassIntUniq; ma++) { // TODO should probably use iterator instead
          if (pepMassIntUnique[ma] == pepMassInt[pe]) { // TODO pepMassIntUnique should be accessed with an iterator
            pepMassIntIdx = ma;
            break;
          }
        }
        // score XCorr for target peptide with integerized evidenceObs array
        for (ma = 0; ma < maxPrecurMass; ma++) {
          intensArrayTheor[ma] = 0;
        }
        for (iter_uint = iter1_->unordered_peak_list_.begin();
             iter_uint != iter1_->unordered_peak_list_.end();
             iter_uint++) {
          intensArrayTheor[*iter_uint] = 1;
        }

        int scoreRefactInt = 0;
        for (ma = 0; ma < maxPrecurMass; ma++) {
          scoreRefactInt += evidenceObs[pepMassIntIdx][ma] * intensArrayTheor[ma];
        }
        int scoreCountIdx = scoreRefactInt + scoreOffsetObs[pepMassIntIdx];
        double pValue = pValueScoreObs[pepMassIntIdx][scoreCountIdx];
        if (peptide_centric){
            (*iter_)->AddHit(spectrum, pValue, (double)scoreRefactInt, nCandPeptide - pe, charge);
        } else {
          TideMatchSet::Pair pair;
          pair.first.first = pValue;
          pair.first.second = (double)scoreRefactInt / RESCALE_FACTOR;
          pair.second = nCandPeptide - pe; // TODO ugly hack to conform with the way these indices are generated in standard tide-search
          match_arr.push_back(pair);
       }

        // move to next peptide and b ion queue
        ++iter_; // TODO need to add test to make sure haven't gone past available peptides
        ++iter1_; // TODO need to add test to make sure haven't gone past available b ion queues
      }
      // clean up
      delete [] pepMassInt;
      delete [] scoreOffsetObs;
      for (pe = 0; pe < nPepMassIntUniq; pe++) {
        delete [] evidenceObs[pe];
        delete [] pValueScoreObs[pe];
      }
      delete [] evidenceObs;
      delete [] pValueScoreObs;
      delete [] intensArrayTheor;
      if (!peptide_centric){
        // matches will arrange the results in a heap by score, return the top
        // few, and recover the association between counter and peptide. We output
        // the top matches.
          TideMatchSet matches(&match_arr, highest_mz);
          matches.exact_pval_search_ = exact_pval_search_;
          if (output_files) {
            matches.report(output_files, top_matches, spectrum_filename, spectrum, charge,
                           active_peptide_queue, proteins, locations, compute_sp, false);
          } else {
            matches.report(target_file, decoy_file, top_matches, spectrum_filename,
                           spectrum, charge, active_peptide_queue, proteins,
                           locations, compute_sp, false);
          }
      } // end peptide_centric == true
   }

    ++sc_index;
    if (print_interval > 0 && sc_index % print_interval == 0) {
      carp(CARP_INFO, "%d spectrum-charge combinations searched, %.0f%% complete",
           sc_index, sc_index / sc_total * 100);
    }
  }
  carp(CARP_INFO, "Time per spectrum-charge combination: %lf s.", wall_clock() / (1e6*sc_total));
  carp(CARP_INFO, "Average number of candidates per spectrum-charge combination: %lf ",
                  total_candidate_peptides / sc_total);  
  if (output_files) {
    output_files->writeFooters();
  }
}

void TideSearchApplication::collectScoresCompiled(
  ActivePeptideQueue* active_peptide_queue,
  const Spectrum* spectrum,
  const ObservedPeakSet& observed,
  TideMatchSet::Arr2* match_arr,
  int queue_size,
  int charge
) {
  if (!active_peptide_queue->HasNext()) {
    return;
  }
  // prog gets the address of the dot-product program for the first peptide
  // in the active queue.
  const void* prog = active_peptide_queue->NextPeptide()->Prog(charge);
  const int* cache = observed.GetCache();
  // results will get (score, counter) pairs, where score is the dot product
  // of the observed peak set with a candidate peptide. The candidate
  // peptide is given by counter, which refers to the index within the
  // ActivePeptideQueue, counting from the back. This complication
  // simplifies the generated programs, which now simply dump the counter.
  pair<int, int>* results = match_arr->data();

  // See compiler.h for a description of the programs beginning at prog and
  // how they are generated. Here we initialize certain registers to the
  // values expected by the programs and call the first one (*prog).
  //
  // See gnu assembler format for more on this format. We tell the compiler
  // to set these registers:
  // edx/rdx points to the cache.
  // eax/rax points to the first program.
  // ecx/rcx is the counter and gets the size of the active queue.
  // edi/rdi points to the results buffer.
  //
  // The push and pop operations are a workaround for a compiler that
  // doesn't understand that %ecx and %edi (or %rcx and %rdi) get
  // clobbered. Since they're already input registers, they can't be
  // included in the clobber list.

#ifdef _MSC_VER
#ifdef _WIN64
  DWORD64 rcx;
  DWORD64 rdi;

  volatile bool restored = false;
  CONTEXT context;
  RtlCaptureContext(&context);
  if (!restored) {
    rcx = context.Rcx;
    rdi = context.Rdi;

    context.Rdx = (DWORD64)cache;
    context.Rax = (DWORD64)prog;
    context.Rcx = (DWORD64)queue_size;
    context.Rdi = (DWORD64)results;

    restored = true;
    RtlRestoreContext(&context, NULL);
  } else {
    ((void(*)(void))prog)();
  }

  restored = false;
  RtlCaptureContext(&context);
  if (!restored) {
    context.Rcx = rcx;
    context.Rdi = rdi;

    restored = true;
    RtlRestoreContext(&context, NULL);
  }
#else
  __asm {
    cld
    push ecx
    push edi
    mov edx, cache
    mov eax, prog
    mov ecx, queue_size
    mov edi, results
    call eax
    pop edi
    pop ecx
  }
#endif
#else
  __asm__ __volatile__("cld\n" // stos operations increment edi
#ifdef __x86_64__
                       "push %%rcx\n"
                       "push %%rdi\n"
                       "call *%%rax\n"
                       "pop %%rdi\n"
                       "pop %%rcx\n"
#else
                       "push %%ecx\n"
                       "push %%edi\n"
                       "call *%%eax\n"
                       "pop %%edi\n"
                       "pop %%ecx\n"
#endif
                       : // no outputs
                       : "d" (cache),
                         "a" (prog),
                         "c" (queue_size),
                         "D" (results)
  );
#endif

  // match_arr is filled by the compiled programs, not by calls to
  // push_back(). We have to set the final size explicitly.
  match_arr->set_size(queue_size);
}

void TideSearchApplication::computeWindow(
  const SpectrumCollection::SpecCharge& sc,
  WINDOW_TYPE_T window_type,
  double precursor_window,
  int max_charge,
  double* out_min,
  double* out_max,
  double* min_range,
  double* max_range
) {
  switch (window_type) {
  case WINDOW_MASS:
    *out_min = sc.neutral_mass - precursor_window;
    *out_max = sc.neutral_mass + precursor_window;
    *min_range = *out_min;
    *max_range = *out_max;
    break;
  case WINDOW_MZ: {
    double mz_minus_proton = sc.spectrum->PrecursorMZ() - MASS_PROTON;
    *out_min = (mz_minus_proton - precursor_window) * sc.charge;
    *out_max = (mz_minus_proton + precursor_window) * sc.charge;
    *min_range = mz_minus_proton*sc.charge - precursor_window*max_charge;
    *max_range = mz_minus_proton*sc.charge + precursor_window*max_charge;
    break;
    }
  case WINDOW_PPM: {
    double tiny_precursor = precursor_window * 1e-6;
    *out_min = sc.neutral_mass * (1.0 - tiny_precursor);
    *out_max = sc.neutral_mass * (1.0 + tiny_precursor);
    *min_range = *out_min;
    *max_range = *out_max;
   break;
    }
  default:
    carp(CARP_FATAL, "Invalid window type");
  }
  carp(CARP_DETAILED_DEBUG, "Scan %d.%d mass window is [%f, %f]",
       sc.spectrum->SpectrumNumber(), sc.charge, *out_min, *out_max);
}

bool TideSearchApplication::hasDecoys() {
  return HAS_DECOYS;
}

string TideSearchApplication::getName() const {
  return "tide-search";
}

string TideSearchApplication::getDescription() const {
  return
    "[[nohtml:Search a collection of spectra against a sequence database, "
    "returning a collection of peptide-spectrum matches (PSMs). This is a "
    "fast search engine but requires that you first build an index with "
    "tide-index.]]"
    "[[html:<p>Tide is a tool for identifying peptides from tandem mass "
    "spectra. It is an independent reimplementation of the SEQUEST<sup>&reg;"
    "</sup> algorithm, which assigns peptides to spectra by comparing the "
    "observed spectra to a catalog of theoretical spectra derived from a "
    "database of known proteins. Tide's primary advantage is its speed. Our "
    "published paper provides more detail on how Tide works. If you use Tide "
    "in your research, please cite:</p><blockquote>Benjamin J. Diament and "
    "William Stafford Noble. <a href=\"http://dx.doi.org/10.1021/pr101196n\">"
    "&quot;Faster SEQUEST Searching for Peptide Identification from Tandem "
    "Mass Spectra&quot;</a>. <em>Journal of Proteome Research</em>. "
    "10(9):3871-9, 2011.</blockquote><p>To use <code>crux tide-search</code>, "
    "you must first create a database index using the <code>crux tide-index"
    "</code> command.</p><p>When <code>tide-search</code> runs, it performs "
    "several intermediate steps, as follows:</p><ol><li>Convert the given "
    "fragmentation spectra to a binary format.</li><li>Search the spectra "
    "against the database and store the results in binary format.</li><li>"
    "Convert the results to one or more requested output formats.</li></ol><p>"
    "By default, the intermediate binary files are stored in the output "
    "directory and deleted when Tide finishes execution. If you plan to search "
    "a given set of spectra more than once, then you can direct Tide to save "
    "the binary spectrum files. Subsequent runs of the program will go faster "
    "if provided with inputs in binary format.</p>]]";
}

vector<string> TideSearchApplication::getArgs() const {
  string arr[] = {
    "tide spectra file+",
    "tide database index"
  };
  return vector<string>(arr, arr + sizeof(arr) / sizeof(string));
}

vector<string> TideSearchApplication::getOptions() const {
  string arr[] = {
    "precursor-window",
    "precursor-window-type",
    "spectrum-min-mz",
    "spectrum-max-mz",
    "min-peaks",
    "spectrum-charge",
    "scan-number",
    "top-match",
    "store-spectra",
    "concat",
    "compute-sp",
    "remove-precursor-peak",
    "remove-precursor-tolerance",
    "print-search-progress",
    "spectrum-parser",
    "use-z-line",
    "txt-output",
    "sqt-output",
    "pepxml-output",
    "mzid-output",
    "pin-output",
    "fileroot",
    "output-dir",
    "overwrite",
    "parameter-file",
    "exact-p-value",
    "use-neutral-loss-peaks",
    "use-flanking-peaks",
    "mz-bin-width",
    "mz-bin-offset",
    "max-precursor-charge",
    "peptide-centric-search",
    "elution-window-size",
    "verbosity"
  };
  return vector<string>(arr, arr + sizeof(arr) / sizeof(string));
}

map<string, string> TideSearchApplication::getOutputs() const {
  map<string, string> outputs;
  outputs["tide-search.target.txt"] =
    "a tab-delimited text file containing the target PSMs. See <a href=\""
    "txt-format.html\">txt file format</a> for a list of the fields.";
  outputs["tide-search.decoy.txt"] =
    "a tab-delimited text file containing the decoy PSMs. This file will only "
    "be created if the index was created with decoys.";
  outputs["tide-search.params.txt"] =
    "a file containing the name and value of all parameters/options for the "
    "current operation. Not all parameters in the file may have been used in "
    "the operation. The resulting file can be used with the --parameter-file "
    "option for other Crux programs.";
  outputs["tide-search.log.txt"] =
    "a log file containing a copy of all messages that were printed to the "
    "screen during execution.";
  return outputs;
}
bool TideSearchApplication::needsOutputDirectory() const {
  return true;
}

COMMAND_T TideSearchApplication::getCommand() const {
  return TIDE_SEARCH_COMMAND;
}

/* Calculates counts of peptides with various XCorr scores, given a preprocessed
 * MS2 spectrum, using dynamic programming.
 * Written by Jeff Howbert, October, 2012 (as function calcScoreCount).
 * Ported to and integrated with Tide by Jeff Howbert, November, 2013.
 */
int TideSearchApplication::calcScoreCount(
  int numelEvidenceObs,
  int* evidenceObs,
  int pepMassInt,
  int maxEvidence,
  int minEvidence,
  int maxScore,
  int minScore,
  int nAA,
  double* aaFreqN,
  double* aaFreqI,
  double* aaFreqC,
  int* aaMass,
  double* pValueScoreObs
) {
  const int nDeltaMass = nAA;
  int minDeltaMass = aaMass[0];
  int maxDeltaMass = aaMass[nDeltaMass - 1];

  // internal variables
  int row;
  int col;
  int ma;
  int evidence;
  int de;
  int evidenceRow;
  double sumScore;

  int bottomRowBuffer = maxEvidence + 1;
  int topRowBuffer = -minEvidence;
  int colBuffer = maxDeltaMass;
  int colStart = MassConstants::mass2bin(MassConstants::mono_h);
  int scoreOffsetObs = bottomRowBuffer - minScore;

  int nRow = bottomRowBuffer - minScore + 1 + maxScore + topRowBuffer;
  int nCol = colBuffer + pepMassInt;
  int rowFirst = bottomRowBuffer;
  int rowLast = rowFirst - minScore + maxScore;
  int colFirst = colStart + MassConstants::mass2bin(MassConstants::mono_h);
  int colLast = MassConstants::mass2bin(MassConstants::bin2mass(pepMassInt)
      - MassConstants::mono_oh 
      );
  int initCountRow = bottomRowBuffer - minScore;
  int initCountCol = maxDeltaMass + colStart;

  double** dynProgArray = new double*[nRow];
  for (row = 0; row < nRow; row++) {
    dynProgArray[row] = new double[nCol];
    for (col = 0; col < nCol; col++) {
      dynProgArray[row][col] = 0.0;
    }
  }
  double* scoreCountBinAdjust = 0;
  scoreCountBinAdjust = new double [nRow];
  for (row = 0; row < nRow; row++) {
    scoreCountBinAdjust[row] = 0.0;
  }

  dynProgArray[initCountRow][initCountCol] = 1.0; // initial count of peptides with mass = 1
  vector<int> deltaMassCol(nDeltaMass);
  // populate matrix with scores for first (i.e. N-terminal) amino acid in sequence
  for (de = 0; de < nDeltaMass; de++) {
    ma = aaMass[de];
    row = initCountRow + evidenceObs[ma + colStart];
    col = initCountCol + ma;
    if (col <= maxDeltaMass + colLast) {
      dynProgArray[row][col] += dynProgArray[initCountRow][initCountCol] * aaFreqN[de];
    }
  }
  // set to zero now that score counts for first amino acid are in matrix
  dynProgArray[initCountRow][initCountCol] = 0.0;
  // populate matrix with score counts for non-terminal amino acids in sequence 
  for (ma = colFirst; ma < colLast; ma++) {
    col = maxDeltaMass + ma;
    evidence = evidenceObs[ma];
    for (de = 0; de < nDeltaMass; de++) {
      deltaMassCol[de] = col - aaMass[de];
    }
    for (row = rowFirst; row <= rowLast; row++) {
      evidenceRow = row - evidence;
      sumScore = dynProgArray[row][col];
      for (de = 0; de < nDeltaMass; de++) {
        sumScore += dynProgArray[evidenceRow][deltaMassCol[de]] * aaFreqI[de];
      }
      dynProgArray[row][col] = sumScore;
    }
  }
  // populate matrix with score counts for last (i.e. C-terminal) amino acid in sequence
  ma = colLast;
  col = maxDeltaMass + ma;
  evidence = 0; // no evidence should be added for last amino acid in sequence
  for (de = 0; de < nDeltaMass; de++) {
    deltaMassCol[de] = col - aaMass[de];
  }
  for (row = rowFirst; row <= rowLast; row++) {
    evidenceRow = row - evidence;
    sumScore = 0.0;
    for (de = 0; de < nDeltaMass; de++) {
      sumScore += dynProgArray[evidenceRow][deltaMassCol[de]] * aaFreqC[de];  // C-terminal residue
    }
    dynProgArray[row][col] = sumScore;
  }

  int colScoreCount = maxDeltaMass + colLast;
  double totalCount = 0.0;
  for (row = 0; row < nRow; row++) {
    // at this point pValueScoreObs just holds counts from last column of dynamic programming array
    pValueScoreObs[row] = dynProgArray[row][colScoreCount];
    totalCount += pValueScoreObs[row];
    scoreCountBinAdjust[row] = pValueScoreObs[row] / 2.0;
  }
  // convert from counts to cumulative sum of counts
  for (row = nRow - 2; row >= 0; row--) {
    pValueScoreObs[row] += pValueScoreObs[row + 1];
  }
  double logTotalCount = log(totalCount);
  for (row = 0; row < nRow; row++) {
    // adjust counts to reflect center of bin, not edge
    pValueScoreObs[row] -= scoreCountBinAdjust[row];
    // normalize distribution; use exp( log ) to avoid potential underflow
    pValueScoreObs[row] = exp(log(pValueScoreObs[row]) - logTotalCount);
  }

  // clean up
  for (row = 0; row < nRow; row++) {
    delete [] dynProgArray[row];
  }
  delete [] dynProgArray;
  delete [] scoreCountBinAdjust;
  
  return scoreOffsetObs;
}

void TideSearchApplication::processParams() {
  pb::Header peptides_header;
  string peptides_file = Params::GetString("tide database index") + "/pepix";
  HeadedRecordReader peptide_reader(peptides_file, &peptides_header);
  if (!peptides_header.file_type() == pb::Header::PEPTIDES ||
      !peptides_header.has_peptides_header()) {
    carp(CARP_FATAL, "Error reading index (%s)", peptides_file.c_str());
  }

  const pb::Header::PeptidesHeader& pepHeader = peptides_header.peptides_header();

  Params::Set("enzyme", pepHeader.enzyme());
  char* digestString =
    digest_type_to_string(pepHeader.full_digestion() ? FULL_DIGEST : PARTIAL_DIGEST);
  Params::Set("digestion", digestString);
  free(digestString);
  Params::Set("monoisotopic-precursor", pepHeader.monoisotopic_precursor() ? true : false);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 2
 * End:
 */