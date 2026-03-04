#include "LineTypeCleaner.hpp"
#include "ResourceIds.hpp"
#include "DGFileDlg.hpp"

#include <fstream>
#include <sstream>
#include <ctime>

#ifdef macintosh
#include <dispatch/dispatch.h>
#endif

// Logging helpers
void LogLineTypeCleanerMessage (const char* message)
{
	std::ofstream logFile ("/tmp/ac28-addon.log", std::ios::app);
	if (logFile.is_open ()) {
		time_t now = time (nullptr);
		char timeStr[26];
		ctime_r (&now, timeStr);
		timeStr[24] = '\0';
		logFile << "[" << timeStr << "] [LTC] " << message << std::endl;
		logFile.close ();
	}
}

void LogLineTypeCleanerMessage (const std::string& message)
{
	LogLineTypeCleanerMessage (message.c_str ());
}

// ============================================================================
// LineTypeCleaner Implementation
// ============================================================================

LineTypeCleaner::LineTypeCleaner ()
{
	LogLineTypeCleanerMessage ("LineTypeCleaner created");
}

LineTypeCleaner::~LineTypeCleaner ()
{
	LogLineTypeCleanerMessage ("LineTypeCleaner destroyed");
}

void LineTypeCleaner::AddAllowedLineType (const GS::UniString& name)
{
	allowedLineTypes.insert (name);
}

void LineTypeCleaner::ClearAllowedLineTypes ()
{
	allowedLineTypes.clear ();
}

bool LineTypeCleaner::LoadAllowedFromXML (const IO::Location& xmlPath)
{
	LogLineTypeCleanerMessage ("Loading allowed line types from XML...");
	return ParseArchiCADXML (xmlPath);
}

bool LineTypeCleaner::ParseArchiCADXML (const IO::Location& xmlPath)
{
	// Simple XML parsing for ArchiCAD attribute files
	// Looking for <Name>...</Name> tags within <LineType> elements

	IO::File xmlFile (xmlPath);
	if (xmlFile.Open (IO::File::ReadMode) != NoError) {
		LogLineTypeCleanerMessage ("Failed to open XML file");
		return false;
	}

	// Read entire file
	UInt64 fileSize = 0;
	xmlFile.GetDataLength (&fileSize);

	if (fileSize == 0 || fileSize > 10 * 1024 * 1024) { // Max 10MB
		LogLineTypeCleanerMessage ("XML file too large or empty");
		xmlFile.Close ();
		return false;
	}

	char* buffer = new char[fileSize + 1];
	USize bytesRead = 0;
	xmlFile.ReadBin (buffer, static_cast<USize>(fileSize), &bytesRead);
	buffer[bytesRead] = '\0';
	xmlFile.Close ();

	std::string content (buffer);
	delete[] buffer;

	std::ostringstream logOss;
	logOss << "XML file size: " << bytesRead << " bytes";
	LogLineTypeCleanerMessage (logOss.str ());

	// Parse for LineType names
	// Looking for patterns like: <Name>LineName</Name> within LineType context
	ClearAllowedLineTypes ();

	size_t pos = 0;
	int blocksFound = 0;

	while (pos < content.length ()) {
		// Look for <LineType (ArchiCAD format: <LineType Idx="1" Name="...">)
		size_t ltStart = content.find ("<LineType", pos);
		if (ltStart == std::string::npos) {
			break;
		}

		// Find end of this LineType block
		size_t nextLtEnd = content.find ("</LineType>", ltStart);
		if (nextLtEnd == std::string::npos) {
			std::ostringstream oss;
			oss << "  Found <LineType at pos " << ltStart << " but no closing tag";
			LogLineTypeCleanerMessage (oss.str ());
			break;
		}

		blocksFound++;
		std::string lineTypeBlock = content.substr (ltStart, nextLtEnd - ltStart);

		std::string name;

		// Method 1: Try to get Name from attribute: <LineType Idx="1" Name="Volllinie">
		size_t nameAttrPos = lineTypeBlock.find ("Name=\"");
		if (nameAttrPos != std::string::npos) {
			size_t nameStart = nameAttrPos + 6; // Skip past Name="
			size_t nameEnd = lineTypeBlock.find ("\"", nameStart);
			if (nameEnd != std::string::npos) {
				name = lineTypeBlock.substr (nameStart, nameEnd - nameStart);
			}
		}

		// Method 2: If no attribute, try <Name>...</Name> tag inside the block
		if (name.empty ()) {
			size_t nameTagStart = lineTypeBlock.find ("<Name>");
			size_t nameTagEnd = lineTypeBlock.find ("</Name>");
			if (nameTagStart != std::string::npos && nameTagEnd != std::string::npos && nameTagEnd > nameTagStart) {
				name = lineTypeBlock.substr (nameTagStart + 6, nameTagEnd - nameTagStart - 6);
			}
		}

		// Trim whitespace from name
		if (!name.empty ()) {
			size_t start = name.find_first_not_of (" \t\n\r");
			size_t end = name.find_last_not_of (" \t\n\r");
			if (start != std::string::npos && end != std::string::npos) {
				name = name.substr (start, end - start + 1);
			}
		}

		if (!name.empty ()) {
			AddAllowedLineType (GS::UniString (name.c_str (), CC_UTF8));
			std::ostringstream oss;
			oss << "  Found allowed line type #" << blocksFound << ": [" << name << "]";
			LogLineTypeCleanerMessage (oss.str ());
		} else {
			std::ostringstream oss;
			oss << "  LineType block #" << blocksFound << " has no Name attribute or tag";
			LogLineTypeCleanerMessage (oss.str ());
		}

		pos = nextLtEnd + 11; // Move past </LineType>
	}

	std::ostringstream oss;
	oss << "Parsed " << blocksFound << " LineType blocks, loaded " << allowedLineTypes.size () << " allowed line types";
	LogLineTypeCleanerMessage (oss.str ());

	return !allowedLineTypes.empty ();
}

