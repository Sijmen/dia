/* Dia -- an diagram creation/manipulation program
 * Copyright (C) 1998 Alexander Larsson
 *
 * pdf-import.cpp - poppler based PDF import plug-in for Dia
 * Copyright (C) 2012 Hans Breuer <hans@breuer.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include "config.h"

// Dia includes before poppler to avoid getting the wrong 'object.h'
#define Rectangle DiaRectangle
#include "create.h"
#include "properties.h"
#include "attributes.h"
#include "object.h"
#include "diagramdata.h"
#include "pattern.h"
#undef Rectangle

// namespacing poppler to avoid conflict on Object
//#undef OBJECT_H /* should be fixed in POPPLER I think */
#include <poppler/OutputDev.h>
#include <poppler/GfxState.h>
#include <poppler/GfxFont.h>

#include <poppler/GlobalParams.h>
#include <poppler/PDFDocFactory.h>

#include <vector>

/*!
 * \brief A Poppler output device turning PDF to _DiaObject
 *
 * Pretty straight translation of poppler/poppler/CairoOutputDev.cc
 * to _DiaObject semantics. A lot of things in PDF can not be easily
 * mapped to Dia capabilities, so this will stay incomplete for a while.
 */
class DiaOutputDev : public OutputDev
{
public :
  //! Does this device use upside-down coordinates?
  GBool upsideDown() { return gTrue; }
  //! Does this device use drawChar() or drawString()?
  GBool useDrawChar() { return gFalse; }
  //! Type 3 font support?
  GBool interpretType3Chars() { return gFalse; }
  //! Overwrite for single page support??
  GBool checkPageSlice (Page *page, double hDPI, double vDPI,
			int rotate, GBool useMediaBox, GBool crop,
			int sliceX, int sliceY, int sliceW, int sliceH,
			GBool printing,
			GBool (* abortCheckCbk)(void *data),
			void * abortCheckCbkData,
			GBool (*annotDisplayDecideCbk)(Annot *annot, void *user_data),
			void *annotDisplayDecideCbkData)
  {
    PDFRectangle *mediaBox = page->getMediaBox(); 
    PDFRectangle *clipBox = page->getCropBox ();

    if (page->isOk()) {
      real w1 = (clipBox->x2 - clipBox->x1);
      real h1 = (clipBox->y2 - clipBox->y1);
      real w2 = (mediaBox->x2 - mediaBox->x1);
      real h2 = (mediaBox->y2 - mediaBox->y1);

      if (w2 > w1 || h2 > h1) {
        page_width = w2 * scale;
        page_height = h2 * scale;
      } else {
        page_width = w1 * scale;
        page_height = h1 * scale;
      }
      // Check to see if a page slice should be displayed.  If this
      // returns false, the page display is aborted.  Typically, an
      // OutputDev will use some alternate means to display the page
      // before returning false.
      // At least so documentation says, but I've found no OutputDev
      // actually following this;)
      return gTrue;
    }
    return gFalse;
  }
  //! Apparently no effect at all - so we translate everything to Dia space ouself
  void setDefaultCTM(double *ctm)
  {
    DiaMatrix mat;

    mat.xx = ctm[0];
    mat.yx = ctm[1];
    mat.xy = ctm[2];
    mat.yy = ctm[3];
    mat.x0 = ctm[4] * scale;
    mat.y0 = ctm[5] * scale;
    this->matrix = mat;

    OutputDev::setDefaultCTM (ctm);
  }

  void updateCTM(GfxState *state,
		 double m11, double m12,
		 double m21, double m22,
		 double m31, double m32)
  {
    double *ctm = getDefCTM();
    DiaMatrix mat;

    mat.xx = m11;
    mat.yx = m12;
    mat.xy = m21;
    mat.yy = m22;
    mat.x0 = m31 * scale;
    mat.y0 = m32 * scale;

    //this->matrix = mat;
    dia_matrix_multiply (&this->matrix, &mat, &this->matrix);

    updateLineDash(state);
    updateLineJoin(state);
    updateLineCap(state);
    updateLineWidth(state);
  }

