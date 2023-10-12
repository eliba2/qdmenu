/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <QApplication>
#include <QScreen>
#include <QWidget>
#include <QColor>
#include <QWindow>
#include <QPainter>
#include <QKeyEvent>
#include <QDebug>
#include <QClipboard>
#include <QMimeData>
#include <QLineEdit>

#include "drw.h"
#include "util.h"

/* macros */
#define INTERSECT(x,y,w,h,r)  (MAX(0, MIN((x)+(w),(r).x_org+(r).width)  - MAX((x),(r).x_org)) \
                             * MAX(0, MIN((y)+(h),(r).y_org+(r).height) - MAX((y),(r).y_org)))
#define TEXTW(X)              (drw_fontset_getwidth(drw, (X)) + lrpad)

/* enums */
enum { SchemeNorm, SchemeSel, SchemeOut, SchemeLast }; /* color schemes */

struct item {
	char *text;
	struct item *left, *right;
	int out;
};

static char text[BUFSIZ] = "";
static char *embed;
static int bh, mw, mh;
static int inputw = 0, promptw;
static int lrpad; /* sum of left and right padding */
static size_t cursor;
static struct item *items = NULL;
static struct item *matches, *matchend;
static struct item *prev, *curr, *next, *sel;
static int mon = -1;
static QScreen *screen, *root, *parentwin, *win;

/* 
 * not needed in the qt version
static Atom clip, utf8;
static XIC xic;
*/

static Drw *drw;
static QColor **scheme[SchemeLast];

#include "config.h"

static int (*fstrncmp)(const char *, const char *, size_t) = strncmp;
static char *(*fstrstr)(const char *, const char *) = strstr;

// forwared declarations
static void keypress(QKeyEvent *ev);
static void cleanup(void);

// edit window
class DMenuLineEdit : public QLineEdit {
    Q_OBJECT

public:
    explicit DMenuLineEdit(QWidget* parent = nullptr)
        : QLineEdit(parent)
    {}

protected:
    void keyPressEvent(QKeyEvent* event) override {
        // Call base class implementation for unhandled key presses
        QLineEdit::keyPressEvent(event);
	keypress(event);
    }
};

/* main window */
class DMenuWindow : public QWidget {
    Q_OBJECT

private:
    DMenuLineEdit* lineEdit;

protected:

    void paintEvent(QPaintEvent* event) override {
		QPainter painter(this);
		if (drw->drawable) {
			painter.drawPixmap(0, 0, *drw->drawable);
		}
    }

    void keyPressEvent(QKeyEvent* event) override {
		keypress(event);
    }

    void focusInEvent(QFocusEvent* event) override {
    }

    void closeEvent(QCloseEvent* event) override {
		cleanup();
    }

    // For handling clipboard pasting
    void paste() {
        const QClipboard *clipboard = QApplication::clipboard();
        const QMimeData *mimeData = clipboard->mimeData();
        if (mimeData->hasText()) {
            // Handle the pasted text here
        }
    }

public:
	DMenuWindow(QWidget* parent = nullptr) : QWidget(parent) {
		// no title bar
        setWindowFlags(Qt::FramelessWindowHint);

		// text edit
        lineEdit = new DMenuLineEdit(this);
        lineEdit->setGeometry(0, 0, 0, 0); /* initial size */

        // Optionally, connect QLineEdit signals to slots for handling text changes, return pressed, etc.
        connect(lineEdit, &QLineEdit::textChanged, this, &DMenuWindow::onTextChanged);
    }

	void updateEditBox(int ex, int ey, int ew, int eh) {
		lineEdit->setStyleSheet(QString("QLineEdit { border: none; background-color: %1; color: %2; }").arg(drw->scheme[ColBg]->name()).arg(drw->scheme[ColFg]->name()));
		lineEdit->setGeometry(ex, ey, ew, eh);
	}

	void focusEditBox() {
		lineEdit->setFocus();
	}

	QString getText() {
		return lineEdit->text();
	}

	DMenuLineEdit* getLineEdit() {
		return lineEdit;
	}

public slots:

	void onTextChanged(const QString& newText) {
		// Handle text changes in the QLineEdit here if needed
	}
};


