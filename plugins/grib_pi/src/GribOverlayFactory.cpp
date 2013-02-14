/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  GRIB Object
 * Author:   David Register
 *
 ***************************************************************************
 *   Copyright (C) 2010 by David S. Register   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 ***************************************************************************
 *
 */

#include "wx/wxprec.h"

#ifndef  WX_PRECOMP
  #include "wx/wx.h"
  #include <wx/glcanvas.h>
#endif //precompiled headers

#include <wx/progdlg.h>

#include "GribUIDialog.h"

// Calculates if two boxes intersect. If so, the function returns _ON.
// If they do not intersect, two scenario's are possible:
// other is outside this -> return _OUT
// other is inside this -> return _IN
OVERLAP Intersect( PlugIn_ViewPort *vp, double lat_min, double lat_max, double lon_min,
        double lon_max, double Marge )
{

    if( ( ( vp->lon_min - Marge ) > ( lon_max + Marge ) )
            || ( ( vp->lon_max + Marge ) < ( lon_min - Marge ) )
            || ( ( vp->lat_max + Marge ) < ( lat_min - Marge ) )
            || ( ( vp->lat_min - Marge ) > ( lat_max + Marge ) ) ) return _OUT;

    // Check if other.bbox is inside this bbox
    if( ( vp->lon_min <= lon_min ) && ( vp->lon_max >= lon_max ) && ( vp->lat_max >= lat_max )
            && ( vp->lat_min <= lat_min ) ) return _IN;

    // Boundingboxes intersect
    return _ON;
}

// Is the given point in the vp ??
bool PointInLLBox( PlugIn_ViewPort *vp, double x, double y )
{

    if( x >= ( vp->lon_min ) && x <= ( vp->lon_max ) && y >= ( vp->lat_min )
            && y <= ( vp->lat_max ) ) return TRUE;
    return FALSE;
}

//----------------------------------------------------------------------------------------------------------
//    Grib Overlay Factory Implementation
//----------------------------------------------------------------------------------------------------------
GRIBOverlayFactory::GRIBOverlayFactory( GRIBUIDialog &dlg )
    : m_dlg(dlg), m_Config(dlg.m_OverlayConfig)
{
    m_pGribTimelineRecordSet = NULL;
    m_last_vp_scale = 0.;

    m_bReadyToRender = false;

    for(int i=0; i<GribOverlayConfig::CONFIG_COUNT; i++)
        m_pOverlay[i] = NULL;
}

GRIBOverlayFactory::~GRIBOverlayFactory()
{
    ClearCachedData();
}

void GRIBOverlayFactory::Reset()
{
    m_pGribTimelineRecordSet = NULL;

    ClearCachedData();
    m_bReadyToRender = false;
}

void GRIBOverlayFactory::SetGribTimelineRecordSet( GribTimelineRecordSet *pGribTimelineRecordSet )
{
    Reset();
    m_pGribTimelineRecordSet = pGribTimelineRecordSet;
    m_bReadyToRender = true;

}

void GRIBOverlayFactory::ClearCachedData( void )
{
    //    Clear out the cached bitmaps
    for(int i=0; i<GribOverlayConfig::CONFIG_COUNT; i++) {
        delete m_pOverlay[i];
        m_pOverlay[i] = NULL;
    }
}

bool GRIBOverlayFactory::RenderGLGribOverlay( wxGLContext *pcontext, PlugIn_ViewPort *vp )
{
    m_pdc = NULL;                  // inform lower layers that this is OpenGL render
    return DoRenderGribOverlay( vp );
}

bool GRIBOverlayFactory::RenderGribOverlay( wxDC &dc, PlugIn_ViewPort *vp )
{
#if wxUSE_GRAPHICS_CONTEXT
    wxMemoryDC *pmdc;
    pmdc = wxDynamicCast(&dc, wxMemoryDC);
    wxGraphicsContext *pgc = wxGraphicsContext::Create( *pmdc );
    m_gdc = pgc;
    m_pdc = &dc;
#else
    m_pdc = &dc;
#endif
    return DoRenderGribOverlay( vp );
}

void ConfigIdToGribId(int i, int &idx, int &idy, bool &polar)
{
    idx = idy = -1;
    polar = false;
    switch(i) {
    case GribOverlayConfig::WIND:
        idx = Idx_WIND_VX, idy = Idx_WIND_VY; break;
    case GribOverlayConfig::PRESSURE:
        idx = Idx_PRESS; break;
    case GribOverlayConfig::WAVE:
        idx = Idx_HTSIGW, idy = Idx_WVDIR; polar = true; break;
    case GribOverlayConfig::SEA_TEMPERATURE:
        idx = Idx_SEATEMP; break;
    case GribOverlayConfig::CURRENT:
        idx = Idx_SEACURRENT_VX, idy = Idx_SEACURRENT_VY; break;
    }
}

bool GRIBOverlayFactory::DoRenderGribOverlay( PlugIn_ViewPort *vp )
{
    if( !m_pGribTimelineRecordSet )
        return false;

    //    If the scale has changed, clear out the cached bitmaps
    if( vp->view_scale_ppm != m_last_vp_scale )
        ClearCachedData();

    m_last_vp_scale = vp->view_scale_ppm;

    //     render each type of record
    GribRecord **pGR = m_pGribTimelineRecordSet->m_GribRecordPtrArray;
    wxArrayPtrVoid **pIA = m_pGribTimelineRecordSet->m_IsobarArray;
    
    for(int i=0; i<GribOverlayConfig::CONFIG_COUNT; i++) {
        if((i == GribOverlayConfig::WIND            && !m_dlg.m_cbWind->GetValue()) ||
           (i == GribOverlayConfig::WAVE            && !m_dlg.m_cbWave->GetValue()) ||
           (i == GribOverlayConfig::CURRENT         && !m_dlg.m_cbCurrent->GetValue()) ||
           (i == GribOverlayConfig::PRESSURE        && !m_dlg.m_cbPressure->GetValue()) ||
           (i == GribOverlayConfig::SEA_TEMPERATURE && !m_dlg.m_cbSeaTemperature->GetValue()))
            continue;

        RenderGribBarbedArrows( i, pGR, vp );
        RenderGribIsobar( i, pGR, pIA, vp );
        RenderGribDirectionArrows( i, pGR, vp );
        RenderGribOverlayMap( i, pGR, vp );
        RenderGribNumbers( i, pGR, vp );
    }

    return true;
}