  //! 
  void updateLineWidth(GfxState *state)
  {
    this->line_width = state->getLineWidth() * scale;
  }
  void updateLineDash(GfxState * state)
  { 
    double *dashPattern;
    int dashLength;
    double dashStart;

    state->getLineDash(&dashPattern, &dashLength, &dashStart);
    this->dash_length = dashLength ? dashPattern[0] * scale : 1.0;
    if (dashLength == 0)
      this->line_style = LINESTYLE_SOLID;
    else if (dashLength > 5)
      this->line_style = LINESTYLE_DASH_DOT_DOT;
    else if (dashLength > 3)
      this->line_style = LINESTYLE_DASH_DOT;
    else if (dashLength > 1) {
      if (dashPattern[0] != dashPattern[1])
        this->line_style = LINESTYLE_DOTTED;
      else
        this->line_style = LINESTYLE_DASHED;
    }
  }
  void updateLineJoin(GfxState * state)
  {
    if (state->getLineJoin() == 0)
      this->line_join = LINEJOIN_MITER;
    else if (state->getLineJoin() == 1)
      this->line_join = LINEJOIN_ROUND;
    else
      this->line_join = LINEJOIN_BEVEL;
  }
  void updateLineCap(GfxState *state)
  {
    if (state->getLineCap() == 0)
      this->line_caps = LINECAPS_BUTT;
    else if (state->getLineCap() == 1)
      this->line_caps = LINECAPS_ROUND;
    else
      this->line_caps = LINECAPS_PROJECTING;
  }
  void updateStrokeColor(GfxState *state)
  {
    GfxRGB color;

    state->getStrokeRGB(&color);
    this->stroke_color.red = colToDbl(color.r);
    this->stroke_color.green = colToDbl(color.g);
    this->stroke_color.blue = colToDbl(color.b);
  }
  void updateStrokeOpacity(GfxState *state)
  { 
    this->stroke_color.alpha = state->getStrokeOpacity();
  }  
  void updateFillColor(GfxState *state)
  {
    GfxRGB color;

    // if we have a pattern in use forget it
    if (this->pattern) {
      g_object_unref (this->pattern);
      this->pattern = NULL;
    }
    state->getFillRGB(&color);
    this->fill_color.red = colToDbl(color.r);
    this->fill_color.green = colToDbl(color.g);
    this->fill_color.blue = colToDbl(color.b);
  }
  void updateFillOpacity(GfxState *state)
  { 
    this->fill_color.alpha = state->getFillOpacity();
  }
  //! gradients are just emulated - but not if returning false here
  GBool useShadedFills(int type) { return type < 4; }
  GBool useFillColorStop() { return gTrue; }
  //! follow the CairoOutputDev pattern once more
  GBool axialShadedSupportExtend(GfxState *state, GfxAxialShading *shading)
  {
    return (shading->getExtend0() == shading->getExtend1());
  }

