// dec.cpp

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

#include "fdk-aac/aacdecoder_lib.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <vector>



void check( AAC_DECODER_ERROR rc )
{
    if ( rc != AAC_DEC_OK ) {
        std::cout << "Error: " << std::hex << rc << "\n";
        exit(1);
    }
}

HANDLE_AACDECODER decoder_make( std::ifstream& conf_file )
{
    AAC_DECODER_ERROR rc;
    HANDLE_AACDECODER dec = aacDecoder_Open( TT_MP4_LOAS, 1 );

    std::vector<UCHAR> conf(
        (std::istreambuf_iterator<char>( conf_file )),
        std::istreambuf_iterator<char>()
    );
    const unsigned conf_size = conf.size();
    UCHAR* conf_data = conf.data();

    rc = aacDecoder_ConfigRaw( dec, &conf_data, &conf_size );
    check( rc );

    return dec;
}

// from test file
void decode(
    HANDLE_AACDECODER decoder,
    const uint8_t *ptr,
    int size,
    uint8_t *decoder_buffer,
    int decoder_buffer_size,
    int channels,
    AudioFile<INT_PCM>& file_out
) {
    const int block_size = decoder_buffer_size / sizeof(INT_PCM);
    AAC_DECODER_ERROR rc;
    CStreamInfo *info;
    UINT valid, buffer_size;
    do {
        valid = buffer_size = size;
        rc = aacDecoder_Fill(decoder, (UCHAR**) &ptr, &buffer_size, &valid);
        ptr += buffer_size - valid;
        size -= buffer_size - valid;
        if (rc == AAC_DEC_NOT_ENOUGH_BITS)
            continue;
        if (rc != AAC_DEC_OK)
            break;
        rc = aacDecoder_DecodeFrame(decoder, (INT_PCM *) decoder_buffer, block_size, 0);
        if (!ptr && rc != AAC_DEC_OK)
            break;
        if (rc == AAC_DEC_NOT_ENOUGH_BITS)
            continue;
        if (rc != AAC_DEC_OK) {
            std::cout << "Decoding failed\n";
            check( rc );
            return;
        }
        info = aacDecoder_GetStreamInfo(decoder);
        if (info->numChannels != channels) {
            std::cout << "Mismatched number of channels\n";
            return;
        }

        const INT_PCM* buf_out = reinterpret_cast<INT_PCM*>( decoder_buffer );
        for ( int i = 0; i < block_size; i++ )
            file_out.samples[i % channels].push_back( buf_out[i] );
    } while (size > 0);
    return;
}

void decode_all(
    HANDLE_AACDECODER dec,
    std::ifstream& file_in,
    AudioFile<INT_PCM>& file_out
) {
    AAC_DECODER_ERROR rc;
    const CStreamInfo* info = aacDecoder_GetStreamInfo( dec );

    const int n_channels = info->channelConfig;
    const int block_size = info->aacSamplesPerFrame;

    file_out.setSampleRate( info->aacSampleRate );
    file_out.setNumChannels( n_channels );
    
    std::vector<UCHAR> in_buf(
        (std::istreambuf_iterator<char>( file_in )),
        std::istreambuf_iterator<char>()
    );
    const UINT in_buf_size = in_buf.size();
    UCHAR* in_buf_data = in_buf.data();

    std::vector<INT_PCM> out_buf( block_size * n_channels );

    decode( dec, in_buf_data, in_buf_size, (uint8_t* )out_buf.data(),
        out_buf.size() * sizeof( INT_PCM ), n_channels, file_out );
}


int main( int argc, char** argv )
{
    args::ArgumentParser parser( "AAC Raw Decoder.", "" );
    args::HelpFlag help( parser, "help", "Display this help menu", {'h', "help"} );
    args::Positional<std::string> args_filename_in( parser, ".raw", "Input filename" );
    args::Positional<std::string> args_filename_conf( parser, ".conf", "Configuration filename" );
    args::Positional<std::string> args_filename_out( parser, ".wav", "Output filename" );
    args::Flag args_verbose(parser, "verbose", "Log debug information.", {'v', "verbose"});

    parser.ParseCLI( argc, argv );

    if ( !args_filename_in || !args_filename_conf || !args_filename_out ) {
        std::cout << parser;
        return 0;
    }

    const std::string& filename_in = args::get( args_filename_in );
    const std::string& filename_conf = args::get( args_filename_conf );
    const std::string& filename_out = args::get( args_filename_out );
    const bool verbose = args::get( args_verbose );


    std::ifstream file_in( filename_in, std::ios::binary );
    std::ifstream file_conf( filename_conf, std::ios::binary );
    AudioFile<INT_PCM> file_out;

    HANDLE_AACDECODER dec = decoder_make( file_conf );

    if ( verbose ) { 
        const CStreamInfo* info = aacDecoder_GetStreamInfo( dec );

        const int n_channels = info->channelConfig;
        const int sample_rate = info->aacSampleRate;
        const int block_size = info->aacSamplesPerFrame;
        
        std::cout << "Channels:     " << n_channels << "\n";
        std::cout << "Block size:   " << block_size << "\n";
        std::cout << "Sample rate:  " << sample_rate << " Hz\n";
        std::cout << "\n";
        std::cout << "Decoding... ";
    }

    decode_all( dec, file_in, file_out );

    if ( verbose ) {
        std::cout << "done.\n";
        std::cout << "Writing " << filename_out << "... ";
    }

    aacDecoder_Close( dec );
    file_in.close();
    file_out.save( filename_out );

    if ( verbose )
        std::cout << "done\n";

}