static unsigned int textw_clamp(const char *str, unsigned int n)
{
	unsigned int w = drw_fontset_getwidth_clamp(drw, str, n) + lrpad;
	return MIN(w, n);
}


static void appenditem(struct item *item, struct item **list, struct item **last)
{
	if (*last)
		(*last)->right = item;
	else
		*list = item;

	item->left = *last;
	item->right = NULL;
	*last = item;
}

static void calcoffsets(void)
{
	int i, n;

	if (lines > 0)
		n = lines * bh;
	else
		n = mw - (promptw + inputw + TEXTW("<") + TEXTW(">"));
	/* calculate which items will begin the next page and previous page */
	for (i = 0, next = curr; next; next = next->right)
		if ((i += (lines > 0) ? bh : textw_clamp(next->text, n)) > n)
			break;
	for (i = 0, prev = curr; prev && prev->left; prev = prev->left)
		if ((i += (lines > 0) ? bh : textw_clamp(prev->left->text, n)) > n)
			break;
}

static void cleanup(void) 
{
	size_t i;

	for (i = 0; i < SchemeLast; i++)
		free(scheme[i]);
	for (i = 0; items && items[i].text; ++i)
		free(items[i].text);
	free(items);
	drw_free(drw);
}

static char * cistrstr(const char *h, const char *n)
{
	size_t i;

	if (!n[0])
		return (char *)h;

	for (; *h; ++h) {
		for (i = 0; n[i] && tolower((unsigned char)n[i]) ==
		            tolower((unsigned char)h[i]); ++i)
			;
		if (n[i] == '\0')
			return (char *)h;
	}
	return NULL;
}

static int drawitem(struct item *item, int x, int y, int w)
{
	if (item == sel)
		drw_setscheme(drw, scheme[SchemeSel]);
	else if (item->out)
		drw_setscheme(drw, scheme[SchemeOut]);
	else
		drw_setscheme(drw, scheme[SchemeNorm]);
	return drw_text(drw, x, y, w, bh, lrpad / 2, item->text, 0);
}

static void drawmenu(void)
{
	unsigned int curpos;
	struct item *item;
	int x = 0, y = 0, w;

	drw_setscheme(drw, scheme[SchemeNorm]);
	drw_rect(drw, 0, 0, mw, mh, 1, 1);

	if (prompt && *prompt) {
		drw_setscheme(drw, scheme[SchemeSel]);
		x = drw_text(drw, x, 0, promptw, bh, lrpad / 2, prompt, 0);
	}

	// draw input field - moved to the main win class
	w = (lines > 0 || !matches) ? mw - x : inputw;
	((DMenuWindow *)drw->win)->updateEditBox(x, 0, w, bh);

	curpos = TEXTW(text) - TEXTW(&text[cursor]);
	if ((curpos += lrpad / 2 - 1) < w) {
		drw_setscheme(drw, scheme[SchemeNorm]);
		drw_rect(drw, x + curpos, 2, 2, bh - 4, 1, 0);
	}

	if (lines > 0) {
		// draw vertical list
		for (item = curr; item != next; item = item->right)
			drawitem(item, x, y += bh, mw - x);
	} else if (matches) {
		// draw horizontal list
		x += inputw;
		w = TEXTW("<");
		if (curr->left) {
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_text(drw, x, 0, w, bh, lrpad / 2, "<", 0);
		}
		x += w;
		for (item = curr; item != next; item = item->right)
			x = drawitem(item, x, 0, textw_clamp(item->text, mw - x - TEXTW(">")));
		if (next) {
			w = TEXTW(">");
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_text(drw, mw - w, 0, w, bh, lrpad / 2, ">", 0);
		}
	}
	drw_map(drw, drw->win, 0, 0, mw, mh);
}


static void grabfocus(void)
{
}

static void grabkeyboard(void)
{
}


