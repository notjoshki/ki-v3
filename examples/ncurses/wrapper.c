#include <ncurses.h>

void ki_initscr() {
    initscr();
}

void ki_cbreak() {
    cbreak();
}

int ki_getch() {
    return getch();
}

void ki_endwin() {
    endwin();
}

void ki_mvprintw(unsigned int y, unsigned int x, char *format, char *s1, char *s2, char *s3) {
    mvprintw(y, x, format, s1, s2, s3);
}