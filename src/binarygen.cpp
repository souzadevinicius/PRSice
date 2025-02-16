﻿// This file is part of PRSice-2, copyright (C) 2016-2019
// Shing Wan Choi, Paul F. O’Reilly
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "binarygen.hpp"

BinaryGen::BinaryGen(const GenoFile& geno, const Phenotype& pheno,
                     const std::string& delim, Reporter* reporter)
{
    m_ignore_fid = pheno.ignore_fid;
    m_keep_file = geno.keep;
    m_remove_file = geno.remove;
    m_delim = delim;
    m_hard_coded = geno.hard_coded;
    m_reporter = reporter;
    init_chr();
    std::string message = "Initializing Genotype";
    std::string file_name = geno.file_name;
    const bool is_list = !(geno.file_list.empty());
    std::vector<std::string> token;
    if (!is_list) { token = misc::split(file_name, ","); }
    else
    {
        file_name = geno.file_list;
        token = misc::split(file_name, ",");
    }
    const bool external_sample = (token.size() == 2);
    if (external_sample) { m_sample_file = token[1]; }
    file_name = token.front();
    if (is_list)
    {
        message.append(" info from file " + file_name + " (bgen)\n");

        m_genotype_file_names = load_genotype_prefix(file_name);
    }
    else
    {
        message.append(" file: " + file_name + " (bgen)\n");
        m_genotype_file_names = set_genotype_files(file_name);
    }
    if (external_sample)
    { message.append("With external sample file: " + m_sample_file + "\n"); }
    else if (pheno.pheno_file.empty())
    {
        throw std::runtime_error("Error: You must provide a phenotype "
                                 "file for bgen format!\n");
    }
    else
    {
        m_sample_file = pheno.pheno_file;
    }
    m_has_external_sample = true;
    m_reporter->report(message);
}


