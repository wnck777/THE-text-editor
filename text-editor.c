// includes

#include <asm-generic/errno-base.h>
#include <asm-generic/ioctls.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <termios.h>

// defines

#define CTRL_KEY(key) ((key) & 0x1f)
#define ABUF_INIT {NULL, 0}

// data
struct editorConfig
{
	int screenrows;
	int screencols;
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

char editorReadKey(void)
{
	int nread;
	char c;

	while((nread = read(STDIN_FILENO, &c, 1)) != 1)
		if(nread == -1 && errno != EAGAIN) die("read");
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

void editorProcessKeypress(void)
{
	char c = editorReadKey();

	switch(c)
	{
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;
	}	
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

void editorDrawTildas(struct abuf *ab)
{
	int y;
	for(y = 0; y < E.screenrows; y++)
	{
		abAppend(ab, "~", 1);

		if(y < E.screenrows - 1)
			abAppend(ab, "\r\n", 2);
	}
}

void editorRefresh(void)
{
	struct abuf ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[2J", 4);
	abAppend(&ab, "\x1b[H", 3);

	editorDrawTildas(&ab);

	abAppend(&ab, "\x1b[H", 3);
	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

// initialization

void initEditor(void)
{
	if(getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");
}

int main(void)
{
	enableRawMode();
	initEditor();

	while(1)
	{
		editorRefresh();
		editorProcessKeypress();
	}

	return 0;
}
