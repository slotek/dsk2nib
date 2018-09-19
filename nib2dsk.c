//
// nib2dsk.c - convert Apple II NIB image file into DSK file
// Copyright (C) 1996, 2017 slotek@nym.hush.com
//
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/********** Symbolic Constants **********/
#define DEBUG

#define VERSION_MAJOR       1
#define VERSION_MINOR       0

#define TRACKS_PER_DISK     35
#define SECTORS_PER_TRACK   16
#define BYTES_PER_SECTOR    256
#define BYTES_PER_TRACK     4096
#define DSK_LEN             143360L

#define PRIMARY_BUF_LEN     256
#define SECONDARY_BUF_LEN   86
#define DATA_LEN            (PRIMARY_BUF_LEN+SECONDARY_BUF_LEN)

/********** Typedefs **********/
typedef unsigned char uchar;

/********** Statics **********/
static char *ueof = "Unexpected End of File";
static uchar addr_prolog[] = { 0xd5, 0xaa, 0x96 };
static uchar addr_epilog[] = { 0xde, 0xaa, 0xeb };
static uchar data_prolog[] = { 0xd5, 0xaa, 0xad };
static uchar data_epilog[] = { 0xde, 0xaa, 0xeb };
static int interleave[ SECTORS_PER_TRACK ] =
    { 0, 7, 0xE, 6, 0xD, 5, 0xC, 4, 0xB, 3, 0xA, 2, 9, 1, 8, 0xF };

/********** Globals **********/
int infd, outfd;
uchar sector, track, volume;
uchar primary_buf[ PRIMARY_BUF_LEN ];
uchar secondary_buf[ SECONDARY_BUF_LEN ];
uchar *dsk_buf[ TRACKS_PER_DISK ];

/********** Prototypes **********/
void convert_image( void );
void process_data( uchar byte );
uchar odd_even_decode( uchar byte1, uchar byte2 );
uchar untranslate( uchar x );
int get_byte( uchar *byte );
void dsk_init( void );
void dsk_reset( void );
void dsk_write( void );
void usage( char *path );
void myprintf( char *format, ... );
void fatal( char *format, ... );

int main( int argc, char **argv )
{
    int i;

    printf( "Apple II NIB to DSK Image Converter Version %d.%d\n\n",
        VERSION_MAJOR, VERSION_MINOR );

    //
    // Check args
    //
    if ( argc != 3 )
        usage( argv[ 0 ] );

    //
    // Init dsk_buf and open files
    //
    dsk_init();

    if ( ( infd = open( argv[ 1 ], O_RDONLY ) ) == -1 )
        fatal( "cannot open %s for reading", argv[ 1 ] );

    if ( ( outfd = open( argv[ 2 ], O_RDWR | O_CREAT | O_TRUNC,
        S_IREAD | S_IWRITE ) ) == -1 )
            fatal( "cannot open %s for writing", argv[ 2 ] );

    //
    // Do conversion and write DSK file
    //
    printf( "Converting %s => %s\n", argv[ 1 ], argv[ 2 ] );
    convert_image();
    dsk_write();

    //
    // Close files & free dsk
    //
    close( infd );
    close( outfd );
    dsk_reset();

    return 0;
}

