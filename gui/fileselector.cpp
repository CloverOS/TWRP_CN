// FileSelector.cpp - GUIFileSelector object

#include <linux/input.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <ctype.h>

#include <algorithm>

extern "C" {
#include "../common.h"
#include "../roots.h"
#include "../minuitwrp/minui.h"
#include "../recovery_ui.h"
}

#include "rapidxml.hpp"
#include "objects.hpp"
#include "../data.hpp"

#define TW_FILESELECTOR_UP_A_LEVEL "(Up A Level)"

#define SCROLLING_SPEED_DECREMENT 6
#define SCROLLING_FLOOR 10
#define SCROLLING_MULTIPLIER 6

int GUIFileSelector::mSortOrder = 0;

GUIFileSelector::GUIFileSelector(xml_node<>* node)
{
	xml_attribute<>* attr;
	xml_node<>* child;
	int header_separator_color_specified = 0, header_separator_height_specified = 0, header_text_color_specified = 0, header_background_color_specified = 0;

	mStart = mLineSpacing = startY = mFontHeight = mSeparatorH = scrollingY = scrollingSpeed = 0;
	mIconWidth = mIconHeight = mFolderIconHeight = mFileIconHeight = mFolderIconWidth = mFileIconWidth = mHeaderIconHeight = mHeaderIconWidth = 0;
	mHeaderSeparatorH = mLineHeight = mHeaderIsStatic = mHeaderH = actualLineHeight = 0;
	mFolderIcon = mFileIcon = mBackground = mFont = mHeaderIcon = NULL;
	mBackgroundX = mBackgroundY = mBackgroundW = mBackgroundH = 0;
	mShowFolders = mShowFiles = mShowNavFolders = 1;
	mUpdate = 0;
	touchDebounce = 6;
	mPathVar = "cwd";
	ConvertStrToColor("black", &mBackgroundColor);
	ConvertStrToColor("black", &mHeaderBackgroundColor);
	ConvertStrToColor("black", &mSeparatorColor);
	ConvertStrToColor("black", &mHeaderSeparatorColor);
	ConvertStrToColor("white", &mFontColor);
	ConvertStrToColor("white", &mHeaderFontColor);

	// Load header text
	child = node->first_node("header");
	if (child)
	{
		attr = child->first_attribute("icon");
		if (attr)
			mHeaderIcon = PageManager::FindResource(attr->value());

		attr = child->first_attribute("background");
		if (attr)
		{
			std::string color = attr->value();
			ConvertStrToColor(color, &mHeaderBackgroundColor);
			header_background_color_specified = -1;
		}
		attr = child->first_attribute("textcolor");
		if (attr)
		{
			std::string color = attr->value();
			ConvertStrToColor(color, &mHeaderFontColor);
			header_text_color_specified = -1;
		}
		attr = child->first_attribute("separatorcolor");
		if (attr)
		{
			std::string color = attr->value();
			ConvertStrToColor(color, &mHeaderSeparatorColor);
			header_separator_color_specified = -1;
		}
		attr = child->first_attribute("separatorheight");
		if (attr) {
			string parsevalue = gui_parse_text(attr->value());
			mHeaderSeparatorH = atoi(parsevalue.c_str());
			header_separator_height_specified = -1;
		}
	}
	child = node->first_node("text");
	if (child)  mHeaderText = child->value();

	// Simple way to check for static state
	mLastValue = gui_parse_text(mHeaderText);
	if (mLastValue != mHeaderText)
		mHeaderIsStatic = 0;
	else
		mHeaderIsStatic = -1;

	child = node->first_node("icon");
	if (child)
	{
		attr = child->first_attribute("folder");
		if (attr)
			mFolderIcon = PageManager::FindResource(attr->value());
		attr = child->first_attribute("file");
		if (attr)
			mFileIcon = PageManager::FindResource(attr->value());
	}
	child = node->first_node("background");
	if (child)
	{
		attr = child->first_attribute("resource");
		if (attr)
			mBackground = PageManager::FindResource(attr->value());
		attr = child->first_attribute("color");
		if (attr)
		{
			std::string color = attr->value();
			ConvertStrToColor(color, &mBackgroundColor);
			if (!header_background_color_specified)
				ConvertStrToColor(color, &mHeaderBackgroundColor);
		}
	}

	// Load the placement
	LoadPlacement(node->first_node("placement"), &mRenderX, &mRenderY, &mRenderW, &mRenderH);
	SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);

	// Load the font, and possibly override the color
	child = node->first_node("font");
	if (child)
	{
		attr = child->first_attribute("resource");
		if (attr)
			mFont = PageManager::FindResource(attr->value());

		attr = child->first_attribute("color");
		if (attr)
		{
			std::string color = attr->value();
			ConvertStrToColor(color, &mFontColor);
			if (!header_text_color_specified)
				ConvertStrToColor(color, &mHeaderFontColor);
		}

		attr = child->first_attribute("spacing");
		if (attr) {
			string parsevalue = gui_parse_text(attr->value());
			mLineSpacing = atoi(parsevalue.c_str());
		}
	}

	// Load the separator if it exists
	child = node->first_node("separator");
	if (child)
	{
		attr = child->first_attribute("color");
		if (attr)
		{
			std::string color = attr->value();
			ConvertStrToColor(color, &mSeparatorColor);
			if (!header_separator_color_specified)
				ConvertStrToColor(color, &mHeaderSeparatorColor);
		}

		attr = child->first_attribute("height");
		if (attr) {
			string parsevalue = gui_parse_text(attr->value());
			mSeparatorH = atoi(parsevalue.c_str());
			if (!header_separator_height_specified)
				mHeaderSeparatorH = mSeparatorH;
		}
	}

	child = node->first_node("filter");
	if (child)
	{
		attr = child->first_attribute("extn");
		if (attr)
			mExtn = attr->value();
		attr = child->first_attribute("folders");
		if (attr)
			mShowFolders = atoi(attr->value());
		attr = child->first_attribute("files");
		if (attr)
			mShowFiles = atoi(attr->value());
		attr = child->first_attribute("nav");
		if (attr)
			mShowNavFolders = atoi(attr->value());
	}

	// Handle the path variable
	child = node->first_node("path");
	if (child)
	{
		attr = child->first_attribute("name");
		if (attr)
			mPathVar = attr->value();
		attr = child->first_attribute("default");
		if (attr)
			DataManager::SetValue(mPathVar, attr->value());
	}

	// Handle the result variable
	child = node->first_node("data");
	if (child)
	{
		attr = child->first_attribute("name");
		if (attr)
			mVariable = attr->value();
		attr = child->first_attribute("default");
		if (attr)
			DataManager::SetValue(mVariable, attr->value());
	}

	// Handle the sort variable
	child = node->first_node("sort");
	if (child)
	{
		attr = child->first_attribute("name");
		if (attr)
			mSortVariable = attr->value();
		attr = child->first_attribute("default");
		if (attr)
			DataManager::SetValue(mSortVariable, attr->value());

		DataManager::GetValue(mSortVariable, mSortOrder);
	}

	// Handle the selection variable
	child = node->first_node("selection");
	if (child)
	{
		attr = child->first_attribute("name");
		if (attr)
			mSelection = attr->value();
		else
			mSelection = "0";
	} else
		mSelection = "0";

	// Retrieve the line height
	gr_getFontDetails(mFont ? mFont->GetResource() : NULL, &mFontHeight, NULL);
	mLineHeight = mFontHeight;
	mHeaderH = mFontHeight;

	if (mFolderIcon && mFolderIcon->GetResource())
	{
		mFolderIconWidth = gr_get_width(mFolderIcon->GetResource());
		mFolderIconHeight = gr_get_height(mFolderIcon->GetResource());
		if (mFolderIconHeight > (int)mLineHeight)
			mLineHeight = mFolderIconHeight;
		mIconWidth = mFolderIconWidth;
	}

	if (mFileIcon && mFileIcon->GetResource())
	{
		mFileIconWidth = gr_get_width(mFileIcon->GetResource());
		mFileIconHeight = gr_get_height(mFileIcon->GetResource());
		if (mFileIconHeight > (int)mLineHeight)
			mLineHeight = mFileIconHeight;
		if (mFileIconWidth > mIconWidth)
			mIconWidth = mFileIconWidth;
	}

	if (mHeaderIcon && mHeaderIcon->GetResource())
	{
		mHeaderIconWidth = gr_get_width(mHeaderIcon->GetResource());
		mHeaderIconHeight = gr_get_height(mHeaderIcon->GetResource());
		if (mHeaderIconHeight > mHeaderH)
			mHeaderH = mHeaderIconHeight;
		if (mHeaderIconWidth > mIconWidth)
			mIconWidth = mHeaderIconWidth;
	}

	mHeaderH += mLineSpacing + mHeaderSeparatorH;
	actualLineHeight = mLineHeight + mLineSpacing + mSeparatorH;
	if (mHeaderH < actualLineHeight)
		mHeaderH = actualLineHeight;

	if (actualLineHeight / 3 > 6)
		touchDebounce = actualLineHeight / 3;

	if (mBackground && mBackground->GetResource())
	{
		mBackgroundW = gr_get_width(mBackground->GetResource());
		mBackgroundH = gr_get_height(mBackground->GetResource());
	}

	// Fetch the file/folder list
	std::string value;
	DataManager::GetValue(mPathVar, value);
	if (GetFileList(value) != 0 && (mShowNavFolders != 0 || mShowFiles != 0)) {
		GetFileList(DataManager::GetCurrentStoragePath());
		DataManager::SetValue(mPathVar, DataManager::GetCurrentStoragePath());
	}
}

