#include <iostream>
#include <vector>
#include <set>
#include <string>

#include "remove_duplicates.h"

using namespace std;

void RemoveDuplicates(SearchServer& search_server) {
	set<set<string>> document_to_words;
	vector<int> documents_to_remove;

	for (const int id : search_server) {
		set<string> words;
		for (const auto& [word, idf] : search_server.GetWordFrequencies(id)) {
			words.insert(word);
		}
		if (document_to_words.count(words)) {
			documents_to_remove.push_back(id);
		} else {
			document_to_words.insert(words);
		}
	}

	for (int id : documents_to_remove) {
		search_server.RemoveDocument(id);
		cout << "Found duplicate document id "s << id << endl;
	}
}