std::vector<Sample_ID> BinaryGen::gen_sample_vector()
{
    // first, check if the sample file is in the sample format specified by BGEN
    // for reference file, get the number of sample size from the context file

    std::vector<Sample_ID> sample_name;
    std::vector<bool> temp_inclusion_vec;
    if (m_is_ref)
    {
        if ((!m_keep_file.empty() || !m_remove_file.empty())
            && (!m_has_external_sample))
        {
            throw std::runtime_error("Error: Cannot perform sample "
                                     "filtering on the LD reference "
                                     "file wihtout the sample file!");
        }
        else
        {
            get_context(0);
            m_unfiltered_sample_ct = m_context_map[0].number_of_samples;
            temp_inclusion_vec.resize(m_unfiltered_sample_ct, true);
        }
    }
    if (!m_is_ref || (!m_keep_file.empty() || !m_remove_file.empty()))
    {
        // this is the target, where the m_sample_file must be correct, or this
        // is the reference, which we asked for --keep or --remove and an
        // external sample file was provided (that's why we don't get into the
        // runtime_error)
        const bool is_sample_format = check_is_sample_format(m_sample_file);
        std::ifstream sample_file(m_sample_file.c_str());
        if (!sample_file.is_open())
        {
            throw std::runtime_error("Error: Cannot open sample file: "
                                     + m_sample_file);
        }
        std::string line;
        size_t sex_col = ~size_t(0);
        // now check if there's a sex information
        if (is_sample_format)
        {
            // only do this if the file is sample format
            std::getline(sample_file, line);
            std::vector<std::string> header_names = misc::split(line);
            m_reporter->report("Detected bgen sample file format\n");
            for (size_t i = 3; i < header_names.size(); ++i)
            {
                // try to identify column containing SEX
                std::transform(header_names[i].begin(), header_names[i].end(),
                               header_names[i].begin(), ::toupper);
                if (header_names[i].compare("SEX") == 0)
                {
                    sex_col = i;
                    break;
                }
            }
            // now we read in the second line
            std::getline(sample_file, line);
            if (sex_col != ~size_t(0))
            {
                // double check if the format is alright
                std::vector<std::string> header_format = misc::split(line);
                // no need range check as we know for sure sex_col must be in
                // range
                if (header_format[sex_col] != "D")
                {
                    m_reporter->report("Warning: Sex must be coded as "
                                       "\"D\" in bgen sample file!\n"
                                       "We will ignore the sex information.");
                    sex_col = ~size_t(0);
                }
            }
        }
        // now start reading the file
        size_t line_id = 0;
        std::unordered_set<std::string> sample_in_file;
        std::vector<std::string> duplicated_sample_id;
        std::vector<std::string> token;
        bool inclusion = false;
        std::string FID, IID;
        while (std::getline(sample_file, line))
        {
            misc::trim(line);
            ++line_id;
            if (line.empty()) continue;
            token = misc::split(line);
            // if it is not the sample file, check if this has a header
            // not the best way, but will do it
            if (line_id == 1)
            {
                std::string header_test = token[0];
                std::transform(header_test.begin(), header_test.end(),
                               header_test.begin(), ::toupper);
                if (header_test == "FID"
                    || (header_test == "IID" && m_ignore_fid))
                {
                    // this is the header, skip
                    continue;
                }
                else
                {
                    // emit a warning so people might be aware of it
                    m_reporter->report(
                        "We assume the following line is not a header:\n" + line
                        + "\n(first column isn't FID or IID)\n");
                }
            }
            if (token.size()
                < ((sex_col != ~size_t(0)) ? (sex_col) : (1 + !m_ignore_fid)))
            {
                throw std::runtime_error(
                    "Error: Line " + std::to_string(line_id)
                    + " must have at least "
                    + std::to_string((sex_col != ~size_t(0))
                                         ? (sex_col)
                                         : (1 + !m_ignore_fid))
                    + " columns! Number of column="
                    + misc::to_string(token.size()));
            }
            ++m_unfiltered_sample_ct;

            if (is_sample_format || !m_ignore_fid)
            {
                FID = token[0];
                IID = token[1];
            }
            else
            {
                // not a sample format or used ignore fid
                FID = "";
                IID = token[0];
            }
            const std::string id = (m_ignore_fid) ? IID : FID + m_delim + IID;
            // we assume all bgen samples are founders
            if (!m_remove_sample)
            {
                inclusion = (m_sample_selection_list.find(id)
                             != m_sample_selection_list.end());
            }
            else
            {
                inclusion = (m_sample_selection_list.find(id)
                             == m_sample_selection_list.end());
            }
            if (sex_col != ~size_t(0))
            {
                // anything that's not 1 or 2 are considered as ambiguous. This
                // mean that M and F will also be "ambiguous"
                m_num_male += (token[sex_col] == "1");
                m_num_female += (token[sex_col] == "2");
                m_num_ambig_sex +=
                    (token[sex_col] != "1" && token[sex_col] != "2");
            }
            else
            {
                ++m_num_ambig_sex;
            }
            if (sample_in_file.find(id) != sample_in_file.end())
            { duplicated_sample_id.push_back(id); }
            sample_in_file.insert(id);
            temp_inclusion_vec.push_back(inclusion);
            if (!m_is_ref && inclusion)
            {
                // all sample must be a founder
                sample_name.emplace_back(Sample_ID(FID, IID, "", true));
            }
        }
        if (!duplicated_sample_id.empty())
        {
            // TODO: Produce a file containing id of all valid samples
            throw std::runtime_error(
                "Error: A total of "
                + misc::to_string(duplicated_sample_id.size())
                + " duplicated samples detected! Please ensure all samples "
                  "have an "
                  "unique identifier");
        }
        sample_file.close();
    }

    uintptr_t unfiltered_sample_ctl = BITCT_TO_WORDCT(m_unfiltered_sample_ct);
    m_sample_include.resize(unfiltered_sample_ctl, 0);
    m_founder_info.resize(unfiltered_sample_ctl, 0);
    m_founder_ct = 0;
    for (size_t i = 0; i < temp_inclusion_vec.size(); ++i)
    {
        // using temp_inclusion_vec allow us to set the sample_include vector
        // and founder_info vector without generating the sample vector
        if (temp_inclusion_vec[i])
        {
            ++m_sample_ct;
            SET_BIT(i, m_sample_include.data());
            // we assume all bgen samples to be founder
            SET_BIT(i, m_founder_info.data());
        }
    }
    m_founder_ct = m_sample_ct;
    // initialize the PRS vector (can't reserve, otherwise seg fault(no idea
    // why))
    for (size_t i = 0; i < m_sample_ct; ++i) { m_prs_info.emplace_back(PRS()); }
    // initialize regression flag
    m_in_regression.resize(m_sample_include.size(), 0);
    return sample_name;
}

bool BinaryGen::check_is_sample_format(const std::string& input)
{
    // read the sample file
    // might want to change it according to the new sample file,
    // which only mandate the first column
    std::ifstream sample_file(input.c_str());
    if (!sample_file.is_open())
    {
        std::string error_message = "Error: Cannot open sample file: " + input;
        throw std::runtime_error(error_message);
    }
    // get the first two line of input
    std::string first_line, second_line;
    std::getline(sample_file, first_line);
    std::getline(sample_file, second_line);
    sample_file.close();
    // split the first two lines
    const std::vector<std::string> first_row = misc::split(first_line);
    const std::vector<std::string> second_row = misc::split(second_line);
    // each row should have the same number of column
    if (first_row.size() != second_row.size() || first_row.size() < 3)
    { return false; }
    // first 3 must be 0
    for (size_t i = 0; i < 3; ++i)
    {
        if (second_row[i] != "0") return false;
    }
    // Allowed character codes are: DCPB
    for (size_t i = 4; i < second_row.size(); ++i)
    {
        if (second_row[i].length() > 1) return false;
        // then any remaining column must contain either D, C, P or B
        switch (second_row[i].at(0))
        {
        case 'D':
        case 'C':
        case 'P':
        case 'B': break;
        default: return false;
        }
    }
    return true;
}