bool LineTypeCleaner::ScanProject ()
{
	LogLineTypeCleanerMessage ("Scanning project for line types...");
	projectLineTypes.clear ();

	// Get number of line types
	API_AttrTypeID attrType = API_LinetypeID;
	UInt32 count = 0;

	GSErrCode err = ACAPI_Attribute_GetNum (attrType, count);
	if (err != NoError) {
		LogLineTypeCleanerMessage ("Failed to get line type count");
		return false;
	}

	std::ostringstream oss;
	oss << "Found " << count << " line types in project";
	LogLineTypeCleanerMessage (oss.str ());

	// Iterate through all line types
	for (Int32 i = 1; i <= static_cast<Int32>(count); ++i) {
		API_AttributeIndex attrIdx = ACAPI_CreateAttributeIndex (i);
		API_Attribute attr = {};
		attr.header.typeID = API_LinetypeID;
		attr.header.index = attrIdx;

		if (ACAPI_Attribute_Get (&attr) == NoError) {
			ProjectLineType plt;
			plt.index = attrIdx;
			plt.name = GS::UniString (attr.header.name);
			plt.usageCount = CountUsageForLineType (attrIdx);
			plt.isAllowed = (allowedLineTypes.find (plt.name) != allowedLineTypes.end ());

			projectLineTypes.push_back (plt);

			std::ostringstream info;
			info << "  " << plt.name.ToCStr ().Get ()
				 << " (index " << i << ")"
				 << " usage: " << plt.usageCount
				 << (plt.isAllowed ? " [OK]" : " [NON-STD]");
			LogLineTypeCleanerMessage (info.str ());
		}
	}

	return true;
}

std::vector<ProjectLineType> LineTypeCleaner::GetNonStandardLineTypes () const
{
	std::vector<ProjectLineType> result;
	for (const auto& plt : projectLineTypes) {
		if (!plt.isAllowed) {
			result.push_back (plt);
		}
	}
	return result;
}

Int32 LineTypeCleaner::CountUsageForLineType (API_AttributeIndex ltypeIndex)
{
	// Count elements using this line type
	// Check LINE elements (2D lines)
	Int32 totalCount = 0;

	GS::Array<API_Guid> lineElements;
	if (ACAPI_Element_GetElemList (API_LineID, &lineElements) == NoError) {
		for (const API_Guid& guid : lineElements) {
			API_Element elem = {};
			elem.header.guid = guid;
			if (ACAPI_Element_Get (&elem) == NoError) {
				if (elem.line.ltypeInd == ltypeIndex) {
					totalCount++;
				}
			}
		}
	}

	// Check ARC elements
	GS::Array<API_Guid> arcElements;
	if (ACAPI_Element_GetElemList (API_ArcID, &arcElements) == NoError) {
		for (const API_Guid& guid : arcElements) {
			API_Element elem = {};
			elem.header.guid = guid;
			if (ACAPI_Element_Get (&elem) == NoError) {
				if (elem.arc.ltypeInd == ltypeIndex) {
					totalCount++;
				}
			}
		}
	}

	// Check POLYLINE elements
	GS::Array<API_Guid> polyElements;
	if (ACAPI_Element_GetElemList (API_PolyLineID, &polyElements) == NoError) {
		for (const API_Guid& guid : polyElements) {
			API_Element elem = {};
			elem.header.guid = guid;
			if (ACAPI_Element_Get (&elem) == NoError) {
				if (elem.polyLine.ltypeInd == ltypeIndex) {
					totalCount++;
				}
			}
		}
	}

	return totalCount;
}

