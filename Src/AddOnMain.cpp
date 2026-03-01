#include "APIEnvir.h"
#include "ACAPinc.h"

#include "ResourceIds.hpp"
#include "DGModule.hpp"

#include <iostream>
#include <fstream>
#include <ctime>

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

static const GSResID AddOnInfoID			= ID_ADDON_INFO;
	static const Int32 AddOnNameID			= 1;
	static const Int32 AddOnDescriptionID	= 2;

static const short AddOnMenuID				= ID_ADDON_MENU;
	static const Int32 AddOnCommandID		= 1;

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
	LogMessage ("MenuCommandHandler called");
	switch (menuParams->menuItemRef.menuResID) {
		case AddOnMenuID:
			switch (menuParams->menuItemRef.itemIndex) {
				case AddOnCommandID:
					{
						LogMessage ("Opening dialog...");
						ExampleDialog dialog;
						LogMessage ("Dialog created, invoking...");
						dialog.Invoke ();
						LogMessage ("Dialog closed");
					}
					break;
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