bool GRIBOverlayFactory::CreateGribGLTexture( GribOverlay *pGO, int config, GribRecord *pGR,
                                              PlugIn_ViewPort *vp, int grib_pixel_size,
                                              const wxPoint &porg )
{
    wxPoint pmin;
    GetCanvasPixLL( vp, &pmin, pGR->getLatMin(), pGR->getLonMin() );
    wxPoint pmax;
    GetCanvasPixLL( vp, &pmax, pGR->getLatMax(), pGR->getLonMax() );

    int width = abs( pmax.x - pmin.x )/grib_pixel_size;
    int height = abs( pmax.y - pmin.y )/grib_pixel_size;

    //    Dont try to create enormous GRIB textures
    if( ( width > 512 ) || ( height > 512 ))
        return false;

    unsigned char *data = new unsigned char[width*height*4];

    for( int ipix = 0; ipix < width; ipix++ ) {
        for( int jpix = 0; jpix < height; jpix++ ) {
            wxPoint p;
            p.x = grib_pixel_size*ipix + porg.x;
            p.y = grib_pixel_size*jpix + porg.y;

            double lat, lon;
            GetCanvasLLPix( vp, p, &lat, &lon );

            double v = pGR->getInterpolatedValue(lon, lat);
            unsigned char r, g, b, a;
            if( v != GRIB_NOTDEF ) {
                v = m_Config.CalibrateValue(config, v);
                wxColour c = GetGraphicColor(config, v);
                r = c.Red();
                g = c.Green();
                b = c.Blue();
                a = 220;
            } else {
                r = 255;
                g = 255;
                b = 255;
                a = 0;
            }

            int doff = 4*(jpix*width + ipix);
            /* for some reason r g b values are inverted, but not alpha,
               this fixes it, but I would like to find the actual cause */
            data[doff + 0] = 255-r;
            data[doff + 1] = 255-g;
            data[doff + 2] = 255-b;
            data[doff + 3] = a;
        }
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, texture);

    glPushClientAttrib( GL_CLIENT_PIXEL_STORE_BIT );

    glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
    glPixelStorei( GL_UNPACK_SKIP_PIXELS, 0 );
    glPixelStorei( GL_UNPACK_SKIP_ROWS, 0 );
    glPixelStorei( GL_UNPACK_ROW_LENGTH, width );

    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    glPopClientAttrib();

    delete [] data;

    pGO->m_iTexture = texture;
    pGO->m_width = width;
    pGO->m_height = height;

    return true;
}

wxImage GRIBOverlayFactory::CreateGribImage( int config, GribRecord *pGR,
                                             PlugIn_ViewPort *vp, int grib_pixel_size,
                                             const wxPoint &porg )
{
    wxPoint pmin;
    GetCanvasPixLL( vp, &pmin, pGR->getLatMin(), pGR->getLonMin() );
    wxPoint pmax;
    GetCanvasPixLL( vp, &pmax, pGR->getLatMax(), pGR->getLonMax() );

    int width = abs( pmax.x - pmin.x );
    int height = abs( pmax.y - pmin.y );

    //    Dont try to create enormous GRIB bitmaps
    if( width > 1024 || height > 1024 )
        return wxNullImage;

    //    This could take a while....
    wxImage gr_image( width, height );
    gr_image.InitAlpha();
    
    wxPoint p;
    for( int ipix = 0; ipix < ( width - grib_pixel_size + 1 ); ipix += grib_pixel_size ) {
        for( int jpix = 0; jpix < ( height - grib_pixel_size + 1 ); jpix += grib_pixel_size ) {
            double lat, lon;
            p.x = ipix + porg.x;
            p.y = jpix + porg.y;
            GetCanvasLLPix( vp, p, &lat, &lon );
            
            double v = pGR->getInterpolatedValue(lon, lat);
            if( v != GRIB_NOTDEF ) {
                v = m_Config.CalibrateValue(config, v);
                wxColour c = GetGraphicColor(config, v);
                
                unsigned char r = c.Red();
                unsigned char g = c.Green();
                unsigned char b = c.Blue();
                
                for( int xp = 0; xp < grib_pixel_size; xp++ )
                    for( int yp = 0; yp < grib_pixel_size; yp++ ) {
                        gr_image.SetRGB( ipix + xp, jpix + yp, r, g, b );
                        gr_image.SetAlpha( ipix + xp, jpix + yp, 220 );
                    }
            } else {
                for( int xp = 0; xp < grib_pixel_size; xp++ )
                    for( int yp = 0; yp < grib_pixel_size; yp++ )
                        gr_image.SetAlpha( ipix + xp, jpix + yp, 0 );
            }
        }
    }
    
    return gr_image.Blur( 4 );
}

struct ColorMap {
    int val;
    wxString text;
};

ColorMap CurrentMap[] =
{{0,  _T("#d90000")},  {1, _T("#d92a00")},  {2, _T("#d96e00")},  {3, _T("#d9b200")},
 {4,  _T("#d4d404")},  {5, _T("#a6d906")},  {7, _T("#06d9a0")},  {9, _T("#00d9b0")},
 {12, _T("#00d9c0")}, {15, _T("#00aed0")}, {18, _T("#0083e0")}, {21, _T("#0057e0")},
 {24, _T("#0000f0")}, {27, _T("#0400f0")}, {30, _T("#1c00f0")}, {36, _T("#4800f0")},
 {42, _T("#6900f0")}, {48, _T("#a000f0")}, {56, _T("#f000f0")}};

