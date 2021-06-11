#include "variant_calling.hpp"

/**
 * Find the most likely genotype, if we observe bases counts
 * @parm nBases (array of length 4: number of As, Cs, etc.)
 * @param heteroPrior the prior on heterozygous genotype
 * @param theta sequencing error rate
 */
uint8_t
mostLikelyGenotype(const std::array<uint16_t, 4> &nBases, double heteroPrior, double theta) {
    double logTheta = std::log(theta / 3);
    double logOneMinusTheta = std::log(1 - theta);
    double logHalfMinusTheta = std::log(0.5 - theta / 3);

    std::vector<uint32_t> idx = argsort(nBases.begin(), nBases.end());
    // coverage for the given position; // we require coverage at least 9
    uint32_t cov = sum(nBases.begin(), nBases.end());
    if (cov < 9) {
        return NO_GENOTYPE;
    }

    // probability of the most likely homozygous genotype
    double logProb_homo = nBases[idx[3]] * logOneMinusTheta + (cov - nBases[idx[3]]) * logTheta;
    // probability of the most likely heterozygous genotype
    double logProb_hetero = (nBases[idx[2]] + nBases[idx[3]]) * logHalfMinusTheta
            + (nBases[idx[0]] + nBases[idx[1]]) * logTheta + std::log(heteroPrior);

    // if logProb_homo == logProb_hetero, we are not able to decide
    if (logProb_homo == logProb_hetero) {
        return NO_GENOTYPE;
    }

    // homozygous genotype
    if (logProb_homo > logProb_hetero) {
        // check that the genotype is unique
        if (nBases[idx[2]] == nBases[idx[3]]) {
            return NO_GENOTYPE;
        }
        return (idx[3] << 3) | idx[3];
    }

    // heterozygous genotype
    if (nBases[idx[2]] == nBases[idx[1]]) // check uniqueness
        return NO_GENOTYPE;
    return (idx[3] << 3) | idx[2];
}

std::unordered_map<std::string, std::vector<ChrMap>> read_map(const std::string &map_file) {
    if (map_file.empty()) {
        return {};
    }
    if (!std::filesystem::exists(map_file)) {
        logger()->error("Map file: {} does not exist.", map_file);
        exit(1);
    }
    std::unordered_map<std::string, std::vector<ChrMap>> result;
    std::ifstream f(map_file);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::vector<std::string> cols = split(line, '\t');
        if (cols.size() != 8) {
            logger()->error("Invalid map file: {}. Has {} columns, expected 8.", map_file,
                            cols.size());
            std::exit(1);
        }
        if (cols[1][0] < '1' && cols[1][0] > '9' && cols[1][0] != 'X' && cols[1][0] != 'Y') {
            continue; // not a chromosome mapping
        }
        if (cols[6] == "SEQ") {
            continue;
        }
        uint32_t start_pos = std::stoul(cols[2]) - 1;
        uint32_t len = std::stoul(cols[0]);
        result[cols[1]].push_back(
                { chromosome_to_id(cols[3]), start_pos, len, cols[6] == "INS" ? 'I' : 'D' });
    }
    return result;
}

void apply_map(const std::vector<ChrMap> &map,
               const std::vector<uint8_t> &chr_data,
               std::vector<uint8_t> *new_chr_data) {
    uint32_t i = 0;
    new_chr_data->resize(0);
    for (const ChrMap &map_entry : map) {
        while (i < map_entry.start_pos) {
            new_chr_data->push_back(chr_data[i]);
            i++;
        }
        if (map_entry.tr == 'D') {
            for (uint32_t pos = 0; pos < map_entry.len; ++pos) {
                new_chr_data->push_back(CharToInt[(uint32_t)'N']);
            }
        } else {
            for (uint32_t pos = 0; pos < map_entry.len; ++pos) {
                i++;
            }
        }
    }
    while (i < chr_data.size()) {
        new_chr_data->push_back(chr_data[i]);
        i++;
    }
}

void read_contig(std::ifstream &fasta_file, std::vector<uint8_t> *chr_data) {
    std::string line;
    while (std::getline(fasta_file, line)) {
        for (char c : line) {
            chr_data->push_back(CharToInt[(int)c]);
        }
        if (fasta_file.peek() == '>') {
            break;
        }
    }
}