  void updateFillColorStop(GfxState * state, double offset)
  {
    GfxRGB color;
    Color fill = this->fill_color;

    state->getFillRGB(&color);
    fill.red = colToDbl(color.r);
    fill.green = colToDbl(color.g);
    fill.blue = colToDbl(color.b);

    g_return_if_fail (this->pattern != NULL);
    dia_pattern_add_color (this->pattern, offset, &fill);
  }
  GBool axialShadedFill(GfxState *state, GfxAxialShading *shading, double tMin, double tMax)
  {
    double x0, y0, x1, y1;
    double dx, dy;

    shading->getCoords(&x0, &y0, &x1, &y1);
    x0 *= scale;
    y0 *= scale;
    x1 *= scale;
    y1 *= scale;
    dx = x1 - x0;
    dy = y1 - y0;

    if (this->pattern)
      g_object_unref (this->pattern);
    this->pattern = dia_pattern_new (DIA_LINEAR_GRADIENT, DIA_PATTERN_USER_SPACE,
				     x0 + tMin * dx, y0 + tMin * dy);
    dia_pattern_set_point (this->pattern, x0 + tMax * dx, y0 + tMax * dy);
    // continue with updateFillColorStop calls
    // although wasteful, because Poppler samples these to 256 entries 
    return gFalse;
  }
  GBool radialShadedSupportExtend(GfxState *state, GfxRadialShading *shading)
  {
    return (shading->getExtend0() == shading->getExtend1());
  }
  GBool radialShadedFill(GfxState *state, GfxRadialShading *shading, double sMin, double sMax)
  {
    double x0, y0, r0, x1, y1, r1;
    double dx, dy, dr;

    shading->getCoords(&x0, &y0, &r0, &x1, &y1, &r1);
    x0 *= scale;
    y0 *= scale;
    x1 *= scale;
    y1 *= scale;
    r0 *= scale;
    r1 *= scale;
    dx = x1 - x0;
    dy = y1 - y0;
    dr = r1 - r0;

    if (this->pattern)
      g_object_unref (this->pattern);
    this->pattern = dia_pattern_new (DIA_RADIAL_GRADIENT, DIA_PATTERN_USER_SPACE,
				     x0 + sMax * dx, y0 + sMax * dy);
    dia_pattern_set_radius (this->pattern, r0 + sMax * dr);
    dia_pattern_set_point (this->pattern, x0 + sMin * dx, y0 + sMin * dy);
    // continue with updateFillColorStop calls
    // although wasteful, because Poppler samples these to 256 entries 
    return gFalse;
  }
  void updateBlendMode(GfxState *state)
  {
    if (state->getBlendMode() != gfxBlendNormal)
      g_print ("BlendMode %d\n", state->getBlendMode());
  }

  void updateFont(GfxState * state)
  {
    DiaFont *font;
      
    // without a font it wont make sense
    if (!state->getFont())
      return;
    //FIXME: Dia is really unhappy about zero size fonts
    if (!(state->getFontSize() > 0.0))
      return;
    GfxFont *f = state->getFont();

    // instead of building the same font over and over again
    if (g_hash_table_lookup (this->font_map, f)) {
      ++font_map_hits;
      return;
    }

    DiaFontStyle style = (f->isSerif() ? DIA_FONT_SERIF : DIA_FONT_SANS)
		       | (f->isItalic() ? DIA_FONT_ITALIC : DIA_FONT_NORMAL)
		          // mapping all the font weights is just too much code for now ;)
		       | (f->isBold () ? DIA_FONT_BOLD : DIA_FONT_WEIGHT_NORMAL);
    gchar *family = g_strdup (f->getFamily() ? f->getFamily()->getCString() : "sans");

    double *mat = state->getTextMat();

    // we are (not anymore) building the same font over and over again
    g_print ("Font 0x%08x: '%s' size=%g (* %g)\n", f, family, state->getFontSize(), mat[3]);

    // now try to make a fontname Dia/Pango can cope with
    // strip style postfix - we already have extracted the style bits above
    char *pf;
    if ((pf = strstr (family, " Regular")) != NULL)
      *pf = 0;
    if ((pf = strstr (family, " Bold")) != NULL)
      *pf = 0;
    if ((pf = strstr (family, " Italic")) != NULL)
      *pf = 0;
    if ((pf = strstr (family, " Oblique")) != NULL)
      *pf = 0;

    if (mat[3] > 0.0)
      font = dia_font_new (family, style, state->getFontSize() * mat[3] * scale / 0.8);
    else // bug: in Gfx::opShowSpaceText() missing matrix setup?
      font = dia_font_new (family, style, state->getFontSize() / 0.8);

    g_hash_table_insert (this->font_map, f, font);
    g_free (family);
  }
  void updateTextShift(GfxState *state, double shift)
  {
    // Return the writing mode (0=horizontal, 1=vertical)
    // state->getFont()->getWMode()
#if 0
    if (shift == 0) //FIXME:
      this->alignment = ALIGN_LEFT;
    else
      this->alignment = ALIGN_CENTER;
#endif
  }
  void saveState(GfxState *state)
  {
    // just rember the matrix for now
    this->matrices.push_back (this->matrix);
  }
  //! Change back to the old state
  void restoreState(GfxState *state)
  {
    // just restore the matrix for now
    this->matrices.pop_back();
    this->matrix = this->matrices.back();
    // should contain all of our update*() methods?
    updateLineWidth(state);
    updateLineDash(state);
    updateLineJoin(state);
    updateLineCap(state);
    updateStrokeColor(state);
    updateStrokeOpacity(state);
    updateFillColor(state);
    updateFillOpacity(state);
    updateFont(state);
  }