GUIFileSelector::~GUIFileSelector()
{
}

int GUIFileSelector::Render(void)
{
	// First step, fill background
	gr_color(mBackgroundColor.red, mBackgroundColor.green, mBackgroundColor.blue, 255);
	gr_fill(mRenderX, mRenderY + mHeaderH, mRenderW, mRenderH - mHeaderH);

	// Next, render the background resource (if it exists)
	if (mBackground && mBackground->GetResource())
	{
		mBackgroundX = mRenderX + ((mRenderW - mBackgroundW) / 2);
		mBackgroundY = mRenderY + ((mRenderH - mBackgroundH) / 2);
		gr_blit(mBackground->GetResource(), 0, 0, mBackgroundW, mBackgroundH, mBackgroundX, mBackgroundY);
	}

	// This tells us how many lines we can actually render
	int lines = (mRenderH - mHeaderH) / (actualLineHeight);
	int line;

	int folderSize = mShowFolders ? mFolderList.size() : 0;
	int fileSize = mShowFiles ? mFileList.size() : 0;

	if (folderSize + fileSize < lines) {
		lines = folderSize + fileSize;
		scrollingY = 0;
	} else {
		lines++;
		if (lines < folderSize + fileSize)
			lines++;
	}

	void* fontResource = NULL;
	if (mFont)  fontResource = mFont->GetResource();

	int yPos = mRenderY + mHeaderH + scrollingY;
	int fontOffsetY = (int)((actualLineHeight - mFontHeight) / 2);
	int currentIconHeight = 0, currentIconWidth = 0;
	int currentIconOffsetY = 0, currentIconOffsetX = 0;
	int folderIconOffsetY = (int)((actualLineHeight - mFolderIconHeight) / 2), fileIconOffsetY = (int)((actualLineHeight - mFileIconHeight) / 2);
	int folderIconOffsetX = (mIconWidth - mFolderIconWidth) / 2, fileIconOffsetX = (mIconWidth - mFileIconWidth) / 2;

	for (line = 0; line < lines; line++)
	{
		Resource* icon;
		std::string label;

		// Set the color for the font
		gr_color(mFontColor.red, mFontColor.green, mFontColor.blue, 255);

		if (line + mStart < folderSize)
		{
			icon = mFolderIcon;
			label = mFolderList.at(line + mStart).fileName;
			currentIconHeight = mFolderIconHeight;
			currentIconWidth = mFolderIconWidth;
			currentIconOffsetY = folderIconOffsetY;
			currentIconOffsetX = folderIconOffsetX;
		}
		else if (line + mStart < folderSize + fileSize)
		{
			icon = mFileIcon;
			label = mFileList.at((line + mStart) - folderSize).fileName;
			currentIconHeight = mFileIconHeight;
			currentIconWidth = mFileIconWidth;
			currentIconOffsetY = fileIconOffsetY;
			currentIconOffsetX = fileIconOffsetX;
		} else {
			continue;
		}

		if (icon && icon->GetResource())
		{
			int rect_y = 0, image_y = (yPos + currentIconOffsetY);
			if (image_y + currentIconHeight > mRenderY + mRenderH)
				rect_y = mRenderY + mRenderH - image_y;
			else
				rect_y = currentIconHeight;
			gr_blit(icon->GetResource(), 0, 0, currentIconWidth, rect_y, mRenderX + currentIconOffsetX, image_y);
		}
		gr_textExWH(mRenderX + mIconWidth + 5, yPos + fontOffsetY, label.c_str(), fontResource, mRenderX + mRenderW, mRenderY + mRenderH);

		// Add the separator
		if (yPos + actualLineHeight < mRenderH + mRenderY) {
			gr_color(mSeparatorColor.red, mSeparatorColor.green, mSeparatorColor.blue, 255);
			gr_fill(mRenderX, yPos + actualLineHeight - mSeparatorH, mRenderW, mSeparatorH);
		}

		// Move the yPos
		yPos += actualLineHeight;
	}

	// Render the Header (last so that it overwrites the top most row for per pixel scrolling)
	// First step, fill background
	gr_color(mHeaderBackgroundColor.red, mHeaderBackgroundColor.green, mHeaderBackgroundColor.blue, 255);
	gr_fill(mRenderX, mRenderY, mRenderW, mHeaderH);

	// Now, we need the header (icon + text)
	yPos = mRenderY;
	{
		Resource* headerIcon;
		int mIconOffsetX = 0;

		// render the icon if it exists
		headerIcon = mHeaderIcon;
		if (headerIcon && headerIcon->GetResource())
		{
			gr_blit(headerIcon->GetResource(), 0, 0, mHeaderIconWidth, mHeaderIconHeight, mRenderX + ((mHeaderIconWidth - mIconWidth) / 2), (yPos + (int)((mHeaderH - mHeaderIconHeight) / 2)));
			mIconOffsetX = mIconWidth;
		}

		// render the text
		gr_color(mHeaderFontColor.red, mHeaderFontColor.green, mHeaderFontColor.blue, 255);
		gr_textExWH(mRenderX + mIconOffsetX + 5, yPos + (int)((mHeaderH - mFontHeight) / 2), mLastValue.c_str(), fontResource, mRenderX + mRenderW, mRenderY + mRenderH);

		// Add the separator
		gr_color(mHeaderSeparatorColor.red, mHeaderSeparatorColor.green, mHeaderSeparatorColor.blue, 255);
		gr_fill(mRenderX, yPos + mHeaderH - mHeaderSeparatorH, mRenderW, mHeaderSeparatorH);
	}

	mUpdate = 0;
	return 0;
}