void get_next_chromosome(std::ifstream &fasta_file,
                         const std::unordered_map<std::string, std::vector<ChrMap>> &map,
                         std::vector<uint8_t> *chr_data,
                         std::vector<uint8_t> *tmp1,
                         std::vector<uint8_t> *tmp2) {
    std::string line;
    if (!std::getline(fasta_file, line)) {
        return;
    }
    assert(line[0] == '>');
    char chromosome = line[1];
    std::string chr_name = line.substr(1);

    logger()->trace("Reading chromosome: {} (first allele)", chr_name);

    tmp1->resize(0);
    read_contig(fasta_file, tmp1);

    if (map.find(chr_name) != map.end()) {
        apply_map(map.at(chr_name), *tmp1, chr_data);
    } else {
        std::swap(*tmp1, *chr_data);
    }


    char ch1 = fasta_file.get();
    assert(ch1 == '>' || ch1 == EOF);

    char ch2 = fasta_file.peek();

    fasta_file.putback(ch1);

    if (chromosome == 'X' || chromosome == 'Y') {
        if (ch2 == 'Y') {
            logger()->info("Male genome detected; Simulating homozygous X");
        } else {
            logger()->info("Simulating homozygous Y");
        }
        for (uint32_t i = 0; i < chr_data->size(); ++i) {
            chr_data->at(i) |= (chr_data->at(i) << 3);
        }
        return;
    }

    std::getline(fasta_file, line); // skip the >chr_name line

    chr_name = line.substr(1);

    logger()->trace("Reading chromosome: {} (second allele)", line);
    tmp1->resize(0);
    read_contig(fasta_file, tmp1);

    if (map.find(chr_name) != map.end()) {
        apply_map(map.at(chr_name), *tmp1, tmp2);
    } else {
        std::swap(*tmp1, *tmp2);
    }

    if (chr_data->size() != tmp2->size()) {
        logger()->error(
                "Invalid reference genome. Maternal and paternal chromosome sizes don't match ({} "
                "vs {})",
                chr_data->size(), tmp2->size());
        std::exit(1);
    }

    for (uint32_t i = 0; i < chr_data->size(); ++i) {
        chr_data->at(i) = (chr_data->at(i) << 3) | tmp2->at(i);
    }
}

bool is_homozygous(uint8_t base) {
    return (base & 7) == (base >> 3);
}

void write_vcf_preamble(std::ofstream &f, const std::string &reference_file, uint32_t cluster) {
    f << "##fileformat=VCFv4.3" << std::endl;
    std::time_t end_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    f << "##fileDate=" << std::ctime(&end_time);
    f << "##source=SVC (Somatic Variant Caller)" << std::endl;
    f << "##reference=" << reference_file << std::endl;
    f << "##cluster=" << cluster << std::endl;
    f << "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tcluster-" + std::to_string(cluster)
      << std::endl;
}

/** Retturns true if a and b have the same genotype (irrespective of allele ordering) */
bool is_same_genotype(uint8_t a, uint8_t b) {
    return a == b || ((a >> 3) | ((a & 7) << 3)) == b;
}

/**
 * Get the bases that are not the same in the given genotypes. E.g. if the first is A/C and the
 * second is A/T,the result will be {C,T}.
 */
std::vector<std::pair<char, char>> get_different_bases(uint8_t reference_genotype,
                                                       uint8_t genotype) {
    assert((reference_genotype & 7) < 6 && (reference_genotype >> 3) < 6 && (genotype & 7) < 6
           && (genotype >> 3) < 6);
    char r1 = IntToChar[reference_genotype & 7];
    char r2 = IntToChar[reference_genotype >> 3];
    if (r1 > r2) {
        std::swap(r1, r2);
    }

    char g1 = IntToChar[genotype & 7];
    char g2 = IntToChar[genotype >> 3];


    if (g1 > g2) {
        std::swap(g1, g2);
    }

    if (g1 == r1 && g2 == r2) {
        return {};
    }

    if (g1 == r1) {
        return { { r2, g2 } };
    } else if (g2 == r2) {
        return { { r1, g1 } };
    } else {
        return { { r1, g1 }, { r2, g2 } };
    }
}