void BinaryGen::get_context(const size_t& idx)
{
    // get the context information for the input bgen file
    // most of these codes are copy from the bgen library
    const std::string prefix = m_genotype_file_names[idx];
    const std::string bgen_name = prefix + ".bgen";
    std::ifstream bgen_file(bgen_name.c_str(), std::ifstream::binary);
    if (!bgen_file.is_open())
    { throw std::runtime_error("Error: Cannot open bgen file " + bgen_name); }
    // initialize the bgen context object
    genfile::bgen::Context context;
    uint32_t offset, header_size = 0, number_of_snp_blocks = 0,
                     number_of_samples = 0, flags = 0;
    char magic[4];
    const std::size_t fixed_data_size = 20;
    std::vector<char> free_data;
    // read in the offset information
    genfile::bgen::read_little_endian_integer(bgen_file, &offset);
    // read in the size of the header
    genfile::bgen::read_little_endian_integer(bgen_file, &header_size);
    // the size of header should be bigger than or equal to the fixed data size
    assert(header_size >= fixed_data_size);
    genfile::bgen::read_little_endian_integer(bgen_file, &number_of_snp_blocks);
    genfile::bgen::read_little_endian_integer(bgen_file, &number_of_samples);
    // now read in the magic number
    bgen_file.read(&magic[0], 4);
    free_data.resize(header_size - fixed_data_size);
    if (free_data.size() > 0)
    {
        bgen_file.read(&free_data[0],
                       static_cast<std::streamsize>(free_data.size()));
    }
    // now read in the flag
    genfile::bgen::read_little_endian_integer(bgen_file, &flags);
    if ((magic[0] != 'b' || magic[1] != 'g' || magic[2] != 'e'
         || magic[3] != 'n')
        && (magic[0] != 0 || magic[1] != 0 || magic[2] != 0 || magic[3] != 0))
    {
        throw std::runtime_error("Error: Incorrect magic string!\nPlease "
                                 "check you have provided a valid bgen "
                                 "file!");
    }
    if (bgen_file)
    {
        context.number_of_samples = number_of_samples;
        context.number_of_variants = number_of_snp_blocks;
        context.magic.assign(&magic[0], &magic[0] + 4);
        context.offset = offset;
        context.flags = flags;
        m_context_map[idx].offset = context.offset;
        m_context_map[idx].flags = context.flags;
        m_context_map[idx].number_of_samples = context.number_of_samples;
        m_context_map[idx].number_of_variants = context.number_of_variants;
        if ((flags & genfile::bgen::e_CompressedSNPBlocks)
            == genfile::bgen::e_ZstdCompression)
        {
            throw std::runtime_error(
                "Error: zstd compression currently not supported");
        }
    }
    else
    {
        throw std::runtime_error("Error: Problem reading bgen file!");
    }
}

bool BinaryGen::check_sample_consistent(const std::string& bgen_name,
                                        const genfile::bgen::Context& context)
{
    // only do this if our gen file contains the sample information
    if (context.flags & genfile::bgen::e_SampleIdentifiers)
    {
        std::ifstream bgen_file(bgen_name.c_str(), std::ifstream::binary);
        uint32_t tmp_offset;
        genfile::bgen::Context tmp_context;
        genfile::bgen::read_offset(bgen_file, &tmp_offset);
        genfile::bgen::read_header_block(bgen_file, &tmp_context);
        uint32_t sample_block_size = 0;
        uint32_t actual_number_of_samples = 0;
        uint16_t identifier_size;
        std::string identifier;
        std::size_t bytes_read = 0;
        std::unordered_set<std::string> dup_check;
        genfile::bgen::read_little_endian_integer(bgen_file,
                                                  &sample_block_size);
        genfile::bgen::read_little_endian_integer(bgen_file,
                                                  &actual_number_of_samples);
        // +8 here to account for the sample_block_size and
        // actual_number_of_samples read above. Direct Copy and paste from
        // BGEN lib function read_sample_identifier_block
        bytes_read += 8;
        if (actual_number_of_samples != context.number_of_samples)
        {
            throw std::runtime_error("Error: Number of sample from your "
                                     ".sample/ phenotype file does not match "
                                     "the number of sample included in the "
                                     "bgen file. Maybe check if you have used "
                                     "a filtered sample or phenotype file?");
        }
        // we don't need to check the sample name when we are doing the LD
        // as we don't store any sample ID
        // though might be good practice to actually check the names matched
        // between the sample name and the ld bgen file to avoid mismatch
        // That can be done later on
        if (!m_is_ref)
        {
            const bool has_fid = !m_sample_id.front().FID.empty();
            size_t sample_vector_idx = 0;
            for (size_t i = 0; i < actual_number_of_samples; ++i)
            {
                genfile::bgen::read_length_followed_by_data(
                    bgen_file, &identifier_size, &identifier);
                if (!bgen_file)
                    throw std::runtime_error(
                        "Error: Problem reading bgen file!");
                bytes_read += sizeof(identifier_size) + identifier_size;
                // Need to double check. BGEN format might differ depends
                // if FID is provided. When FID is provided, then the ID
                // should be FID + delimitor + IID; otherwise it'd be IID
                if (IS_SET(m_sample_include.data(), i))
                {
                    if (m_sample_id[sample_vector_idx].IID != identifier
                        && (m_sample_id[sample_vector_idx].FID + m_delim
                            + m_sample_id[sample_vector_idx].IID)
                               != identifier)
                    {
                        std::string error_message =
                            "Error: Sample mismatch "
                            "between bgen and phenotype file! Name in BGEN "
                            "file is "
                            ":"
                            + identifier + " and in phentoype file is: ";
                        if (has_fid)
                            error_message.append(
                                m_sample_id[sample_vector_idx].FID + m_delim
                                + m_sample_id[sample_vector_idx].IID);
                        else
                            error_message.append(
                                m_sample_id[sample_vector_idx].IID);
                        error_message.append(
                            ". Please note that PRSice require the bgen file "
                            "and "
                            "the .sample (or phenotype file if sample file is "
                            "not provided) to have sample in the same order. "
                            "(We "
                            "might be able to losen this requirement in future "
                            "when we have more time)");
                        throw std::runtime_error(error_message);
                    }
                    ++sample_vector_idx;
                }
            }
        }
        assert(bytes_read == sample_block_size);
    }
    return true;
}


