#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include "qrcodegen.h"
#include <string.h>

#include <brotli/encode.h>
#include "ascii85.h"

#include "numberfont.h"

#define DEBUG 0

#define DATA_FRAME_MAGIC     "XPOP"
#define PARITY_FRAME_MAGIC   "XPAR"
#define DATA_FRAMES_PER_PARITY_FRAME 4

#define QRVERSION (20U)
#define QRMODULECOUNT (97U)
#define QRDATASIZE (450U)

#define QUIETZONE (0U)
#define FRAMEDELAY (4U)

//gif options 
#define PIXELS_PER_BLOCK 450

#define MAX_BUF (3*1024*1024)
//#define TEXT_MODE 1
#define FONT_OFFSET 30
#define SHOW_FRAME_COUNTER 1
uint64_t global_counter = 0;


int main(int argc, char** argv)
{


    uint8_t* input_data = (uint8_t*)malloc(MAX_BUF);
    size_t input_length = 0;
    
    uint8_t* compressed_data = (uint8_t*)malloc(MAX_BUF);
    size_t compressed_length = MAX_BUF;

    size_t frame_count = 0;
    size_t last_parity_frame = 0;

    {
        int read_fd = 0;
        
        size_t read_size = 1;
        
        size_t read_upto = 0;

        while (read(read_fd, 0, 0) > -1)    // !EOF
        {
            if (read_upto + read_size > MAX_BUF)
                read_size = MAX_BUF - read_upto;

            if (read_size <= 0)
                return fprintf(stderr, "Input too large\n");

            size_t result = read(read_fd, input_data + read_upto, read_size);
            if (result <= 0)
                break;

            read_upto += result;
        }

        input_length = read_upto;

        if (DEBUG)
            fprintf(stderr, "raw input len: %d\n", input_length);
        
        if (BrotliEncoderCompress(BROTLI_DEFAULT_QUALITY, BROTLI_DEFAULT_WINDOW, BROTLI_MODE_TEXT,
            input_length, input_data, &compressed_length, compressed_data) != BROTLI_TRUE)
            return fprintf(stderr, "Failed to compress input\n");

        if (DEBUG)
            fprintf(stderr, "compressed len: %d\n", compressed_length);

        size_t max_ascii85_length = ascii85_get_max_encoded_length(compressed_length);

        if (DEBUG)    
            fprintf(stderr, "max ascii85 size: %d\n", max_ascii85_length);

        if (max_ascii85_length > MAX_BUF)
            return fprintf(stderr, "Input too large: ascii85 would not fit in buffer\n");

        
        int32_t ascii85_length = encode_ascii85(compressed_data, compressed_length, input_data, MAX_BUF);

        if (DEBUG)
            fprintf(stderr, "asci85 len: %d\n", ascii85_length);

        if (ascii85_length <= 0 || ascii85_length > MAX_BUF)
            return fprintf(stderr, "Failed to ascii85 encode compressed input\n");

        input_length = (size_t)(ascii85_length);


        if (DEBUG)
            fprintf(stderr, "ascii85: `%.*s`\n", ascii85_length, input_data);

        // compute frame number
        frame_count = input_length / QRDATASIZE;
        if (input_length % QRDATASIZE)
            frame_count++;


        if (DEBUG)
            fprintf(stderr, "input length: %d\nframe count: %d\n", input_length, frame_count);
    }



    uint8_t* b = (uint8_t*)malloc(MAX_BUF);
    uint32_t u = 0;

    // HEADER
    {
        // header signature
        b[u++] = 'G'; b[u++] = 'I'; b[u++] = 'F';

        // header version
        b[u++] = '8'; b[u++] = '9'; b[u++] = 'a';
    }

    // LOGICAL SCREEN
    {
#define LOGICALSIZE (QRMODULECOUNT + QUIETZONE*2)        

        // logical screen width
        b[u++] = LOGICALSIZE & 0xFFU; b[u++] = LOGICALSIZE >> 8U;

        // logical screen height
        b[u++] = LOGICALSIZE & 0xFFU; b[u++] = LOGICALSIZE >> 8U;

        // packed fields and flags
        b[u++] = 0b10000000; /* global colour table = 1, colour resolution = 000,
                                sort flag = 0,    size global colour table = 000 */

        // background colour index
        b[u++] = 0x1;

        // pixel aspect ratio
        b[u++] = 0x1U;
    }

    // GLOBAL COLOUR TABLE
    {
        b[u++] = 0x00;   b[u++] = 0x00;   b[u++] = 0x00;
        b[u++] = 0xFFU;  b[u++] = 0xFFU; b[u++] = 0xFFU;

    }

    // APPLICATION EXTENSION
    {
        // extension header
        b[u++] = 0x21U; b[u++] = 0xFFU;

        // application name size
        b[u++] = 0x0BU;

        // application name
        for (uint8_t* x = "NETSCAPE2.0"; *x; ++x)
            b[u++] = *x;

        // size of subblock
        b[u++] = 0x03U;

        // index of subblock
        b[u++] = 0x01U;

        // animated gif repetitions
        b[u++] = 0x0;   b[u++] = 0x0;   // zero = unlimited loops

        // end of subblock chain
        b[u++] = 0x0;
    }


    uint8_t parity_data[QRDATASIZE + 13];
    // prepare parity frame header, which never changes
    for (int i = 0; i < 4; ++i)
        parity_data[i] = PARITY_FRAME_MAGIC[i];

    // zero parity frame ready to receive bytes
    for (int i = 12; i < sizeof(parity_data); ++i)
        parity_data[i] = 0;


    size_t last_frame_size = 0; // this is used by parity frames, in particular the final frame

    // FRAME
    for (int frame = 0; frame <= frame_count; ++frame)
    {

        int is_parity_frame = frame > 0 && 
            frame % DATA_FRAMES_PER_PARITY_FRAME == 0 && last_parity_frame < frame ||
                frame == frame_count;

        uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(QRVERSION)];
        if (is_parity_frame)
        {

//            fprintf(stderr, "Generating parity frame at frame %d\n", frame);

            // GENERATE PARITY FRAME
            uint8_t hdr[9];

            // first finish header
            sprintf(hdr, "%02X%02X%04X", 1, frame, last_frame_size);
            for (int i = 4; i < 12; ++i)
                parity_data[i] = hdr[i-4];
        
            // next normalize ascii values (by adding ascii85 base char)
            for (int i = 12; i < sizeof(parity_data) - 1; ++i)
                parity_data[i] += 33U;
                      
            // place a null mark at the end of the frame
            parity_data[QRDATASIZE + 12] = '\0'; // (note: should already be there due to memclear)


            if (DEBUG)
            {
                fprintf(stderr, "parse_parity(Buffer.from('");
                for (int i = 0; i < QRDATASIZE + 12; ++i)
                    fprintf(stderr, "%02X", parity_data[i]);
                fprintf(stderr, "', 'hex').toString('utf-8'));\n");
            }

            // now generate the qr
            uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(QRVERSION)];
            if (!qrcodegen_encodeText(parity_data, tmp, qrcode, qrcodegen_Ecc_QUARTILE, QRVERSION, QRVERSION, -1, 1))
            {
                fprintf(stderr, "failed to generate qr\n");
                return 1;
            } 

            // denormalize parity frame
            for (int i = 12; i < sizeof(parity_data) - 1; ++i)
                parity_data[i] -= 33U;

            last_parity_frame = frame;

            // ensure the regular frame in this spot is written unless we're at the end
            if (frame != frame_count)
                frame--;
        }
        else
        {
            // GENERATE NORMAL FRAME
            
            size_t start_of_frame = frame * QRDATASIZE;
            size_t end_of_frame = (frame + 1) * QRDATASIZE;
            if (end_of_frame > input_length)
                end_of_frame = input_length;

            // record this for the next parity frame header
            last_frame_size = end_of_frame - start_of_frame;

            // XOR this frame toward next parity frame
            for (int i = start_of_frame, j = 12; i < end_of_frame; ++i, ++j)
                parity_data[j] = (uint8_t)((((uint64_t)parity_data[j]) + (uint64_t)(input_data[i]) - 33U) 
                        % 85U);

            // for text mode only we will add and then remove \0 at end of frame
            uint8_t c = input_data[end_of_frame];
            input_data[end_of_frame] = '\0';
            uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(QRVERSION)];
            uint8_t data[qrcodegen_BUFFER_LEN_FOR_VERSION(QRVERSION)];
            size_t len = end_of_frame - start_of_frame;
            sprintf(data, DATA_FRAME_MAGIC "%02X%02X%04X", frame+1, frame_count, last_frame_size);
            memcpy(data + 12, input_data + start_of_frame, len);
            data[len + 12] = '\0';

            if (DEBUG)
            {
                fprintf(stderr, "parse_frame(Buffer.from('");
                for (int i = 0; i < len + 12; ++i)
                    fprintf(stderr, "%02X", data[i]);
                fprintf(stderr, "', 'hex').toString('utf-8'));\n");
            }

            if (!qrcodegen_encodeText(data, tmp, qrcode, qrcodegen_Ecc_QUARTILE, QRVERSION, QRVERSION, -1, 1))
            {
                fprintf(stderr, "failed to generate qr\n");
                return 1;
            } 

            input_data[end_of_frame] = c;
        }

        // GRAPHIC CONTROL EXTENSION
        {
            // graphic control extension for frame
            b[u++] = 0x21U; b[u++] = 0xF9U;

            // subblock length
            b[u++] = 0x04U;

            // disposal method = 1, no user input, no transparent colour
            b[u++] = 0b00000100;

            // number of 1/100ths of a second to wait
            b[u++] = FRAMEDELAY & 0xFFU;   b[u++] = FRAMEDELAY >> 8U;

            // transparent colour index (unused)
            b[u++] = 0xFFU;

            // end of subblock
            b[u++] = 0x0;
        }

        // IMAGE DESCRIPTOR
        {
            // image seperator
            b[u++] = 0x2C;

            // left position
            b[u++] = QUIETZONE % 0xFFU;   b[u++] = QUIETZONE >> 8U;

            // top position
            b[u++] = QUIETZONE % 0xFFU;   b[u++] = QUIETZONE >> 8U;

            // width
            b[u++] = (QRMODULECOUNT-0) & 0xFFU; b[u++] = (QRMODULECOUNT-0) >> 8U;

            // height
            b[u++] = (QRMODULECOUNT-0) & 0xFFU; b[u++] = (QRMODULECOUNT-0) >> 8U;

            // packed fields
            b[u++] = 0x0;   // no flags light


        }



        // IMAGE DATA
        {
            // LZW Minimum code size
            b[u++] = 2U; // minimum code size = 2 even for monochrome

            int64_t pixel_count = (QRMODULECOUNT-0) * (QRMODULECOUNT-0);



            int64_t block_count = pixel_count / PIXELS_PER_BLOCK;

            uint8_t final_block_size = 255;

            // edge case: last block will be smaller
            if (pixel_count % PIXELS_PER_BLOCK)
            {
                block_count++;
                int64_t remaining_pixels = pixel_count % PIXELS_PER_BLOCK;


                int64_t final_bits = remaining_pixels * 3;
                final_bits += final_bits >> 1U; // allowance for the clear codes

                final_block_size = (uint8_t)(final_bits / 8);
                if (final_bits % 8)
                    final_block_size++;
            }


            while (block_count-- > 0)
            {

                uint8_t bytes_remaining_in_block = 0xFFU;

                int64_t pixels_remaining_in_block = 678;

                if (block_count == 0)
                {
                    bytes_remaining_in_block = final_block_size;
                    pixels_remaining_in_block = pixel_count;
                }

                b[u++] = bytes_remaining_in_block;

//                printf("\nBlock size: %02x\n\t", bytes_remaining_in_block);

                // we'll code using a uint16_t as a shift register
                uint16_t sr = 0;
                uint8_t bc= 0;

                // clear code
                sr |= (0b100 << bc); bc += 3;

                int counter = 0;
                int max_counter = 2;

                while (bytes_remaining_in_block-- > 0)
                {

                    while (bc < 8)
                    {

                            size_t y = QRMODULECOUNT - ((pixel_count-1) / (QRMODULECOUNT-0)) - 1;
                            size_t x = QRMODULECOUNT - ((pixel_count-1) % (QRMODULECOUNT-0)) - 1;

                            bool light = qrcodegen_getModule(qrcode, x, y);

                            // frame counter
                            if (SHOW_FRAME_COUNTER)
                            {
                                int digits[5] = {
                                    (frame+1) / 10,
                                    (frame+1) % 10,
                                    10,
                                    (is_parity_frame ? 10 : frame_count / 10),
                                    (is_parity_frame ? 10 : frame_count % 10)
                                };

                                
                                for (int d = 0; d < 5; ++d)
                                {
                                    int startx = FONT_OFFSET + 8*d;
                                    int starty = FONT_OFFSET + 17;
                                    if (y >= starty && y < starty + 8 && x > startx && x <= startx + 8)
                                        light = !(number_font[digits[d]][y-starty]&(1<<(8-(x-startx))));
                                }

                            }


                            sr |= (light ? 0b000 : 0b001) << bc; bc+= 3;
                                

                            --pixels_remaining_in_block;
                            --pixel_count;
                            ++counter;


                            if (counter >= max_counter)
                            {
                                // clear code
                                sr |= (0b100 << bc); bc += 3;
                                counter = 0;
                            }
                            if (pixels_remaining_in_block <= 0)
                                break;
                    }



                    // write lead out for the block
                    if (pixels_remaining_in_block == 0)
                    {
                        sr |= (0b101 << bc); bc += 3;   // stop
                    }


                    // send the byte out
                    uint8_t byte_out = sr & 0xFFU;
//                    printf("%02X ", byte_out);
                    b[u++] = byte_out;

                    sr >>= 8U;
                    bc -= 8;
                }

            }

            // block terminator
            b[u++] = 0x0;


        }
    }

    // TRAILER
    {
        b[u++] = 0x3BU;

    }

//    printf("\n");

    write(1, b, u);
    //close(fd);
    free(b);
}