Int32 LineTypeCleaner::CountElementsUsingLineType (API_AttributeIndex ltypeIndex)
{
	return CountUsageForLineType (ltypeIndex);
}

GS::Array<API_Guid> LineTypeCleaner::GetElementsUsingLineType (API_AttributeIndex ltypeIndex)
{
	GS::Array<API_Guid> result;

	// Get LINE elements
	GS::Array<API_Guid> lineElements;
	if (ACAPI_Element_GetElemList (API_LineID, &lineElements) == NoError) {
		for (const API_Guid& guid : lineElements) {
			API_Element elem = {};
			elem.header.guid = guid;
			if (ACAPI_Element_Get (&elem) == NoError) {
				if (elem.line.ltypeInd == ltypeIndex) {
					result.Push (guid);
				}
			}
		}
	}

	// Get ARC elements
	GS::Array<API_Guid> arcElements;
	if (ACAPI_Element_GetElemList (API_ArcID, &arcElements) == NoError) {
		for (const API_Guid& guid : arcElements) {
			API_Element elem = {};
			elem.header.guid = guid;
			if (ACAPI_Element_Get (&elem) == NoError) {
				if (elem.arc.ltypeInd == ltypeIndex) {
					result.Push (guid);
				}
			}
		}
	}

	// Get POLYLINE elements
	GS::Array<API_Guid> polyElements;
	if (ACAPI_Element_GetElemList (API_PolyLineID, &polyElements) == NoError) {
		for (const API_Guid& guid : polyElements) {
			API_Element elem = {};
			elem.header.guid = guid;
			if (ACAPI_Element_Get (&elem) == NoError) {
				if (elem.polyLine.ltypeInd == ltypeIndex) {
					result.Push (guid);
				}
			}
		}
	}

	return result;
}

bool LineTypeCleaner::GetLineTypeByName (const GS::UniString& name, API_AttributeIndex& outIndex)
{
	for (const auto& plt : projectLineTypes) {
		if (plt.name == name) {
			outIndex = plt.index;
			return true;
		}
	}
	return false;
}

Int32 LineTypeCleaner::ReplaceLineTypes (const GS::Array<API_AttributeIndex>& sourceTypes,
										API_AttributeIndex targetType)
{
	Int32 changedCount = 0;

	std::ostringstream oss;
	oss << "Replacing " << sourceTypes.GetSize () << " line type(s) with index " << targetType.ToInt32_Deprecated ();
	LogLineTypeCleanerMessage (oss.str ());

	// Collect all elements using any of the source types
	for (const API_AttributeIndex& srcType : sourceTypes) {
		GS::Array<API_Guid> elements = GetElementsUsingLineType (srcType);

		if (elements.IsEmpty ()) {
			continue;
		}

		// Change each element's line type
		for (const API_Guid& guid : elements) {
			API_Element element = {};
			element.header.guid = guid;

			GSErrCode err = ACAPI_Element_Get (&element);
			if (err != NoError) {
				continue;
			}

			API_Element mask = {};

			// Determine element type and set appropriate fields
			API_AttributeIndex maskIndex = ACAPI_CreateAttributeIndex (1);
			switch (element.header.type.typeID) {
				case API_LineID:
					element.line.ltypeInd = targetType;
					mask.line.ltypeInd = maskIndex;
					break;
				case API_ArcID:
					element.arc.ltypeInd = targetType;
					mask.arc.ltypeInd = maskIndex;
					break;
				case API_PolyLineID:
					element.polyLine.ltypeInd = targetType;
					mask.polyLine.ltypeInd = maskIndex;
					break;
				default:
					continue;
			}

			err = ACAPI_Element_Change (&element, &mask, nullptr, 0, false);
			if (err == NoError) {
				changedCount++;
			}
		}
	}

	oss.str ("");
	oss << "Changed " << changedCount << " elements";
	LogLineTypeCleanerMessage (oss.str ());

	return changedCount;
}

