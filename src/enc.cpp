// enc.cpp

// TODO: only mono currently works -- fix

#include "args.hpp"

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#pragma GCC diagnostic ignored "-Woverflow"
#endif
#include "AudioFile.h"
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include "fdk-aac/aacenc_lib.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <vector>

static const unsigned DEFAULT_PROFILE       = 2;
static const unsigned DEFAULT_BITRATE       = 64000;

/*
Profile
    - 2: MPEG-4 AAC Low Complexity.
    - 5: MPEG-4 AAC Low Complexity with Spectral Band Replication
        (HE-AAC).
    - 29: MPEG-4 AAC Low Complexity with Spectral Band Replication
        and Parametric Stereo (HE-AAC v2). Stereo only.
    - 23: MPEG-4 AAC Low-Delay.
    - 39: MPEG-4 AAC Enhanced Low-Delay.
    - 129: MPEG-2 AAC Low Complexity.
    - 132: MPEG-2 AAC Low Complexity with Spectral Band Replication
        (HE-AAC).

Sample Rate:
    - Supports 8000, 11025, 12000, 16000, 22050, 24000, 32000,
        44100, 48000, 64000, 88200, 96000
*/

void check( AACENC_ERROR rc )
{
    if ( rc != AACENC_OK ) {
        std::cout << "Error: " << std::hex << rc << "\n";
        exit(1);
    }
}

HANDLE_AACENCODER encoder_make(
    unsigned profile,
    unsigned sample_rate,
    unsigned channel_mode,
    unsigned bitrate
) {
    AACENC_ERROR rc;
    HANDLE_AACENCODER enc = nullptr;

    // allocate
    rc = aacEncOpen( &enc, 0, 0 );
    check( rc );

    // set user params
    rc = aacEncoder_SetParam( enc, AACENC_AOT, profile );
    if ( rc ) std::cout << "Invalid profile.\n";
    check( rc );
    
    rc = aacEncoder_SetParam( enc, AACENC_SAMPLERATE, sample_rate );
    if ( rc ) std::cout << "Invalid sample rate.\n";
    check( rc );

    rc = aacEncoder_SetParam( enc, AACENC_CHANNELMODE, channel_mode );
    check( rc );

    rc = aacEncoder_SetParam( enc, AACENC_BITRATE, bitrate );
    if ( rc ) std::cout << "Invalid bitrate.\n";
    check( rc );
    
    // Disable bit reservoir by setting peak bitrate = bitrate
    rc = aacEncoder_SetParam( enc, AACENC_PEAK_BITRATE, bitrate );
    check( rc );

    // CBR
    rc = aacEncoder_SetParam( enc, AACENC_BITRATEMODE, 0 );
    check( rc );

    // Audio Sync Stream - keeps frame length constant
    rc = aacEncoder_SetParam( enc, AACENC_TRANSMUX, TT_MP4_LOAS );
    check( rc );


    // initialize
    rc = aacEncEncode( enc, NULL, NULL, NULL, NULL );
    check( rc );

    return enc;
}

int encode_all(
    HANDLE_AACENCODER enc,
    const AudioFile<INT_PCM>& file_in,
    std::ofstream& file_out
) {
    AACENC_InfoStruct info;
    AACENC_ERROR rc = aacEncInfo( enc, &info );

    const int n_samples = file_in.getNumSamplesPerChannel();
    const int n_channels = file_in.getNumChannels();

    const int block_size = info.frameLength;
    const int alloc_size = block_size * n_channels * sizeof( INT_PCM );

    std::vector<unsigned char> in_alloc( alloc_size );
    void* in_data = (void* )in_alloc.data();
    int in_ids = IN_AUDIO_DATA;
    int in_el_sizes = sizeof( INT_PCM ); 
    int in_sizes = alloc_size;
    
    AACENC_BufDesc in_buf;
    in_buf.numBufs = 1;
    in_buf.bufs = &in_data;
    in_buf.bufferIdentifiers = &in_ids;
    in_buf.bufElSizes = &in_el_sizes;
    in_buf.bufSizes = &in_sizes;

    std::vector<unsigned char> out_alloc( alloc_size );
    void* out_data = (void* )out_alloc.data();
    int out_ids = OUT_BITSTREAM_DATA;
    int out_el_sizes = 1; 
    int out_sizes = alloc_size;
    
    AACENC_BufDesc out_buf;
    out_buf.numBufs = 1;
    out_buf.bufs = &out_data;
    out_buf.bufferIdentifiers = &out_ids;
    out_buf.bufElSizes = &out_el_sizes;
    out_buf.bufSizes = &out_sizes;

    AACENC_InArgs in_args;
    AACENC_OutArgs out_args;

    in_args.numInSamples = block_size * n_channels;
    in_args.numAncBytes = 0;

    INT_PCM* in = reinterpret_cast<INT_PCM*>( in_buf.bufs[0] );
    char* out = reinterpret_cast<char*>( out_buf.bufs[0] );

    int min_size = INT32_MAX;
    int max_size = 0;

    for ( int block = 0; block < n_samples; block += block_size ) {
        if ( block + block_size > n_samples )
            std::fill_n( in, block_size * n_channels, 0 );

        const int remaining = std::min( block_size, (int )n_samples - block );

        for ( int i = 0; i < remaining; i++ )
            for ( int c = 0; c < n_channels; c++ )
                in[i + c] = file_in.samples[c][block + i];

        rc = aacEncEncode( enc, &in_buf, &out_buf, &in_args, &out_args );
        check( rc );

        const int out_size = out_args.numOutBytes;

        if ( out_size < min_size )
            min_size = out_size;
        if ( out_size > max_size )
            max_size = out_size;

        file_out.write( out, out_size );
    }

    if ( min_size < max_size ) {
        std::cout << "WARNING: Variable encoded size\n";
        std::cout << "Encoded Size: [" << min_size << " B, " << max_size << " B]\n";
        
        aacEncClose( &enc );
        exit(0);
    }

    return max_size;
}

