#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
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

uint16_t image_width = 177U;
uint16_t image_height = 177U;

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
        // logical screen width
        b[u++] = image_width & 0xFFU; b[u++] = image_width >> 8U;

        // logical screen height
        b[u++] = image_height & 0xFFU; b[u++] = image_height >> 8U;
        
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
        b[u++] = 0x00;   b[u++] = 0xFF;   b[u++] = 0x0;
        b[u++] = 0xFFU;  b[u++] = 0x00U; b[u++] = 0x00U;
        
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
    // GRAPHIC CONTROL EXTENSION
    {
        // graphic control extension for frame
        b[u++] = 0x21U; b[u++] = 0xF9U;

        // subblock length
        b[u++] = 0x04U;

        // disposal method = 1, no user input, no transparent colour
        b[u++] = 0b00000100;

        // number of 1/100ths of a second to wait
        b[u++] = 50U;   b[u++] = 0x0; 
        
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
        b[u++] = 0x0;   b[u++] = 0x0;

        // top position
        b[u++] = 0x0;   b[u++] = 0x0;
        
        // width
        b[u++] = image_width & 0xFFU; b[u++] = image_width >> 8U;

        // height
        b[u++] = image_height & 0xFFU; b[u++] = image_height >> 8U;

        // packed fields
        b[u++] = 0x0;   // no flags active

        
    }

    // IMAGE DATA
    {
        // LZW Minimum code size
        b[u++] = 2U; // minimum code size = 2 even for monochrome

        int64_t pixel_count = image_width * image_height;

        // we can fit exactly 680 code words in a 255 byte block
        // which means we can fit exactly 678 pixels since there is a start and end code

#define PIXELS_PER_BLOCK 450

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

            printf("\nBlock size: %02x\n\t", bytes_remaining_in_block);

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

                        sr |= ((pixel_count % 2 == 0 ? 0b000 : 0b001) << bc); bc += 3; 
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
                printf("%02X ", byte_out);
                b[u++] = byte_out;

                sr >>= 8U;
                bc -= 8;
            }
        
        }

        // block terminator
        b[u++] = 0x0;

        
    }

    // TRAILER
    {
        b[u++] = 0x3BU;

    }

    printf("\n");

    int fd = open("./out.gif",  O_WRONLY | O_CREAT | O_TRUNC);
    write(fd, b, u); 
    close(fd); 
    free(b);
}
