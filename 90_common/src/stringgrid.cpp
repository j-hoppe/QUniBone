#include <stdio.h>
#include <algorithm>

using namespace std;

#include "stringgrid.hpp"

stringgrid_c::stringgrid_c() {
	clear();
}

stringgrid_c::~stringgrid_c() {
}

void stringgrid_c::clear() {
	grid.clear();
	col_count = 0;
	row_count = 0;
}

void stringgrid_c::set(unsigned col, unsigned row, string s) {
	stringgrid_index_c idx(col, row);
	col_count = max(col_count, col + 1);
	row_count = max(row_count, row + 1);
	// inserts new values, changes old values
	grid[idx] = s;
}

// map [] fills empty cells with empty string
string *stringgrid_c::get(unsigned col, unsigned row) {
	stringgrid_index_c idx(col, row);
	return &grid[idx];
}

void stringgrid_c::calc_columnwidths(void) {
	unsigned c, r;
	columm_widths.clear();
	for (c = 0; c < col_count; c++) {
		unsigned width = 0;
		for (r = 0; r < row_count; r++) {
			string *s = get(c, r);
			width = max(width, (unsigned)s->length());
		}
		columm_widths.push_back(width);
	}
}

// colsep: horiconmtal separator between columns
// titel_Sep: char for horicontal lines, to be printed after
// column titles in row 0
void stringgrid_c::print_row(unsigned r, string colsep) {
	unsigned c;
	for (c = 0; c < col_count; c++) {
		if (c > 0)
			printf("%s", colsep.c_str());
		printf("%-*s", columm_widths[c], get(c, r)->c_str());
	}
	printf("\n");
}

void stringgrid_c::print(string colsep, char titlesep) {
	unsigned c, r;
	calc_columnwidths();

	print_row(0, colsep);

	if (titlesep) {
		for (c = 0; c < col_count; c++) {
			if (c > 0)
				printf("%s", colsep.c_str());
			printf("%s", string(columm_widths[c], titlesep).c_str());
		}
		printf("\n");

	}
	for (r = 1; r < row_count; r++)
		print_row(r, colsep);

}

