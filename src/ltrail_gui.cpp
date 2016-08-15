/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file ltrail_gui.cpp GUI for building light rails. */

#include "stdafx.h"
#include "gui.h"
#include "window_gui.h"
#include "station_gui.h"
#include "terraform_gui.h"
#include "viewport_func.h"
#include "command_func.h"
#include "road_cmd.h"
#include "station_func.h"
#include "window_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "company_func.h"
#include "tunnelbridge.h"
#include "tunnelbridge_map.h"
#include "tilehighlight_func.h"
#include "company_base.h"
#include "hotkeys.h"
#include "road_gui.h"
#include "ltrail_gui.h"
#include "zoom_func.h"

#include "widgets/road_widget.h"

#include "table/strings.h"

#include "safeguards.h"

static void ShowLRStationPicker(Window *parent, RoadStopType rs);
static void ShowRoadDepotPicker(Window *parent);

static bool _remove_button_clicked;

/**
 * Define the values of the RoadFlags
 * @see CmdBuildLongRoad
 */
enum RoadFlags {
	RF_NONE             = 0x00,
	RF_START_HALFROAD_Y = 0x01,    // The start tile in Y-dir should have only a half road
	RF_END_HALFROAD_Y   = 0x02,    // The end tile in Y-dir should have only a half road
	RF_DIR_Y            = 0x04,    // The direction is Y-dir
	RF_DIR_X            = RF_NONE, // Dummy; Dir X is set when RF_DIR_Y is not set
	RF_START_HALFROAD_X = 0x08,    // The start tile in X-dir should have only a half road
	RF_END_HALFROAD_X   = 0x10,    // The end tile in X-dir should have only a half road
};
DECLARE_ENUM_AS_BIT_SET(RoadFlags)

static RoadFlags _place_road_flag;

static RoadType _cur_ltrailtype;

static Axis _ltrail_station_picker_orientation;
static DiagDirection _road_depot_orientation;

/** Structure holding information per roadtype for several functions */
struct RoadTypeInfo {
	StringID err_build_road;        ///< Building a normal piece of road
	StringID err_remove_road;       ///< Removing a normal piece of road
	StringID err_depot;             ///< Building a depot
	StringID err_build_station[2];  ///< Building a bus or truck station
	StringID err_remove_station[2]; ///< Removing of a bus or truck station

	StringID picker_title[2];       ///< Title for the station picker for bus or truck stations
	StringID picker_tooltip[2];     ///< Tooltip for the station picker for bus or truck stations

	SpriteID cursor_nesw;           ///< Cursor for building NE and SW bits
	SpriteID cursor_nwse;           ///< Cursor for building NW and SE bits
	SpriteID cursor_autoroad;       ///< Cursor for building autoroad
};

/** What errors/cursors must be shown for several types of roads */
static const RoadTypeInfo _ltrail_type_info = {
	STR_ERROR_CAN_T_BUILD_TRAMWAY_HERE,
	STR_ERROR_CAN_T_REMOVE_TRAMWAY_FROM,
	STR_ERROR_CAN_T_BUILD_TRAM_DEPOT,
	{ STR_ERROR_CAN_T_BUILD_PASSENGER_TRAM_STATION,         STR_ERROR_CAN_T_BUILD_CARGO_TRAM_STATION },
	{ STR_ERROR_CAN_T_REMOVE_PASSENGER_TRAM_STATION,        STR_ERROR_CAN_T_REMOVE_CARGO_TRAM_STATION },
	{ STR_STATION_BUILD_PASSENGER_TRAM_ORIENTATION,         STR_STATION_BUILD_CARGO_TRAM_ORIENTATION },
	{ STR_STATION_BUILD_PASSENGER_TRAM_ORIENTATION_TOOLTIP, STR_STATION_BUILD_CARGO_TRAM_ORIENTATION_TOOLTIP },

	SPR_CURSOR_TRAMWAY_NESW,
	SPR_CURSOR_TRAMWAY_NWSE,
	SPR_CURSOR_AUTOTRAM,
};

/**
 * TODO: some of the next functions are duplicate from the road_gui.cpp (like the ToggleRoadButton_Remove and RoadToolbar_CtrlChanged), others are
 * really similar but specific for Tram (I removed the checks for the roadtype, it should be done on the road_gui.cpp too)
 */

/**
 * Callback to start placing a bridge.
 * @param tile Start tile of the bridge.
 */
static void PlaceRoad_Bridge(TileIndex tile, Window *w)
{
	if (IsBridgeTile(tile)) {
		TileIndex other_tile = GetOtherTunnelBridgeEnd(tile);
		Point pt = { 0, 0 };
		w->OnPlaceMouseUp(VPM_X_OR_Y, DDSP_BUILD_BRIDGE, pt, other_tile, tile);
	}
	else {
		VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_BUILD_BRIDGE);
	}
}

