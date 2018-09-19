//
// dsk2nib.c - convert Apple II DSK image file format into NIB file
// Copyright (C) 1996, 2017 slotek@nym.hush.com
//
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/********** symbolic constants **********/
#define VERSION_MAJOR       1
#define VERSION_MINOR       1

#define TRACKS_PER_DISK     35
#define SECTORS_PER_TRACK   16
#define BYTES_PER_SECTOR    256
#define BYTES_PER_TRACK     4096
#define DSK_LEN             143360L

#define PRIMARY_BUF_LEN     256
#define SECONDARY_BUF_LEN   86
#define DATA_LEN            (PRIMARY_BUF_LEN+SECONDARY_BUF_LEN)

#define PROLOG_LEN          3
#define EPILOG_LEN          3
#define GAP1_LEN            48
#define GAP2_LEN            5

#define BYTES_PER_NIB_SECTOR 416
#define BYTES_PER_NIB_TRACK  6656

#define DEFAULT_VOLUME      254
#define GAP_BYTE            0xff

/********** typedefs **********/
typedef unsigned char uchar;

typedef struct {
    uchar prolog[ PROLOG_LEN ];
    uchar volume[ 2 ];
    uchar track[ 2 ];
    uchar sector[ 2 ];
    uchar checksum[ 2 ];
    uchar epilog[ EPILOG_LEN ];
} addr_t;

typedef struct {
    uchar prolog[ PROLOG_LEN ];
    uchar data[ DATA_LEN ];
    uchar data_checksum;
    uchar epilog[ EPILOG_LEN ];
} data_t;

typedef struct {
    uchar gap1[ GAP1_LEN ];
    addr_t addr;
    uchar gap2[ GAP2_LEN ];
    data_t data;
} nib_sector_t;
    
/********** statics **********/
static uchar addr_prolog[] = { 0xd5, 0xaa, 0x96 };
static uchar addr_epilog[] = { 0xde, 0xaa, 0xeb };
static uchar data_prolog[] = { 0xd5, 0xaa, 0xad };
static uchar data_epilog[] = { 0xde, 0xaa, 0xeb };
static int soft_interleave[ SECTORS_PER_TRACK ] =
    { 0, 7, 0xE, 6, 0xD, 5, 0xC, 4, 0xB, 3, 0xA, 2, 9, 1, 8, 0xF };
static int phys_interleave[ SECTORS_PER_TRACK ] =
    { 0, 0xD, 0xB, 9, 7, 5, 3, 1, 0xE, 0xC, 0xA, 8, 6, 4, 2, 0xF };

/********** globals **********/
uchar primary_buf[ PRIMARY_BUF_LEN ];
uchar secondary_buf[ SECONDARY_BUF_LEN ];
nib_sector_t nib_sector;

/********** prototypes **********/
void odd_even_encode( uchar a[], int i );
void nibbilize( int track, int sector );
uchar translate( uchar byte );

void dsk_init( void );
void dsk_reset( void );
void dsk_read( char *path );
uchar *dsk_get( int track, int sector );

void nib_init( void );
void nib_reset( void );
void nib_write( char *path );
uchar *nib_get( int track, int sector );

void usage( char *path );
void fatal( char *format, ... );

int main( int argc, char **argv )
{
    int sec, trk, csum;
    int volume = DEFAULT_VOLUME;
    uchar *buf;

    printf( "Apple II DSK to NIB Image Converter Version %d.%d\n\n",
        VERSION_MAJOR, VERSION_MINOR );

    //
    // Check args
    //
    if ( argc < 3 || argc > 4 )
        usage( argv[ 0 ] );
    if ( argc == 4 ) {
        volume = atoi( argv[ 3 ] );
        if ( volume < 0 || volume > 255 )
            usage( argv[ 0 ] );
    }

    printf( "Converting %s => %s [Volume:%03d]\n", argv[ 1 ], argv[ 2 ],
        volume );

    //
    // Init DSK and NIB image buffers and read DSK image
    //
    nib_init();
    dsk_init();
    dsk_read( argv[ 1 ] );

    //
    // Init addr & data field marks & volume number
    //
    memcpy( nib_sector.addr.prolog, addr_prolog, 3 );
    memcpy( nib_sector.addr.epilog, addr_epilog, 3 );
    memcpy( nib_sector.data.prolog, data_prolog, 3 );
    memcpy( nib_sector.data.epilog, data_epilog, 3 );
    odd_even_encode( nib_sector.addr.volume, volume );

    //
    // Init gap fields
    //
    memset( nib_sector.gap1, GAP_BYTE, GAP1_LEN );
    memset( nib_sector.gap2, GAP_BYTE, GAP2_LEN );

    //
    // Loop thru DSK tracks
    //
    for ( trk = 0; trk < TRACKS_PER_DISK; trk++ ) {

        //
        // Loop thru DSK sectors
        //
        for ( sec = 0; sec < SECTORS_PER_TRACK; sec++ ) {
            int softsec = soft_interleave[ sec ];
            int physsec = phys_interleave[ sec ];

            //
            // Set ADDR field contents
            //
            csum = volume ^ trk ^ sec;
            odd_even_encode( nib_sector.addr.track, trk );
            odd_even_encode( nib_sector.addr.sector, sec );
            odd_even_encode( nib_sector.addr.checksum, csum );

            //
            // Set DATA field contents (encode sector data)
            //
            nibbilize( trk, softsec );

            //
            // Copy to NIB image buffer
            //
            buf = nib_get( trk, physsec );
            memcpy( buf, &nib_sector, sizeof( nib_sector ) );
        }
    }

    //
    // Write NIB image, free DSK and NIB image buffers, & close output file
    //
    nib_write( argv[ 2 ] );
    dsk_reset();

    return 0;
}