int GUIFileSelector::Update(void)
{
	if (!mHeaderIsStatic) {
		std::string newValue = gui_parse_text(mHeaderText);
		if (mLastValue != newValue) {
			mLastValue = newValue;
			mUpdate = 1;
		}
	}

	if (mUpdate)
	{
		mUpdate = 0;
		if (Render() == 0)
			return 2;
	}

	// Handle kinetic scrolling
	if (scrollingSpeed == 0) {
		// Do nothing
	} else if (scrollingSpeed > 0) {
		if (scrollingSpeed < ((int) (actualLineHeight * 2.5))) {
			scrollingY += scrollingSpeed;
			scrollingSpeed -= SCROLLING_SPEED_DECREMENT;
		} else {
			scrollingY += ((int) (actualLineHeight * 2.5));
			scrollingSpeed -= SCROLLING_SPEED_DECREMENT;
		}
		while (mStart && scrollingY > 0) {
			mStart--;
			scrollingY -= actualLineHeight;
		}
		if (mStart == 0 && scrollingY > 0) {
			scrollingY = 0;
			scrollingSpeed = 0;
		} else if (scrollingSpeed < SCROLLING_FLOOR)
			scrollingSpeed = 0;
		mUpdate = 1;
	} else if (scrollingSpeed < 0) {
		int totalSize = (mShowFolders ? mFolderList.size() : 0) + (mShowFiles ? mFileList.size() : 0);
		int lines = (mRenderH - mHeaderH) / (actualLineHeight);

		if (totalSize > lines) {
			int bottom_offset = ((int)(mRenderH) - mHeaderH) - (lines * actualLineHeight);

			bottom_offset -= actualLineHeight;

			if (abs(scrollingSpeed) < ((int) (actualLineHeight * 2.5))) {
				scrollingY += scrollingSpeed;
				scrollingSpeed += SCROLLING_SPEED_DECREMENT;
			} else {
				scrollingY -= ((int) (actualLineHeight * 2.5));
				scrollingSpeed += SCROLLING_SPEED_DECREMENT;
			}
			while (mStart + lines + (bottom_offset ? 1 : 0) < totalSize && abs(scrollingY) > actualLineHeight) {
				mStart++;
				scrollingY += actualLineHeight;
			}
			if (bottom_offset != 0 && mStart + lines + 1 >= totalSize && scrollingY <= bottom_offset) {
				mStart = totalSize - lines - 1;
				scrollingY = bottom_offset;
			} else if (mStart + lines >= totalSize && scrollingY < 0) {
				mStart = totalSize - lines;
				scrollingY = 0;
			} else if (scrollingSpeed * -1 < SCROLLING_FLOOR)
				scrollingSpeed = 0;
			mUpdate = 1;
		}
	}

	return 0;
}