//
// Convert NIB image into DSK image
//
#define STATE_INIT  0
#define STATE_DONE  666
void convert_image( void )
{
    int state;
    int addr_prolog_index, addr_epilog_index;
    int data_prolog_index, data_epilog_index;
    uchar byte;

    //
    // Image conversion FSM
    //
    if ( get_byte( &byte ) == 0 )
        fatal( ueof );

    for ( state = STATE_INIT; state != STATE_DONE; ) {

        switch( state ) {

            //
            // Scan for 1st addr prolog byte (skip gap bytes)
            //
            case 0:
                addr_prolog_index = 0;
                if ( byte == addr_prolog[ addr_prolog_index ] ) {
                    ++addr_prolog_index;
                    ++state;
                }
                if ( get_byte( &byte ) == 0 )
                    state = STATE_DONE;
                break;

            //
            // Accept 2nd and 3rd addr prolog bytes
            //
            case 1:
            case 2:
                if ( byte == addr_prolog[ addr_prolog_index ] ) {
                    ++addr_prolog_index;
                    ++state;
                    if ( get_byte( &byte ) == 0 )
                        fatal( ueof );
                } else
                    state = 0;
                break;

            //
            // Read and decode volume number
            //
            case 3:
            {
                uchar byte2;
                if ( get_byte( &byte2 ) == 0 )
                    fatal( ueof );
                volume = odd_even_decode( byte, byte2 );
                myprintf( "V:%02x ", volume );
                myprintf( "{%02x%02x} ", byte, byte2 );
                ++state;
                if ( get_byte( &byte ) == 0 )
                    fatal( ueof );
                break;
            }

            //
            // Read and decode track number
            //
            case 4:
            {
                uchar byte2;
                if ( get_byte( &byte2 ) == 0 )
                    fatal( ueof );
                track = odd_even_decode( byte, byte2 );
                myprintf( "T:%02x ", track );
                myprintf( "{%02x%02x} ", byte, byte2 );
                ++state;
                if ( get_byte( &byte ) == 0 )
                    fatal( ueof );
                break;
            }

            //
            // Read and decode sector number
            //
            case 5:
            {
                uchar byte2;
                if ( get_byte( &byte2 ) == 0 )
                    fatal( ueof );
                sector = odd_even_decode( byte, byte2 );
                myprintf( "S:%02x ", sector );
                myprintf( "{%02x%02x} ", byte, byte2 );
                ++state;
                if ( get_byte( &byte ) == 0 )
                    fatal( ueof );
                break;
            }

            //
            // Read and decode addr field checksum
            //
            case 6:
            {
                uchar byte2, csum;
                if ( get_byte( &byte2 ) == 0 )
                    fatal( ueof );
                csum = odd_even_decode( byte, byte2 );
                myprintf( "C:%02x ", csum );
                myprintf( "{%02x%02x} - ", byte, byte2 );
                ++state;
                if ( get_byte( &byte ) == 0 )
                    fatal( ueof );
                break;
            }

            //
            // Accept 1st addr epilog byte
            //
            case 7:
                addr_epilog_index = 0;
                if ( byte == addr_epilog[ addr_epilog_index ] ) {
                    ++addr_epilog_index;
                    ++state;
                    if ( get_byte( &byte ) == 0 )
                        fatal( ueof );
                } else {
                    myprintf( "Reset!\n" );
                    state = 0;
                }
                break;

            //
            // Accept 2nd addr epilog byte
            //
            case 8:
                if ( byte == addr_epilog[ addr_epilog_index ] ) {
                    ++state;
                    if ( get_byte( &byte ) == 0 )
                        fatal( ueof );
                } else {
                    myprintf( "Reset!\n" );
                    state = 0;
                }
                break;

            //
            // Scan for 1st data prolog byte (skip gap bytes)
            //
            case 9:
                data_prolog_index = 0;
                if ( byte == data_prolog[ data_prolog_index ] ) {
                    ++data_prolog_index;
                    ++state;
                }
                if ( get_byte( &byte ) == 0 )
                    fatal( ueof );
                break;

            //
            // Accept 2nd and 3rd data prolog bytes
            //
            case 10:
            case 11:
                if ( byte == data_prolog[ data_prolog_index ] ) {
                    ++data_prolog_index;
                    ++state;
                    if ( get_byte( &byte ) == 0 )
                        fatal( ueof );
                } else
                    state = 9;
                break;

            //
            // Process data
            //
            case 12:
                process_data( byte );
                myprintf( "OK!\n" );
                ++state;
                if ( get_byte( &byte ) == 0 )
                    fatal( ueof );
                break;

            //
            // Scan(!) for 1st data epilog byte
            //
            case 13:
            {
                static int extra = 0;
                data_epilog_index = 0;
                if ( byte == data_epilog[ data_epilog_index ] ) {
                    if ( extra ) {
                        printf( "Warning: %d extra bytes before data epilog\n",
                            extra );
                        extra = 0;
                    }
                    ++data_epilog_index;
                    ++state;
                } else
                    ++extra;
                if ( get_byte( &byte ) == 0 )
                    fatal( ueof );
                break;
            }

            //
            // Accept 2nd data epilog byte
            //
            case 14:
                if ( byte == data_epilog[ data_epilog_index ] ) {
                    ++data_epilog_index;
                    ++state;
                    if ( get_byte( &byte ) == 0 )
                        fatal( ueof );
                } else
                    fatal( "data epilog mismatch (%02x)\n", byte );
                break;

            //
            // Accept 3rd data epilog byte
            //
            case 15:
                if ( byte == data_epilog[ data_epilog_index ] ) {
                    if ( get_byte( &byte ) == 0 )
                        state = STATE_DONE;
                    else
                        state = 0;
                } else
                    fatal( "data epilog mismatch (%02x)\n", byte );
                break;

            default:
                fatal( "Undefined state!" );
                break;
        }
    }
}

