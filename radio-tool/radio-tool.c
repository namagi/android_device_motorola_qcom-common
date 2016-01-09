/* Allow messing with the radio bands and the US gsm lock
 *
 * Copyright (c) 2015 mionica @ xda
 *
 * disclaimer #1: if this breaks your phone, you shouldn't have used it
 * disclaimer #2: if you don't understand what this does, stay away!
 */

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define NV_RADIO_BANDS   1877
#define NV_RADIO_US_LOCK 8322

/*
Clean-room reverse engineered NV item 1877
(obtained by comparing the xt897 and xt907 values)
(and playing with them on xt897/xt907)

2G CDMA   800 [c1]
2G CDMA  1900 [c2]
3G CDMA2000 1xEV-DO [c3]
2G GSM    850 [g1]
2G GSM    900 [g2]
2G GSM   1800 [g3]
2G GSM   1900 [g4]
3G HSDPA  850 [h1]
3G HSDPA  900 [h2]
3G HSDPA 1900 [h3]
3G HSDPA 2100 [h4]
4G LTE   1900 [l1]
4G LTE    700 [l2]

 3322 2222 2222 1111 1111 11             | 6666 5555 5555 5544 4444 4444 3333 3333
 1098 7654 3210 9876 5432 1098 7654 3210 | 3210 9876 5432 1098 7654 3210 9876 5432    
[0000.0100 1110.1000 0100.0001 1000.0111 | 0000.0000 0000.0000 0000.0000 0000.0000] xt897
[0000.0100 1110.1000 0000.0011 1000.0111 | 0000.0000 0000.0010 0000.0000 0000.0000] xt907
       h   hhg  g     l     hg g     ccc |                  l                    
       1   344  1     1     22 3     ??? |                  2                    
                            ??       ??? |                  ?                    
 ----.-U-- UEU-.U--- -S--.--EE E---.-CCC | ----.---- ----.--V- ----.---- ----.----
*/
static const uint32_t bands_cdma[2]   = { 0x00000007, 0x00000000 };
static const uint32_t bands_eu_gsm[2] = { 0x00400380, 0x00000000 };
static const uint32_t bands_us_gsm[2] = { 0x04A80000, 0x00000000 };
static const uint32_t band_spr_lte[2] = { 0x00004000, 0x00000000 };
static const uint32_t band_vzw_lte[2] = { 0x00000000, 0x00020000 };

static const uint32_t known_bands[2]  = { 0x04E84387, 0x00020000 };

static int dbg = 0;

static void whoami();
static void help( const char *whoami );
static int logging_mode( int internal ); // < 0 on fail
static int nv_open(); // <0 on fail
static void nv_close( int fd );
static int nv_read( uint16_t idx, void *buf, int buf_size, int fd ); // <0 on fail
static int nv_write( uint16_t idx, const void *buf, int buf_size, int fd ); // <0 on fail
static void print_bytes( const void *src, int n, int compact );
static void print_band_state( const uint32_t *bands, const uint32_t *ref );
static int bands_changed( const uint32_t *pre, const uint32_t *post, const uint32_t *mask );