  void stroke (GfxState *state);
  void fill (GfxState *state);
  void eoFill (GfxState *state);

  void drawString(GfxState *, GooString *);

  void drawImage(GfxState *state, Object *ref, Stream *str,
		 int width, int height, GfxImageColorMap *colorMap,
		 GBool interpolate, int *maskColors, GBool inlineImg);

  
  //! everything on a single page it put into a Dia Group
  void startPage(int pageNum, GfxState *state)
  {
    this->pageNum = pageNum;
  }
  //! push the current list of objects to Dia
  void endPage()
  {
    DiaObject *group;
    g_return_if_fail (objects != NULL);

    /* approx 4:3 page distribution */
    int m = (int)sqrt (num_pages / 0.75);
    if (m < 2)
      m = 2;
    gchar *name = g_strdup_printf (_("Page %d"), this->pageNum);
    group = create_standard_group (this->objects);
    this->objects = NULL; // Group eats list
    // page advance
    Point advance = { this->page_width * ((this->pageNum - 1) % m),
		      this->page_height * ((this->pageNum - 1) / m)};
    advance.x += group->position.x;
    advance.y += group->position.y;
    group->ops->move (group, &advance);
    layer_add_object (this->dia->active_layer, group);
    dia_object_set_meta (group, "name", name);
    g_free (name);
  }

  //! construtor
  DiaOutputDev (DiagramData *_dia, int n);
  //! destrutor
  ~DiaOutputDev ();
private :
  void _fill (GfxState *state, bool winding);

  bool doPath (GArray *points, GfxState *state, GfxPath *path, bool &haveClose);
  void applyStyle (DiaObject *obj, bool fill);
  void addObject (DiaObject *obj);

  DiagramData *dia;

  Color stroke_color;
  real line_width;
  LineStyle line_style;
  real dash_length;
  LineJoin line_join;
  LineCaps line_caps;
  Color fill_color;

  Alignment alignment;

  // multiply with to get from PDF to Dia
  real scale;
  //! list of DiaObject not yet added
  GList *objects;
  //! just the number got from poppler
  int pageNum;
  //! already translated to Dia space, too
  real page_width;
  //! same for height
  real page_height;
  //! the total number of pages
  int num_pages;

  // GfxFont * -> DiaFont *
  GHashTable *font_map;
  //! statistics of the font_map
  guint font_map_hits;
  //! active matrix
  DiaMatrix matrix;
  //! stack of matrix
  std::vector<DiaMatrix> matrices;
  //! radial or linear gradient fill
  DiaPattern *pattern;
  //! cache for already created pixbufs
  GHashTable *image_cache;
};

DiaOutputDev::DiaOutputDev (DiagramData *_dia, int _n) :
  dia(_dia),
  stroke_color(attributes_get_foreground ()),
  line_width(attributes_get_default_linewidth()),
  // favoring member intitialization list over attributes_get_default_line_style()
  line_style(LINESTYLE_SOLID),
  dash_length(1.0),
  line_join(LINEJOIN_MITER),
  line_caps(LINECAPS_PROJECTING),
  fill_color(attributes_get_background ()),
  alignment(ALIGN_LEFT),
  scale(2.54/72.0),
  objects(NULL),
  pageNum(0),
  page_width(1.0),
  page_height(1.0),
  num_pages(_n),
  font_map_hits(0),
  pattern(NULL)
{
  font_map = g_hash_table_new_full(g_direct_hash, g_direct_equal,
				   NULL, (GDestroyNotify)dia_font_unref);
  matrix.xx = matrix.yy = 1.0;
  matrix.yx = matrix.xy = 0.0;
  matrix.x0 = matrix.y0 = 0.0;

  image_cache = g_hash_table_new_full(g_direct_hash, g_direct_equal,
				      NULL, (GDestroyNotify)g_object_unref);
}
DiaOutputDev::~DiaOutputDev ()
{
  g_print ("Fontmap hits=%d, misses=%d\n", font_map_hits, g_hash_table_size (font_map));
  g_hash_table_destroy (font_map);
  if (pattern)
    g_object_unref (pattern);
  g_hash_table_destroy (image_cache);
}
static void
_path_moveto (GArray *path, const Point *pt)
{
  BezPoint bp;
  bp.type = BezPoint::BEZ_MOVE_TO;
  bp.p1 = *pt;
  g_array_append_val (path, bp);
}
static void
_path_lineto (GArray *path, const Point *pt)
{
  BezPoint bp;
  bp.type = BezPoint::BEZ_LINE_TO;
  bp.p1 = *pt;
  g_array_append_val (path, bp);
}