/**
 * Place a new road stop.
 * @param start_tile First tile of the area.
 * @param end_tile Last tile of the area.
 * @param p2 bit 0: 0 For bus stops, 1 for truck stops.
 *           bit 2..3: The roadtypes.
 *           bit 5: Allow stations directly adjacent to other stations.
 * @param cmd Command to use.
 * @see CcRoadStop()
 */
static void PlaceLtRailStop(TileIndex start_tile, TileIndex end_tile, uint32 p2, uint32 cmd)
{
	uint8 ddir = _ltrail_station_picker_orientation;
	SB(p2, 16, 16, INVALID_STATION); // no station to join

	SetBit(p2, 1); // It's a drive-through stop.
	ddir -= AXIS_END; // Adjust picker result to actual direction.
	p2 |= ddir << 6; // Set the DiagDirecion into p2 bits 6 and 7.

	TileArea ta(start_tile, end_tile);
	CommandContainer cmdcont = { ta.tile, (uint32)(ta.w | ta.h << 8), p2, cmd, CcRoadStop, "" };
	ShowSelectStationIfNeeded(cmdcont, ta);
}

/**
 * Callback for placing a bus station.
 * @param tile Position to place the station.
 */
static void PlaceLtRail_PaxStation(TileIndex tile)
{
	if (_remove_button_clicked) {
		VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_REMOVE_BUSSTOP);
	}
	else {
		VpStartPlaceSizing(tile, VPM_X_AND_Y_LIMITED, DDSP_BUILD_BUSSTOP);
		VpSetPlaceSizingLimit(_settings_game.station.station_spread);
	}
}

/**
 * Callback for placing a truck station.
 * @param tile Position to place the station.
 */
static void PlaceLtRail_FreightStation(TileIndex tile)
{
	if (_remove_button_clicked) {
		VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_REMOVE_TRUCKSTOP);
	}
	else {
		VpStartPlaceSizing(tile, VPM_X_AND_Y_LIMITED, DDSP_BUILD_TRUCKSTOP);
		VpSetPlaceSizingLimit(_settings_game.station.station_spread);
	}
}

typedef void OnButtonClick(Window *w);

/**
* Toggles state of the Remove button of Build road toolbar
* @param w window the button belongs to
*/
static void ToggleRoadButton_Remove(Window *w)
{
	w->ToggleWidgetLoweredState(WID_LRT_REMOVE);
	w->SetWidgetDirty(WID_LRT_REMOVE);
	_remove_button_clicked = w->IsWidgetLowered(WID_LRT_REMOVE);
	SetSelectionRed(_remove_button_clicked);
}

/**
 * Updates the Remove button because of Ctrl state change
 * @param w window the button belongs to
 * @return true iff the remove button was changed
 */
static bool RoadToolbar_CtrlChanged(Window *w)
{
	if (w->IsWidgetDisabled(WID_LRT_REMOVE)) return false;

	/* allow ctrl to switch remove mode only for these widgets */
	for (uint i = WID_LRT_ROAD_X; i <= WID_LRT_AUTOROAD; i++) {
		if (w->IsWidgetLowered(i)) {
			ToggleRoadButton_Remove(w);
			return true;
		}
	}

	return false;
}

struct BuildLtRailToolbarWindow : Window {
	int last_started_action; ///< Last started user action.

	BuildLtRailToolbarWindow(WindowDesc *desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
		this->SetWidgetsDisabledState(true,
			WID_LRT_REMOVE,
			WIDGET_LIST_END);

		this->OnInvalidateData();
		this->last_started_action = WIDGET_LIST_END;

		if (_settings_client.gui.link_terraform_toolbar) ShowTerraformToolbar(this);
	}

	~BuildLtRailToolbarWindow()
	{
		if (_settings_client.gui.link_terraform_toolbar) DeleteWindowById(WC_SCEN_LAND_GEN, 0, false);
	}

	/**
	* Some data on this window has become invalid.
	* @param data Information about the changed data.
	* @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	*/
	virtual void OnInvalidateData(int data = 0, bool gui_scope = true)
	{
		if (!gui_scope) return;

		bool can_build = CanBuildVehicleInfrastructure(VEH_TRAM);
		this->SetWidgetsDisabledState(!can_build,
			WID_LRT_DEPOT,
			WID_LRT_PAX_STATION,
			WID_LRT_FREIGHT_STATION,
			WIDGET_LIST_END);
		if (!can_build) {
			DeleteWindowById(WC_BUILD_DEPOT, TRANSPORT_ROAD);
			DeleteWindowById(WC_BUS_STATION, TRANSPORT_ROAD);
			DeleteWindowById(WC_TRUCK_STATION, TRANSPORT_ROAD);
		}
	}

