#ifndef LINETYPECLEANER_HPP
#define LINETYPECLEANER_HPP

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "DGModule.hpp"

#include <vector>
#include <string>
#include <set>
#include <map>

// Forward declaration for logging
void LogLineTypeCleanerMessage (const char* message);
void LogLineTypeCleanerMessage (const std::string& message);

// Data structure for a line type in the project
struct ProjectLineType {
	API_AttributeIndex	index;
	GS::UniString		name;
	Int32				usageCount;		// Number of elements using this line type
	bool				isAllowed;		// Whether it's in the allowed list
};

// LineTypeCleaner: Business logic for cleaning line types
class LineTypeCleaner {
public:
	LineTypeCleaner ();
	~LineTypeCleaner ();

	// Load allowed line types from ArchiCAD XML attribute file
	bool LoadAllowedFromXML (const IO::Location& xmlPath);

	// Add allowed line type name manually
	void AddAllowedLineType (const GS::UniString& name);

	// Clear all allowed line types
	void ClearAllowedLineTypes ();

	// Scan project for line types and count usage
	bool ScanProject ();

	// Get results
	const std::vector<ProjectLineType>& GetProjectLineTypes () const { return projectLineTypes; }
	const std::set<GS::UniString>& GetAllowedLineTypes () const { return allowedLineTypes; }

	// Get only non-standard line types
	std::vector<ProjectLineType> GetNonStandardLineTypes () const;

	// Count elements using a specific line type
	Int32 CountElementsUsingLineType (API_AttributeIndex ltypeIndex);

	// Find all element GUIDs using a specific line type
	GS::Array<API_Guid> GetElementsUsingLineType (API_AttributeIndex ltypeIndex);

	// Batch replace line types (returns number of elements changed)
	Int32 ReplaceLineTypes (const GS::Array<API_AttributeIndex>& sourceTypes,
						   API_AttributeIndex targetType);

	// Get line type by name
	bool GetLineTypeByName (const GS::UniString& name, API_AttributeIndex& outIndex);

private:
	std::set<GS::UniString>			allowedLineTypes;	// Names of allowed line types
	std::vector<ProjectLineType>	projectLineTypes;	// Line types found in project

	// Parse XML file for line type names
	bool ParseArchiCADXML (const IO::Location& xmlPath);

	// Count usage for a line type across all element types
	Int32 CountUsageForLineType (API_AttributeIndex ltypeIndex);
};

// Line Type Cleaning Dialog
class LineTypeCleaningDialog : public DG::ModalDialog,
							   public DG::PanelObserver,
							   public DG::ButtonItemObserver,
							   public DG::CheckItemObserver,
							   public DG::PopUpObserver,
							   public DG::ListBoxObserver,
							   public DG::CompoundItemObserver
{
public:
	LineTypeCleaningDialog ();
	~LineTypeCleaningDialog ();

	// Get number of changes made
	Int32 GetChangesApplied () const { return changesApplied; }

private:
	enum DialogResourceIds {
		DialogId = 32601,
		ListBoxId = 1,
		FilterCheckBoxId = 2,
		ReplaceLabelId = 3,
		ReplacementPopUpId = 4,
		ApplyButtonId = 5,
		CancelButtonId = 6,
		SeparatorId = 7,
		StatusTextId = 8,
		ImportXMLButtonId = 9
	};

	// Column indices for the list box (1-based)
	enum ColumnIndices {
		ColName = 1,
		ColStatus = 2,
		ColUsage = 3,
		ColReplacement = 4,
		NumColumns = 4
	};

	// UI controls
	DG::MultiSelListBox		lineTypeList;
	DG::CheckBox			filterCheckBox;
	DG::LeftText			replaceLabel;
	DG::PopUp				replacementPopUp;
	DG::Button				applyButton;
	DG::Button				cancelButton;
	DG::Separator			separator;
	DG::LeftText			statusText;
	DG::Button				importXMLButton;

	// Data
	LineTypeCleaner			cleaner;
	Int32					changesApplied;
	bool					showOnlyNonStandard;
	short					editingRowIndex;	// Row being edited (-1 if none)

	// Map: source line type index -> target replacement index
	// Key: API_AttributeIndex as Int32, Value: replacement API_AttributeIndex as Int32
	std::map<Int32, Int32>	replacementMap;

	// List of allowed line types with their indices (for dropdown)
	std::vector<std::pair<GS::UniString, API_AttributeIndex>>	allowedLineTypesList;

	// Per-row popup controls for NON-STD rows
	std::vector<DG::PopUp*>	rowPopups;
	std::map<DG::PopUp*, Int32>	popupToSourceIndex;	// Map popup -> source line type index

	// Setup methods
	void SetupListBoxColumns ();
	void PopulateLineTypes ();
	void PopulateReplacementPopUp ();
	void BuildAllowedLineTypesList ();
	void UpdateStatusText ();
	void RefreshList ();
	void CreateRowPopups ();
	void ClearRowPopups ();

	// Action methods
	void ApplyReplacement ();
	void ImportXMLFile ();
	void ShowReplacementPopUpForRow (short rowIndex);
	void UpdateRowReplacement (short rowIndex, API_AttributeIndex replacementIndex);
	GS::UniString GetReplacementNameForRow (short rowIndex);

	// Observer overrides
	virtual void PanelOpened (const DG::PanelOpenEvent& ev) override;
	virtual void PanelClosed (const DG::PanelCloseEvent& ev) override;
	virtual void PanelResized (const DG::PanelResizeEvent& ev) override;
	virtual void PanelIdle (const DG::PanelIdleEvent& ev) override;
	virtual void ButtonClicked (const DG::ButtonClickEvent& ev) override;
	virtual void CheckItemChanged (const DG::CheckItemChangeEvent& ev) override;
	virtual void PopUpChanged (const DG::PopUpChangeEvent& ev) override;
	virtual void ListBoxSelectionChanged (const DG::ListBoxSelectionEvent& ev) override;
	virtual void ListBoxDoubleClicked (const DG::ListBoxDoubleClickEvent& ev) override;

	// Scroll tracking
	void RepositionPopupAtEditingRow ();
	short lastPopupY;
};

#endif // LINETYPECLEANER_HPP