// ============================================================================
// LineTypeCleaningDialog Implementation
// ============================================================================

LineTypeCleaningDialog::LineTypeCleaningDialog () :
	DG::ModalDialog (ACAPI_GetOwnResModule (), DialogId, ACAPI_GetOwnResModule ()),
	lineTypeList (GetReference (), ListBoxId),
	filterCheckBox (GetReference (), FilterCheckBoxId),
	replaceLabel (GetReference (), ReplaceLabelId),
	replacementPopUp (GetReference (), ReplacementPopUpId),
	applyButton (GetReference (), ApplyButtonId),
	cancelButton (GetReference (), CancelButtonId),
	separator (GetReference (), SeparatorId),
	statusText (GetReference (), StatusTextId),
	importXMLButton (GetReference (), ImportXMLButtonId),
	changesApplied (0),
	showOnlyNonStandard (false),
	editingRowIndex (-1),
	lastPopupY (-1)
{
	SetTitle ("Line Type Cleaner");

	Attach (*this);
	AttachToAllItems (*this);

	// Also attach to list box for events
	lineTypeList.Attach (*this);

	// Setup list box columns
	SetupListBoxColumns ();

	// Start with no allowed line types - user must import XML
	// (Do not add hardcoded defaults)

	// Scan project
	cleaner.ScanProject ();

	// Setup UI
	PopulateReplacementPopUp ();
	PopulateLineTypes ();
	UpdateStatusText ();

	// Popup at fixed position below the list, disabled until NON-STD rows are selected
	replacementPopUp.Show ();
	replacementPopUp.Disable ();

	LogLineTypeCleanerMessage ("LineTypeCleaningDialog created");
}

LineTypeCleaningDialog::~LineTypeCleaningDialog ()
{
	lineTypeList.Detach (*this);
	Detach (*this);
	DetachFromAllItems (*this);
	LogLineTypeCleanerMessage ("LineTypeCleaningDialog destroyed");
}

void LineTypeCleaningDialog::ClearRowPopups ()
{
	// No longer used - single popup approach
}

void LineTypeCleaningDialog::CreateRowPopups ()
{
	// No longer used - single popup approach, embedded on selection
}

void LineTypeCleaningDialog::SetupListBoxColumns ()
{
	LogLineTypeCleanerMessage ("Setting up list box columns...");

	// Set row height to accommodate popup control (default is ~18, increase to 24)
	lineTypeList.SetItemHeight (24);

	// Setup 4 columns
	lineTypeList.SetTabFieldCount (NumColumns);
	lineTypeList.SetHeaderItemCount (NumColumns);

	// Column widths (total ~680)
	const short colNameWidth = 280;
	const short colStatusWidth = 100;
	const short colUsageWidth = 80;
	const short colReplacementWidth = 220;

	short pos = 0;

	// Column 1: Line Type Name
	lineTypeList.SetTabFieldProperties (ColName, pos, pos + colNameWidth,
										DG::ListBox::Left, DG::ListBox::EndTruncate, true);
	lineTypeList.SetHeaderItemText (ColName, "Line Type");
	pos += colNameWidth;

	// Column 2: Status (OK / NON-STD)
	lineTypeList.SetTabFieldProperties (ColStatus, pos, pos + colStatusWidth,
										DG::ListBox::Center, DG::ListBox::NoTruncate, true);
	lineTypeList.SetHeaderItemText (ColStatus, "Status");
	pos += colStatusWidth;

	// Column 3: Usage count
	lineTypeList.SetTabFieldProperties (ColUsage, pos, pos + colUsageWidth,
										DG::ListBox::Right, DG::ListBox::NoTruncate, true);
	lineTypeList.SetHeaderItemText (ColUsage, "Usage");
	pos += colUsageWidth;

	// Column 4: Replacement (placeholder for now)
	lineTypeList.SetTabFieldProperties (ColReplacement, pos, pos + colReplacementWidth,
										DG::ListBox::Left, DG::ListBox::EndTruncate, false);
	lineTypeList.SetHeaderItemText (ColReplacement, "Replacement");

	LogLineTypeCleanerMessage ("List box columns configured");
}

