#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include "qrcodegen.h"
/*

RH NOTE: Multibyte values are little endian

The Grammar.

<GIF Data Stream> ::=     Header <Logical Screen> <Data>* Trailer

<Logical Screen> ::=      Logical Screen Descriptor [Global Color Table]

<Data> ::=                <Graphic Block>  |
                          <Special-Purpose Block>

<Graphic Block> ::=       [Graphic Control Extension] <Graphic-Rendering Block>

<Graphic-Rendering Block> ::=  <Table-Based Image>  |
                               Plain Text Extension

<Table-Based Image> ::=   Image Descriptor [Local Color Table] Image Data

<Special-Purpose Block> ::=    Application Extension  |
                               Comment Extension

*/

#define QRVERSION (25U)
#define QRMODULECOUNT (117U)
#define QRDATASIZE (900U)
#define QUIETZONE (5U)
#define FRAMEDELAY (0U)

//gif options 
#define PIXELS_PER_BLOCK 450


uint64_t global_counter = 0;

int main()
{

    uint8_t* b = (uint8_t*)malloc(1024*1024);
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
        b[u++] = 0x0;

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

    // FRAME
    for (int flip = 0; flip < 36; ++flip)
    {
        // GENERATE QR
        uint8_t data[qrcodegen_BUFFER_LEN_FOR_VERSION(QRVERSION)];
        uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(QRVERSION)];

        //data[0] = flip;
        //data[1] = 2;
        for (int i = 0; i < QRDATASIZE; ++i)
            data[i] = (uint8_t)(flip + 'a');
        

        data[QRDATASIZE] = '\0';

        uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(QRVERSION)];
        //if (!qrcodegen_encodeBinary(data, QRDATASIZE, qrcode, 0, QRVERSION, QRVERSION, -1, 1))
        if (!qrcodegen_encodeText(data, tmp, qrcode, 0, QRVERSION, QRVERSION, -1, 1))
        {
            fprintf(stderr, "failed to generate qr\n");
            return 1;
        } 

    /*
    for (size_t j = 0; j < QRMODULECOUNT; ++j)
    {
        for (size_t i = 0; i < QRMODULECOUNT; ++i)
        {
            printf("%s", (qrcodegen_getModule(qrcode, j, i) ? "%" : " "));
        }
        printf("\n");
    }
*/

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
            b[u++] = (QRMODULECOUNT) & 0xFFU; b[u++] = (QRMODULECOUNT) >> 8U;

            // height
            b[u++] = (QRMODULECOUNT) & 0xFFU; b[u++] = (QRMODULECOUNT) >> 8U;

            // packed fields
            b[u++] = 0x0;   // no flags active


        }



        // IMAGE DATA
        {
            // LZW Minimum code size
            b[u++] = 2U; // minimum code size = 2 even for monochrome

            int64_t pixel_count = QRMODULECOUNT * QRMODULECOUNT;



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

                            size_t y = QRMODULECOUNT -1 - ((pixel_count) / QRMODULECOUNT);
                            size_t x = QRMODULECOUNT - ((pixel_count) % QRMODULECOUNT);

                            bool active = qrcodegen_getModule(qrcode, x, y);

                            if (x == QRMODULECOUNT || y == 0)
                                active = true;
                                    
                            sr |= (active ? 0b001 : 0b000) << bc; bc+= 3;
                                
                                //(/*x >= QRMODULECOUNT-1 || y >= QRMODULECOUNT-1 ||*/ qrcodegen_getModule(qrcode, x, y))
                                //? 0b001 : 0b000 << bc; bc += 3;


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

    int fd = open("./out.gif",  O_WRONLY | O_CREAT | O_TRUNC);
    write(fd, b, u);
    close(fd);
    free(b);
}
