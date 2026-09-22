// includes
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <asm-generic/errno-base.h>
#include <asm-generic/ioctls.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <stdlib.h>
#include <termios.h>

// defines

#define TTE_VERSION "0.0.2"

#define CTRL_KEY(key) ((key) & 0x1f)
#define ABUF_INIT {NULL, 0}

enum editorkey
{
	ARROW_RIGHT = 1000,
	ARROW_DOWN,
	ARROW_UP,
	ARROW_LEFT,
	PAGE_UP,
	PAGE_DOWN,
	HOME_KEY,
	END_KEY,
	DEL_KEY
};

// data

typedef struct erow
{
	int size;
	char *chars;
} erow;

struct editorConfig
{
	int curx, cury;
	int rowoff;
	int coloff;
	int screenrows;
	int screencols;
	int numrows;
	erow *row;
	struct termios orig_termios;
};

struct editorConfig E;
// terminal

void die(const char *s)
{
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(s);
	exit(1);
}

void disableRawMode(void)
{
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		die("tcsetattr");
}

void enableRawMode(void)
{
	if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
		die("tcgetattr");
	atexit(disableRawMode);

	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= ~(CS8);
	raw.c_lflag &= ~(ECHO | IEXTEN | ICANON | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
		die("tcsetattr");
}

int editorReadKey(void)
{
	int nread;
	char c;

	while((nread = read(STDIN_FILENO, &c, 1)) != 1)
		if(nread == -1 && errno != EAGAIN) die("read");

	if (c == '\x1b')
	{
		char seq[3];
		
		if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		if(seq[0] == '[')
		{
			if(seq[1] >= '0' && seq[1] <= '9')
			{
				if(read(STDERR_FILENO, &seq[2], 1) != 1)
					return '\x1b';
				if(seq[2] == '~')
				{
					switch(seq[1])
					{
						case '1': return HOME_KEY;
						case '3': return DEL_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;

					}
				}

			}	
			else
			{
				switch(seq[1])
				{
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'F': return END_KEY;
					case 'H': return HOME_KEY;
				}
			}
		}
		else if(seq[0] == 'O')
		{
			switch(seq[1])
			{
				case 'F': return END_KEY;
				case 'H': return HOME_KEY;
			}
		}

		return '\x1b';
	} else
		return c;
}

int getWindowSize(int *rows, int *cols)
{
	struct winsize ws;

	if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 1 || ws.ws_col == 0)
		return -1;
	else
	{
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

// input

void editorMoveCursor(int key)
{
	erow *row = (E.cury >= E.numrows) ? NULL : &E.row[E.cury];
	
	switch(key)
	{
		case ARROW_LEFT:
			if(E.curx != 0)
				E.curx--;
			break;
		case ARROW_UP:
			if(E.cury != 0)
				E.cury--;
			break;
		case ARROW_DOWN:
			if(E.cury != E.numrows)
				E.cury++;
			break;
		case ARROW_RIGHT:
			if(row && E.curx < row->size)
				E.curx++;
			break;
	}

	row = (E.cury >= E.numrows) ? NULL : &E.row[E.cury];
	int rowlen = row ? row->size : 0;
	if(E.curx > rowlen)
		E.curx = rowlen;

}

void editorProcessKeypress(void)
{
	int c = editorReadKey();

	switch(c)
	{
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;

		case PAGE_UP:
		case PAGE_DOWN:
			{
				int times = E.screenrows;
				while (times--)
					editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
			break;

		case HOME_KEY:
			E.curx = 0;
			break;
		case END_KEY:
			E.curx = E.screencols - 1;
			break;

		case ARROW_LEFT:
		case ARROW_DOWN:
		case ARROW_UP:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;
	}	
}

// row operaions

void editorAppendRow(char *s, size_t len)
{
	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

	int at = E.numrows;
	E.row[at].size = len;
	E.row[at].chars = malloc(len + 1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';
	E.numrows++;
}


// file i/o

void editorOpen(char *filename)
{
	FILE *fp = fopen(filename, "r");
	if(!fp) die("fopen");

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;

	while((linelen = getline(&line, &linecap, fp)) != -1)
	{
		while(linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
			linelen--;
		editorAppendRow(line, linelen);
	}
	free(line);
	fclose(fp);
}

// append buff

struct abuf
{
	char *b;
	int len;
};

void abAppend(struct abuf *ab, const char *s, int len)
{
	char *new = realloc(ab->b, ab->len + len);

	if(new == NULL) return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf *ab)
{
	free(ab->b);
}


// output

void editorScroll()
{
	if(E.cury < E.rowoff)
		E.rowoff = E.cury;
	if(E.cury >= E.rowoff + E.screenrows )
		E.rowoff = E.cury - E.screenrows + 1;
	if(E.curx < E.coloff)
		E.coloff = E.curx;
	if(E.curx >= E.coloff + E.screencols)
		E.coloff = E.curx - E.screencols + 1;

}

void editorDrawRows(struct abuf *ab)
{
	int y;
	for(y = 0; y < E.screenrows; y++)
	{
		int filerow = y + E.rowoff;
		if(filerow >= E.numrows)
		{
			if(E.numrows == 0 && y == E.screenrows / 3)
			{
				char welcome[80];
				int welcomelen = snprintf(welcome,sizeof(welcome),
					"The Text Editor - version %s", TTE_VERSION);
			
				if(welcomelen > E.screencols)
					welcomelen = E.screencols;

				int padding = (E.screencols - welcomelen) / 2;
			
				if(padding)
				{
					abAppend(ab, "~", 1);
					padding--;
				}

				while(padding--)
					abAppend(ab, " ", 1);

				abAppend(ab, welcome, welcomelen);
			}
			else
				abAppend(ab, "~", 1);

			abAppend(ab, "\x1b[K", 3);
			if(y < E.screenrows - 1)
				abAppend(ab, "\r\n", 2);
		}
		else
		{
			int len = E.row[filerow].size - E.coloff;
			if(len < 0)
				len = 0;
			if(len > E.screencols)
				len = E.screencols;
			abAppend(ab, &E.row[filerow].chars[E.coloff], len);
		}

		abAppend(ab, "\x1b[K", 3);
		if(y < E.screenrows - 1)
			abAppend(ab, "\r\n", 2);
	}
}

void editorRefresh(void)
{
	editorScroll();
	struct abuf ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cury - E.rowoff) + 1, (E.curx - E.coloff) + 1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

// initialization

void initEditor(void)
{
	E.curx = 0;
	E.cury = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.row = NULL;

	if(getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");
}

int main(int argc, char *argv[])
{
	enableRawMode();
	initEditor();

	if(argc >= 2)
		editorOpen(argv[1]);
	while(1)
	{
		editorRefresh();
		editorProcessKeypress();
	}

	return 0;
}