int GUIFileSelector::GetSelection(int x, int y)
{
	// We only care about y position
	if (y < mRenderY || y - mRenderY <= mHeaderH || y - mRenderY > mRenderH) return -1;
	return (y - mRenderY - mHeaderH);
}

int GUIFileSelector::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	static int startSelection = -1;
	static int lastY = 0, last2Y = 0;
	int selection = 0;

	switch (state)
	{
	case TOUCH_START:
		if (scrollingSpeed != 0)
			startSelection = -1;
		else
			startSelection = GetSelection(x,y);
		startY = lastY = last2Y = y;
		scrollingSpeed = 0;
		break;

	case TOUCH_DRAG:
		// Check if we dragged out of the selection window
		if (GetSelection(x, y) == -1) {
			last2Y = lastY = 0;
			break;
		}

		// Provide some debounce on initial touches
		if (startSelection != -1 && abs(y - startY) < touchDebounce) {
			break;
		}

		last2Y = lastY;
		lastY = y;	
		startSelection = -1;

		// Handle scrolling
		scrollingY += y - startY;
		startY = y;
		while(mStart && scrollingY > 0) {
			mStart--;
			scrollingY -= actualLineHeight;
		}
		if (mStart == 0 && scrollingY > 0)
			scrollingY = 0;
		{
			int totalSize = (mShowFolders ? mFolderList.size() : 0) + (mShowFiles ? mFileList.size() : 0);
			int lines = (mRenderH - mHeaderH) / (actualLineHeight);

			if (totalSize > lines) {
				int bottom_offset = ((int)(mRenderH) - mHeaderH) - (lines * actualLineHeight);

				bottom_offset -= actualLineHeight;

				while (mStart + lines + (bottom_offset ? 1 : 0) < totalSize && abs(scrollingY) > actualLineHeight) {
					mStart++;
					scrollingY += actualLineHeight;
				}
				if (bottom_offset != 0 && mStart + lines + 1 >= totalSize && scrollingY <= bottom_offset) {
					mStart = totalSize - lines - 1;
					scrollingY = bottom_offset;
				} else if (mStart + lines >= totalSize && scrollingY < 0) {
					mStart = totalSize - lines;
					scrollingY = 0;
				}
			} else
				scrollingY = 0;
		}
		mUpdate = 1;
		break;

	case TOUCH_RELEASE:
		if (startSelection >= 0)
		{
			// We've selected an item!
			std::string str;

			int folderSize = mShowFolders ? mFolderList.size() : 0;
			int fileSize = mShowFiles ? mFileList.size() : 0;
			int selectY = scrollingY, actualSelection = mStart;

			// Move the selection to the proper place in the array
			while (selectY + actualLineHeight < startSelection) {
				selectY += actualLineHeight;
				actualSelection++;
			}
			startSelection = actualSelection;

			if (startSelection < folderSize + fileSize)
			{
				if (startSelection < folderSize)
				{
					std::string oldcwd;
					std::string cwd;

					str = mFolderList.at(startSelection).fileName;
					if (mSelection != "0")
						DataManager::SetValue(mSelection, str);
					DataManager::GetValue(mPathVar, cwd);

					oldcwd = cwd;
					// Ignore requests to do nothing
					if (str == ".")	 return 0;
					if (str == TW_FILESELECTOR_UP_A_LEVEL)
					{
						if (cwd != "/")
						{
							size_t found;
							found = cwd.find_last_of('/');
							cwd = cwd.substr(0,found);

							if (cwd.length() < 2)   cwd = "/";
						}
					}
					else
					{
						// Add a slash if we're not the root folder
						if (cwd != "/")	 cwd += "/";
						cwd += str;
					}

					if (mShowNavFolders == 0 && mShowFiles == 0)
					{
						// This is a "folder" selection
						DataManager::SetValue(mVariable, cwd);
					}
					else
					{
						DataManager::SetValue(mPathVar, cwd);
						if (GetFileList(cwd) != 0)
						{
							LOGE("Unable to change folders.\n");
							DataManager::SetValue(mPathVar, oldcwd);
							GetFileList(oldcwd);
						}
						mStart = 0;
						scrollingY = 0;
						mUpdate = 1;
					}
				}
				else if (!mVariable.empty())
				{
					str = mFileList.at(startSelection - folderSize).fileName;
					if (mSelection != "0")
						DataManager::SetValue(mSelection, str);

					std::string cwd;
					DataManager::GetValue(mPathVar, cwd);
					if (cwd != "/")	 cwd += "/";
					DataManager::SetValue(mVariable, cwd + str);
				}
			}
		} else {
			// This is for kinetic scrolling
			scrollingSpeed = lastY - last2Y;
			if (abs(scrollingSpeed) > SCROLLING_FLOOR)
				scrollingSpeed *= SCROLLING_MULTIPLIER;
			else
				scrollingSpeed = 0;
		}
	case TOUCH_REPEAT:
	case TOUCH_HOLD:
		break;
	}
	return 0;
}