ColorMap GenericMap[] =
{{0, _T("#00d900")},  {1, _T("#2ad900")},  {2, _T("#6ed900")},  {3, _T("#b2d900")},
 {4, _T("#d4d400")},  {5, _T("#d9a600")},  {7, _T("#d90000")},  {9, _T("#d90040")},
 {12, _T("#d90060")}, {15, _T("#ae0080")}, {18, _T("#8300a0")}, {21, _T("#5700c0")},
 {24, _T("#0000d0")}, {27, _T("#0400e0")}, {30, _T("#0800e0")}, {36, _T("#a000e0")},
 {42, _T("#c004c0")}, {48, _T("#c008a0")}, {56, _T("#c0a008")}};

ColorMap QuickscatMap[] =
{{0, _T("#000000")},  {5, _T("#000000")},  {10, _T("#00b2d9")}, {15, _T("#00d4d4")},
 {20, _T("#00d900")}, {25, _T("#d9d900")}, {30, _T("#d95700")}, {35, _T("#ae0000")},
 {40, _T("#870000")}, {45, _T("#414100")}};

ColorMap SeaTempMap[] =
{{0, _T("#0000d9")},  {1, _T("#002ad9")},  {2, _T("#006ed9")},  {3, _T("#00b2d9")},
 {4, _T("#00d4d4")},  {5, _T("#00d9a6")},  {7, _T("#00d900")},  {9, _T("#95d900")},
 {12, _T("#d9d900")}, {15, _T("#d9ae00")}, {18, _T("#d98300")}, {21, _T("#d95700")},
 {24, _T("#d90000")}, {27, _T("#ae0000")}, {30, _T("#8c0000")}, {36, _T("#870000")},
 {42, _T("#690000")}, {48, _T("#550000")}, {56, _T("#410000")}};

ColorMap *ColorMaps[4] = {CurrentMap, GenericMap, QuickscatMap, SeaTempMap};

enum {
    CURRENT_GRAPHIC_INDEX, GENERIC_GRAPHIC_INDEX, QUICKSCAT_GRAPHIC_INDEX,
    SEATEMP_GRAPHIC_INDEX, CRAIN_GRAPHIC_INDEX
};

wxColour GRIBOverlayFactory::GetGraphicColor(int config, double val_in)
{
    int colormap_index = m_Config.Configs[config].m_iOverlayMapColors;
    ColorMap *map;
    int maplen;

    /* normalize input value */
    double min = m_Config.GetMin(config), max = m_Config.GetMax(config);

    val_in -= min;
    val_in /= max-min;

    switch(colormap_index) {
    case CURRENT_GRAPHIC_INDEX:
        map = CurrentMap;
        maplen = (sizeof CurrentMap) / (sizeof *CurrentMap);
        break;
    case GENERIC_GRAPHIC_INDEX:
        map = GenericMap;
        maplen = (sizeof GenericMap) / (sizeof *GenericMap);
        break;
    case QUICKSCAT_GRAPHIC_INDEX:
        map = QuickscatMap;
        maplen = (sizeof QuickscatMap) / (sizeof *QuickscatMap);
        break;
    case SEATEMP_GRAPHIC_INDEX: 
        map = SeaTempMap;
        maplen = (sizeof SeaTempMap) / (sizeof *SeaTempMap);
        break;
    case CRAIN_GRAPHIC_INDEX:
        return wxColour((unsigned char) val_in * 255, 0, 0 );  break;
    }

    /* normalize map from 0 to 1 */
    double cmax = map[maplen-1].val;

    for(int i=1; i<maplen; i++) {
        double nmapvala = map[i-1].val/cmax;
        double nmapvalb = map[i].val/cmax;
        if(nmapvalb > val_in || i==maplen-1) {
            wxColour b, c;
            c.Set(map[i].text);
            if(m_bGradualColors) {
                b.Set(map[i-1].text);
                double d = (val_in-nmapvala)/(nmapvalb-nmapvala);
                c.Set((1-d)*b.Red()   + d*c.Red(),
                      (1-d)*b.Green() + d*c.Green(),
                      (1-d)*b.Blue()  + d*c.Blue());
            }
            return c;
        }
    }
    return wxColour(0, 0, 0); /* unreachable */

}

wxImage &GRIBOverlayFactory::getLabel(double value)
{
    std::map <double, wxImage >::iterator it;
    it = m_labelCache.find(value);
    if (it != m_labelCache.end())
        return m_labelCache[value];

    wxString labels;
    labels.Printf(_T("%d"), (int)(value+0.5));

    wxColour text_color;
    GetGlobalColor( _T ( "DILG3" ), &text_color );
    wxColour back_color;
    wxPen penText(text_color);

    GetGlobalColor( _T ( "DILG0" ), &back_color );
    wxBrush backBrush(back_color);
    wxBitmap bm(100,100);          // big enough
    wxMemoryDC mdc(bm);
    mdc.Clear();

    int w, h;
    mdc.GetTextExtent(labels, &w, &h);
          
    mdc.SetPen(penText);
    mdc.SetBrush(backBrush);
    mdc.SetTextForeground(text_color);
    mdc.SetTextBackground(back_color);

    int label_offset = 10;          
    int xd = 0;
    int yd = 0;
//            mdc.DrawRoundedRectangle(xd, yd, w+(label_offset * 2), h, -.25);
    mdc.DrawRectangle(xd, yd, w+(label_offset * 2), h+2);
    mdc.DrawText(labels, label_offset/2 + xd, yd-1);
          
    mdc.SelectObject(wxNullBitmap);

    wxBitmap sub_BMLabel = bm.GetSubBitmap(wxRect(0,0,w+(label_offset * 2), h+2));
    m_labelCache[value] = sub_BMLabel.ConvertToImage();
    return m_labelCache[value];
}