	/**
	* Update the remove button lowered state of the road toolbar
	*
	* @param clicked_widget The widget which the client clicked just now
	*/
	void UpdateOptionWidgetStatus(RoadToolbarWidgets clicked_widget)
	{
		/* The remove and the one way button state is driven
		* by the other buttons so they don't act on themselves.
		* Both are only valid if they are able to apply as options. */
		switch (clicked_widget) {
		case WID_LRT_REMOVE:
			break;

		case WID_LRT_PAX_STATION:
		case WID_LRT_FREIGHT_STATION:
			this->SetWidgetDisabledState(WID_LRT_REMOVE, !this->IsWidgetLowered(clicked_widget));
			break;

		case WID_LRT_ROAD_X:
		case WID_LRT_ROAD_Y:
		case WID_LRT_AUTOROAD:
			this->SetWidgetsDisabledState(!this->IsWidgetLowered(clicked_widget),
				WID_LRT_REMOVE,
				WIDGET_LIST_END);
			break;

		default:
			/* When any other buttons than road/station, raise and
			* disable the removal button */
			this->SetWidgetsDisabledState(true,
				WID_LRT_REMOVE,
				WIDGET_LIST_END);
			this->SetWidgetsLoweredState(false,
				WID_LRT_REMOVE,
				WIDGET_LIST_END);
			break;
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		_remove_button_clicked = false;
		switch (widget) {
		case WID_LRT_ROAD_X:
			HandlePlacePushButton(this, WID_LRT_ROAD_X, _ltrail_type_info.cursor_nwse, HT_RECT);
			this->last_started_action = widget;
			break;

		case WID_LRT_ROAD_Y:
			HandlePlacePushButton(this, WID_LRT_ROAD_Y, _ltrail_type_info.cursor_nesw, HT_RECT);
			this->last_started_action = widget;
			break;

		case WID_LRT_AUTOROAD:
			HandlePlacePushButton(this, WID_LRT_AUTOROAD, _ltrail_type_info.cursor_autoroad, HT_RECT);
			this->last_started_action = widget;
			break;

		case WID_LRT_DEMOLISH:
			HandlePlacePushButton(this, WID_LRT_DEMOLISH, ANIMCURSOR_DEMOLISH, HT_RECT | HT_DIAGONAL);
			this->last_started_action = widget;
			break;

		case WID_LRT_DEPOT:
			if (_game_mode == GM_EDITOR || !CanBuildVehicleInfrastructure(VEH_ROAD)) return;
			if (HandlePlacePushButton(this, WID_LRT_DEPOT, SPR_CURSOR_ROAD_DEPOT, HT_RECT)) {
				ShowRoadDepotPicker(this);
				this->last_started_action = widget;
			}
			break;

		case WID_LRT_PAX_STATION:
			if (_game_mode == GM_EDITOR || !CanBuildVehicleInfrastructure(VEH_ROAD)) return;
			if (HandlePlacePushButton(this, WID_LRT_PAX_STATION, SPR_CURSOR_BUS_STATION, HT_RECT)) {
				ShowLRStationPicker(this, ROADSTOP_BUS);
				this->last_started_action = widget;
			}
			break;

		case WID_LRT_FREIGHT_STATION:
			if (_game_mode == GM_EDITOR || !CanBuildVehicleInfrastructure(VEH_ROAD)) return;
			if (HandlePlacePushButton(this, WID_LRT_FREIGHT_STATION, SPR_CURSOR_TRUCK_STATION, HT_RECT)) {
				ShowLRStationPicker(this, ROADSTOP_TRUCK);
				this->last_started_action = widget;
			}
			break;

		case WID_LRT_BUILD_BRIDGE:
			HandlePlacePushButton(this, WID_LRT_BUILD_BRIDGE, SPR_CURSOR_BRIDGE, HT_RECT);
			this->last_started_action = widget;
			break;

		case WID_LRT_BUILD_TUNNEL:
			HandlePlacePushButton(this, WID_LRT_BUILD_TUNNEL, SPR_CURSOR_ROAD_TUNNEL, HT_SPECIAL);
			this->last_started_action = widget;
			break;

		case WID_LRT_REMOVE:
			if (this->IsWidgetDisabled(WID_LRT_REMOVE)) return;

			DeleteWindowById(WC_SELECT_STATION, 0);
			ToggleRoadButton_Remove(this);
			if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
			break;

		default: NOT_REACHED();
		}
		this->UpdateOptionWidgetStatus((RoadToolbarWidgets)widget);
		if (_ctrl_pressed) RoadToolbar_CtrlChanged(this);
	}

	virtual EventState OnHotkey(int hotkey)
	{
		MarkTileDirtyByTile(TileVirtXY(_thd.pos.x, _thd.pos.y)); // redraw tile selection
		return Window::OnHotkey(hotkey);
	}

	virtual void OnPlaceObject(Point pt, TileIndex tile)
	{
		_remove_button_clicked = this->IsWidgetLowered(WID_LRT_REMOVE);
		switch (this->last_started_action) {
		case WID_LRT_ROAD_X:
			_place_road_flag = RF_DIR_X;
			if (_tile_fract_coords.x >= 8) _place_road_flag |= RF_START_HALFROAD_X;
			VpStartPlaceSizing(tile, VPM_FIX_Y, DDSP_PLACE_ROAD_X_DIR);
			break;

		case WID_LRT_ROAD_Y:
			_place_road_flag = RF_DIR_Y;
			if (_tile_fract_coords.y >= 8) _place_road_flag |= RF_START_HALFROAD_Y;
			VpStartPlaceSizing(tile, VPM_FIX_X, DDSP_PLACE_ROAD_Y_DIR);
			break;

		case WID_LRT_AUTOROAD:
			_place_road_flag = RF_NONE;
			if (_tile_fract_coords.x >= 8) _place_road_flag |= RF_START_HALFROAD_X;
			if (_tile_fract_coords.y >= 8) _place_road_flag |= RF_START_HALFROAD_Y;
			VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_PLACE_AUTOROAD);
			break;

		case WID_LRT_DEMOLISH:
			PlaceProc_DemolishArea(tile);
			break;

		case WID_LRT_DEPOT:
			DoCommandP(tile, _cur_ltrailtype << 2 | _road_depot_orientation, 0,
				CMD_BUILD_ROAD_DEPOT | CMD_MSG(_ltrail_type_info.err_depot), CcRoadDepot);
			break;

		case WID_LRT_PAX_STATION:
			PlaceLtRail_PaxStation(tile);
			break;

		case WID_LRT_FREIGHT_STATION:
			PlaceLtRail_FreightStation(tile);
			break;

		case WID_LRT_BUILD_BRIDGE:
			PlaceRoad_Bridge(tile, this);
			break;

		case WID_LRT_BUILD_TUNNEL:
			DoCommandP(tile, RoadTypeToRoadTypes(_cur_ltrailtype) | (TRANSPORT_ROAD << 8), 0,
				CMD_BUILD_TUNNEL | CMD_MSG(STR_ERROR_CAN_T_BUILD_TUNNEL_HERE), CcBuildRoadTunnel);
			break;

		default: NOT_REACHED();
		}
	}