void BinaryGen::gen_snp_vector(
    const std::vector<IITree<size_t, size_t>>& exclusion_regions,
    const std::string& out_prefix, Genotype* target)
{
    const std::string mismatch_snp_record_name = out_prefix + ".mismatch";
    const std::string mismatch_source = m_is_ref ? "Reference" : "Base";
    std::unordered_set<std::string> duplicated_snps;
    std::unordered_set<std::string> processed_snps;
    auto&& genotype = (m_is_ref) ? target : this;
    std::vector<bool> retain_snp(genotype->m_existed_snps.size(), false);
    std::ifstream bgen_file;
    std::string bgen_name;
    std::string allele;
    std::string SNPID;
    std::string RSID;
    std::string cur_id;
    std::string chromosome;
    std::string prev_chr = "";
    std::string file_name;
    std::string error_message = "";
    std::string A1, A2, prefix;
    long long byte_pos, start, data_size;
    size_t total_unfiltered_snps = 0;
    size_t ref_target_match = 0;
    size_t num_snp;
    size_t chr_num = 0;
    uint32_t SNP_position = 0;
    uint32_t offset;
    int chr_code = 0;
    bool exclude_snp = false;
    bool chr_sex_error = false;
    bool chr_error = false;
    bool prev_chr_sex_error = false;
    bool prev_chr_error = false;
    bool flipping = false;
    bool to_remove = false;
    for (size_t i = 0; i < m_genotype_file_names.size(); ++i)
    {
        // go through each genotype file and get the context information
        get_context(i);
        // get the total unfiltered snp size so that we can initalize the vector
        // TODO: Check if there are too many SNPs, therefore cause integer
        // overflow
        total_unfiltered_snps += m_context_map[i].number_of_variants;
    }
    if (!m_is_ref)
    {
        check_sample_consistent(
            std::string(m_genotype_file_names.front() + ".bgen"),
            m_context_map[0]);
    }
    for (size_t file_idx = 0; file_idx < m_genotype_file_names.size();
         ++file_idx)
    {
        // now start reading each bgen file
        prefix = m_genotype_file_names[file_idx];
        bgen_name = prefix + ".bgen";
        if (bgen_file.is_open()) bgen_file.close();
        bgen_file.clear();
        bgen_file.open(bgen_name.c_str(), std::ifstream::binary);
        if (!bgen_file.is_open())
        {
            std::string error_message =
                "Error: Cannot open bgen file " + bgen_name;
            throw std::runtime_error(error_message);
        }
        // read in the offset
        auto&& context = m_context_map[file_idx];
        offset = context.offset;
        // skip the offest
        bgen_file.seekg(offset + 4);
        num_snp = context.number_of_variants;
        // obtain the context information. We don't check out of bound as that
        // is unlikely to happen
        for (size_t i_snp = 0; i_snp < num_snp; ++i_snp)
        {
            // go through each SNP in the file
            if (i_snp % 1000 == 0)
            {
                fprintf(stderr, "\r%zuK SNPs processed in %s   \r",
                        i_snp / 1000, bgen_name.c_str());
            }
            else if (i_snp < 1000)
            {
                fprintf(stderr, "\r%zu SNPs processed in %s\r", i_snp,
                        bgen_name.c_str());
            }
            m_unfiltered_marker_ct++;
            start = bgen_file.tellg();
            // directly use the library without decompressing the genotype
            read_snp_identifying_data(bgen_file, context, &SNPID, &RSID,
                                      &chromosome, &SNP_position, &A1, &A2);
            exclude_snp = false;
            if (chromosome != prev_chr)
            {
                chr_code = get_chrom_code_raw(chromosome.c_str());
                if (chr_code_check(chr_code, chr_sex_error, chr_error,
                                   error_message))
                {
                    if (chr_error && !prev_chr_error)
                    {
                        std::cerr << error_message << "\n";
                        prev_chr_error = chr_error;
                    }
                    if (chr_sex_error && !prev_chr_sex_error)
                    {
                        std::cerr << error_message << "\n";
                        prev_chr_sex_error = chr_sex_error;
                    }
                    exclude_snp = true;
                }
                chr_num = ~size_t(0);
                if (!exclude_snp)
                {
                    // only update prev_chr if we want to include this SNP
                    prev_chr = chromosome;
                    chr_num = static_cast<size_t>(chr_code);
                }
            }

            if (RSID == "." && SNPID == ".")
            {
                // when both rs id and SNP id isn't available, just skip
                exclude_snp = true;
            }
            // default to RS
            cur_id = RSID;
            // by this time point, we should always have the
            // m_existed_snps_index propagated with SNPs from the base. So we
            // can first check if the SNP are presented in base
            auto&& find_rs = genotype->m_existed_snps_index.find(RSID);
            auto&& find_snp = genotype->m_existed_snps_index.find(SNPID);
            if (find_rs == genotype->m_existed_snps_index.end()
                && find_snp == genotype->m_existed_snps_index.end())
            {
                // this is the reference panel, and the SNP wasn't found in the
                // target doens't matter if we use RSID or SNPID
                ++m_base_missed;
                exclude_snp = true;
            }
            else if (find_snp != genotype->m_existed_snps_index.end())
            {
                // we found the SNPID
                cur_id = SNPID;
            }
            else if (find_rs != genotype->m_existed_snps_index.end())
            {
                // we found the RSID
                cur_id = RSID;
            }

            if (processed_snps.find(cur_id) != processed_snps.end())
            {
                duplicated_snps.insert(cur_id);
                exclude_snp = true;
            }
            // perform check on ambiguousity
            else if (ambiguous(A1, A2))
            {
                ++m_num_ambig;
                if (!m_keep_ambig) exclude_snp = true;
            }

            to_remove = Genotype::within_region(exclusion_regions, chr_num,
                                                SNP_position);
            if (to_remove)
            {
                ++m_num_xrange;
                exclude_snp = true;
            }
            // get the current location of bgen file, this will be used to skip
            // to current location later on
            byte_pos = bgen_file.tellg();
            // read in the genotype data block so that we advance the ifstream
            // pointer to the next SNP entry
            read_genotype_data_block(bgen_file, context, &m_buffer1);
            data_size = static_cast<long long>(bgen_file.tellg()) - start;
            if (data_size > static_cast<long long>(m_data_size))
            { m_data_size = static_cast<unsigned long long>(data_size); }
            // if we want to exclude this SNP, we will not perform
            // decompression
            if (!exclude_snp)
            {
                // A1 = alleles.front();
                // A2 = alleles.back();
                auto&& target_index = genotype->m_existed_snps_index[cur_id];
                if (!genotype->m_existed_snps[target_index].matching(
                        chr_num, SNP_position, A1, A2, flipping))
                {

                    genotype->print_mismatch(
                        mismatch_snp_record_name, mismatch_source,
                        genotype->m_existed_snps[target_index], cur_id, A1, A2,
                        chr_num, SNP_position);
                    ++m_num_ref_target_mismatch;
                }
                else
                {
                    processed_snps.insert(cur_id);
                    genotype->m_existed_snps[target_index].add_snp_info(
                        file_idx, byte_pos, chr_num, SNP_position, A1, A2,
                        flipping, m_is_ref);
                    retain_snp[target_index] = true;
                    ++ref_target_match;
                }
            }
        }
        if (num_snp % 1000 == 0)
        {
            fprintf(stderr, "\r%zuK SNPs processed in %s   \r", num_snp / 1000,
                    bgen_name.c_str());
        }
        else if (num_snp < 1000)
        {
            fprintf(stderr, "\r%zu SNPs processed in %s\r", num_snp,
                    bgen_name.c_str());
        }
        bgen_file.close();
        fprintf(stderr, "\n");
    }
    if (ref_target_match != genotype->m_existed_snps.size())
    {
        // there are mismatch, so we need to update the snp vector
        genotype->shrink_snp_vector(retain_snp);
        // now update the SNP vector index
        genotype->update_snp_index();
    }
    if (duplicated_snps.size() != 0)
    {
        throw std::runtime_error(
            genotype->print_duplicated_snps(duplicated_snps, out_prefix));
    }
}


