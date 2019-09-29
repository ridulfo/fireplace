/***************************************************************************************************
*                                                                                                  *
*       oooooooooooo  o8o                                oooo                                      *
*       `888'     `8  `"'                                `888                                      *
*        888         oooo  oooo d8b  .ooooo.  oo.ooooo.   888   .oooo.    .ooooo.   .ooooo.        *
*        888oooo8    `888  `888""8P d88' `88b  888' `88b  888  `P  )88b  d88' `"Y8 d88' `88b       *
*        888    "     888   888     888ooo888  888   888  888   .oP"888  888       888ooo888       *
*        888          888   888     888    .o  888   888  888  d8(  888  888   .o8 888    .o       *
*       o888o        o888o d888b    `Y8bod8P'  888bod8P' o888o `Y888""8o `Y8bod8P' `Y8bod8P'       *
*                                              888                                                 *
*                                             o888o                                                *
*                                                                                                  *
*                                                                                                  *
*   File:      main.c                                                                              *
*   Author:    Wyatt Sheffield                                                                     *
*                                                                                                  *
*   Lights a cozy fire in your terminal. Goes well with coffee.                                    *
*                                                                                                  *
*                   Copyright (c) 2017 Wyatt Sheffield and GitHub contributors                     *
*                                                                                                  *
***************************************************************************************************/

#include <time.h>
#include <ncurses.h>
#include <signal.h>
#include <stdlib.h> //random
#include <unistd.h> //usleep, getopt

#ifdef _WIN32
#include <windows.h> //Sleep
#endif

#define MIN(X, Y)  ((X) < (Y) ? (X) : (Y))
#define MAX(X, Y)  ((X) > (Y) ? (X) : (Y))

//----------------------------------------[Global variables]----------------------------------------

static char dispch;        //the character used to draw the flames
static int WIDTH, HEIGHT;  //Set by ncurses
static int framerate;      //framerate
static int heightrecord;   //max height reached by the flames
static int maxtemp;        //maximum flame temperature
static int wolfrule;       //rule for wolfram eca
static int NUM_COLORS;     //Number of flame colors used
static volatile sig_atomic_t sig_caught = 0;

//--------------------------------------------[Structs]---------------------------------------------

typedef struct colorvalstruct{ short r,g,b; } color_val;

//------------------------------[Memory Management and Initialization]------------------------------

int** init(int y, int x) {
    int** out = malloc(y * sizeof(int*));
    for (int i = 0; i < y; i++) {
        out[i] = calloc(x, sizeof(int));
    }
    return out;
}

void deallocate(int** in, int rows)
{
    for (int i = 0; i < rows; i++) {
        free(in[i]);
    }
    free(in);
}

void start_ncurses(color_val* colors)
{
    initscr();
    start_color();
    if (COLORS < 256){
        for (int i = 0; i < 8; i++) {
            color_content(i, &colors[i].r, &colors[i].g, &colors[i].b);
        }
        NUM_COLORS = 7;
        init_color(COLOR_BLACK,    100,   100,   100);
        init_color(COLOR_RED,      300,   0,     0);
        init_color(COLOR_GREEN,    500,   0,     0);
        init_color(COLOR_BLUE,     700,   100,   0);
        init_color(COLOR_YELLOW,   900,   300,   0);
        init_color(COLOR_MAGENTA,  1000,  500,   100);
        init_color(COLOR_CYAN,     1000,  800,   500);
        init_color(COLOR_WHITE,    1000,  1000,  1000);

        init_pair(1,  COLOR_RED,      COLOR_BLACK);
        init_pair(2,  COLOR_GREEN,    COLOR_BLACK);
        init_pair(3,  COLOR_BLUE,     COLOR_BLACK);
        init_pair(4,  COLOR_YELLOW,   COLOR_BLACK);
        init_pair(5,  COLOR_MAGENTA,  COLOR_BLACK);
        init_pair(6,  COLOR_CYAN,     COLOR_BLACK);
        init_pair(7,  COLOR_WHITE,    COLOR_BLACK);
    }
    else {
        int x256[] = { 233, 52, 88, 124, 160, 166, 202, 208, 214, 220, 226, 227, 228, 229, 230, 231 };
        NUM_COLORS = sizeof(x256)/sizeof(int);
        // the first color in the list will be the background, so we start at 1.
        for(size_t i = 1; i < NUM_COLORS; i++){
            init_pair(i, x256[i], x256[0]);
        }
        NUM_COLORS -= 1;
    }
    curs_set(0);    //invisible cursor
    timeout(0);     //make getch() non-blocking

    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    getmaxyx(stdscr, HEIGHT, WIDTH);
    heightrecord = HEIGHT;
}

