//
// sidd.c:  A VLF signal monitor for recording sudden ionospheric disturbances
//
// author: Paul Nicholson, sid0807@abelian.org
// contributions from: Peter Schnoor, DF3LP
//

///////////////////////////////////////////////////////////////////////////////
//  Tuneable Settings                                                        //
///////////////////////////////////////////////////////////////////////////////

//
//  Max number of bands which can be read from the config file.
#define MAXBANDS 40

//
//  Name of the default configuration file.  Override with -c option
#define CONFIG_FILE "/etc/sidd.conf"

//
//  End of tuneable definitions.
//

//////////////////////////////////////////////////////////////////////////////
//  C Headers                                                               //
//////////////////////////////////////////////////////////////////////////////

#include "config.h"

#if !LINUX && !SOLARIS
   #define LINUX 1
#endif
#if !ALSA && !OSS
   #define ALSA 1
#endif
#if SOLARIS && ALSA
   #error ALSA not available for Solaris
#endif

#if HAVE_STDINT_H
   #include <stdint.h>
#endif

#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sched.h>

#include "/usr/include/fftw3.h"

#if LINUX
   #ifndef OPEN_MAX
      #define OPEN_MAX sysconf(_SC_OPEN_MAX)
   #endif
#endif
#if SOLARIS
    #define OPEN_MAX 255
#endif

//
//  Choose the right header file for the prevailing sound system
//

#if SOLARIS && !OSS
   #include <sys/audioio.h>
#endif
#if LINUX && ALSA
   #include <alsa/asoundlib.h>
#endif
#if LINUX && OSS
   #if HAVE__USR_LIB_OSS_INCLUDE_SYS_SOUNDCARD_H
      #include "/usr/lib/oss/include/sys/soundcard.h"
   #else
      #include <linux/soundcard.h>
   #endif
#endif
#if SOLARIS && OSS
   #include <sys/soundcard.h>
#endif

//
//  Set a suitable default device
//

#if LINUX && ALSA
   #define DEVICE "hw:0,0"
#endif
#if OSS
   #define DEVICE "/dev/dsp"
#endif
#if SOLARIS && !OSS
   #define DEVICE "/dev/audio"
#endif

///////////////////////////////////////////////////////////////////////////////
//  Globals and fixed definitions                                            // 
///////////////////////////////////////////////////////////////////////////////
//
//  Variables beginning CF_ are set directly by the configuration file.
//

char *config_file = CONFIG_FILE;

int CF_chans = 1;                                      //  1 = mono, 2 = stereo
int CF_bytes = 2;                                        // Sample width, bytes
int CF_bins = 2048;                                 // Number of frequency bins

int CF_card_delay = 0;                    // Delay seconds before starting work
int CF_output_header = 0;                // Whether to output data file headers
int CF_nread = 2048;             // Number of samples (pairs) to read at a time
char *CF_output_files = "%y%m%d.dat";                // Output file name format
char *CF_timestamp = "%u";               // Format of timestamps in output file
char *CF_field_format = "%.2e";     // Format of power fields in output file
int CF_log_scale = 0;             // Whether to output logarithmic power values

double CF_offset_db = 0;             // Fixed offset to all output power values

int background = 1;                        // Set zero if running in foreground
int VFLAG = 0;                                    //  Set non-zero by -v option

double CF_uspec_secs = 30  ;    // Issue a spectrum for every spec_secs seconds
char *CF_uspec_file = NULL;                     // Filename of utility spectrum 
int uspec_cnt = 0;                        // Frame counter for utility spectrum
int uspec_max = 0;                     // Number of frames per utility spectrum

int CF_sample_rate = 192000;                              // Samples per second

double CF_output_interval = 0;               // Output record interval, seconds
int output_int;                               // Output record interval, frames
int frame_cnt = 0;                          // Frame counter for output records

int CF_priority = 0;                    // Set to 1 if high scheduling priority
struct sigaction sa;

int CF_output_peak = 0;                  // Set to 1 if peak output is required
int CF_output_power = 0;          // Set to 1 if total power output is required

int alert_on = 0;                  // Set when the program actually starts work
char *CF_mailaddr = NULL;                         // Address for alert messages

double CF_los_thresh = 0;                 // Threshold for loss of signal, 0..1
int CF_los_timeout = 0;     // Number of seconds before loss of signal declared

double *hamwin;                    // Array of precomputed hamming coefficients

double DF;                                   // Frequency resolution of the FFT
int bailout_flag = 0;                           // To prevent bailout() looping
int grab_cnt = 0;                       // Count of samples into the FFT buffer

char *logfile = "/var/log/sidd/sidd.log";
char *CF_device = DEVICE;                              // Soundcard device name

char CF_datadir[100] = "/var/lib/sidd/";                       // Directory for output files

#define FFTWID (2 * CF_bins)                  // Number of samples per FT frame

//
//  Independent state variables and buffers for left and right channels
//
struct CHAN
{
   char *name;
   double *sigavg;
   double *powspec;
   double *fft_inbuf;
   fftw_complex *fft_data;
   fftw_plan ffp;
   double peak;
   double sum_sq;
   int los_state;
   time_t los_time;
   char fname[100];
}
 left = { "left" }, right = { "right" };

//
// Table of frequency bands to monitor
//

