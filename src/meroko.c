/* Meroko - Explorer 1 hardware-level emulator 

 This file is subject to the terms and conditions of the GNU General Public
 License.  See the file COPYING in the main directory of this archive for
 more details.

 $Id$
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>
#include <ncurses.h>
#include <signal.h>
#include <string.h>

#include "meroko.h"
/* Expansion cards */
#include "raven_cpu.h"

int no_curses;
extern int no_throttle;
int do_log;
int do_inst_trace;
int do_inst_trace_count;
int do_inst_log;

/* NCurses data */
/* Root window */
WINDOW *msg_w;

FILE *logfile;

extern void raven_disp_init();
struct itimerval interval;

unsigned int period = 0;

extern void updatetime(int arg);

// Alarm
void alarmhandler(int fnord){
  int key;
  extern void sib_updateslow();

  signal(SIGALRM,&alarmhandler);
  setitimer(ITIMER_REAL,&interval,NULL);    
  /*
  updatetime(0);
  */
  key = no_curses ? ERR : wgetch(msg_w);
  if(key != ERR){
    // Stuff
    switch(key){
    case 's': // STEP
      raven_step();
      break;
    case 'g': // GO
      raven_cont();
      break;
    case 'h': // HALT
      raven_halt();
      break;
    case 'd': // DUMP
      raven_dump();
      break;
    case 'r': // tRap
      {
        extern void raven_trap();
        raven_trap();
      } 
      break;
    case 'l':
      if(do_log == 0){
	do_log++;
        logfile = fopen("DEBUG.LOG","w+");
      }	
      break;
    case 't':
      do_inst_trace++;
      break;
    };
  }
  sib_updateslow();
  if(no_curses == 0){ raven_dispupdate(); }
  period=0;
}

// SIGUSR1: dump machine state and a screenshot (works without curses)
void usr1handler(int fnord){
  signal(SIGUSR1,&usr1handler);
  raven_dump();
}

// To log a message
void logmsg(char *msg)
{
  if (logfile) {
    // Save debug log
    fprintf(logfile,"%s",msg);
    fflush(logfile);
  }
  // Don't do this for some stuff.
  if(strncmp(msg,"DEBUG:",6)==0){ return; }
  if(strncmp(msg,"::",2)==0){ return; }

  if (no_curses) {
    printf("%s", msg);
    return;
  }

  scroll(msg_w);                      // Drop a line
  mvwprintw(msg_w,(LINES/2)-2,1,"%s",msg); // Print text
  box(msg_w,0,0);                     // Redraw box
  wmove(msg_w,0,(COLS/2)-4);          // Move cursor
  wprintw(msg_w,"MESSAGES");          // Write title
  wrefresh(msg_w);                    // Redraw
}

// log a message after formating (like printf)
void logmsgf(char *fmt, ...)
{
    char string[512];
    va_list ap;

    va_start(ap, fmt);
    vsprintf(string, fmt, ap);
    va_end(ap);

    logmsg(string);
}

extern char *optarg;

// ENTRY POINT
int main(int argc, char *argv[]){
   int c;

  while ((c = getopt(argc, argv, "nltfh")) != -1) {
    switch (c) {
    case 'f':
      no_throttle++;
      break;
    case 'h':
    default:
      fprintf(stderr,
              "usage: %s [-n] [-l] [-t] [-f]\n"
              "  -n  no curses status display; messages go to stdout\n"
              "  -l  write a debug log to DEBUG.LOG\n"
              "  -t  trace microinstructions (needs a TRACELOG build)\n"
              "  -f  full speed: do not hold the CPU to real-time speed\n",
              argv[0]);
      return (c == 'h') ? 0 : 1;
    case 'n':
      no_curses++;
      break;
    case 'l':
      do_log++;
      break;
    case 't':
      do_inst_trace++;
      break;
    }
  }

  if (do_log) {
    logfile = fopen("DEBUG.LOG","w+");
  }

  raven_initialize();
  signal(SIGUSR1,&usr1handler);

  if (no_curses == 0) {
    // Curses init
    printf("[SYS] Initializing curses...\n");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr,TRUE);
    //  printw("Got %dx%d",LINES,COLS);
    msg_w = newwin((LINES/2),COLS,(LINES/2),0);
    scrollok(msg_w,TRUE);
    nodelay(msg_w,TRUE);
    raven_disp_init();
  }

  /* Establish monitoring: once a second, advance the real-time clock and
     (with curses) poll the console keyboard and redraw the status display */
  signal(SIGALRM,&alarmhandler);
  interval.it_interval.tv_usec = 0;
  interval.it_interval.tv_sec = 1;
  interval.it_value.tv_usec = 0;
  interval.it_value.tv_sec = 1;

  setitimer(ITIMER_REAL,&interval,NULL);    

  logmsg("[SYS] Starting CPU at 0\n");  
  while(1){
    raven_clockpulse();
  }

  // Die
  if (no_curses == 0) {
    endwin();
  }

  if (do_log) {
    fclose(logfile);
  }

  return(0);
}


/*
 * Local Variables:
 * indent-tabs-mode:nil
 * c-basic-offset:2
 * End:
*/
