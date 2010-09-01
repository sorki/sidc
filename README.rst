Sudden ionospheric disturbance collector (sidc)
================================================

Note: This is the fork of the `original sidc <http://gitorious.org/sidc>`_ with various improvements and bugfixes.

1. Introduction
----------------

sidc is a simple C program to monitor and record VLF signal for sudden ionospheric disturbances.
It is forked from sidd program wirrten by Paul Nicholson <sid0807@abelian.org> http://abelian.org/sid/

2. Installation
----------------

- First install FFTW3 dependency from www.fftw.org or use your favorite package manager
  to install `fftw-devel` package.

- Configure and compile the source

   ./configure

   make

- Install the source (if appliable)

   make install

- Edit sidc.conf to suit your requirements.  Select an output policy from the
  three available.  

- Start sidc in verbose foreground mode with the command

   ./sidc -vvf

- The program will output peak and rms readings in the range 0.0 to 1.0
  Adjust your mixer gain settings to leave a little headroom on the peak
  reading.

- Plot the utility spectrum file

- Check the data file columns are the ones you want.

- Set your PC clock and activate your favourite time synchronisation 
  software.  Make sure it slews the clock rather than stepping the time.

- Restart sidc in background with

   ./sidc -v

- After a period of time, plot some of the data from the output file.

- After a midnight crossing, make sure sidc has switched to the next output file.

3. Command line options
------------------------

There are just a few command line options - most controls are
in the config file. 

 -v    Be a little more verbose with log messages. 
       Use several -v for more detail.

 -f    Run in foreground.  By default, sidc detaches from the process
       group and terminal and becomes a daemon.  In foreground mode,
       log messages are duplicated to stderr.

 -c config_file   Run with a specified config file.  By default, sidc looks
                  for /etc/sidc.conf

 -p pid_file   Override default PID file location which is /var/run/sidc.pid.
        Pid file is created everytime this process becomes a daemon. Creation
        is skipped if file is not writable.

4 Miscellaneous notes
----------------------
- sidc will set the soundcard to the nearest available sample rate to that
  specified in sidc.conf

- 24 bit soundcards may return data in 32 bit words.  Try setting 'bits 24'
  and if sidc reports the mode unavailable, use 'bits 32'.

- Make sure you have enough disk space.   The example sidc.conf with 8 bands
  generates files of about 100Mbytes per day, which compress down to about 
  30Mbytes.    Arrange scripts for plotting.  Arrange scripts for compressing
  and archiving files that are a few days old.

- Simple init scripts provided. Check the readme in `init_scripts` directory.