//
// Convert 343 6+2 encoded bytes into 256 data bytes and 1 checksum
//
void process_data( uchar byte )
{
    int i, sec;
    uchar checksum;
    uchar bit0, bit1;

    //
    // Fill primary and secondary buffers according to iterative formula:
    //    buf[0] = trans(byte[0])
    //    buf[1] = trans(byte[1]) ^ buf[0]
    //    buf[n] = trans(byte[n]) ^ buf[n-1]
    //
    checksum = untranslate( byte );
    secondary_buf[ 0 ] = checksum;

    for ( i = 1; i < SECONDARY_BUF_LEN; i++ ) {
        if ( get_byte( &byte ) == 0 )
            fatal( ueof );
        checksum ^= untranslate( byte );
        secondary_buf[ i ] = checksum;
    }

    for ( i = 0; i < PRIMARY_BUF_LEN; i++ ) {
        if ( get_byte( &byte ) == 0 )
            fatal( ueof );
        checksum ^= untranslate( byte );
        primary_buf[ i ] = checksum;
    }

    //
    // Validate resultant checksum
    //
    if ( get_byte( &byte ) == 0 )
        fatal( ueof );
    checksum ^= untranslate( byte );
    if ( checksum != 0 )
        printf( "Warning: data checksum mismatch\n" );

    //
    // Denibbilize
    //
    for ( i = 0; i < PRIMARY_BUF_LEN; i++ ) {
        int index = i % SECONDARY_BUF_LEN;

        switch( i / SECONDARY_BUF_LEN ) {
            case 0:
                bit0 = ( secondary_buf[ index ] & 2 ) > 0;
                bit1 = ( secondary_buf[ index ] & 1 ) > 0;
                break;
            case 1:
                bit0 = ( secondary_buf[ index ] & 8 ) > 0;
                bit1 = ( secondary_buf[ index ] & 4 ) > 0;
                break;
            case 2:
                bit0 = ( secondary_buf[ index ] & 0x20 ) > 0;
                bit1 = ( secondary_buf[ index ] & 0x10 ) > 0;
                break;
            default:
                fatal( "huh?" );
                break;
        }

        sec = interleave[ sector ];

        *( dsk_buf[ track ] + (sec*BYTES_PER_SECTOR) + i )
            = ( primary_buf[ i ] << 2 ) | ( bit1 << 1 ) | bit0;
    }
}

//
// decode 2 "4 and 4" bytes into 1 byte
//
uchar odd_even_decode( uchar byte1, uchar byte2 )
{
    uchar byte;

    byte = ( byte1 << 1 ) & 0xaa;
    byte |= byte2 & 0x55;

    return byte;
}

//
// do "6 and 2" un-translation
//
#define TABLE_SIZE 0x40
static uchar table[ TABLE_SIZE ] = {
    0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
    0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
    0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
    0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
    0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
    0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
    0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};
uchar untranslate( uchar x )
{
    uchar y;
    uchar *ptr;
    int index;

    if ( ( ptr = memchr( table, x, TABLE_SIZE ) ) == NULL )
        fatal( "Non-translatable byte %02x\n", x );

    index = ptr - table;

    return index;
}

//
// Read byte from input file
// Returns 0 on EOF
//
//HACK #define BUFLEN 16384
#define BUFLEN 232960
static uchar buf[ BUFLEN ];
int get_byte( uchar *byte )
{
    static int index = BUFLEN;
    static int buflen = BUFLEN;

    myprintf("(%d)", index);

    if ( index >= buflen ) {
        if ( ( buflen = read( infd, buf, BUFLEN ) ) == -1 )
            fatal( "read error" );
        index = 0;
    }

    *byte = buf[ index++ ];
    return buflen;
}

//
// Alloc dsk_buf
//
void dsk_init( void )
{
    int i;
    for ( i = 0; i < TRACKS_PER_DISK; i++ )
        if ( ( dsk_buf[ i ] = (uchar *) malloc( BYTES_PER_TRACK ) ) == NULL )
            fatal( "cannot allocate %ld bytes", DSK_LEN );
}

//
// Free dsk_buf
//
void dsk_reset( void )
{
    int i;
    for ( i = 0; i < TRACKS_PER_DISK; i++ )
        free( dsk_buf[ i ] );
}

//
// Write DSK file
//
void dsk_write( void )
{
    int i;
    for ( i = 0; i < TRACKS_PER_DISK; i++ )
        if ( write( outfd, dsk_buf[ i ], BYTES_PER_TRACK ) != BYTES_PER_TRACK )
            fatal( "write failure" );
}

//
// usage info
//
void usage( char *path )
{
    printf( "Usage: %s <nibfile> <dskfile>\n", path );
    printf( "Where: <nibfile> is the input NIB file name\n" );
    printf( "       <dskfile> is the output DSK file name\n" );

    exit( 1 );
}

//
// myprintf
//
#pragma argsused
void myprintf( char *format, ... )
{
#ifdef DEBUG
    va_list argp;
    va_start( argp, format );
    vprintf( format, argp );
    va_end( argp );
#endif
}

//
// fatal
//
void fatal( char *format, ... )
{
    va_list argp;

    printf( "\nFatal: " );

    va_start( argp, format );
    vprintf( format, argp );
    va_end( argp );

    printf( "\n" );

    exit( 1 );
}