void restore_colors(color_val* colors)
{
    for (int i = 0; i < 8; i++) {
        init_color(i, colors[i].r, colors[i].g, colors[i].b);
    }
}

//-----------------------------------[Cellular Automata Helpers]------------------------------------

//As a cell cools it has a higher chance of cooling again on the next frame.
int cooldown(int heat) {
    if (heat == 0) return 0;
    int r = (rand() % heat);
    if (r == 0) heat--;
    return heat;
}

void cleargrid(int** grid, int h)
{
    for (int i = h; i < HEIGHT; i++) {
        for (int j = 0; j < WIDTH; j++) {
            grid[i][j] = 0;
        }
    }
}

void warm(int* heater, int* hotplate)
{
    for (int i = 0; i < WIDTH; i++) {
        hotplate[i] /= 2;
    }
    for (int i = 0; i < WIDTH; i++) {
        hotplate[i] += heater[i] * maxtemp;
    }
}

//---------------------------------------[Cellular Automata]----------------------------------------

void nextframe(int** field, int** count, int* hotplate)
{
    cleargrid(count, heightrecord);
    int rowsum = 0;
    int h = heightrecord - 3;
    h = MAX(h, 1);  //we can ignore the vast majority of cold cells
                    //and skip down to the bottom of the window
    for (int i = h; i <= HEIGHT; i++) {
        for (int j = 0; j < WIDTH; j++) {
            float avg = 0;
            //int temp = rand() % maxtemp * 4;
            int counter = 0;

            //the search space around the current cell is as follows
            //    .......
            //    ...X...
            //    .......
            //    .......
            //    .......
            for (int xoff = -3; xoff <= 3; xoff++) {
                for (int yoff = -1; yoff <= 3; yoff++) {
                    int y = i + yoff;
                    y = MAX(y,0); //if y is less than zero, clamp it to zero.
                    int x = j + xoff;
                    //if the search has gon beyond the left or right, no heat is added
                    if (x < 0 || x >= WIDTH) avg += 0;
                    //if the search goes below the screen, add the hotplate value.
                    //the hotplate has infinite depth.
                    else if (y >= HEIGHT)  avg += hotplate[x];
                    else avg += field[y][x];
                    counter++;
                }
            }
            avg /= counter;
            //see if the cell cools or not
            //we add the value at (i-1) so that an upward motion will be created.
            count[i - 1][j] = cooldown(avg);
            rowsum += count[i-1][j];
        }
        if (rowsum > 0 && i < heightrecord) heightrecord = i;
        rowsum = 0;
    }

    for (int i = 0; i < HEIGHT; i++) {
        for (int j = 0; j < WIDTH; j++) {
            field[i][j] = count[i][j];
        }
    }
}

//Wolfram's Elementary cellular atomaton
//https://en.wikipedia.org/wiki/Elementary_cellular_automaton
void wolfram(int* world, const int rule)
{
    int* next = malloc(WIDTH * sizeof(int));
    int l,c,r;
    int lidx, ridx;
    int current;
    for (int i = 0; i < WIDTH; i++) {
        lidx = i > 0 ? i - 1 : WIDTH - 1;
        ridx = (i + 1) % WIDTH;
        l = world[lidx];
        c = world[i];
        r = world[ridx];
        current = (l<<2) | (c<<1) | r;
        next[i] = (rule>>current) & 0b1;
    }

    for (int i = 0; i < WIDTH; i++) {
        world[i] = next[i];
    }
    free(next);
}

//----------------------------------------[Draw and Animate]----------------------------------------

void printframe(int** field, int** count)
{
    char disp;
    for (int i = 0; i < HEIGHT; i++) {
        for (int j = 0; j < WIDTH; j++) {

            move(i,j);
            //if the cell is cold, print a space, otherwise print [dispch]
            int color = (NUM_COLORS * field[i][j] / maxtemp) + 1;
            color = MIN(color,NUM_COLORS);
            disp = field[i][j] == 0 ? ' ' : dispch;
            attron(COLOR_PAIR(color));
            addch(disp);
            attroff(COLOR_PAIR(color));
        }
    }
    refresh();
}