	virtual void OnPlaceObjectAbort()
	{
		this->RaiseButtons();
		this->SetWidgetsDisabledState(true,
			WID_LRT_REMOVE,
			WIDGET_LIST_END);
		this->SetWidgetDirty(WID_LRT_REMOVE);

		DeleteWindowById(WC_BUS_STATION, TRANSPORT_ROAD);
		DeleteWindowById(WC_TRUCK_STATION, TRANSPORT_ROAD);
		DeleteWindowById(WC_BUILD_DEPOT, TRANSPORT_ROAD);
		DeleteWindowById(WC_SELECT_STATION, 0);
		DeleteWindowByClass(WC_BUILD_BRIDGE);
	}

	virtual void OnPlaceDrag(ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt)
	{
		/* Here we update the end tile flags
		* of the road placement actions.
		* At first we reset the end halfroad
		* bits and if needed we set them again. */
		switch (select_proc) {
		case DDSP_PLACE_ROAD_X_DIR:
			_place_road_flag &= ~RF_END_HALFROAD_X;
			if (pt.x & 8) _place_road_flag |= RF_END_HALFROAD_X;
			break;

		case DDSP_PLACE_ROAD_Y_DIR:
			_place_road_flag &= ~RF_END_HALFROAD_Y;
			if (pt.y & 8) _place_road_flag |= RF_END_HALFROAD_Y;
			break;

		case DDSP_PLACE_AUTOROAD:
			_place_road_flag &= ~(RF_END_HALFROAD_Y | RF_END_HALFROAD_X);
			if (pt.y & 8) _place_road_flag |= RF_END_HALFROAD_Y;
			if (pt.x & 8) _place_road_flag |= RF_END_HALFROAD_X;

			/* For autoroad we need to update the
			* direction of the road */
			if (_thd.size.x > _thd.size.y || (_thd.size.x == _thd.size.y &&
				((_tile_fract_coords.x < _tile_fract_coords.y && (_tile_fract_coords.x + _tile_fract_coords.y) < 16) ||
				(_tile_fract_coords.x > _tile_fract_coords.y && (_tile_fract_coords.x + _tile_fract_coords.y) > 16)))) {
				/* Set dir = X */
				_place_road_flag &= ~RF_DIR_Y;
			}
			else {
				/* Set dir = Y */
				_place_road_flag |= RF_DIR_Y;
			}

			break;

		default:
			break;
		}

		VpSelectTilesWithMethod(pt.x, pt.y, select_method);
	}

