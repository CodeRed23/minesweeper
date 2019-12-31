#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ncurses.h>

#define PORT 12345

struct winsize size;

int grid_rows = 7;
int grid_cols = 20;
#define GRID_Y                                  4
#define GRID_X                                  3

#define CELL_LENGTH 				1
#define CELL_WIDTH 				1
#define START_X 				((CELL_WIDTH / 2) + 1)
#define START_Y 				((CELL_LENGTH / 2) + 1)

int status_rows = 1;
int status_cols = 37;
#define STATUS_Y                                GRID_Y - 2
#define STATUS_X                                GRID_X


#define MAX_GRID_ROWS ((size.ws_row) - (status_rows) - (STATUS_Y))

#define OFF(board, x, y) (board[(y - START_Y) / (CELL_LENGTH + 1)][(x - START_X) / (CELL_WIDTH + 1)])

static WINDOW *status = NULL;
static WINDOW *grid = NULL;

char **playboard = NULL;
char **puzzle = NULL;

static int topen = 0;
static int bombs = 0;
static int flags = 0;

//static sigset_t caught_signals;

static void clean(void) {

	int i = 0;
	if(puzzle != NULL) {
		for(i = (size.ws_row - GRID_Y) / 2; i >= 0; i--) {
			free(puzzle[i]);
			puzzle[i] = NULL;
			free(playboard[i]);
			playboard[i] = NULL;
		}
	}

	free(puzzle);
	puzzle = NULL;
	free(playboard);
	playboard = NULL;

	endwin();

}

// sets up the interface for the game
// used also when terminal is resized
static int init(void) 
{
	int i;
	ioctl(fileno(stdout), TIOCGWINSZ, &size);

	if(size.ws_row <= grid_rows * 2  + GRID_Y || size.ws_col <= grid_cols * 2 + GRID_X) {
		return 1;
	}

	endwin();
	keypad(stdscr, true);
	initscr();			/* Start curses mode 		*/
	raw();				/* Line buffering disabled	*/
	noecho();			/* Don't echo() while we do getch */

	status = newwin(status_rows, size.ws_col - STATUS_X, STATUS_Y, STATUS_X);
	grid = newwin(MAX_GRID_ROWS, size.ws_col - GRID_X, GRID_Y, GRID_X);
	
	puzzle = realloc(puzzle, (size.ws_row - GRID_Y) / 2 * sizeof(char *));
	playboard = realloc(playboard, (size.ws_row - GRID_Y) / 2 * sizeof(char *));
	for(i = 0; i < (size.ws_row - GRID_Y) / 2; i++) {
		puzzle[i] = realloc(puzzle[i], (size.ws_col - GRID_X) / 2 * sizeof(char));
		playboard[i] = realloc(playboard[i], (size.ws_col - GRID_X) / 2 * sizeof(char));
	}


	refresh();

	return 0;

}

// 
static void create_puzzle(void) {

	int i = 0;
	int j = 0;
	int count = 0;

	bombs = (grid_rows * grid_cols) / 6;

	wprintw(status, "bomb(s) = %d, flag(s) = %d", bombs, flags);
	wrefresh(status);

	for(i = 0; i < grid_rows; i++) {
		for(j = 0; j < grid_cols; j++) {
			puzzle[i][j] = '0';
			playboard[i][j] = ' ';
		}
	}

	srand(time(NULL));

	while(count < bombs) {
		int random = rand() % (grid_rows);
		int random2 = rand() % (grid_cols);
		if(puzzle[random][random2] != 'b') {
			puzzle[random][random2] = 'b';
			count++;
			for(i = -1; i <= 1; i++) {
				for(j = -1; j <= 1; j++) {
					if(i == 0 && j == 0) {
						continue;
					} else if(random + i < 0 || random2 + j < 0) {
						continue;
					} else if(random + i >= grid_rows || random2 + j >= grid_cols) {
						continue;
					} else if(puzzle[random + i][random2 + j] == 'b') {
						continue;
					}
					puzzle[random + i][random2 + j]++;
				}
			}
		}
	}
}

