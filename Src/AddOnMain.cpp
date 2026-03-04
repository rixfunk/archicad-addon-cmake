#include "APIEnvir.h"
#include "ACAPinc.h"

#include "ResourceIds.hpp"
#include "DGModule.hpp"
#include "LineTypeCleaner.hpp"

#include <iostream>
#include <fstream>
#include <ctime>
#include <sstream>

#define LOG_FILE "/tmp/ac28-addon.log"

static void LogMessage (const char* message)
{
	std::ofstream logFile (LOG_FILE, std::ios::app);
	if (logFile.is_open ()) {
		time_t now = time (nullptr);
		char timeStr[26];
		ctime_r (&now, timeStr);
		timeStr[24] = '\0';  // Remove newline
		logFile << "[" << timeStr << "] " << message << std::endl;
		logFile.close ();
	}
}

static void LogMessage (const std::string& message)
{
	LogMessage (message.c_str ());
}

// Get layer name from layer index
static GS::UniString GetLayerName (API_AttributeIndex layerIndex)
{
	API_Attribute attrib = {};
	attrib.header.typeID = API_LayerID;
	attrib.header.index = layerIndex;
	
	if (ACAPI_Attribute_Get (&attrib) == NoError) {
		return GS::UniString (attrib.header.name);
	}
	return GS::UniString ("Unknown Layer");
}

// Get element type name
static const char* GetElementTypeName (API_ElemType elemType)
{
	switch (elemType.typeID) {
		case API_WallID:		return "Wall";
		case API_ColumnID:		return "Column";
		case API_BeamID:		return "Beam";
		case API_SlabID:		return "Slab";
		case API_RoofID:		return "Roof";
		case API_MeshID:		return "Mesh";
		case API_ShellID:		return "Shell";
		case API_MorphID:		return "Morph";
		case API_DoorID:		return "Door";
		case API_WindowID:		return "Window";
		case API_SkylightID:	return "Skylight";
		case API_ObjectID:		return "Object";
		case API_LampID:		return "Lamp";
		case API_ZoneID:		return "Zone";
		case API_StairID:		return "Stair";
		case API_RailingID:		return "Railing";
		case API_CurtainWallID:	return "Curtain Wall";
		default:				return "Other";
	}
}

// Check if element type can be load-bearing
static bool IsStructuralElementType (API_ElemTypeID typeID)
{
	return (typeID == API_WallID || 
			typeID == API_ColumnID || 
			typeID == API_BeamID || 
			typeID == API_SlabID ||
			typeID == API_RoofID ||
			typeID == API_ShellID);
}

// List all elements with Layer, Element ID, and Load-bearing status
static void ListAllElements ()
{
	LogMessage ("=== ELEMENT LIST START ===");
	LogMessage ("Layer | Type | Element ID | Load-bearing");
	LogMessage ("------|------|------------|-------------");
	
	GS::Array<API_Guid> elemList;
	
	// Get all elements (API_ZombieElemID means all types)
	GSErrCode err = ACAPI_Element_GetElemList (API_ZombieElemID, &elemList, APIFilt_OnVisLayer);
	
	if (err != NoError) {
		std::ostringstream oss;
		oss << "Error getting element list: " << err;
		LogMessage (oss.str ());
		return;
	}
	
	std::ostringstream countMsg;
	countMsg << "Found " << elemList.GetSize () << " elements";
	LogMessage (countMsg.str ());
	
	int processedCount = 0;
	
	for (const API_Guid& guid : elemList) {
		API_Element element = {};
		element.header.guid = guid;
		
		err = ACAPI_Element_Get (&element);
		if (err != NoError) {
			continue;
		}
		
		// Get layer name
		GS::UniString layerName = GetLayerName (element.header.layer);
		
		// Get element type
		const char* typeName = GetElementTypeName (element.header.type);
		
		// Get Element ID (user-assigned ID string)
		GS::UniString elemId;
		API_ElementMemo memo = {};
		if (ACAPI_Element_GetMemo (guid, &memo, APIMemoMask_ElemInfoString) == NoError) {
			if (memo.elemInfoString != nullptr) {
				elemId = *memo.elemInfoString;
			}
			ACAPI_DisposeElemMemoHdls (&memo);
		}
		
		// Check load-bearing status
		// For structural elements, check the specific property
		std::string loadBearing = "-";
		if (IsStructuralElementType (element.header.type.typeID)) {
			// For walls, check if it's a structural wall
			if (element.header.type.typeID == API_WallID) {
				// Walls don't have a direct load-bearing flag in basic API
				// Would need to check IFC properties or classifications
				loadBearing = "Check IFC";
			} else if (element.header.type.typeID == API_ColumnID ||
					   element.header.type.typeID == API_BeamID) {
				// Columns and beams are typically load-bearing
				loadBearing = "Yes (structural)";
			} else if (element.header.type.typeID == API_SlabID) {
				loadBearing = "Check props";
			}
		}
		
		// Format and log the element info
		std::ostringstream oss;
		oss << layerName.ToCStr ().Get () << " | "
			<< typeName << " | "
			<< (elemId.IsEmpty () ? "(no ID)" : elemId.ToCStr ().Get ()) << " | "
			<< loadBearing;
		LogMessage (oss.str ());
		
		processedCount++;
		
		// Limit output to first 100 elements to avoid huge logs
		if (processedCount >= 100) {
			LogMessage ("... (limited to 100 elements)");
			break;
		}
	}
	
	LogMessage ("=== ELEMENT LIST END ===");
}