void GRIBOverlayFactory::RenderGribBarbedArrows( int config, GribRecord **pGR,
                                                    PlugIn_ViewPort *vp )
{
    if(!m_Config.Configs[config].m_bBarbedArrows)
        return;

    //  Need two records to draw the barbed arrows
    GribRecord *pGRX, *pGRY;
    int idx, idy;
    bool polar;
    ConfigIdToGribId(config, idx, idy, polar);
    if(idx < 0 || idy < 0)
        return;

    pGRX = pGR[idx];
    pGRY = pGR[idy];

    if(!pGRX || !pGRY)
        return;

    //    Get the the grid
    int imax = pGRX->getNi();                  // Longitude
    int jmax = pGRX->getNj();                  // Latitude

    //    Barbs?
    bool barbs = true;

    //    Set minimum spacing between wind arrows
    int space;

    if( barbs )
        space = 30;
    else
        space = 20;

    int oldx = -1000;
    int oldy = -1000;

    wxColour colour;
    GetGlobalColor( _T ( "YELO2" ), &colour );
    for( int i = 0; i < imax; i++ ) {
        double lonl = pGRX->getX( i );
        /* at midpoint of grib so as to avoid problems in projection on
           gribs that go all the way to the north or south pole */
        double latl = pGRX->getY( pGRX->getNj()/2 );
        wxPoint pl;
        GetCanvasPixLL( vp, &pl, latl, lonl );

        if( abs( pl.x - oldx ) >= space ) {
            oldx = pl.x;
            for( int j = 0; j < jmax; j++ ) {
                double lon = pGRX->getX( i );
                double lat = pGRX->getY( j );
                wxPoint p;
                GetCanvasPixLL( vp, &p, lat, lon );

                if( abs( p.y - oldy ) >= space ) {
                    oldy = p.y;

                    if( PointInLLBox( vp, lon, lat ) || PointInLLBox( vp, lon - 360., lat ) ) {
                        double vx =  pGRX->getValue( i, j );
                        double vy =  pGRY->getValue( i, j );

                        vx = m_Config.CalibrateValue(config, vx);
                        vy = m_Config.CalibrateValue(config, vy);

                        if( vx != GRIB_NOTDEF && vy != GRIB_NOTDEF )
                            drawWindArrowWithBarbs( p.x, p.y, vx, vy, polar, ( lat < 0. ), colour );
                    }
                }
            }
        }
    }
}

void GRIBOverlayFactory::RenderGribIsobar( int config, GribRecord **pGR,
                                           wxArrayPtrVoid **pIsobarArray, PlugIn_ViewPort *vp )
{
    if(!m_Config.Configs[config].m_bIsoBars)
        return;

    //  Need magnitude to draw isobars
    int idx, idy;
    bool polar;
    ConfigIdToGribId(config, idx, idy, polar);
    if(idx < 0)
        return;

    GribRecord *pGRA = pGR[idx], *pGRM = NULL;

    if(!pGRA)
        return;

    /* build magnitude from multiple record types like wind and current */
    if(idy >= 0 && !polar) {
        pGRM = GribRecord::MagnitudeRecord(*pGR[idx], *pGR[idy]);
        pGRA = pGRM;
    }

    //    Initialize the array of Isobars if necessary
    if( !pIsobarArray[idx] ) {
        pIsobarArray[idx] = new wxArrayPtrVoid;
        IsoLine *piso;

        wxProgressDialog *progressdialog = NULL;
        wxDateTime start = wxDateTime::Now();

        double min = m_Config.GetMin(config);
        double max = m_Config.GetMax(config);

        /* convert min and max to units being used */
        for( double press = min; press <= max; press += m_Config.Configs[config].m_iIsoBarSpacing) {
            if(progressdialog)
                progressdialog->Update(press);
            else {
                wxDateTime now = wxDateTime::Now();
                if((now-start).GetSeconds() > 3 && press < max/2) {
                    progressdialog = new wxProgressDialog(
                        _("Building Isobar map"), _("Wind"), 100, NULL,
                        wxPD_SMOOTH | wxPD_ELAPSED_TIME | wxPD_REMAINING_TIME);
                }
            }

            piso = new IsoLine( press, m_Config.CalibrationFactor(config), pGRA );

            pIsobarArray[idx]->Add( piso );
        }
        delete progressdialog;
    }

    //    Draw the Isobars
    for( unsigned int i = 0; i < pIsobarArray[idx]->GetCount(); i++ ) {
        IsoLine *piso = (IsoLine *) pIsobarArray[idx]->Item( i );
        if( m_pdc )
            piso->drawIsoLine( this, *m_pdc, vp, true, true ); //g_bGRIBUseHiDef
        else
            piso->drawGLIsoLine( this, vp, true, true ); //g_bGRIBUseHiDef

        // Draw Isobar labels

        int density = 40;
        int first = 0;

        if( m_pdc )
            piso->drawIsoLineLabels( this, *m_pdc, vp, density,
                                     first, getLabel(piso->getValue()) );
        else
            piso->drawGLIsoLineLabels( this, vp, density, first, getLabel(piso->getValue()));
    }

    delete pGRM;
}