static void draw_grid(void)
{
	int i, j;
	wmove(grid, 0, 0);

	for(i = 0; i < grid_rows + 1; i++) {
		for(j = 0; j < grid_cols + 1; j++) {
			if(i == 0 && j == 0)
				waddch(grid, ACS_ULCORNER);
			else if(i == 0 && j == grid_cols)
				waddch(grid, ACS_URCORNER);
			else if(i == grid_rows && j == grid_cols)
				waddch(grid, ACS_LRCORNER);
			else if(i == grid_rows && j == 0)
				waddch(grid, ACS_LLCORNER);
			else if(i == 0)
				waddch(grid, ACS_TTEE);
			else if(i == grid_rows)
				waddch(grid, ACS_BTEE);
			else if(j == 0)
				waddch(grid, ACS_LTEE);
			else if(j == grid_cols)
				waddch(grid, ACS_RTEE);
			else
				waddch(grid, ACS_PLUS);

			if(j < grid_cols)
			{
				waddch(grid, ACS_HLINE);
			}
		}
		waddch(grid, '\n');
		for(j = 0;j <= grid_cols && i < grid_rows;j++) {
			waddch(grid, ACS_VLINE);
			int k = 0;
			for(k = 1; k <= CELL_WIDTH; k++) {
				if(CELL_WIDTH / 2 + 1 == k)
					waddch(grid, OFF(playboard, i, j));
				else
					waddch(grid, ' ');
			}
			
		}
		waddch(grid, '\n');
	}

}

static void reset(void) {

	wrefresh(status);
	getch();
	wmove(grid, START_X, START_Y);

	topen = 0;
	flags = 0;

	create_puzzle();
	werase(grid);
	draw_grid();
	wrefresh(grid);

	werase(status);
	wprintw(status, "bomb(s) = %d, flag(s) = %d", bombs, flags);
	wrefresh(status);

}


static void resize_game(void) {

	int tmp_rows = 0, tmp_cols = 0;

	werase(status);
	wprintw(status, "Enter dimensions[rows cols]: ");
	wrefresh(status);
	echo();
	wscanw(status, "%d%d", &tmp_rows, &tmp_cols);

	if(tmp_rows > (size.ws_row - GRID_Y) / 2 
		|| tmp_cols > (size.ws_col - GRID_X) / 2 || tmp_rows <= 0 
		|| tmp_cols <= 0) {

		werase(status);
		wprintw(status, "no no");
		wrefresh(status);
		getch();
		werase(status);
		wprintw(status, "bomb(s) = %d, flag(s) = %d", bombs, flags);
		wrefresh(status);
	} else { 
		grid_rows = tmp_rows;
		grid_cols = tmp_cols;

		reset();
	}

	noecho();

}

static void resize_ter(void) {

	int failed = 1;

	werase(status);
	wprintw(status, "press c to confirm new terminal size");
	wrefresh(status);

	while(failed) {
		if(getch() == 'c') {
			failed = init();
		}
	}

	draw_grid();
	wrefresh(grid);

	werase(status);
	wprintw(status, "bomb(s) = %d, flag(s) = %d", bombs, flags);
	wrefresh(status);

}


int main(int argc, char **argv) {

	atexit(clean);

	int key, x = START_X, y = START_Y;
	bool run = true;

	if(init()) {
		puts("terminal size is too small");
		exit(0);
	}

	create_puzzle();
	draw_grid();
	wmove(grid, y, x);	
	wrefresh(grid);

	while(run) {
		key = getch();	

		switch(key) {
			case 'h':
			if(x > START_X) {
				x -= CELL_WIDTH + 1;
			}
			break;
			case 'l':
			if(x < grid_cols * 2 - CELL_WIDTH) {
				x += CELL_WIDTH + 1;
			}
			break;
			case 'j':
			if(y < (grid_rows * 2) - CELL_LENGTH) {
				y += CELL_LENGTH + 1;
			}
			break;
			case 'k':
			if(y > START_Y) {
				y -= CELL_LENGTH + 1;
			}
			break;
			case 'q':
			run = false;
			break;
			case 'r':
			resize_game();
			break;
			case 'f':
			if(OFF(playboard, x, y) == ' ') { 
				(OFF(playboard, x, y)) = 'f';
				flags++;
				mvwaddch(grid, y, x, 'f');
				werase(status);
				wprintw(status, "bomb(s) = %d, flag(s) = %d", bombs, flags);
			}
			break;
			case '\n':
			if(OFF(playboard, x, y) == ' ') {
				topen++;
				mvwaddch(grid, y, x, OFF(puzzle, x, y));
				if((OFF(playboard, x, y) = OFF(puzzle, x, y)) == 'b') {
					werase(status);
					wprintw(status, "You lose!");
					reset();
				} else if(topen == grid_rows * grid_cols - bombs) {
					werase(status);
					wprintw(status, "You win!");
					reset();
				}
			} else if (OFF(playboard, x, y) == 'f') {
				OFF(playboard, x, y) = ' ';
				mvwaddch(grid, y, x, ' ');
				flags--;
				if(OFF(puzzle, x, y) == 'b') {
					werase(status);
					wprintw(status, "bomb(s) = %d, flag(s) = %d", bombs, flags);
				}
			}
			break;
			case KEY_RESIZE:
			resize_ter();
			break;
		}

		wrefresh(status);
		wmove(grid, y, x);	
		wrefresh(grid);
	}

	return 0;
}