	virtual void OnPlaceMouseUp(ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt, TileIndex start_tile, TileIndex end_tile)
	{
		if (pt.x != -1) {
			switch (select_proc) {
			default: NOT_REACHED();
			case DDSP_BUILD_BRIDGE:
				if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
				ShowBuildBridgeWindow(start_tile, end_tile, TRANSPORT_ROAD, RoadTypeToRoadTypes(_cur_ltrailtype));
				break;

			case DDSP_DEMOLISH_AREA:
				GUIPlaceProcDragXY(select_proc, start_tile, end_tile);
				break;

			case DDSP_PLACE_ROAD_X_DIR:
			case DDSP_PLACE_ROAD_Y_DIR:
			case DDSP_PLACE_AUTOROAD:
				/* Flag description:
				* Use the first three bits (0x07) if dir == Y
				* else use the last 2 bits (X dir has
				* not the 3rd bit set) */
				_place_road_flag = (RoadFlags)((_place_road_flag & RF_DIR_Y) ? (_place_road_flag & 0x07) : (_place_road_flag >> 3));

				DoCommandP(start_tile, end_tile, _place_road_flag | (_cur_ltrailtype << 3),
					_remove_button_clicked ?
					CMD_REMOVE_LONG_ROAD | CMD_MSG(_ltrail_type_info.err_remove_road) :
					CMD_BUILD_LONG_ROAD | CMD_MSG(_ltrail_type_info.err_build_road), CcPlaySound_SPLAT_OTHER);
				break;

			case DDSP_BUILD_BUSSTOP:
				PlaceLtRailStop(start_tile, end_tile, (_ctrl_pressed << 5) | RoadTypeToRoadTypes(_cur_ltrailtype) << 2 | ROADSTOP_BUS, CMD_BUILD_ROAD_STOP | CMD_MSG(_ltrail_type_info.err_build_station[ROADSTOP_BUS]));
				break;

			case DDSP_BUILD_TRUCKSTOP:
				PlaceLtRailStop(start_tile, end_tile, (_ctrl_pressed << 5) | RoadTypeToRoadTypes(_cur_ltrailtype) << 2 | ROADSTOP_TRUCK, CMD_BUILD_ROAD_STOP | CMD_MSG(_ltrail_type_info.err_build_station[ROADSTOP_TRUCK]));
				break;

			case DDSP_REMOVE_BUSSTOP: {
				TileArea ta(start_tile, end_tile);
				DoCommandP(ta.tile, ta.w | ta.h << 8, (_ctrl_pressed << 1) | ROADSTOP_BUS, CMD_REMOVE_ROAD_STOP | CMD_MSG(_ltrail_type_info.err_remove_station[ROADSTOP_BUS]), CcPlaySound_SPLAT_OTHER);
				break;
			}

			case DDSP_REMOVE_TRUCKSTOP: {
				TileArea ta(start_tile, end_tile);
				DoCommandP(ta.tile, ta.w | ta.h << 8, (_ctrl_pressed << 1) | ROADSTOP_TRUCK, CMD_REMOVE_ROAD_STOP | CMD_MSG(_ltrail_type_info.err_remove_station[ROADSTOP_TRUCK]), CcPlaySound_SPLAT_OTHER);
				break;
			}
			}
		}
	}

	virtual void OnPlacePresize(Point pt, TileIndex tile)
	{
		DoCommand(tile, RoadTypeToRoadTypes(_cur_ltrailtype) | (TRANSPORT_ROAD << 8), 0, DC_AUTO, CMD_BUILD_TUNNEL);
		VpSetPresizeRange(tile, _build_tunnel_endtile == 0 ? tile : _build_tunnel_endtile);
	}

	virtual EventState OnCTRLStateChange()
	{
		if (RoadToolbar_CtrlChanged(this)) return ES_HANDLED;
		return ES_NOT_HANDLED;
	}

	static HotkeyList hotkeys;
};