int GUIFileSelector::NotifyVarChange(std::string varName, std::string value)
{
	if (varName.empty())
	{
		// Always clear the data variable so we know to use it
		DataManager::SetValue(mVariable, "");
	}
	if (!mHeaderIsStatic) {
		std::string newValue = gui_parse_text(mHeaderText);
		if (mLastValue != newValue) {
			mLastValue = newValue;
			mStart = 0;
			scrollingY = 0;
			scrollingSpeed = 0;
			mUpdate = 1;
		}
	}
	if (varName == mPathVar || varName == mSortVariable)
	{
		DataManager::GetValue(mPathVar, value);  // sometimes the value will be the sort order instead of the path, so we read the path everytime
		DataManager::GetValue(mSortVariable, mSortOrder);
		mStart = 0;
		scrollingY = 0;
		scrollingSpeed = 0;
		GetFileList(value);
		mUpdate = 1;
		return 0;
	}
	return 0;
}

int GUIFileSelector::SetRenderPos(int x, int y, int w /* = 0 */, int h /* = 0 */)
{
	mRenderX = x;
	mRenderY = y;
	if (w || h)
	{
		mRenderW = w;
		mRenderH = h;
	}
	SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);
	mUpdate = 1;
	return 0;
}

bool GUIFileSelector::fileSort(FileData d1, FileData d2)
{
	if (d1.fileName == ".")
		return -1;
	if (d2.fileName == ".")
		return 0;
	if (d1.fileName == TW_FILESELECTOR_UP_A_LEVEL)
		return -1;
	if (d2.fileName == TW_FILESELECTOR_UP_A_LEVEL)
		return 0;
	
	switch (mSortOrder) {
		case 3: // by size largest first
			if (d1.fileSize == d2.fileSize || d1.fileType == DT_DIR) // some directories report a different size than others - but this is not the size of the files inside the directory, so we just sort by name on directories
				return (strcasecmp(d1.fileName.c_str(), d2.fileName.c_str()) < 0);
			return d1.fileSize > d2.fileSize;
		case -3: // by size smallest first
			if (d1.fileSize == d2.fileSize || d1.fileType == DT_DIR) // some directories report a different size than others - but this is not the size of the files inside the directory, so we just sort by name on directories
				return (strcasecmp(d1.fileName.c_str(), d2.fileName.c_str()) > 0);
			return d1.fileSize < d2.fileSize;
		case 2: // by last modified date newest first
			if (d1.lastModified == d2.lastModified)
				return (strcasecmp(d1.fileName.c_str(), d2.fileName.c_str()) < 0);
			return d1.lastModified > d2.lastModified;
		case -2: // by date oldest first
			if (d1.lastModified == d2.lastModified)
				return (strcasecmp(d1.fileName.c_str(), d2.fileName.c_str()) > 0);
			return d1.lastModified < d2.lastModified;
		case -1: // by name descending
			return (strcasecmp(d1.fileName.c_str(), d2.fileName.c_str()) > 0);
		default: // should be a 1 - sort by name ascending
			return (strcasecmp(d1.fileName.c_str(), d2.fileName.c_str()) < 0);
	}
}

