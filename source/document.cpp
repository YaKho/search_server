#include "document.h"

using namespace std;

ostream& operator<<(ostream& out, Document document) {
	cout << "{ "s << "document_id = "s << document.id
				<< ", relevance = "s << document.relevance
				<< ", rating = "s << document.rating << " }"s;

	return out;
}