void flames()
{
    int** field = init(HEIGHT, WIDTH); //The cells that will be displayed
    int** count = init(HEIGHT, WIDTH); //A grid of cells used to tally neighbors for CA purposes
    // these special cells provide "heat" at the bottom of the screen.
    int* heater = malloc(WIDTH * sizeof(int));
    // The heater heats the hotplate. The hotplate will cool without heat.
    int* hotplate = malloc(WIDTH * sizeof(int));

    for (int i = 0; i < WIDTH; i++) {
        heater[i] = rand() % 2;
        hotplate[i] = 0;
    }

    int c = 0;

    while (sig_caught == 0 && (c = getch()) != 'q') {
        //Use Rule 60 to make flames flicker nicely.
        if(c == KEY_UP) maxtemp++;
        if(c == KEY_DOWN && maxtemp > 1) maxtemp--;
        wolfram(heater, wolfrule);
        warm(heater, hotplate);
        printframe(field, count);
        nextframe(field, count, hotplate);
        #ifdef _WIN32
            Sleep(framerate);
        #elif __linux__
            usleep(framerate);
        #endif
    }

    if (c == 'q') raise(SIGINT);

    refresh();
    free(hotplate); free(heater);
    deallocate(field, HEIGHT); deallocate(count, HEIGHT);
}

void run(){
    while(sig_caught != 1){
        getmaxyx(stdscr, HEIGHT, WIDTH);
        flames();
        endwin();
        clear();
        refresh();
        if (sig_caught == 2) sig_caught = 0;
    }
}
//----------------------------------------------[Help]----------------------------------------------

void printhelp(const char progname[])
{
    const char fmtstr[] =
        "\nUsage: %s [options]\n"\
        "\t-c character\tAn ASCII character to draw the flames. Default is '@'.\n"\
        "\t-h\t\tPrint this message.\n"\
        "\t-f framerate\tSet the framerate in frames/sec. Default is 20.\n"\
        "\t\t\tA framerate of zero will make frames spit out as soon as they are ready.\n"\
        "\t-t temp\t\tSet the maximum temperature of the flames. Default is 10.\n"\
        "\t\t\tA higher temp means taller flames. Press the up/down arrows\n"\
        "\t\t\tto change the temperature at any time.\n\n"\
        "Press ^C or q at any time to douse the flames.\n\n";
    fprintf(stderr, fmtstr, progname);
}

//--------------------------------------------[Signals]---------------------------------------------

void sig_handler(int signum)
{
    if (signum == SIGINT) {
        sig_caught = 1;
    }
    if (signum == SIGWINCH) {
        sig_caught = 2;
    }
}

//----------------------------------------------[Main]----------------------------------------------


int main(int argc, char** argv)
{
    signal(SIGINT, sig_handler);
    signal(SIGWINCH, sig_handler);

    int persecond = 1000000;
    srand(time(NULL));
    framerate = persecond / 20;
    maxtemp = 10;
    dispch = '@';
    wolfrule = 60;

    int c;
    opterr = 0;
    while ((c = getopt(argc, argv, "c:hf:t:w:")) != -1) {
        switch (c) {
            case 'c':
                dispch = optarg[0];
                break;
            case 'h':
                printhelp(argv[0]);
                return 0;
            case 'f':
                if (atoi(optarg) < 1) framerate = 0;
                else framerate = persecond / atoi(optarg);
                break;
            case 't':
                maxtemp = atoi(optarg);
                break;
            case 'w':
                wolfrule = atoi(optarg);
                break;
            case '?':
                fprintf(stderr, "\nYou've really bunged this one up. Here, this may help:\n");
                printhelp(argv[0]);
                return 1;
            default:
                fprintf(stderr, "What the hell?!");
                return 2;
        }
    }
    color_val* colors = malloc(8 * sizeof(color_val));
    start_ncurses(colors);
    run();
    if(COLORS < 256){
        restore_colors(colors);
    }
    free(colors);
    clear();
    refresh();
    endwin();
    return 0;
}
