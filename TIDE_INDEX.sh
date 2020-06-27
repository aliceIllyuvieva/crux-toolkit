#!/bin/bash

ROOT=/home/alice/crux-toolkit
CRUX=$ROOT/src/crux
ROOT=/home/alice/crux-toolkit/crux_test/
FIX_PARAM="--missed-cleavages 1 --overwrite T --peptide-list T"
FOLDER_POST=_IDX

FASTA=/home/data/Fasta/ups1.fasta
TAXON=UPS1
PARAM="--max-mods 1 --mods-spec 1M+15.9949"
$CRUX tide-index $FIX_PARAM $PARAM --output-dir $ROOT$TAXON$FOLDER_POST $FASTA $ROOT$TAXON$FOLDER_POST



 