bool 
DiaOutputDev::doPath (GArray *points, GfxState *state, GfxPath *path, bool &haveClose)
{
  int i, j;
  Point start;
  haveClose = false;

  for (i = 0; i < path->getNumSubpaths(); ++i) {
    GfxSubpath *subPath = path->getSubpath(i);

    if (subPath->getNumPoints() < 2)
      continue;

    Point cp; // current point

    cp.x = subPath->getX(0) * scale;
    cp.y = subPath->getY(0) * scale;
    start = cp;
    transform_point (&cp, &this->matrix);
    _path_moveto (points, &cp);
    for (j = 1; j < subPath->getNumPoints(); ) {
      if (subPath->getCurve(j)) {
        BezPoint bp;

        bp.type = BezPoint::BEZ_CURVE_TO;
        bp.p1.x = subPath->getX(j) * scale;
        bp.p1.y = subPath->getY(j) * scale;
        bp.p2.x = subPath->getX(j+1) * scale;
        bp.p2.y = subPath->getY(j+1) * scale;
        bp.p3.x = subPath->getX(j+2) * scale;
        bp.p3.y = subPath->getY(j+2) * scale;
        cp = bp.p3;
        transform_bezpoint (&bp, &this->matrix);
        g_array_append_val (points, bp);
        j += 3;
      } else {
        cp.x = subPath->getX(j) * scale;
        cp.y = subPath->getY(j) * scale;
        transform_point (&cp, &this->matrix);
        _path_lineto (points, &cp);
        j += 1;
      }
    } // foreach point in subpath
    if (subPath->isClosed()) {
      // add an extra point just to be sure
      transform_point (&start, &this->matrix);
      _path_lineto (points, &start);
      haveClose = true;
    }
  } // foreach subpath
  return (i > 0);
}
/*!
 * \brief Apply the current style properties to the given object
 */
void
DiaOutputDev::applyStyle (DiaObject *obj, bool fill)
{
  GPtrArray *plist = g_ptr_array_new ();


  if (!fill) {
    prop_list_add_line_width (plist, this->line_width);
    prop_list_add_line_style (plist, this->line_style, this->dash_length);
    prop_list_add_line_colour (plist, &this->stroke_color);
  } else {
    prop_list_add_line_width (plist, 0);
    prop_list_add_line_colour (plist, &this->fill_color);
    prop_list_add_fill_colour (plist, &this->fill_color);
  }
  prop_list_add_show_background (plist, fill ? TRUE : FALSE);
  // using the "Standard - Path" internal enum values here is a bit dirty
  prop_list_add_enum (plist, "stroke_or_fill", fill ? 0x2 : 0x1);
  obj->ops->set_props (obj, plist);
  prop_list_free (plist);
}

void
DiaOutputDev::addObject (DiaObject *obj)
{
  g_return_if_fail (this->dia != NULL);

  this->objects = g_list_append (this->objects, obj);
}
/*!
 * \brief create a _Bezierline or _StdPath from the graphics state
 */
