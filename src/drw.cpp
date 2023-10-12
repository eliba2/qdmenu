/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <QScreen>
#include <QWidget>
#include <QRect>
#include <QFont>
#include <QColor>
#include <QPixmap>
#include <QPainter>
#include <QFontDatabase>
#include <QDebug>

#include "drw.h"
#include "util.h"

#define UTF_INVALID 0xFFFD
#define UTF_SIZ     4

static const unsigned char utfbyte[UTF_SIZ + 1] = {0x80,    0, 0xC0, 0xE0, 0xF0};
static const unsigned char utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
static const long utfmin[UTF_SIZ + 1] = {       0,    0,  0x80,  0x800,  0x10000};
static const long utfmax[UTF_SIZ + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

static long utf8decodebyte(const char c, size_t *i)
{
	for (*i = 0; *i < (UTF_SIZ + 1); ++(*i))
		if (((unsigned char)c & utfmask[*i]) == utfbyte[*i])
			return (unsigned char)c & ~utfmask[*i];
	return 0;
}

static size_t utf8validate(long *u, size_t i)
{
	if (!BETWEEN(*u, utfmin[i], utfmax[i]) || BETWEEN(*u, 0xD800, 0xDFFF))
		*u = UTF_INVALID;
	for (i = 1; *u > utfmax[i]; ++i)
		;
	return i;
}

static size_t utf8decode(const char *c, long *u, size_t clen)
{
	size_t i, j, len, type;
	long udecoded;

	*u = UTF_INVALID;
	if (!clen)
		return 0;
	udecoded = utf8decodebyte(c[0], &len);
	if (!BETWEEN(len, 1, UTF_SIZ))
		return 1;
	for (i = 1, j = 1; i < clen && j < len; ++i, ++j) {
		udecoded = (udecoded << 6) | utf8decodebyte(c[i], &type);
		if (type)
			return j;
	}
	if (j < len)
		return 0;
	*u = udecoded;
	utf8validate(u, len);

	return len;
}

Drw * drw_create(QScreen *screen, QScreen *root, unsigned int w, unsigned int h)
{
	QPixmap* pixmap= new QPixmap(w, h);
	QPainter painter(pixmap);

	// Set line attributes
	QPen pen;
	pen.setWidth(1);
	pen.setStyle(Qt::SolidLine);
	pen.setCapStyle(Qt::FlatCap);
	pen.setJoinStyle(Qt::MiterJoin);
	// Apply the pen to the QPainter
	painter.setPen(pen);

	Drw *drw = (Drw *)ecalloc(1, sizeof(Drw));
	drw->screen = screen;
	drw->root = root;
	drw->w = w;
	drw->h = h;
	drw->drawable = pixmap;
	return drw;
}

void drw_resize(Drw *drw, unsigned int w, unsigned int h)
{
	if (!drw)
		return;

	drw->w = w;
	drw->h = h;
	if (drw->drawable) {
		drw->drawable->scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	} else {
		QPixmap pixmap(w, h);
		QPainter painter(&pixmap);

		// Set line attributes
		QPen pen;
		pen.setWidth(1);
		pen.setStyle(Qt::SolidLine);
		pen.setCapStyle(Qt::FlatCap);
		pen.setJoinStyle(Qt::MiterJoin);
		painter.setPen(pen);
		painter.end();
		drw->drawable = &pixmap;
	}
}

void drw_free(Drw *drw)
{
	drw_fontset_free(drw->fonts);
	free(drw);
}

/* This function is an implementation detail. Library users should use
 * drw_fontset_create instead.
 */
static Fnt * xfont_create(Drw *drw, const char *fontname, char *fontpattern)
{
	Fnt *font;
	QFont *xfont = NULL;
	char *pattern = NULL;

	if (fontname) {
		xfont = new QFont(fontname);
		if (!(QFontDatabase::families().contains(xfont->family()))) {
			qDebug() << "Font familiy not available, using default " << xfont->family();
		}
	} else if (fontpattern) {
		// not supported atm 
		xfont = new QFont(fontname);
		qDebug() << "Pattern not supported, using default " << xfont->family();
	} else {
		die("no font specified.");
	}

	QFontMetrics metrics(*xfont);
	font = (Fnt *)ecalloc(1, sizeof(Fnt));
	font->xfont = xfont;
	font->pattern = pattern;
	font->h = metrics.ascent() + metrics.descent();

	return font;
}

static void xfont_free(Fnt *font)
{
	if (!font)
		return;
	free(font);
}

Fnt* drw_fontset_create(Drw* drw, const char *fonts[], size_t fontcount) 
{
	Fnt *cur, *ret = NULL;
	size_t i;

	if (!drw || !fonts)
		return NULL;

	for (i = 1; i <= fontcount; i++) {
		if ((cur = xfont_create(drw, fonts[fontcount - i], NULL))) {
			cur->next = ret;
			ret = cur;
		}
	}
	return (drw->fonts = ret);
}

void drw_fontset_free(Fnt *font)
{
	if (font) {
		drw_fontset_free(font->next);
		xfont_free(font);
	}
}

void drw_clr_create(Drw *drw, QColor **dest, const char *clrname)
{
	if (!drw || !dest || !clrname)
		return;
	*dest = new QColor(clrname);
}

/* Wrapper to create color schemes. The caller has to call free(3) on the
 * returned color scheme when done using it. */
QColor ** drw_scm_create(Drw *drw, const char *clrnames[], size_t clrcount)
{
	size_t i;
	QColor **retColors = (QColor **)ecalloc(clrcount, sizeof(QColor));
	QColor *ret;
	/* need at least two colors for a scheme */
	if (!drw || !clrnames || clrcount < 2)
		return NULL;

	for (i = 0; i < clrcount; i++) {
		ret = (QColor *)ecalloc(1, sizeof(QColor));
		if (!ret)
			return NULL;
		drw_clr_create(drw, &ret, clrnames[i]);
		retColors[i] = ret;
	}
	return retColors;
}

void drw_setfontset(Drw *drw, Fnt *set)
{
	if (drw)
		drw->fonts = set;
}

void drw_setscheme(Drw *drw, QColor **scm)
{
	if (drw)
		drw->scheme = scm;
}

void drw_rect(Drw *drw, int x, int y, unsigned int w, unsigned int h, int filled, int invert)
{
	if (!drw || !drw->scheme)
		return;

	QPainter painter(drw->drawable);
	QColor *fgColor = drw->scheme[ColBg];
	QColor *bgColor = drw->scheme[ColFg];

	if(!invert) {
		painter.setPen(*bgColor);
		painter.setBrush(*bgColor);
	} else {
		painter.setPen(*fgColor);
		painter.setBrush(*fgColor);
	}
	if(filled) {
		painter.fillRect(x, y, w, h, painter.brush());  // Draw filled rectangle
	} else {
		painter.drawRect(x, y, w-1, h-1);  // Draw outlined rectangle
	}
	drw->win->update();
}

int drw_text(Drw *drw, int x, int y, unsigned int w, unsigned int h, unsigned int lpad, const char *text, int invert)
{
	int ty, ellipsis_x = 0;
	unsigned int tmpw, ew, ellipsis_w = 0, ellipsis_len, hash, h0, h1;
	Fnt *usedfont, *curfont, *nextfont;
	int utf8strlen, utf8charlen, render = x || y || w || h;
	long utf8codepoint = 0;
	const char *utf8str;
	int charexists = 0, overflow = 0;
	static unsigned int nomatches[128], ellipsis_width;

	if (!drw || (render && (!drw->scheme || !w)) || !text || !drw->fonts)
		return 0;

	if (!render) {
		w = invert ? invert : ~invert;
	} else {
		QPainter painter(drw->drawable);
		/* inverse video colors for the text: its diffrent than x11 */
		QColor *color = invert ? drw->scheme[ColFg] : drw->scheme[ColBg];
		painter.setPen(*color);
		painter.setBrush(*color);
		painter.fillRect(x, y, w, h, painter.brush());
		drw->win->update();
		x += lpad;
		w -= lpad;
	}

	usedfont = drw->fonts;
	if (!ellipsis_width && render)
		ellipsis_width = drw_fontset_getwidth(drw, "...");

	while (1) {
		ew = ellipsis_len = utf8strlen = 0;
		utf8str = text;
		nextfont = NULL;
		while (*text) {
			utf8charlen = QString::fromUtf8(text).length();
			for (curfont = drw->fonts; curfont; curfont = curfont->next) {
				charexists = 1; //charexists || 1XftCharExists(drw->dpy, curfont->xfont, utf8codepoint);
				if (charexists) {
					drw_font_getexts(drw, curfont, text, utf8charlen, &tmpw, NULL);
					if (ew + ellipsis_width <= w) {
						// keep track where the ellipsis still fits
						ellipsis_x = x + ew;
						ellipsis_w = w - ew;
						ellipsis_len = utf8strlen;
					}

					if (ew + tmpw > w) {
						overflow = 1;
						// called from drw_fontset_getwidth_clamp():
						// it wants the width AFTER the overflow
						//
						if (!render)
							x += tmpw;
						else
							utf8strlen = ellipsis_len;
					} else if (curfont == usedfont) {
						utf8strlen += utf8charlen;
						text += utf8charlen;
						ew += tmpw;
					} else {
						nextfont = curfont;
					}
					break;
				}
			}

			if (overflow || !charexists || nextfont)
				break;
			else
				charexists = 0;
		}

		if (utf8strlen) {
			if (render) {

				QFontMetrics metrics(*usedfont->xfont);
				ty = y + (h - usedfont->h) / 2 + metrics.ascent();

				QPainter painter(drw->drawable);
				QColor *color = !invert ? drw->scheme[ColFg] : drw->scheme[ColBg];
				painter.setPen(*color);  // Set the pen (outline) color to background color
				painter.setBrush(*color);  // Set the brush (fill) color to background color
				painter.setFont(*usedfont->xfont);
				drw->win->update();

				// Draw text
				QString qttext = QString::fromUtf8(utf8str, utf8strlen);
				painter.drawText(x, ty, qttext);
				drw->win->update();
			}
			x += ew;
			w -= ew;
		}
		if (render && overflow)
			drw_text(drw, ellipsis_x, y, ellipsis_w, h, 0, "...", invert);

		if (!*text || overflow) {
			break;
		} else if (nextfont) {
			charexists = 0;
			usedfont = nextfont;
		} else {
			qDebug() << "fallback paint";
		}
	}
	return x + (render ? w : 0);
}

void drw_map(Drw *drw, QWidget *win, int x, int y, unsigned int w, unsigned int h)
{
	if (!drw)
		return;
	drw->win->update();
}

unsigned int drw_fontset_getwidth(Drw *drw, const char *text)
{
	if (!drw || !drw->fonts || !text)
		return 0;
	return drw_text(drw, 0, 0, 0, 0, 0, text, 0);
}

unsigned int drw_fontset_getwidth_clamp(Drw *drw, const char *text, unsigned int n)
{
	unsigned int tmp = 0;
	if (drw && drw->fonts && text && n)
		tmp = drw_text(drw, 0, 0, 0, 0, 0, text, n);
	return MIN(n, tmp);
}

void drw_font_getexts(Drw *drw, Fnt *font, const char *text, unsigned int len, unsigned int *w, unsigned int *h)
{
	if (!font || !text)
		return;

	QPixmap dummy(1, 1);
	QPainter painter(&dummy);
	painter.setFont(*font->xfont);

	// Get the bounding rectangle of the text
	QRect rect = painter.boundingRect(QRect(), Qt::AlignLeft, text);

	if (w) {
		*w = rect.width(); // Width of the text
	}
	if (h) {
		*h = rect.height(); // Height of the text
	}
}

/* 
 * not used in the qt version 
 *
 Cur * drw_cur_create(Drw *drw, int shape)
 void drw_cur_free(Drw *drw, Cur *cursor)
 */