static void match(void)
{
	static char **tokv = NULL;
	static int tokn = 0;

	char buf[sizeof text], *s;
	int i, tokc = 0;
	size_t len, textsize;
	struct item *item, *lprefix, *lsubstr, *prefixend, *substrend;

	strcpy(buf, text);
	/* separate input text into tokens to be matched individually */
	for (s = strtok(buf, " "); s; tokv[tokc - 1] = s, s = strtok(NULL, " "))
		if (++tokc > tokn && !(tokv = (char **)realloc(tokv, ++tokn * sizeof *tokv)))
			die("cannot realloc %zu bytes:", tokn * sizeof *tokv);
	len = tokc ? strlen(tokv[0]) : 0;

	matches = lprefix = lsubstr = matchend = prefixend = substrend = NULL;
	textsize = strlen(text) + 1;
	for (item = items; item && item->text; item++) {
		for (i = 0; i < tokc; i++)
			if (!fstrstr(item->text, tokv[i]))
				break;
		if (i != tokc) /* not all tokens match */
			continue;
		/* exact matches go first, then prefixes, then substrings */
		if (!tokc || !fstrncmp(text, item->text, textsize))
			appenditem(item, &matches, &matchend);
		else if (!fstrncmp(tokv[0], item->text, len))
			appenditem(item, &lprefix, &prefixend);
		else
			appenditem(item, &lsubstr, &substrend);
	}
	if (lprefix) {
		if (matches) {
			matchend->right = lprefix;
			lprefix->left = matchend;
		} else
			matches = lprefix;
		matchend = prefixend;
	}
	if (lsubstr) {
		if (matches) {
			matchend->right = lsubstr;
			lsubstr->left = matchend;
		} else
			matches = lsubstr;
		matchend = substrend;
	}
	curr = sel = matches;
	calcoffsets();
}

static void insert(const char *str, ssize_t n)
{
	memcpy(text, str, n + 1);
	match();
}


static size_t nextrune(int inc)
{
	ssize_t n;

	/* return location of next utf8 rune in the given direction (+1 or -1) */
	for (n = cursor + inc; n + inc >= 0 && (text[n] & 0xc0) == 0x80; n += inc);
	return n;
}

static void movewordedge(int dir)
{
	if (dir < 0) { /* move cursor to the start of the word*/
		while (cursor > 0 && strchr(worddelimiters, text[nextrune(-1)]))
			cursor = nextrune(-1);
		while (cursor > 0 && !strchr(worddelimiters, text[nextrune(-1)]))
			cursor = nextrune(-1);
	} else { /* move cursor to the end of the word */
		while (text[cursor] && strchr(worddelimiters, text[cursor]))
			cursor = nextrune(+1);
		while (text[cursor] && !strchr(worddelimiters, text[cursor]))
			cursor = nextrune(+1);
	}
}