static const NWidgetPart _nested_build_tramway_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_ROAD_TOOLBAR_TRAM_CONSTRUCTION_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_LRT_ROAD_X),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_TRAMWAY_X_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_LRT_ROAD_Y),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_TRAMWAY_Y_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_LRT_AUTOROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_AUTOTRAM, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOTRAM),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_LRT_DEMOLISH),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_LRT_DEPOT),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_DEPOT, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAM_VEHICLE_DEPOT),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_LRT_PAX_STATION),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_BUS_STATION, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_PASSENGER_TRAM_STATION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_LRT_FREIGHT_STATION),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_TRUCK_BAY, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_CARGO_TRAM_STATION),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN, -1), SetMinimalSize(0, 22), SetFill(1, 1), EndContainer(),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_LRT_BUILD_BRIDGE),
						SetFill(0, 1), SetMinimalSize(43, 22), SetDataTip(SPR_IMG_BRIDGE, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_BRIDGE),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_LRT_BUILD_TUNNEL),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_TUNNEL, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_TUNNEL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_LRT_REMOVE),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_REMOVE, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_TRAMWAYS),
	EndContainer(),
};

static WindowDesc _build_tramway_desc(
	WDP_ALIGN_TOOLBAR, "toolbar_tramway", 0, 0,
	WC_BUILD_TOOLBAR, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_build_tramway_widgets, lengthof(_nested_build_tramway_widgets),
	&BuildLtRailToolbarWindow::hotkeys
);

/**
 * Open the build light rail toolbar window
 *
 * If the terraform toolbar is linked to the toolbar, that window is also opened.
 *
 * @return newly opened tram toolbar, or NULL if the toolbar could not be opened.
 */
Window *ShowBuildLtRailToolbar(RoadType roadtype)
{
	if (!Company::IsValidID(_local_company)) return NULL;
	_cur_ltrailtype = roadtype;

	DeleteWindowByClass(WC_BUILD_TOOLBAR);
	return AllocateWindowDescFront<BuildLtRailToolbarWindow>(&_build_tramway_desc, TRANSPORT_ROAD);
}

/**
 * Handler for global hotkeys of the BuildRoadToolbarWindow.
 * @param hotkey Hotkey
 * @return ES_HANDLED if hotkey was accepted.
 */
static EventState RoadToolbarGlobalHotkeys(int hotkey)
{
	Window *w = NULL;
	extern RoadType _last_built_roadtype;
	switch (_game_mode) {
	case GM_NORMAL: {
		w = ShowBuildRoadToolbar(_last_built_roadtype);
		break;
	}

	case GM_EDITOR:
		// The scenario editor does not have a light rail toolbar
		if (_last_built_roadtype == ROADTYPE_TRAM) break;
		w = ShowBuildRoadScenToolbar();
		break;

	default:
		break;
	}

	if (w == NULL) return ES_NOT_HANDLED;
	return w->OnHotkey(hotkey);
}

static Hotkey ltrailtoolbar_hotkeys[] = {
	Hotkey('1', "build_x", WID_LRT_ROAD_X),
	Hotkey('2', "build_y", WID_LRT_ROAD_Y),
	Hotkey('3', "autoroad", WID_LRT_AUTOROAD),
	Hotkey('4', "demolish", WID_LRT_DEMOLISH),
	Hotkey('5', "depot", WID_LRT_DEPOT),
	Hotkey('6', "pax_station", WID_LRT_PAX_STATION),
	Hotkey('7', "freight_station", WID_LRT_FREIGHT_STATION),
	Hotkey('B', "bridge", WID_LRT_BUILD_BRIDGE),
	Hotkey('T', "tunnel", WID_LRT_BUILD_TUNNEL),
	Hotkey('R', "remove", WID_LRT_REMOVE),
	HOTKEY_LIST_END
};
HotkeyList BuildLtRailToolbarWindow::hotkeys("ltrailtoolbar", ltrailtoolbar_hotkeys, RoadToolbarGlobalHotkeys);

struct BuildLtRailStationWindow : public PickerWindowBase {
	BuildLtRailStationWindow(WindowDesc *desc, Window *parent, RoadStopType rs) : PickerWindowBase(desc, parent)
	{
		this->CreateNestedTree();

		/* Trams only have drivethrough stations */
		if (_ltrail_station_picker_orientation > AXIS_END) {
			_ltrail_station_picker_orientation = AXIS_END;
		}

		this->GetWidget<NWidgetCore>(WID_BLRS_CAPTION)->widget_data = _ltrail_type_info.picker_title[rs];
		for (uint i = WID_BLRS_STATION_X; i < WID_BLRS_LT_OFF; i++) this->GetWidget<NWidgetCore>(i)->tool_tip = _ltrail_type_info.picker_tooltip[rs];

		this->LowerWidget(_ltrail_station_picker_orientation + WID_BLRS_STATION_X);
		this->LowerWidget(_settings_client.gui.station_show_coverage + WID_BLRS_LT_OFF);

		this->FinishInitNested(TRANSPORT_ROAD);

		this->window_class = (rs == ROADSTOP_BUS) ? WC_BUS_STATION : WC_TRUCK_STATION;
	}