bool BinaryGen::calc_freq_gen_inter(const QCFiltering& filter_info,
                                    const std::string& prefix, Genotype* target,
                                    bool force_cal)
{
    if (!m_intermediate
        && (misc::logically_equal(filter_info.geno, 1.0)
            || filter_info.geno > 1.0)
        && (misc::logically_equal(filter_info.maf, 0.0)
            || filter_info.maf < 0.0)
        && (misc::logically_equal(filter_info.info_score, 0.0)
            || filter_info.maf < 0.0)
        && !force_cal)
    { return false; }
    const std::string print_target = (m_is_ref) ? "reference" : "target";
    m_reporter->report("Calculate MAF and perform filtering on " + print_target
                       + " SNPs\n"
                         "==================================================");
    auto&& genotype = (m_is_ref) ? target : this;
    std::sort(
        begin(genotype->m_existed_snps), end(genotype->m_existed_snps),
        [this](SNP const& t1, SNP const& t2) {
            if (t1.get_file_idx(m_is_ref) == t2.get_file_idx(m_is_ref))
            { return t1.get_byte_pos(m_is_ref) < t2.get_byte_pos(m_is_ref); }
            else
                return t1.get_file_idx(m_is_ref) == t2.get_file_idx(m_is_ref);
        });
    const double sample_ct_recip = 1.0 / (static_cast<double>(m_sample_ct));
    const uintptr_t unfiltered_sample_ctl =
        BITCT_TO_WORDCT(m_unfiltered_sample_ct);
    const uintptr_t unfiltered_sample_ctv2 = 2 * unfiltered_sample_ctl;
    std::vector<bool> retain_snps(genotype->m_existed_snps.size(), false);

    std::string bgen_name = "";
    const std::string intermediate_name = prefix + ".inter";
    std::ifstream bgen_file;
    double cur_maf, cur_geno;
    long long byte_pos, tmp_byte_pos;
    size_t processed_count = 0;
    size_t cur_file_idx = 0;
    size_t retained = 0;
    size_t ll_ct = 0;
    size_t lh_ct = 0;
    size_t hh_ct = 0;
    size_t uii = 0;
    size_t missing = 0;
    // initialize the sample inclusion mask
    std::vector<uintptr_t> sample_include2(unfiltered_sample_ctv2);
    std::vector<uintptr_t> founder_include2(unfiltered_sample_ctv2);
    // fill it with the required mask (copy from PLINK2)
    init_quaterarr_from_bitarr(m_sample_include.data(), m_unfiltered_sample_ct,
                               sample_include2.data());
    init_quaterarr_from_bitarr(m_founder_info.data(), m_unfiltered_sample_ct,
                               founder_include2.data());
    m_tmp_genotype.resize(unfiltered_sample_ctv2, 0);
    // we initialize the plink converter with the sample inclusion vector and
    // also the tempory genotype vector list. We also provide the hard coding
    // threshold
    PLINK_generator setter(&m_sample_include, m_tmp_genotype.data(),
                           m_hard_threshold, m_dose_threshold);
    // now consider if we are generating the intermediate file
    std::ofstream inter_out;
    if (m_intermediate)
    {
        // allow generation of intermediate file
        if (m_target_plink && m_is_ref)
        {
            // target already generated some intermediate, now append for
            // reference
            inter_out.open(intermediate_name.c_str(),
                           std::ios::binary | std::ios::app);
        }
        else
        {
            // a new intermediate file
            inter_out.open(intermediate_name.c_str(), std::ios::binary);
        }
    }
    // now start processing the bgen file
    double progress = 0, prev_progress = -1.0;
    const size_t total_snp = genotype->m_existed_snps.size();
    genfile::bgen::Context context;
    for (auto&& snp : genotype->m_existed_snps)
    {
        progress = static_cast<double>(processed_count)
                   / static_cast<double>(total_snp) * 100;
        if (progress - prev_progress > 0.01)
        {
            fprintf(stderr, "\rCalculating allele frequencies: %03.2f%%",
                    progress);
            prev_progress = progress;
        }
        snp.get_file_info(cur_file_idx, byte_pos, m_is_ref);
        context = m_context_map[cur_file_idx];
        ++processed_count;
        // now read in the genotype information
        genfile::bgen::read_and_parse_genotype_data_block<PLINK_generator>(
            m_genotype_file, m_genotype_file_names[cur_file_idx] + ".bgen",
            context, setter, &m_buffer1, &m_buffer2, byte_pos);
        // no founder, much easier
        setter.get_count(ll_ct, lh_ct, hh_ct, missing);
        uii = ll_ct + lh_ct + hh_ct;
        cur_geno = 1.0 - ((static_cast<int32_t>(uii)) * sample_ct_recip);
        uii = 2 * (ll_ct + lh_ct + hh_ct);
        // tmp_total = (ll_ctf + lh_ctf + hh_ctf);
        // assert(m_founder_ct >= tmp_total);
        // missing = m_founder_ct - tmp_total;
        if (!uii) { cur_maf = 0.5; }
        else
        {
            cur_maf = (static_cast<double>(2 * hh_ct + lh_ct))
                      / (static_cast<double>(uii));
            cur_maf = (cur_maf > 0.5) ? 1 - cur_maf : cur_maf;
        }
        // filter by genotype missingness
        if (filter_info.geno < cur_geno)
        {
            ++m_num_geno_filter;
            continue;
        }
        // filter by MAF
        // do not flip the MAF for now, so that we
        // are not confuse later on
        // remove SNP if maf lower than threshold
        if (cur_maf < filter_info.maf)
        {
            ++m_num_maf_filter;
            continue;
        }
        else if (ll_ct == m_sample_ct || hh_ct == m_sample_ct)
        {
            // none of the sample contain this SNP
            // still count as MAF filtering (for now)
            ++m_num_maf_filter;
            continue;
        }

        if (setter.info_score() < filter_info.info_score)
        {
            ++m_num_info_filter;
            continue;
        }
        // if we can reach here, it is not removed
        snp.set_counts(ll_ct, lh_ct, hh_ct, missing, m_is_ref);
        if (m_is_ref) { snp.set_ref_expected(setter.expected()); }
        else
        {
            snp.set_expected(setter.expected());
        }
        ++retained;
        // we need to -1 because we put processed_count ++ forward
        // to avoid continue skipping out the addition
        retain_snps[processed_count - 1] = true;
        if (m_intermediate
            && (m_is_ref || !m_expect_reference || (!m_is_ref && m_hard_coded)))
        {
            // we will only generate the intermediate file if
            // the following happen:
            // 1. User want to generate the intermediate file
            // 2. We are dealing with reference file format
            // 3. We are dealing with target file and there is
            // no reference file
            // 4. We are dealing with target file and we are
            // expected to use hard_coding
            tmp_byte_pos = inter_out.tellp();
            inter_out.write(reinterpret_cast<char*>(&m_tmp_genotype[0]),
                            m_tmp_genotype.size() * sizeof(m_tmp_genotype[0]));
            if (!m_is_ref)
            {
                // target file
                if (m_hard_coded)
                {
                    m_target_plink = true;
                    snp.update_file(m_genotype_file_names.size(), tmp_byte_pos,
                                    false);
                }
                if (!m_expect_reference)
                {
                    // we don't have reference
                    m_ref_plink = true;
                    snp.update_file(m_genotype_file_names.size(), tmp_byte_pos,
                                    true);
                }
            }
            else
            {
                // this is the reference file
                m_ref_plink = true;
                snp.update_file(m_genotype_file_names.size(), tmp_byte_pos,
                                true);
            }
        }
    }
    if (m_intermediate
        && (m_is_ref || (!m_is_ref && m_hard_coded) || !m_expect_reference))
    { // update our genotype file
        inter_out.close();
        m_genotype_file_names.push_back(intermediate_name);
    }
    fprintf(stderr, "\rCalculating allele frequencies: %03.2f%%\n", 100.0);
    // now update the vector
    if (retained != genotype->m_existed_snps.size())
    { genotype->shrink_snp_vector(retain_snps); }
    return true;
}