void LineTypeCleaningDialog::PopulateLineTypes ()
{
	const auto& lineTypes = cleaner.GetProjectLineTypes ();

	std::ostringstream oss;
	oss << "PopulateLineTypes: " << lineTypes.size () << " line types to display";
	LogLineTypeCleanerMessage (oss.str ());

	// Clear existing items
	lineTypeList.DeleteItem (DG::ListBox::AllItems);

	Int32 displayCount = 0;

	for (const auto& lt : lineTypes) {
		if (showOnlyNonStandard && lt.isAllowed) {
			continue;
		}

		// Add item to list
		lineTypeList.AppendItem ();
		short itemIndex = lineTypeList.GetItemCount ();

		// Column 1: Line Type Name
		lineTypeList.SetTabItemText (itemIndex, ColName, lt.name);

		// Column 2: Status
		lineTypeList.SetTabItemText (itemIndex, ColStatus, lt.isAllowed ? "OK" : "NON-STD");

		// Column 3: Usage count
		GS::UniString usageStr;
		usageStr.Printf ("%d", lt.usageCount);
		lineTypeList.SetTabItemText (itemIndex, ColUsage, usageStr);

		// Column 4: Replacement
		if (lt.isAllowed) {
			lineTypeList.SetTabItemText (itemIndex, ColReplacement, "-");
		} else {
			// For non-standard types, show the selected replacement or default to first allowed
			GS::UniString replacementName = GetReplacementNameForRow (itemIndex);
			lineTypeList.SetTabItemText (itemIndex, ColReplacement, replacementName);

			// If no replacement set yet, set default to first allowed type
			Int32 sourceIdx = lt.index.ToInt32_Deprecated ();
			if (replacementMap.find (sourceIdx) == replacementMap.end () && !allowedLineTypesList.empty ()) {
				replacementMap[sourceIdx] = allowedLineTypesList[0].second.ToInt32_Deprecated ();
			}
		}

		// Store line type index for later reference
		lineTypeList.SetItemValue (itemIndex, static_cast<DGUserData>(lt.index.ToInt32_Deprecated ()));

		displayCount++;
	}

	std::ostringstream oss2;
	oss2 << "PopulateLineTypes: Displayed " << displayCount << " items";
	LogLineTypeCleanerMessage (oss2.str ());
}

GS::UniString LineTypeCleaningDialog::GetReplacementNameForRow (short rowIndex)
{
	// Get the source line type index from the row
	DGUserData sourceValue = lineTypeList.GetItemValue (rowIndex);
	Int32 sourceIdx = static_cast<Int32>(sourceValue);

	// Check if we have a replacement set
	auto it = replacementMap.find (sourceIdx);
	if (it != replacementMap.end ()) {
		// Find the name of the replacement
		Int32 targetIdx = it->second;
		for (const auto& allowed : allowedLineTypesList) {
			if (allowed.second.ToInt32_Deprecated () == targetIdx) {
				return allowed.first;
			}
		}
	}

	// Default to first allowed type if any
	if (!allowedLineTypesList.empty ()) {
		return allowedLineTypesList[0].first;
	}

	return GS::UniString ("(none)");
}

void LineTypeCleaningDialog::PopulateReplacementPopUp ()
{
	replacementPopUp.DeleteItem (DG::PopUp::AllItems);

	const auto& lineTypes = cleaner.GetProjectLineTypes ();

	for (const auto& lt : lineTypes) {
		if (lt.isAllowed) {
			replacementPopUp.AppendItem ();
			short itemIndex = replacementPopUp.GetItemCount ();
			replacementPopUp.SetItemText (itemIndex, lt.name);
			replacementPopUp.SetItemValue (itemIndex, static_cast<DGUserData>(lt.index.ToInt32_Deprecated ()));
		}
	}

	if (replacementPopUp.GetItemCount () > 0) {
		replacementPopUp.SelectItem (1);
	}

	// Also build the allowed line types list for per-row selection
	BuildAllowedLineTypesList ();
}

void LineTypeCleaningDialog::BuildAllowedLineTypesList ()
{
	allowedLineTypesList.clear ();

	const auto& lineTypes = cleaner.GetProjectLineTypes ();
	for (const auto& lt : lineTypes) {
		if (lt.isAllowed) {
			allowedLineTypesList.push_back (std::make_pair (lt.name, lt.index));
		}
	}

	std::ostringstream oss;
	oss << "BuildAllowedLineTypesList: " << allowedLineTypesList.size () << " allowed types";
	LogLineTypeCleanerMessage (oss.str ());
}