void variant_calling(const std::vector<std::vector<PosData>> &pos_data,
                     const std::vector<uint16_t> &clusters,
                     const std::string &reference_genome_file,
                     const std::string &map_file,
                     double hetero_prior,
                     double theta,
                     const std::filesystem::path &out_dir) {
    if (clusters.empty()) {
        return;
    }

    std::filesystem::create_directories(out_dir);
    if (!std::filesystem::exists(reference_genome_file)) {
        logger()->error("Reference genome {} does not exist", reference_genome_file);
        exit(1);
    }

    std::unordered_map<std::string, std::vector<ChrMap>> map = read_map(map_file);

    // clusters are numbered starting with 1; 0 means "no cluster"
    uint32_t num_clusters = *std::max_element(clusters.begin(), clusters.end()) + 1;
    std::ifstream fasta_file(reference_genome_file);
    std::vector<uint8_t> reference_chromosome, tmp1, tmp2;
    if (std::filesystem::file_size(reference_genome_file) > 1e6) { // not a test setting
        reference_chromosome.reserve(250'000'000);
        tmp1.reserve(250'000'000);
        tmp2.reserve(250'000'000);
    }

    std::vector<std::ofstream> vcfs(num_clusters);
    for (uint32_t i = 0; i < vcfs.size(); ++i) {
        vcfs[i].open(std::filesystem::path(out_dir) / ("cluster_" + std::to_string(i) + ".vcf"));
        write_vcf_preamble(vcfs[i], reference_genome_file, i);
    }

    // the qual,filter,info,format fields are identical for all rows.
    std::string info_format = "\t.\t.\tVARIANT_OVERALL_TYPE=SNP\tGT\t";

    std::ofstream f(out_dir / "variant");
    for (uint32_t chr_idx = 0; chr_idx < pos_data.size(); ++chr_idx) {
        const std::vector<PosData> &chromosome_data = pos_data[chr_idx];
        get_next_chromosome(fasta_file, map, &reference_chromosome, &tmp1, &tmp2);
        for (const PosData &pd : chromosome_data) {
            std::vector<std::array<uint16_t, 4>> nbases(num_clusters);
            for (uint32_t cl_idx = 0; cl_idx < num_clusters; ++cl_idx) {
                for (uint32_t i = 0; i < pd.size(); ++i) {
                    if (std::abs(static_cast<int64_t>(cl_idx - clusters[pd.cell_id(i)]))
                        <= 0.05) {
                        nbases[cl_idx][pd.base(i)]++;
                    }
                }
            }

            // -1 because PosData is 1-based to emulate samtools et all
            uint8_t reference_genotype = reference_chromosome[pd.position - 1];
            for (uint32_t cl_idx = 0; cl_idx < num_clusters; ++cl_idx) {
                uint8_t genotype = mostLikelyGenotype(nbases[cl_idx], hetero_prior, theta);
                // write position to file if different
                if (!is_same_genotype(genotype, reference_genotype) && genotype != NO_GENOTYPE) {
                    if (is_homozygous(reference_genotype)) {
                        std::string alt(1, IntToChar[genotype & 7]);
                        std::string gt = "1/1"; // VCF genotype indicator
                        if (!is_homozygous(genotype)) {
                            alt += IntToChar[genotype >> 3];
                            gt = "0/1";
                        }
                        assert((reference_genotype & 7) < 6);
                        vcfs[cl_idx] << id_to_chromosome(chr_idx) << '\t' << pd.position << "\t.\t"
                                     << IntToChar[reference_genotype & 7] << '\t' << alt
                                     << info_format << gt << std::endl;
                    } else {
                        std::vector<std::pair<char, char>> bases
                                = get_different_bases(reference_genotype, genotype);
                        for (const auto &p : bases) {
                            vcfs[cl_idx] << id_to_chromosome(chr_idx) << '\t' << pd.position
                                         << "\t.\t" << p.first << '\t' << p.second << info_format
                                         << "1/1" << std::endl;
                        }
                    }
                }
            }
        }
    }
}
