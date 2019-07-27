//
//  main.c
//  text-editor
//  Created by Gabriel Villalonga Simón following a tutorial on 27/07/2019.
//


#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>

// helpers
int clamp(int target, int min, int max) {
	if (target > max) {
		return max;
	}
	if (target < min) {
		return min;
	}
	return target;
}


// defines
/*
 * @description strips down the 5th and 6th bits to simulate ctrl inputs like the terminal does...
 *				take in mind that we stop all kind of ctrl shortcuts in terminals with enableRawMode()
 */
#define CTRL_KEY(k) ((k) & 0x1f)
#define STR_INIT {NULL, 0}

#define EDITOR_VERSION "0.0.1"

// data
enum editorKey {
	ARROW_LEFT = 11111,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	PAGE_UP,
	PAGE_DOWN,
	HOME_KEY,
	END_KEY,
	DEL_KEY,
};

struct str {
	char* b;
	int len;
};

void strAppend(struct str *s, const char* c, int len) {
	char* new = realloc(s->b, s->len + len);
	
	if (new == NULL) return;
	memcpy(&new[s->len], c, len);
	s->b = new;
	s->len += len;
}

void strFree(struct str *s) {
	free(s->b);
}


struct editorConfig {
	int cx, cy;
	int screenrows;
	int screencols;
	struct termios orig_termios;
};

struct editorConfig E;

// terminal stuff


void die(const char *s) {
	// reset and clean
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	perror(s);
	exit(1);
}

void disableRawMode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		die("tcsetattr");
}

/*
 * @description Stops the echoing into the terminal. Similar to when you enter password on sudo.
 */
void enableRawMode() {
//    if (-1 == tcgetattr(STDIN_FILENO, &E.orig_termios))
//		die("tcsetattr");
//    atexit(disableRawMode);
//    struct termios raw = E.orig_termios;
//    tcgetattr(STDIN_FILENO, &raw);
//	// ICANON READ BYTE BY BYTE INSTEAD OF LINE BY LINE (DEFAULT...)
//	// WE CHANGE ECHO BITES WITH AND AND NOT OPERATORS TO SET THE 4TH BIT TO 0. SO IT
//	// TURNS OFF THE ECHOING...
//	// ....01000 -> 0000...
//    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
//	raw.c_oflag &= ~(OPOST);
//	raw.c_cflag |= (CS8);
//	raw.c_cc[VMIN] = 0;
//	raw.c_cc[VTIME] = 1;
//    if (-1 == tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw)) die("tcsetattr");
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
	atexit(disableRawMode);
	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

/*
 * @description Returns the last char pressed.
 */
int editorReadKey() {
	int nread;
	char c;
	while ((nread = (int)read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}
	
	if (c == '\x1b') {
		char seq[3];
		if (read(STDIN_FILENO, &seq[0], 1) != 1) {
			return '\x1b';
		}
		if (read(STDIN_FILENO, &seq[1], 1) != 1) {
			return '\x1b';
		}
		
		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2] == '~') {
					switch (seq[1]) {
						case '1': return HOME_KEY;
						case '3': return DEL_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
					}
				}
			} else {
				switch (seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}
		} else if (seq[0] == 'O') {
			switch (seq[1]) {
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}
			
		
		return '\x1b';
	}
	return c;
}


int getCursorPosition(int *rows, int *cols) {
	char buf[32];
	unsigned int i = 0;
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
	while (i < sizeof(buf) - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if (buf[i] == 'R') break;
		i++;
	}
	buf[i] = '\0';
	
	if (buf[0] != '\x1b' || buf[1] != '[') return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
	return 0;
}

int getWindowSize(int *rows, int *cols) {
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return getCursorPosition(rows, cols);
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

// output

void editorDrawRows(struct str *s) {
	int y;
	for (y = 0; y < E.screenrows; y++) {
		if (y == E.screenrows - 1) {
			char welcome[80];
			int welcomelen = snprintf(welcome, sizeof(welcome), "Editor editor -- Current version %s", EDITOR_VERSION);
			if (welcomelen > E.screencols) welcomelen = E.screencols;
			int padding = (E.screencols - welcomelen) / 2;
			if (padding) {
				strAppend(s, "~", 1);
				padding--;
			}
			while (padding--) strAppend(s, " ", 1);
			strAppend(s, welcome, welcomelen);
		} else {
			strAppend(s, "~", 1);
		}

		strAppend(s, "\x1b[K", 3);
		if (y < E.screenrows - 1) {
			strAppend(s, "\r\n", 2);
		}
	}
}

void editorRefreshScreen() {
	struct str s = STR_INIT;
	strAppend(&s, "\x1b[?25l", 6);
	strAppend(&s, "\x1b[H", 3);
	editorDrawRows(&s);
	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
	strAppend(&s, buf, (int)strlen(buf));
	strAppend(&s, "\x1b[?25h", 6);
	write(STDOUT_FILENO, s.b, s.len);
	strFree(&s);
}

// editor

/*** input ***/

void editorMoveCursor(int key) {
	switch (key) {
		case ARROW_LEFT:
			E.cx = clamp(--E.cx, 0, E.screencols);
			break;
		case ARROW_RIGHT:
			E.cx = clamp(++E.cx, 0, E.screencols);
			break;
		case ARROW_UP:
			E.cy = clamp(--E.cy, 0, E.screenrows - 1);
			break;
		case ARROW_DOWN:
			E.cy = clamp(++E.cy, 0, E.screenrows - 1);
			break;
	}
}

void editorProcessKeypress() {
	int c = editorReadKey();
	switch (c) {
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;
		case HOME_KEY:
			E.cx = 0;
			break;
		case END_KEY:
			E.cx = E.screencols - 1;
			break;
		case PAGE_UP:
		case PAGE_DOWN:
			{
				int times = E.screenrows;
				
				while (times--)
					editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
			break;
		
		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;
	}
}

// init
void initEditor() {
	E.cx = 0;
	E.cy = 0;
	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main() {
    enableRawMode();
	initEditor();
    while(1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}
