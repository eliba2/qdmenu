/* See LICENSE file for copyright and license details. */

/* not used in the qt version 
 *
typedef struct {
	Cursor cursor;
} Cur;
*/

typedef struct Fnt {
	unsigned int h;
	QFont *xfont;
	char * pattern;
	struct Fnt *next;
} Fnt;

enum { ColFg, ColBg }; /* Clr scheme index */

typedef struct {
	unsigned int w, h;
	QScreen *screen;
	QScreen *root;
	QPixmap *drawable;
	Fnt *fonts;
	QColor **scheme;
	QWidget *win;
} Drw;

/* Drawable abstraction */
Drw * drw_create(QScreen *screen, QScreen *root, unsigned int w, unsigned int h);
void drw_resize(Drw *drw, unsigned int w, unsigned int h);
void drw_free(Drw *drw);

/* Fnt abstraction */
Fnt *drw_fontset_create(Drw* drw, const char *fonts[], size_t fontcount);
void drw_fontset_free(Fnt* set);
unsigned int drw_fontset_getwidth(Drw *drw, const char *text);
unsigned int drw_fontset_getwidth_clamp(Drw *drw, const char *text, unsigned int n);
void drw_font_getexts(Drw *drw, Fnt *font, const char *text, unsigned int len, unsigned int *w, unsigned int *h);

/* Colorscheme abstraction */
void drw_clr_create(Drw *drw, QColor *dest, const char *clrname);
QColor **drw_scm_create(Drw *drw, const char *clrnames[], size_t clrcount);

/* Cursor abstraction
Cur *drw_cur_create(Drw *drw, int shape);
void drw_cur_free(Drw *drw, Cur *cursor);
*/

/* Drawing context manipulation */
void drw_setfontset(Drw *drw, Fnt *set);
void drw_setscheme(Drw *drw, QColor **scm);

/* Drawing functions */
void drw_rect(Drw *drw, int x, int y, unsigned int w, unsigned int h, int filled, int invert);
int drw_text(Drw *drw, int x, int y, unsigned int w, unsigned int h, unsigned int lpad, const char *text, int invert);

/* Map functions */
void drw_map(Drw *drw, QWidget *win, int x, int y, unsigned int w, unsigned int h);