int main( int argc, char **argv )
{
  whoami();

  signed char lock_state = -1;
  uint32_t bands_and[2] = { 0xFFFFFFFF, 0xFFFFFFFF };
  uint32_t bands_or[2]  = { 0x00000000, 0x00000000 };

  // parse the command line
  int i;
  for( i = 1;  i < argc; ++i ) {
    if( !strcasecmp( argv[i], "dbg" ))
      dbg = 1;
    else if( !strcasecmp( argv[i], "-uslock" ))
      lock_state = 0;
    else if( !strcasecmp( argv[i], "+uslock" ))
      lock_state = 1;
    else if( !strcasecmp( argv[i], "-cdma" ))
      { bands_and[0] &= ~bands_cdma[0];   bands_and[1] &= ~bands_cdma[1];   }
    else if( !strcasecmp( argv[i], "+cdma" ))
      { bands_or[0]  |=  bands_cdma[0];   bands_or[1]  |=  bands_cdma[1];   }
    else if( !strcasecmp( argv[i], "-eugsm" ))
      { bands_and[0] &= ~bands_eu_gsm[0]; bands_and[1] &= ~bands_eu_gsm[1]; }
    else if( !strcasecmp( argv[i], "+eugsm" ))
      { bands_or[0]  |=  bands_eu_gsm[0]; bands_or[1]  |=  bands_eu_gsm[1]; }
    else if( !strcasecmp( argv[i], "-usgsm" ))
      { bands_and[0] &= ~bands_us_gsm[0]; bands_and[1] &= ~bands_us_gsm[1]; }
    else if( !strcasecmp( argv[i], "+usgsm" ))
      { bands_or[0]  |=  bands_us_gsm[0]; bands_or[1]  |=  bands_us_gsm[1]; }
    else if( !strcasecmp( argv[i], "-sprlte" ))
      { bands_and[0] &= ~band_spr_lte[0]; bands_and[1] &= ~band_spr_lte[1]; }
    else if( !strcasecmp( argv[i], "+sprlte" ))
      { bands_or[0]  |=  band_spr_lte[0]; bands_or[1]  |=  band_spr_lte[1]; }
    else if( !strcasecmp( argv[i], "-vzwlte" ))
      { bands_and[0] &= ~band_vzw_lte[0]; bands_and[1] &= ~band_vzw_lte[1]; }
    else if( !strcasecmp( argv[i], "+vzwlte" ))
      { bands_or[0]  |=  band_vzw_lte[0]; bands_or[1]  |=  band_vzw_lte[1]; }
    else {
      help( argv[0] );
      return EXIT_FAILURE;
    }
  }
  
  // enable internal logging mode
  // (whatever that means :D)
  if( logging_mode( 1 ))
    return EXIT_FAILURE;

  // open the diagnostics file
  int fd;
  if(( fd = nv_open() )< 0 )
    goto err;

  // check if the US GSM is locked
  if( dbg )
    printf( "Reading US GSM lock value...\n" );
  char lock_state_pre;
  if( sizeof(lock_state) != nv_read( NV_RADIO_US_LOCK, &lock_state_pre, sizeof(lock_state_pre), fd ))
    goto err;
  if( dbg )
    print_bytes( &lock_state_pre, sizeof(lock_state_pre), 0 );

  // if a lock change was not requested, load existing value
  if( lock_state == -1 )
    lock_state = lock_state_pre;

  // check the enabled radio bands
  if( dbg )
    printf( "Reading radio bands...\n" );
  uint32_t bands_pre[2];
  if( sizeof(bands_pre) != nv_read( NV_RADIO_BANDS, &bands_pre, sizeof(bands_pre), fd ))
    goto err;
  if( dbg ) {
    print_bytes( &bands_pre, sizeof(bands_pre), 0 );
    printf( "\n" );
  }

  // apply the band change operations
  uint32_t bands[2];
  bands[0] =( bands_pre[0] & bands_and[0] )| bands_or[0]; // +flag takes precedence
  bands[1] =( bands_pre[1] & bands_and[1] )| bands_or[1]; // +flag takes precedence

  // print the current status
  printf( "Current radio configuration\n" );
  printf( "          US GSM lock: %s\n", lock_state_pre ? "ON" : "off" );
  printf( "           CDMA bands: " ); print_band_state( bands_pre, bands_cdma );
  printf( "   US Sprint LTE band: " ); print_band_state( bands_pre, band_spr_lte );
  printf( "  US Verizon LTE band: " ); print_band_state( bands_pre, band_vzw_lte );
  printf( "    US GSM/HSPA bands: " ); print_band_state( bands_pre, bands_us_gsm );
  printf( "    EU GSM/HSPA bands: " ); print_band_state( bands_pre, bands_eu_gsm );
  // check for unknown radio bands
  uint32_t weird_bands[2] = { bands[0] & ~known_bands[0], bands[1] & ~known_bands[1] };
  if( weird_bands[0] || weird_bands[1] ) {
    printf( "NOTE: additional (unknown) bands are enabled: " );
    print_bytes( weird_bands, sizeof(weird_bands), 1 );
    printf( "      please report your phone model and the value above to the author.\n" );
  }
  printf( "\n" );

  // if needed, write the unlock flag
  int n = 0;
  if( lock_state != lock_state_pre ) {
    ++n;
    printf( "Writing US GSM lock value...\n" );
    if( nv_write( NV_RADIO_US_LOCK, &lock_state, sizeof(lock_state), fd )< 0 )
      goto err;
    if( dbg )
      printf( "Checking US GSM lock value...\n" );
    char lock_state_new;
    if( sizeof(lock_state) != nv_read( NV_RADIO_US_LOCK, &lock_state_new, sizeof(lock_state_new), fd ))
      goto err;
    if( lock_state != lock_state_new ) {
      printf( "WARNING. Failed to set the US GSM lock value; current value is\n" );
      print_bytes( &lock_state_pre, sizeof(lock_state_pre), 0 );
    }
  }
  
  // if needed, write the new radio bands
  if(( bands[0] != bands_pre[0] )||( bands[1] != bands_pre[1] )) {
    ++n;
    printf( "Writing radio bands...\n" );
    if( nv_write( NV_RADIO_BANDS, &bands, sizeof(bands), fd )< 0 )
      goto err;
    if( dbg )
      printf( "Checking radio bands...\n" );
    uint32_t bands_new[2];
    if( sizeof(bands_new) != nv_read( NV_RADIO_BANDS, &bands_new, sizeof(bands_new), fd ))
      goto err;
    if(( bands[0] != bands_new[0] )||( bands[1] != bands_new[1] )) {
      printf( "WARNING. Failed to set the radio bands; current value is\n" );
      print_bytes( &bands_new, sizeof(bands_new), 0 );
    }
  }
  if( n ) {
    printf( "\n" );
    // print the new status
    printf( "New radio configuration\n" );
    if( lock_state_pre != lock_state )
      printf( "          US GSM lock: %s\n", lock_state ? "ON" : "off" );
    if( bands_changed( bands_pre, bands, bands_cdma ))
      printf( "           CDMA bands: " ), print_band_state( bands, bands_cdma );
    if( bands_changed( bands_pre, bands, band_spr_lte ))
      printf( "   US Sprint LTE band: " ), print_band_state( bands, band_spr_lte );
    if( bands_changed( bands_pre, bands, band_vzw_lte ))
      printf( "  US Verizon LTE band: " ), print_band_state( bands, band_vzw_lte );
    if( bands_changed( bands_pre, bands, bands_us_gsm ))
      printf( "    US GSM/HSPA bands: " ), print_band_state( bands, bands_us_gsm );
    if( bands_changed( bands_pre, bands, bands_eu_gsm ))
      printf( "    EU GSM/HSPA bands: " ), print_band_state( bands, bands_eu_gsm );
    printf( "\n" );
  }

  // close the diagnostics file
  nv_close( fd );

  // disable internal logging mode
  if( logging_mode( 0 ))
    return EXIT_FAILURE;

  printf( "All done.\n" );
  return EXIT_SUCCESS;

err:
  // disable internal logging mode
  logging_mode( 0 );
  return EXIT_FAILURE;
}