void LineTypeCleaningDialog::UpdateStatusText ()
{
	const auto& lineTypes = cleaner.GetProjectLineTypes ();
	const auto& allowedTypes = cleaner.GetAllowedLineTypes ();
	Int32 nonStdCount = 0;
	Int32 okCount = 0;
	for (const auto& lt : lineTypes) {
		if (!lt.isAllowed) {
			nonStdCount++;
		} else {
			okCount++;
		}
	}

	GS::UniString status;
	status.Printf ("%d OK, %d non-standard (imported: %d)", okCount, nonStdCount, static_cast<Int32>(allowedTypes.size ()));
	statusText.SetText (status);

	// Log the allowed types for debugging
	std::ostringstream oss;
	oss << "UpdateStatusText: " << allowedTypes.size () << " allowed types loaded:";
	LogLineTypeCleanerMessage (oss.str ());
	for (const auto& name : allowedTypes) {
		std::ostringstream nameOss;
		nameOss << "  - " << name.ToCStr ().Get ();
		LogLineTypeCleanerMessage (nameOss.str ());
	}
}

void LineTypeCleaningDialog::RefreshList ()
{
	PopulateLineTypes ();
	UpdateStatusText ();
}

void LineTypeCleaningDialog::ApplyReplacement ()
{
	LogLineTypeCleanerMessage ("Applying per-row replacements...");

	if (replacementMap.empty ()) {
		LogLineTypeCleanerMessage ("No replacements configured");
		DG::WarningAlert ("No Changes", "No replacement mappings configured.\nSelect a NON-STD row and use the dropdown to set a replacement.", "OK");
		return;
	}

	// Count how many replacements we'll do
	std::ostringstream preOss;
	preOss << "Replacement map has " << replacementMap.size () << " entries";
	LogLineTypeCleanerMessage (preOss.str ());

	changesApplied = 0;

	// Wrap in undoable command
	GSErrCode err = ACAPI_CallUndoableCommand ("Replace Line Types",
		[&] () -> GSErrCode {
			// Apply each source->target replacement individually
			for (const auto& mapping : replacementMap) {
				Int32 sourceIdx = mapping.first;
				Int32 targetIdx = mapping.second;

				// Skip if source == target
				if (sourceIdx == targetIdx) {
					continue;
				}

				GS::Array<API_AttributeIndex> singleSource;
				singleSource.Push (ACAPI_CreateAttributeIndex (sourceIdx));
				API_AttributeIndex targetType = ACAPI_CreateAttributeIndex (targetIdx);

				Int32 count = cleaner.ReplaceLineTypes (singleSource, targetType);
				changesApplied += count;

				std::ostringstream oss;
				oss << "  Replaced source " << sourceIdx << " -> target " << targetIdx << ": " << count << " elements";
				LogLineTypeCleanerMessage (oss.str ());
			}
			return NoError;
		});

	if (err == NoError) {
		std::ostringstream oss;
		oss << "Applied " << changesApplied << " total changes";
		LogLineTypeCleanerMessage (oss.str ());

		// Clear replacement map since changes are applied
		replacementMap.clear ();

		// Rescan and refresh
		cleaner.ScanProject ();
		RefreshList ();

		// Close dialog with success if changes were made
		if (changesApplied > 0) {
			PostCloseRequest (DG::ModalDialog::Accept);
		}
	} else {
		LogLineTypeCleanerMessage ("Failed to apply changes");
		DG::WarningAlert ("Error", "Failed to apply line type replacements.", "OK");
	}
}

void LineTypeCleaningDialog::PanelOpened (const DG::PanelOpenEvent& /*ev*/)
{
	// Additional setup after dialog opens
}

void LineTypeCleaningDialog::PanelClosed (const DG::PanelCloseEvent& /*ev*/)
{
	// Cleanup
}

void LineTypeCleaningDialog::PanelIdle (const DG::PanelIdleEvent& /*ev*/)
{
	// Not used - popup is at fixed position
}

void LineTypeCleaningDialog::PanelResized (const DG::PanelResizeEvent& ev)
{
	short hGrow = ev.GetHorizontalChange ();
	short vGrow = ev.GetVerticalChange ();

	BeginMoveResizeItems ();

	// Resize list box to fill available space (grow with dialog)
	lineTypeList.Resize (hGrow, vGrow);

	// Move bottom controls down by vertical change
	filterCheckBox.Move (0, vGrow);
	replaceLabel.Move (0, vGrow);
	replacementPopUp.Move (0, vGrow);
	importXMLButton.Move (hGrow, vGrow);
	applyButton.Move (hGrow, vGrow);
	cancelButton.Move (hGrow, vGrow);
	separator.Move (0, vGrow);
	separator.Resize (hGrow, 0);
	statusText.Move (0, vGrow);

	EndMoveResizeItems ();
}