void GRIBOverlayFactory::RenderGribDirectionArrows( int config, GribRecord **pGR,
                                                    PlugIn_ViewPort *vp )
{
    if(!m_Config.Configs[config].m_bDirectionArrows)
        return;

    //   need two records or a polar record to draw arrows
    GribRecord *pGRX, *pGRY;
    int idx, idy;
    bool polar;
    ConfigIdToGribId(config, idx, idy, polar);
    if(idx < 0 || idy < 0)
        return;

    if(polar) {
        pGRX = pGR[idy];
        if(!pGRX)
            return;
    } else {
        pGRX = pGR[idx];
        pGRY = pGR[idy];
        if(!pGRX || !pGRY)
            return;
    }

    //    Get the the grid
    int imax = pGRX->getNi();                  // Longitude
    int jmax = pGRX->getNj();                  // Latitude

    //    Set minimum spacing between arrows
    int space;
    space = 60;

    int oldx = -1000;
    int oldy = -1000;

    wxColour colour;
    GetGlobalColor( _T ( "UBLCK" ), &colour );

    for( int i = 0; i < imax; i++ ) {
        double lonl = pGRX->getX( i );
        double latl = pGRX->getY( pGRX->getNj()/2 );
        wxPoint pl;
        GetCanvasPixLL( vp, &pl, latl, lonl );

        if( abs( pl.x - oldx ) >= space ) {
            oldx = pl.x;
            for( int j = 0; j < jmax; j++ ) {
                double lon = pGRX->getX( i );
                double lat = pGRX->getY( j );
                wxPoint p;
                GetCanvasPixLL( vp, &p, lat, lon );

                if( abs( p.y - oldy ) >= space ) {
                    oldy = p.y;

                    if( PointInLLBox( vp, lon, lat ) || PointInLLBox( vp, lon - 360., lat ) ) {
                        if(polar) {
                            double dir = pGRX->getValue( i, j );
                            if( dir != GRIB_NOTDEF )
                                drawWaveArrow( p.x, p.y, (dir - 90) * M_PI / 180, colour );
                        } else {
                            double vx = pGRX->getValue( i,j ), vy = pGRY->getValue( i,j );
                            if( vx != GRIB_NOTDEF || vy != GRIB_NOTDEF )
                                drawWaveArrow( p.x, p.y, atan2(vy, -vx), colour );
                        }
                    }
                }
            }
        }
    }
}

void GRIBOverlayFactory::RenderGribOverlayMap( int config, GribRecord **pGR, PlugIn_ViewPort *vp)
{
    if(!m_Config.Configs[config].m_bOverlayMap)
        return;

    const int grib_pixel_size = 4;
    bool polar;
    int idx, idy;
    ConfigIdToGribId(config, idx, idy, polar);
    if(idx < 0 || !pGR[idx])
        return;

    GribRecord *pGRA = pGR[idx], *pGRM = NULL;
    if(!pGRA)
        return;

    if(idy >= 0 && !polar) {
        pGRM = GribRecord::MagnitudeRecord(*pGR[idx], *pGR[idy]);
        pGRA = pGRM;
    }

    wxPoint porg;
    GetCanvasPixLL( vp, &porg, pGRA->getLatMax(), pGRA->getLonMin() );

    //    Check two BBoxes....
    //    TODO Make a better Intersect method
    bool bdraw = false;
    if( Intersect( vp, pGRA->getLatMin(), pGRA->getLatMax(),
                   pGRA->getLonMin(), pGRA->getLonMax(),
                   0. ) != _OUT ) bdraw = true;
    if( Intersect( vp, pGRA->getLatMin(), pGRA->getLatMax(),
                   pGRA->getLonMin() - 360., pGRA->getLonMax() - 360.,
                   0. ) != _OUT ) bdraw = true;

    if( bdraw ) {
        // If needed, create the overlay
        if( !m_pOverlay[config] )
            m_pOverlay[config] = new GribOverlay;

        GribOverlay *pGO = m_pOverlay[config];

        if( !m_pdc )       //OpenGL mode
        {
            if( !pGO->m_iTexture )
                CreateGribGLTexture( pGO, config, pGRA, vp,
                                     grib_pixel_size, porg);

            if( pGO->m_iTexture )
                DrawGLTexture( pGO->m_iTexture, pGO->m_width, pGO->m_height,
                               porg.x, porg.y, grib_pixel_size );
            else
                DrawMessageZoomOut(vp);
        }
        else        //DC mode
        {
            if( !pGO->m_pDCBitmap ) {
                wxImage bl_image = CreateGribImage( config, pGRA, vp, grib_pixel_size, porg );
                if( bl_image.IsOk() ) {
                    //    Create a Bitmap
                    pGO->m_pDCBitmap = new wxBitmap( bl_image );
                    wxMask *gr_mask = new wxMask( *( pGO->m_pDCBitmap ), wxColour( 0, 0, 0 ) );
                    pGO->m_pDCBitmap->SetMask( gr_mask );
                }
            }

            if( pGO->m_pDCBitmap )
                m_pdc->DrawBitmap( *( pGO->m_pDCBitmap ), porg.x, porg.y, true );
            else
                DrawMessageZoomOut(vp);
        }
    }

    delete pGRM;
}

void GRIBOverlayFactory::RenderGribNumbers( int config, GribRecord **pGR, PlugIn_ViewPort *vp )
{
    if(!m_Config.Configs[config].m_bNumbers)
        return;

    //  Need magnitude to draw numbers
    int idx, idy;
    bool polar;
    ConfigIdToGribId(config, idx, idy, polar);
    if(idx < 0)
        return;

    GribRecord *pGRA = pGR[idx], *pGRM = NULL;

    if(!pGRA)
        return;

    /* build magnitude from multiple record types like wind and current */
    if(idy >= 0 && !polar) {
        pGRM = GribRecord::MagnitudeRecord(*pGR[idx], *pGR[idy]);
        pGRA = pGRM;
    }

    //    Get the the grid
    int imax = pGRA->getNi();                  // Longitude
    int jmax = pGRA->getNj();                  // Latitude

    //    Set minimum spacing between arrows
    int space = m_Config.Configs[config].m_iNumbersSpacing;

    int oldx = -1000;
    int oldy = -1000;

    wxColour colour;
    GetGlobalColor( _T ( "UBLCK" ), &colour );

    for( int i = 0; i < imax; i++ ) {
        double lonl = pGRA->getX( i );
        double latl = pGRA->getY( pGRA->getNj()/2 );
        wxPoint pl;
        GetCanvasPixLL( vp, &pl, latl, lonl );

        if( abs( pl.x - oldx ) >= space ) {
            oldx = pl.x;
            for( int j = 0; j < jmax; j++ ) {
                double lon = pGRA->getX( i );
                double lat = pGRA->getY( j );
                wxPoint p;
                GetCanvasPixLL( vp, &p, lat, lon );

                if( abs( p.y - oldy ) >= space ) {
                    oldy = p.y;

                    if( PointInLLBox( vp, lon, lat ) || PointInLLBox( vp, lon - 360., lat ) ) {
                        double mag = pGRA->getValue( i, j );

                        if( mag != GRIB_NOTDEF ) {
                            double value = m_Config.CalibrateValue(config, mag);
                            wxImage &label = getLabel(value);
                            if( m_pdc ) {
                                m_pdc->DrawBitmap(label, p.x, p.y, true);
                            } else {
                                glRasterPos2i(p.x, p.y);
                                glPixelZoom(1, -1); /* draw data from top to bottom */
                                glDrawPixels(label.GetWidth(), label.GetHeight(),
                                             GL_RGB, GL_UNSIGNED_BYTE, label.GetData());
                                glPixelZoom(1, 1);
                            }
                        }
                    }
                }
            }
        }
    }

    delete pGRM;
}