void whoami()
{
  printf( "moto_msm8960 radio tool by mionica@xda\n\n" );
}

void help( const char *whoami )
{
  printf( "Use: %s [dbg] [{+|-}flag [...]]\n"
          "Options:\n"
          "  dbg:       enable debug info\n"
          "  {+|-}flag: enable(+) / disable(-) radio option\n"
          "Flags:\n"
          "  uslock: US GSM lockout\n"
          "  cdma:   CDMA/EVDO radio bands\n"
          "  usgsm:  US GSM/HSPA radio bands\n"
          "  eugsm:  EU GSM/HSPA radio bands\n"
          "  sprlte: US Sprint LTE\n"
          "  vzwlte: US Verizon LTE\n",
          whoami );
}

int logging_mode( int internal )
{
  const char *mode;
  if( internal )
    mode = "internal";
  else
    mode = "usb";

  if( dbg )
    printf( "Setting logging_mode to %s... ", mode );
  int fd;
  if( -1 ==( fd = open( "/sys/devices/virtual/diag/diag/logging_mode", O_WRONLY ))) {
    if( dbg )
      printf( "FAILED\n" );
    fprintf( stderr, "ERROR opening sysfs diagnostics logging_mode file\n" );
    fprintf( stderr, "  file=[%s]\n", 
             "/sys/devices/virtual/diag/diag/logging_mode" );
    return -1;
  }

  int len = write( fd, mode, strlen( mode ));
  if( len != (int)strlen( mode )) {
    if( dbg )
      printf( "FAILED\n" );
    fprintf( stderr, "ERROR setting diagnostics logging mode\n" );
    fprintf( stderr, "  file=[%s] wr_val=[%s]\n", 
             "/sys/devices/virtual/diag/diag/logging_mode", mode );
    return -2;
  }
  close( fd );
  if( dbg )
    printf( "ok\n" );
  return 0;
}

