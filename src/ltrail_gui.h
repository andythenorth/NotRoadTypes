/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file ltrail_gui.h Functions/types related to the light rail GUIs. */

#ifndef LTRAIL_GUI_H
#define LTRAIL_GUI_H

#include "road_type.h"
#include "tile_type.h"
#include "direction_type.h"
#include "widgets/dropdown_type.h"

struct Window *ShowBuildLtRailToolbar(RoadType roadtype);
DropDownList *GetLtRailTypeDropDownList(bool for_replacement = false);

#endif /* LTRAIL_GUI_H */