struct BAND
{
   char ident[50];

   struct CHAN *side;    // Input side to use, left or right
   int start, end;       // Frequency range, Hertz

   FILE *fo;         // Output handle when using BANDS_EACH policy
}
 bands[MAXBANDS];    // Table of bands to be monitored

int nbands = 0;

char out_prefix[100];

//
//  Output policy
//

#define OP_SPECTRUM 1
#define OP_BANDS_MULTI 2
#define OP_BANDS_EACH 3
int CF_output_policy = OP_BANDS_EACH;

// 
// Variables for output policy BANDS_MULTI
//

FILE *bm_fo = NULL;

//
// Variables for output policy BANDS_EACH
//

//
// Variables for output policy SPECTRUM
//

int CF_range1;
int CF_range2;
int cuton;
int cutoff;
FILE *sf_fo;

///////////////////////////////////////////////////////////////////////////////
//  Various Utility Functions                                                //
///////////////////////////////////////////////////////////////////////////////

//
//  Issue a message to the log file, if the verbosity level is high enough...
//

void report( int level, char *format, ...)
{
   va_list ap;
   void bailout( char *format, ...);
   char temp[ 200];

   if( VFLAG < level) return;

   va_start( ap, format);
   vsprintf( temp, format, ap);
   va_end( ap);

   if( !logfile || !background)
      if( background != 2) fprintf( stderr, "%s\n", temp);

   if( logfile)
   {
      time_t now = time( NULL);
      struct tm *tm = gmtime( &now);
      FILE *flog = NULL;
   
      if( (flog = fopen( logfile, "a+")) == NULL)
         bailout( "cannot open logfile [%s]: %s", logfile, strerror( errno));
   
      fprintf( flog, "%04d/%02d/%02d %02d:%02d:%02d %s\n", 
                tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
                tm->tm_hour, tm->tm_min, tm->tm_sec, temp);
      fclose( flog);
   }
}

void alert( char *format, ...)
{
   FILE *f;
   va_list( ap);
   char cmd[100], temp[100];

   va_start( ap, format);
   vsprintf( temp, format, ap);
   va_end( ap);
 
   report( -1, "%s", temp);

   if( !alert_on || !CF_mailaddr) return;

   sprintf( cmd, "mail -s 'sidd alert' '%s'", CF_mailaddr);
   if( (f=popen( cmd, "w")) == NULL)
   {
      report( 0, "cannot exec [%s]: %s", cmd, strerror( errno));
      return;
   }

   fprintf( f, "sidd: %s\n", temp);
   fclose( f);
}

//
//  We try to exit the program through here, if possible.
//  

void bailout( char *format, ...)
{
   va_list ap;
   char temp[ 200];

   if( bailout_flag) exit( 1);
   bailout_flag = 1;
   va_start( ap, format);
   vsprintf( temp, format, ap);
   va_end( ap);

   alert( "terminating: %s", temp);
   exit( 1);
}

//
//  Exit with a message if we get any signals.
//  

void handle_sigs( int signum)
{
   bailout( "got signal %d", signum);
}

void check_los( struct CHAN *c)
{
   if( !c->los_state)
   {
      if( !c->los_time && c->peak < CF_los_thresh) time( &c->los_time);
      if( c->los_time && c->peak > CF_los_thresh) c->los_time = 0;
      if( c->los_time && c->los_time + CF_los_timeout < time( NULL))
      {
         c->los_state = 1;
         c->los_time = 0;
         if( CF_chans == 1) alert( "loss of signal");
         else alert( "loss of signal on %s", c->name);
      }
   }
   else
   {
      if( !c->los_time && c->peak > CF_los_thresh) time( &c->los_time);
      if( c->los_time && c->peak < CF_los_thresh) c->los_time = 0;
      if( c->los_time && c->los_time + CF_los_timeout < time( NULL))
      {
         c->los_state = 0;
         c->los_time = 0;
         if( CF_chans == 1) alert( "signal restored");
         else alert( "signal restored on %s", c->name);
      }
   } 
}

void utility_spectrum( void)
{
   FILE *f;
   int i;

   if( !CF_uspec_file) return;     // Spectrum file not wanted.

   if( (f=fopen( CF_uspec_file, "w+")) == NULL)
      bailout( "cannot open spectrum file %s", strerror( errno));

   if( CF_chans == 1)
      for( i=0; i<CF_bins; i++) fprintf( f, "%.5e %.5e\n", 
             (i+0.5) * DF, left.sigavg[i]/uspec_max);
   else
      for( i=0; i<CF_bins; i++) fprintf( f, "%.5e %.5e %.5e\n", 
             (i+0.5) * DF, left.sigavg[i]/uspec_max,
                          right.sigavg[i]/uspec_max);
   fclose( f);

   for( i=0; i<CF_bins; i++) left.sigavg[i] = 0;
   if( CF_chans == 2) for( i=0; i<CF_bins; i++) right.sigavg[i] = 0;
}

///////////////////////////////////////////////////////////////////////////////
//  Soundcard Setup                                                          //
///////////////////////////////////////////////////////////////////////////////

//
//  Prepare the input stream, setting up the soundcard if the input
//  is a character device.
//

#if OSS

#define soundsystem "OSS"
int capture_handle;