void GRIBOverlayFactory::drawWaveArrow( int i, int j, double ang, wxColour arrowColor )
{
    double si = sin( ang ), co = cos( ang );

    wxPen pen( arrowColor, 1 );

    if( m_pdc ) {
        m_pdc->SetPen( pen );
        m_pdc->SetBrush( *wxTRANSPARENT_BRUSH);
    }

    int arrowSize = 26;
    int dec = -arrowSize / 2;

    drawTransformedLine( pen, si, co, i, j, dec, -2, dec + arrowSize, -2 );
    drawTransformedLine( pen, si, co, i, j, dec, 2, dec + arrowSize, +2 );

    drawTransformedLine( pen, si, co, i, j, dec - 2, 0, dec + 5, 6 );    // flèche
    drawTransformedLine( pen, si, co, i, j, dec - 2, 0, dec + 5, -6 );   // flèche

}

void GRIBOverlayFactory::drawSingleArrow( int i, int j, double ang, wxColour arrowColor, int width )
{
    double si = sin( ang * PI / 180. ), co = cos( ang * PI / 180. );

    wxPen pen( arrowColor, width );

    if( m_pdc ) {
        m_pdc->SetPen( pen );
        m_pdc->SetBrush( *wxTRANSPARENT_BRUSH);
    }

    int arrowSize = 26;
    int dec = -arrowSize / 2;

    drawTransformedLine( pen, si, co, i, j, dec, 0, dec + arrowSize, 0 );

    drawTransformedLine( pen, si, co, i, j, dec - 2, 0, dec + 5, 6 );    // flèche
    drawTransformedLine( pen, si, co, i, j, dec - 2, 0, dec + 5, -6 );   // flèche

}

void GRIBOverlayFactory::drawWindArrowWithBarbs( int i, int j, double vx, double vy,
                                                 bool polar, bool south, wxColour arrowColor )
{
    double vkn, ang;

    if(polar) {
        vkn = vx;
        ang = vy * M_PI/180;
    } else {
        vkn = sqrt( vx * vx + vy * vy );
        ang = atan2( vy, -vx );
    }

    double si = sin( ang ), co = cos( ang );

    wxPen pen( arrowColor, 2 );

    if( m_pdc ) {
        m_pdc->SetPen( pen );
        m_pdc->SetBrush( *wxTRANSPARENT_BRUSH);
    }

    if( vkn < 1 ) {
        int r = 5;     // wind is very light, draw a circle
        if( m_pdc )
            m_pdc->DrawCircle( i, j, r );
        else {
            double w = pen.GetWidth(), s = 2 * M_PI / 10;
            if( m_hiDefGraphics ) w *= 0.75;
            for( double a = 0; a < 2 * M_PI; a += s )
                DrawGLLine( i + r*sin(a), j + r*cos(a), i + r*sin(a+s), j + r*cos(a+s), w );
        }
    } else {
        // Arrange for arrows to be centered on origin
        int windBarbuleSize = 26;
        int dec = -windBarbuleSize / 2;
        drawTransformedLine( pen, si, co, i, j, dec, 0, dec + windBarbuleSize, 0 );   // hampe
        drawTransformedLine( pen, si, co, i, j, dec, 0, dec + 5, 2 );    // flèche
        drawTransformedLine( pen, si, co, i, j, dec, 0, dec + 5, -2 );   // flèche

        int b1 = dec + windBarbuleSize - 4;  // position de la 1ère barbule
        if( vkn >= 7.5 && vkn < 45 ) {
            b1 = dec + windBarbuleSize;  // position de la 1ère barbule si >= 10 noeuds
        }

        if( vkn < 7.5 ) {  // 5 ktn
            drawPetiteBarbule( pen, south, si, co, i, j, b1 );
        } else if( vkn < 12.5 ) { // 10 ktn
            drawGrandeBarbule( pen, south, si, co, i, j, b1 );
        } else if( vkn < 17.5 ) { // 15 ktn
            drawGrandeBarbule( pen, south, si, co, i, j, b1 );
            drawPetiteBarbule( pen, south, si, co, i, j, b1 - 4 );
        } else if( vkn < 22.5 ) { // 20 ktn
            drawGrandeBarbule( pen, south, si, co, i, j, b1 );
            drawGrandeBarbule( pen, south, si, co, i, j, b1 - 4 );
        } else if( vkn < 27.5 ) { // 25 ktn
            drawGrandeBarbule( pen, south, si, co, i, j, b1 );
            drawGrandeBarbule( pen, south, si, co, i, j, b1 - 4 );
            drawPetiteBarbule( pen, south, si, co, i, j, b1 - 8 );
        } else if( vkn < 32.5 ) { // 30 ktn
            drawGrandeBarbule( pen, south, si, co, i, j, b1 );
            drawGrandeBarbule( pen, south, si, co, i, j, b1 - 4 );
            drawGrandeBarbule( pen, south, si, co, i, j, b1 - 8 );
        } else if( vkn < 37.5 ) { // 35 ktn
            drawGrandeBarbule( pen, south, si, co, i, j, b1 );
            drawGrandeBarbule( pen, south, si, co, i, j, b1 - 4 );
            drawGrandeBarbule( pen, south, si, co, i, j, b1 - 8 );
            drawPetiteBarbule( pen, south, si, co, i, j, b1 - 12 );
        } else if( vkn < 45 ) { // 40 ktn
            drawGrandeBarbule( pen, south, si, co, i, j, b1 );
            drawGrandeBarbule( pen, south, si, co, i, j, b1 - 4 );
            drawGrandeBarbule( pen, south, si, co, i, j, b1 - 8 );
            drawGrandeBarbule( pen, south, si, co, i, j, b1 - 12 );
        } else if( vkn < 55 ) { // 50 ktn
            drawTriangle( pen, south, si, co, i, j, b1 - 4 );
        } else if( vkn < 65 ) { // 60 ktn
            drawTriangle( pen, south, si, co, i, j, b1 - 4 );
            drawGrandeBarbule( pen, south, si, co, i, j, b1 - 8 );
        } else if( vkn < 75 ) { // 70 ktn
            drawTriangle( pen, south, si, co, i, j, b1 - 4 );
            drawGrandeBarbule( pen, south, si, co, i, j, b1 - 8 );
            drawGrandeBarbule( pen, south, si, co, i, j, b1 - 12 );
        } else if( vkn < 85 ) { // 80 ktn
            drawTriangle( pen, south, si, co, i, j, b1 - 4 );
            drawGrandeBarbule( pen, south, si, co, i, j, b1 - 8 );
            drawGrandeBarbule( pen, south, si, co, i, j, b1 - 12 );
            drawGrandeBarbule( pen, south, si, co, i, j, b1 - 16 );
        } else { // > 90 ktn
            drawTriangle( pen, south, si, co, i, j, b1 - 4 );
            drawTriangle( pen, south, si, co, i, j, b1 - 12 );
        }
    }
}