static void keypress(QKeyEvent *ev)
{

	QByteArray byteArray = ((DMenuWindow *)drw->win)->getText().toUtf8();
	const char *buf = byteArray.constData();

	int len = ((DMenuWindow *)drw->win)->getText().length();
	int cursorPos = ((DMenuWindow *)drw->win)->getLineEdit()->cursorPosition();
	cursor = cursorPos;

	// Ctrl pressed
	if(ev->modifiers() & Qt::ControlModifier) {
		switch(ev->key()) {
			case Qt::Key_K:
				text[cursor] = '\0';
				match();
				break;
			case Qt::Key_U:
				insert(NULL, 0 - cursor);
				break;
			case Qt::Key_W:
				while (cursor > 0 && strchr(worddelimiters, text[nextrune(-1)]))
					insert(NULL, nextrune(-1) - cursor);
				while (cursor > 0 && !strchr(worddelimiters, text[nextrune(-1)]))
					insert(NULL, nextrune(-1) - cursor);
				break;
			case Qt::Key_Y:
				{
					QClipboard *clipboard = QApplication::clipboard();
					QString text = ((DMenuWindow *)drw->win)->getText();
					clipboard->setText(text);
				}
				return;
			case Qt::Key_Return:
				break;
			case Qt::Key_BracketLeft:
				cleanup();
				exit(1);
			default:
				break;
		}
	}
	// Alt pressed
	else if(ev->modifiers() & Qt::AltModifier) {
		switch(ev->key()) {
			//case Qt::Key_A:
				//movewordedge(-1);
				//goto draw;
			//case Qt::Key_F:
				//movewordedge(+1);
				//goto draw;
			case Qt::Key_J:
				if (!next)
					return;
				sel = curr = next;
				calcoffsets();
				break;
			case Qt::Key_K:
				if (!prev)
					return;
				sel = curr = prev;
				calcoffsets();
				break;
			default:
				break;
		}
	}
	// No modifiers are pressed
	else {
		switch(ev->key()) {
			case Qt::Key_End:
				//if (text[cursor] != '\0') {
				if (cursor == len) {
					cursor = strlen(text);
					break;
				}
				if (next) {
					// jump to end of list and position items in reverse
					curr = matchend;
					calcoffsets();
					curr = prev;
					calcoffsets();
					while (next && (curr = curr->right))
						calcoffsets();
				}
				sel = matchend;
				break;
			case Qt::Key_Escape:
				cleanup();
				exit(1);
			case Qt::Key_Home:
				if (sel == matches) {
					cursor = 0;
					break;
				}
				sel = curr = matches;
				calcoffsets();
				break;
			case Qt::Key_Left:
				if (cursor > 0 && (!sel || !sel->left || lines > 0)) {
					cursor = nextrune(-1);
					break;
				}
				if (lines > 0)
					return;
				// fallthrough
			case Qt::Key_Up:
				if (sel && sel->left && (sel = sel->left)->right == curr) {
					curr = prev;
					calcoffsets();
				}
				break;
			case Qt::Key_Enter:
			case Qt::Key_Return:
				puts((sel && !(ev->modifiers() & Qt::ShiftModifier)) ? sel->text : text);
				if (!(ev->modifiers() & Qt::ControlModifier)) {
					cleanup();
					exit(0);
				}
				if (sel)
					sel->out = 1;
				break;
			case Qt::Key_Right:
				if (text[cursor] != '\0') {
					cursor = nextrune(+1);
					break;
				}
				if (lines > 0)
					return;
				// fallthrough
			case Qt::Key_Down:
				if (sel && sel->right && (sel = sel->right) == next) {
					curr = next;
					calcoffsets();
				}
				break;
			case Qt::Key_Tab:
				if (!sel)
					return;
				cursor = strnlen(sel->text, sizeof text - 1);
				memcpy(text, sel->text, cursor);
				text[cursor] = '\0';
				match();
				break;
			default:
				insert(buf, len);
				break;
		}
	}

draw:
	drawmenu();
}

static void paste(void)
{
}

static void readstdin(void)
{
	char *line = NULL;
	size_t i, itemsiz = 0, linesiz = 0;
	ssize_t len;

	/* read each line from stdin and add it to the item list */
	for (i = 0; (len = getline(&line, &linesiz, stdin)) != -1; i++) {
		if (i + 1 >= itemsiz) {
			itemsiz += 256;
			if (!(items = (struct item *)realloc(items, itemsiz * sizeof(*items))))
				die("cannot realloc %zu bytes:", itemsiz * sizeof(*items));
		}
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';
		if (!(items[i].text = strdup(line)))
			die("strdup:");

		items[i].out = 0;
	}
	free(line);
	if (items)
		items[i].text = NULL;
	lines = MIN(lines, i);
}

static void run(void)
{
}