static uint16_t crc16( uint8_t *data_p, int length )
{
  uint16_t crc = 0xFFFF;
  int i;

  while( length-- ) {
    crc ^= *data_p++;
    for( i = 0; i < 8; ++i )
      if( crc & 1 )
        crc =( crc >> 1 )^ 0x8408;
      else
        crc = crc >> 1;
  }
  return ~crc;
}

static int diag_rw( int fd, uint8_t *req, int req_len, uint8_t *resp, int *resp_len )
{
  int ret;
  int len;
  unsigned char buf[1024];
  uint16_t crc;
  int i;

  memset( buf, 0, sizeof(buf) );
  memcpy( buf, req, req_len );

  crc = crc16( req, req_len );
  buf[req_len++] = crc & 0xff;
  buf[req_len++] = crc >> 8;

  for( i = 0; i < req_len; ++i ) {
    if(( buf[i] != 0x7d )&&( buf[i] != 0x7e ))
      continue;
    memmove( buf+i+1, buf+i, req_len - i );
    req_len++;
    buf[i++] = 0x7d;
    buf[i] ^= 0x20;
  }

  buf[i] = 0x7e;
  req_len++;

  do {
    errno = 0;
    ret = write( fd, buf, req_len );
  } while((ret != req_len )&&( errno == EAGAIN ));
  if( ret != req_len )
    return perror( "write" ), 0;

  len = read( fd, resp, *resp_len );
  if( len == -1 )
    return perror( "read" ), 0;

  for( i = 0; i < len; ++i ) {
    if( resp[i] == 0x7e ) {
      /* Should actually check CRC here... */
      len = i - 2;
      break;
    }
    if( resp[i] != 0x7d )
      continue;
    memmove( resp + i, resp + i + 1, len -( i + 1 ));
    resp[i] ^= 0x20;
    len--;
  }
  *resp_len = len;
  return 1;
}

// open the diagnostics device
int nv_open()
{
  if( dbg )
    printf( "Opening diagnostic file... " );
  int fd = open( "/dev/diag_tty", O_RDWR );
  if(( fd == -1 )&&( errno == ENOENT )) {
    if( dbg ) {
      printf( "not found\n" );
      printf( "Creating diagnostic file... " );
    }
    dev_t dev;
    dev = makedev( 185, 0 );
    if( -1 == mknod( "/dev/diag_tty", S_IFCHR | 0600, dev )) {
      if( dbg )
        printf( "FAILED\n" );
      fprintf( stderr, "ERROR creating the diagnostics device\n" );
      fprintf( stderr, "  file=[%s] type=%c(%d,%d)\n", 
               "/dev/diag_tty", 'c', 185, 0 );
      return -1;
    }
    else {
      if( dbg ) {
        printf( "ok\n" );
        printf( "Opening diagnostic file... " );
      }
    }
    fd = open("/dev/diag_tty", O_RDWR);
  }
  if( fd == -1 ) {
    if( dbg )
      printf( "FAILED\n" );
    fprintf( stderr, "ERROR opening the diagnostics device\n" );
    fprintf( stderr, "  file=[%s]\n", 
             "/dev/diag_tty" );
    return -2;
  }
  if( dbg )
    printf( "ok\n" );
  return fd;
}

// close the diagnostics device
void nv_close( int fd )
{
  close( fd );
}

static uint8_t opbuf[3+16*8+2];
static uint8_t rbuf[512];

