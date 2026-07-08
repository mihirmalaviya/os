#include "psf.h"

/* import the font contained in the object file created above */
extern char _binary_font_psf_start;
extern char _binary_font_psf_end;

uint16_t *unicode;

void psf_init()
{
    uint16_t glyph = 0;
    /* cast the address to PSF header struct */
    PSF_font *font = (PSF_font*)&_binary_font_psf_start;
    /* is there a unicode table? */
    if (font->flags == 0) {
        unicode = NULL;
        return;
    }

    /* get the offset of the table */
    char *s = (char *)(
    (unsigned char*)&_binary_font_psf_start +
      font->headersize +
      font->numglyph * font->bytesperglyph
    );

    /* allocate memory for translation table */
    unicode = NULL;
    // unicode = calloc(USHRT_MAX, 2);
    // while(s < (unsigned char*)&_binary_font_psf_end) {
    //     uint16_t uc = (uint16_t)((unsigned char *)s[0]);
    //     if(uc == 0xFF) {
    //         glyph++;
    //         s++;
    //         continue;
    //     } else if(uc & 128) {
    //         /* UTF-8 to unicode */
    //         if((uc & 32) == 0 ) {
    //             uc = ((s[0] & 0x1F)<<6)+(s[1] & 0x3F);
    //             s++;
    //         } else
    //         if((uc & 16) == 0 ) {
    //             uc = ((((s[0] & 0xF)<<6)+(s[1] & 0x3F))<<6)+(s[2] & 0x3F);
    //             s+=2;
    //         } else
    //         if((uc & 8) == 0 ) {
    //             uc = ((((((s[0] & 0x7)<<6)+(s[1] & 0x3F))<<6)+(s[2] & 0x3F))<<6)+(s[3] & 0x3F);
    //             s+=3;
    //         } else
    //             uc = 0;
    //     }
    //     /* save translation */
    //     unicode[uc] = glyph;
    //     s++;
    // }
}

/* the linear framebuffer */
extern char *fb;
/* number of bytes in each line, it's possible it's not screen width * bytesperpixel! */
extern int scanline;
/* import the font contained in the object file created above */
extern char _binary_font_start[];

#define PIXEL uint32_t   /* pixel pointer */

void putchar(
    /* note that this is int, not char as it's a unicode character */
    unsigned short int c,
    /* cursor position on screen, in characters not in pixels */
    int cx, int cy,
    /* foreground and background colors, say 0xFFFFFF and 0x000000 */
    uint32_t fg, uint32_t bg)
{
    /* cast the address to PSF header struct */
    PSF_font *font = (PSF_font*)&_binary_font_psf_start;
    /* unicode translation */
    if(unicode != NULL) {
        c = unicode[c];
    }
    /* get the glyph for the character. If there's no
       glyph for a given character, we'll display the first glyph. */
    unsigned char *glyph =
     (unsigned char*)&_binary_font_psf_start +
     font->headersize +
     (c>0&&c<font->numglyph?c:0)*font->bytesperglyph;
    /* calculate the upper left corner on screen where we want to display.
       we only do this once, and adjust the offset later. This is faster. */
    int offs =
        (cy * font->height * scanline) +
        (cx * (font->width + 1) * sizeof(PIXEL));

    /* Calculate the number of bytes for a line in a glyph. If the
    glyph width isn't byte aligned, then it is rounded up to be byte aligned. */
    uint32_t bytesPerGlyphLine = (font->width + 7) / 8;
    /* finally display pixels according to the bitmap */
    int x, y, line;
    for(y = 0; y < font->height; y++){
        /* save the starting position of the line */
        line = offs;
        /* Calulate where the first byte for this line of the glyph is. */
        unsigned char* currentByte = glyph + (bytesPerGlyphLine * y);
        /* Start with a mask at this byte's MSB */
        uint8_t mask = 1<< 7;
        /* display a row */
        for(x = 0; x <font->width; x++){
            *((PIXEL*)(fb + line)) = (*currentByte & mask) ? fg : bg;
            mask >>= 1;
            if (mask == 0){
                /* We have read this byte of the glyph.
                Reset mask and move to next byte */
                mask = 1<<7;
                currentByte += 1;
            }
            /* adjust to the next pixel in framebuffer */
            line += sizeof(PIXEL);
        }
        /* adjust to the next line in framebuffer */
        offs  += scanline;
    }
}

int psf_glyph_height(void) {
    PSF_font *font = (PSF_font*)&_binary_font_psf_start;
    return font->height;
}

int psf_glyph_width(void) {
    PSF_font *font = (PSF_font*)&_binary_font_psf_start;
    return font->width;
}
