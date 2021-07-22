#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include "qrcodegen.h"
#include <string.h>
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

#define MAX_BUF (1024*1024)
#define TEXT_MODE 0
#define FONT_OFFSET 15
uint64_t global_counter = 0;

uint8_t number_font[][8] = {
    {   // 0
        0b00000000,
        0b00111100,
        0b01000110,
        0b01001010,
        0b01010010,
        0b01100010,
        0b00111100,
        0b00000000
    },
    {   // 1
        0b00000000,
        0b00011000,
        0b00101000,
        0b01001000,
        0b00001000,
        0b00001000,
        0b01111110,
        0b00000000
    },
    {   // 2
        0b00000000,
        0b00111100,
        0b01000010,
        0b00000010,
        0b00111100,
        0b01000000,
        0b01111110,
        0b00000000
    },
    {   // 3
        0b00000000,
        0b00111100,
        0b01000010,
        0b00001100,
        0b00000010,
        0b01000010,
        0b00111100,
        0b00000000
    },
    {   // 4
        0b00000000,
        0b00011100,
        0b00100100,
        0b01000100,
        0b01000100,
        0b01111110,
        0b00000100,
        0b00000000
    },
    {   // 5
        0b00000000,
        0b01111100,
        0b01000000,
        0b01111100,
        0b00000010,
        0b00000010,
        0b01111100,
        0b00000000
    },
    {   // 6
        0b00000000,
        0b00111100,
        0b01000000,
        0b01111100,
        0b01000010,
        0b01000010,
        0b00111100,
        0b00000000
    },
    {   // 7  RH UPTO
        0b00000000,
        0b01111110,
        0b00000010,
        0b00000100,
        0b00001000,
        0b00010000,
        0b00100000,
        0b00000000
    },
    {   // 8
        0b00000000,
        0b00111100,
        0b01000010,
        0b00111100,
        0b01000010,
        0b01000010,
        0b00111100,
        0b00000000
    },
    {   // 9
        0b00000000,
        0b00111110,
        0b01000010,
        0b01000010,
        0b00111110,
        0b00000010,
        0b00000010,
        0b00000000
    },
    {   // slash
        0b00000000,
        0b00000010,
        0b00000100,
        0b00001000,
        0b00010000,
        0b00100000,
        0b01000000,
        0b00000000
    }
};


int main(int argc, char** argv)
{

    uint8_t* input_data = (uint8_t*)malloc(MAX_BUF + 1);
    size_t input_length = 0;
    size_t frame_count = 0;
    {
        int read_fd = 0;
        size_t read_size = 1;
        if (argc == 2)
        {
            read_fd = open(argv[1], O_RDONLY);
            if (read_fd == -1)
                return fprintf(stderr, "Could not open file `%s`\n", argv[1]);

            read_size = lseek(read_fd, 0L, SEEK_END);
            lseek(read_fd, 0L, SEEK_SET);

            if (read_size > MAX_BUF)
                return fprintf(stderr, "File size too large\n");
        }

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

        close(read_fd);

        input_data[read_upto] = '\0';
        input_length = read_upto;

        // compute frame number
        frame_count = input_length / QRDATASIZE;
        if (input_length % QRDATASIZE)
            frame_count++;


        printf("input length: %d\nframe count: %d\n", input_length, frame_count);
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
    for (int frame = 0; frame < frame_count; ++frame)
    {
        // GENERATE QR
        uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(QRVERSION)];

        
        size_t start_of_frame = frame * QRDATASIZE;
        size_t end_of_frame = (frame + 1) * QRDATASIZE;
        if (end_of_frame > input_length)
            end_of_frame = input_length;

        // for text mode only we will add and then remove \0 at end of frame
        
        if (TEXT_MODE)
        {        
            uint8_t c = input_data[end_of_frame];
            input_data[end_of_frame] = '\0';

            uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(QRVERSION)];
            //if (!qrcodegen_encodeBinary(data, QRDATASIZE, qrcode, 0, QRVERSION, QRVERSION, -1, 1))
            if (!qrcodegen_encodeText(input_data + start_of_frame, tmp, qrcode, 0, QRVERSION, QRVERSION, -1, 1))
            {
                fprintf(stderr, "failed to generate qr\n");
                return 1;
            } 

            input_data[end_of_frame] = c;
        }
        else
        {
            // the call to generate a qr binary is destructive of the input buffer so copy it first
            // RH TODO: might be unnecessary
            size_t len = end_of_frame - start_of_frame;
            uint8_t data[qrcodegen_BUFFER_LEN_FOR_VERSION(QRVERSION)];

            memcpy(data, input_data + start_of_frame, len);
            if (!qrcodegen_encodeBinary(data, len, qrcode, 0, QRVERSION, QRVERSION, -1, 1))
            {
                fprintf(stderr, "failed to generate qr\n");
                return 1;
            }
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
                                active = false;
                           
                            // frame counter
                            {
                                int digits[5] = {
                                    (frame+1) / 10,
                                    (frame+1) % 10,
                                    10,
                                    frame_count / 10,
                                    frame_count % 10
                                };

                                
                                for (int d = 0; d < 5; ++d)
                                {
                                    int startx = FONT_OFFSET + 8*d;
                                    int starty = FONT_OFFSET;
                                    if (y >= starty && y < starty + 8 && x >= startx && x < startx + 8)
                                        active = (number_font[digits[d]][y-starty]&(1<<(8-(x-startx))));
                                }

                            }

                            //if (y == 0 && x < (frame+1.0)*QRMODULECOUNT/frame_count)
                            //    active = true;


                            sr |= (active ? 0b000 : 0b001) << bc; bc+= 3;
                                
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