static const GSResID AddOnInfoID			= ID_ADDON_INFO;
	static const Int32 AddOnNameID			= 1;
	static const Int32 AddOnDescriptionID	= 2;

static const short AddOnMenuID				= ID_ADDON_MENU;

class ExampleDialog :	public DG::ModalDialog,
						public DG::PanelObserver,
						public DG::ButtonItemObserver,
						public DG::CompoundItemObserver
{
public:
	enum DialogResourceIds
	{
		ExampleDialogResourceId = ID_ADDON_DLG,
		OKButtonId = 1,
		CancelButtonId = 2,
		SeparatorId = 3
	};

	ExampleDialog () :
		DG::ModalDialog (ACAPI_GetOwnResModule (), ExampleDialogResourceId, ACAPI_GetOwnResModule ()),
		okButton (GetReference (), OKButtonId),
		cancelButton (GetReference (), CancelButtonId),
		separator (GetReference (), SeparatorId)
	{
		SetTitle (ADDON_NAME " " ADDON_VERSION);
		AttachToAllItems (*this);
		Attach (*this);
	}

	~ExampleDialog ()
	{
		Detach (*this);
		DetachFromAllItems (*this);
	}

private:
	virtual void PanelResized (const DG::PanelResizeEvent& ev) override
	{
		BeginMoveResizeItems ();
		okButton.Move (ev.GetHorizontalChange (), ev.GetVerticalChange ());
		cancelButton.Move (ev.GetHorizontalChange (), ev.GetVerticalChange ());
		separator.MoveAndResize (0, ev.GetVerticalChange (), ev.GetHorizontalChange (), 0);
		EndMoveResizeItems ();
	}

	virtual void ButtonClicked (const DG::ButtonClickEvent& ev) override
	{
		if (ev.GetSource () == &okButton) {
			PostCloseRequest (DG::ModalDialog::Accept);
		} else if (ev.GetSource () == &cancelButton) {
			PostCloseRequest (DG::ModalDialog::Cancel);
		}
	}

	DG::Button		okButton;
	DG::Button		cancelButton;
	DG::Separator	separator;
};

static GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	std::ostringstream debugMsg;
	debugMsg << "MenuCommandHandler called - menuResID: " << menuParams->menuItemRef.menuResID
			 << ", itemIndex: " << menuParams->menuItemRef.itemIndex;
	LogMessage (debugMsg.str ());

	switch (menuParams->menuItemRef.menuResID) {
		case AddOnMenuID:
			{
				// ArchiCAD sends itemIndex based on position in STR# resource
				// With submenu headers, first 2 strings are headers, commands start at index 1
				// But ArchiCAD may offset by the number of header strings
				Int32 idx = menuParams->menuItemRef.itemIndex;

				// Based on log: "List Elements..." = index 2, "Clean Line Types..." = index 3
				// The first two STR# entries are submenu headers, commands are offset by 1
				if (idx == 1 || idx == 2) {
					// List Elements command (index 2 observed, index 1 as fallback)
					LogMessage ("Executing: List Elements...");
					ListAllElements ();
					LogMessage ("Element listing complete. Opening dialog...");
					ExampleDialog dialog;
					dialog.Invoke ();
					LogMessage ("Dialog closed");
				} else if (idx == 3 || idx == 4) {
					// Clean Line Types command (index 3 observed, index 4 as fallback)
					LogMessage ("Executing: Clean Line Types...");
					LineTypeCleaningDialog cleanerDialog;
					if (cleanerDialog.Invoke ()) {
						std::ostringstream oss;
						oss << "Line Type Cleaner: " << cleanerDialog.GetChangesApplied () << " changes applied";
						LogMessage (oss.str ());
					} else {
						LogMessage ("Line Type Cleaner cancelled");
					}
				} else {
					std::ostringstream oss;
					oss << "Unknown menu itemIndex: " << idx;
					LogMessage (oss.str ());
				}
			}
			break;
	}
	return NoError;
}

API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	LogMessage ("CheckEnvironment called");
	RSGetIndString (&envir->addOnInfo.name, AddOnInfoID, AddOnNameID, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, AddOnInfoID, AddOnDescriptionID, ACAPI_GetOwnResModule ());

	LogMessage ("CheckEnvironment completed");
	return APIAddon_Normal;
}

GSErrCode RegisterInterface (void)
{
	LogMessage ("RegisterInterface called");
#ifdef ServerMainVers_2700
	GSErrCode err = ACAPI_MenuItem_RegisterMenu (AddOnMenuID, 0, MenuCode_Tools, MenuFlag_Default);
#else
	GSErrCode err = ACAPI_Register_Menu (AddOnMenuID, 0, MenuCode_Tools, MenuFlag_Default);
#endif
	LogMessage (err == NoError ? "RegisterInterface succeeded" : "RegisterInterface FAILED");
	return err;
}

GSErrCode Initialize (void)
{
	LogMessage ("Initialize called");
#ifdef ServerMainVers_2700
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (AddOnMenuID, MenuCommandHandler);
#else
	GSErrCode err = ACAPI_Install_MenuHandler (AddOnMenuID, MenuCommandHandler);
#endif
	LogMessage (err == NoError ? "Initialize succeeded" : "Initialize FAILED");
	return err;
}

GSErrCode FreeData (void)
{
	LogMessage ("FreeData called");
	return NoError;
}
