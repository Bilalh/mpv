/*
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <libavutil/common.h>

/* Stuff for correct aspect scaling. */
#include "aspect.h"
#include "vo.h"
#include "common/msg.h"
#include "options/options.h"

#include "vo.h"

void aspect_save_videores(struct vo *vo, int w, int h, int d_w, int d_h)
{
    vo->aspdat.orgw = w;
    vo->aspdat.orgh = h;
    vo->aspdat.prew = d_w;
    vo->aspdat.preh = d_h;
    vo->aspdat.par = (double)d_w / d_h * h / w;
}

static void aspect_calc(struct vo *vo, int *srcw, int *srch)
{
    struct aspect_data *aspdat = &vo->aspdat;
    float pixelaspect = vo->monitor_par;

    int fitw = FFMAX(1, vo->dwidth);
    int fith = FFMAX(1, vo->dheight);

    MP_DBG(vo, "aspect(0) fitin: %dx%d monitor_par: %.2f\n",
           fitw, fith, pixelaspect);
    *srcw = fitw;
    *srch = (float)fitw / aspdat->prew * aspdat->preh / pixelaspect;
    MP_DBG(vo, "aspect(1) wh: %dx%d (org: %dx%d)\n",
           *srcw, *srch, aspdat->prew, aspdat->preh);
    if (*srch > fith || *srch < aspdat->orgh) {
        int tmpw = (float)fith / aspdat->preh * aspdat->prew * pixelaspect;
        if (tmpw <= fitw) {
            *srch = fith;
            *srcw = tmpw;
        } else if (*srch > fith) {
            MP_WARN(vo, "No suitable new aspect found!\n");
        }
    }
    MP_DBG(vo, "aspect(2) wh: %dx%d (org: %dx%d)\n",
           *srcw, *srch, aspdat->prew, aspdat->preh);
}

void aspect_calc_panscan(struct vo *vo, int *out_w, int *out_h)
{
    struct mp_vo_opts *opts = vo->opts;
    int fwidth, fheight;
    aspect_calc(vo, &fwidth, &fheight);

    int vo_panscan_area = vo->dheight - fheight;
    if (!vo_panscan_area)
        vo_panscan_area = vo->dwidth - fwidth;

    *out_w = fwidth + vo_panscan_area * opts->panscan * fwidth / fheight;
    *out_h = fheight + vo_panscan_area * opts->panscan;
}