BinaryGen::~BinaryGen()
{
    if (m_target_plink || m_ref_plink)
    {
        // if we have constructed the intermediate file, we should remove it
        // to save space (plus that file isn't of any useful format and
        // can't be used by any other problem nor can it be reused)
        std::remove(m_genotype_file_names.back().c_str());
    }
}

void BinaryGen::dosage_score(
    const std::vector<size_t>::const_iterator& start_idx,
    const std::vector<size_t>::const_iterator& end_idx, bool reset_zero)
{
    // currently, use_ref_maf doesn't work on bgen dosage file
    // main reason is we need expected value instead of
    // the MAF
    bool not_first = !reset_zero;
    // we initialize the PRS interpretor with the required information.
    // m_prs_info is where we store the PRS information
    // and m_sample_include let us know if the sample is required.
    // m_missing_score will inform us as to how to handle the missingness
    PRS_Interpreter setter(&m_prs_info, &m_sample_include,
                           m_prs_calculation.missing_score);
    std::vector<size_t>::const_iterator cur_idx = start_idx;
    size_t file_idx;
    long long byte_pos;
    for (; cur_idx != end_idx; ++cur_idx)
    {
        auto&& snp = m_existed_snps[(*cur_idx)];
        snp.get_file_info(file_idx, byte_pos, m_is_ref);
        // if the file name differ, or the file isn't open, we will open it
        auto&& context = m_context_map[file_idx];
        setter.set_stat(snp.stat(), m_homcom_weight, m_het_weight,
                        m_homrar_weight, snp.is_flipped(), not_first);

        // start performing the parsing
        genfile::bgen::read_and_parse_genotype_data_block<PRS_Interpreter>(
            m_genotype_file, m_genotype_file_names[file_idx] + ".bgen", context,
            setter, &m_buffer1, &m_buffer2, byte_pos);
        // check if this SNP has some non-missing sample, if not, invalidate
        // it
        // after reading in this SNP, we no longer need to reset the PRS
        not_first = true;
    }
}