// read an NV item
// return: number of bytes written to the buffer
// (negative on error)
int nv_read( uint16_t idx, void *buf, int buf_size, int fd )
{
  // adjust the read length
  if( buf_size <= 0 ) {
    fprintf( stderr, "Warning: attempted to read %d bytes from NV item %d\n", 
             buf_size, idx );
    return 0;
  }

  // form the NV read command
  memset( opbuf, 0, sizeof(opbuf) );

  // read command
  opbuf[0] = 0x26;

  // NV item index
  opbuf[1] = idx & 0xff;
  opbuf[2] = idx >> 8;

  // perform the read
  int len;
  len = sizeof(rbuf);
  if( !diag_rw( fd, opbuf, sizeof(opbuf), rbuf, &len )) {
    fprintf( stderr, "ERROR reading NV item %d\n", idx );
    return -1;
  }
  if( len != sizeof(opbuf) ) {
    fprintf( stderr, "ERROR reading NV item %d; ", 
             idx );
    fprintf( stderr, "read: %d bytes, expected: %d bytes\n", 
             len, sizeof(opbuf) );
    return -2;
  }
  
  // only copy back up to sizeof(rbuf) - 3 bytes
  int n = buf_size;
  if( n > (int)sizeof(rbuf) - 3 )
    n = sizeof(rbuf) - 3;
  // copy back the result
  memcpy( buf, &rbuf[3], n );
  // fill the rest of the buffer with 0's
  if( n < buf_size )
    memset( &(((char*)buf)[n]), 0, buf_size - n );

  return n;
}

// write an NV item
// return: number of bytes written to the device (usually > buf_size)
// (negative on error)
int nv_write( uint16_t idx, const void *buf, int buf_size, int fd )
{
  if( buf_size <= 0 ) {
    fprintf( stderr, "Warning: attempted to write %d bytes to NV item %d\n", 
             buf_size, idx );
    return 0;
  }
  // copy buf_size bytes to opbuf+3
  if( buf_size > (int)sizeof(opbuf) - 3 ) {
    fprintf( stderr, "Error: attempted to write %d bytes to NV item %d (max=%d)\n", 
             buf_size, idx, sizeof(opbuf) - 3 );
    return -1;
  }

  // form the NV write command
  memset( opbuf, 0, sizeof(opbuf) );

  // write command
  opbuf[0] = 0x27;

  // NV item index
  opbuf[1] = idx & 0xff;
  opbuf[2] = idx >> 8;

  // NV data to be written
  memcpy( &opbuf[3], buf, buf_size );

  // perform the write
  int len;
  len = sizeof( rbuf );
  if( !diag_rw( fd, opbuf, sizeof(opbuf), rbuf, &len )) {
    fprintf( stderr, "ERROR writing NV item %d\n", idx );
    return -2;
  }

  // successfully wrote n bytes
  return len - 3;
}

// print n bytes
void print_bytes( const void *src, int n, int compact )
{
  const uint8_t *s = (const uint8_t*)src;
  printf( compact?"[":"  [" );
  int i;
  for( i = 0; i < n; ++i )
    printf( compact?"%02X":" %02X", *s++ );
  printf( compact?"]\n":" ]\n" );
}

// print whether a (set of) radio bands is enabled
void print_band_state( const uint32_t *bands, const uint32_t *ref )
{
  uint32_t x[2] = { bands[0] & ref[0], bands[1] & ref[1] };
  if(( x[0] == 0 )&&( x[1] == 0 ))
    printf( "disabled\n" );
  else if(( x[0] == ref[0] )&&( x[1] == ref[1] ))
    printf( "enabled\n" );
  else {
    x[0] = ref[0] & ~bands[0];
    x[1] = ref[1] & ~bands[1];
    printf( "PARTIAL; missing=" );
    print_bytes( x, sizeof(x), 1 );
  }
}

// check if the masked bands have changed
int bands_changed( const uint32_t *pre, const uint32_t *post, const uint32_t *mask )
{
  return((( pre[0] & mask[0] )^( post[0] & mask[0] ))|(( pre[1] & mask[1] )^( post[1] & mask[1] )));
}

