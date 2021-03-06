/* xoreos-tools - Tools to help with xoreos development
 *
 * xoreos-tools is the legal property of its developers, whose names
 * can be found in the AUTHORS file distributed with this source
 * distribution.
 *
 * xoreos-tools is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * xoreos-tools is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with xoreos-tools. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file
 *  Fix broken, non-standard NWN2 XML files.
 */

#include <iostream>
#include <string>
#include <fstream>
#include <algorithm>
#include <stdio.h>
#include <ctype.h>

#include "src/common/ustring.h"
#include "src/common/encoding.h"
#include "src/common/memreadstream.h"
#include "src/common/memwritestream.h"
#include "src/common/error.h"
#include "src/common/util.h"
#include "src/aurora/xmlfixer.h"

const Common::Encoding encoding = Common::kEncodingLatin9; // Encoding format for reading NWN2 XML

namespace Aurora {

/**
 * This filter converts the contents of an NWN2 XML data stream 'in'
 * into standardized XML and returned the result as a new data stream.
 */
Common::SeekableReadStream *XMLFixer::fixXMLStream(Common::SeekableReadStream &in) {
	Common::MemoryWriteStreamDynamic out(true, in.size());
	XMLFixer fixer;

	// Initialization
	int buttonCount = 0;

	try {
		ElementList elements;

		// Set to the stream start
		in.seek(0);

		// Check for a valid header
		if (!fixer.isValidXMLHeader(in))
			throw Common::Exception("Input stream does not have an XML header");

		// Convert input stream to a list of elements
		fixer.readXMLStream(in, &elements);

		// Write a standard header
		out.writeString("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
		out.writeString("<Root>\n");

		// Fix each element and write to the output stream
		for (ElementList::iterator it = elements.begin(); it != elements.end(); ++it) {
			// Fix the element
			Common::UString line = fixer.fixXMLElement(*it);

			// Check for a misplaced UIButton close tag
			// An example is levelup_bfeats.xml
			if (fixer.isBadUIButtonRange(line, &buttonCount)) {
				// Comment out the line and update the count
				line = "<!-- " + line + "-->";
				buttonCount++;
			}

			// Write to output stream with an end of line marker
			out.writeString(line + "\n");
		}

		// Close the root element
		out.writeString("</Root>\n");

	} catch (Common::Exception &e) {
		e.add("Failed to fix XML stream");
		throw e;
	}

	// Return the converted stream
	out.setDisposable(false);
	return new Common::MemoryReadStream(out.getData(), out.size(), true);
}

/**
 * Bring the element into a valid XML form
 */
Common::UString XMLFixer::fixXMLElement(const Common::UString element) {
	Common::UString line = element, name = "", value = "";
	SegmentList segments;

	// Split on the equals sign
	line.split(line, (uint32)'=', segments);

	// Cycle through the segments
	line = "";
	for (SegmentList::iterator it1 = segments.begin(); it1 != segments.end(); ++it1) {
		// Initialization
		Common::UString segment = *it1;
		name = "";
		value = *it1;

		// Correct for " = " in playermenu_popup.xml
		segment.trim();

		// Find the last white space character
		Common::UString::iterator it2 = segment.end();
		do {
			--it2;
			if (segment.isSpace(*it2)) {
				segment.split(it2, value, name, true);
				break;
			}
		} while (it2 != segment.begin());

		// Trim both parts
		name.trim();
		value.trim();

		// Reassemble the line
		if (line.size() == 0) {
			// First segment should have the element type
			if (name.size() > 0) {
				if (value.size() > 0)
					line = value + " " + name;
				else
					line = name;
			} else {
				line = value;
			}
		} else {
			// Fix the value segment
			value = fixXMLValue(value);

			// Subsequent segment
			if (name.size() > 0)
				line += "=" + value + " " + name;
			else
				line += "=" + value;
		}
	}

	return line;
}

/*
 * In internetfilters.xml, there is an extraneos UIButton
 * close tag that causes the XML file to fail validation.
 * This function counts UIButton open (+1) and close (-1)
 * tags, then returns false if the count turns negative.
 *
 * Cases:
 *
 * <UIButton ... />	_button_count += 0
 * <UIButton ... >	_button_count += 1
 * ... </UIButton>	_button_count -= 1
 */
bool XMLFixer::isBadUIButtonRange(const Common::UString line, int *buttonCount) {
	Common::UString::iterator it;
	const Common::UString sKey = "<UIButton";
	const Common::UString eKey = "</UIButton>";
	const Common::UString eTag = "/>";
	Common::UString text, subtext;
	size_t sKeyLen = sKey.size();
	size_t eKeyLen = eKey.size();
	size_t eTagLen = eTag.size();
	size_t len;

	// Initialization
	text = line;
	len = text.size();

	// Check for a starting UIButton
	it = text.getPosition(sKeyLen);
	subtext = text.substr(text.begin(), it);
	if (subtext == sKey) {
		// Open UIButton tag
		*buttonCount += 1;

		// Check for a tag close
		it = text.getPosition(len - eTagLen);
		subtext = text.substr(it, text.end());
		if (subtext == eTag) {
			// Closes the UIButton tag
			*buttonCount -= 1;
		}

	}

	// Look for a ending UIButton tag
	it = text.getPosition(len - eKeyLen);
	subtext = text.substr(it, text.end());
	if (subtext == eKey) {
		// Closing tag
		*buttonCount -= 1;
	}

	return (*buttonCount < 0);
}

/*
 * Fix the value to be valid XML
 */
Common::UString XMLFixer::fixXMLValue(const Common::UString value) {
	Common::UString line, tail;
	Common::UString::iterator it, it2;
	uint32 c;
	size_t n;

	// Initialization
	line = value;
	tail = ""; // For a closing tag

	// Strip quotes from the ends
	line = stripEndQuotes(line);

	// Handle special cases
	if (line.size() > 0 && isFixSpecialCase(&line))
		return line;

	// Extract a closing tag
	n = line.size();
	if (n > 0) {
		c = line.at(n - 1);
		if (c == '>') {
			if (n > 1 && line.at(n - 2) == '/') {
				// Ends with '/>'
				it = line.getPosition(n - 2);
				line.erase(it, line.end());
				tail = "/>";
			} else {
				// Ends with '>'
				it = line.getPosition(n - 1);
				line.erase(it, line.end());
				tail = ">";
			}
		}
	}

	// Remove extra quotes
	line = stripEndQuotes(line);

	// Bypass if line is empty
	if (line.size() > 0) {
		// Check for a new element start in this value
		splitNewElement(&line, &tail);

		// Check for a function
		it = line.findFirst('(');
		if (it != line.end()) {
			// Split on the '('
			Common::UString function, params;
			line.split(it, function, params, true);

			// Fix the parameters
			params = fixParams(params);
			line = function + '(' + params + ')';
		}
	}

	// Add quotes back to both ends
	return "\"" + line + "\"" + tail;
}

/*
 * Search the value for the start of a new element.
 * If found, move that part of the text into the tail.
 */
void XMLFixer::splitNewElement(Common::UString *value, Common::UString *tail) {
	Common::UString::iterator it1, it2;
	Common::UString line = *value;
	size_t n;
	uint32 c;

	// Cycle through the string
	for (it1 = line.begin(); it1 != line.end(); ++it1) {
		// Look for a potential end tag
		n = line.getPosition(it1);
		c = line.at(n);
		if (c == '>') {
			// Search forward for a start tag
			it2 = it1;
			++it2; // Move forward one character
			while (it2 != line.end()) {
				n = line.getPosition(it2);
				c = line.at(n);
				if (c == '<') {
					// Check for a '/' prior to the '>'
					n = line.getPosition(it1);
					if (n > 1) {
						// Shift the iterator back one character
						c = line.at(n - 1);
						if (c == '/')
							--it1;
					}

					// Found a new start tag, so insert it in the tail
					*tail = line.substr(it1, line.end()) + *tail;

					// Build a new value from the sub-string
					line.erase(it1, line.end());
					line = stripEndQuotes(line);
					*value = line;

					// No need to continue
					return;
				} else if (!line.isSpace(c)) {
					// Not a new element
					it1 = it2;
					break;
				}

				// Next character position
				++it2;
			}
		}
	}
}

/**
 * Fix parameters for a function call
 */
Common::UString XMLFixer::fixParams(const Common::UString params) {
	Common::UString::iterator it1;
	Common::UString line = params;
	SegmentList args;

	// Remove a trailing ')', if any
	size_t i = line.size();
	uint32 c = line.at(i - 1);
	if ( c == ')') {
		it1 = line.end();
		it1--;
		line.erase(it1);
	}

	// Remove end quotes
	line = stripEndQuotes(line);

	// Split on the commas
	line.split(line, (uint32)',', args);

	// If there is only one segment, just return it
	if (args.size() < 2) {
		if (line.size() > 0) {
			return "&quot;" + line + "&quot;";
		} else {
			// No quotes needed
			return line;
		}
	}

	// Cycle through the segments
	line = "";
	for (SegmentList::iterator it2 = args.begin(); it2 != args.end(); ++it2) {
		// Remove the end quote marks, if any
		Common::UString arg = stripEndQuotes(*it2);

		// Reassemble the line
		if (line.size() == 0) {
			line = "&quot;" + arg + "&quot;";
		} else {
			line += ",&quot;" + arg + "&quot;";
		}
	}

	return line;
}

/**
 * Address special issues found in specific NWN2 XML files
 * by looking for exact matches to the problematic value
 * strings then correcting the value on a match.
 */
bool XMLFixer::isFixSpecialCase(Common::UString *value) {
	bool isFix = false;
	const Common::UString swap[4][2] = {
		{ "truefontfamily",
		  "\"true\" fontfamily" },	// examine.xml
		{ "Character\"fontfamily",
		  "\"Character\" fontfamily" },	// multiplayer_downloadx2.xml
		{ "->", "\"-&#62;\"" },		// gamespydetails.xml
		{ ">>", "\"&#62;&#62;\"" },	// internetbrowser.xml
	};
	int rows = ARRAYSIZE(swap);

	// Loop through the array
	for (int i = 0; i < rows; i++) {
		if (*value == swap[i][0]) {
			// The strings match, so swap in the correction
			*value = swap[i][1];
			isFix = true;
			break;
		}
	}

	return isFix;
}

/**
 * Remove quote marks from either end of the line.
 */
Common::UString XMLFixer::stripEndQuotes(const Common::UString value) {
	Common::UString line = value;
	Common::UString::iterator it;
	size_t n = line.size();
	uint32 c;

	// Skip if string is empty
	if (line.size() == 0)
		return line;

	c = line.at(n - 1);
	if (c == '\"') {
		it = line.getPosition(n - 1);
		line.erase(it);
	}

	if (line.size() > 0) {
		c = line.at(0);
		if (c == '\"') {
			it = line.getPosition(0);
			line.erase(it);
		}
	}

	return line;
}

/**
 * Convert the input stream to a vector of elements.
 */
void XMLFixer::readXMLStream(Common::SeekableReadStream &in, ElementList *elements) {
	const Common::UString startComment = "<!--";
	const Common::UString endComment = "-->";
	Common::UString::iterator it1, it2;
	Common::UString line, buffer;
	bool openTag = false;
	bool priorTag  = false;
	bool inComment = false;

	// Cycle through the remaining input stream
	while (!in.eos()) {
		// Track the previous state
		priorTag = openTag;

		// Read a line of text
		line = Common::readStringLine(in, encoding);
		line.trim(); // Trim now for maximum performance benefit

		// Check for comment tags
		it1 = line.findFirst(startComment);
		it2 = line.findFirst(endComment);
		if (it1 != line.end() && it2 != line.end()) {
			// Move iterator past close tag
			size_t pos = line.getPosition(it2) + sizeof(endComment);
			it2 = line.getPosition(pos);

			// Remove appended comment
			line.erase(it1, it2);
			line.trimRight();
		} else if (inComment) {
			// End of a comment element
			if (it2 != line.end()) {
				// Move iterator past close tag
				size_t pos = line.getPosition(it2) + sizeof(endComment);
				it2 = line.getPosition(pos);

				// Remove comment
				line.erase(line.begin(), it2);
				line.trimLeft();
				inComment = false;
			} else {
				// Remove comment line
				line = "";
			}
		} else if (it1 != line.end()) {
			// Start of a comment element
			inComment = true;

			// Remove comment line
			line = "";
		}

		// Check for a non-comment end tag
		openTag = !isTagClose(line);

		/*
		 * If current element is still open, add line to buffer.
		 * Otherwise, add completed element to the vectors.
		 */
		if (openTag) {
			// This is a multi-line wrap
			if (!priorTag || buffer.size() == 0) {
				// Starting a new buffer
				buffer = line;
			} else if (line.size() > 0) {
				// Append line to the buffer with a space
				buffer += " " + line;
			}
		} else {
			// Check for a multi-line wrap
			if (buffer.size() > 0) {
				// Finish wrapping the lines
				if (line.size() > 0) {
					line = buffer + " " + line;
				} else {
					line = buffer;
				}
				buffer = "";
			}

			// Only append if line has text
			if (line.size() > 0) {
				// Append to the list
				elements->push_back(line);
			}

			// Initialize for the next line
			inComment = false;
			priorTag = false;
		}
	}
}

/**
 * Check for a valid header
 */
bool XMLFixer::isValidXMLHeader(Common::SeekableReadStream &in) {
	Common::UString line;

	// Loop until a non-blank line is found
	do {
		// Read a line
		line = Common::readStringLine(in, encoding);

		// Trim white space off the ends
		line.trim();
	} while (line.size() == 0);

	// Check for an XML header line
	Common::UString::iterator it = line.findFirst("<?xml");
	if (it == line.end())
		return false;

	// Valid header
	return true;
}

/**
 * Return true if the line ends with a closing tag
 */
bool XMLFixer::isTagClose(const Common::UString value) {
	Common::UString::iterator it1, it2;
	Common::UString line = value;

	// Skip blank lines
	if (line.size() == 0)
		return false;

	// Search for a close tag
	it1 = line.findLast('>');
	if (it1 == line.end())
		return false;

	// Search backwards for an equals, quote, or comma
	it2 = line.end();
	do {
		// Decrement the iterator
		--it2;

		// Found the close mark
		if (it1 == it2)
			return true;

		// Get the character at this position
		size_t i = line.getPosition(it2);
		uint32 c = line.at(i);

		/*
		 * Look for an indication the '>' is within
		 * the element, such as inside a quote.
		 */
		if (c == '\"' || c == '=' || c == ',')
			return false;

	} while (it2 != line.begin());

	// Fail-safe
	return true;
}

} // End of namespace AURORA
