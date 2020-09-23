/* fills and prints an array of strings
 * each string has col,row
 */
#ifndef _STRINGRID_HPP_
#define _STRINGRID_HPP_

#include <utility>
#include <map>
#include <vector>
#include <string>
using namespace std;

// a col,row index pair
// first =col, second = row
typedef pair<unsigned, unsigned> stringgrid_index_c;

class stringgrid_c {
private:
	map<stringgrid_index_c, string> grid;

public:
	unsigned col_count;
	unsigned row_count;
	stringgrid_c();
	~stringgrid_c();
	void clear();
	void set(unsigned col, unsigned row, string s);
	string *get(unsigned col, unsigned row);

	vector<unsigned> columm_widths;
	void calc_columnwidths(void);
	void print_row(unsigned r, string colsep);
	void print(string colsep, char titlesep);
};

#endif