void GRIBOverlayFactory::drawTransformedLine( wxPen pen, double si, double co, int di, int dj,
                                              int i, int j, int k, int l )
{
    int ii, jj, kk, ll;
    double fi, fj, fk, fl; // For Hi Def Graphics.

    fi = ( i * co - j * si + 0.5 ) + di;
    fj = ( i * si + j * co + 0.5 ) + dj;
    fk = ( k * co - l * si + 0.5 ) + di;
    fl = ( k * si + l * co + 0.5 ) + dj;

    ii = fi; jj = fj; kk = fk; ll = fl;

    if( m_pdc ) {
        m_pdc->SetPen( pen );
        m_pdc->SetBrush( *wxTRANSPARENT_BRUSH);
#if wxUSE_GRAPHICS_CONTEXT
        if( m_hiDefGraphics && m_gdc ) {
            m_gdc->SetPen( pen );
            m_gdc->StrokeLine( fi, fj, fk, fl );
        }
        else {
            m_pdc->DrawLine( ii, jj, kk, ll );
        }
#else
        m_pdc->DrawLine(ii, jj, kk, ll);
#endif
    } else {                       // OpenGL mode
        wxColour c = pen.GetColour();
        glColor4ub( c.Red(), c.Green(), c.Blue(), 255);
        double w = pen.GetWidth();
        if( m_hiDefGraphics ) w *= 0.75;
        DrawGLLine( fi, fj, fk, fl, w );
    }
}

void GRIBOverlayFactory::drawPetiteBarbule( wxPen pen, bool south, double si, double co, int di,
        int dj, int b )
{
    if( south )
        drawTransformedLine( pen, si, co, di, dj, b, 0, b + 2, -5 );
    else
        drawTransformedLine( pen, si, co, di, dj, b, 0, b + 2, 5 );
}

void GRIBOverlayFactory::drawGrandeBarbule( wxPen pen, bool south, double si, double co, int di,
        int dj, int b )
{
    if( south ) drawTransformedLine( pen, si, co, di, dj, b, 0, b + 4, -10 );
    else
        drawTransformedLine( pen, si, co, di, dj, b, 0, b + 4, 10 );
}

void GRIBOverlayFactory::drawTriangle( wxPen pen, bool south, double si, double co, int di, int dj,
        int b )
{
    if( south ) {
        drawTransformedLine( pen, si, co, di, dj, b, 0, b + 4, -10 );
        drawTransformedLine( pen, si, co, di, dj, b + 8, 0, b + 4, -10 );
    } else {
        drawTransformedLine( pen, si, co, di, dj, b, 0, b + 4, 10 );
        drawTransformedLine( pen, si, co, di, dj, b + 8, 0, b + 4, 10 );
    }
}

void GRIBOverlayFactory::DrawGLLine( double x1, double y1, double x2, double y2, double width )
{
    {
        glPushAttrib(GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_ENABLE_BIT |
                     GL_POLYGON_BIT | GL_HINT_BIT ); //Save state
        {

            //      Enable anti-aliased lines, at best quality
            glEnable( GL_LINE_SMOOTH );
            glEnable( GL_BLEND );
            glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
            glHint( GL_LINE_SMOOTH_HINT, GL_NICEST );
            glLineWidth( width );

            glBegin( GL_LINES );
            glVertex2d( x1, y1 );
            glVertex2d( x2, y2 );
            glEnd();
        }

        glPopAttrib();
    }
}

