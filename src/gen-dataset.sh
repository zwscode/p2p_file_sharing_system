#!/bin/bash
# This script generates the datasets used in the fileio experiments

# create the datasets directory
mkdir -p shared_files

# Check if prefix is provided
if [ -z "$1" ]; then
    echo "No prefix provided, abort."
    exit 1
else
    echo "prefix: $1"
fi


gen_file() {
    local prefix="$1"
    local file_number="$2"
    local block_size="$3"
    local block_number="$4"
    for ((i = 1; i <= $file_number; i++)) do
        local file_name="${prefix}_${i}.bin"
        dd if=/dev/zero of=shared_files/$file_name bs=$3 count=$4
    done
}

// generate small files
gen_file "$1_small_file" 100 10K 1

// generate large files
gen_file "$1_large_file" 10 1M 100
