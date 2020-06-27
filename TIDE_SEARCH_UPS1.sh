#!/bin/bash

ROOT=/home/alice/crux-toolkit
CRUX=$ROOT/src/crux
ROOT=/home/alice/crux-toolkit/crux_test/

FIX_PARAM="--precursor-window 100 --precursor-window-type ppm --top-match 1 --concat T --overwrite T --num-threads 1 --use-neutral-loss-peaks F --min-peaks 10 --max-precursor-charge 9"
IDX_POST=_IDX
DATA_FILE=/home/data/mass_spec_data/UPS1.recalre.mzML
TAXON=UPS1


FOLDER_POST=_BASELINE_LR
PARAM="--mz-bin-width 1.0005079 --mz-bin-offset 0.4 --use-flanking-peaks F"
INDEX=$ROOT$TAXON$IDX_POST
OUTPUT_DIR=$ROOT$TAXON$FOLDER_POST
$CRUX tide-search $FIX_PARAM $PARAM --output-dir $OUTPUT_DIR $DATA_FILE $INDEX
$CRUX assign-confidence --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/tide-search.txt