	virtual ~BuildLtRailStationWindow()
	{
		DeleteWindowById(WC_SELECT_STATION, 0);
	}

	virtual void OnPaint()
	{
		this->DrawWidgets();

		int rad = _settings_game.station.modified_catchment ? ((this->window_class == WC_BUS_STATION) ? CA_BUS : CA_TRUCK) : CA_UNMODIFIED;
		if (_settings_client.gui.station_show_coverage) {
			SetTileSelectBigSize(-rad, -rad, 2 * rad, 2 * rad);
		}
		else {
			SetTileSelectSize(1, 1);
		}

		/* 'Accepts' and 'Supplies' texts. */
		StationCoverageType sct = (this->window_class == WC_BUS_STATION) ? SCT_PASSENGERS_ONLY : SCT_NON_PASSENGERS_ONLY;
		int top = this->GetWidget<NWidgetBase>(WID_BLRS_LT_ON)->pos_y + this->GetWidget<NWidgetBase>(WID_BLRS_LT_ON)->current_y + WD_PAR_VSEP_NORMAL;
		NWidgetBase *back_nwi = this->GetWidget<NWidgetBase>(WID_BLRS_BACKGROUND);
		int right = back_nwi->pos_x + back_nwi->current_x;
		int bottom = back_nwi->pos_y + back_nwi->current_y;
		top = DrawStationCoverageAreaText(back_nwi->pos_x + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, top, sct, rad, false) + WD_PAR_VSEP_NORMAL;
		top = DrawStationCoverageAreaText(back_nwi->pos_x + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, top, sct, rad, true) + WD_PAR_VSEP_NORMAL;
		/* Resize background if the window is too small.
		* Never make the window smaller to avoid oscillating if the size change affects the acceptance.
		* (This is the case, if making the window bigger moves the mouse into the window.) */
		if (top > bottom) {
			ResizeWindow(this, 0, top - bottom, false);
		}
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		if (!IsInsideMM(widget, WID_BLRS_STATION_X, WID_BLRS_STATION_Y + 1)) return;

		size->width = ScaleGUITrad(64) + 2;
		size->height = ScaleGUITrad(48) + 2;
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		if (!IsInsideMM(widget, WID_BLRS_STATION_X, WID_BLRS_STATION_Y + 1)) return;

		StationType st = (this->window_class == WC_BUS_STATION) ? STATION_BUS : STATION_TRUCK;
		StationPickerDrawSprite(r.left + 1 + ScaleGUITrad(31), r.bottom - ScaleGUITrad(31), st, INVALID_RAILTYPE, ROADTYPE_TRAM, widget + 2); // + 2 because I removed the 2 widgets of the road stops
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
		case WID_BLRS_STATION_X:
		case WID_BLRS_STATION_Y:
			this->RaiseWidget(_ltrail_station_picker_orientation + WID_BLRS_STATION_X);
			_ltrail_station_picker_orientation = (Axis)(widget - WID_BLRS_STATION_X);
			this->LowerWidget(_ltrail_station_picker_orientation + WID_BLRS_STATION_X);
			if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
			this->SetDirty();
			DeleteWindowById(WC_SELECT_STATION, 0);
			break;

		case WID_BLRS_LT_OFF:
		case WID_BLRS_LT_ON:
			this->RaiseWidget(_settings_client.gui.station_show_coverage + WID_BLRS_LT_OFF);
			_settings_client.gui.station_show_coverage = (widget != WID_BLRS_LT_OFF);
			this->LowerWidget(_settings_client.gui.station_show_coverage + WID_BLRS_LT_OFF);
			if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
			this->SetDirty();
			break;

		default:
			break;
		}
	}

	virtual void OnTick()
	{
		CheckRedrawStationCoverage(this);
	}
};

