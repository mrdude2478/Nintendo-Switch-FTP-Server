#pragma once

#include <switch.h>
#include <string>
#include <vector>

#include "gfx.h"

class console
{
public:
	console(int maxLines) {
		maxl = maxLines;
		lines.push_back("");

		// Initialize position values in constructor
		maintext_startpos = 110;
		maintext_endpos = 654;
		maintext_leftpos = 30;
	}

	int maintext_startpos;
	int maintext_endpos;
	int maintext_leftpos;

	// Add these setter methods
	void setConsoleArea(int start, int end, int left) {
		maintext_startpos = start;
		maintext_endpos = end;
		maintext_leftpos = left;
	}

	void setStartPos(int start) { maintext_startpos = start; }
	void setEndPos(int end) { maintext_endpos = end; }
	void setLeftPos(int left) { maintext_leftpos = left; }

	void out(const std::string& s)
	{
		mutexLock(&consMutex);
		lines[cLine] += s;
		mutexUnlock(&consMutex);
	}

	void nl()
	{
		mutexLock(&consMutex);
		lines.push_back("");
		if ((int)lines.size() == maxl)
		{
			lines.erase(lines.begin());
		}
		cLine = lines.size() - 1;
		mutexUnlock(&consMutex);
	}

	void draw(const font* f, int fontSize = 20, uint32_t color = 0xFFFFFFFF, int maxwidth = 1250, uint32_t hashColor = 0xFFFFFF00)
	{
		mutexLock(&consMutex);

		// Calculate available height (from top of console area to top of bottom bar)
		int consoleAreaHeight = maintext_endpos - maintext_startpos; // 648 is bottom bar position, 92 is console start

		// Calculate how many lines can fit in the available space
		int maxVisibleLines = consoleAreaHeight / fontSize;

		// Calculate vertical offset for scrolling
		int totalLines = lines.size();
		int startLine = 0;

		if (totalLines > maxVisibleLines) {
			startLine = totalLines - maxVisibleLines; // Show only the last 'maxVisibleLines' lines
		}

		// Draw only the visible lines
		for (unsigned i = startLine; i < lines.size(); i++) {
			int yPos = maintext_startpos + ((i - startLine) * fontSize); // Adjust Y position based on scroll
			drawTextWrap(lines[i].c_str(), frameBuffer, f, maintext_leftpos, yPos, fontSize, clrCreateU32(color), maxwidth, hashColor);
		}

		mutexUnlock(&consMutex);
	}

	void clearAlt() //force clear the console
	{
		mutexLock(&consMutex);  // ADD THIS LINE

		int total = (cLine + 1);
		for (int i = 0; i < total; i++)
		{
			if (cLine >= 1)
			{
				lines.erase(lines.begin());
				cLine = lines.size() - 1;
			}
		}

		// Also add this to ensure we always have at least one empty line
		if (lines.empty()) {
			lines.push_back("");
		}
		cLine = lines.size() - 1;

		mutexUnlock(&consMutex);
	}

	void clear() //force clear the console
	{
		mutexLock(&consMutex);

		lines.clear();
		lines.push_back("");  // Always keep at least one empty line
		cLine = 0;

		mutexUnlock(&consMutex);
	}

private:
	//jic
	Mutex consMutex = 0;
	std::vector<std::string> lines;
	int maxl, cLine = 0;
};