void
DiaOutputDev::stroke (GfxState *state)
{
  GArray *points = g_array_new (FALSE, FALSE, sizeof(BezPoint));
  DiaObject *obj = NULL;
  GfxPath *path = state->getPath();
  bool haveClose = false;

  if (doPath (points, state, path, haveClose) && points->len > 1) {
    if (path->getNumSubpaths() == 1) {
      if (!haveClose)
        obj = create_standard_bezierline (points->len, &g_array_index (points, BezPoint, 0), NULL, NULL);
      else
        obj = create_standard_beziergon (points->len, &g_array_index (points, BezPoint, 0));
    } else {
      obj = create_standard_path (points->len, &g_array_index (points, BezPoint, 0));
    }
    applyStyle (obj, false);
  }
  g_array_free (points, TRUE);
  if (obj)
    addObject (obj);
}
/*!
 */
void
DiaOutputDev::_fill (GfxState *state, bool winding)
{
  GArray *points = g_array_new (FALSE, FALSE, sizeof(BezPoint));
  DiaObject *obj = NULL;
  GfxPath *path = state->getPath();
  bool haveClose = true;

  if (doPath (points, state, path, haveClose) && points->len > 2) {
    if (path->getNumSubpaths() == 1 && haveClose)
      obj = create_standard_beziergon (points->len, &g_array_index (points, BezPoint, 0));
    else
      obj = create_standard_path (points->len, &g_array_index (points, BezPoint, 0));
    applyStyle (obj, true);
    if (this->pattern) {
      ObjectChange *change = dia_object_set_pattern (obj, this->pattern);
      if (change) {
	change->free (change);
	g_free (change);
      }
    }
  }
  g_array_free (points, TRUE);
  if (obj) {
    // Useful for debugging but high performance penalty 
    // dia_object_set_meta (obj, "fill-rule", winding ? "winding" : "even-odd");
    addObject (obj);
  }
}

/*!
 * \brief create a _BezierShape or _StdPath from the graphics state
 * \todo respect FILL_RULE_WINDING
 */
void
DiaOutputDev::fill (GfxState *state)
{
  _fill (state, true);
}
/*!
 * \brief create a _Beziergon or _StdPath from the graphics state
 * \todo respect FILL_RULE_EVEN_ODD
 */
void
DiaOutputDev::eoFill (GfxState *state)
{
  _fill (state, false);
}

/*!
 * \brief Draw a string to _Textobj
 * To get objects more similar to what we had during export we
 * should probably use TextOutputDev. It reassembles strings
 * based on their position on the page. Or maybe Dia/cairo should
 * stop realigning single glyphs in it's output?
 * \todo Check alignment options - it's just guessed yet.
 */
void 
DiaOutputDev::drawString(GfxState *state, GooString *s)
{
  Color text_color = this->fill_color;
  int len = s->getLength();
  DiaObject *obj;
  gchar *utf8 = NULL;
  DiaFont *font;

  // ignore empty strings
  if (len == 0)
    return;
  // get the font
  if (!state->getFont())
    return;
  if (!(state->getFontSize() > 0.0))
    return;
  font = (DiaFont *)g_hash_table_lookup (this->font_map, state->getFont());

  // we have to decode the string data first
  {
    GfxFont *f = state->getFont();
    char *p = s->getCString();
    CharCode code;
    int   j = 0, m, n;
    utf8 = g_new (gchar, len * 6 + 1);
    Unicode *u; 
    int uLen;
    double dx, dy, ox, oy;

    while (len > 0) {
      n = f->getNextChar(p, len, &code,
			    &u, &uLen,
			    &dx, &dy, &ox, &oy);
      p += n;
      len -= n;
      m = g_unichar_to_utf8 (u[0], &utf8[j]);
      j += m;
    }
    utf8[j] = '\0';
  }

  // check for invisible text -- this is used by Acrobat Capture
  if (state->getRender() == 3)
    text_color.alpha = 0.0;

  double *mat = state->getTextMat();
  double tx = state->getCurX();
  double ty = state->getCurY();
  obj = create_standard_text (tx * scale, page_height - ty * scale);
  //not applyStyle (obj, TEXT);
  GPtrArray *plist = g_ptr_array_new ();
  // the "text" property is special, it must be initialized with text 
  // attributes, too. So here it comes first to avoid overwriting
  // the other values with defaults.
  prop_list_add_text (plist, "text", utf8);
  prop_list_add_font (plist, "text_font", font);
  prop_list_add_text_colour (plist, &text_color);
  prop_list_add_enum (plist, "text_alignment", this->alignment);
  if (mat[3] > 0.0)
    prop_list_add_fontsize (plist, "text_height", state->getFontSize() * mat[3] * scale / 0.8);
  else
    prop_list_add_fontsize (plist, "text_height", state->getFontSize() / 0.8);
  obj->ops->set_props (obj, plist);
  prop_list_free (plist);
  g_free (utf8);

  addObject (obj);
}

