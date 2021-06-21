# SILVER (SIngle celL Variant callER)
`SILVER` is able to cluster cells and perform variant calling based on information obtained from single-cell DNA sequencing. `SILVER` takes as input `BAM` files containing the aligned data for each cell and provides as output a clustering of the cells and, optionally, VCF files pinpointing the changes relative to a reference genome.

`SILVER` is descirbed in detail in the following paper: [Clustering cells based on single-cell DNA-sequencing data withultra-low coverage](coming soon)

## Prerequisites
* GNU GCC with C++17 (gcc-8.0.1 or higher), LLVM Clang (clang-7 or higher), or AppleClang
* cmake 3.13 or newer
* omp
* openblas

## Installation
Clone the latest version of the code from the git repository:
```
git clone https://github.com/ratschlab/silver.git
```
### Building (Mac or Linux)
* `brew install libomp openblas` (for Mac) or `sudo apt-get install libblas-dev` (Linux)
* `mkdir silver/build; cd silver/build` 
* `cmake .. -DCMAKE_BUILD_TYPE=Release`
* `make -j`
* `./tests`

## Usage examples
### Creating the input pileup files
If you have a bunch of BAM files, the first step is to create a pileup (a file that contains all the sequenced bases for each locus) file. You can either split the BAM files by chromosome and then distribute the pileup creation to 23 jobs, or you can just go the easy way and pile up all the data in one go. This step doesn't require much RAM, as the pileup is streamed to disk while being created - 35GB should suffice even when processing 8000 cells at coverage 0.5x (~1.4TB of data):
```
./pileup -i <BAM_DIR> -o <OUT_DIR>/chromosome --chromosomes 1 --num_threads 20 --log_level=trace --min_base_quality 30 --max_coverage 1000
```
The command above will create a pileup for the first chromosome. If you ommit the `--chromosome` parameter, `pileup` will create 24 pileup files, one for each chromosome.

The pileup files are created in SILVER's binary format (extension .bin) and in the `samtools` compatible textual format (extension .txt), which is useful for debugging and manual inspection.

### Clustering and variant calling
```
./silver -i <PILEUP_DIR> -o <OUT_DIR> --num_threads 20 --log_level=trace \
             --not_informative_rate=0.5 --seq_error_rate=0.01 \
             --reference_genome=OPTIONAL_REFERENCE_GENOME_FASTA \
             --max_coverage 1000 --min_cluster_size 500
```

This will run the clustering and variant calling on the pileup files available in `PILEUP_DIR` and write the clustering information to `OUT_DIR/clustering`. If a reference genome was provided via `--reference_genome`, the VCF files for each cluster will be written to `OUT_DIR/cluster_<cluster_no>.vcf`.

You will need enough RAM to fit the pileups (they are loaded in memory for faster processing; if you're short on RAM it's easy to change the code to only load the relevant loci - drop me a line, should reduce the RAM usage by 10x, but it will be slower). I our case, we had 300GB worth of pileup, and needed about 600GB of RAM to handle it.

Take a look at [breast_cancer/variant_call.sh](https://github.com/ratschlab/silver/blob/main/experiments/breast_cancer/variant_call.sh) for inspiration on how to use `SILVER`.