void setup_input_stream( void)
{
   int chans;
   struct stat st;

   report( 1, "taking data from [%s]", CF_device);

   if( (capture_handle = open( CF_device, O_RDONLY | O_EXCL)) < 0)
      bailout( "cannot open [%s]: %s", CF_device, strerror( errno));

   if( fstat( capture_handle, &st) < 0)
      bailout( "cannot stat input stream: %s", strerror( errno));

   if( !S_ISCHR( st.st_mode)) 
      bailout( "%s is not a character device", CF_device);

//   int blksize;
//   int fragreq = 0x7fff000a;

   unsigned int format;
   unsigned int req_format = AFMT_S16_LE;
   switch( CF_bytes)
   {
      case 1: req_format = AFMT_U8;       break;
      case 2: req_format = AFMT_S16_LE;   break;
      #ifdef AFMT_S24_LE
         case 3: req_format = AFMT_S24_LE;   break;
      #endif
      #ifdef AFMT_S32_LE
         case 4: req_format = AFMT_S32_LE;   break;
      #endif
   }

//   if (ioctl( capture_handle, SNDCTL_DSP_SETFRAGMENT, &fragreq))
//      report( 01, "cannot set fragment size");

//   if( ioctl( capture_handle, SNDCTL_DSP_RESET, NULL) < 0)
//      bailout( "cannot reset input device");

   chans = CF_chans;
   if( ioctl( capture_handle, SNDCTL_DSP_CHANNELS, &chans) < 0 ||
       chans != CF_chans) bailout( "cannot set channels on input device");

   if( ioctl( capture_handle, SNDCTL_DSP_GETFMTS, &format) < 0)
      bailout( "cannot get formats from input device");

   report( 2, "formats available: %08X", format);
   if( (format & req_format) == 0)
   {
      report( 0, "available dsp modes: %08X", format);
      bailout( "unable to set %d bit dsp mode", 8 * CF_bytes);
   }

   format = req_format;
   if( ioctl( capture_handle, SNDCTL_DSP_SETFMT, &format) < 0)
      bailout( "cannot set dsp format on input device");

//   if( ioctl( capture_handle, SNDCTL_DSP_GETBLKSIZE, &blksize) < 0)
//      bailout( "cannot get block size from input device");

//   report( 2, "dsp block size: %d", blksize);
//   if( ioctl( capture_handle, SNDCTL_DSP_CHANNELS, &chans) < 0)
//      bailout( "cannot get channels from input device");

   report( 1, "requesting rate: %d", CF_sample_rate);
   if( ioctl( capture_handle, SNDCTL_DSP_SPEED, &CF_sample_rate) < 0)
      bailout( "cannot set sample rate of input device");

   report( 1, "actual rate set: %d samples/sec", CF_sample_rate);
   report( 1, "soundcard channels: %d  bits: %d", chans, 8 * CF_bytes);
}

int read_soundcard( char *buf)
{
   int ne, retry_cnt = 0;
   int nread = CF_nread * CF_chans * CF_bytes;  // Number of bytes to read

   while( (ne = read( capture_handle, buf, nread)) < 0)
   {
      if( ++retry_cnt == 5)
         bailout( "audio read failed, %s", strerror( errno));

      if( !ne || errno == ENOENT || errno == 0) 
      {  
         sched_yield();
         usleep( 10000); 
         continue;
      }

      report( 2, "soundcard read failed: %s, retry %d", 
                         strerror( errno), retry_cnt);
   }

   return ne / (CF_chans * CF_bytes);   // Count of sample (pairs) read
}

#endif // OSS

#if ALSA

#define soundsystem "ALSA"
snd_pcm_t *capture_handle;