unsigned get_channel_mode( int n_channels )
{
    if ( n_channels > 8 ) {
        std::cout << "Too many channels in input file.\n";
        exit(0);
    }
    switch ( n_channels ) {
        case 1: return MODE_1;
        case 2: return MODE_2;
        case 3: return MODE_1_2;
        case 4: return MODE_1_2_1;
        case 5: return MODE_1_2_2;
        case 6: return MODE_1_2_2_1;;
        case 7: return MODE_6_1;
        case 8: return MODE_7_1_BACK;
    }

    return MODE_INVALID;
}

void write_conf_file( const AACENC_InfoStruct& info, const std::string& filename )
{
    std::ofstream file( filename, std::ios::trunc | std::ios::binary );
    file.write( reinterpret_cast<const char*>( info.confBuf ), info.confSize );
    file.close();
}

int main( int argc, char** argv )
{
    args::ArgumentParser parser( "AAC Raw Encoder.", "" );
    args::HelpFlag help( parser, "help", "Display this help menu", {'h', "help"} );
    args::Positional<std::string> args_filename_in( parser, ".wav", "Input filename" );
    args::Positional<std::string> args_filename_out( parser, ".raw", "Output filename" );
    args::ValueFlag<unsigned> args_profile( parser, "profile",
        "Codec profile. Options:\n"
        "2:     MPEG-4 AAC-LC\n"
        "5:     MPEG-4 HE-AAC\n"
        "29:    MPEG-4 HE-AAC v2\n"
        "23:    MPEG-4 AAC Low-Delay\n"
        "39:    MPEG-4 AAC Enhanced Low-Delay\n"
        "129:   MPEG-2 AAC-LC\n"
        "132:   MPEG-2 HE-AAC",
        {'p', "profile"} );
    args::ValueFlag<unsigned> args_bitrate( parser,
        "bitrate", "Bitrate", {'b', "bitrate"} );
    args::ValueFlag<std::string> args_filename_conf( parser, ".conf",
        "Additional output filename for raw configuration.", {'c', "conf"} );
    args::Flag args_verbose(parser, "verbose", "Log debug information.", {'v', "verbose"});

    parser.ParseCLI( argc, argv );

    if ( !args_filename_in || !args_filename_out ) {
        std::cout << parser;
        return 0;
    }

    const std::string& filename_in = args::get( args_filename_in );
    const std::string& filename_out = args::get( args_filename_out );
    const bool verbose = args::get( args_verbose );

    AudioFile<INT_PCM> file_in( filename_in );

    std::ofstream file_out( filename_out, std::ios::trunc | std::ios::binary );
    
    const unsigned n_channels = file_in.getNumChannels();
    const unsigned n_samples = file_in.getNumSamplesPerChannel();
    const unsigned sample_rate = file_in.getSampleRate();

    const unsigned profile = args_profile 
        ? args::get( args_profile ) : DEFAULT_PROFILE;

    const unsigned channel_mode = get_channel_mode( n_channels );
    
    const unsigned bitrate = args_bitrate
        ? args::get( args_bitrate ) : DEFAULT_BITRATE;

    HANDLE_AACENCODER enc = encoder_make(
        profile,
        sample_rate,
        channel_mode,
        bitrate
    );
    
    AACENC_ERROR rc;
    
    AACENC_InfoStruct info;
    rc = aacEncInfo( enc, &info );
    check( rc );

    const int block_size = info.frameLength;
    const int decoded_size = block_size * n_channels * sizeof( INT_PCM );

    if ( verbose ) {
        std::cout << "Channels:     " << n_channels << "\n";
        std::cout << "Block size:   " << block_size << "\n";
        std::cout << "Length:       " << (float )n_samples / sample_rate << " s\n";
        std::cout << "Samples:      " << n_samples << "\n";
        std::cout << "Sample rate:  " << sample_rate << " Hz\n";
        std::cout << "\n";
    }

    if ( verbose )
        std::cout << "Encoding... " << std::flush;

    const int encoded_size = encode_all( enc, file_in, file_out );

    if ( verbose )
        std::cout << "done.\n";

    if ( args_filename_conf ) {
        const std::string filename_conf = args::get( args_filename_conf );
        rc = aacEncInfo( enc, &info );
        check( rc );

        if ( verbose )
            std::cout << "Writing " << filename_conf << "... ";

        write_conf_file( info, filename_conf );

        if ( verbose )
            std::cout << "done\n";
    }

    aacEncClose( &enc );

    if ( verbose ) {
        std::cout << "\n";

        const int in_size = n_samples * n_channels * sizeof( INT_PCM );
        const int out_size = file_out.tellp();

        std::cout << "Total Input:  " << in_size << " B\n";
        std::cout << "Total Output: " << out_size << " B\n";
        std::cout << "Encoded Size: " << encoded_size << " B\n";
        std::cout << "Decoded Size: " << decoded_size << " B\n"; 
        std::cout << "\n";
        std::cout << "Writing " << filename_out << "... ";
    }
    
    file_out.close();

    if ( verbose )
        std::cout << "done.\n";
}