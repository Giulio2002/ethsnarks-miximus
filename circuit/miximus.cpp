/*    
    copyright 2018 to the Miximus Authors

    This file is part of Miximus.

    Miximus is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Miximus is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Miximus.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "miximus.hpp"
#include "export.hpp"
#include "import.hpp"
#include "stubs.hpp"
#include "utils.hpp"

#include "gadgets/mimc.hpp"
#include "gadgets/merkle_tree.cpp"

#include <libsnark/gadgetlib1/gadgets/basic_gadgets.hpp>


using libsnark::generate_r1cs_equals_const_constraint;
using libff::convert_field_element_to_bit_vector;
using ethsnarks::ppT;
using ethsnarks::FieldT;
using ethsnarks::ProtoboardT;
using ethsnarks::ProvingKeyT;

const size_t MIXIMUS_TREE_DEPTH = 29;

namespace ethsnarks {

/**
* 
*/
class mod_miximus : public GadgetT
{
public:
    typedef MiMC_hash_gadget HashT;
    const size_t tree_depth = MIXIMUS_TREE_DEPTH;


    // public inputs
    const VariableT root_var;
    const VariableT nullifier_var;
    const VariableT external_hash_var;

    // public constants
    const VariableArrayT m_IVs;

    // constant inputs
    const VariableT spend_hash_IV;
    const VariableT leaf_hash_IV;

    // private inputs
    const VariableT spend_preimage_var;
    const VariableArrayT address_bits;    
    const VariableArrayT path_var;

    // logic gadgets
    HashT spend_hash;
    HashT leaf_hash;

    merkle_path_authenticator<HashT> m_authenticator;


    mod_miximus(
        ProtoboardT &in_pb,
        const std::string &annotation_prefix
    ) :
        GadgetT(in_pb, annotation_prefix),

        // public inputs
        root_var(make_variable(in_pb, FMT(annotation_prefix, ".root_var"))),
        nullifier_var(make_variable(in_pb, FMT(annotation_prefix, ".nullifier_var"))),
        external_hash_var(make_variable(in_pb, FMT(annotation_prefix, ".external_hash_var"))),

        // Initialisation vector for merkle tree
        // Hard-coded constants
        // Means that H('a', 'b') on level1 will have a different output than the same values on level2
        m_IVs(merkle_tree_IVs(in_pb)),

        // constant inputs
        spend_hash_IV(make_variable(in_pb, FMT(annotation_prefix, ".spend_hash_IV"))),
        leaf_hash_IV(make_variable(in_pb, FMT(annotation_prefix, ".leaf_hash_IV"))),

        // private inputs
        spend_preimage_var(make_variable(in_pb, FMT(annotation_prefix, ".spend_preimage_var"))),
        address_bits(make_var_array(in_pb, tree_depth, FMT(annotation_prefix, ".address_bits")) ),
        path_var(make_var_array(in_pb, tree_depth, FMT(annotation_prefix, ".path"))),

        // logic gadgets
        spend_hash(in_pb, spend_hash_IV, {spend_preimage_var, nullifier_var}, FMT(annotation_prefix, ".spend_hash")),
        leaf_hash(in_pb, leaf_hash_IV, {nullifier_var, spend_hash.result()}, FMT(annotation_prefix, ".leaf_hash")),
        m_authenticator(in_pb, tree_depth, address_bits, m_IVs, leaf_hash.result(), root_var, path_var, FMT(annotation_prefix, ".authenticator"))
    {
        in_pb.set_input_sizes( 3 );

        // TODO: verify that inputs are expected publics
    }

    void generate_r1cs_constraints()
    {
        spend_hash.generate_r1cs_constraints();
        leaf_hash.generate_r1cs_constraints();
        m_authenticator.generate_r1cs_constraints();
    }

    void generate_r1cs_witness(
        FieldT in_root,         // merkle tree root
        FieldT in_nullifier,    // unique linkable tag
        FieldT in_exthash,      // hash of external parameters
        FieldT in_preimage,     // spend preimage
        libff::bit_vector in_address,
        std::vector<FieldT> &in_path
    ) {
        // public inputs
        this->pb.val(root_var) = in_root;
        this->pb.val(nullifier_var) = in_nullifier;
        this->pb.val(external_hash_var) = in_exthash;

        // private inputs
        this->pb.val(spend_preimage_var) = in_preimage;
        address_bits.fill_with_bits(this->pb, in_address);

        for( size_t i = 0; i < tree_depth; i++ ) {
            this->pb.val(path_var[i]) = in_path[i];
        }

        // gadgets
        spend_hash.generate_r1cs_witness();
        leaf_hash.generate_r1cs_witness();
        m_authenticator.generate_r1cs_witness();
    }
};

// namespace ethsnarks
}


size_t miximus_tree_depth( void ) {
    return MIXIMUS_TREE_DEPTH;
}


char *miximus_prove(
    const char *pk_file,
    const char *in_root,
    const char *in_nullifier,
    const char *in_exthash,
    const char *in_spend_preimage,
    const char *in_address,
    const char **in_path
) {
    ppT::init_public_params();

    FieldT arg_root(in_root);
    FieldT arg_nullifier(in_nullifier);
    FieldT arg_exthash(in_exthash);
    FieldT arg_spend_preimage(in_spend_preimage);

    // Fill address bits with 0s and 1s from str
    libff::bit_vector address_bits;
    address_bits.resize(MIXIMUS_TREE_DEPTH);
    if( strlen(in_address) != MIXIMUS_TREE_DEPTH )
    {
        std::cerr << "Address length doesnt match depth" << std::endl;
        return nullptr;
    }
    for( size_t i = 0; i < MIXIMUS_TREE_DEPTH; i++ ) {
        if( in_address[i] != '0' and in_address[i] != '1' ) {
            std::cerr << "Address bit " << i << " invalid, unknown: " << in_address[i] << std::endl;
            return nullptr;
        }
        address_bits[i] = '0' - in_address[i];
    }


    // Fill path from field elements from in_path
    std::vector<FieldT> arg_path;
    arg_path.resize(MIXIMUS_TREE_DEPTH);
    for( size_t i = 0; i < MIXIMUS_TREE_DEPTH; i++ ) {
        assert( in_path[i] != nullptr );
        arg_path[i] = FieldT(in_path[i]);
    }

    ProtoboardT pb;
    ethsnarks::mod_miximus mod(pb, "module");
    mod.generate_r1cs_constraints();
    mod.generate_r1cs_witness(arg_root, arg_nullifier, arg_exthash, arg_spend_preimage, address_bits, arg_path);

    if( ! pb.is_satisfied() )
    {
        std::cerr << "Not Satisfied!" << std::endl;
        return nullptr;
    }

    auto json = ethsnarks::stub_prove_from_pb(pb, pk_file);

    return ::strdup(json.c_str());
}


int miximus_genkeys( const char *pk_file, const char *vk_file )
{
    return ethsnarks::stub_genkeys<ethsnarks::mod_miximus>(pk_file, vk_file);
}


bool miximus_verify( const char *vk_json, const char *proof_json )
{
    return ethsnarks::stub_verify( vk_json, proof_json );
}