void GRIBOverlayFactory::DrawOLBitmap( const wxBitmap &bitmap, wxCoord x, wxCoord y, bool usemask )
{
    wxBitmap bmp;
    if( x < 0 || y < 0 ) {
        int dx = ( x < 0 ? -x : 0 );
        int dy = ( y < 0 ? -y : 0 );
        int w = bitmap.GetWidth() - dx;
        int h = bitmap.GetHeight() - dy;
        /* picture is out of viewport */
        if( w <= 0 || h <= 0 ) return;
        wxBitmap newBitmap = bitmap.GetSubBitmap( wxRect( dx, dy, w, h ) );
        x += dx;
        y += dy;
        bmp = newBitmap;
    } else {
        bmp = bitmap;
    }
    if( m_pdc )
        m_pdc->DrawBitmap( bmp, x, y, usemask );
    else {
        wxImage image = bmp.ConvertToImage();
        int w = image.GetWidth(), h = image.GetHeight();

        if( usemask ) {
            unsigned char *d = image.GetData();
            unsigned char *a = image.GetAlpha();

            unsigned char mr, mg, mb;
            if( !image.GetOrFindMaskColour( &mr, &mg, &mb ) && !a ) printf(
                    "trying to use mask to draw a bitmap without alpha or mask\n" );

            unsigned char *e = new unsigned char[4 * w * h];
            {
                for( int y = 0; y < h; y++ )
                    for( int x = 0; x < w; x++ ) {
                        unsigned char r, g, b;
                        int off = ( y * image.GetWidth() + x );
                        r = d[off * 3 + 0];
                        g = d[off * 3 + 1];
                        b = d[off * 3 + 2];

                        e[off * 4 + 0] = r;
                        e[off * 4 + 1] = g;
                        e[off * 4 + 2] = b;

                        e[off * 4 + 3] =
                                a ? a[off] : ( ( r == mr ) && ( g == mg ) && ( b == mb ) ? 0 : 255 );
                    }
            }

            glColor4f( 1, 1, 1, 1 );

            glEnable( GL_BLEND );
            glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
            glRasterPos2i( x, y );
            glPixelZoom( 1, -1 );
            glDrawPixels( w, h, GL_RGBA, GL_UNSIGNED_BYTE, e );
            glPixelZoom( 1, 1 );
            glDisable( GL_BLEND );

            delete[] ( e );
        } else {
            glRasterPos2i( x, y );
            glPixelZoom( 1, -1 ); /* draw data from top to bottom */
            glDrawPixels( w, h, GL_RGB, GL_UNSIGNED_BYTE, image.GetData() );
            glPixelZoom( 1, 1 );
        }
    }
}

void GRIBOverlayFactory::DrawGLImage( wxImage *pimage, wxCoord xd, wxCoord yd, bool usemask )
{
    int w = pimage->GetWidth(), h = pimage->GetHeight();
    int x_offset = 0;
    int y_offset = 0;

    unsigned char *d = pimage->GetData();
    unsigned char *a = pimage->GetAlpha();

    unsigned char *e = new unsigned char[4 * w * h];
    {
        for( int y = 0; y < h; y++ )
            for( int x = 0; x < w; x++ ) {
                unsigned char r, g, b;
                int off = ( ( y + y_offset ) * pimage->GetWidth() + x + x_offset );
                r = d[off * 3 + 0];
                g = d[off * 3 + 1];
                b = d[off * 3 + 2];

                int doff = ( y * w + x );
                e[doff * 4 + 0] = r;
                e[doff * 4 + 1] = g;
                e[doff * 4 + 2] = b;

                e[doff * 4 + 3] = a ? a[off] : 255;
            }
    }

    DrawGLRGBA( e, w, h, xd, yd );
    delete[] e;
}

void GRIBOverlayFactory::DrawGLTexture( GLuint texture, int width, int height,
                                        int xd, int yd, int grib_pixel_size )
{ 
    glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, texture);

    glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP );
    glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP );
    glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR );

    glDisable( GL_MULTISAMPLE );

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glColor4f(1, 1, 1, 1);

    glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
    
    int x = xd, y = yd, w = width*grib_pixel_size, h = height*grib_pixel_size;
    
    glBegin(GL_QUADS);
    glTexCoord2i(0, 0),          glVertex2i(x, y);
    glTexCoord2i(width, 0),      glVertex2i(x+w, y);
    glTexCoord2i(width, height), glVertex2i(x+w, y+h);
    glTexCoord2i(0, height),     glVertex2i(x, y+h);
    glEnd();
    
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
}

void GRIBOverlayFactory::DrawGLRGBA( unsigned char *pRGBA, int width, int height, int xd,
        int yd )
{
    int x_offset = 0;
    int y_offset = 0;
    int draw_width = width;
    int draw_height = height;

    glColor4f( 1, 1, 1, 1 );

    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glPixelZoom( 1, -1 );

    glPushClientAttrib( GL_CLIENT_PIXEL_STORE_BIT );

    glPixelStorei( GL_UNPACK_ROW_LENGTH, width );
    if( xd < 0 ) {
        x_offset = -xd;
        draw_width += xd;
    }
    if( yd < 0 ) {
        y_offset = -yd;
        draw_height += yd;
    }

    glRasterPos2i( xd + x_offset, yd + y_offset );

    glPixelStorei( GL_UNPACK_SKIP_PIXELS, x_offset );
    glPixelStorei( GL_UNPACK_SKIP_ROWS, y_offset );

    glDrawPixels( draw_width, draw_height, GL_RGBA, GL_UNSIGNED_BYTE, pRGBA );
    glPixelZoom( 1, 1 );
    glDisable( GL_BLEND );

    glPopClientAttrib();

}

void GRIBOverlayFactory::DrawMessageZoomOut( PlugIn_ViewPort *vp )
{

    wxString msg = _("Please Zoom or Scale Out to view suppressed GRIB Overlay");

    int x = vp->pix_width / 2, y = vp->pix_height / 2;


    wxMemoryDC mdc;
    wxBitmap bm( 1000, 1000 );
    mdc.SelectObject( bm );
    mdc.Clear();

    wxFont mfont( 15, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_NORMAL );
    mdc.SetFont( mfont );
    mdc.SetPen( *wxBLACK_PEN);
    mdc.SetBrush( *wxWHITE_BRUSH);

    int w, h;
    mdc.GetTextExtent( msg, &w, &h );

    int label_offset = 10;
    int wdraw = w + ( label_offset * 2 );
    mdc.DrawRectangle( 0, 0, wdraw, h + 2 );
    mdc.DrawText( msg, label_offset / 2, -1 );

    mdc.SelectObject( wxNullBitmap );

    wxBitmap sbm = bm.GetSubBitmap( wxRect( 0, 0, wdraw, h + 2 ) );
    DrawOLBitmap( sbm, x - wdraw / 2, y, false );
}