int GUIFileSelector::GetFileList(const std::string folder)
{
	DIR* d;
	struct dirent* de;
	struct stat st;

	// Clear all data
	mFolderList.clear();
	mFileList.clear();

	d = opendir(folder.c_str());
	if (d == NULL)
	{
		LOGI("Unable to open '%s'\n", folder.c_str());
		return -1;
	}

	while ((de = readdir(d)) != NULL)
	{
		FileData data;

		data.fileName = de->d_name;
		if (data.fileName == ".")
			continue;
		if (data.fileName == ".." && folder == "/")
			continue;
		if (data.fileName == "..")
			data.fileName = TW_FILESELECTOR_UP_A_LEVEL;
		data.fileType = de->d_type;

		std::string path = folder + "/" + data.fileName;
		stat(path.c_str(), &st);
		data.protection = st.st_mode;
		data.userId = st.st_uid;
		data.groupId = st.st_gid;
		data.fileSize = st.st_size;
		data.lastAccess = st.st_atime;
		data.lastModified = st.st_mtime;
		data.lastStatChange = st.st_ctime;

		if (data.fileType == DT_DIR)
		{
			if (mShowNavFolders || (data.fileName != "." && data.fileName != TW_FILESELECTOR_UP_A_LEVEL))
				mFolderList.push_back(data);
		}
		else if (data.fileType == DT_REG || data.fileType == DT_LNK || data.fileType == DT_BLK)
		{
			if (mExtn.empty() || (data.fileName.length() > mExtn.length() && data.fileName.substr(data.fileName.length() - mExtn.length()) == mExtn))
			{
				mFileList.push_back(data);
			}
		}
	}
	closedir(d);

	std::sort(mFolderList.begin(), mFolderList.end(), fileSort);
	std::sort(mFileList.begin(), mFileList.end(), fileSort);
	return 0;
}

void GUIFileSelector::SetPageFocus(int inFocus)
{
	if (inFocus)
	{
		std::string value;
		DataManager::GetValue(mPathVar, value);
		if (GetFileList(value) != 0 && (mShowNavFolders != 0 || mShowFiles != 0)) {
			GetFileList(DataManager::GetCurrentStoragePath());
			DataManager::SetValue(mPathVar, DataManager::GetCurrentStoragePath());
		}
	}
}