//
// Encode 1 byte into two "4 and 4" bytes
//
void odd_even_encode( uchar a[], int i )
{
    a[ 0 ] = ( i >> 1 ) & 0x55;
    a[ 0 ] |= 0xaa;

    a[ 1 ] = i & 0x55;
    a[ 1 ] |= 0xaa;
}

//
// Convert 256 data bytes into 342 6+2 encoded bytes and a checksum
//
void nibbilize( int track, int sector )
{
    int i, index, section;
    uchar pair;
    uchar *src = dsk_get( track, sector );
    uchar *dest = nib_sector.data.data;

    //
    // Nibbilize data into primary and secondary buffers
    //
    memset( primary_buf, 0, PRIMARY_BUF_LEN );
    memset( secondary_buf, 0, SECONDARY_BUF_LEN );

    for ( i = 0; i < PRIMARY_BUF_LEN; i++ ) {
        primary_buf[ i ] = src[ i ] >> 2;

        index = i % SECONDARY_BUF_LEN;
        section = i / SECONDARY_BUF_LEN;
        pair = ((src[i]&2)>>1) | ((src[i]&1)<<1);       // swap the low bits
        secondary_buf[ index ] |= pair << (section*2);
    }

    //
    // Xor pairs of nibbilized bytes in correct order
    //
    index = 0;
    dest[ index++ ] = translate( secondary_buf[ 0 ] );

    for ( i = 1; i < SECONDARY_BUF_LEN; i++ )
        dest[index++] = translate( secondary_buf[i] ^ secondary_buf[i-1] );

    dest[index++] =
        translate( primary_buf[0] ^ secondary_buf[SECONDARY_BUF_LEN-1] );

    for ( i = 1; i < PRIMARY_BUF_LEN; i++ )
        dest[index++] = translate( primary_buf[i] ^ primary_buf[i-1] );

    nib_sector.data.data_checksum = translate( primary_buf[PRIMARY_BUF_LEN-1] );
}

//
// Do "6 and 2" translation
//
static uchar table[ 0x40 ] = {
    0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
    0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
    0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
    0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
    0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
    0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
    0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};
uchar translate( uchar byte )
{
    return table[ byte & 0x3f ];
}

/************************* DSK Image Routines *************************/

static uchar *dsk_buf[ TRACKS_PER_DISK ];

//
// Alloc DSK image buffer
//
void dsk_init( void )
{
    int i;
    for ( i = 0; i < TRACKS_PER_DISK; i++ )
        if ( ( dsk_buf[ i ] = (uchar *) malloc( BYTES_PER_TRACK ) ) == NULL )
            fatal( "cannot allocate %ld bytes", DSK_LEN );
}

//
// Free DSK image buffer
//
void dsk_reset( void )
{
    int i;
    for ( i = 0; i < TRACKS_PER_DISK; i++ )
        if ( dsk_buf[ i ] )
            free( dsk_buf[ i ] );
}

//
// Read DSK image buffer
//
void dsk_read( char *path )
{
    int i, fd;

    if ( ( fd = open( path, O_RDONLY ) ) == -1 )
        fatal( "cannot open %s for reading", path );

    for ( i = 0; i < TRACKS_PER_DISK; i++ )
        if ( read( fd, dsk_buf[ i ], BYTES_PER_TRACK ) != BYTES_PER_TRACK )
            fatal( "dsk write failure" );

    close( fd );
}

//
// Return pointer to DSK image buffer
//
uchar *dsk_get( int track, int sector )
{
    return dsk_buf[ track ] + sector * BYTES_PER_SECTOR;
}

/************************* NIB Image Routines *************************/

static uchar *nib_buf[ TRACKS_PER_DISK ];

//
// Alloc NIB image buffer
//
void nib_init( void )
{
    int i;
    for ( i = 0; i < TRACKS_PER_DISK; i++ )
        if ( ( nib_buf[ i ] = (uchar *) malloc( BYTES_PER_NIB_TRACK ) ) == NULL )
            fatal( "cannot allocate %d bytes", BYTES_PER_NIB_TRACK );
}

//
// Free NIB image buffer
//
void nib_reset( void )
{
    int i;
    for ( i = 0; i < TRACKS_PER_DISK; i++ )
        if ( nib_buf[ i ] )
            free( nib_buf[i] );
}

//
// Write NIB image buffer to disk
//
void nib_write( char *path )
{
    int i, fd;

    if ( ( fd = open( path, O_RDWR | O_CREAT | O_TRUNC,
        S_IREAD | S_IWRITE ) ) == -1 )
            fatal( "cannot open %s for writing", path );

    for ( i = 0; i < TRACKS_PER_DISK; i++ )
        if ( write( fd, nib_buf[ i ], BYTES_PER_NIB_TRACK ) !=
            BYTES_PER_NIB_TRACK )
                fatal( "nib write error" );

    close( fd );
}

//
// Return pointer to NIB image buffer
//
uchar *nib_get( int track, int sector )
{
    return nib_buf[ track ] + sector * BYTES_PER_NIB_SECTOR;
}

/************************* Utility Routines *************************/

//
// Usage info
//
void usage( char *path )
{
    printf( "Usage: %s <dskfile> <nibfile> [<volume>]\n", path );
    printf( "Where: <dskfile> is the input DSK file name\n" );
    printf( "       <nibfile> is the output NIB file name\n" );
    printf( "       <volume> is an optional volume number from 0 to 255\n" );

    exit( 1 );
}

//
// Fatal
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