static void setup(QApplication *app)
{
	int x, y, i, j;
	unsigned int du;
	QWidget w, dw, *dws;
#ifdef XINERAMA
	XineramaScreenInfo *info;
	Window pw;
	int a, di, n, area = 0;
#endif
	// init appearance
	for (j = 0; j < SchemeLast; j++)
		scheme[j] = drw_scm_create(drw, colors[j], 2);

	// calculate menu geometry 
	bh = drw->fonts->h + 2;
	lines = MAX(lines, 0);
	mh = (lines + 1) * bh;
#ifdef XINERAMA
	i = 0;
	if (parentwin == root && (info = XineramaQueryScreens(dpy, &n))) {
		XGetInputFocus(dpy, &w, &di);
		if (mon >= 0 && mon < n)
			i = mon;
		else if (w != root && w != PointerRoot && w != None) {
			// find top-level window containing current input focus
			do {
				if (XQueryTree(dpy, (pw = w), &dw, &w, &dws, &du) && dws)
					XFree(dws);
			} while (w != root && w != pw);
			// find xinerama screen with which the window intersects most
			if (XGetWindowAttributes(dpy, pw, &wa))
				for (j = 0; j < n; j++)
					if ((a = INTERSECT(wa.x, wa.y, wa.width, wa.height, info[j])) > area) {
						area = a;
						i = j;
					}
		}
		// no focused window is on screen, so use pointer location instead
		if (mon < 0 && !area && XQueryPointer(dpy, root, &dw, &dw, &x, &y, &di, &di, &du))
			for (i = 0; i < n; i++)
				if (INTERSECT(x, y, 1, 1, info[i]) != 0)
					break;

		x = info[i].x_org;
		y = info[i].y_org + (topbar ? 0 : info[i].height - mh);
		mw = info[i].width;
		XFree(info);
	} else
#endif
	{
		x = 0;
		y = topbar ? 0 : parentwin->size().height() - mh;
		mw = parentwin->size().width();
	}

	promptw = (prompt && *prompt) ? TEXTW(prompt) - lrpad / 4 : 0;
	inputw = mw / 3; // input width: ~33% of monitor width
	match();

	DMenuWindow *window = new DMenuWindow();
    window->setGeometry(x, y, mw, mh);
	window->setStyleSheet(QString("background-color: %1;").arg(scheme[SchemeNorm][ColBg]->name()));
    window->show();
	drw->win = window;

	drw_resize(drw, mw, mh);
	drawmenu();
}


static void
usage(void)
{
	die("usage: dmenu [-bfiv] [-l lines] [-p prompt] [-fn font] [-m monitor]\n"
	    "             [-nb color] [-nf color] [-sb color] [-sf color] [-w windowid]");
}

int main(int argc, char *argv[])
{
	// XWindowAttributes wa;
	QApplication app(argc, argv);
	int i, fast = 0;

	for (i = 1; i < argc; i++)
		/* these options take no arguments */
		if (!strcmp(argv[i], "-v")) {      /* prints version information */
			puts(DMENU_VERSION);
			exit(0);
		} else if (!strcmp(argv[i], "-b")) /* appears at the bottom of the screen */
			topbar = 0;
		else if (!strcmp(argv[i], "-f"))   /* grabs keyboard before reading stdin */
			fast = 1;
		else if (!strcmp(argv[i], "-i")) { /* case-insensitive item matching */
			fstrncmp = strncasecmp;
			fstrstr = cistrstr;
		} else if (i + 1 == argc)
			usage();
		/* these options take one argument */
		else if (!strcmp(argv[i], "-l"))   /* number of lines in vertical list */
			lines = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-m"))
			mon = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-p"))   /* adds prompt to left of input field */
			prompt = argv[++i];
		else if (!strcmp(argv[i], "-fn"))  /* font or font set */
			fonts[0] = argv[++i];
		else if (!strcmp(argv[i], "-nb"))  /* normal background color */
			colors[SchemeNorm][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-nf"))  /* normal foreground color */
			colors[SchemeNorm][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-sb"))  /* selected background color */
			colors[SchemeSel][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-sf"))  /* selected foreground color */
			colors[SchemeSel][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-w"))   /* embedding window id */
			embed = argv[++i];
		else
			usage();

	if (!setlocale(LC_CTYPE, ""))
		fputs("warning: no locale support\n", stderr);
	
	// Get a pointer to the primary (default) screen
	screen = QGuiApplication::primaryScreen();

	// root = RootWindow(dpy, screen);
	if (const QWindow *window = QGuiApplication::focusWindow()) {
		root = window->screen();
	} else {
		root = screen;
	}

	parentwin = root;
	drw = drw_create(screen, root, screen->size().width(), screen->size().height());
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");
	lrpad = drw->fonts->h;

#ifdef __OpenBSD__
	if (pledge("stdio rpath", NULL) == -1)
		die("pledge");
#endif
	// TODO: check!
	if (fast && !isatty(0)) {
		grabkeyboard();
		readstdin();
	} else {
		readstdin();
		grabkeyboard();
	}

	setup(&app);
	run();


	((DMenuWindow *)drw->win)->focusEditBox();
	return app.exec();
}

#include "qdmenu.moc"