void BinaryGen::hard_code_score(
    const std::vector<size_t>::const_iterator& start_idx,
    const std::vector<size_t>::const_iterator& end_idx, bool reset_zero)
{
    // we need to calculate the size of possible vectors
    const uintptr_t unfiltered_sample_ctl =
        BITCT_TO_WORDCT(m_unfiltered_sample_ct);
    const uintptr_t unfiltered_sample_ct4 = (m_unfiltered_sample_ct + 3) / 4;
    // genotype counts
    size_t homrar_ct = 0;
    size_t missing_ct = 0;
    size_t het_ct = 0;
    size_t homcom_ct = 0;
    // weight of each genotype
    double homcom_weight = m_homcom_weight;
    double het_weight = m_het_weight;
    double homrar_weight = m_homrar_weight;
    // this is needed if we want to calculate the MAF of the sample
    const size_t ploidy = 2;
    const size_t miss_count =
        (m_prs_calculation.missing_score != MISSING_SCORE::SET_ZERO) * ploidy;
    const bool is_centre =
        (m_prs_calculation.missing_score == MISSING_SCORE::CENTER);
    const bool mean_impute =
        (m_prs_calculation.missing_score == MISSING_SCORE::MEAN_IMPUTE);
    // check if we need to reset the sample's PRS
    bool not_first = !reset_zero;
    double stat, maf, adj_score, miss_score;
    long long byte_pos;
    // initialize the data structure for storing the genotype
    std::vector<uintptr_t> genotype(unfiltered_sample_ctl * 2, 0);
    genfile::bgen::Context context;
    PLINK_generator setter(&m_sample_include, genotype.data(), m_hard_threshold,
                           m_dose_threshold);
    std::vector<size_t>::const_iterator cur_idx = start_idx;
    size_t idx;
    for (; cur_idx != end_idx; ++cur_idx)
    {
        auto&& cur_snp = m_existed_snps[(*cur_idx)];
        // read in the genotype using the modified load_and_collapse_incl
        // function. m_target_plink will inform the function wheter there's
        // an intermediate file
        // if it has the intermediate file, then we should have already
        // calculated the counts
        cur_snp.get_file_info(idx, byte_pos, m_is_ref);

        auto&& file_name = m_genotype_file_names[idx];
        if (m_intermediate
            && cur_snp.get_counts(homcom_ct, het_ct, homrar_ct, missing_ct,
                                  m_prs_calculation.use_ref_maf))
        {
            // Have intermediate file and have the counts
            // read in the genotype information to the genotype vector
            m_genotype_file.read(file_name, byte_pos, unfiltered_sample_ct4,
                                 reinterpret_cast<char*>(genotype.data()));
        }
        else if (m_intermediate)
        {
            // Have intermediate file but not have the counts
            throw std::logic_error("Error: Sam has a logic error in bgen");
        }
        else
        {
            // now read in the genotype information
            context = m_context_map[idx];
            // start performing the parsing
            genfile::bgen::read_and_parse_genotype_data_block<PLINK_generator>(
                m_genotype_file, file_name + ".bgen", context, setter,
                &m_buffer1, &m_buffer2, byte_pos);
            setter.get_count(homcom_ct, het_ct, homrar_ct, missing_ct);
        }
        // TODO: if we haven't got the count from the genotype matrix, we will
        // need to calculate that, we might not need to do the
        // counting as that is already done when we convert the dosages into
        // the binary genotypes
        homcom_weight = m_homcom_weight;
        het_weight = m_het_weight;
        homrar_weight = m_homrar_weight;
        maf = 1.0
              - static_cast<double>(homcom_weight * homcom_ct
                                    + het_ct * het_weight
                                    + homrar_weight * homrar_ct)
                    / (static_cast<double>(homcom_ct + het_ct + homrar_ct)
                       * ploidy);
        if (cur_snp.is_flipped())
        {
            // change the mean to reflect flipping
            maf = 1.0 - maf;
            // swap the weighting
            std::swap(homcom_weight, homrar_weight);
        }
        stat = cur_snp.stat(); // Multiply by ploidy
        // only set these value to the imputed value if we require them
        adj_score = 0;
        if (is_centre) { adj_score = ploidy * stat * maf; }
        miss_score = 0;
        if (mean_impute)
        {
            // again, mean_impute is stable, branch prediction should be ok
            miss_score = ploidy * stat * maf;
        }
        // start reading the genotype
        read_prs(genotype, ploidy, stat, adj_score, miss_score, miss_count,
                 homcom_weight, het_weight, homrar_weight, not_first);
        // we've finish processing the first SNP no longer need to reset the
        // PRS
        not_first = true;
    }
}


void BinaryGen::read_score(const std::vector<size_t>::const_iterator& start_idx,
                           const std::vector<size_t>::const_iterator& end_idx,
                           bool reset_zero)
{
    // because I don't want to touch the code in dosage_score, we will reset
    // the sample here reset_sample_prs();
    if (m_hard_coded)
    {
        // for hard coded, we need to check if intermediate file is used
        // instead
        hard_code_score(start_idx, end_idx, reset_zero);
    }
    else
    {
        dosage_score(start_idx, end_idx, reset_zero);
    }
}