void setup_input_stream( void)
{
   int err;
   snd_pcm_hw_params_t *hw_params;

   if( (err = snd_pcm_open( &capture_handle, CF_device, 
                            SND_PCM_STREAM_CAPTURE, 0)) < 0) 
      bailout( "cannot open audio device %s (%s)\n",
         CF_device, snd_strerror( err));

   if( (err = snd_pcm_hw_params_malloc( &hw_params)) < 0 ||
       (err = snd_pcm_hw_params_any( capture_handle, hw_params)) < 0)
      bailout( "cannot init hardware params struct (%s)\n", snd_strerror( err));

   int rate_min, rate_max;
   snd_pcm_hw_params_get_rate_min( hw_params, &rate_min, 0);
   snd_pcm_hw_params_get_rate_max( hw_params, &rate_max, 0);

   report( 1, "rate min %d max %d", rate_min, rate_max);

   if( (err = snd_pcm_hw_params_set_access( capture_handle,
              hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
      bailout("cannot set access type (%s)\n", snd_strerror (err));

   if ((err = snd_pcm_hw_params_set_format(
        capture_handle, hw_params, SND_PCM_FORMAT_S16_LE)) < 0)
      bailout( "cannot set sample format (%s)\n", snd_strerror (err));

   if ((err = snd_pcm_hw_params_set_rate_near(
             capture_handle, hw_params, &CF_sample_rate, 0)) < 0)
      bailout( "cannot set sample rate (%s)\n", snd_strerror (err));

   report( 0, "sample rate set: %d", CF_sample_rate);

   if( (err = snd_pcm_hw_params_set_channels(
              capture_handle, hw_params, CF_chans)) < 0)
      bailout( "cannot set channel count (%s)\n", snd_strerror (err));

  if( (err = snd_pcm_hw_params( capture_handle, hw_params)) < 0)
      bailout( "cannot set parameters (%s)\n", snd_strerror (err));

   snd_pcm_hw_params_free( hw_params);
   if ((err = snd_pcm_prepare( capture_handle)) < 0)
      bailout( "cannot prepare soundcard (%s)", snd_strerror (err));
}

int read_soundcard( char *buf)
{
   int ne, retry_cnt = 0;

   while( (ne = snd_pcm_readi( capture_handle, buf, CF_nread)) < 0)
   {
      if( ++retry_cnt == 5) 
         bailout( "audio read failed, %s", snd_strerror( ne));

      report( 2, "soundcard read failed: %s, retry %d",
             snd_strerror( ne), retry_cnt);
      snd_pcm_prepare( capture_handle);  // Must restart capture
   }

   return ne;   // Number of sample (pairs) read
}

#endif // ALSA

///////////////////////////////////////////////////////////////////////////////
//  Output Functions                                                         //
///////////////////////////////////////////////////////////////////////////////

int substitute_params( char *d, struct timeval *tv, 
                              char *format, char *band)
{
   double fsecs = tv->tv_sec + 1e-6 * tv->tv_usec;
   time_t ud = tv->tv_sec - tv->tv_sec % 86400;
   struct tm *tm = gmtime( &ud);

   while( *format)
   {
      if( *format != '%') { *d++ = *format++; continue; }

      format++;
      switch( *format++)
      {
         case '%': *d++ = '%'; break;

         case 'y': d += sprintf( d, "%02d", tm->tm_year % 100);  break;
         case 'm': d += sprintf( d, "%02d", tm->tm_mon+1); break;
         case 'd': d += sprintf( d, "%02d", tm->tm_mday); break;

         case 'H': d += sprintf( d, "%02d", tm->tm_hour);  break;
         case 'M': d += sprintf( d, "%02d", tm->tm_min);  break;
         case 'S': d += sprintf( d, "%02d", tm->tm_sec);  break;

         case 'B': if( !band) bailout( "cannot specify %B in this mode");
                   d += sprintf( d, "%s", band);   break;
         case 'U': d += sprintf( d, "%ld", tv->tv_sec); break;
         case 'u': d += sprintf( d, "%.3f", fsecs);  break;

         case 'E': d += sprintf( d, "%d", (int)(tv->tv_sec % 86400)); break;
         case 'e': d += sprintf( d, "%.3f", 1e-6 * tv->tv_usec + 
                                            (tv->tv_sec % 86400)); break;
         default: return 0;
      }
   }

   *d = 0;
   return 1;
}

void output_record_multi( struct timeval *tv)
{
   int i, j;
   struct BAND *b;
   char prefix[100], stamp[100], filename[100];

   if( !substitute_params( prefix, tv, CF_output_files, NULL))
      bailout( "error in output_files configuration");

   if( strcmp( prefix, out_prefix))
   {
      strcpy( out_prefix, prefix);

      sprintf( filename, "%s/%s", CF_datadir, out_prefix); 
      report( 0, "using output file [%s]", filename);

      if( bm_fo) fclose( bm_fo);
      if( (bm_fo=fopen( filename, "a+")) == NULL)
         bailout( "cannot open [%s], %s", filename, strerror( errno));

      if( CF_output_header)
      {
         // Header record required.  Output a header only if the file is
         // empty - this may be a restart.
         struct stat st;

         if( stat( filename, &st) < 0) 
            bailout( "cannot stat output file %s: %s", 
               filename, strerror( errno));

         if( !st.st_size)
         {
            fputs( "stamp lpeak rpeak lrms rrms ", bm_fo);
            for( b = bands, i = 0; i < nbands; i++, b++)
               fprintf( bm_fo, "%s ", b->ident);
            fputs( "\n", bm_fo);
         }
      }
   }

   substitute_params( stamp, tv, CF_timestamp, NULL);

   fprintf( bm_fo, "%s %.3f %.3f %.3f %.3f", stamp, left.peak, right.peak,
                 sqrt( left.sum_sq/FFTWID), sqrt( right.sum_sq/FFTWID));

   for( b = bands, i = 0; i < nbands; i++, b++)
   {
      double e = 0;
      int n1 = b->start/DF;
      int n2 = b->end/DF;
      for( j=n1; j<= n2; j++) e += b->side->powspec[j];
      e /= output_int * (n2 - n1 + 1);
      if( CF_log_scale) e = CF_offset_db + 10 * log10( e + 1e-9);
      fputs( " ", bm_fo);
      fprintf( bm_fo, CF_field_format, e);
   }
   fputs( "\n", bm_fo);
   fflush( bm_fo);
}

void output_record_each( struct timeval *tv)
{
   int i, j;
   struct BAND *b;
   char prefix[100], stamp[100], filename[100];

   if( !substitute_params( prefix, tv, CF_output_files, "ID"))
      bailout( "error in output_files configuration");

   if( strcmp( prefix, out_prefix))
   {
      strcpy( out_prefix, prefix);
      report( 0, "using output files [%s]", out_prefix);

      for( b = bands, i = 0; i < nbands; i++, b++)
      {
         if( b->fo) fclose( b->fo);
         substitute_params( prefix, tv, CF_output_files, b->ident);
         sprintf( filename, "%s/%s", CF_datadir, prefix);
         if( (b->fo=fopen( filename, "a")) == NULL)
            bailout( "cannot open [%s], %s", filename, strerror( errno));

         if( CF_output_header)
         {
            // Header record required.  Output a header only if the file is
            // empty - this may be a restart.
            struct stat st;

            if( stat( filename, &st) < 0) 
               bailout( "cannot stat output file %s: %s", 
                  filename, strerror( errno));

            if( !st.st_size) fputs( "stamp power\n", b->fo);
         }
      }
   }

   for( b = bands, i = 0; i < nbands; i++, b++)
   {
      double e = 0;
      int n1 = b->start/DF;
      int n2 = b->end/DF;

      substitute_params( stamp, tv, CF_timestamp, b->ident);

      for( j=n1; j<= n2; j++) e += b->side->powspec[j];
      e /= output_int * (n2 - n1 + 1);
      if( CF_log_scale) e = CF_offset_db + 10 * log10( e + 1e-9);
      fprintf( b->fo, "%s ", stamp);
      fprintf( b->fo, CF_field_format, e);
      fputs( "\n", b->fo);
      fflush( b->fo);
   }
}

void output_spectrum_record( struct timeval *tv)
{
   int i;
   char stamp[100], prefix[100], filename[100];

   if( !substitute_params( prefix, tv, CF_output_files, NULL))
      bailout( "error in output_files configuration");

   if( strcmp( prefix, out_prefix))
   {
      strcpy( out_prefix, prefix);
      sprintf( filename, "%s/%s", CF_datadir, out_prefix); 
      report( 0, "using output file [%s]", filename);

      if( sf_fo) fclose( sf_fo);
      if( (sf_fo = fopen( filename, "a+")) == NULL)
            bailout( "cannot open [%s], %s", filename, strerror( errno));

      if( CF_output_header)
      {
         // Header record required.  Output a header only if the file is
         // empty - this may be a restart.
         struct stat st;

         if( stat( filename, &st) < 0) 
            bailout( "cannot stat output file %s: %s", 
               filename, strerror( errno));

         if( !st.st_size)
         {
            fputs( "FREQ ", sf_fo);
            for( i = cuton; i < cutoff; i++)
               fprintf( sf_fo, "%.2f ", (i+0.5) * DF);
            fputs( "\n", sf_fo);
         }
      }
   }

   substitute_params( stamp, tv, CF_timestamp, NULL);
   fprintf( sf_fo, "%s", stamp);

   for( i=cuton; i<cutoff; i++)
   { 
      double e = left.powspec[i]/output_int;
      if( CF_log_scale) e = CF_offset_db + 10 * log10( e + 1e-9);
      fputs( " ", sf_fo);
      fprintf( sf_fo, CF_field_format, e); 
   }

   fputs( "\n", sf_fo);
   fflush( sf_fo);
}

void output_record( void)
{
   int i;
   struct timeval tv;

   if( CF_chans == 2)
      report( 2, "peak/rms left=%.3f/%.3f right=%.3f/%.3f", 
              left.peak, sqrt( left.sum_sq/(FFTWID * output_int)),
              right.peak, sqrt( right.sum_sq/(FFTWID * output_int)));
   else
      report( 2, "peak/rms %.3f/%.3f", 
              left.peak, sqrt( left.sum_sq/FFTWID));
 
   gettimeofday( &tv, NULL);

   if( CF_output_policy == OP_SPECTRUM) output_spectrum_record( &tv);
   else
   if( CF_output_policy == OP_BANDS_MULTI) output_record_multi( &tv);
   else
   if( CF_output_policy == OP_BANDS_EACH) output_record_each( &tv);

   //
   // Clear down the spectrum and peak/rms accumulators
   //

   for( i=0; i<CF_bins; i++) left.powspec[i] = right.powspec[i] = 0;
   left.peak = left.sum_sq = 0;
   right.peak = right.sum_sq = 0;
}


///////////////////////////////////////////////////////////////////////////////
//  Signal Processing                                                        //
///////////////////////////////////////////////////////////////////////////////

void process_fft( struct CHAN *c)
{
   int i;

   fftw_execute( c->ffp);   // Do the FFT

   //
   //  Obtain squared amplitude of each bin.
   //

   c->powspec[ 0] = 0.0;  // Zero the DC component
   for( i=1; i<CF_bins; i++)
   {
      double t1 = c->fft_data[i][0];  
      double t2 = c->fft_data[i][1]; 
      double f = t1*t1 + t2*t2;
      c->powspec[ i] += f;                    // Accumulator for output records
      c->sigavg[i] += f;                    // Accumulator for utility spectrum
   }

   check_los( c);
}

void setup_hamming_window( void)
{
   int i;

   if( (hamwin = malloc( sizeof( double) * FFTWID)) == NULL)
      bailout( "not enough memory for hamming window");

   for( i=0; i<FFTWID; i++) hamwin[i] = sin( i * M_PI/FFTWID);
}

static inline void insert_sample( struct CHAN *c, double f)
{
   c->sum_sq += f * f;
   if( f > c->peak) c->peak = f;
   if( f < -c->peak) c->peak = -f;

   c->fft_inbuf[grab_cnt] = f * hamwin[grab_cnt];
}

static inline void maybe_do_fft( void)
{
   if( ++grab_cnt < FFTWID) return;
   grab_cnt = 0;

   process_fft( &left);
   if( CF_chans == 2) process_fft( &right);

   if( ++frame_cnt == output_int)
   {
      frame_cnt = 0;
      output_record();
   }

   if( ++uspec_cnt == uspec_max)
   {
      uspec_cnt = 0;
      utility_spectrum(); 
   }
}

//
// Main signal processing loop.  Never returns.
//

void process_signal( void)
{
   char *buff = malloc( CF_nread * CF_chans * CF_bytes);

   if( !buff) bailout( "not enough memory for input buffer");

   while( 1) 
   {
      int i, q;
      double f;

      q = read_soundcard( buff);

      //  Unpack the input buffer and scale to -1..+1 for further processing.
      if( CF_bytes == 1)
      {
         unsigned char *dp = (unsigned char *) buff;

         if( CF_chans == 1)
            for( i=0; i<q; i++)
            {
               f = *dp++;
               insert_sample( &left, (f - 127)/128);
               maybe_do_fft();
            }
         else  // CF_chans == 2
            for( i=0; i<q; i++)
            {
               f = *dp++;
               insert_sample( &left, (f - 127)/128);

               f = *dp++;
               insert_sample( &right, (f - 127)/128);
               maybe_do_fft();
            }
      }
      else
      if( CF_bytes == 2)
      {
         short *dp = (short *) buff;

         if( CF_chans == 1)
            for( i=0; i<q; i++)
            {
               f = *dp++;
               insert_sample( &left, f/32768);
               maybe_do_fft();
            }
         else  // CF_chans == 2
            for( i=0; i<q; i++)
            {
               f = *dp++;
               insert_sample( &left, f/32768);

               f = *dp++;
               insert_sample( &right, f/32768);
               maybe_do_fft();
            }
      }
      else
      if( CF_bytes == 3)
      {
         char *dp = buff;

         if( CF_chans == 1)
            for( i=0; i<q; i++)
            {
               f = (* (int *) dp) & 0xffffff;  dp += 3;
               insert_sample( &left, f/8388608);
               maybe_do_fft();
            }
         else  // CF_chans == 2
            for( i=0; i<q; i++)
            {
               f = (* (int *) dp) & 0xffffff;  dp += 3;
               insert_sample( &left, f/8388608);

               f = (* (int *) dp) & 0xffffff;  dp += 3;
               insert_sample( &right, f/8388608);
               maybe_do_fft();
            }
      }
      else
      if( CF_bytes == 4)
      {
         int *dp = (int *) buff;

         if( CF_chans == 1)
            for( i=0; i<q; i++)
            {
               f = *dp++;
               insert_sample( &left, f/2147483648UL);
               maybe_do_fft();
            }
         else  // CF_chans == 2
            for( i=0; i<q; i++)
            {
               f = *dp++;
               insert_sample( &left, f/2147483648UL);

               f = *dp++;
               insert_sample( &right, f/2147483648UL);
               maybe_do_fft();
            }
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
//  Configuration File Stuff                                                 //
///////////////////////////////////////////////////////////////////////////////

void config_band( char *ident, char *start, char *end, char *side)
{
   struct BAND *b = bands + nbands++;

   if( nbands == MAXBANDS) bailout( "too many bands specified in config file");

   strcpy( b->ident, ident);
   b->start = atoi( start);
   b->end = atoi( end);

   if( !strcasecmp( side, "left")) b->side = &left;
   else
   if( !strcasecmp( side, "right")) b->side = &right;
   else
      bailout( "must specify left or right side for band %s", ident);

   b->fo = NULL;
}

void load_config( void)
{
   int lino = 0, nf;
   FILE *f;
   char buff[100], *p, *fields[20];

   if( (f=fopen( config_file, "r")) == NULL) bailout( "no config file found");

   while( fgets( buff, 99, f))
   {
      lino++;

      if( (p=strchr( buff, '\r')) != NULL) *p = 0;
      if( (p=strchr( buff, '\n')) != NULL) *p = 0;
      if( (p=strchr( buff, ';')) != NULL) *p = 0;

      p = buff;  nf = 0;
      while( 1)
      {
         while( *p && isspace( *p)) p++;
         if( !*p) break;
         fields[nf++] = p;
         while( *p && !isspace( *p)) p++;
         if( *p) *p++ = 0;
      }
      if( !nf) continue;

      if( nf == 2 && !strcasecmp( fields[0], "output_policy"))
      {
         if( !strcasecmp( fields[1], "SPECTRUM"))
            CF_output_policy = OP_SPECTRUM;
         else
         if( !strcasecmp( fields[1], "BANDS_EACH"))
            CF_output_policy = OP_BANDS_EACH;
         else
         if( !strcasecmp( fields[1], "BANDS_MULTI"))
            CF_output_policy = OP_BANDS_MULTI;
         else
            bailout( "unrecognised output policy [%s]", fields[1]);
      }
      else
      if( nf == 2 && !strcasecmp( fields[0], "output_interval"))
      {
         CF_output_interval = atof( fields[1]);   // Seconds
      }
      else
      if( nf == 3 && !strcasecmp( fields[0], "spectrum_range"))
      {
         CF_range1 = atoi( fields[1]);
         CF_range2 = atoi( fields[2]);
      }
      else
      if( nf == 5 && !strcasecmp( fields[0], "band")) 
         config_band( fields[1], fields[2], fields[3], fields[4]);
      else
      if( nf == 2 && !strcasecmp( fields[0], "logfile"))
      {
         if(strlen( logfile))
            logfile = strdup( fields[1]);
         else
            logfile = NULL;
         report( 1, "logfile %s", logfile ? logfile : "(none)");
      }
      else
      if( nf == 3 && !strcasecmp( fields[0], "los"))
      {
         CF_los_thresh = atof( fields[1]);
         CF_los_timeout = atoi( fields[2]);
         report( 1, "los threshold %.3f, timeout %d seconds", 
                    CF_los_thresh, CF_los_timeout);
      }
      else
      if( nf == 2 && !strcasecmp( fields[0], "device"))
         CF_device = strdup( fields[1]);
      else
      if( nf == 2 && !strcasecmp( fields[0], "mode"))
      {
         if( !strcasecmp( fields[1], "mono")) CF_chans = 1;
         else
         if( !strcasecmp( fields[1], "stereo")) CF_chans = 2;
         else
            bailout( "error in config file, line %d", lino);
      }
      else
      if( nf == 2 && !strcasecmp( fields[0], "bits"))
      {
         switch( atoi( fields[1]))
         {
            case 8:  CF_bytes = 1;  break;
            case 16: CF_bytes = 2;  break;
            case 24: CF_bytes = 3;  break;
            case 32: CF_bytes = 4;  break;
            default:
            bailout( "can only do 8,16,24,32  bits, config file line %d", lino);
         }
      }
      else
      if( nf == 2 && !strcasecmp( fields[0], "output_files"))
         CF_output_files = strdup( fields[1]);
      else
      if( nf == 2 && !strcasecmp( fields[0], "timestamp"))
         CF_timestamp = strdup( fields[1]);
      else
      if( nf == 2 && !strcasecmp( fields[0], "output_header"))
      {
         if( !strcasecmp( fields[1], "yes")) CF_output_header = 1;
         else
         if( !strcasecmp( fields[1], "no")) CF_output_header = 0;
         else
            bailout( "expecting yes or no for output_header");
      }
      else
      if( nf == 2 && !strcasecmp( fields[0], "output_power"))
      {
         if( !strcasecmp( fields[1], "yes")) CF_output_power = 1;
         else
         if( !strcasecmp( fields[1], "no")) CF_output_power = 0;
         else
            bailout( "expecting yes or no for output_power");
      }
      else
      if( nf == 2 && !strcasecmp( fields[0], "output_peak"))
      {
         if( !strcasecmp( fields[1], "yes")) CF_output_peak = 1;
         else
         if( !strcasecmp( fields[1], "no")) CF_output_peak = 0;
         else
            bailout( "expecting yes or no for output_peak");
      }
      else
      if( nf == 3 && !strcasecmp( fields[0], "utility_spectrum"))
      {
         CF_uspec_file = strdup( fields[1]);
         CF_uspec_secs = atof( fields[2]);
      }
      else
      if( nf == 2 && !strcasecmp( fields[0], "sched"))
      {
         if( !strcasecmp( fields[1], "high")) CF_priority = 1;
         else
         if( !strcasecmp( fields[1], "low")) CF_priority = 0;
         else
            bailout( "expecting high or low for sched parameter");
      }
      else
      if( nf == 2 && !strcasecmp( fields[0], "rate"))
         CF_sample_rate = atoi( fields[1]);
      else
      if( nf == 2 && !strcasecmp( fields[0], "bins"))
         CF_bins = atoi( fields[1]);
      else
      if( nf == 2 && !strcasecmp( fields[0], "datadir"))
      {
         struct stat st;
         strcpy( CF_datadir, fields[1]);
         if( stat( CF_datadir, &st) < 0 || !S_ISDIR( st.st_mode))
            bailout( "no data directory, %s", CF_datadir);
      }
      else
      if( nf == 2 && !strcasecmp( fields[0], "card_delay"))
         CF_card_delay = atoi( fields[1]);
      else
      if( nf == 2 && !strcasecmp( fields[0], "offset_db"))
         CF_offset_db = atof( fields[1]);
      else
      if( nf == 2 && !strcasecmp( fields[0], "nread"))
         CF_nread = atoi( fields[1]);
      else
      if( nf == 2 && !strcasecmp( fields[0], "field_format"))
         CF_field_format = strdup( fields[1]);
      else
      if( nf == 2 && !strcasecmp( fields[0], "field_scale"))
      {
         if( !strcasecmp( fields[1], "db")) CF_log_scale = 1;
         else
         if( !strcasecmp( fields[1], "linear")) CF_log_scale = 0;
         else
            bailout( "expecting linear or db for field_scale");
      }
      else
      if( nf == 2 && !strcasecmp( fields[0], "mail"))
         CF_mailaddr = strdup( fields[1]);
      else
         bailout( "error in config file, line %d", lino);
   }

   fclose( f);
}

///////////////////////////////////////////////////////////////////////////////
//  Main                                                                     //
///////////////////////////////////////////////////////////////////////////////

void make_daemon( void)
{
   int childpid, fd;
   long open_max = OPEN_MAX;

   if( (childpid = fork()) < 0)
      bailout( "cannot fork: %s", strerror( errno));
   else if( childpid > 0) exit( 0);

   if( setpgrp() == -1) bailout( "cannot setpgrp");

#ifdef TIOCNOTTY
   if( (fd = open( "/dev/tty", O_RDWR)) >= 0)
   {
      ioctl( fd, TIOCNOTTY, 0);
      close( fd);
   }
#endif /* TIOCNOTTY */

   if( (childpid = fork()) < 0)
      bailout( "cannot fork: %s", strerror( errno));
   else if( childpid > 0) exit( 0);

   for( fd = 0; fd < open_max; fd++) close( fd);

   background = 2;
}

void initialise_channel( struct CHAN *c)
{
   int i;

   c->fft_inbuf = (double *) malloc( FFTWID * sizeof( double));
   c->fft_data = fftw_malloc( sizeof( fftw_complex) * FFTWID);
   c->ffp = fftw_plan_dft_r2c_1d( FFTWID, c->fft_inbuf, c->fft_data,
                           FFTW_ESTIMATE | FFTW_DESTROY_INPUT);

   c->powspec = (double *) malloc( CF_bins * sizeof( double));
   c->sigavg = (double *) malloc( CF_bins * sizeof( double));
   for( i=0; i<CF_bins; i++) c->sigavg[i] = c->powspec[i] = 0;
}

void setup_signal_handling( void)
{
   sa.sa_handler = handle_sigs;
   sigemptyset( &sa.sa_mask);
   sa.sa_flags = 0;
   sigaction( SIGINT, &sa, NULL);
   sigaction( SIGTERM, &sa, NULL);
   sigaction( SIGHUP, &sa, NULL);
   sigaction( SIGQUIT, &sa, NULL);
   sigaction( SIGFPE, &sa, NULL);
   sigaction( SIGBUS, &sa, NULL);
   sigaction( SIGSEGV, &sa, NULL);
}

void set_scheduling( void)
{
   struct sched_param pa;
   int min = sched_get_priority_min( SCHED_FIFO);

   // Set scheduling priority to the minimum SCHED_FIFO value.
   pa.sched_priority = min;
   if( sched_setscheduler( 0, SCHED_FIFO, &pa) < 0)
   {
      report( -1, "cannot set scheduling priority: %s", strerror( errno));
      return;
   }

   report( 0, "using SCHED_FIFO priority %d", min);

   if( mlockall( MCL_CURRENT | MCL_FUTURE) < 0)
      report( -1, "unable to lock memory: %s", strerror( errno));
}

int main( int argc, char *argv[])
{
   while( 1)
   {
      int c = getopt( argc, argv, "vfmic:");

      if( c == 'v') VFLAG++;
      else
      if( c == 'f') background = 0;
      else
      if( c == 'c') config_file = optarg;
      else 
      if( c == -1) break;
      else bailout( "unknown option [%c]", c);
   }

   setup_signal_handling();
   load_config();

   if( CF_output_policy != OP_SPECTRUM)
   {
      int i;
      struct BAND *b;

      for( i=0, b=bands; i<nbands; i++, b++)
         report( 1, "band %s %d %d %s", 
            b->ident, b->start, b->end, b->side->name);
   }

   if( background && !logfile) 
      report( -1, "warning: no logfile specified for daemon");

   if( background) make_daemon();

   setup_input_stream();

   DF = (double) CF_sample_rate/(double) FFTWID;
   report( 1, "resolution: bins=%d fftwid=%d df=%f", CF_bins, FFTWID, DF);

   if( CF_uspec_file)
   {
      // Convert CF_uspec_secs seconds to uspec_max frames
      uspec_max = rint( CF_uspec_secs * CF_sample_rate / FFTWID);
      report( 2, "utility spectrum interval: %d frames", uspec_max);
      report( 2, "utility spectrum file: %s", CF_uspec_file); 
   }

   // Convert CF_output_interval seconds to output_int frames
   output_int = rint( CF_output_interval * CF_sample_rate / FFTWID);
   if( output_int == 0) output_int = 1;
   report( 2, "output interval: %d frames", output_int);

   if( CF_output_policy == OP_SPECTRUM)
   {
      // Convert range variables Hertz to bins
      cuton = CF_range1 / DF;
      cutoff = CF_range2 / DF;
      report( 2, "output bins: %d to %d", cuton, cutoff);
   }   

   // Both sets of channel data structures are initialised, even if mono
   initialise_channel( &left);
   initialise_channel( &right);

   setup_hamming_window();

   if( CF_card_delay) sleep( CF_card_delay);
   report( 0, "sidd version %s %s: starting work", 
      PACKAGE_VERSION, soundsystem);
   alert_on = 1;
   if( CF_priority) set_scheduling();   // Setup real time scheduling

   process_signal();
   if( CF_uspec_file)
      free( CF_uspec_file);
   if( CF_mailaddr)
      free( CF_mailaddr);
   return 0;
}