void LineTypeCleaningDialog::ButtonClicked (const DG::ButtonClickEvent& ev)
{
	if (ev.GetSource () == &applyButton) {
		ApplyReplacement ();
	} else if (ev.GetSource () == &cancelButton) {
		PostCloseRequest (DG::ModalDialog::Cancel);
	} else if (ev.GetSource () == &importXMLButton) {
		ImportXMLFile ();
	}
}

void LineTypeCleaningDialog::CheckItemChanged (const DG::CheckItemChangeEvent& ev)
{
	if (ev.GetSource () == &filterCheckBox) {
		showOnlyNonStandard = filterCheckBox.IsChecked ();
		RefreshList ();
	}
}

void LineTypeCleaningDialog::PopUpChanged (const DG::PopUpChangeEvent& ev)
{
	if (ev.GetSource () == &replacementPopUp) {
		short selectedPopupItem = replacementPopUp.GetSelectedItem ();
		if (selectedPopupItem > 0 && !allowedLineTypesList.empty ()) {
			DGUserData targetValue = replacementPopUp.GetItemValue (selectedPopupItem);
			API_AttributeIndex targetIndex = ACAPI_CreateAttributeIndex (static_cast<Int32>(targetValue));

			// Apply to ALL selected NON-STD rows
			GS::Array<short> selectedRows = lineTypeList.GetSelectedItems ();
			Int32 updatedCount = 0;

			for (short row : selectedRows) {
				GS::UniString status = lineTypeList.GetTabItemText (row, ColStatus);
				if (status == "NON-STD") {
					UpdateRowReplacement (row, targetIndex);
					updatedCount++;
				}
			}

			std::ostringstream oss;
			oss << "PopUpChanged: updated " << updatedCount << " rows to target " << targetIndex.ToInt32_Deprecated ();
			LogLineTypeCleanerMessage (oss.str ());
		}
	}
}

void LineTypeCleaningDialog::ListBoxSelectionChanged (const DG::ListBoxSelectionEvent& ev)
{
	if (ev.GetSource () == &lineTypeList) {
		// Get all selected items
		GS::Array<short> selectedRows = lineTypeList.GetSelectedItems ();

		// Find the first selected NON-STD row and count
		short firstNonStdRow = -1;
		Int32 nonStdCount = 0;

		for (short row : selectedRows) {
			GS::UniString status = lineTypeList.GetTabItemText (row, ColStatus);
			if (status == "NON-STD") {
				nonStdCount++;
				if (firstNonStdRow < 0) {
					firstNonStdRow = row;
				}
			}
		}

		// Update status text with selection count
		if (nonStdCount > 0) {
			GS::UniString statusStr;
			statusStr.Printf ("%d non-standard selected - use dropdown to set replacement:", nonStdCount);
			statusText.SetText (statusStr);

			// Enable popup when NON-STD rows are selected
			if (!allowedLineTypesList.empty ()) {
				// Set popup selection based on first selected row's replacement
				DGUserData sourceValue = lineTypeList.GetItemValue (firstNonStdRow);
				Int32 sourceIdx = static_cast<Int32>(sourceValue);

				auto it = replacementMap.find (sourceIdx);
				if (it != replacementMap.end ()) {
					Int32 targetIdx = it->second;
					for (short i = 1; i <= replacementPopUp.GetItemCount (); i++) {
						DGUserData itemValue = replacementPopUp.GetItemValue (i);
						if (static_cast<Int32>(itemValue) == targetIdx) {
							replacementPopUp.SelectItem (i);
							break;
						}
					}
				} else if (replacementPopUp.GetItemCount () > 0) {
					replacementPopUp.SelectItem (1);
				}

				editingRowIndex = firstNonStdRow;
				replacementPopUp.Enable ();
			}
		} else {
			UpdateStatusText ();
			replacementPopUp.Disable ();
			editingRowIndex = -1;
		}
	}
}

void LineTypeCleaningDialog::RepositionPopupAtEditingRow ()
{
	// Not used - popup is at fixed position below the list
}

void LineTypeCleaningDialog::ShowReplacementPopUpForRow (short rowIndex)
{
	(void)rowIndex;
}