/*!
 * \brief Draw an image to Dia's _Image
 * \todo use maskColors to have some alpha support
 */
void
DiaOutputDev::drawImage(GfxState *state, Object *ref, Stream *str,
			int width, int height, GfxImageColorMap *colorMap,
			GBool interpolate, int *maskColors, GBool inlineImg)
{
  DiaObject *obj;
  GdkPixbuf *pixbuf;
  Point pos;
  ObjectChange *change;
  double *ctm = state->getCTM();

  pos.x = ctm[4] * scale;
  // there is some undocumented magic done with the ctm for drawImage
  // deduced from SplashOutputDev::drawImage()
  // cmt[2] and ctm[3] being negative - use that for y postion
  // ctm[0] and ctm[3] have width and height in device coordinates
  pos.y = (ctm[5] + ctm[3]) * scale;

  // rather than creating the image over and over again we try to cache them
  // via the given 'ref' object
  if ((pixbuf = static_cast<GdkPixbuf*>(g_hash_table_lookup (this->image_cache, ref))) != NULL) {
    g_object_ref (pixbuf);
  } else {

    pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, maskColors ? TRUE : FALSE, 8, width, height);

    {
       // 3 channels, 8 bit
      ImageStream imgStr(str, width, colorMap->getNumPixelComps(), colorMap->getBits());
      Guchar *line;
      int rowstride = gdk_pixbuf_get_rowstride (pixbuf);
      guchar *pixels = gdk_pixbuf_get_pixels (pixbuf);
      int y;

      imgStr.reset(); // otherwise getLine() is crashing right away
      line = imgStr.getLine ();
      for (y = 0; y < height && line; ++y) {
	guchar *dest = pixels + y * rowstride;

	colorMap->getRGBLine (line, dest, width);

	// ToDo: respect maskColors

	line = imgStr.getLine ();
      }
    }
    // insert the new image into our cache
    g_hash_table_insert (this->image_cache, ref, g_object_ref (pixbuf));
  }
  obj = create_standard_image (pos.x, pos.y, 
			       ctm[0]  * scale,
			       ctm[3]  * scale, NULL);
  if ((change = dia_object_set_pixbuf (obj, pixbuf)) != NULL) {
    change->free (change);
    g_free (change);
  }

  g_object_unref (pixbuf);

  addObject (obj);
}

extern "C"
gboolean
import_pdf(const gchar *filename, DiagramData *dia, DiaContext *ctx, void* user_data)
{
  PDFDoc *doc;
  GooString *fileName = new GooString(filename);
  // no passwords yet
  GooString *ownerPW = NULL;
  GooString *userPW = NULL;
  gboolean ret = FALSE;

  // without this we will get strange crashes (at least with /O2 build)
  globalParams = new GlobalParams();

  doc = PDFDocFactory().createPDFDoc(*fileName, ownerPW, userPW);
  if (!doc->isOk()) {
    dia_context_add_message (ctx, _("PDF document not OK.\n%s"),
			     dia_context_get_filename (ctx));
  } else {
    DiaOutputDev *diaOut = new DiaOutputDev(dia, doc->getNumPages());

    for (int pg = 1; pg <= doc->getNumPages(); ++pg) {
      Page *page = doc->getPage (pg);
      if (!page || !page->isOk())
        continue;
      doc->displayPage(diaOut, pg,
		       72.0, 72.0, /* DPI, scaling elsewhere */
		       0, /* rotate */
		       gTrue, /* useMediaBox */
		       gTrue, /* Crop */
		       gFalse /* printing */
		       );
    }
    delete diaOut;
    ret = TRUE;
  }
  delete doc;
  delete globalParams;
  delete fileName;

  return ret;
}
