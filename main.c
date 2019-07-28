#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ncurses.h>

struct winsize size;

int grid_rows = 7;
int grid_cols = 10;
#define GRID_Y                                  4
#define GRID_X                                  3

#define CELL_LENGTH 1
#define CELL_WIDTH 1
#define START_X (CELL_WIDTH / 2 + 1)
#define START_Y (CELL_LENGTH / 2 + 1)


int status_rows = 1;
int status_cols = 37;
#define STATUS_Y                                GRID_Y - 2
#define STATUS_X                                GRID_X

#define MAX_GRID_ROWS ((size.ws_row) - (status_rows) - (STATUS_Y))

#define OFFSET(board, x, y) (board[(y - START_Y) / (CELL_LENGTH + 1)][(x - START_X) / (CELL_WIDTH + 1)])

static WINDOW *status = NULL;
static WINDOW *grid = NULL;

char **playboard = NULL;
char **puzzle = NULL;

static int open = 0;
static int bombs = 0;
static int bombflags = 0;
static int correctflags = 0;

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
	
	puzzle = realloc(puzzle, (size.ws_col - GRID_X) / 2 * sizeof(char *));
	playboard = realloc(playboard, (size.ws_col - GRID_X) / 2 * sizeof(char *));
	for(i = 0; i < (size.ws_row - GRID_Y) / 2; i++) {
		puzzle[i] = realloc(puzzle[i], (size.ws_row - GRID_Y) / 2 * sizeof(char));
		playboard[i] = realloc(playboard[i], (size.ws_row - GRID_Y) / 2 * sizeof(char));
	}


	refresh();

	return 0;

}

// 
static void create_puzzle(void) {

	int i = 0;
	int j = 0;

	bombs = (grid_rows * grid_cols) / 6;

	wprintw(status, "bomb(s) = %d, flag(s) = %d", bombs, bombflags);
	wrefresh(status);

	for(i = 0; i < grid_rows; i++) {
		for(j = 0; j < grid_cols; j++) {
			puzzle[i][j] = '0';
			playboard[i][j] = '#';
		}
	}

	srand(time(NULL));

	int count = 0;
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

static void draw_row(int r) {
	int j = 0;
	for(j = 0; j < grid_cols + 1; j++) {
		waddch(grid, '|');
		if(j < grid_cols)
			waddch(grid, playboard[r][j]);
	}
	waddch(grid, '\n');
}


static void create_grid(void)
 {
        int i, j;
	wmove(grid, 0, 0);

	for(i = 0; i < grid_rows + 1; i++) {
		for(j = 0; j < grid_cols + 1; j++) {
			waddch(grid, '+');
			if(j < grid_cols) 
				waddch(grid, '-');
		}
		waddch(grid, '\n');
		if(i < grid_rows) {
			draw_row(i);
		}
	}

}

static void resize_game(void) {

	int tmp_rows, tmp_cols;

	werase(status);
	wprintw(status, "Enter rows: ");
	wrefresh(status);
	echo();
	wscanw(status, "%d", &tmp_rows);
	if(tmp_rows > (size.ws_row - GRID_Y) / 2) {
		werase(status);
		wprintw(status, "too many rows!");
		wrefresh(status);
		noecho();
		getch();
		werase(status);
		wprintw(status, "bomb(s) = %d, flag(s) = %d", bombs, bombflags);
		wrefresh(status);
		return;
	} 

	werase(status);
	wprintw(status, "Enter cols: ");
	wrefresh(status);
	wscanw(status, "%d", &tmp_cols);

	if(tmp_cols > (size.ws_col - GRID_X) / 2) {
		werase(status);
		wprintw(status, "too many cols!");
		wrefresh(status);
		noecho();
		getch();
		werase(status);
		wprintw(status, "bomb(s) = %d, flag(s) = %d", bombs, bombflags);
		wrefresh(status);
		return;
	} 

	grid_rows = tmp_rows;
	grid_cols = tmp_cols;

	noecho();
	create_puzzle();
	create_grid();
	wrefresh(grid);

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

	create_grid();
	wrefresh(grid);

	wprintw(status, "bomb(s) = %d, flag(s) = %d", bombs, bombflags);
	wrefresh(status);

}

static void reset(void) {

	getch();

	open = 0;
	correctflags = 0;
	bombflags = 0;

	create_puzzle();
	create_grid();
	wrefresh(grid);

	werase(status);
	wprintw(status, "bomb(s) = %d, flag(s) = %d", bombs, bombflags);
	wrefresh(status);

}


int main(void) {

	atexit(clean);

	int key, x = START_X, y = START_Y;
	bool run = true;

	if(init()) {
		puts("terminal size is too small");
		exit(0);
	}

	create_puzzle();
	create_grid();
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
			if(OFFSET(playboard, x, y) == '#') { 
				(OFFSET(playboard, x, y)) = 'f';
				bombflags++;
				wmove(grid, y, START_X - 1 - (CELL_WIDTH / 2));
				draw_row((y - START_Y) / (CELL_LENGTH + 1));
				if(correctflags < bombs && ((OFFSET(puzzle, x, y))) == 'b') {
					correctflags++;
					werase(status);
					wprintw(status, "bomb(s) = %d, flag(s) = %d", bombs, bombflags);
				} else {
					werase(status);
					wprintw(status, "You win!");
					reset();
				}
			}
			break;
			case '\n':
			if(OFFSET(playboard, x, y) == '#') {
				open++;
				if((OFFSET(playboard, x, y) = OFFSET(puzzle, x, y)) == 'b') {
					werase(status);
					wprintw(status, "You lose!");
					reset();
				} else if(open == grid_rows * grid_cols - bombs) {
					werase(status);
					wprintw(status, "You win!");
					reset();
				}
			} else if(OFFSET(playboard, x, y) == 'f') {
				OFFSET(playboard, x, y) = '#';
				bombflags--;
				if(OFFSET(puzzle, x, y) == 'b') {
					correctflags--;
					werase(status);
					wprintw(status, "bomb(s) = %d, flag(s) = %d", bombs, bombflags);
				}
			}
			wmove(grid, y, START_X - 1 - (CELL_WIDTH / 2));
			draw_row((y - START_Y) / (CELL_LENGTH + 1));
			break;
			case KEY_RESIZE:
			resize_ter();
			break;
		}

		wrefresh(status);
		wmove(grid, y, x);	
		wrefresh(grid);
	}

	endwin();
	clear();

	return 0;
}