void LineTypeCleaningDialog::ListBoxDoubleClicked (const DG::ListBoxDoubleClickEvent& ev)
{
	if (ev.GetSource () == &lineTypeList) {
		short clickedRow = ev.GetListItem ();
		if (clickedRow > 0) {
			GS::UniString status = lineTypeList.GetTabItemText (clickedRow, ColStatus);
			if (status == "NON-STD" && replacementPopUp.GetItemCount () > 0) {
				// Double-clicked on a NON-STD row - cycle to next replacement option
				DGUserData sourceValue = lineTypeList.GetItemValue (clickedRow);
				Int32 sourceIdx = static_cast<Int32>(sourceValue);

				// Find current replacement index
				short currentPopupItem = 1;
				auto it = replacementMap.find (sourceIdx);
				if (it != replacementMap.end ()) {
					Int32 targetIdx = it->second;
					for (short i = 1; i <= replacementPopUp.GetItemCount (); i++) {
						DGUserData itemValue = replacementPopUp.GetItemValue (i);
						if (static_cast<Int32>(itemValue) == targetIdx) {
							currentPopupItem = i;
							break;
						}
					}
				}

				// Cycle to next item
				short nextItem = currentPopupItem + 1;
				if (nextItem > replacementPopUp.GetItemCount ()) {
					nextItem = 1;
				}

				// Apply the new replacement
				DGUserData targetValue = replacementPopUp.GetItemValue (nextItem);
				API_AttributeIndex targetIndex = ACAPI_CreateAttributeIndex (static_cast<Int32>(targetValue));
				UpdateRowReplacement (clickedRow, targetIndex);

				// Update popup selection to match
				replacementPopUp.SelectItem (nextItem);

				std::ostringstream oss;
				oss << "Double-click on row " << clickedRow << " - cycled to replacement option " << nextItem;
				LogLineTypeCleanerMessage (oss.str ());
			}
		}
	}
}

void LineTypeCleaningDialog::UpdateRowReplacement (short rowIndex, API_AttributeIndex replacementIndex)
{
	// Get the source line type index
	DGUserData sourceValue = lineTypeList.GetItemValue (rowIndex);
	Int32 sourceIdx = static_cast<Int32>(sourceValue);

	// Update the map
	replacementMap[sourceIdx] = replacementIndex.ToInt32_Deprecated ();

	// Find the name of the replacement
	GS::UniString replacementName;
	for (const auto& allowed : allowedLineTypesList) {
		if (allowed.second.ToInt32_Deprecated () == replacementIndex.ToInt32_Deprecated ()) {
			replacementName = allowed.first;
			break;
		}
	}

	// Update the UI
	lineTypeList.SetTabItemText (rowIndex, ColReplacement, replacementName);

	std::ostringstream oss;
	oss << "UpdateRowReplacement: row " << rowIndex << ", source " << sourceIdx
		<< " -> target " << replacementIndex.ToInt32_Deprecated () << " (" << replacementName.ToCStr ().Get () << ")";
	LogLineTypeCleanerMessage (oss.str ());
}

void LineTypeCleaningDialog::ImportXMLFile ()
{
	LogLineTypeCleanerMessage ("ImportXMLFile called - opening file dialog...");

	// Setup file type filter for XML
	DGTypePopupItem xmlFilter;
	xmlFilter.text = "ArchiCAD Attribute XML Files (*.xml)";
	xmlFilter.extensions = "xml";
	xmlFilter.macType = 0;

	// Show open file dialog
	IO::Location selectedFile;
	bool result = DGGetOpenFile (&selectedFile, 1, &xmlFilter, nullptr,
								 GS::UniString ("Select ArchiCAD Attribute XML File"));

	if (result) {
		GS::UniString filePath;
		selectedFile.ToPath (&filePath);

		std::ostringstream oss;
		oss << "Selected XML file: " << filePath.ToCStr ().Get ();
		LogLineTypeCleanerMessage (oss.str ());

		// Load allowed line types from XML
		if (cleaner.LoadAllowedFromXML (selectedFile)) {
			LogLineTypeCleanerMessage ("XML loaded successfully");

			// Rescan project with new allowed list
			cleaner.ScanProject ();

			// Refresh UI
			PopulateReplacementPopUp ();
			PopulateLineTypes ();
			UpdateStatusText ();
		} else {
			LogLineTypeCleanerMessage ("Failed to load XML file");
			DG::WarningAlert ("Error", "Failed to load attribute XML file.\nMake sure it contains <LineType><Name>...</Name></LineType> elements.", "OK");
		}
	} else {
		LogLineTypeCleanerMessage ("File dialog cancelled");
	}
}

