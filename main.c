#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <ncurses.h>

extern int errno;

#define PORT 12345

struct winsize size;

int grid_rows = 4;
int grid_cols = 7;
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
	int j = 0;
	for(i = 0; i < grid_rows; i++) {
		for(j = 0; j < grid_cols; j++) {
			free(playboard[i]);
			free(puzzle[i]);
		}
	}

	free(puzzle);
	free(playboard);
	playboard = NULL;

	endwin();

}

#ifdef DEBUG

static int backup(size_t no, ...) 
{
	int fd = creat("backup", S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if(fd == -1) {
		/* errors */	
		return errno;
	}

	va_list ap;
	va_start(ap, no);
	struct iovec *iov = alloca(no);
	size_t i;
	for(i = 0; i < no; i++) {
		iov[i].iov_base = va_arg(ap, char *);
		iov[i].iov_len = grid_rows * grid_cols;
	}

	iov[no].iov_base = &grid_rows;
	iov[no].iov_len = sizeof(int);

	iov[no + 1].iov_base = &grid_cols;
	iov[no + 1].iov_len = sizeof(int);

	if(writev(fd, iov, no) < 0) {
		return errno;
	}

	va_end(ap);
	close(fd);
	
	return EXIT_SUCCESS;
}

static int retrieve(size_t no, ...) 
{
	int fd = open("backup", O_RDWR | O_NONBLOCK);
	struct stat sb;
	if(fstat(fd, &sb)) {
		return errno;
	}

	if(fd < 0) {
		return errno;
	}

	posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);

	va_list ap;
	va_start(ap, no);
	struct iovec *iov = alloca(no + 2);
	size_t i;
	for(i = 0; i < no; i++) {
		iov[i].iov_base = va_arg(ap, char *);
		iov[i].iov_len = grid_rows * grid_cols;
	}

	iov[no].iov_base = &grid_rows;
	iov[no].iov_len = sizeof(int);

	iov[no + 1].iov_base = &grid_cols;
	iov[no + 1].iov_len = sizeof(int);

	if(readv(fd, iov, no) < 0) {
		return errno;
	}

	va_end(ap);
	if(close(fd) < 0) {
		return errno;
	}

	return EXIT_SUCCESS;
}

#endif

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
static void create_puzzle(void) 
{

	int i = 0;
	int j = 0;
	int count = 0;

	bombs = (grid_rows * grid_cols) / 6;

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
				waddch(grid, ACS_HLINE);
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

static void reset(void) 
{

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

#ifdef DEBUG
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

#endif

int main(int argc, char **argv) {

	atexit(clean);

	int key, x = START_X, y = START_Y;
	bool run = true;

	if(init()) {
		puts("terminal size is too small");
		exit(0);
	}

	create_puzzle();

	wprintw(status, "bomb(s) = %d, flag(s) = %d", bombs, flags);
	wrefresh(status);

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
			#ifdef DEBUG
			case 'r':
			resize_game();
			break;
			#endif
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
			#ifdef DEBUG
			case KEY_RESIZE:
			resize_ter();
			break;
			case 's':
			if((backup(2, puzzle, playboard)) >= 0) {
				werase(status);
				wprintw(status, "successfully created backup");
				wrefresh(status);
			} else {
				werase(status);
				wprintw(status, "Failed to create backup");
				wrefresh(status);
			}
			break;
			case 'b':
			if(retrieve(2, puzzle, playboard) >= 0) {
				werase(status);
				wprintw(status, "successfully retrieved backup");
				draw_grid();
				wrefresh(status);
			} else {
				werase(status);
				wprintw(status, "Failed to create backup");
				wrefresh(status);
			}
			break;
			#endif
		}

		wrefresh(status);
		wmove(grid, y, x);	
		wrefresh(grid);
	}

	return EXIT_SUCCESS;
}