/** Widget definition of the build light rail station window */
static const NWidgetPart _nested_lr_station_picker_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION,  COLOUR_DARK_GREEN, WID_BLRS_CAPTION),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BLRS_BACKGROUND),
		NWidget(NWID_SPACER), SetMinimalSize(0, 1),
		NWidget(NWID_HORIZONTAL), SetPIP(0, 2, 0),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_BLRS_STATION_X),  SetMinimalSize(66, 50), EndContainer(),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_BLRS_STATION_Y),  SetMinimalSize(66, 50), EndContainer(),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 1),
		NWidget(NWID_HORIZONTAL), SetPIP(2, 0, 2),
			NWidget(WWT_LABEL, COLOUR_DARK_GREEN, WID_BLRS_INFO), SetMinimalSize(140, 14), SetDataTip(STR_STATION_BUILD_COVERAGE_AREA_TITLE, STR_NULL),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_HORIZONTAL), SetPIP(2, 0, 2),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BLRS_LT_OFF), SetMinimalSize(60, 12),
											SetDataTip(STR_STATION_BUILD_COVERAGE_OFF, STR_STATION_BUILD_COVERAGE_AREA_OFF_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BLRS_LT_ON), SetMinimalSize(60, 12),
											SetDataTip(STR_STATION_BUILD_COVERAGE_ON, STR_STATION_BUILD_COVERAGE_AREA_ON_TOOLTIP),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 10), SetResize(0, 1),
	EndContainer(),
};

static WindowDesc _lr_station_picker_desc(
	WDP_AUTO, NULL, 0, 0,
	WC_BUS_STATION, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_lr_station_picker_widgets, lengthof(_nested_lr_station_picker_widgets)
);

static void ShowLRStationPicker(Window *parent, RoadStopType rs)
{
	new BuildLtRailStationWindow(&_lr_station_picker_desc, parent, rs);
}


struct BuildRoadDepotWindow : public PickerWindowBase {
	BuildRoadDepotWindow(WindowDesc *desc, Window *parent) : PickerWindowBase(desc, parent)
	{
		this->CreateNestedTree();

		this->LowerWidget(_road_depot_orientation + WID_BROD_DEPOT_NE);
		this->GetWidget<NWidgetCore>(WID_BROD_CAPTION)->widget_data = STR_BUILD_DEPOT_TRAM_ORIENTATION_CAPTION;
		for (int i = WID_BROD_DEPOT_NE; i <= WID_BROD_DEPOT_NW; i++) this->GetWidget<NWidgetCore>(i)->tool_tip = STR_BUILD_DEPOT_TRAM_ORIENTATION_SELECT_TOOLTIP;

		this->FinishInitNested(TRANSPORT_ROAD);
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		if (!IsInsideMM(widget, WID_BROD_DEPOT_NE, WID_BROD_DEPOT_NW + 1)) return;

		size->width  = ScaleGUITrad(64) + 2;
		size->height = ScaleGUITrad(48) + 2;
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		if (!IsInsideMM(widget, WID_BROD_DEPOT_NE, WID_BROD_DEPOT_NW + 1)) return;

		DrawRoadDepotSprite(r.left + 1 + ScaleGUITrad(31), r.bottom - ScaleGUITrad(31), (DiagDirection)(widget - WID_BROD_DEPOT_NE + DIAGDIR_NE), _cur_ltrailtype);
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_BROD_DEPOT_NW:
			case WID_BROD_DEPOT_NE:
			case WID_BROD_DEPOT_SW:
			case WID_BROD_DEPOT_SE:
				this->RaiseWidget(_road_depot_orientation + WID_BROD_DEPOT_NE);
				_road_depot_orientation = (DiagDirection)(widget - WID_BROD_DEPOT_NE);
				this->LowerWidget(_road_depot_orientation + WID_BROD_DEPOT_NE);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				break;

			default:
				break;
		}
	}
};

static const NWidgetPart _nested_build_road_depot_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_BROD_CAPTION), SetDataTip(STR_BUILD_DEPOT_ROAD_ORIENTATION_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
		NWidget(NWID_HORIZONTAL_LTR),
			NWidget(NWID_SPACER), SetMinimalSize(3, 0), SetFill(1, 0),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_PANEL, COLOUR_GREY, WID_BROD_DEPOT_NW), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_ROAD_ORIENTATION_SELECT_TOOLTIP),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2),
				NWidget(WWT_PANEL, COLOUR_GREY, WID_BROD_DEPOT_SW), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_ROAD_ORIENTATION_SELECT_TOOLTIP),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_PANEL, COLOUR_GREY, WID_BROD_DEPOT_NE), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_ROAD_ORIENTATION_SELECT_TOOLTIP),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2),
				NWidget(WWT_PANEL, COLOUR_GREY, WID_BROD_DEPOT_SE), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_ROAD_ORIENTATION_SELECT_TOOLTIP),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(3, 0), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
	EndContainer(),
};

static WindowDesc _build_road_depot_desc(
	WDP_AUTO, NULL, 0, 0,
	WC_BUILD_DEPOT, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_build_road_depot_widgets, lengthof(_nested_build_road_depot_widgets)
);

static void ShowRoadDepotPicker(Window *parent)
{
	new BuildRoadDepotWindow(&_build_road_depot_desc, parent);
}
